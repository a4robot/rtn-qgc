#include "WebBridgeServer.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>
#include <QtNetwork/QHostAddress>

#include "QGCLoggingCategory.h"
#include "WebBridge.h"
#include "WsTransport.h"

// Exactly one of these is compiled into WebBridgeModule (src/WebBridge/CMakeLists.txt selects
// the source file based on QGC_ENABLE_QT_WEBSOCKETS); the compile definition of the same name
// tells this file which header/type to use, so this is the only place that needs to know both
// implementations exist.
#if defined(QGC_ENABLE_QT_WEBSOCKETS)
#include "QtWsTransport.h"
#else
#include "IxWsTransport.h"
#endif

QGC_LOGGING_CATEGORY(WebBridgeServerLog, "WebBridge.WebBridgeServer")

WebBridgeServer::WebBridgeServer(WebBridge *bridge, QObject *parent)
    : QObject(parent)
    , _bridge(bridge)
#if defined(QGC_ENABLE_QT_WEBSOCKETS)
    , _transport(std::make_unique<QtWsTransport>(this))
#else
    , _transport(std::make_unique<IxWsTransport>(this))
#endif
{
    if (!_bridge) {
        qCWarning(WebBridgeServerLog) << "constructed with null WebBridge";
    }

    connect(_transport.get(), &WsTransport::clientConnected, this, &WebBridgeServer::_onClientConnected);
    connect(_transport.get(), &WsTransport::textMessageReceived, this, &WebBridgeServer::_onTextMessageReceived);
    connect(_transport.get(), &WsTransport::clientDisconnected, this, &WebBridgeServer::_onClientDisconnected);
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
    if (_transport->isListening()) {
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

    if (!_transport->listen(_listenAddress, _bridge->listenPort())) {
        qCWarning(WebBridgeServerLog) << "failed to listen on" << _listenAddress.toString() << ":" << _bridge->listenPort();
        return false;
    }

    qCDebug(WebBridgeServerLog) << "listening on" << _listenAddress.toString() << ":" << _bridge->listenPort();
    return true;
}

void WebBridgeServer::stop()
{
    if (!_transport->isListening() && _clients.isEmpty()) {
        return;
    }

    _transport->stop();
    _clients.clear();

    qCDebug(WebBridgeServerLog) << "stopped";
}

bool WebBridgeServer::isListening() const
{
    return _transport->isListening();
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

void WebBridgeServer::broadcastAll(const QJsonObject &message)
{
    for (auto it = _clients.constBegin(); it != _clients.constEnd(); ++it) {
        if (it.value().authed) {
            _sendJson(it.key(), message);
        }
    }
}

void WebBridgeServer::broadcastBinary(const QString &channel, int streamId, const QByteArray &frame)
{
    const QString key = WebBridge::streamKey(channel, streamId);

    // §9.2 header layout: offset 4 is the 1-byte `flags` field, bit 0 = keyframe. A frame too
    // short to even carry the header is never a keyframe (defensive; VideoStreamServer never
    // actually emits these).
    const bool isKeyframe = frame.size() > 4 && (static_cast<quint8>(frame.at(4)) & 0x01) != 0;

    // Non-const iteration: the per-client keyframe gate (ClientState::videoPendingKeyframe) is
    // cleared here the moment a keyframe is forwarded.
    for (auto it = _clients.begin(); it != _clients.end(); ++it) {
        ClientState &state = it.value();
        if (!state.authed || !state.subscriptions.contains(key)) {
            continue;
        }
        if (state.videoPendingKeyframe.contains(key)) {
            if (!isKeyframe) {
                // §9.2: "a client joining mid-stream renders nothing until the first keyframe" --
                // withhold this frame for this client only; other already-synced clients still
                // get it via the loop's next iteration.
                continue;
            }
            state.videoPendingKeyframe.remove(key);
        }
        _transport->sendBinary(it.key(), frame);
    }
}

void WebBridgeServer::sendToClient(quint64 clientId, const QJsonObject &message)
{
    if (!_clients.contains(clientId)) {
        // Client disconnected since the request was received: silent no-op (PROTOCOL.md §11.2 #4).
        qCDebug(WebBridgeServerLog) << "sendToClient: no client" << clientId;
        return;
    }
    _sendJson(clientId, message);
}

void WebBridgeServer::reply(quint64 clientId, const QJsonObject &message)
{
    if (_clients.contains(clientId)) {
        _sendJson(clientId, message);
    }
}

void WebBridgeServer::replyError(quint64 clientId, const QString &code, const QString &message, bool retryable, const QString &id)
{
    if (_clients.contains(clientId)) {
        _sendError(clientId, code, message, retryable, id);
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

void WebBridgeServer::_onClientConnected(quint64 clientId)
{
    _clients.insert(clientId, ClientState());
    qCDebug(WebBridgeServerLog) << "client" << clientId << "connected";
}

void WebBridgeServer::_onClientDisconnected(quint64 clientId)
{
    qCDebug(WebBridgeServerLog) << "client" << clientId << "disconnected";
    _clients.remove(clientId);
}

void WebBridgeServer::_onTickReady(const QJsonObject &tick)
{
    broadcastAll(tick);
}

void WebBridgeServer::_onTextMessageReceived(quint64 clientId, const QString &message)
{
    if (!_clients.contains(clientId)) {
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        // PROTOCOL.md §10 BAD_MESSAGE: "Unparseable JSON / missing required field". Connection
        // stays open (PROTOCOL.md §12 mock conformance #7).
        _sendError(clientId, QStringLiteral("BAD_MESSAGE"), QStringLiteral("Unparseable JSON: %1").arg(parseError.errorString()), false);
        return;
    }

    const QJsonObject obj = doc.object();
    const QString id = obj.value(QStringLiteral("id")).toString();
    const QString type = obj.value(QStringLiteral("type")).toString();
    if (type.isEmpty()) {
        _sendError(clientId, QStringLiteral("BAD_MESSAGE"), QStringLiteral("Missing required field: type"), false, id);
        return;
    }

    ClientState &state = _clients[clientId];

    if (!state.authed) {
        // PROTOCOL.md §1.1: "The first message on a connection MUST be hello. Any other first
        // message -> error AUTH_REQUIRED and the server closes the socket (WS close code 1008)."
        if (type != QStringLiteral("hello")) {
            _sendError(clientId, QStringLiteral("AUTH_REQUIRED"), QStringLiteral("First message must be hello"), false);
            _transport->closeClient(clientId, kClosePolicyViolated, QStringLiteral("auth required"));
            return;
        }
        _handleHello(clientId, obj);
        return;
    }

    if (type == QStringLiteral("hello")) {
        // Re-hello on an already-authenticated connection: treated as idempotent re-ack rather
        // than UNKNOWN_TYPE, since the protocol does not forbid it.
        _handleHello(clientId, obj);
    } else if (type == QStringLiteral("subscribe")) {
        _handleSubscription(clientId, obj, true);
    } else if (type == QStringLiteral("unsubscribe")) {
        _handleSubscription(clientId, obj, false);
    } else if (type == QStringLiteral("getParam") || type == QStringLiteral("setParam")) {
        emit factMessageReceived(clientId, obj);
    } else if (type == QStringLiteral("command")) {
        _handleCommand(clientId, obj);
    } else if (type == QStringLiteral("missionUpload") || type == QStringLiteral("missionDownload") || type == QStringLiteral("missionClear")) {
        _handleMission(clientId, obj);
    } else {
        _sendError(clientId, QStringLiteral("UNKNOWN_TYPE"), QStringLiteral("Unrecognized type: %1").arg(type), false, id);
    }
}

void WebBridgeServer::_handleHello(quint64 clientId, const QJsonObject &obj)
{
    const QString id = obj.value(QStringLiteral("id")).toString();

    // PROTOCOL.md §1.1: "the field is required but its value is not validated -- the mock and
    // the M1-M5 bridge MUST accept any non-empty string."
    const QString token = obj.value(QStringLiteral("token")).toString();
    if (token.isEmpty()) {
        _sendError(clientId, QStringLiteral("BAD_MESSAGE"), QStringLiteral("hello requires a non-empty token"), false, id);
        return;
    }

    // PROTOCOL.md §1.1: "If protocolVersion major version is unsupported, server replies with
    // error UNSUPPORTED_VERSION and closes."
    const QString protocolVersion = obj.value(QStringLiteral("protocolVersion")).toString();
    if (!protocolVersion.isEmpty() && protocolVersion.section(QLatin1Char('.'), 0, 0) != QString(kProtocolVersion).section(QLatin1Char('.'), 0, 0)) {
        _sendError(clientId, QStringLiteral("UNSUPPORTED_VERSION"), QStringLiteral("Unsupported protocolVersion: %1").arg(protocolVersion), false, id);
        _transport->closeClient(clientId, kCloseProtocolError, QStringLiteral("unsupported protocol version"));
        return;
    }

    // PROTOCOL.md §1.1 / --bridge-token: when the server is configured with a token
    // (setAuthToken()), the client's token must match it exactly. Unconfigured (the v0.1
    // default) keeps accepting any non-empty token, checked above.
    if (!_authToken.isEmpty() && token != _authToken) {
        _sendError(clientId, QStringLiteral("AUTH_FAILED"), QStringLiteral("Token rejected"), false, id);
        _transport->closeClient(clientId, kClosePolicyViolated, QStringLiteral("auth failed"));
        return;
    }

    if (!_bridge) {
        _sendError(clientId, QStringLiteral("INTERNAL"), QStringLiteral("Bridge not available"), false, id);
        return;
    }

    _clients[clientId].authed = true;

    QJsonObject ack;
    ack[QStringLiteral("type")] = QStringLiteral("helloAck");
    ack[QStringLiteral("protocolVersion")] = QString(kProtocolVersion);
    ack[QStringLiteral("serverVersion")] = QString(kServerVersion);
    ack[QStringLiteral("serverTimeUs")] = static_cast<qint64>(_bridge->serverTimeUs());
    _sendJson(clientId, ack);

    qCDebug(WebBridgeServerLog) << "client" << clientId << "authenticated";
    emit clientAuthenticated(clientId);
}

void WebBridgeServer::_handleSubscription(quint64 clientId, const QJsonObject &obj, bool subscribe)
{
    const QString id = obj.value(QStringLiteral("id")).toString();
    const QString channel = obj.value(QStringLiteral("channel")).toString();

    if (channel.isEmpty()) {
        _sendError(clientId, QStringLiteral("BAD_MESSAGE"), QStringLiteral("Missing required field: channel"), false, id);
        return;
    }

    if (!_isKnownChannel(channel)) {
        _sendError(clientId, QStringLiteral("UNKNOWN_CHANNEL"), QStringLiteral("Unknown channel: %1").arg(channel), false, id);
        return;
    }

    // Most channels are scoped by `vehicleId`; `video` is scoped by `streamId` instead
    // (PROTOCOL.md §9). Both are folded into the same integer scope id for stream-key purposes
    // (WebBridge::streamKey()); non-scoped channels (adsb) carry neither field.
    const bool hasVehicleId = obj.contains(QStringLiteral("vehicleId"));
    const bool hasStreamId = obj.contains(QStringLiteral("streamId"));

    // PROTOCOL.md §3's envelope table: `vehicleId` is required "for vehicle-scoped messages".
    // `telemetry`/`mission`/`image` are vehicle-scoped (unlike `video`, scoped by `streamId`, and
    // `adsb`, not scoped at all) -- omitting it entirely is a malformed request, not "unknown
    // vehicle" (§10: "missing required field" -> BAD_MESSAGE).
    static const QSet<QString> kVehicleScopedChannels = { QStringLiteral("telemetry"), QStringLiteral("mission"), QStringLiteral("image") };
    if (!hasVehicleId && kVehicleScopedChannels.contains(channel)) {
        _sendError(clientId, QStringLiteral("BAD_MESSAGE"), QStringLiteral("%1 requires vehicleId").arg(channel), false, id);
        return;
    }
    const int scopeId = hasVehicleId ? obj.value(QStringLiteral("vehicleId")).toInt(WebBridge::kNoVehicleId)
                       : hasStreamId ? obj.value(QStringLiteral("streamId")).toInt(WebBridge::kNoVehicleId)
                       : WebBridge::kNoVehicleId;

    const QString key = WebBridge::streamKey(channel, scopeId);
    ClientState &state = _clients[clientId];

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
        _sendJson(clientId, ack);
        emit snapshotRequested(channel, scopeId);

        if (channel == QStringLiteral("video")) {
            // §9.2 per-client keyframe gate: every (re)subscribe re-arms the wait -- a client
            // that already saw a keyframe before is still withheld frames again until the next
            // one, since it has no guarantee the stream state it previously decoded is still
            // valid (matches "a client joining mid-stream renders nothing until the first
            // keyframe" applying equally to a client REjoining). See broadcastBinary().
            state.videoPendingKeyframe.insert(key);

            // Late subscriber: replay the last videoConfig directly to this client so it isn't
            // stuck without codec config until the next SPS/PPS change (which may be GOPs away)
            // -- see cacheVideoConfig()'s doc comment.
            const auto cacheIt = _cachedVideoConfig.constFind(static_cast<quint8>(scopeId));
            if (cacheIt != _cachedVideoConfig.constEnd()) {
                QJsonObject snapshot = cacheIt.value();
                snapshot[QStringLiteral("snapshot")] = true;
                _sendJson(clientId, snapshot);
            }
        }

        qCDebug(WebBridgeServerLog) << "client" << clientId << "subscribed" << key;
    } else {
        state.subscriptions.remove(key);
        state.videoPendingKeyframe.remove(key);
        _sendJson(clientId, ack);
        qCDebug(WebBridgeServerLog) << "client" << clientId << "unsubscribed" << key;
    }
}

void WebBridgeServer::_handleCommand(quint64 clientId, const QJsonObject &obj)
{
    const QString id = obj.value(QStringLiteral("id")).toString();

    // PROTOCOL.md §5.1 request envelope: { type: "command", id, vehicleId, action, params }.
    // CommandChannel (B7b) owns action/params semantics; this only guards envelope shape.
    if (id.isEmpty() || !obj.contains(QStringLiteral("vehicleId")) || obj.value(QStringLiteral("action")).toString().isEmpty()) {
        _sendError(clientId, QStringLiteral("BAD_MESSAGE"), QStringLiteral("command requires id, vehicleId, and action"), false, id);
        return;
    }

    emit commandReceived(clientId, obj);
}

void WebBridgeServer::_handleMission(quint64 clientId, const QJsonObject &obj)
{
    const QString id = obj.value(QStringLiteral("id")).toString();

    // PROTOCOL.md §7.2 request envelopes: { type: "missionUpload"|"missionDownload"|
    // "missionClear", id, vehicleId, ... }. MissionChannel owns item-schema/op semantics; this
    // only guards envelope shape (matches _handleCommand()'s division of responsibility).
    if (id.isEmpty() || !obj.contains(QStringLiteral("vehicleId"))) {
        _sendError(clientId, QStringLiteral("BAD_MESSAGE"), QStringLiteral("mission messages require id and vehicleId"), false, id);
        return;
    }

    emit missionMessageReceived(clientId, obj);
}

void WebBridgeServer::_sendJson(quint64 clientId, const QJsonObject &obj)
{
    _transport->sendText(clientId, QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

void WebBridgeServer::_sendError(quint64 clientId, const QString &code, const QString &message, bool retryable, const QString &id)
{
    QJsonObject err;
    err[QStringLiteral("type")] = QStringLiteral("error");
    if (!id.isEmpty()) {
        err[QStringLiteral("id")] = id;
    }
    err[QStringLiteral("code")] = code;
    err[QStringLiteral("message")] = message;
    err[QStringLiteral("retryable")] = retryable;
    _sendJson(clientId, err);

    qCDebug(WebBridgeServerLog) << "client" << clientId << "error" << code << message;
}

bool WebBridgeServer::_isKnownChannel(const QString &channel)
{
    static const QStringList kKnownChannels = {
        QStringLiteral("telemetry"),
        QStringLiteral("mission"),
        QStringLiteral("video"),
        QStringLiteral("adsb"),
        QStringLiteral("image"),
    };
    return kKnownChannels.contains(channel);
}
