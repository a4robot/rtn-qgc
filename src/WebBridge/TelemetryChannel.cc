#include "TelemetryChannel.h"

#include <QtCore/QJsonValue>
#include <QtCore/QtNumeric>
#include <QtPositioning/QGeoCoordinate>

#include "BatteryFactGroupListModel.h"
#include "Fact.h"
#include "MAVLinkLib.h"
#include "MultiVehicleManager.h"
#include "QGCLoggingCategory.h"
#include "QmlObjectListModel.h"
#include "Vehicle.h"
#include "VehicleGPSFactGroup.h"
#include "WebBridge.h"

QGC_LOGGING_CATEGORY(TelemetryChannelLog, "WebBridge.TelemetryChannel")

namespace {

const QString kTelemetryChannel = QStringLiteral("telemetry");

/// Maps a raw MAV_GPS_FIX_TYPE value (VehicleGPSFactGroup::lock()->rawValue()) onto the closed
/// set of `gps.fix` wire values (PROTOCOL.md §4: `none` | `2d` | `3d` | `rtkFloat` | `rtkFixed`).
/// DGPS/PPP are folded into "3d" and STATIC into "rtkFixed": the wire enum has no bucket for
/// them and they are the closest precision-equivalent neighbor.
QString gpsFixToString(int fixType)
{
    switch (fixType) {
    case GPS_FIX_TYPE_NO_GPS:
    case GPS_FIX_TYPE_NO_FIX:
        return QStringLiteral("none");
    case GPS_FIX_TYPE_2D_FIX:
        return QStringLiteral("2d");
    case GPS_FIX_TYPE_3D_FIX:
    case GPS_FIX_TYPE_DGPS:
    case GPS_FIX_TYPE_PPP:
        return QStringLiteral("3d");
    case GPS_FIX_TYPE_RTK_FLOAT:
        return QStringLiteral("rtkFloat");
    case GPS_FIX_TYPE_RTK_FIXED:
    case GPS_FIX_TYPE_STATIC:
        return QStringLiteral("rtkFixed");
    default:
        return QStringLiteral("none");
    }
}

/// PROTOCOL.md §4: "Fields whose value is unknown are null, never omitted."
QJsonValue doubleOrNull(double value)
{
    return qIsNaN(value) ? QJsonValue() : QJsonValue(value);
}

} // namespace

TelemetryChannel::TelemetryChannel(WebBridge *bridge, QObject *parent)
    : QObject(parent)
    , _bridge(bridge)
{
    qCDebug(TelemetryChannelLog) << this << "bridge" << static_cast<void*>(_bridge);

    MultiVehicleManager *multiVehicleManager = MultiVehicleManager::instance();
    if (multiVehicleManager) {
        connect(multiVehicleManager, &MultiVehicleManager::vehicleAdded, this, &TelemetryChannel::_onVehicleAdded);
        connect(multiVehicleManager, &MultiVehicleManager::vehicleRemoved, this, &TelemetryChannel::_onVehicleRemoved);

        // Pick up vehicles that connected before this object was constructed; vehicleAdded()
        // only fires for vehicles added after the connect() above.
        QmlObjectListModel *vehicles = multiVehicleManager->vehicles();
        if (vehicles) {
            for (int i = 0; i < vehicles->count(); ++i) {
                _onVehicleAdded(vehicles->value<Vehicle *>(i));
            }
        }
    }

    _telemetryTimer.setInterval(kTelemetryIntervalMs);
    connect(&_telemetryTimer, &QTimer::timeout, this, &TelemetryChannel::_sendPeriodicTelemetry);
    _telemetryTimer.start();
}

TelemetryChannel::~TelemetryChannel()
{
    qCDebug(TelemetryChannelLog) << this;
}

void TelemetryChannel::_onVehicleAdded(Vehicle *vehicle)
{
    if (!vehicle) {
        return;
    }

    const int vehicleId = vehicle->id();
    qCDebug(TelemetryChannelLog) << "_onVehicleAdded" << vehicleId;
    _vehicles[vehicleId] = vehicle;
    _syncBridgeVehicleIds();

    // Defensive: guarantee the vehicle is dropped even if it is destroyed without
    // MultiVehicleManager::vehicleRemoved() having fired first.
    connect(vehicle, &QObject::destroyed, this, [this, vehicleId] {
        _vehicles.remove(vehicleId);
        _syncBridgeVehicleIds();
    });
}

void TelemetryChannel::_onVehicleRemoved(Vehicle *vehicle)
{
    if (!vehicle) {
        return;
    }

    qCDebug(TelemetryChannelLog) << "_onVehicleRemoved" << vehicle->id();
    _vehicles.remove(vehicle->id());
    _syncBridgeVehicleIds();
}

// Keep the bridge's tick vehicleIds (§11.1 — state, not events) in step with
// the tracked vehicle set; without this the tick reports an empty list forever.
void TelemetryChannel::_syncBridgeVehicleIds()
{
    if (_bridge) {
        _bridge->setVehicleIds(_vehicles.keys());
    }
}

