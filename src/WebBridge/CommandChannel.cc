#include "CommandChannel.h"

#include <QtCore/QJsonValue>
#include <QtCore/QtNumeric>
#include <QtPositioning/QGeoCoordinate>

#include "MultiVehicleManager.h"
#include "QGCLoggingCategory.h"
#include "Vehicle.h"
#include "WebBridge.h"

QGC_LOGGING_CATEGORY(CommandChannelLog, "WebBridge.CommandChannel")

namespace {

/// Returns the double value of @p value if it is a finite JSON number, else NaN. Used to reject
/// missing/non-numeric/NaN/Inf command params (PROTOCOL.md §5.1 params are always plain numbers).
double numberOrNaN(const QJsonValue &value)
{
    if (!value.isDouble()) {
        return qQNaN();
    }
    const double d = value.toDouble();
    return qIsFinite(d) ? d : qQNaN();
}

} // namespace

CommandChannel::CommandChannel(WebBridge *bridge, QObject *parent)
    : QObject(parent)
    , _bridge(bridge)
{
    qCDebug(CommandChannelLog) << this << "bridge" << static_cast<void*>(_bridge);
}

CommandChannel::~CommandChannel()
{
    qCDebug(CommandChannelLog) << this;
}

QJsonObject CommandChannel::_makeAck(const QString &id, int vehicleId, bool accepted, const QString &reason)
{
    // PROTOCOL.md §5.2: "Every response carries status (accepted|rejected), a human-readable
    // reason when rejected, and mavResult (integer MAV_RESULT value) when the autopilot
    // answered." This class never correlates Vehicle::mavCommandResult() (see class doc), so
    // mavResult is always omitted here.
    QJsonObject ack;
    ack[QStringLiteral("type")] = QStringLiteral("commandAck");
    ack[QStringLiteral("id")] = id;
    ack[QStringLiteral("vehicleId")] = vehicleId;
    ack[QStringLiteral("status")] = accepted ? QStringLiteral("accepted") : QStringLiteral("rejected");
    if (!accepted) {
        ack[QStringLiteral("reason")] = reason;
    }
    return ack;
}

void CommandChannel::handleCommand(quint64 clientToken, const QJsonObject &request)
{
    // WebBridgeServer::_handleCommand() already validated id/vehicleId/action are present
    // (PROTOCOL.md §5.1) before emitting commandReceived(); re-derive them here without
    // re-validating presence.
    const QString id = request.value(QStringLiteral("id")).toString();
    const int vehicleId = request.value(QStringLiteral("vehicleId")).toInt();
    const QString action = request.value(QStringLiteral("action")).toString();
    const QJsonObject params = request.value(QStringLiteral("params")).toObject();

    MultiVehicleManager *multiVehicleManager = MultiVehicleManager::instance();
    Vehicle *vehicle = multiVehicleManager ? multiVehicleManager->getVehicleById(vehicleId) : nullptr;
    if (!vehicle) {
        qCDebug(CommandChannelLog) << "handleCommand: unknown vehicle" << vehicleId << "for action" << action;
        emit responseReady(clientToken, _makeAck(id, vehicleId, false, QStringLiteral("Unknown vehicle")));
        return;
    }

    if (action == QStringLiteral("arm")) {
        _handleArmDisarm(clientToken, id, vehicle, true);
    } else if (action == QStringLiteral("disarm")) {
        _handleArmDisarm(clientToken, id, vehicle, false);
    } else if (action == QStringLiteral("takeoff")) {
        _handleTakeoff(clientToken, id, vehicle, params);
    } else if (action == QStringLiteral("land")) {
        _handleLand(clientToken, id, vehicle);
    } else if (action == QStringLiteral("rtl")) {
        _handleRtl(clientToken, id, vehicle);
    } else if (action == QStringLiteral("gotoLocation")) {
        _handleGotoLocation(clientToken, id, vehicle, params);
    } else if (action == QStringLiteral("setFlightMode")) {
        _handleSetFlightMode(clientToken, id, vehicle, params);
    } else if (action == QStringLiteral("pause")) {
        _handlePause(clientToken, id, vehicle);
    } else {
        // Unknown action: rejected ack, not a §10 error (BAD_MESSAGE is reserved for envelope
        // shape problems, which WebBridgeServer already screens for).
        qCDebug(CommandChannelLog) << "handleCommand: unknown action" << action << "vehicle" << vehicleId;
        emit responseReady(clientToken, _makeAck(id, vehicleId, false, QStringLiteral("Unknown action: %1").arg(action)));
    }
}

