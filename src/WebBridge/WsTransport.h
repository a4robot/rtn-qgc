#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtNetwork/QHostAddress>

/// Transport-layer abstraction for WebBridgeServer (the wire layer underneath PROTOCOL.md).
/// Introduced in Wave 15 / job Q7e so WebBridgeServer's protocol state machine (hello §1.1,
/// subscribe/unsubscribe §2.2, command §5, mission §7, tick §11.1, error envelope §10, ...) can
/// stay byte-for-byte identical while the concrete WebSocket implementation underneath it swaps
/// between Qt's QWebSocketServer (QtWsTransport, QGC_ENABLE_QT_WEBSOCKETS=ON, default) and
/// IXWebSocket (IxWsTransport, QGC_ENABLE_QT_WEBSOCKETS=OFF, the ghost-diet build). See
/// IxWsTransport.cc's header comment for the library-choice rationale. Exactly one implementation
/// is compiled per build (selected in src/WebBridge/CMakeLists.txt); both are built against this
/// same header, so this is deliberately the ONE abstraction this swap introduces -- WebBridgeServer
/// itself, and every channel class above it, only ever sees this interface plus plain quint64
/// client ids.
///
/// Client identity is a stable, monotonically increasing quint64 assigned by the transport when a
/// connection is accepted (never 0, never reused for the lifetime of the process) -- the same
/// "clientToken" WebBridgeServer already handed to CommandChannel/MissionChannel before this swap;
/// FactChannel is updated by this swap to use it too (see FactChannel.h), so nothing above this
/// interface ever needs to name the underlying socket type.
///
/// Threading contract: an implementation MAY run its I/O on background thread(s) (IxWsTransport
/// does -- one thread per connection, IXWebSocket's own model), but every signal below MUST be
/// delivered on the thread that owns this QObject (i.e. marshalled via
/// QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection) or equivalent from any non-owning
/// thread) -- WebBridgeServer and every channel class assume they run on the Qt main thread and
/// are not otherwise thread-safe. Conversely, sendText()/sendBinary()/closeClient() are always
/// called BY the Qt main thread and MUST be safe to call from there regardless of which thread
/// actually owns the connection -- both implementations satisfy this (QWebSocket is only ever
/// touched from the Qt thread by QtWsTransport; IXWebSocket's WebSocket::send()/close() are
/// documented thread-safe by design, guarded by their own internal write mutex).
class WsTransport : public QObject
{
    Q_OBJECT

public:
    explicit WsTransport(QObject *parent = nullptr) : QObject(parent) {}
    ~WsTransport() override = default;

    /// Starts listening on (address, port). Idempotent: returns true if already listening.
    /// Returns false on bind failure.
    virtual bool listen(const QHostAddress &address, quint16 port) = 0;

    /// Stops listening and forcibly disconnects every connected client. clientDisconnected() is
    /// NOT guaranteed to fire (synchronously or at all) for clients dropped by this call --
    /// WebBridgeServer::stop() clears its own client table directly rather than relying on the
    /// signal for this path, matching pre-swap QWebSocketServer::close() behavior.
    virtual void stop() = 0;

    virtual bool isListening() const = 0;

    /// Sends a single WS text frame to @p clientId. Silently dropped if the client is no longer
    /// connected -- callers are expected to already track their own client set, but this must
    /// never crash on a stale id (mirrors PROTOCOL.md §11.2 #4's "in-flight requests at
    /// disconnect time are lost").
    virtual void sendText(quint64 clientId, const QString &text) = 0;

    /// Sends a single WS binary frame to @p clientId (§9 video frames). Same stale-id contract
    /// as sendText().
    virtual void sendBinary(quint64 clientId, const QByteArray &data) = 0;

    /// Closes @p clientId's connection with the given WS close code/reason (RFC 6455 §7.4 codes,
    /// e.g. 1000 normal, 1002 protocol error, 1008 policy violation). No-op if already
    /// disconnected.
    virtual void closeClient(quint64 clientId, quint16 code, const QString &reason) = 0;

signals:
    /// A new client connection was accepted; @p clientId is now valid for sendText()/
    /// sendBinary()/closeClient() until the matching clientDisconnected().
    void clientConnected(quint64 clientId);

    /// A complete WS text message was received from @p clientId. Binary frames received FROM a
    /// client are not surfaced -- PROTOCOL.md has no client->server binary message (§9 video is
    /// server->client only), so neither transport implementation wires one up.
    void textMessageReceived(quint64 clientId, const QString &message);

    /// @p clientId's connection closed (either end), outside of an explicit stop(). Always
    /// paired 1:1 with an earlier clientConnected() for the same id within a process lifetime.
    void clientDisconnected(quint64 clientId);
};
