#pragma once

#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtNetwork/QHostAddress>
#include <memory>

class WebBridge;
class WsTransport;

Q_DECLARE_LOGGING_CATEGORY(WebBridgeServerLog)

/// WebSocket transport for the WebBridge protocol defined in src/WebBridge/PROTOCOL.md (v0.1).
/// This class owns the per-connection protocol state machine (hello handshake §1.1,
/// subscribe/unsubscribe §2.2, error envelope §10, tick fan-out §11.1); it defers the concrete
/// WebSocket implementation to a WsTransport (QtWsTransport/QWebSocketServer when
/// QGC_ENABLE_QT_WEBSOCKETS=ON, the default; IxWsTransport/IXWebSocket when OFF -- see
/// WsTransport.h, selected at compile time in src/WebBridge/CMakeLists.txt), the
/// transport-independent mechanics (server clock, sequence counters, message shaping) to
/// WebBridge, and channel payload production (telemetry, mission, video, adsb -- B2a+) to
/// snapshotRequested()/broadcast().
///
/// Every client is identified by a stable, opaque quint64 id assigned by the transport
/// (WsTransport::clientConnected()) -- this class and every channel above it never sees the
/// underlying socket type, which is what lets the transport swap without touching channel logic
/// (FactChannel is the one exception; see its header comment).
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

    /// Sends @p message to every authenticated client, unconditionally -- no subscription
    /// filtering, unlike broadcast(). Same fan-out as the internal tick path (_onTickReady()),
    /// exposed publicly for channels whose messages need no subscribe per PROTOCOL.md (currently
    /// `notification`, §14; NotificationChannel connects to this via notificationReady()).
    void broadcastAll(const QJsonObject &message);

    /// Sends @p frame as a single binary WS message to every authenticated client subscribed to
    /// the `video` stream identified by (channel, streamId) -- same subscription-key filtering as
    /// broadcast() (PROTOCOL.md §9: binary frames carry no seq/envelope of their own, so this
    /// bypasses WebBridge::makeStreamMessage() and sends @p frame verbatim). @p channel is
    /// expected to be "video"; kept as a parameter (rather than hardcoded) so this stays
    /// consistent with broadcast()'s (channel, scopeId) key shape.
    ///
    /// §9.2 per-client keyframe gate: a client is additionally withheld @p frame (regardless of
    /// the above) while its ClientState::videoPendingKeyframe still contains this stream's key --
    /// i.e. from the moment it (re)subscribes until the first keyframe (flags bit 0) for this
    /// stream is seen, per §9.2's "the first binary frame after videoConfig is a keyframe; a
    /// client joining mid-stream renders nothing until the first keyframe". @p frame's flags byte
    /// (offset 4, bit 0) is inspected once per call, not per client.
    void broadcastBinary(const QString &channel, int streamId, const QByteArray &frame);

    /// Sends @p message to exactly one client -- the one that sent the request identified by
    /// @p clientId (assigned by the transport, stable for the lifetime of the connection). Used
    /// by CommandChannel (B7b) to answer `command` requests (§5), which unlike broadcast()'s
    /// subscription fan-out must go only to the requester. An id for a client that has since
    /// disconnected is a silent no-op (the response is simply dropped, matching PROTOCOL.md
    /// §11.2 #4: "In-flight requests ... at disconnect time are lost").
    void sendToClient(quint64 clientId, const QJsonObject &message);

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

    /// Emitted when a "getParam" or "setParam" message is received. @p clientId identifies the
    /// requester for reply()/replyError().
    void factMessageReceived(quint64 clientId, const QJsonObject &message);

    /// Emitted for every authenticated client's `command` message once the §5.1 envelope has
    /// been validated (id/vehicleId/action present -- BAD_MESSAGE is sent directly and this
    /// signal is not emitted otherwise). @p clientId identifies the requester for
    /// sendToClient(); @p request is the parsed request object verbatim. CommandChannel (B7b)
    /// connects to this and answers via sendToClient().
    void commandReceived(quint64 clientId, const QJsonObject &request);

    /// Emitted for every authenticated client's `missionUpload`/`missionDownload`/`missionClear`
    /// message once the §7.2 envelope has been validated (id/vehicleId present -- BAD_MESSAGE is
    /// sent directly and this signal is not emitted otherwise). @p clientId identifies the
    /// requester for sendToClient(); @p request is the parsed request object verbatim.
    /// MissionChannel connects to this and answers via sendToClient().
    void missionMessageReceived(quint64 clientId, const QJsonObject &request);

    /// Emitted once a client's `hello` has been accepted and `helloAck` sent (PROTOCOL.md §1.1).
    /// @p clientId is the same stable per-connection id used by sendToClient(). NotificationChannel
    /// (§14.3) connects to this to send a one-shot per-connection welcome notification that does
    /// not depend on the client racing broadcastAll()'s fan-out against its own connect time.
    void clientAuthenticated(quint64 clientId);

public slots:
    /// Sends a point-to-point response to a specific client.
    void reply(quint64 clientId, const QJsonObject &message);

    /// Sends a point-to-point error envelope to a specific client (PROTOCOL.md §10).
    void replyError(quint64 clientId, const QString &code, const QString &message, bool retryable, const QString &id = QString());


private slots:
    void _onClientConnected(quint64 clientId);
    void _onTextMessageReceived(quint64 clientId, const QString &message);
    void _onClientDisconnected(quint64 clientId);

    /// Fans out WebBridge::tickReady() to every authenticated client (PROTOCOL.md §11.1: tick
    /// is unconditional, no subscription required).
    void _onTickReady(const QJsonObject &tick);

