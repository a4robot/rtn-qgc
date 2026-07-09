#include "MissionChannel.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonValue>
#include <QtCore/QTimer>
#include <QtCore/QtNumeric>

#include "MissionItem.h"
#include "MissionManager.h"
#include "MultiVehicleManager.h"
#include "QGCLoggingCategory.h"
#include "QmlObjectListModel.h"
#include "Vehicle.h"
#include "WebBridge.h"

QGC_LOGGING_CATEGORY(MissionChannelLog, "WebBridge.MissionChannel")

namespace {

const QString kMissionChannel = QStringLiteral("mission");

/// Returns the double value of @p value if it is a finite JSON number, else NaN. Mirrors
/// CommandChannel.cc's helper of the same name/shape (PROTOCOL.md §7.1 item fields are always
/// plain numbers).
double numberOrNaN(const QJsonValue &value)
{
    if (!value.isDouble()) {
        return qQNaN();
    }
    const double d = value.toDouble();
    return qIsFinite(d) ? d : qQNaN();
}

} // namespace

MissionChannel::MissionChannel(WebBridge *bridge, QObject *parent)
    : QObject(parent)
    , _bridge(bridge)
{
    qCDebug(MissionChannelLog) << this << "bridge" << static_cast<void*>(_bridge);

    MultiVehicleManager *multiVehicleManager = MultiVehicleManager::instance();
    if (multiVehicleManager) {
        connect(multiVehicleManager, &MultiVehicleManager::vehicleAdded, this, &MissionChannel::_onVehicleAdded);
        connect(multiVehicleManager, &MultiVehicleManager::vehicleRemoved, this, &MissionChannel::_onVehicleRemoved);

        // Pick up vehicles that connected before this object was constructed; vehicleAdded()
        // only fires for vehicles added after the connect() above.
        QmlObjectListModel *vehicles = multiVehicleManager->vehicles();
        if (vehicles) {
            for (int i = 0; i < vehicles->count(); ++i) {
                _onVehicleAdded(vehicles->value<Vehicle *>(i));
            }
        }
    }
}

MissionChannel::~MissionChannel()
{
    qCDebug(MissionChannelLog) << this;
}

void MissionChannel::_onVehicleAdded(Vehicle *vehicle)
{
    if (!vehicle) {
        return;
    }

    const int vehicleId = vehicle->id();
    qCDebug(MissionChannelLog) << "_onVehicleAdded" << vehicleId;
    _vehicles[vehicleId] = vehicle;

    MissionManager *missionManager = vehicle->missionManager();
    if (missionManager) {
        _managerToVehicleId[missionManager] = vehicleId;
        connect(missionManager, &PlanManager::newMissionItemsAvailable, this, &MissionChannel::_onNewMissionItemsAvailable);
        connect(missionManager, &PlanManager::currentIndexChanged, this, &MissionChannel::_onCurrentIndexChanged);
        connect(missionManager, &PlanManager::sendComplete, this, &MissionChannel::_onSendComplete);
        connect(missionManager, &PlanManager::removeAllComplete, this, &MissionChannel::_onRemoveAllComplete);
        connect(missionManager, &PlanManager::error, this, &MissionChannel::_onMissionManagerError);
    }

    // Defensive: guarantee the vehicle is dropped even if it is destroyed without
    // MultiVehicleManager::vehicleRemoved() having fired first (mirrors TelemetryChannel).
    connect(vehicle, &QObject::destroyed, this, [this, vehicleId, missionManager] {
        _vehicles.remove(vehicleId);
        if (missionManager) {
            _managerToVehicleId.remove(missionManager);
        }
        _lastError.remove(vehicleId);
        _failPendingOp(vehicleId, QStringLiteral("Vehicle disconnected"));
    });
}

void MissionChannel::_onVehicleRemoved(Vehicle *vehicle)
{
    if (!vehicle) {
        return;
    }

    const int vehicleId = vehicle->id();
    qCDebug(MissionChannelLog) << "_onVehicleRemoved" << vehicleId;
    _vehicles.remove(vehicleId);

    MissionManager *missionManager = vehicle->missionManager();
    if (missionManager) {
        _managerToVehicleId.remove(missionManager);
    }
    _lastError.remove(vehicleId);
    _failPendingOp(vehicleId, QStringLiteral("Vehicle disconnected"));
}

