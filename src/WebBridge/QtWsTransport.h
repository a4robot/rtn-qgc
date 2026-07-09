#pragma once

#include <QtCore/QHash>
#include <QtCore/QLoggingCategory>
#include <QtWebSockets/QWebSocketServer>

#include "WsTransport.h"

class QWebSocket;

Q_DECLARE_LOGGING_CATEGORY(QtWsTransportLog)

/// Default transport (QGC_ENABLE_QT_WEBSOCKETS=ON, upstream QGC's existing behavior): a thin
/// QWebSocketServer/QWebSocket wrapper behind WsTransport. Everything here already runs on the Qt
/// main thread (Qt's own event loop owns the sockets), so signal delivery is direct -- no
/// cross-thread marshalling needed, unlike IxWsTransport (see its header comment for why the
/// off-path needs one). This is a mechanical port of the QWebSocketServer/QWebSocket plumbing
/// that used to live directly in WebBridgeServer -- see git history pre-Q7e for the byte-for-byte
/// original.
class QtWsTransport : public WsTransport
{
    Q_OBJECT

public:
    explicit QtWsTransport(QObject *parent = nullptr);
    ~QtWsTransport() override;

    bool listen(const QHostAddress &address, quint16 port) override;
    void stop() override;
    bool isListening() const override;
    void sendText(quint64 clientId, const QString &text) override;
    void sendBinary(quint64 clientId, const QByteArray &data) override;
    void closeClient(quint64 clientId, quint16 code, const QString &reason) override;

private slots:
    void _onNewConnection();
    void _onTextMessageReceived(const QString &message);
    void _onDisconnected();

private:
    QWebSocketServer _server;
    QHash<quint64, QWebSocket *> _clients;   ///< clientId -> socket
    QHash<QWebSocket *, quint64> _ids;       ///< socket -> clientId (reverse index)
    quint64 _nextId = 1;                     ///< Monotonic; 0 is never issued (WsTransport contract)
};
