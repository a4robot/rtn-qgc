#include "FactChannel.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include "Fact.h"
#include "FactMetaData.h"
#include "MultiVehicleManager.h"
#include "ParameterManager.h"
#include "QGCLoggingCategory.h"
#include "QmlObjectListModel.h"
#include "Vehicle.h"
#include "WebBridge.h"

QGC_LOGGING_CATEGORY(FactChannelLog, "WebBridge.FactChannel")

namespace {

QString getFactTypeString(FactMetaData::ValueType_t type)
{
    switch (type) {
    case FactMetaData::valueTypeUint8: return QStringLiteral("uint8");
    case FactMetaData::valueTypeInt8: return QStringLiteral("int8");
    case FactMetaData::valueTypeUint16: return QStringLiteral("uint16");
    case FactMetaData::valueTypeInt16: return QStringLiteral("int16");
    case FactMetaData::valueTypeUint32: return QStringLiteral("uint32");
    case FactMetaData::valueTypeInt32: return QStringLiteral("int32");
    case FactMetaData::valueTypeUint64: return QStringLiteral("uint64");
    case FactMetaData::valueTypeInt64: return QStringLiteral("int64");
    case FactMetaData::valueTypeFloat: return QStringLiteral("float");
    case FactMetaData::valueTypeDouble: return QStringLiteral("double");
    default: return QStringLiteral("float");
    }
}

} // namespace

FactChannel::FactChannel(QObject *parent)
    : QObject(parent)
{
    qCDebug(FactChannelLog) << this;

    MultiVehicleManager *multiVehicleManager = MultiVehicleManager::instance();
    if (multiVehicleManager) {
        connect(multiVehicleManager, &MultiVehicleManager::vehicleAdded, this, &FactChannel::_onVehicleAdded);
        connect(multiVehicleManager, &MultiVehicleManager::vehicleRemoved, this, &FactChannel::_onVehicleRemoved);

        QmlObjectListModel *vehicles = multiVehicleManager->vehicles();
        if (vehicles) {
            for (int i = 0; i < vehicles->count(); ++i) {
                _onVehicleAdded(vehicles->value<Vehicle *>(i));
            }
        }
    }
}

FactChannel::~FactChannel()
{
    qCDebug(FactChannelLog) << this;
}

void FactChannel::_onVehicleAdded(Vehicle *vehicle)
{
    if (!vehicle) {
        return;
    }

    const int vehicleId = vehicle->id();
    qCDebug(FactChannelLog) << "_onVehicleAdded" << vehicleId;
    _vehicles[vehicleId] = vehicle;

    ParameterManager *paramMgr = vehicle->parameterManager();
    if (paramMgr) {
        connect(paramMgr, &ParameterManager::_paramSetSuccess, this, &FactChannel::_paramSetSuccess);
        connect(paramMgr, &ParameterManager::_paramSetFailure, this, &FactChannel::_paramSetFailure);
    }

    connect(vehicle, &QObject::destroyed, this, [this, vehicleId] {
        _vehicles.remove(vehicleId);
    });
}

void FactChannel::_onVehicleRemoved(Vehicle *vehicle)
{
    if (!vehicle) {
        return;
    }
    qCDebug(FactChannelLog) << "_onVehicleRemoved" << vehicle->id();
    _vehicles.remove(vehicle->id());
}

void FactChannel::handleMessage(QWebSocket *client, const QJsonObject &message)
{
    const QString type = message.value(QStringLiteral("type")).toString();
    const QString id = message.value(QStringLiteral("id")).toString();
    const int vehicleId = message.value(QStringLiteral("vehicleId")).toInt(-1);
    const QString path = message.value(QStringLiteral("path")).toString();

    if (path.isEmpty()) {
        emit errorReady(client, QStringLiteral("BAD_MESSAGE"), QStringLiteral("Missing required field: path"), false, id);
        return;
    }
    if (vehicleId == -1) {
        emit errorReady(client, QStringLiteral("BAD_MESSAGE"), QStringLiteral("Missing or invalid vehicleId"), false, id);
        return;
    }

    if (!_vehicles.contains(vehicleId)) {
        emit errorReady(client, QStringLiteral("UNKNOWN_VEHICLE"), QStringLiteral("Unknown vehicle"), false, id);
        return;
    }

    // path is expected to be "vehicle.<vehicleId>.<PARAM_NAME>"
    const QString prefix = QStringLiteral("vehicle.%1.").arg(vehicleId);
    if (!path.startsWith(prefix)) {
        emit errorReady(client, QStringLiteral("UNKNOWN_PARAM"), QStringLiteral("Invalid param path prefix"), false, id);
        return;
    }
    const QString paramName = path.mid(prefix.length());

    Vehicle *vehicle = _vehicles.value(vehicleId);
    ParameterManager *paramMgr = vehicle->parameterManager();
    if (!paramMgr->parameterExists(ParameterManager::defaultComponentId, paramName)) {
        emit errorReady(client, QStringLiteral("UNKNOWN_PARAM"), QStringLiteral("Parameter not found"), false, id);
        return;
    }

    Fact *fact = paramMgr->getParameter(ParameterManager::defaultComponentId, paramName);
    if (!fact) {
        emit errorReady(client, QStringLiteral("UNKNOWN_PARAM"), QStringLiteral("Parameter not found"), false, id);
        return;
    }

    if (type == QStringLiteral("getParam")) {
        QJsonObject paramMsg = _buildParamValueMessage(id, vehicleId, path, fact);
        emit responseReady(client, paramMsg);
    } else if (type == QStringLiteral("setParam")) {
        if (!message.contains(QStringLiteral("value"))) {
            emit errorReady(client, QStringLiteral("BAD_MESSAGE"), QStringLiteral("Missing required field: value"), false, id);
            return;
        }
        
        QVariant newValue = message.value(QStringLiteral("value")).toVariant();

        // Store the pending request so we can respond later
        PendingRequest req;
        req.client = client;
        req.id = id;
        req.vehicleId = vehicleId;
        _pendingRequests[path].append(req);

        // Actually set the value
        fact->setRawValue(newValue);
        
        // When vehicle confirms, ParameterManager emits _paramSetSuccess or _paramSetFailure
    }
}

