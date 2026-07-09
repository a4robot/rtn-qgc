#include "QtWsTransport.h"

#include <QtWebSockets/QWebSocket>
#include <QtWebSockets/QWebSocketProtocol>

#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(QtWsTransportLog, "WebBridge.QtWsTransport")

QtWsTransport::QtWsTransport(QObject *parent)
    : WsTransport(parent)
    , _server(QStringLiteral("WebBridgeServer"), QWebSocketServer::NonSecureMode, this)
{
    connect(&_server, &QWebSocketServer::newConnection, this, &QtWsTransport::_onNewConnection);
}

QtWsTransport::~QtWsTransport()
{
    stop();
}

bool QtWsTransport::listen(const QHostAddress &address, quint16 port)
{
    if (_server.isListening()) {
        qCDebug(QtWsTransportLog) << "listen: already listening";
        return true;
    }

    if (!_server.listen(address, port)) {
        qCWarning(QtWsTransportLog) << "failed to listen on" << address.toString() << ":" << port << _server.errorString();
        return false;
    }

    qCDebug(QtWsTransportLog) << "listening on" << address.toString() << ":" << port;
    return true;
}

void QtWsTransport::stop()
{
    if (!_server.isListening() && _clients.isEmpty()) {
        return;
    }

    // Copy the key list: closing a client may synchronously trigger _onDisconnected(), which
    // would otherwise mutate _clients while we are iterating it.
    const QList<QWebSocket *> clients = _ids.keys();
    for (QWebSocket *client : clients) {
        client->close(QWebSocketProtocol::CloseCodeNormal, QStringLiteral("server stopping"));
        client->deleteLater();
    }
    _clients.clear();
    _ids.clear();

    _server.close();
    qCDebug(QtWsTransportLog) << "stopped";
}

bool QtWsTransport::isListening() const
{
    return _server.isListening();
}

void QtWsTransport::sendText(quint64 clientId, const QString &text)
{
    QWebSocket *client = _clients.value(clientId, nullptr);
    if (!client) {
        return;
    }
    client->sendTextMessage(text);
}

void QtWsTransport::sendBinary(quint64 clientId, const QByteArray &data)
{
    QWebSocket *client = _clients.value(clientId, nullptr);
    if (!client) {
        return;
    }
    client->sendBinaryMessage(data);
}

void QtWsTransport::closeClient(quint64 clientId, quint16 code, const QString &reason)
{
    QWebSocket *client = _clients.value(clientId, nullptr);
    if (!client) {
        return;
    }
    client->close(static_cast<QWebSocketProtocol::CloseCode>(code), reason);
}

void QtWsTransport::_onNewConnection()
{
    while (_server.hasPendingConnections()) {
        QWebSocket *client = _server.nextPendingConnection();
        if (!client) {
            continue;
        }

        const quint64 id = _nextId++;
        _clients.insert(id, client);
        _ids.insert(client, id);
        connect(client, &QWebSocket::textMessageReceived, this, &QtWsTransport::_onTextMessageReceived);
        connect(client, &QWebSocket::disconnected, this, &QtWsTransport::_onDisconnected);

        qCDebug(QtWsTransportLog) << client << "connected from" << client->peerAddress().toString() << client->peerPort() << "id" << id;
        emit clientConnected(id);
    }
}

void QtWsTransport::_onTextMessageReceived(const QString &message)
{
    QWebSocket *client = qobject_cast<QWebSocket *>(sender());
    if (!client) {
        return;
    }
    const auto it = _ids.constFind(client);
    if (it == _ids.constEnd()) {
        return;
    }
    emit textMessageReceived(it.value(), message);
}

void QtWsTransport::_onDisconnected()
{
    QWebSocket *client = qobject_cast<QWebSocket *>(sender());
    if (!client) {
        return;
    }

    const auto it = _ids.constFind(client);
    if (it != _ids.constEnd()) {
        const quint64 id = it.value();
        qCDebug(QtWsTransportLog) << client << "disconnected, id" << id;
        _clients.remove(id);
        _ids.erase(it);
        client->deleteLater();
        emit clientDisconnected(id);
    } else {
        client->deleteLater();
    }
}
