#include "WebBridgeServer.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>
#include <QtNetwork/QHostAddress>
#include <QtWebSockets/QWebSocketProtocol>

#include "QGCLoggingCategory.h"
#include "WebBridge.h"

QGC_LOGGING_CATEGORY(WebBridgeServerLog, "WebBridge.WebBridgeServer")

WebBridgeServer::WebBridgeServer(WebBridge *bridge, QObject *parent)
    : QObject(parent)
    , _bridge(bridge)
    , _server(QStringLiteral("WebBridgeServer"), QWebSocketServer::NonSecureMode, this)
{
    if (!_bridge) {
        qCWarning(WebBridgeServerLog) << "constructed with null WebBridge";
    }

    connect(&_server, &QWebSocketServer::newConnection, this, &WebBridgeServer::_onNewConnection);
    if (_bridge) {
        connect(_bridge, &WebBridge::tickReady, this, &WebBridgeServer::_onTickReady);
    }
}

WebBridgeServer::~WebBridgeServer()
{
    stop();
}

bool WebBridgeServer::start()
{
    if (_server.isListening()) {
        qCDebug(WebBridgeServerLog) << "start: already listening";
        return true;
    }

    if (!_bridge) {
        qCWarning(WebBridgeServerLog) << "start: no WebBridge bound";
        return false;
    }

    // Localhost-only bind by default per PROTOCOL.md §1 transport table security note;
    // --bridge-host lets an operator opt into a wider bind (e.g. 0.0.0.0 for LAN use).
    if (_listenAddress != QHostAddress::LocalHost && _listenAddress != QHostAddress::LocalHostIPv6) {
        qCWarning(WebBridgeServerLog) << "binding non-localhost address" << _listenAddress.toString()
                                       << "-- the bridge protocol has no transport encryption; only do this on a trusted network";
    }

    if (!_server.listen(_listenAddress, _bridge->listenPort())) {
        qCWarning(WebBridgeServerLog) << "failed to listen on" << _listenAddress.toString() << ":" << _bridge->listenPort() << _server.errorString();
        return false;
    }

    qCDebug(WebBridgeServerLog) << "listening on" << _listenAddress.toString() << ":" << _bridge->listenPort();
    return true;
}

void WebBridgeServer::stop()
{
    if (!_server.isListening() && _clients.isEmpty()) {
        return;
    }

    // Copy the key list: closing a client may synchronously trigger _onDisconnected(), which
    // would otherwise mutate _clients while we are iterating it.
    const QList<QWebSocket *> clients = _clients.keys();
    for (QWebSocket *client : clients) {
        client->close(QWebSocketProtocol::CloseCodeNormal, QStringLiteral("server stopping"));
        client->deleteLater();
    }
    _clients.clear();
    _tokenToClient.clear();

    _server.close();
    qCDebug(WebBridgeServerLog) << "stopped";
}

bool WebBridgeServer::isListening() const
{
    return _server.isListening();
}

void WebBridgeServer::broadcast(const QString &channel, int vehicleId, const QJsonObject &message)
{
    const QString key = WebBridge::streamKey(channel, vehicleId);
    for (auto it = _clients.constBegin(); it != _clients.constEnd(); ++it) {
        if (it.value().authed && it.value().subscriptions.contains(key)) {
            _sendJson(it.key(), message);
        }
    }
}

void WebBridgeServer::broadcastBinary(const QString &channel, int streamId, const QByteArray &frame)
{
    const QString key = WebBridge::streamKey(channel, streamId);
    for (auto it = _clients.constBegin(); it != _clients.constEnd(); ++it) {
        if (it.value().authed && it.value().subscriptions.contains(key)) {
            it.key()->sendBinaryMessage(frame);
        }
    }
}

void WebBridgeServer::sendToClient(quint64 clientToken, const QJsonObject &message)
{
    QWebSocket *client = _tokenToClient.value(clientToken, nullptr);
    if (!client) {
        // Client disconnected since the request was received: silent no-op (PROTOCOL.md §11.2 #4).
        qCDebug(WebBridgeServerLog) << "sendToClient: no client for token" << clientToken;
        return;
    }
    _sendJson(client, message);
}

void WebBridgeServer::reply(QWebSocket *client, const QJsonObject &message)
{
    if (_clients.contains(client)) {
        _sendJson(client, message);
    }
}

void WebBridgeServer::replyError(QWebSocket *client, const QString &code, const QString &message, bool retryable, const QString &id)
{
    if (_clients.contains(client)) {
        _sendError(client, code, message, retryable, id);
    }
}