private:
    /// Per-connection protocol state.
    struct ClientState
    {
        bool authed = false;              ///< Set once a valid `hello` has been received (§1.1)
        QSet<QString> subscriptions;       ///< Stream keys (WebBridge::streamKey) this client is subscribed to (§2.2)
        /// §9.2 per-client keyframe gate: stream keys (video/streamId) for which this client has
        /// (re)subscribed but has not yet been forwarded a keyframe. While a key is present here,
        /// broadcastBinary() withholds non-keyframe binary frames for that stream from this client
        /// -- "a client joining mid-stream renders nothing until the first keyframe" (§9.2). Set on
        /// every subscribe to `video` (see _handleSubscription()); cleared the moment a keyframe is
        /// forwarded, or on unsubscribe.
        QSet<QString> videoPendingKeyframe;
    };

    /// Handles a `hello` message: validates the (non-empty, per §1.1) token and, if present, the
    /// major protocolVersion, then marks the client authenticated and replies `helloAck`.
    /// Replies `BAD_MESSAGE` for a missing/empty token, `UNSUPPORTED_VERSION` (+ close) for a
    /// protocolVersion major-version mismatch, or `AUTH_FAILED` (+ close) if a server token is
    /// configured (setAuthToken()) and the client's token does not match it.
    void _handleHello(quint64 clientId, const QJsonObject &obj);

    /// Handles `subscribe`/`unsubscribe` (§2.2): validates the channel, updates the client's
    /// subscription set, replies `subscribeAck`/`unsubscribeAck`, and (for subscribe) emits
    /// snapshotRequested(). Replies `BAD_MESSAGE` for a missing channel, `UNKNOWN_CHANNEL` for
    /// an unrecognized one, and `BAD_MESSAGE` for a vehicle-scoped channel (`telemetry`,
    /// `mission`) whose `vehicleId` field is omitted entirely (§3's envelope table: `vehicleId`
    /// is required "for vehicle-scoped messages"; §10: "missing required field").
    ///
    /// Deliberately does NOT reject a `vehicleId` that is merely not (yet) in the tracked vehicle
    /// set: PROTOCOL.md §3.1 documents this as an accepted late-binding subscribe (see the doc
    /// comment there) rather than `UNKNOWN_VEHICLE` -- the web client's own startup handshake
    /// (web/src/bridge/session.ts) subscribes telemetry/mission for its configured vehicle
    /// immediately after `hello`, routinely before a MockLink/real vehicle has finished attaching.
    void _handleSubscription(quint64 clientId, const QJsonObject &obj, bool subscribe);

    /// Handles a `command` message (PROTOCOL.md §5.1): validates that `id`, `vehicleId`, and
    /// `action` are present, replying `BAD_MESSAGE` (§10) if not, then emits commandReceived()
    /// for CommandChannel (B7b) to execute. This class does not itself know how to run guided
    /// actions -- validation here is limited to envelope shape, not action semantics/params.
    void _handleCommand(quint64 clientId, const QJsonObject &obj);

    /// Handles `missionUpload`/`missionDownload`/`missionClear` messages (PROTOCOL.md §7.2):
    /// validates that `id` and `vehicleId` are present, replying `BAD_MESSAGE` (§10) if not, then
    /// emits missionMessageReceived() for MissionChannel to execute. This class does not itself
    /// know mission item schema/semantics -- validation here is limited to envelope shape.
    void _handleMission(quint64 clientId, const QJsonObject &obj);

    /// Serializes @p obj as compact JSON and sends it as a single text frame (PROTOCOL.md §1:
    /// "server never fragments a JSON message across frames").
    void _sendJson(quint64 clientId, const QJsonObject &obj);

    /// Builds and sends the error envelope exactly per PROTOCOL.md §10. @p id is echoed when
    /// non-empty; omitted otherwise (unsolicited errors omit `id`).
    void _sendError(quint64 clientId, const QString &code, const QString &message, bool retryable, const QString &id = QString());

    /// Subscribable channels per PROTOCOL.md §2.2/§15: telemetry, mission, video, adsb, image.
    static bool _isKnownChannel(const QString &channel);

    static constexpr const char *kProtocolVersion = "0.1";           ///< PROTOCOL.md §1.1 helloAck/hello
    static constexpr const char *kServerVersion = "rtn-qgc 5.0";     ///< PROTOCOL.md §1.1 helloAck `serverVersion`

    // RFC 6455 §7.4 close codes used below -- kept as plain constants (rather than an enum from
    // a WS library header) so this file has no dependency on which WsTransport implementation is
    // compiled in.
    static constexpr quint16 kCloseNormal = 1000;
    static constexpr quint16 kCloseProtocolError = 1002;
    static constexpr quint16 kClosePolicyViolated = 1008;

    WebBridge *_bridge = nullptr;    ///< Not owned; must outlive this object
    QHostAddress _listenAddress = QHostAddress::LocalHost;    ///< Bind address for start() (--bridge-host override)
    QString _authToken;              ///< Required `hello` token (--bridge-token); empty = accept any non-empty token
    std::unique_ptr<WsTransport> _transport;
    QHash<quint64, ClientState> _clients;
    QHash<quint8, QJsonObject> _cachedVideoConfig;  ///< Last enveloped videoConfig message sent per streamId (§9.1), for late-subscriber replay
};
