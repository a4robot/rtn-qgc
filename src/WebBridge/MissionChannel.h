#pragma once

#include <QtCore/QHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QString>

class MissionItem;
class MissionManager;
class QTimer;
class Vehicle;
class WebBridge;

Q_DECLARE_LOGGING_CATEGORY(MissionChannelLog)

/// Implements the `mission` channel per the WebBridge websocket protocol contract
/// (src/WebBridge/PROTOCOL.md §7): the missionState snapshot/update stream (§2, §7.2) plus the
/// missionUpload/missionDownload/missionClear request/response operations (§7.2).
///
/// Stream side follows the TelemetryChannel pattern (src/WebBridge/TelemetryChannel.{h,cc}):
/// tracks every vehicle known to MultiVehicleManager and, for each, connects to its
/// MissionManager (src/MissionManager/MissionManager.h / PlanManager.h) so that mission-item or
/// current-waypoint changes are pushed as a full-snapshot `missionState` message (PROTOCOL.md
/// §7.2: "the on-vehicle mission is state like any other").
///
/// Ops side follows the CommandChannel request-routing pattern (src/WebBridge/CommandChannel.{h,cc}):
/// WebBridgeServer validates the §7.2 envelope shape (id/vehicleId present) before emitting
/// missionMessageReceived(), which is connected to handleMissionMessage() here; this class owns
/// action/params semantics and answers with exactly one response via responseReady(), addressed
/// to the requesting client's opaque token.
///
/// missionUpload and missionClear are asynchronous (PlanManager::writeMissionItems()/removeAll()
/// complete via sendComplete()/removeAllComplete(), which carry no vehicle identity of their
/// own); this class correlates them by the MissionManager instance that emitted the signal
/// (see _managerToVehicleId) and allows at most one such op in flight per vehicle at a time
/// (see _pendingOps). missionDownload is answered synchronously from the already-tracked
/// MissionManager::missionItems() cache -- see handleMissionMessage()'s doc comment for why this
/// class does not itself trigger MissionManager::loadFromVehicle() for a download request.
///
/// Qt Core + Positioning only (QGeoCoordinate is not used directly here, but Vehicle.h pulls it
/// in transitively) -- no QtWebSockets, matching the rest of this module
/// (src/WebBridge/CMakeLists.txt).
class MissionChannel : public QObject
{
    Q_OBJECT

public:
    /// @param bridge Not owned. Used for the server clock / seq bookkeeping via
    ///                WebBridge::makeStreamMessage() and WebBridge::resetSeq()
    ///                (PROTOCOL.md §2.4). Must outlive this object.
    explicit MissionChannel(WebBridge *bridge, QObject *parent = nullptr);
    ~MissionChannel() override;

public slots:
    /// Connectable to WebBridgeServer::snapshotRequested(channel, vehicleId). Resets the
    /// mission stream's sequence counter (PROTOCOL.md §2.4/§11.2) and immediately emits the
    /// vehicle's current mission state as the snapshot (seq 1, PROTOCOL.md §2.2).
    ///
    /// Requests for any channel other than "mission", and requests for a vehicleId this class
    /// isn't tracking, are ignored: WebBridgeServer is expected to have already validated the
    /// (channel, vehicleId) pair against UNKNOWN_CHANNEL/UNKNOWN_VEHICLE (PROTOCOL.md §3.1)
    /// before routing here, so this is a defensive no-op rather than an error path.
    void sendSnapshot(const QString &channel, int vehicleId);

    /// Connectable to WebBridgeServer::missionMessageReceived(clientToken, request). @p request
    /// is the full §7.2 envelope (id/vehicleId already validated present by WebBridgeServer);
    /// `type` selects missionUpload/missionDownload/missionClear, each validated and dispatched
    /// here. Always emits exactly one responseReady() -- either a `missionItems` response
    /// (successful missionDownload) or a `missionAck` (§7.2: every other outcome, success or
    /// rejected) -- never a §10 error envelope; unknown message types or malformed items are
    /// expressed as a rejected missionAck instead, matching CommandChannel's "unknown action ->
    /// rejected ack, not a §10 error" convention.
    void handleMissionMessage(quint64 clientToken, const QJsonObject &request);

signals:
    /// One `missionState` channel message (PROTOCOL.md §3/§7.2), already wrapped by
    /// WebBridge::makeStreamMessage() with the envelope fields (`type`, `channel`, `seq`,
    /// `snapshot`, `timeUs`, `vehicleId`). `channel` is always "mission". Emitted whenever the
    /// tracked vehicle's mission items or current mission index change, plus once immediately
    /// from sendSnapshot().
    void missionStateReady(const QString &channel, int vehicleId, const QJsonObject &message);

    /// One `missionItems` or `missionAck` message (§7.2) addressed to @p clientToken. Connect to
    /// WebBridgeServer::sendToClient() so the response reaches only the requester.
    void responseReady(quint64 clientToken, const QJsonObject &message);

private slots:
    void _onVehicleAdded(Vehicle *vehicle);
    void _onVehicleRemoved(Vehicle *vehicle);

    /// Connected to PlanManager::newMissionItemsAvailable() (fires after MissionManager::
    /// loadFromVehicle() completes, e.g. triggered by InitialConnectStateMachine or the Plan
    /// view -- this class never calls loadFromVehicle() itself, see handleMissionMessage()).
    /// Republishes the stream for whichever vehicle owns the emitting MissionManager.
    void _onNewMissionItemsAvailable(bool removeAllRequested);

    /// Connected to PlanManager::currentIndexChanged(). Republishes the stream (currentSeq
    /// changed) for whichever vehicle owns the emitting MissionManager.
    void _onCurrentIndexChanged(int currentIndex);