void WebBridgeServer::cacheVideoConfig(quint8 streamId, const QJsonObject &config)
{
    if (!_bridge) {
        return;
    }

    // VideoStreamServer::configReady() emits its own "streamIndex" field (VideoStreamServer.h);
    // PROTOCOL.md §9.1 instead carries the identifier at the envelope level as "streamId", like
    // every other stream message (added below via makeStreamMessage()'s vehicleId parameter).
    // Drop the redundant field so the wire message matches the spec exactly.
    QJsonObject payload = config;
    payload.remove(QStringLiteral("streamIndex"));

    QJsonObject message = _bridge->makeStreamMessage(QStringLiteral("video"), QStringLiteral("videoConfig"), payload, static_cast<int>(streamId));
    // makeStreamMessage() names its scope parameter "vehicleId"; video streams are scoped by
    // "streamId" instead (PROTOCOL.md §9), so rename the field it wrote.
    message[QStringLiteral("streamId")] = message.take(QStringLiteral("vehicleId"));

    _cachedVideoConfig[streamId] = message;

    // Live update for clients already watching (§9.1: "again whenever SPS/PPS change
    // mid-stream"); a late subscriber instead gets this replayed in _handleSubscription().
    broadcast(QStringLiteral("video"), static_cast<int>(streamId), message);
}

void WebBridgeServer::_onNewConnection()
{
    while (_server.hasPendingConnections()) {
        QWebSocket *client = _server.nextPendingConnection();
        if (!client) {
            continue;
        }

        ClientState state;
        state.token = _nextClientToken++;
        _clients.insert(client, state);
        _tokenToClient.insert(state.token, client);
        connect(client, &QWebSocket::textMessageReceived, this, &WebBridgeServer::_onTextMessageReceived);
        connect(client, &QWebSocket::disconnected, this, &WebBridgeServer::_onDisconnected);

        qCDebug(WebBridgeServerLog) << client << "connected from" << client->peerAddress().toString() << client->peerPort() << "token" << state.token;
    }
}

void WebBridgeServer::_onDisconnected()
{
    QWebSocket *client = qobject_cast<QWebSocket *>(sender());
    if (!client) {
        return;
    }

    qCDebug(WebBridgeServerLog) << client << "disconnected";
    const auto it = _clients.constFind(client);
    if (it != _clients.constEnd()) {
        _tokenToClient.remove(it.value().token);
    }
    _clients.remove(client);
    client->deleteLater();
}

void WebBridgeServer::_onTickReady(const QJsonObject &tick)
{
    for (auto it = _clients.constBegin(); it != _clients.constEnd(); ++it) {
        if (it.value().authed) {
            _sendJson(it.key(), tick);
        }
    }
}

void WebBridgeServer::_onTextMessageReceived(const QString &message)
{
    QWebSocket *client = qobject_cast<QWebSocket *>(sender());
    if (!client || !_clients.contains(client)) {
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        // PROTOCOL.md §10 BAD_MESSAGE: "Unparseable JSON / missing required field". Connection
        // stays open (PROTOCOL.md §12 mock conformance #7).
        _sendError(client, QStringLiteral("BAD_MESSAGE"), QStringLiteral("Unparseable JSON: %1").arg(parseError.errorString()), false);
        return;
    }

    const QJsonObject obj = doc.object();
    const QString id = obj.value(QStringLiteral("id")).toString();
    const QString type = obj.value(QStringLiteral("type")).toString();
    if (type.isEmpty()) {
        _sendError(client, QStringLiteral("BAD_MESSAGE"), QStringLiteral("Missing required field: type"), false, id);
        return;
    }

    ClientState &state = _clients[client];

    if (!state.authed) {
        // PROTOCOL.md §1.1: "The first message on a connection MUST be hello. Any other first
        // message -> error AUTH_REQUIRED and the server closes the socket (WS close code 1008)."
        if (type != QStringLiteral("hello")) {
            _sendError(client, QStringLiteral("AUTH_REQUIRED"), QStringLiteral("First message must be hello"), false);
            client->close(QWebSocketProtocol::CloseCodePolicyViolated, QStringLiteral("auth required"));
            return;
        }
        _handleHello(client, obj);
        return;
    }

    if (type == QStringLiteral("hello")) {
        // Re-hello on an already-authenticated connection: treated as idempotent re-ack rather
        // than UNKNOWN_TYPE, since the protocol does not forbid it.
        _handleHello(client, obj);
    } else if (type == QStringLiteral("subscribe")) {
        _handleSubscription(client, obj, true);
    } else if (type == QStringLiteral("unsubscribe")) {
        _handleSubscription(client, obj, false);
    } else if (type == QStringLiteral("getParam") || type == QStringLiteral("setParam")) {
        emit factMessageReceived(client, obj);
    } else if (type == QStringLiteral("command")) {
        _handleCommand(client, obj);
    } else if (type == QStringLiteral("missionUpload") || type == QStringLiteral("missionDownload") || type == QStringLiteral("missionClear")) {
        _handleMission(client, obj);
    } else {
        _sendError(client, QStringLiteral("UNKNOWN_TYPE"), QStringLiteral("Unrecognized type: %1").arg(type), false, id);
    }
}

