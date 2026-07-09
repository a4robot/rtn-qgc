/*
 * Library choice: IXWebSocket (github.com/machinezone/IXWebSocket), evaluated 2026-07-09
 * against the STRANGLER_MILESTONES.md Q7e brief (drop libQt6WebSockets from the ghost) as the
 * non-Qt WS server underneath WsTransport (see WsTransport.h). Candidates considered:
 *
 *   - uWebSockets: fastest of the four, but its uSockets C dependency wants to OWN the event
 *     loop (epoll/kqueue directly). Every existing channel class (TelemetryChannel,
 *     CommandChannel, MissionChannel, FactChannel, NotificationChannel) runs on the Qt main
 *     thread and talks to WebBridgeServer via signals/slots -- bridging that to a foreign loop
 *     would mean either running uSockets on its own thread (re-deriving most of what
 *     IXWebSocket already gives us for free) or pumping it from inside Qt's loop via raw fd
 *     integration, which is exactly the "awkward" case the task brief called out up front.
 *     Rejected on Qt-loop-bridging cleanliness.
 *   - libwebsockets: mature, small, but service-loop based (lws_service()) -- integrating it
 *     cleanly with QSocketNotifier per fd is a well-trodden path but noticeably more plumbing
 *     (and more C-style callback bookkeeping) than the other options for a single-file,
 *     one-swap job. Rejected on integration complexity for the value delivered here.
 *   - Boost.Beast: needs Boost, which this repo does not vendor anywhere (checked: no
 *     find_package(Boost)/CPMAddPackage(NAME boost) in CMakeLists.txt or cmake/). Pulling in
 *     Boost for one WS transport is a large, hard-to-review dependency for a "friction ①"
 *     job whose whole premise is that src/WebBridge/ is 100% our own code and should stay
 *     easy to reason about. Rejected on footprint/vendorability.
 *   - IXWebSocket: simple C++ (no dependency beyond zlib, which this repo already CPM-vendors
 *     for QGCCompression -- see src/Utilities/Compression/CMakeLists.txt's ZLIB::ZLIB target,
 *     reused as-is by IXWebSocket's own `find_package(ZLIB REQUIRED)`); TLS support is a build
 *     option (USE_TLS, default OFF) we leave off entirely -- this is a localhost-only bridge
 *     per PROTOCOL.md §1's transport table, so no TLS backend (and no OpenSSL/mbedTLS
 *     dependency) is pulled in at all. Ships a plain CMakeLists.txt (target `ixwebsocket`),
 *     fetched here via CPM exactly like GeographicLib in src/Utilities/Geo/CMakeLists.txt.
 *     Binary frames are first-class (WebSocket::sendBinary()/msg->binary), covering §9 video.
 *     Most importantly for the Qt bridge: IXWebSocket's server model is thread-PER-CONNECTION
 *     (ix::WebSocketServer spawns one std::thread per accepted connection -- see
 *     IXWebSocketServer.cpp's handleConnection()) with connection objects whose send()/close()
 *     are documented thread-safe (internally mutex-guarded) precisely so a caller on a
 *     different thread can drive them. That is *exactly* the shape the task brief asked for:
 *     "incoming WS events must marshal onto the Qt thread ... outgoing sends must be
 *     thread-safe from the Qt thread" -- IXWebSocket already keeps its I/O off the Qt thread
 *     and hands us thread-safe send/close, so this class's entire job is the marshal-in half
 *     (QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection) from IXWebSocket's
 *     connection thread into every WsTransport signal below); the marshal-out half is already
 *     satisfied by IXWebSocket's own guarantees. Chosen.
 *
 * Threading model implemented below (see also WsTransport.h's contract):
 *   - ix::WebSocketServer::setOnConnectionCallback() fires once per accepted connection, on a
 *     freshly spawned thread dedicated to that connection (confirmed against
 *     IXWebSocketServer.cpp: handleConnection() -> handleUpgrade() runs the whole connection,
 *     including the TCP accept-to-close lifetime, on that one thread). We register the message
 *     callback synchronously inside it (IXWebSocket requires this -- it errors out if
 *     isOnMessageCallbackRegistered() is still false right after the connection callback
 *     returns) and assign this connection's clientId there too, so it is fixed and available
 *     to every subsequent callback on this same thread without needing a reverse lookup.
 *   - ix::WebSocket::setOnMessageCallback() then delivers Open (once, synthesized right after
 *     the WS handshake completes), Message* (one per complete text/binary frame -- IXWebSocket
 *     reassembles fragmentation internally, so WebBridgeServer never sees partial frames any
 *     more than it did under QWebSocket), and Close (at most once), all on that same
 *     connection thread, all handled by forwarding onto the Qt thread via invokeMethod(...,
 *     QueuedConnection) so WsTransport's signals are only ever emitted there.
 *   - `_clients` (clientId -> shared_ptr<ix::WebSocket>) is touched ONLY inside those
 *     queued-onto-Qt-thread callbacks, and by sendText()/sendBinary()/closeClient()/stop(),
 *     which are only ever called BY the Qt thread (WebBridgeServer's own contract) -- so no
 *     mutex is needed around it despite connections living on background threads.
 *
 * Backpressure caveat (the one place this transport's behavior is NOT a byte-for-byte match for
 * QWebSocket): QWebSocket::sendTextMessage()/sendBinaryMessage() are fully asynchronous -- they
 * hand off to Qt's own socket write buffer and never block the calling (Qt main) thread.
 * IXWebSocket's WebSocket::send()/sendBinary(), in contrast, write synchronously on whichever
 * thread calls them, and WebSocketServer defaults to NO send timeout (blocks indefinitely if a
 * client's TCP receive window stays full). Since WebBridgeServer's broadcast()/broadcastAll()/
 * broadcastBinary() call sendText()/sendBinary() directly from the Qt thread (by design -- see
 * WsTransport.h), an unbounded block there would stall the whole ghost, not just one client's
 * stream. listen() below passes an explicit, bounded sendTimeoutSeconds to WebSocketServer so a
 * stalled/malicious reader degrades to a multi-second stall (and likely connection drop) instead
 * of an indefinite hang -- not identical to QWebSocket's true non-blocking behavior, but the
 * closest safe equivalent IXWebSocket's synchronous-send model allows without a redesign into a
 * per-client outbound queue, which the conformance suite's clients (well-behaved, same-host)
 * never exercise either way.
 */
