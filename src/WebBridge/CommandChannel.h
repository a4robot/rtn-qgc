#pragma once

#include <functional>

#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QPair>
#include <QtCore/QString>

#include "GuidedActionGate.h"

class QTimer;
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
/// action completed". Whenever the underlying Vehicle call is verified (src/Vehicle/Vehicle.cc,
/// src/FirmwarePlugin/{PX4,APM}/*FirmwarePlugin.cc) to end in a plain Vehicle::sendMavCommand()/
/// sendMavCommandInt() with no custom ack handler -- so that Vehicle::mavCommandResult() is
/// guaranteed to fire for it -- this class defers the ack until that signal arrives (or a
/// conservative ~7s watchdog elapses) and populates `mavResult` from the real MAV_RESULT. See
/// _expectedMavCmdForAction() for the exact per-action, per-firmware table and the reasoning
/// behind each entry (several guided actions turn out to be flight-mode changes with no MAV_CMD
/// round trip at all, or route through a firmware-plugin-private ack handler that consumes the
/// result before it would ever reach us). Actions/firmware combinations not in that table keep
/// the old immediate `accepted` ack with no `mavResult`, exactly as before; completion for those
/// is still only observable by the client via telemetry / commandProgress.
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
    /// params/action semantics validated here). Always emits exactly one responseReady(): a
    /// `commandAck` (§5.2) for every outcome except an unknown vehicleId, which answers with a
    /// §10 `error` envelope (UNKNOWN_VEHICLE) instead -- consistent with FactChannel's handling
    /// of the same condition. Unknown/invalid action (a *known* vehicle, bad `action`/`params`)
    /// stays a rejected ack, not a §10 error (per: "Unknown action -> rejected ack, not a §10
    /// error").
    void handleCommand(quint64 clientToken, const QJsonObject &request);

signals:
    /// One `commandAck` message (§5.2) addressed to @p clientToken. Connect to
    /// WebBridgeServer::sendToClient() so the response reaches only the requester.
    void responseReady(quint64 clientToken, const QJsonObject &message);