void WebBridgeServer::_handleHello(QWebSocket *client, const QJsonObject &obj)
{
    const QString id = obj.value(QStringLiteral("id")).toString();

    // PROTOCOL.md §1.1: "the field is required but its value is not validated -- the mock and
    // the M1-M5 bridge MUST accept any non-empty string."
    const QString token = obj.value(QStringLiteral("token")).toString();
    if (token.isEmpty()) {
        _sendError(client, QStringLiteral("BAD_MESSAGE"), QStringLiteral("hello requires a non-empty token"), false, id);
        return;
    }

    // PROTOCOL.md §1.1: "If protocolVersion major version is unsupported, server replies with
    // error UNSUPPORTED_VERSION and closes."
    const QString protocolVersion = obj.value(QStringLiteral("protocolVersion")).toString();
    if (!protocolVersion.isEmpty() && protocolVersion.section(QLatin1Char('.'), 0, 0) != QString(kProtocolVersion).section(QLatin1Char('.'), 0, 0)) {
        _sendError(client, QStringLiteral("UNSUPPORTED_VERSION"), QStringLiteral("Unsupported protocolVersion: %1").arg(protocolVersion), false, id);
        client->close(QWebSocketProtocol::CloseCodeProtocolError, QStringLiteral("unsupported protocol version"));
        return;
    }

    // PROTOCOL.md §1.1 / --bridge-token: when the server is configured with a token
    // (setAuthToken()), the client's token must match it exactly. Unconfigured (the v0.1
    // default) keeps accepting any non-empty token, checked above.
    if (!_authToken.isEmpty() && token != _authToken) {
        _sendError(client, QStringLiteral("AUTH_FAILED"), QStringLiteral("Token rejected"), false, id);
        client->close(QWebSocketProtocol::CloseCodePolicyViolated, QStringLiteral("auth failed"));
        return;
    }

    if (!_bridge) {
        _sendError(client, QStringLiteral("INTERNAL"), QStringLiteral("Bridge not available"), false, id);
        return;
    }

    _clients[client].authed = true;

    QJsonObject ack;
    ack[QStringLiteral("type")] = QStringLiteral("helloAck");
    ack[QStringLiteral("protocolVersion")] = QString(kProtocolVersion);
    ack[QStringLiteral("serverVersion")] = QString(kServerVersion);
    ack[QStringLiteral("serverTimeUs")] = static_cast<qint64>(_bridge->serverTimeUs());
    _sendJson(client, ack);

    qCDebug(WebBridgeServerLog) << client << "authenticated";
}

