#pragma once

#include <QtCore/QHash>
#include <QtCore/QLoggingCategory>
#include <atomic>
#include <memory>

#include "WsTransport.h"

namespace ix {
class WebSocket;
class WebSocketServer;
} // namespace ix

Q_DECLARE_LOGGING_CATEGORY(IxWsTransportLog)

/// Non-Qt transport (QGC_ENABLE_QT_WEBSOCKETS=OFF, the ghost-diet build): wraps IXWebSocket's
/// ix::WebSocketServer/ix::WebSocket. See IxWsTransport.cc's header comment for the library
/// choice rationale and the threading model this class bridges.
class IxWsTransport : public WsTransport
{
    Q_OBJECT

public:
    explicit IxWsTransport(QObject *parent = nullptr);
    ~IxWsTransport() override;

    bool listen(const QHostAddress &address, quint16 port) override;
    void stop() override;
    bool isListening() const override;
    void sendText(quint64 clientId, const QString &text) override;
    void sendBinary(quint64 clientId, const QByteArray &data) override;
    void closeClient(quint64 clientId, quint16 code, const QString &reason) override;

private:
    /// Owns the listening socket + IXWebSocket's per-connection accept threads. Constructed
    /// fresh on every listen() (IXWebSocket's SocketServer is not designed to re-listen after
    /// stop()); torn down in stop().
    std::unique_ptr<ix::WebSocketServer> _server;

    /// clientId -> connection, touched ONLY from the Qt thread (see .cc's threading discussion):
    /// every mutation is itself performed inside a QMetaObject::invokeMethod(..., QueuedConnection)
    /// callback running on this object's thread, even though the callback is scheduled FROM one
    /// of IXWebSocket's background connection threads. This is what lets sendText()/sendBinary()/
    /// closeClient() (always called by the Qt thread) look this map up without a mutex.
    QHash<quint64, std::shared_ptr<ix::WebSocket>> _clients;

    /// Monotonic id source; 0 is never issued (WsTransport contract). Atomic because
    /// onConnectionCallback fires on a fresh thread per connection -- two connections accepted
    /// back-to-back can race here (see IxWsTransport.cc's threading discussion).
    std::atomic<quint64> _nextId { 1 };
    bool _listening = false;
};
