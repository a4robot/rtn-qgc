#include "CommandChannel.h"

#include <QtCore/QJsonValue>
#include <QtCore/QTimer>
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

/// Human-readable reason for a rejected commandAck when the autopilot did answer (mavResult is a
/// real MAV_RESULT). Vehicle::mavCommandResult() carrying MAV_RESULT_ACCEPTED never reaches this
/// (see CommandChannel::_onMavCommandResult()); MAV_RESULT_IN_PROGRESS never reaches it either
/// (MavCommandQueue only emits commandResult() for terminal results).
QString mavResultReason(int ackResult)
{
    switch (ackResult) {
    case MAV_RESULT_TEMPORARILY_REJECTED:
        return QStringLiteral("Command temporarily rejected by autopilot");
    case MAV_RESULT_DENIED:
        return QStringLiteral("Command denied by autopilot");
    case MAV_RESULT_UNSUPPORTED:
        return QStringLiteral("Command not supported by autopilot");
    case MAV_RESULT_FAILED:
        return QStringLiteral("Command failed on autopilot");
    case MAV_RESULT_CANCELLED:
        return QStringLiteral("Command cancelled");
    default:
        return QStringLiteral("Command rejected by autopilot (MAV_RESULT=%1)").arg(ackResult);
    }
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

QJsonObject CommandChannel::_makeAck(const QString &id, int vehicleId, bool accepted, const QString &reason, int mavResult)
{
    // PROTOCOL.md §5.2: "Every response carries status (accepted|rejected), a human-readable
    // reason when rejected, and mavResult (integer MAV_RESULT value) when the autopilot
    // answered." mavResult is only included when the caller passed a real MAV_RESULT (>= 0);
    // the -1 sentinel default covers both "this action is never correlated" and "no answer
    // arrived before the correlation timeout".
    QJsonObject ack;
    ack[QStringLiteral("type")] = QStringLiteral("commandAck");
    ack[QStringLiteral("id")] = id;
    ack[QStringLiteral("vehicleId")] = vehicleId;
    ack[QStringLiteral("status")] = accepted ? QStringLiteral("accepted") : QStringLiteral("rejected");
    if (!accepted) {
        ack[QStringLiteral("reason")] = reason;
    }
    if (mavResult >= 0) {
        ack[QStringLiteral("mavResult")] = mavResult;
    }
    return ack;
}

QJsonObject CommandChannel::_makeUnknownVehicleError(const QString &id, int vehicleId)
{
    // PROTOCOL.md §10 error envelope, matching FactChannel::handleMessage()'s UNKNOWN_VEHICLE
    // shape exactly (code/message/retryable). Not retryable: the vehicleId in this specific
    // request will never become valid for retry purposes -- a client that wants this vehicle
    // should wait for it to appear in tick's vehicleIds (§3.1) and issue a fresh request with the
    // same id semantics reused per §11.2's "new ids" guidance.
    QJsonObject err;
    err[QStringLiteral("type")] = QStringLiteral("error");
    err[QStringLiteral("id")] = id;
    err[QStringLiteral("code")] = QStringLiteral("UNKNOWN_VEHICLE");
    err[QStringLiteral("message")] = QStringLiteral("Unknown vehicle");
    err[QStringLiteral("vehicleId")] = vehicleId;
    err[QStringLiteral("retryable")] = false;
    return err;
}

int CommandChannel::_expectedMavCmdForAction(const QString &action, const Vehicle *vehicle)
{
    // Per-action, per-firmware table of which MAV_CMD (if any) is guaranteed to produce a
    // terminal Vehicle::mavCommandResult() we can correlate against. Derived from reading the
    // actual send paths in src/Vehicle/Vehicle.cc and src/FirmwarePlugin/{PX4,APM}/*.cc -- see
    // per-branch comments. Returns -1 when the action never yields a correlatable result for
    // this vehicle's firmware, in which case _dispatch() falls back to the old immediate ack.
    if (!vehicle) {
        return -1;
    }

    if (action == QStringLiteral("arm") || action == QStringLiteral("disarm")) {
        // Vehicle::setArmed() (Vehicle.cc) always sends a plain sendMavCommand(...,
        // MAV_CMD_COMPONENT_ARM_DISARM, ...) with no custom result handler, on every firmware.
        return MAV_CMD_COMPONENT_ARM_DISARM;
    }

    if (action == QStringLiteral("takeoff")) {
        // PX4FirmwarePlugin::guidedModeTakeoff() and APMFirmwarePlugin::_guidedModeTakeoff() both
        // end with a plain sendMavCommand(..., MAV_CMD_NAV_TAKEOFF, ...). (PX4 additionally
        // attaches its own internal handler that watches for this same command/result pair to
        // fire a *second*, separate MAV_CMD_COMPONENT_ARM_DISARM -- it does not consume or
        // suppress the TAKEOFF result itself, so mavCommandResult still fires for TAKEOFF.)
        // Generic/unknown firmware's base FirmwarePlugin::guidedModeTakeoff() is a no-op that
        // never sends anything.
        if (vehicle->px4Firmware() || vehicle->apmFirmware()) {
            return MAV_CMD_NAV_TAKEOFF;
        }
        return -1;
    }

    if (action == QStringLiteral("land") || action == QStringLiteral("rtl")) {
        // Both guidedModeLand()/guidedModeRTL() route through
        // FirmwarePlugin::_setFlightModeAndValidate(), which sends MAV_CMD_DO_SET_MODE via
        // sendMavCommand() only when MAV_CMD_DO_SET_MODE_is_supported() is true --
        // APMFirmwarePlugin overrides this to true, PX4FirmwarePlugin does not override it (base
        // FirmwarePlugin default is false), so PX4 instead sends a raw, un-acked legacy SET_MODE
        // message (mavlink_msg_set_mode_pack_chan) with no COMMAND_ACK at all. Only APM is
        // correlatable here.
        if (vehicle->apmFirmware()) {
            return MAV_CMD_DO_SET_MODE;
        }
        return -1;
    }

    if (action == QStringLiteral("pause")) {
        // PX4FirmwarePlugin::pauseVehicle() sends a plain sendMavCommand(...,
        // MAV_CMD_DO_REPOSITION, ...) (no custom handler). APMFirmwarePlugin::pauseVehicle()
        // instead goes through _setFlightModeAndValidate() like land/rtl above, so it is
        // correlatable via MAV_CMD_DO_SET_MODE on APM, not MAV_CMD_DO_REPOSITION.
        if (vehicle->px4Firmware()) {
            return MAV_CMD_DO_REPOSITION;
        }
        if (vehicle->apmFirmware()) {
            return MAV_CMD_DO_SET_MODE;
        }
        return -1;
    }

    if (action == QStringLiteral("gotoLocation")) {
        // PX4FirmwarePlugin::guidedModeGotoLocation() sends a plain sendMavCommandInt()/
        // sendMavCommand() with MAV_CMD_DO_REPOSITION and no custom handler, so it is
        // correlatable -- but note this is the *same* MAV_CMD as "pause" on PX4 (see
        // _dispatch()'s "already in flight" rejection, which also protects this collision).
        // APMFirmwarePlugin::guidedModeGotoLocation() instead attaches its own private
        // sendMavCommandIntWithHandler() result handler that consumes the ack itself before it
        // would ever reach Vehicle::mavCommandResult(), so APM is not correlatable here.
        if (vehicle->px4Firmware()) {
            return MAV_CMD_DO_REPOSITION;
        }
        return -1;
    }

    // setFlightMode: per the task brief, always keep the immediate ack (also true in practice --
    // Vehicle::setFlightMode() either sends MAV_CMD_DO_SET_MODE, whose correlation would be
    // ambiguous against a client-requested arbitrary mode change racing GuidedActionGate-driven
    // land/rtl/pause on APM, or a raw unacked legacy SET_MODE message on PX4).
    return -1;
}

void CommandChannel::_beginCorrelated(quint64 clientToken, const QString &id, Vehicle *vehicle, int expectedCommand)
{
    connect(vehicle, &Vehicle::mavCommandResult, this, &CommandChannel::_onMavCommandResult, Qt::UniqueConnection);

    const PendingKey key(vehicle->id(), expectedCommand);
    PendingCommand pending;
    pending.id = id;
    pending.clientToken = clientToken;
    pending.vehicleId = vehicle->id();
    pending.expectedCommand = expectedCommand;
    pending.timeoutTimer = new QTimer(this);
    pending.timeoutTimer->setSingleShot(true);
    connect(pending.timeoutTimer, &QTimer::timeout, this, [this, key]() {
        // Defensive: _onMavCommandResult() may have already handled and erased this entry in the
        // same event-loop turn the timer was going to fire in (Qt guarantees a QTimer::timeout()
        // that fires after the entry is already gone is simply a no-op here, not a double-ack).
        const auto it = _pending.find(key);
        if (it == _pending.end()) {
            return;
        }
        const PendingCommand timedOut = it.value();
        _pending.erase(it);
        timedOut.timeoutTimer->deleteLater();

        // §5.2 requires exactly one commandAck per request; a silent timeout with no ack at all
        // would violate that, and re-emitting the original "accepted" would misreport an unknown
        // outcome as success. Reject with no mavResult (the autopilot itself never answered) --
        // this is more protocol-faithful than guessing.
        qCDebug(CommandChannelLog) << "correlation timeout: vehicle" << timedOut.vehicleId << "command" << timedOut.expectedCommand << "id" << timedOut.id;
        emit responseReady(timedOut.clientToken, _makeAck(timedOut.id, timedOut.vehicleId, false, QStringLiteral("No response from autopilot")));
    });
    _pending.insert(key, pending);
    pending.timeoutTimer->start(kCorrelationTimeoutMs);
}

void CommandChannel::_cancelCorrelated(const PendingKey &key)
{
    const auto it = _pending.find(key);
    if (it == _pending.end()) {
        return;
    }
    it.value().timeoutTimer->stop();
    it.value().timeoutTimer->deleteLater();
    _pending.erase(it);
}

void CommandChannel::_dispatch(quint64 clientToken, const QString &id, Vehicle *vehicle, int expectedMavCmd, const std::function<void()> &sendFn)
{
    if (expectedMavCmd < 0) {
        // Not correlatable for this action/firmware: preserve the original v0.1 behavior of
        // acking immediately once the autopilot-bound send call has been made.
        sendFn();
        emit responseReady(clientToken, _makeAck(id, vehicle->id(), true));
        return;
    }

    const PendingKey key(vehicle->id(), expectedMavCmd);
    if (_pending.contains(key)) {
        emit responseReady(clientToken, _makeAck(id, vehicle->id(), false, QStringLiteral("Command already in flight")));
        return;
    }

    // Register before sending: MavCommandQueue::sendWorker() can emit mavCommandResult()
    // synchronously (duplicate-command / no-primary-link) before sendFn() returns, and
    // FirmwarePlugin::_setFlightModeAndValidate() pumps the event loop internally, so a real ack
    // can also arrive re-entrantly while sendFn() is still on the stack.
    _beginCorrelated(clientToken, id, vehicle, expectedMavCmd);
    sendFn();
}

void CommandChannel::_onMavCommandResult(int vehicleId, int targetComponent, int command, int ackResult, int failureCode)
{
    Q_UNUSED(targetComponent);

    const PendingKey key(vehicleId, command);
    const auto it = _pending.find(key);
    if (it == _pending.end()) {
        // Not one of ours -- Vehicle::mavCommandResult() is a shared signal (firmware plugins'
        // own internal listeners, e.g. PX4FirmwarePlugin's post-takeoff auto-arm, and any other
        // Vehicle consumer all see the same emission).
        return;
    }

    const PendingCommand pending = it.value();
    _pending.erase(it);
    pending.timeoutTimer->stop();
    pending.timeoutTimer->deleteLater();

    if (failureCode == Vehicle::MavCmdResultFailureDuplicateCommand) {
        // MavCommandQueue::sendWorker() detected a duplicate in-flight send of the same command
        // to the same vehicle -- can arrive synchronously, before our own sendFn() call in
        // _dispatch() has even returned (see _beginCorrelated()'s doc comment).
        emit responseReady(pending.clientToken, _makeAck(pending.id, pending.vehicleId, false, QStringLiteral("Command already in flight")));
        return;
    }
    if (failureCode == Vehicle::MavCmdResultFailureNoResponseToCommand) {
        // MavCommandQueue exhausted its own retries with no primary link / no ack at all --
        // functionally the same client-facing outcome as our own correlation timeout, just
        // reported earlier and more precisely.
        emit responseReady(pending.clientToken, _makeAck(pending.id, pending.vehicleId, false, QStringLiteral("No response from autopilot")));
        return;
    }

    // failureCode == MavCmdResultCommandResultOnly: a genuine terminal COMMAND_ACK. ackResult is
    // never MAV_RESULT_IN_PROGRESS here (MavCommandQueue::handleCommandAck() only ever emits
    // commandResult() for terminal results).
    if (ackResult == MAV_RESULT_ACCEPTED) {
        emit responseReady(pending.clientToken, _makeAck(pending.id, pending.vehicleId, true, QString(), ackResult));
    } else {
        emit responseReady(pending.clientToken, _makeAck(pending.id, pending.vehicleId, false, mavResultReason(ackResult), ackResult));
    }
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
        // §10 error envelope (UNKNOWN_VEHICLE), not a rejected commandAck -- see _makeUnknownVehicleError()'s
        // doc comment for why this one case differs from the class's usual "rejected ack, not a
        // §10 error" convention.
        qCDebug(CommandChannelLog) << "handleCommand: unknown vehicle" << vehicleId << "for action" << action;
        emit responseReady(clientToken, _makeUnknownVehicleError(id, vehicleId));
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
    //
    // Correlated: Vehicle::setArmed() always sends a plain MAV_CMD_COMPONENT_ARM_DISARM (see
    // _expectedMavCmdForAction()); the ack is deferred until Vehicle::mavCommandResult() answers.
    _dispatch(clientToken, id, vehicle, _expectedMavCmdForAction(arm ? QStringLiteral("arm") : QStringLiteral("disarm"), vehicle), [vehicle, arm]() {
        vehicle->setArmed(arm, false);
    });
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

    // Correlated on PX4/APM (MAV_CMD_NAV_TAKEOFF), immediate otherwise -- see
    // _expectedMavCmdForAction(). Note PX4 additionally auto-arms via a second, separate
    // MAV_CMD_COMPONENT_ARM_DISARM after this one resolves; that second command is not tracked
    // here (it was not requested by this client and has no correlatable request of its own).
    _dispatch(clientToken, id, vehicle, _expectedMavCmdForAction(QStringLiteral("takeoff"), vehicle), [vehicle, alt]() {
        vehicle->guidedModeTakeoff(alt);
    });
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

    // Correlated on APM only (MAV_CMD_DO_SET_MODE via _setFlightModeAndValidate()); PX4 sends an
    // unacked legacy SET_MODE message with no COMMAND_ACK at all, so PX4 keeps the immediate ack.
    // See _expectedMavCmdForAction().
    _dispatch(clientToken, id, vehicle, _expectedMavCmdForAction(QStringLiteral("land"), vehicle), [vehicle]() {
        vehicle->guidedModeLand();
    });
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
    // always issue a plain RTL. Correlated on APM only, same reasoning as land above.
    _dispatch(clientToken, id, vehicle, _expectedMavCmdForAction(QStringLiteral("rtl"), vehicle), [vehicle]() {
        vehicle->guidedModeRTL(false);
    });
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

    // Bespoke (not the generic _dispatch() helper): guidedModeGotoLocation() returns a
    // synchronous bool telling us whether the send was even attempted, so a correlation entry
    // (if applicable) must be registered first and unwound if that returns false, rather than
    // being decided purely by expectedMavCmd the way the other handlers' fire-and-forget sendFn
    // lambdas are.
    const int expectedCommand = _expectedMavCmdForAction(QStringLiteral("gotoLocation"), vehicle);
    const PendingKey key(vehicle->id(), expectedCommand);
    if (expectedCommand >= 0) {
        if (_pending.contains(key)) {
            emit responseReady(clientToken, _makeAck(id, vehicle->id(), false, QStringLiteral("Command already in flight")));
            return;
        }
        // Register before sending -- see _dispatch()'s doc comment for why (synchronous/
        // re-entrant mavCommandResult emission from MavCommandQueue::sendWorker()).
        _beginCorrelated(clientToken, id, vehicle, expectedCommand);
    }

    const QGeoCoordinate gotoCoord(lat, lon, alt);
    if (!vehicle->guidedModeGotoLocation(gotoCoord)) {
        // Vehicle::guidedModeGotoLocation() itself returns false for e.g. an out-of-range
        // location or an invalid current/target coordinate (src/Vehicle/Vehicle.cc), i.e. the
        // MAV_CMD was never actually sent -- undo the pending registration (if any) so we answer
        // once with this reason instead of also waiting out the correlation timeout.
        if (expectedCommand >= 0) {
            _cancelCorrelated(key);
        }
        emit responseReady(clientToken, _makeAck(id, vehicle->id(), false, QStringLiteral("Goto command rejected by vehicle (location out of range or invalid)")));
        return;
    }

    if (expectedCommand < 0) {
        // Not correlatable for this firmware (e.g. ArduPilot's private ack-consuming result
        // handler, or generic/unknown firmware) -- immediate ack, as before this change.
        emit responseReady(clientToken, _makeAck(id, vehicle->id(), true));
    }
    // else: correlated (PX4 only) -- ack deferred to _onMavCommandResult()/the timeout.
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

    // Correlated on PX4 (MAV_CMD_DO_REPOSITION) and APM (MAV_CMD_DO_SET_MODE) -- see
    // _expectedMavCmdForAction(). Note the PX4 MAV_CMD_DO_REPOSITION key is shared with
    // gotoLocation; _dispatch()'s "already in flight" check also guards that collision.
    _dispatch(clientToken, id, vehicle, _expectedMavCmdForAction(QStringLiteral("pause"), vehicle), [vehicle]() {
        vehicle->pauseVehicle();
    });
}