void WebBridgeServer::_handleSubscription(QWebSocket *client, const QJsonObject &obj, bool subscribe)
{
    const QString id = obj.value(QStringLiteral("id")).toString();
    const QString channel = obj.value(QStringLiteral("channel")).toString();

    if (channel.isEmpty()) {
        _sendError(client, QStringLiteral("BAD_MESSAGE"), QStringLiteral("Missing required field: channel"), false, id);
        return;
    }

    if (!_isKnownChannel(channel)) {
        _sendError(client, QStringLiteral("UNKNOWN_CHANNEL"), QStringLiteral("Unknown channel: %1").arg(channel), false, id);
        return;
    }

    // Most channels are scoped by `vehicleId`; `video` is scoped by `streamId` instead
    // (PROTOCOL.md §9). Both are folded into the same integer scope id for stream-key purposes
    // (WebBridge::streamKey()); non-scoped channels (adsb) carry neither field.
    const bool hasVehicleId = obj.contains(QStringLiteral("vehicleId"));
    const bool hasStreamId = obj.contains(QStringLiteral("streamId"));
    const int scopeId = hasVehicleId ? obj.value(QStringLiteral("vehicleId")).toInt(WebBridge::kNoVehicleId)
                       : hasStreamId ? obj.value(QStringLiteral("streamId")).toInt(WebBridge::kNoVehicleId)
                       : WebBridge::kNoVehicleId;

    const QString key = WebBridge::streamKey(channel, scopeId);
    ClientState &state = _clients[client];

    QJsonObject ack;
    ack[QStringLiteral("type")] = subscribe ? QStringLiteral("subscribeAck") : QStringLiteral("unsubscribeAck");
    if (!id.isEmpty()) {
        ack[QStringLiteral("id")] = id;
    }
    ack[QStringLiteral("channel")] = channel;
    if (hasVehicleId) {
        ack[QStringLiteral("vehicleId")] = obj.value(QStringLiteral("vehicleId"));
    }
    if (hasStreamId) {
        ack[QStringLiteral("streamId")] = obj.value(QStringLiteral("streamId"));
    }

    if (subscribe) {
        // PROTOCOL.md §2.2: subscribing is idempotent; re-subscribing re-sends the snapshot.
        // The stream's seq counter is reset by the channel implementation (via
        // WebBridge::resetSeq()) when it services snapshotRequested(), not here.
        state.subscriptions.insert(key);
        _sendJson(client, ack);
        emit snapshotRequested(channel, scopeId);

        if (channel == QStringLiteral("video")) {
            // Late subscriber: replay the last videoConfig directly to this client so it isn't
            // stuck without codec config until the next SPS/PPS change (which may be GOPs away)
            // -- see VideoStreamServer.cc's stop-gap note and cacheVideoConfig()'s doc comment.
            const auto cacheIt = _cachedVideoConfig.constFind(static_cast<quint8>(scopeId));
            if (cacheIt != _cachedVideoConfig.constEnd()) {
                QJsonObject snapshot = cacheIt.value();
                snapshot[QStringLiteral("snapshot")] = true;
                _sendJson(client, snapshot);
            }
        }

        qCDebug(WebBridgeServerLog) << client << "subscribed" << key;
    } else {
        state.subscriptions.remove(key);
        _sendJson(client, ack);
        qCDebug(WebBridgeServerLog) << client << "unsubscribed" << key;
    }
}

void WebBridgeServer::_handleCommand(QWebSocket *client, const QJsonObject &obj)
{
    const QString id = obj.value(QStringLiteral("id")).toString();

    // PROTOCOL.md §5.1 request envelope: { type: "command", id, vehicleId, action, params }.
    // CommandChannel (B7b) owns action/params semantics; this only guards envelope shape.
    if (id.isEmpty() || !obj.contains(QStringLiteral("vehicleId")) || obj.value(QStringLiteral("action")).toString().isEmpty()) {
        _sendError(client, QStringLiteral("BAD_MESSAGE"), QStringLiteral("command requires id, vehicleId, and action"), false, id);
        return;
    }

    const quint64 clientToken = _clients.value(client).token;
    emit commandReceived(clientToken, obj);
}

void WebBridgeServer::_handleMission(QWebSocket *client, const QJsonObject &obj)
{
    const QString id = obj.value(QStringLiteral("id")).toString();

    // PROTOCOL.md §7.2 request envelopes: { type: "missionUpload"|"missionDownload"|
    // "missionClear", id, vehicleId, ... }. MissionChannel owns item-schema/op semantics; this
    // only guards envelope shape (matches _handleCommand()'s division of responsibility).
    if (id.isEmpty() || !obj.contains(QStringLiteral("vehicleId"))) {
        _sendError(client, QStringLiteral("BAD_MESSAGE"), QStringLiteral("mission messages require id and vehicleId"), false, id);
        return;
    }

    const quint64 clientToken = _clients.value(client).token;
    emit missionMessageReceived(clientToken, obj);
}

void WebBridgeServer::_sendJson(QWebSocket *client, const QJsonObject &obj)
{
    if (!client) {
        return;
    }
    client->sendTextMessage(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

void WebBridgeServer::_sendError(QWebSocket *client, const QString &code, const QString &message, bool retryable, const QString &id)
{
    QJsonObject err;
    err[QStringLiteral("type")] = QStringLiteral("error");
    if (!id.isEmpty()) {
        err[QStringLiteral("id")] = id;
    }
    err[QStringLiteral("code")] = code;
    err[QStringLiteral("message")] = message;
    err[QStringLiteral("retryable")] = retryable;
    _sendJson(client, err);

    qCDebug(WebBridgeServerLog) << client << "error" << code << message;
}

bool WebBridgeServer::_isKnownChannel(const QString &channel)
{
    static const QStringList kKnownChannels = {
        QStringLiteral("telemetry"),
        QStringLiteral("mission"),
        QStringLiteral("video"),
        QStringLiteral("adsb"),
    };
    return kKnownChannels.contains(channel);
}