void MissionChannel::_onNewMissionItemsAvailable(bool removeAllRequested)
{
    Q_UNUSED(removeAllRequested);

    auto *missionManager = qobject_cast<MissionManager *>(sender());
    if (!missionManager) {
        return;
    }
    Vehicle *vehicle = _vehicles.value(_managerToVehicleId.value(missionManager, -1), nullptr);
    if (vehicle) {
        _publishMissionState(vehicle);
    }
}

void MissionChannel::_onCurrentIndexChanged(int currentIndex)
{
    Q_UNUSED(currentIndex);

    auto *missionManager = qobject_cast<MissionManager *>(sender());
    if (!missionManager) {
        return;
    }
    Vehicle *vehicle = _vehicles.value(_managerToVehicleId.value(missionManager, -1), nullptr);
    if (vehicle) {
        _publishMissionState(vehicle);
    }
}

void MissionChannel::_onSendComplete(bool error)
{
    auto *missionManager = qobject_cast<MissionManager *>(sender());
    if (!missionManager) {
        return;
    }
    const int vehicleId = _managerToVehicleId.value(missionManager, -1);
    if (vehicleId < 0) {
        return;
    }

    PendingOp op;
    if (!_takePendingOp(vehicleId, _OpType::Upload, op)) {
        // Not one of ours: either no missionUpload from this channel is in flight for this
        // vehicle, or PlanManager is shared with another consumer in the process (e.g. the Plan
        // view) driving its own write -- see class doc comment on _onSendComplete().
        return;
    }

    Vehicle *vehicle = _vehicles.value(vehicleId, nullptr);
    if (!error) {
        // PlanManager::_finishTransaction() merges the written items into missionItems() before
        // emitting sendComplete(true /* not error */) on the write path (PlanManager.cc), so this
        // is the authoritative post-write count -- not necessarily equal to the request's items
        // count, since a MISSION_TYPE_MISSION write silently drops the first (home) item when
        // the firmware doesn't want it sent (PlanManager::writeMissionItems()'s skipFirstItem).
        const int itemCount = (vehicle && vehicle->missionManager()) ? vehicle->missionManager()->missionItems().count() : 0;
        emit responseReady(op.clientToken, _makeAck(op.id, vehicleId, true, QString(), itemCount));
        if (vehicle) {
            _publishMissionState(vehicle);
        }
    } else {
        const QString reason = _lastError.value(vehicleId, QStringLiteral("Mission upload failed"));
        emit responseReady(op.clientToken, _makeAck(op.id, vehicleId, false, reason));
    }
}

void MissionChannel::_onRemoveAllComplete(bool error)
{
    auto *missionManager = qobject_cast<MissionManager *>(sender());
    if (!missionManager) {
        return;
    }
    const int vehicleId = _managerToVehicleId.value(missionManager, -1);
    if (vehicleId < 0) {
        return;
    }

    PendingOp op;
    if (!_takePendingOp(vehicleId, _OpType::Clear, op)) {
        return;
    }

    if (!error) {
        emit responseReady(op.clientToken, _makeAck(op.id, vehicleId, true, QString(), 0));
        Vehicle *vehicle = _vehicles.value(vehicleId, nullptr);
        if (vehicle) {
            // PlanManager::removeAll() already cleared missionItems() synchronously before this
            // signal (PlanManager.cc); republish so the stream reflects the empty mission/reset
            // currentSeq immediately (currentIndexChanged(-1) fires from the same call and would
            // do this anyway, but this makes the ordering deterministic relative to the ack).
            _publishMissionState(vehicle);
        }
    } else {
        const QString reason = _lastError.value(vehicleId, QStringLiteral("Mission clear failed"));
        emit responseReady(op.clientToken, _makeAck(op.id, vehicleId, false, reason));
    }
}

void MissionChannel::_onMissionManagerError(int errorCode, const QString &errorMsg)
{
    Q_UNUSED(errorCode);

    auto *missionManager = qobject_cast<MissionManager *>(sender());
    if (!missionManager) {
        return;
    }
    const int vehicleId = _managerToVehicleId.value(missionManager, -1);
    if (vehicleId < 0) {
        return;
    }

    // PlanManager emits error() synchronously just before the matching sendComplete(true)/
    // removeAllComplete(true) (see every _sendError()+_finishTransaction(false) call pair in
    // PlanManager.cc), so this is fresh by the time _onSendComplete()/_onRemoveAllComplete()
    // consumes it.
    _lastError[vehicleId] = errorMsg;
}

