#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QTimer>

Q_DECLARE_LOGGING_CATEGORY(WebBridgeLog)

/// Core of the C++ side of the WebBridge websocket protocol defined in
/// src/WebBridge/PROTOCOL.md (v0.1). This base class owns the protocol mechanics that are
/// independent of the transport: the server clock (PROTOCOL.md §1/§11.1), per-stream sequence
/// counters (§2.4), the server→client stream envelope (§3), and the 1 Hz tick (§11.1).
///
/// It intentionally contains no QtWebSockets (added in B1b, which binds a QWebSocketServer to
/// listenPort() inside start()/stop()) and no channel implementations (added in B2a). Like the
/// rest of this module it has no QML/Quick dependencies so it can back non-QML frontends.
///
/// Stream keys: seq counters are contiguous per (channel, vehicleId) stream (PROTOCOL.md §2.4),
/// so nextSeq()/resetSeq() are keyed by a stream-key string. For non-vehicle channels the key is
/// the channel name; for vehicle-scoped channels use streamKey() to fold the vehicleId in.
/// makeStreamMessage() does this internally; callers only need streamKey() to resetSeq() a
/// vehicle-scoped stream on re-subscribe.
class WebBridge : public QObject
{
    Q_OBJECT

public:
    /// Default listen port per PROTOCOL.md §1 (ws://127.0.0.1:8877/).
    static constexpr quint16 kDefaultPort = 8877;

    /// Sentinel vehicleId for non-vehicle-scoped messages (`video`, `adsb`, `tick`), which omit
    /// the `vehicleId` envelope field per PROTOCOL.md §2.3.
    static constexpr int kNoVehicleId = -1;

    /// @param listenPort Websocket listen port (from --bridge-port). Stored for B1b; this base
    ///                   class does not open a socket.
    explicit WebBridge(quint16 listenPort = kDefaultPort, QObject *parent = nullptr);
    ~WebBridge() override;

    quint16 listenPort() const { return _listenPort; }
    bool isRunning() const { return _running; }

    /// Server timestamp in microseconds since Unix epoch, used for the envelope `timeUs` and
    /// tick `serverTimeUs` fields (PROTOCOL.md §1, §3, §11.1). Anchored to the wall clock once
    /// at construction and advanced by QElapsedTimer, so it is monotonic and immune to
    /// wall-clock steps (NTP, manual set) for the lifetime of the bridge.
    quint64 serverTimeUs() const;

    /// Whole seconds since start(), for the tick `uptimeS` field (PROTOCOL.md §11.1).
    /// Returns 0 while not running.
    quint64 uptimeS() const;

    /// Returns the next sequence number for the given stream key: monotonic, contiguous, and
    /// starting at 1 on first use or after resetSeq() (PROTOCOL.md §2.4).
    quint64 nextSeq(const QString &channel);

    /// Restarts the stream's sequence at 1 (next nextSeq() returns 1). Call on (re)subscribe so
    /// the snapshot goes out with `seq: 1` per PROTOCOL.md §2.4/§11.2.
    void resetSeq(const QString &channel);

    /// Builds the stream key for a (channel, vehicleId) pair: the channel name alone for
    /// kNoVehicleId, otherwise "<channel>/<vehicleId>" (PROTOCOL.md §2.4 stream identity).
    static QString streamKey(const QString &channel, int vehicleId);

    /// Builds a server→client stream message per PROTOCOL.md §3: the payload fields plus the
    /// envelope fields `type`, `channel`, `seq`, `snapshot`, `timeUs`, and `vehicleId` (omitted
    /// for kNoVehicleId). Consumes one sequence number from the stream's counter; `snapshot` is
    /// derived as (seq == 1), matching §2.2/§2.4 where the snapshot is exactly the first message
    /// after (re)subscribe. Envelope fields overwrite same-named payload keys.
    QJsonObject makeStreamMessage(const QString &channel, const QString &type, const QJsonObject &payload, int vehicleId = kNoVehicleId);

    /// Builds the 1 Hz tick message per PROTOCOL.md §11.1: `type`, `serverTimeUs`, `uptimeS`,
    /// `vehicleIds`. Ticks are unconditional (no subscription, no channel/seq envelope).
    QJsonObject makeTick() const;

    /// Sets the connected-vehicle id set published in the tick `vehicleIds` field
    /// (PROTOCOL.md §11.1). Empty until vehicle wiring lands (B2a).
    void setVehicleIds(const QList<int> &vehicleIds) { _vehicleIds = vehicleIds; }
    QList<int> vehicleIds() const { return _vehicleIds; }

    /// Starts the bridge: begins the 1 Hz tick (tickReady()) and emits started(). Idempotent.
    /// B1b extends this to bind the websocket server to listenPort().
    void start();

    /// Stops the bridge: stops the tick, clears all sequence counters (the server keeps no
    /// state across runs, PROTOCOL.md §11.2), and emits stopped(). Idempotent.
    void stop();

signals:
    void started();
    void stopped();

    /// Emitted at 1 Hz while running, carrying the makeTick() message (PROTOCOL.md §11.1).
    /// B1b connects this to the per-client send path.
    void tickReady(const QJsonObject &tick);

private slots:
    void _sendTick();

private:
    static constexpr int kTickIntervalMs = 1000;    ///< PROTOCOL.md §11.1: tick at 1 Hz

    const quint16 _listenPort;                      ///< From --bridge-port; bound by B1b
    bool _running = false;
    quint64 _epochAnchorUs = 0;                     ///< Wall-clock anchor for serverTimeUs()
    QElapsedTimer _monotonic;                       ///< Monotonic source for serverTimeUs()
    quint64 _startedTimeUs = 0;                     ///< serverTimeUs() at start(), for uptimeS()
    QTimer _tickTimer;
    QHash<QString, quint64> _seqCounters;           ///< Stream key → last issued seq (§2.4)
    QList<int> _vehicleIds;                         ///< Tick `vehicleIds` field (§11.1)
};