#include "IxWsTransport.h"

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketServer.h>

#include <QtCore/QMetaObject>

#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(IxWsTransportLog, "WebBridge.IxWsTransport")

IxWsTransport::IxWsTransport(QObject *parent)
    : WsTransport(parent)
{
    ix::initNetSystem();
}

IxWsTransport::~IxWsTransport()
{
    stop();
    ix::uninitNetSystem();
}

bool IxWsTransport::listen(const QHostAddress &address, quint16 port)
{
    if (_listening) {
        qCDebug(IxWsTransportLog) << "listen: already listening";
        return true;
    }

    // Bounded send timeout (see this file's header comment's "Backpressure caveat"): IXWebSocket
    // defaults to -1 (block forever) here, which would let one stalled reader freeze the whole
    // Qt thread since sendText()/sendBinary() below are always called synchronously from it.
    // Every other constructor argument is IXWebSocket's own documented default, spelled out
    // explicitly because C++ has no way to skip a positional argument.
    static constexpr int kSendTimeoutSeconds = 5;
    static constexpr int kPingIntervalSeconds = -1; // disabled, matches ix::WebSocketServer's own default

    const std::string host = address.toString().toStdString();
    _server = std::make_unique<ix::WebSocketServer>(
        static_cast<int>(port),
        host,
        ix::SocketServer::kDefaultTcpBacklog,
        ix::SocketServer::kDefaultMaxConnections,
        ix::WebSocketServer::kDefaultHandShakeTimeoutSecs,
        ix::SocketServer::kDefaultAddressFamily,
        kPingIntervalSeconds,
        kSendTimeoutSeconds);

    // §1 transport table: no compression negotiation is part of the protocol contract, and
    // permessage-deflate is purely a wire-level optimization invisible to PROTOCOL.md-level
    // behavior -- disabled to keep this transport's behavior simple and predictable rather than
    // for any conformance reason.
    _server->disablePerMessageDeflate();

    // Guarded by _nextId's atomicity, not by the Qt thread: onConnectionCallback runs on a
    // freshly spawned thread per connection (see this file's header comment), so two
    // connections accepted back-to-back can race here.
    _server->setOnConnectionCallback(
        [this](std::weak_ptr<ix::WebSocket> weakWebSocket, const std::shared_ptr<ix::ConnectionState> &) {
            std::shared_ptr<ix::WebSocket> webSocket = weakWebSocket.lock();
            if (!webSocket) {
                return;
            }

            const quint64 id = _nextId.fetch_add(1, std::memory_order_relaxed);

            webSocket->setOnMessageCallback(
                [this, webSocket, id](const ix::WebSocketMessagePtr &msg) {
                    switch (msg->type) {
                    case ix::WebSocketMessageType::Open:
                        QMetaObject::invokeMethod(this, [this, webSocket, id]() {
                            _clients.insert(id, webSocket);
                            emit clientConnected(id);
                        }, Qt::QueuedConnection);
                        break;
                    case ix::WebSocketMessageType::Message:
                        // PROTOCOL.md has no client->server binary message (§9 video is
                        // server->client only) -- a binary frame here would be a client bug;
                        // silently dropped, matching QtWsTransport (which never wires
                        // QWebSocket::binaryMessageReceived at all).
                        if (!msg->binary) {
                            const QString text = QString::fromStdString(msg->str);
                            QMetaObject::invokeMethod(this, [this, id, text]() {
                                if (_clients.contains(id)) {
                                    emit textMessageReceived(id, text);
                                }
                            }, Qt::QueuedConnection);
                        }
                        break;
                    case ix::WebSocketMessageType::Close:
                        QMetaObject::invokeMethod(this, [this, id]() {
                            if (_clients.remove(id) > 0) {
                                emit clientDisconnected(id);
                            }
                        }, Qt::QueuedConnection);
                        break;
                    default:
                        // Error/Ping/Pong/Fragment: no WsTransport-level signal for these --
                        // Ping/Pong are handled internally by IXWebSocket (server responds to
                        // pings automatically), and Error precedes a Close that already covers
                        // teardown.
                        break;
                    }
                });
        });

    const std::pair<bool, std::string> result = _server->listen();
    if (!result.first) {
        qCWarning(IxWsTransportLog) << "failed to listen on" << address.toString() << ":" << port
                                     << QString::fromStdString(result.second);
        _server.reset();
        return false;
    }

    _server->start();
    _listening = true;
    qCDebug(IxWsTransportLog) << "listening on" << address.toString() << ":" << port;
    return true;
}

