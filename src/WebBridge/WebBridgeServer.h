#pragma once

#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtWebSockets/QWebSocket>
#include <QtWebSockets/QWebSocketServer>

class WebBridge;

Q_DECLARE_LOGGING_CATEGORY(WebBridgeServerLog)

/// QtWebSockets transport for the WebBridge protocol defined in src/WebBridge/PROTOCOL.md
/// (v0.1). This class owns the QWebSocketServer/QWebSocket plumbing and per-connection
/// protocol state machine (hello handshake §1.1, subscribe/unsubscribe §2.2, error envelope
/// §10, tick fan-out §11.1); it defers the transport-independent mechanics (server clock,
/// sequence counters, message shaping) to WebBridge, and defers channel payload production
/// (telemetry, mission, video, adsb — B2a+) to snapshotRequested()/broadcast().
///
/// One WebBridgeServer binds a single WebBridge (not owned; must outlive this object). It does
/// not start/stop the WebBridge itself -- callers are expected to drive WebBridge::start()/
/// stop() (for the tick) alongside this class's own start()/stop() (for the socket).
class WebBridgeServer : public QObject
{
    Q_OBJECT

public:
    /// @param bridge Protocol/clock/sequence core to bind to. Not owned; must outlive this
    ///                object. May not be null (guarded defensively, but behavior is undefined
    ///                if start() is called without a valid bridge).
    explicit WebBridgeServer(WebBridge *bridge, QObject *parent = nullptr);
    ~WebBridgeServer() override;

    /// Binds the websocket server to 127.0.0.1:WebBridge::listenPort(). Localhost-only per the
    /// PROTOCOL.md §1 transport table security note. Idempotent (returns true if already
    /// listening). Returns false if the bind fails (port in use, etc.).
    bool start();

    /// Closes the listening socket and forcibly disconnects every connected client. Idempotent.
    void stop();

    bool isListening() const;

    /// Sends @p message to every authenticated client currently subscribed to the
    /// (channel, vehicleId) stream (PROTOCOL.md §2.4 stream identity via WebBridge::streamKey()).
    /// @p message is expected to already carry the full server->client envelope (typically built
    /// with WebBridge::makeStreamMessage()); this method only performs subscription-filtered
    /// fan-out. Channel implementations (B2a+) call this to publish snapshots and updates.
    void broadcast(const QString &channel, int vehicleId, const QJsonObject &message);

signals:
    /// Emitted once a client's `subscribe` has been acknowledged (PROTOCOL.md §2.2: "On every
    /// successful subscribe, the server immediately sends one full-state snapshot"). Channel
    /// implementations (B2a+) connect to this and respond by calling broadcast() with a
    /// snapshot message for (channel, vehicleId). @p vehicleId is WebBridge::kNoVehicleId for
    /// non-vehicle-scoped channels (adsb), and carries the `streamId` for `video` (§9), which
    /// this class does not otherwise distinguish from `vehicleId`.
    void snapshotRequested(const QString &channel, int vehicleId);

private slots:
    void _onNewConnection();
    void _onTextMessageReceived(const QString &message);
    void _onDisconnected();

    /// Fans out WebBridge::tickReady() to every authenticated client (PROTOCOL.md §11.1: tick
    /// is unconditional, no subscription required).
    void _onTickReady(const QJsonObject &tick);

private:
    /// Per-connection protocol state.
    struct ClientState
    {
        bool authed = false;              ///< Set once a valid `hello` has been received (§1.1)
        QSet<QString> subscriptions;       ///< Stream keys (WebBridge::streamKey) this client is subscribed to (§2.2)
    };

    /// Handles a `hello` message: validates the (non-empty, per §1.1) token and, if present, the
    /// major protocolVersion, then marks the client authenticated and replies `helloAck`.
    /// Replies `BAD_MESSAGE` for a missing/empty token, or `UNSUPPORTED_VERSION` (+ close) for a
    /// protocolVersion major-version mismatch.
    void _handleHello(QWebSocket *client, const QJsonObject &obj);

    /// Handles `subscribe`/`unsubscribe` (§2.2): validates the channel, updates the client's
    /// subscription set, replies `subscribeAck`/`unsubscribeAck`, and (for subscribe) emits
    /// snapshotRequested(). Replies `BAD_MESSAGE` for a missing channel, `UNKNOWN_CHANNEL` for
    /// an unrecognized one.
    void _handleSubscription(QWebSocket *client, const QJsonObject &obj, bool subscribe);

    /// Serializes @p obj as compact JSON and sends it as a single text frame (PROTOCOL.md §1:
    /// "server never fragments a JSON message across frames").
    void _sendJson(QWebSocket *client, const QJsonObject &obj);

    /// Builds and sends the error envelope exactly per PROTOCOL.md §10. @p id is echoed when
    /// non-empty; omitted otherwise (unsolicited errors omit `id`).
    void _sendError(QWebSocket *client, const QString &code, const QString &message, bool retryable, const QString &id = QString());

    /// Subscribable channels per PROTOCOL.md §2.2: telemetry, mission, video, adsb.
    static bool _isKnownChannel(const QString &channel);

    static constexpr const char *kProtocolVersion = "0.1";           ///< PROTOCOL.md §1.1 helloAck/hello
    static constexpr const char *kServerVersion = "rtn-qgc 5.0";     ///< PROTOCOL.md §1.1 helloAck `serverVersion`

    WebBridge *_bridge = nullptr;    ///< Not owned; must outlive this object
    QWebSocketServer _server;
    QHash<QWebSocket *, ClientState> _clients;
};
