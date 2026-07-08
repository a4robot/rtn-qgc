#pragma once

#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtNetwork/QHostAddress>
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

    /// Overrides the bind address used by start() (default QHostAddress::LocalHost, i.e.
    /// 127.0.0.1, per the PROTOCOL.md §1 transport table security note). Set from --bridge-host
    /// before calling start() to expose the bridge beyond localhost (e.g. QHostAddress::Any or a
    /// specific LAN address for --bridge-host 0.0.0.0). No effect once already listening.
    void setListenAddress(const QHostAddress &address) { _listenAddress = address; }

    /// Sets the token that a client's `hello` (PROTOCOL.md §1.1) must present to authenticate.
    /// Empty (the default) keeps v0.1 behavior: any non-empty token is accepted. Set from
    /// --bridge-token. Takes effect on the next `hello` processed, so it should be called before
    /// start().
    void setAuthToken(const QString &token) { _authToken = token; }

    /// Binds the websocket server to WebBridgeServer::listenAddress():WebBridge::listenPort().
    /// Localhost-only by default per the PROTOCOL.md §1 transport table security note; logs a
    /// warning if configured (via setListenAddress()) to bind a non-localhost address. Idempotent
    /// (returns true if already listening). Returns false if the bind fails (port in use, etc.).
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

    /// Sends @p frame as a single binary WS message to every authenticated client subscribed to
    /// the `video` stream identified by (channel, streamId) -- same subscription-key filtering as
    /// broadcast() (PROTOCOL.md §9: binary frames carry no seq/envelope of their own, so this
    /// bypasses WebBridge::makeStreamMessage() and sends @p frame verbatim). @p channel is
    /// expected to be "video"; kept as a parameter (rather than hardcoded) so this stays
    /// consistent with broadcast()'s (channel, scopeId) key shape.
    void broadcastBinary(const QString &channel, int streamId, const QByteArray &frame);

    /// Sends @p message to exactly one client -- the one that sent the request identified by
    /// @p clientToken (assigned in _onNewConnection(), stable for the lifetime of the
    /// connection). Used by CommandChannel (B7b) to answer `command` requests (§5), which unlike
    /// broadcast()'s subscription fan-out must go only to the requester. A token for a client
    /// that has since disconnected is a silent no-op (the response is simply dropped, matching
    /// PROTOCOL.md §11.2 #4: "In-flight requests ... at disconnect time are lost"). Kept
    /// token-based (rather than QWebSocket*) so that non-QtWebSockets callers (CommandChannel is
    /// Qt Core + Positioning only) never need to see a QWebSocket type.
    void sendToClient(quint64 clientToken, const QJsonObject &message);

public slots:
    /// Caches the latest PROTOCOL.md §9.1 videoConfig payload for @p streamId -- the fields
    /// VideoStreamServer::configReady() emits (codec, streamIndex, width, height, sps, pps), NOT
    /// yet enveloped -- and both (a) broadcasts it now to every client already subscribed to the
    /// `video`/@p streamId stream, so watching clients pick up SPS/PPS or resolution changes
    /// mid-stream (§9.1: "again whenever SPS/PPS change mid-stream"), and (b) remembers it so a
    /// client that subscribes *later* can be caught up: _handleSubscription() replays this same
    /// cached message (with `snapshot` forced true) directly to a newly-subscribing client, since
    /// nothing else would ever hand it codec config until the next SPS/PPS change (which may be
    /// GOPs away). Safe to call at any time, including before any client has subscribed.
    void cacheVideoConfig(quint8 streamId, const QJsonObject &config);