void MissionChannel::_onOpTimeout(int vehicleId)
{
    // Defensive: _onSendComplete()/_onRemoveAllComplete() may have already handled and erased
    // this entry in the same event-loop turn the timer was going to fire in (mirrors
    // CommandChannel::_beginCorrelated()'s timeout lambda doc comment).
    const auto it = _pendingOps.find(vehicleId);
    if (it == _pendingOps.end()) {
        return;
    }
    const PendingOp timedOut = it.value();
    _pendingOps.erase(it);
    timedOut.timeoutTimer->deleteLater();

    qCDebug(MissionChannelLog) << "op timeout: vehicle" << vehicleId << "id" << timedOut.id;
    emit responseReady(timedOut.clientToken, _makeAck(timedOut.id, vehicleId, false, QStringLiteral("No response from vehicle")));
}

void MissionChannel::sendSnapshot(const QString &channel, int vehicleId)
{
    if (channel != kMissionChannel) {
        return;
    }
    if (!_bridge) {
        return;
    }

    Vehicle *vehicle = _vehicles.value(vehicleId, nullptr);
    if (!vehicle) {
        qCDebug(MissionChannelLog) << "sendSnapshot: unknown vehicleId" << vehicleId;
        return;
    }

    // Snapshot is always seq 1 (PROTOCOL.md §2.2/§2.4): reset before building the message so
    // WebBridge::makeStreamMessage()'s internal nextSeq() call returns 1.
    _bridge->resetSeq(WebBridge::streamKey(kMissionChannel, vehicleId));
    _publishMissionState(vehicle);
}

void MissionChannel::handleMissionMessage(quint64 clientToken, const QJsonObject &request)
{
    // WebBridgeServer::_handleMission() (added alongside this class) already validated id/
    // vehicleId are present (per the task's WebBridgeServer routing contract) before emitting
    // missionMessageReceived(); re-derive them here without re-validating presence.
    const QString type = request.value(QStringLiteral("type")).toString();
    const QString id = request.value(QStringLiteral("id")).toString();
    const int vehicleId = request.value(QStringLiteral("vehicleId")).toInt();

    Vehicle *vehicle = _vehicles.value(vehicleId, nullptr);
    if (!vehicle || !vehicle->missionManager()) {
        // §10 error envelope (UNKNOWN_VEHICLE), not a rejected missionAck -- see
        // _makeUnknownVehicleError()'s doc comment for why this one case differs from the class's
        // usual "rejected ack, not a §10 error" convention.
        qCDebug(MissionChannelLog) << "handleMissionMessage: unknown vehicle or no MissionManager for" << vehicleId;
        emit responseReady(clientToken, _makeUnknownVehicleError(id, vehicleId));
        return;
    }

    if (type == QStringLiteral("missionDownload")) {
        _handleDownload(clientToken, id, vehicle);
    } else if (type == QStringLiteral("missionUpload")) {
        _handleUpload(clientToken, id, vehicle, request);
    } else if (type == QStringLiteral("missionClear")) {
        _handleClear(clientToken, id, vehicle);
    } else {
        // Unknown mission message type: rejected ack, not a §10 error (BAD_MESSAGE is reserved
        // for envelope shape problems, which WebBridgeServer already screens for) -- mirrors
        // CommandChannel::handleCommand()'s "unknown action" handling.
        qCDebug(MissionChannelLog) << "handleMissionMessage: unknown type" << type << "vehicle" << vehicleId;
        emit responseReady(clientToken, _makeAck(id, vehicleId, false, QStringLiteral("Unknown mission message type: %1").arg(type)));
    }
}