private:
    /// Builds a `commandAck` (§5.2). `reason` is included only when !accepted. `mavResult` is
    /// included only when @p mavResult is a real MAV_RESULT value (>= 0); the sentinel default
    /// (-1) means "the autopilot did not answer" (or this action is not correlated at all), in
    /// which case §5.2's "mavResult ... when the autopilot answered" is honored by omitting it.
    static QJsonObject _makeAck(const QString &id, int vehicleId, bool accepted, const QString &reason = QString(), int mavResult = -1);

    /// Builds a §10 error envelope ({type: "error", id, code, message, vehicleId, retryable}).
    /// Used for `command` requests targeting a vehicleId this channel has no Vehicle for --
    /// consistent with FactChannel's getParam/setParam handling of the identical condition
    /// (PROTOCOL.md §10's UNKNOWN_VEHICLE: "vehicleId not connected"), unlike every other
    /// rejection in this class (which stays a plain rejected `commandAck` per the class doc's
    /// "unknown action -> rejected ack, not a §10 error" convention -- this one case is different
    /// because the failure is about the envelope's addressing, not the action itself).
    static QJsonObject _makeUnknownVehicleError(const QString &id, int vehicleId);

    /// (vehicleId, MAV_CMD) -- the correlation key for a single in-flight request. At most one
    /// pending entry may exist per key at a time (see _dispatch()); a second request that maps
    /// to the same key is rejected up front with "Command already in flight" rather than being
    /// queued or mis-correlated against the first one's eventual ack.
    using PendingKey = QPair<int, int>;

    /// Bookkeeping for one request awaiting Vehicle::mavCommandResult() for `expectedCommand` on
    /// `vehicleId`. Owns `timeoutTimer` (parented to this CommandChannel).
    struct PendingCommand {
        QString id;
        quint64 clientToken = 0;
        int vehicleId = 0;
        int expectedCommand = 0;
        QTimer *timeoutTimer = nullptr;
    };

    /// ~7s: deliberately conservative and above Vehicle's own ack-timeout/retry machinery
    /// (MavCommandQueue's per-try ack window is on the order of a second, and most guided-action
    /// MAV_CMDs are sent with maxTries == 1 for safety -- see MavCommandQueue::_shouldRetry()) so
    /// that a real MavCmdResultFailureNoResponseToCommand normally arrives well before this fires.
    static constexpr int kCorrelationTimeoutMs = 7000;

    /// Returns the MAV_CMD that @p action will produce a terminal Vehicle::mavCommandResult()
    /// for on @p vehicle, or -1 if this action/firmware combination never yields one (immediate
    /// ack should be used instead). See the class doc and the per-branch comments in the .cc for
    /// the Vehicle.cc / FirmwarePlugin.cc code paths this is derived from.
    static int _expectedMavCmdForAction(const QString &action, const Vehicle *vehicle);

    /// Registers a pending correlation entry for (vehicle->id(), expectedCommand) and arms its
    /// timeout timer. Must be called *before* invoking the Vehicle guided-action API that sends
    /// the MAV_CMD: MavCommandQueue::sendWorker() can emit mavCommandResult synchronously
    /// (duplicate-command or no-primary-link rejection) before the call even returns, and
    /// FirmwarePlugin::_setFlightModeAndValidate() pumps the Qt event loop internally, so a real
    /// ack can also arrive re-entrantly while the guided-action call is still on the stack.
    void _beginCorrelated(quint64 clientToken, const QString &id, Vehicle *vehicle, int expectedCommand);

    /// Removes a pending entry without answering it -- used when the guided-action call itself
    /// reports synchronous, pre-send rejection (e.g. Vehicle::guidedModeGotoLocation() returning
    /// false for an out-of-range location), so the caller can answer once with its own reason
    /// instead of also waiting out the timeout for a MAV_CMD that was never sent.
    void _cancelCorrelated(const PendingKey &key);

    /// Dispatches a guided action: @p sendFn must synchronously invoke the Vehicle API. If
    /// @p expectedMavCmd is -1, replies with an immediate accepted ack exactly as before this
    /// change. Otherwise rejects up front on a same-key command already in flight, or defers the
    /// ack to _onMavCommandResult()/the timeout.
    void _dispatch(quint64 clientToken, const QString &id, Vehicle *vehicle, int expectedMavCmd, const std::function<void()> &sendFn);

    void _handleArmDisarm(quint64 clientToken, const QString &id, Vehicle *vehicle, bool arm);
    void _handleTakeoff(quint64 clientToken, const QString &id, Vehicle *vehicle, const QJsonObject &params);
    void _handleLand(quint64 clientToken, const QString &id, Vehicle *vehicle);
    void _handleRtl(quint64 clientToken, const QString &id, Vehicle *vehicle);
    void _handleGotoLocation(quint64 clientToken, const QString &id, Vehicle *vehicle, const QJsonObject &params);
    void _handleSetFlightMode(quint64 clientToken, const QString &id, Vehicle *vehicle, const QJsonObject &params);
    void _handlePause(quint64 clientToken, const QString &id, Vehicle *vehicle);

private slots:
    /// Connected (Qt::UniqueConnection) to each vehicle's Vehicle::mavCommandResult() the first
    /// time we correlate against it. Completes and removes the matching _pending entry, if any;
    /// mavCommandResult() for a command we are not waiting on (or already answered/timed out) is
    /// silently ignored, since this signal is shared with the firmware plugins' own internal
    /// listeners (e.g. PX4FirmwarePlugin's post-takeoff auto-arm) and other Vehicle consumers.
    void _onMavCommandResult(int vehicleId, int targetComponent, int command, int ackResult, int failureCode);

private:
    WebBridge *_bridge = nullptr;    ///< Not owned
    GuidedActionGate _gate;          ///< Reused across requests; setVehicle() re-points it per request
    QHash<PendingKey, PendingCommand> _pending; ///< In-flight correlated requests, keyed by (vehicleId, MAV_CMD)
};