signals:
    /// Emitted once a client's `subscribe` has been acknowledged (PROTOCOL.md §2.2: "On every
    /// successful subscribe, the server immediately sends one full-state snapshot"). Channel
    /// implementations (B2a+) connect to this and respond by calling broadcast() with a
    /// snapshot message for (channel, vehicleId). @p vehicleId is WebBridge::kNoVehicleId for
    /// non-vehicle-scoped channels (adsb), and carries the `streamId` for `video` (§9), which
    /// this class does not otherwise distinguish from `vehicleId`.
    void snapshotRequested(const QString &channel, int vehicleId);

    /// Emitted when a "getParam" or "setParam" message is received.
    void factMessageReceived(QWebSocket *client, const QJsonObject &message);

    /// Emitted for every authenticated client's `command` message once the §5.1 envelope has
    /// been validated (id/vehicleId/action present -- BAD_MESSAGE is sent directly and this
    /// signal is not emitted otherwise). @p clientToken identifies the requester for
    /// sendToClient(); @p request is the parsed request object verbatim. CommandChannel (B7b)
    /// connects to this and answers via sendToClient().
    void commandReceived(quint64 clientToken, const QJsonObject &request);

    /// Emitted for every authenticated client's `missionUpload`/`missionDownload`/`missionClear`
    /// message once the §7.2 envelope has been validated (id/vehicleId present -- BAD_MESSAGE is
    /// sent directly and this signal is not emitted otherwise). @p clientToken identifies the
    /// requester for sendToClient(); @p request is the parsed request object verbatim.
    /// MissionChannel connects to this and answers via sendToClient().
    void missionMessageReceived(quint64 clientToken, const QJsonObject &request);

public slots:
    /// Sends a point-to-point response to a specific client.
    void reply(QWebSocket *client, const QJsonObject &message);

    /// Sends a point-to-point error envelope to a specific client (PROTOCOL.md §10).
    void replyError(QWebSocket *client, const QString &code, const QString &message, bool retryable, const QString &id = QString());


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
        quint64 token = 0;                 ///< Stable per-connection id for sendToClient() (assigned in _onNewConnection())
        QSet<QString> subscriptions;       ///< Stream keys (WebBridge::streamKey) this client is subscribed to (§2.2)
    };

    /// Handles a `hello` message: validates the (non-empty, per §1.1) token and, if present, the
    /// major protocolVersion, then marks the client authenticated and replies `helloAck`.
    /// Replies `BAD_MESSAGE` for a missing/empty token, `UNSUPPORTED_VERSION` (+ close) for a
    /// protocolVersion major-version mismatch, or `AUTH_FAILED` (+ close) if a server token is
    /// configured (setAuthToken()) and the client's token does not match it.
    void _handleHello(QWebSocket *client, const QJsonObject &obj);

    /// Handles `subscribe`/`unsubscribe` (§2.2): validates the channel, updates the client's
    /// subscription set, replies `subscribeAck`/`unsubscribeAck`, and (for subscribe) emits
    /// snapshotRequested(). Replies `BAD_MESSAGE` for a missing channel, `UNKNOWN_CHANNEL` for
    /// an unrecognized one.
    void _handleSubscription(QWebSocket *client, const QJsonObject &obj, bool subscribe);

    /// Handles a `command` message (PROTOCOL.md §5.1): validates that `id`, `vehicleId`, and
    /// `action` are present, replying `BAD_MESSAGE` (§10) if not, then emits commandReceived()
    /// for CommandChannel (B7b) to execute. This class does not itself know how to run guided
    /// actions -- validation here is limited to envelope shape, not action semantics/params.
    void _handleCommand(QWebSocket *client, const QJsonObject &obj);

    /// Handles `missionUpload`/`missionDownload`/`missionClear` messages (PROTOCOL.md §7.2):
    /// validates that `id` and `vehicleId` are present, replying `BAD_MESSAGE` (§10) if not, then
    /// emits missionMessageReceived() for MissionChannel to execute. This class does not itself
    /// know mission item schema/semantics -- validation here is limited to envelope shape.
    void _handleMission(QWebSocket *client, const QJsonObject &obj);

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
    QHostAddress _listenAddress = QHostAddress::LocalHost;    ///< Bind address for start() (--bridge-host override)
    QString _authToken;              ///< Required `hello` token (--bridge-token); empty = accept any non-empty token
    QWebSocketServer _server;
    QHash<QWebSocket *, ClientState> _clients;
    QHash<quint64, QWebSocket *> _tokenToClient;    ///< Reverse index of ClientState::token for sendToClient()
    QHash<quint8, QJsonObject> _cachedVideoConfig;  ///< Last enveloped videoConfig message sent per streamId (§9.1), for late-subscriber replay
    quint64 _nextClientToken = 1;                   ///< Monotonic counter; 0 is never issued so it can be used as a "no client" sentinel
};