void CommandChannel::_handleArmDisarm(quint64 clientToken, const QString &id, Vehicle *vehicle, bool arm)
{
    _gate.setVehicle(vehicle);
    const GuidedActionGate::GuidedAction action = arm ? GuidedActionGate::GuidedAction::Arm : GuidedActionGate::GuidedAction::Disarm;
    if (!_gate.isActionAvailable(action)) {
        QString reason;
        if (arm) {
            reason = vehicle->armed() ? QStringLiteral("Vehicle already armed")
                                       : QStringLiteral("Vehicle not armable (arming checks failed)");
        } else {
            reason = !vehicle->armed() ? QStringLiteral("Vehicle already disarmed")
                                        : QStringLiteral("Cannot disarm while flying");
        }
        emit responseReady(clientToken, _makeAck(id, vehicle->id(), false, reason));
        return;
    }

    // showError=false: this is a programmatic command channel, not a user-facing UI action --
    // failures are reported back to the client as protocol messages (telemetry/commandProgress),
    // not as a QGCApplication modal message box.
    vehicle->setArmed(arm, false);
    emit responseReady(clientToken, _makeAck(id, vehicle->id(), true));
}

void CommandChannel::_handleTakeoff(quint64 clientToken, const QString &id, Vehicle *vehicle, const QJsonObject &params)
{
    // PROTOCOL.md §5.1: takeoff params = { "alt": <m, relative> }.
    const double alt = numberOrNaN(params.value(QStringLiteral("alt")));
    if (qIsNaN(alt) || alt <= 0.0) {
        emit responseReady(clientToken, _makeAck(id, vehicle->id(), false, QStringLiteral("params.alt must be a positive number")));
        return;
    }

    _gate.setVehicle(vehicle);
    if (!_gate.isActionAvailable(GuidedActionGate::GuidedAction::Takeoff)) {
        const QString reason = vehicle->flying() ? QStringLiteral("Vehicle already flying")
                                                  : QStringLiteral("Vehicle not armable for takeoff (arming checks failed, or guided takeoff unsupported)");
        emit responseReady(clientToken, _makeAck(id, vehicle->id(), false, reason));
        return;
    }

    vehicle->guidedModeTakeoff(alt);
    emit responseReady(clientToken, _makeAck(id, vehicle->id(), true));
}

void CommandChannel::_handleLand(quint64 clientToken, const QString &id, Vehicle *vehicle)
{
    _gate.setVehicle(vehicle);
    if (!_gate.isActionAvailable(GuidedActionGate::GuidedAction::Land)) {
        const QString reason = !vehicle->armed() ? QStringLiteral("Vehicle not armed")
                                                  : QStringLiteral("Land not available in current mode");
        emit responseReady(clientToken, _makeAck(id, vehicle->id(), false, reason));
        return;
    }

    vehicle->guidedModeLand();
    emit responseReady(clientToken, _makeAck(id, vehicle->id(), true));
}

void CommandChannel::_handleRtl(quint64 clientToken, const QString &id, Vehicle *vehicle)
{
    _gate.setVehicle(vehicle);
    if (!_gate.isActionAvailable(GuidedActionGate::GuidedAction::RTL)) {
        const QString reason = !vehicle->flying() ? QStringLiteral("Vehicle not flying")
                                                   : QStringLiteral("RTL not available in current mode");
        emit responseReady(clientToken, _makeAck(id, vehicle->id(), false, reason));
        return;
    }

    // PROTOCOL.md §5.1 rtl has no params (smart-vs-plain RTL is not distinguished on the wire);
    // always issue a plain RTL.
    vehicle->guidedModeRTL(false);
    emit responseReady(clientToken, _makeAck(id, vehicle->id(), true));
}