void TelemetryChannel::_sendPeriodicTelemetry()
{
    if (!_bridge) {
        return;
    }

    for (auto it = _vehicles.constBegin(); it != _vehicles.constEnd(); ++it) {
        Vehicle *vehicle = it.value();
        if (!vehicle) {
            continue;
        }

        const int vehicleId = it.key();
        const QJsonObject payload = _buildPayload(vehicle);
        const QJsonObject message = _bridge->makeStreamMessage(kTelemetryChannel, kTelemetryChannel, payload, vehicleId);
        emit telemetryReady(kTelemetryChannel, vehicleId, message);
    }
}

void TelemetryChannel::sendSnapshot(const QString &channel, int vehicleId)
{
    if (channel != kTelemetryChannel) {
        return;
    }

    if (!_bridge) {
        return;
    }

    Vehicle *vehicle = _vehicles.value(vehicleId, nullptr);
    if (!vehicle) {
        qCDebug(TelemetryChannelLog) << "sendSnapshot: unknown vehicleId" << vehicleId;
        return;
    }

    // Snapshot is always seq 1 (PROTOCOL.md §2.2/§2.4): reset before building the message so
    // WebBridge::makeStreamMessage()'s internal nextSeq() call returns 1.
    _bridge->resetSeq(WebBridge::streamKey(kTelemetryChannel, vehicleId));

    const QJsonObject payload = _buildPayload(vehicle);
    const QJsonObject message = _bridge->makeStreamMessage(kTelemetryChannel, kTelemetryChannel, payload, vehicleId);
    emit telemetryReady(kTelemetryChannel, vehicleId, message);
}

QJsonObject TelemetryChannel::_buildPayload(Vehicle *vehicle) const
{
    if (!vehicle) {
        return {};
    }

    // Vehicle inherits VehicleFactGroup, so roll/pitch/heading/groundSpeed/airSpeed/climbRate/
    // altitudeAMSL/altitudeRelative are Facts directly on vehicle (VehicleFactGroup.h).
    QJsonObject attitude;
    attitude[QStringLiteral("roll")] = vehicle->roll()->rawValue().toDouble();
    attitude[QStringLiteral("pitch")] = vehicle->pitch()->rawValue().toDouble();
    // Wire field is "yaw" per PROTOCOL.md §4 (0-360, heading); the C++ Fact is named "heading".
    attitude[QStringLiteral("yaw")] = vehicle->heading()->rawValue().toDouble();

    const QGeoCoordinate coordinate = vehicle->coordinate();
    QJsonObject position;
    position[QStringLiteral("lat")] = coordinate.latitude();
    position[QStringLiteral("lon")] = coordinate.longitude();
    position[QStringLiteral("altMSL")] = vehicle->altitudeAMSL()->rawValue().toDouble();
    position[QStringLiteral("altRel")] = vehicle->altitudeRelative()->rawValue().toDouble();

    QJsonObject velocity;
    velocity[QStringLiteral("groundSpeed")] = vehicle->groundSpeed()->rawValue().toDouble();
    // airSpeed is only meaningful for fixed-wing/VTOL vehicles; multirotors report null
    // (PROTOCOL.md §4 example: "airSpeed": null).
    velocity[QStringLiteral("airSpeed")] = vehicle->multiRotor()
        ? QJsonValue()
        : doubleOrNull(vehicle->airSpeed()->rawValue().toDouble());
    velocity[QStringLiteral("climbRate")] = vehicle->climbRate()->rawValue().toDouble();

    QJsonObject battery;
    QmlObjectListModel *batteries = vehicle->batteries();
    BatteryFactGroup *primaryBattery = (batteries && batteries->count() > 0)
        ? batteries->value<BatteryFactGroup *>(0)
        : nullptr;
    if (primaryBattery) {
        battery[QStringLiteral("percent")] = doubleOrNull(primaryBattery->percentRemaining()->rawValue().toDouble());
        battery[QStringLiteral("voltage")] = doubleOrNull(primaryBattery->voltage()->rawValue().toDouble());
        battery[QStringLiteral("current")] = doubleOrNull(primaryBattery->current()->rawValue().toDouble());
    } else {
        battery[QStringLiteral("percent")] = QJsonValue();
        battery[QStringLiteral("voltage")] = QJsonValue();
        battery[QStringLiteral("current")] = QJsonValue();
    }

    QJsonObject gps;
    auto *gpsFactGroup = qobject_cast<VehicleGPSFactGroup *>(vehicle->gpsFactGroup());
    if (gpsFactGroup) {
        gps[QStringLiteral("fix")] = gpsFixToString(gpsFactGroup->lock()->rawValue().toInt());
        gps[QStringLiteral("count")] = gpsFactGroup->count()->rawValue().toInt();
        gps[QStringLiteral("hdop")] = doubleOrNull(gpsFactGroup->hdop()->rawValue().toDouble());
    } else {
        gps[QStringLiteral("fix")] = QStringLiteral("none");
        gps[QStringLiteral("count")] = 0;
        gps[QStringLiteral("hdop")] = QJsonValue();
    }

    QJsonObject payload;
    payload[QStringLiteral("attitude")] = attitude;
    payload[QStringLiteral("position")] = position;
    payload[QStringLiteral("velocity")] = velocity;
    payload[QStringLiteral("battery")] = battery;
    payload[QStringLiteral("gps")] = gps;
    payload[QStringLiteral("flightMode")] = vehicle->flightMode();
    payload[QStringLiteral("armed")] = vehicle->armed();
    return payload;
}