    /// Connected to PlanManager::sendComplete(). Completes a pending missionUpload op (if this
    /// class registered one for the emitting MissionManager's vehicle); ignored otherwise, since
    /// PlanManager is shared with any other consumer in the process (e.g. the Plan view) and
    /// this signal reports the outcome of whichever write was actually in flight.
    void _onSendComplete(bool error);

    /// Connected to PlanManager::removeAllComplete(). Completes a pending missionClear op, same
    /// caveats as _onSendComplete().
    void _onRemoveAllComplete(bool error);

    /// Connected to PlanManager::error(). Caches the human-readable reason per vehicle so the
    /// next terminal signal (_onSendComplete()/_onRemoveAllComplete()) can attach it to a
    /// rejected missionAck; PlanManager always emits error() synchronously just before the
    /// corresponding sendComplete(true)/removeAllComplete(true) (see PlanManager.cc), so the
    /// cached value is fresh by the time it is consumed.
    void _onMissionManagerError(int errorCode, const QString &errorMsg);

    /// Fires ~15s after a missionUpload/missionClear was dispatched with no terminal signal
    /// (PlanManager itself silently no-ops in some edge cases -- see handleMissionMessage()'s
    /// upfront inProgress()/isOfflineEditingVehicle() checks for the ones this class can detect
    /// in advance, and the class doc for the ones it cannot). Answers with a rejected missionAck
    /// so exactly one response is still sent per request.
    void _onOpTimeout(int vehicleId);

private:
    enum class _OpType { Upload, Clear };

    /// Bookkeeping for one missionUpload/missionClear request awaiting a terminal PlanManager
    /// signal for `vehicleId`. Owns `timeoutTimer` (parented to this MissionChannel).
    struct PendingOp {
        QString id;
        quint64 clientToken = 0;
        _OpType type = _OpType::Upload;
        QTimer *timeoutTimer = nullptr;
    };

    static constexpr int kOpTimeoutMs = 15000;    ///< ~15s per op, per the task brief

    /// Builds the `missionState` payload ({currentSeq, items[]}, PROTOCOL.md §7.2) for @p
    /// vehicle's current mission and emits missionStateReady(). No-op if @p vehicle, its
    /// MissionManager, or _bridge is null.
    void _publishMissionState(Vehicle *vehicle);

    /// Converts a PlanManager::missionItems() list to the §7.1 wire item schema. Items are
    /// expected to already be in wire order (list position == `seq`); see the per-item mapping
    /// comment in the .cc.
    QJsonArray _itemsToJson(const QList<MissionItem *> &items) const;

    /// Parses and validates @p itemsJson against §7.1 (required fields, correct JSON types, lat
    /// in [-90,90]/lon in [-180,180], all doubles finite, `seq` contiguous from 0 matching array
    /// position -- see _handleMissionRequest() in PlanManager.cc, which indexes the write list
    /// by position, not by MissionItem::sequenceNumber()). On success returns true and fills @p
    /// outItems with newly-heap-allocated MissionItem objects (caller must either hand them to
    /// PlanManager::writeMissionItems(), which takes ownership, or delete them); on failure
    /// returns false, deletes anything already allocated, and fills @p errorReason.
    bool _parseAndValidateItems(const QJsonArray &itemsJson, QList<MissionItem *> &outItems, QString &errorReason) const;

    void _handleDownload(quint64 clientToken, const QString &id, Vehicle *vehicle);
    void _handleUpload(quint64 clientToken, const QString &id, Vehicle *vehicle, const QJsonObject &request);
    void _handleClear(quint64 clientToken, const QString &id, Vehicle *vehicle);

    /// Registers a pending op for @p vehicleId and arms its timeout timer. Must be called before
    /// invoking the PlanManager call that starts the transaction, matching CommandChannel's
    /// _beginCorrelated() reasoning: mirrors that a defensive, synchronous-first ordering is
    /// cheap insurance even though PlanManager's write/removeAll paths are not known to answer
    /// synchronously the way MavCommandQueue can.
    void _beginPendingOp(quint64 clientToken, const QString &id, int vehicleId, _OpType type);

    /// Completes and removes the pending op for @p vehicleId (if any), stopping its timer.
    /// Returns the completed entry via @p outOp; returns false (leaving @p outOp untouched) if
    /// there was none, or if one existed but did not match @p expectedType (the shared-signal
    /// case documented on _onSendComplete()/_onRemoveAllComplete()).
    bool _takePendingOp(int vehicleId, _OpType expectedType, PendingOp &outOp);

    /// Fails and removes a pending op (if any) for @p vehicleId with @p reason -- used on vehicle
    /// removal so a client awaiting an ack is not left hanging until the 15s timeout.
    void _failPendingOp(int vehicleId, const QString &reason);

    /// Builds a `missionAck` (§7.2). `reason` is included only when !accepted; `itemCount` is
    /// included only when @p itemCount >= 0 (missionAck always carries it on success per the
    /// examples, so callers pass a real count in that case).
    static QJsonObject _makeAck(const QString &id, int vehicleId, bool accepted, const QString &reason = QString(), int itemCount = -1);

    WebBridge *_bridge = nullptr;                          ///< Not owned
    QHash<int, Vehicle *> _vehicles;                        ///< vehicleId -> Vehicle (not owned)
    QHash<MissionManager *, int> _managerToVehicleId;       ///< Reverse lookup for signal correlation (sender())
    QHash<int, PendingOp> _pendingOps;                      ///< vehicleId -> in-flight missionUpload/missionClear (at most one each)
    QHash<int, QString> _lastError;                         ///< vehicleId -> most recent PlanManager::error() message
};
