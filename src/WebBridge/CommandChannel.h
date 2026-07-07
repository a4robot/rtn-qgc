#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QString>

#include "GuidedActionGate.h"

class Vehicle;
class WebBridge;

Q_DECLARE_LOGGING_CATEGORY(CommandChannelLog)

/// Executes `command` channel requests (PROTOCOL.md §5) against the live vehicle set and
/// answers the requesting client with exactly one `commandAck` (§5.2), as required by §5.2:
/// "exactly one commandAck per request". Bridges WebBridgeServer::commandReceived() (already
/// validated for id/vehicleId/action presence, PROTOCOL.md §5.1) to the Vehicle guided-action
/// API (src/Vehicle/Vehicle.h) for arm/disarm/takeoff/land/rtl/gotoLocation/setFlightMode/pause,
/// consulting GuidedActionGate (the ported QML `show*` predicates) to reject actions that are
/// not currently legal for the resolved vehicle.
///
/// v0.1 semantics per §5.2: "accepted means the autopilot accepted the command, not that the
/// action completed". This class sends `accepted` once the guided-mode call has been handed to
/// the firmware plugin layer (Vehicle::guidedMode*()); it does not wait for or correlate
/// Vehicle::mavCommandResult(), so `mavResult` is never populated on our acks (completion is
/// tracked by the client via telemetry / commandProgress, not implemented here either).
///
/// Qt Core + Positioning only (QGeoCoordinate for gotoLocation) -- no QtQml/QtQuick, matching
/// the rest of this module (src/WebBridge/CMakeLists.txt). In particular this class never
/// includes QtWebSockets: responses are addressed by the opaque client token WebBridgeServer
/// assigns per connection, not by QWebSocket*.
class CommandChannel : public QObject
{
    Q_OBJECT

public:
    /// @param bridge Not owned; must outlive this object. Not currently used for message
    ///                shaping (commandAck/commandProgress carry no seq/snapshot envelope per
    ///                §5, unlike the streamed channels) but accepted per this module's
    ///                established constructor shape (see TelemetryChannel) and kept for future
    ///                use (e.g. serverTimeUs()-stamped progress messages).
    explicit CommandChannel(WebBridge *bridge, QObject *parent = nullptr);
    ~CommandChannel() override;

public slots:
    /// Connectable to WebBridgeServer::commandReceived(quint64, QJsonObject). @p request is the
    /// full §5.1 envelope (id/vehicleId/action already validated non-empty by WebBridgeServer;
    /// params/action semantics validated here). Always emits exactly one responseReady() with a
    /// `commandAck` (§5.2) -- never a §10 error envelope; unknown/invalid input is expressed as
    /// a rejected ack instead (per assignment: "Unknown action -> rejected ack, not a §10
    /// error").
    void handleCommand(quint64 clientToken, const QJsonObject &request);

signals:
    /// One `commandAck` message (§5.2) addressed to @p clientToken. Connect to
    /// WebBridgeServer::sendToClient() so the response reaches only the requester.
    void responseReady(quint64 clientToken, const QJsonObject &message);

private:
    /// Builds a `commandAck` (§5.2). `reason` is included only when !accepted; `mavResult` is
    /// never included (see class doc -- v0.1 does not correlate autopilot acks here).
    static QJsonObject _makeAck(const QString &id, int vehicleId, bool accepted, const QString &reason = QString());

    void _handleArmDisarm(quint64 clientToken, const QString &id, Vehicle *vehicle, bool arm);
    void _handleTakeoff(quint64 clientToken, const QString &id, Vehicle *vehicle, const QJsonObject &params);
    void _handleLand(quint64 clientToken, const QString &id, Vehicle *vehicle);
    void _handleRtl(quint64 clientToken, const QString &id, Vehicle *vehicle);
    void _handleGotoLocation(quint64 clientToken, const QString &id, Vehicle *vehicle, const QJsonObject &params);
    void _handleSetFlightMode(quint64 clientToken, const QString &id, Vehicle *vehicle, const QJsonObject &params);
    void _handlePause(quint64 clientToken, const QString &id, Vehicle *vehicle);

    WebBridge *_bridge = nullptr;    ///< Not owned
    GuidedActionGate _gate;          ///< Reused across requests; setVehicle() re-points it per request
};