void FactChannel::_paramSetSuccess(int componentId, const QString &paramName)
{
    Q_UNUSED(componentId);
    
    // Find the vehicle that emitted this
    ParameterManager* paramMgr = qobject_cast<ParameterManager*>(sender());
    if (!paramMgr) return;
    
    int vehicleId = paramMgr->vehicle()->id();
    QString path = QStringLiteral("vehicle.%1.%2").arg(vehicleId).arg(paramName);

    if (!_pendingRequests.contains(path)) {
        return;
    }

    Fact *fact = paramMgr->getParameter(ParameterManager::defaultComponentId, paramName);
    
    QList<PendingRequest> reqs = _pendingRequests.take(path);
    for (const PendingRequest &req : reqs) {
        if (fact) {
            QJsonObject paramMsg = _buildParamValueMessage(req.id, req.vehicleId, path, fact);
            emit responseReady(req.client, paramMsg);
        } else {
            emit errorReady(req.client, QStringLiteral("INTERNAL"), QStringLiteral("Fact not found after set"), false, req.id);
        }
    }
}

void FactChannel::_paramSetFailure(int componentId, const QString &paramName)
{
    Q_UNUSED(componentId);

    ParameterManager* paramMgr = qobject_cast<ParameterManager*>(sender());
    if (!paramMgr) return;
    
    int vehicleId = paramMgr->vehicle()->id();
    QString path = QStringLiteral("vehicle.%1.%2").arg(vehicleId).arg(paramName);

    if (!_pendingRequests.contains(path)) {
        return;
    }

    QList<PendingRequest> reqs = _pendingRequests.take(path);
    for (const PendingRequest &req : reqs) {
        emit errorReady(req.client, QStringLiteral("PARAM_TIMEOUT"), QStringLiteral("Vehicle did not confirm set"), true, req.id);
    }
}

QJsonObject FactChannel::_buildParamValueMessage(const QString &id, int vehicleId, const QString &path, Fact *fact) const
{
    QJsonObject msg;
    msg[QStringLiteral("type")] = QStringLiteral("paramValue");
    msg[QStringLiteral("id")] = id;
    msg[QStringLiteral("vehicleId")] = vehicleId;
    msg[QStringLiteral("path")] = path;
    
    // Convert current value. QJsonValue::fromVariant handles numeric correctly.
    msg[QStringLiteral("value")] = QJsonValue::fromVariant(fact->rawValue());

    // Task B10: Send FactMetaData (min, max, type)
    QJsonObject meta;
    meta[QStringLiteral("type")] = getFactTypeString(fact->type());
    
    if (fact->cookedUnits().isEmpty()) {
        meta[QStringLiteral("units")] = QJsonValue::Null;
    } else {
        meta[QStringLiteral("units")] = fact->cookedUnits();
    }
    
    if (fact->minIsDefaultForType()) {
        meta[QStringLiteral("min")] = QJsonValue::Null;
    } else {
        meta[QStringLiteral("min")] = QJsonValue::fromVariant(fact->rawMin());
    }

    if (fact->maxIsDefaultForType()) {
        meta[QStringLiteral("max")] = QJsonValue::Null;
    } else {
        meta[QStringLiteral("max")] = QJsonValue::fromVariant(fact->rawMax());
    }

    if (!fact->defaultValueAvailable()) {
        meta[QStringLiteral("default")] = QJsonValue::Null;
    } else {
        meta[QStringLiteral("default")] = QJsonValue::fromVariant(fact->rawDefaultValue());
    }

    if (fact->shortDescription().isEmpty()) {
        meta[QStringLiteral("description")] = QJsonValue::Null;
    } else {
        meta[QStringLiteral("description")] = fact->shortDescription();
    }

    msg[QStringLiteral("meta")] = meta;

    return msg;
}