void MissionChannel::_handleDownload(quint64 clientToken, const QString &id, Vehicle *vehicle)
{
    // PROTOCOL.md §7.2 documents missionDownload as answered from "the CACHED/current mission".
    // This deliberately does not call MissionManager::loadFromVehicle() itself:
    //   - InitialConnectStateMachine already calls it once per vehicle connection
    //     (src/Vehicle/InitialConnectStateMachine.cc:499), so the cache reflects vehicle state
    //     from connect time onward without any help from this class.
    //   - missionUpload/missionClear below keep the cache in sync with our own writes:
    //     PlanManager::_finishTransaction() merges a successful write's items into
    //     missionItems() before emitting sendComplete(), and removeAll() clears missionItems()
    //     synchronously before the removeAll transaction even starts (PlanManager.cc).
    //   - Triggering our own loadFromVehicle() here would make missionDownload asynchronous
    //     (needing the same 15s-timeout/one-op-per-vehicle machinery as upload/clear below), and
    //     PlanManager::loadFromVehicle() silently no-ops with no signal at all if a transaction
    //     is already inProgress() elsewhere in the process (e.g. the Plan view) -- which would
    //     make an in-flight download hang for the full timeout for no protocol-visible reason.
    // Net effect (documented risk): a missionDownload issued before the vehicle's initial
    // mission load has completed returns whatever is cached at that moment (possibly empty).
    MissionManager *missionManager = vehicle->missionManager();

    QJsonObject message;
    message[QStringLiteral("type")] = QStringLiteral("missionItems");
    message[QStringLiteral("id")] = id;
    message[QStringLiteral("vehicleId")] = vehicle->id();
    message[QStringLiteral("items")] = _itemsToJson(missionManager->missionItems());
    emit responseReady(clientToken, message);
}

void MissionChannel::_handleUpload(quint64 clientToken, const QString &id, Vehicle *vehicle, const QJsonObject &request)
{
    MissionManager *missionManager = vehicle->missionManager();
    const int vehicleId = vehicle->id();

    if (_pendingOps.contains(vehicleId)) {
        emit responseReady(clientToken, _makeAck(id, vehicleId, false, QStringLiteral("Mission operation already in flight for this vehicle")));
        return;
    }

    // PlanManager::writeMissionItems() silently no-ops -- without taking ownership of the list
    // we are about to build -- in both of these cases (PlanManager.cc). Catching them upfront
    // avoids (a) leaking the MissionItem objects below and (b) waiting out the correlation
    // timeout for a transaction that was never actually started.
    if (vehicle->isOfflineEditingVehicle()) {
        emit responseReady(clientToken, _makeAck(id, vehicleId, false, QStringLiteral("Vehicle is offline-editing; mission upload not supported")));
        return;
    }
    if (missionManager->inProgress()) {
        emit responseReady(clientToken, _makeAck(id, vehicleId, false, QStringLiteral("Mission operation already in flight for this vehicle")));
        return;
    }

    const QJsonArray itemsJson = request.value(QStringLiteral("items")).toArray();
    QList<MissionItem *> items;
    QString errorReason;
    if (!_parseAndValidateItems(itemsJson, items, errorReason)) {
        emit responseReady(clientToken, _makeAck(id, vehicleId, false, errorReason));
        return;
    }

    // Register before sending, mirroring CommandChannel's _dispatch()/_beginCorrelated() ordering
    // discipline even though PlanManager's write path is not known to answer synchronously.
    _beginPendingOp(clientToken, id, vehicleId, _OpType::Upload);
    missionManager->writeMissionItems(items); // PlanManager takes ownership of the MissionItem objects from here
}

void MissionChannel::_handleClear(quint64 clientToken, const QString &id, Vehicle *vehicle)
{
    MissionManager *missionManager = vehicle->missionManager();
    const int vehicleId = vehicle->id();

    if (_pendingOps.contains(vehicleId)) {
        emit responseReady(clientToken, _makeAck(id, vehicleId, false, QStringLiteral("Mission operation already in flight for this vehicle")));
        return;
    }
    if (missionManager->inProgress()) {
        // PlanManager::removeAll() itself silently no-ops when inProgress() (PlanManager.cc);
        // answer now rather than waiting out the correlation timeout.
        emit responseReady(clientToken, _makeAck(id, vehicleId, false, QStringLiteral("Mission operation already in flight for this vehicle")));
        return;
    }

    _beginPendingOp(clientToken, id, vehicleId, _OpType::Clear);
    missionManager->removeAll();
}

void MissionChannel::_beginPendingOp(quint64 clientToken, const QString &id, int vehicleId, _OpType type)
{
    PendingOp op;
    op.id = id;
    op.clientToken = clientToken;
    op.type = type;
    op.timeoutTimer = new QTimer(this);
    op.timeoutTimer->setSingleShot(true);
    connect(op.timeoutTimer, &QTimer::timeout, this, [this, vehicleId]() { _onOpTimeout(vehicleId); });
    _pendingOps.insert(vehicleId, op);
    op.timeoutTimer->start(kOpTimeoutMs);
}