void CommandChannel::_handleGotoLocation(quint64 clientToken, const QString &id, Vehicle *vehicle, const QJsonObject &params)
{
    // PROTOCOL.md §5.1: gotoLocation params = { "lat", "lon", "alt" }.
    const double lat = numberOrNaN(params.value(QStringLiteral("lat")));
    const double lon = numberOrNaN(params.value(QStringLiteral("lon")));
    const double alt = numberOrNaN(params.value(QStringLiteral("alt")));
    if (qIsNaN(lat) || qIsNaN(lon) || qIsNaN(alt) || lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
        emit responseReady(clientToken, _makeAck(id, vehicle->id(), false, QStringLiteral("params.lat/lon/alt must be finite numbers with lat in [-90,90] and lon in [-180,180]")));
        return;
    }

    _gate.setVehicle(vehicle);
    if (!_gate.isActionAvailable(GuidedActionGate::GuidedAction::Goto)) {
        emit responseReady(clientToken, _makeAck(id, vehicle->id(), false, QStringLiteral("Vehicle not flying")));
        return;
    }

    const QGeoCoordinate gotoCoord(lat, lon, alt);
    if (!vehicle->guidedModeGotoLocation(gotoCoord)) {
        // Vehicle::guidedModeGotoLocation() itself returns false for e.g. an out-of-range
        // location or an invalid current/target coordinate (src/Vehicle/Vehicle.cc).
        emit responseReady(clientToken, _makeAck(id, vehicle->id(), false, QStringLiteral("Goto command rejected by vehicle (location out of range or invalid)")));
        return;
    }

    emit responseReady(clientToken, _makeAck(id, vehicle->id(), true));
}

void CommandChannel::_handleSetFlightMode(quint64 clientToken, const QString &id, Vehicle *vehicle, const QJsonObject &params)
{
    // PROTOCOL.md §5.1: setFlightMode params = { "mode": "<firmware mode name>" }, "same
    // namespace as telemetry flightMode" -- i.e. must be one of Vehicle::flightModes().
    const QString mode = params.value(QStringLiteral("mode")).toString();
    if (mode.isEmpty()) {
        emit responseReady(clientToken, _makeAck(id, vehicle->id(), false, QStringLiteral("params.mode is required")));
        return;
    }

    // GuidedActionGate has no show* predicate for SetFlightMode (QML showed the confirm dialog
    // unconditionally); GuidedActionGate::isActionAvailable(SetFlightMode) gates only on
    // vehicle != nullptr, which handleCommand() has already guaranteed here, so there is nothing
    // further to consult before validating the mode name itself.
    if (!vehicle->flightModes().contains(mode)) {
        emit responseReady(clientToken, _makeAck(id, vehicle->id(), false, QStringLiteral("Unknown flight mode: %1").arg(mode)));
        return;
    }

    vehicle->setFlightMode(mode);
    emit responseReady(clientToken, _makeAck(id, vehicle->id(), true));
}

void CommandChannel::_handlePause(quint64 clientToken, const QString &id, Vehicle *vehicle)
{
    _gate.setVehicle(vehicle);
    if (!_gate.isActionAvailable(GuidedActionGate::GuidedAction::Pause)) {
        QString reason;
        if (!vehicle->armed()) {
            reason = QStringLiteral("Vehicle not armed");
        } else if (!vehicle->flying()) {
            reason = QStringLiteral("Vehicle not flying");
        } else {
            reason = QStringLiteral("Pause not available (unsupported, already paused, or on final approach)");
        }
        emit responseReady(clientToken, _makeAck(id, vehicle->id(), false, reason));
        return;
    }

    vehicle->pauseVehicle();
    emit responseReady(clientToken, _makeAck(id, vehicle->id(), true));
}
