#pragma once

#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QTimer>

class Vehicle;
class WebBridge;

Q_DECLARE_LOGGING_CATEGORY(TelemetryChannelLog)

/// Turns live Vehicle state into `telemetry` channel messages per the WebBridge websocket
/// protocol contract (src/WebBridge/PROTOCOL.md §4). Tracks every vehicle known to
/// MultiVehicleManager and, while running, emits one full-snapshot telemetry message per
/// tracked vehicle at ~10 Hz (PROTOCOL.md §4: "Every message is a full snapshot of the fields
/// below ... v0.1 always sends the complete object").
///
/// This class has no QtWebSockets/QtQml/QtQuick dependencies (Qt Core + Positioning only, per
/// src/WebBridge/CMakeLists.txt) so it can back non-QML frontends. It does not send anything
/// itself; callers connect telemetryReady() to the actual per-client send path (e.g.
/// WebBridgeServer, added in B1b).
class TelemetryChannel : public QObject
{
    Q_OBJECT

public:
    /// @param bridge Not owned. Used for the server clock / seq bookkeeping via
    ///                WebBridge::makeStreamMessage() and WebBridge::resetSeq()
    ///                (PROTOCOL.md §2.4). Must outlive this object.
    explicit TelemetryChannel(WebBridge *bridge, QObject *parent = nullptr);
    ~TelemetryChannel() override;

public slots:
    /// Connectable to WebBridgeServer::snapshotRequested(channel, vehicleId). Resets the
    /// telemetry stream's sequence counter (PROTOCOL.md §2.4/§11.2) and immediately emits the
    /// vehicle's current full telemetry state as the snapshot (seq 1, PROTOCOL.md §2.2:
    /// "the snapshot is exactly the first message after (re)subscribe").
    ///
    /// Requests for any channel other than "telemetry", and requests for a vehicleId this
    /// class isn't tracking, are ignored: WebBridgeServer is expected to have already validated
    /// the (channel, vehicleId) pair against UNKNOWN_CHANNEL/UNKNOWN_VEHICLE (PROTOCOL.md §3.1)
    /// before routing here, so this is a defensive no-op rather than an error path.
    void sendSnapshot(const QString &channel, int vehicleId);

signals:
    /// One `telemetry` channel message (PROTOCOL.md §3/§4), already wrapped by
    /// WebBridge::makeStreamMessage() with the envelope fields (`type`, `channel`, `seq`,
    /// `snapshot`, `timeUs`, `vehicleId`). `channel` is always "telemetry". Emitted at ~10 Hz
    /// per tracked vehicle while running, plus once immediately from sendSnapshot().
    void telemetryReady(const QString &channel, int vehicleId, const QJsonObject &message);

private slots:
    void _onVehicleAdded(Vehicle *vehicle);
    void _onVehicleRemoved(Vehicle *vehicle);
    void _syncBridgeVehicleIds();
    void _sendPeriodicTelemetry();

private:
    static constexpr int kTelemetryIntervalMs = 100;    ///< PROTOCOL.md §4: ~10 Hz

    /// Builds the `telemetry` payload fields (attitude/position/velocity/battery/gps/
    /// flightMode/armed, PROTOCOL.md §4) for one vehicle. `vehicle` may be null (returns an
    /// empty object); every dereference inside is null-checked independently since sub-objects
    /// (GPS fact group, battery fact group) may also be absent.
    QJsonObject _buildPayload(Vehicle *vehicle) const;

    WebBridge *_bridge = nullptr;             ///< Not owned
    QTimer _telemetryTimer;                   ///< Drives the ~10 Hz per-vehicle stream
    QHash<int, Vehicle *> _vehicles;          ///< vehicleId -> Vehicle (not owned)
};