bool MissionChannel::_takePendingOp(int vehicleId, _OpType expectedType, PendingOp &outOp)
{
    const auto it = _pendingOps.find(vehicleId);
    if (it == _pendingOps.end() || it.value().type != expectedType) {
        return false;
    }
    outOp = it.value();
    _pendingOps.erase(it);
    outOp.timeoutTimer->stop();
    outOp.timeoutTimer->deleteLater();
    return true;
}

void MissionChannel::_failPendingOp(int vehicleId, const QString &reason)
{
    const auto it = _pendingOps.find(vehicleId);
    if (it == _pendingOps.end()) {
        return;
    }
    const PendingOp op = it.value();
    _pendingOps.erase(it);
    op.timeoutTimer->stop();
    op.timeoutTimer->deleteLater();
    emit responseReady(op.clientToken, _makeAck(op.id, vehicleId, false, reason));
}

void MissionChannel::_publishMissionState(Vehicle *vehicle)
{
    if (!vehicle || !_bridge) {
        return;
    }
    MissionManager *missionManager = vehicle->missionManager();
    if (!missionManager) {
        return;
    }

    QJsonObject payload;
    payload[QStringLiteral("currentSeq")] = missionManager->currentIndex();
    payload[QStringLiteral("items")] = _itemsToJson(missionManager->missionItems());

    const QJsonObject message = _bridge->makeStreamMessage(kMissionChannel, QStringLiteral("missionState"), payload, vehicle->id());
    emit missionStateReady(kMissionChannel, vehicle->id(), message);
}

QJsonArray MissionChannel::_itemsToJson(const QList<MissionItem *> &items) const
{
    // PROTOCOL.md §7.1: item schema is field-for-field aligned with MISSION_ITEM_INT, with lat/
    // lon/alt already in degrees/meters. MissionItem stores these directly as param5 (latitude),
    // param6 (longitude), param7 (altitude) -- see MissionItem::coordinate() and every write/read
    // path in PlanManager.cc, which only applies the *1e7 wire scaling at the actual MAVLink
    // encode/decode step, never inside MissionItem itself.
    QJsonArray array;
    for (MissionItem *item : items) {
        if (!item) {
            continue;
        }
        QJsonObject obj;
        obj[QStringLiteral("seq")] = item->sequenceNumber();
        obj[QStringLiteral("frame")] = static_cast<int>(item->frame());
        obj[QStringLiteral("command")] = static_cast<int>(item->command());
        obj[QStringLiteral("current")] = item->isCurrentItem();
        obj[QStringLiteral("autoContinue")] = item->autoContinue();
        obj[QStringLiteral("param1")] = item->param1();
        obj[QStringLiteral("param2")] = item->param2();
        obj[QStringLiteral("param3")] = item->param3();
        obj[QStringLiteral("param4")] = item->param4();
        obj[QStringLiteral("lat")] = item->param5();
        obj[QStringLiteral("lon")] = item->param6();
        obj[QStringLiteral("alt")] = item->param7();
        array.append(obj);
    }
    return array;
}