void IxWsTransport::stop()
{
    if (!_server) {
        return;
    }

    // Synchronous: joins every connection thread. clientDisconnected() is not guaranteed to
    // fire for clients dropped here (see WsTransport.h's stop() contract) -- WebBridgeServer
    // clears its own client table directly instead of relying on it.
    _server->stop();
    _server.reset();
    _clients.clear();
    _listening = false;
    qCDebug(IxWsTransportLog) << "stopped";
}

bool IxWsTransport::isListening() const
{
    return _listening;
}

void IxWsTransport::sendText(quint64 clientId, const QString &text)
{
    const auto it = _clients.constFind(clientId);
    if (it == _clients.constEnd()) {
        return;
    }
    it.value()->sendText(text.toStdString());
}

void IxWsTransport::sendBinary(quint64 clientId, const QByteArray &data)
{
    const auto it = _clients.constFind(clientId);
    if (it == _clients.constEnd()) {
        return;
    }
    it.value()->sendBinary(std::string(data.constData(), static_cast<size_t>(data.size())));
}

void IxWsTransport::closeClient(quint64 clientId, quint16 code, const QString &reason)
{
    const auto it = _clients.constFind(clientId);
    if (it == _clients.constEnd()) {
        return;
    }
    it.value()->close(code, reason.toStdString());
}