bool MissionChannel::_parseAndValidateItems(const QJsonArray &itemsJson, QList<MissionItem *> &outItems, QString &errorReason) const
{
    outItems.clear();

    for (int i = 0; i < itemsJson.count(); ++i) {
        if (!itemsJson.at(i).isObject()) {
            errorReason = QStringLiteral("items[%1] is not an object").arg(i);
            qDeleteAll(outItems);
            outItems.clear();
            return false;
        }
        const QJsonObject obj = itemsJson.at(i).toObject();

        // seq must equal array position: PlanManager writes items indexed by list position, not
        // by MissionItem::sequenceNumber() (see PlanManager::_handleMissionRequest(),
        // "_writeMissionItems[missionRequestSeq]" in PlanManager.cc) -- a mismatched or
        // non-contiguous seq here would silently desync DO_JUMP targets and the wire sequence.
        const QJsonValue seqValue = obj.value(QStringLiteral("seq"));
        if (!seqValue.isDouble() || seqValue.toInt() != i) {
            errorReason = QStringLiteral("items[%1].seq must equal its array position (%1)").arg(i);
            qDeleteAll(outItems);
            outItems.clear();
            return false;
        }

        const QJsonValue frameValue = obj.value(QStringLiteral("frame"));
        const QJsonValue commandValue = obj.value(QStringLiteral("command"));
        if (!frameValue.isDouble() || !commandValue.isDouble()) {
            errorReason = QStringLiteral("items[%1].frame and .command must be integers").arg(i);
            qDeleteAll(outItems);
            outItems.clear();
            return false;
        }

        if (!obj.value(QStringLiteral("current")).isBool() || !obj.value(QStringLiteral("autoContinue")).isBool()) {
            errorReason = QStringLiteral("items[%1].current and .autoContinue must be booleans").arg(i);
            qDeleteAll(outItems);
            outItems.clear();
            return false;
        }
        const bool current = obj.value(QStringLiteral("current")).toBool();
        const bool autoContinue = obj.value(QStringLiteral("autoContinue")).toBool();

        const double param1 = numberOrNaN(obj.value(QStringLiteral("param1")));
        const double param2 = numberOrNaN(obj.value(QStringLiteral("param2")));
        const double param3 = numberOrNaN(obj.value(QStringLiteral("param3")));
        const double param4 = numberOrNaN(obj.value(QStringLiteral("param4")));
        if (qIsNaN(param1) || qIsNaN(param2) || qIsNaN(param3) || qIsNaN(param4)) {
            errorReason = QStringLiteral("items[%1].param1..param4 must be finite numbers").arg(i);
            qDeleteAll(outItems);
            outItems.clear();
            return false;
        }

        const double lat = numberOrNaN(obj.value(QStringLiteral("lat")));
        const double lon = numberOrNaN(obj.value(QStringLiteral("lon")));
        const double alt = numberOrNaN(obj.value(QStringLiteral("alt")));
        if (qIsNaN(lat) || qIsNaN(lon) || qIsNaN(alt) || lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
            errorReason = QStringLiteral("items[%1].lat/lon/alt must be finite numbers with lat in [-90,90] and lon in [-180,180]").arg(i);
            qDeleteAll(outItems);
            outItems.clear();
            return false;
        }

        outItems.append(new MissionItem(
            i, // sequenceNumber; see the seq == i check above
            static_cast<MAV_CMD>(commandValue.toInt()),
            static_cast<MAV_FRAME>(frameValue.toInt()),
            param1, param2, param3, param4,
            lat, lon, alt,
            autoContinue,
            current));
    }

    return true;
}

QJsonObject MissionChannel::_makeAck(const QString &id, int vehicleId, bool accepted, const QString &reason, int itemCount)
{
    // PROTOCOL.md §7.2: "Failures use status: rejected + reason (+ mavResult/MAV_MISSION_RESULT
    // in mavResult when available)." PlanManager's error() signal only ever carries a
    // human-readable string (ErrorCode_t/errorMsg), never a numeric MAV_MISSION_RESULT, so
    // mavResult is never populated here -- see the class risks in the accompanying report.
    QJsonObject ack;
    ack[QStringLiteral("type")] = QStringLiteral("missionAck");
    ack[QStringLiteral("id")] = id;
    ack[QStringLiteral("vehicleId")] = vehicleId;
    ack[QStringLiteral("status")] = accepted ? QStringLiteral("accepted") : QStringLiteral("rejected");
    if (!accepted) {
        ack[QStringLiteral("reason")] = reason;
    }
    if (itemCount >= 0) {
        ack[QStringLiteral("itemCount")] = itemCount;
    }
    return ack;
}

QJsonObject MissionChannel::_makeUnknownVehicleError(const QString &id, int vehicleId)
{
    // PROTOCOL.md §10 error envelope, matching FactChannel::handleMessage()'s UNKNOWN_VEHICLE
    // shape and CommandChannel::_makeUnknownVehicleError() exactly.
    QJsonObject err;
    err[QStringLiteral("type")] = QStringLiteral("error");
    err[QStringLiteral("id")] = id;
    err[QStringLiteral("code")] = QStringLiteral("UNKNOWN_VEHICLE");
    err[QStringLiteral("message")] = QStringLiteral("Unknown vehicle");
    err[QStringLiteral("vehicleId")] = vehicleId;
    err[QStringLiteral("retryable")] = false;
    return err;
}
