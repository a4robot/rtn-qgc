#include "ArduRoverFirmwarePlugin.h"
#include "AppMessages.h"
#include "Vehicle.h"


APMRoverFactGroup::APMRoverFactGroup(QObject *parent)
    : FactGroup(1000, parent)
{
    _addFact(&_trimAngleFact,   QStringLiteral("trimAngle"));
    _addFact(&_rudderAngleFact, QStringLiteral("rudderAngle"));
    _addFact(&_batteryVoltFact, QStringLiteral("batteryVolt"));
    _addFact(&_fuelLevelFact,   QStringLiteral("fuelLevel"));
    _addFact(&_lightsStatFact,  QStringLiteral("lightsStat"));
    _addFact(&_trimStatFact,    QStringLiteral("trimStat"));
    _addFact(&_rpmFact,         QStringLiteral("rpm"));
}

bool ArduRoverFirmwarePlugin::_remapParamNameIntialized = false;
FirmwarePlugin::remapParamNameMajorVersionMap_t ArduRoverFirmwarePlugin::_remapParamName;

ArduRoverFirmwarePlugin::ArduRoverFirmwarePlugin(QObject *parent)
    : APMFirmwarePlugin(parent)
{
    _setModeEnumToModeStringMapping({
        { APMRoverMode::MANUAL       , _manualFlightMode       },
        { APMRoverMode::ACRO         , _acroFlightMode         },
        { APMRoverMode::LEARNING     , _learningFlightMode     },
        { APMRoverMode::STEERING     , _steeringFlightMode     },
        { APMRoverMode::HOLD         , _holdFlightMode         },
        { APMRoverMode::LOITER       , _loiterFlightMode       },
        { APMRoverMode::FOLLOW       , _followFlightMode       },
        { APMRoverMode::SIMPLE       , _simpleFlightMode       },
        { APMRoverMode::DOCK         , _dockFlightMode         },
        { APMRoverMode::CIRCLE       , _circleFlightMode       },
        { APMRoverMode::AUTO         , _autoFlightMode         },
        { APMRoverMode::RTL          , _rtlFlightMode          },
        { APMRoverMode::SMART_RTL    , _smartRtlFlightMode     },
        { APMRoverMode::GUIDED       , _guidedFlightMode       },
        { APMRoverMode::INITIALIZING , _initializingFlightMode },
    });

    static FlightModeList availableFlightModes = {
        // Mode Name              , Custom Mode                CanBeSet  adv
        { _manualFlightMode       , APMRoverMode::MANUAL       , true , true},
        { _acroFlightMode         , APMRoverMode::ACRO         , true , true},
        { _learningFlightMode     , APMRoverMode::LEARNING     , false, true},
        { _steeringFlightMode     , APMRoverMode::STEERING     , true , true},
        { _holdFlightMode         , APMRoverMode::HOLD         , true , true},
        { _loiterFlightMode       , APMRoverMode::LOITER       , true , true},
        { _followFlightMode       , APMRoverMode::FOLLOW       , true , true},
        { _simpleFlightMode       , APMRoverMode::SIMPLE       , true , true},
        { _dockFlightMode         , APMRoverMode::DOCK         , true , true},
        { _circleFlightMode       , APMRoverMode::CIRCLE       , true , true},
        { _autoFlightMode         , APMRoverMode::AUTO         , true , true},
        { _rtlFlightMode          , APMRoverMode::RTL          , true , true},
        { _smartRtlFlightMode     , APMRoverMode::SMART_RTL    , true , true},
        { _guidedFlightMode       , APMRoverMode::GUIDED       , true , true},
        { _initializingFlightMode , APMRoverMode::INITIALIZING , false, true},
    };
    updateAvailableFlightModes(availableFlightModes);

    if (!_remapParamNameIntialized) {
        // ArduPilot 4.7: parameter renames and SI unit conversion
        FirmwarePlugin::remapParamNameMap_t &remapV4_7 = _remapParamName[4][7];

        // EKF
        remapV4_7["EK3_FLOW_MAX"]    = QStringLiteral("EK3_MAX_FLOW");

        // Common
        remapV4_7["ARMING_SKIPCHK"]  = QStringLiteral("ARMING_CHECK");

        _remapParamNameIntialized = true;
    }
}

ArduRoverFirmwarePlugin::~ArduRoverFirmwarePlugin()
{

}

int ArduRoverFirmwarePlugin::remapParamNameHigestMinorVersionNumber(int majorVersionNumber) const
{
    return ((majorVersionNumber == 4) ? 7 : Vehicle::versionNotSetValue);
}

void ArduRoverFirmwarePlugin::initializeVehicle(Vehicle *vehicle)
{
    APMFirmwarePlugin::initializeVehicle(vehicle);

    // Create the fact group safely per-vehicle and expose it as a dynamic property before UI loads
    APMRoverFactGroup* group = new APMRoverFactGroup(vehicle);
    vehicle->setApmRoverInfo(static_cast<FactGroup*>(group));
}

void ArduRoverFirmwarePlugin::guidedModeChangeAltitude(Vehicle* /*vehicle*/, double /*altitudeChange*/, bool /*pauseVehicle*/)
{
    QGC::showAppMessage(QStringLiteral("Change altitude not supported."));
}

QString ArduRoverFirmwarePlugin::stabilizedFlightMode() const
{
    return _modeEnumToString.value(APMRoverMode::MANUAL, _manualFlightMode);
}

QString ArduRoverFirmwarePlugin::pauseFlightMode() const
{
    return _modeEnumToString.value(APMRoverMode::HOLD, _holdFlightMode);
}

QString ArduRoverFirmwarePlugin::followFlightMode() const
{
    return _modeEnumToString.value(APMRoverMode::FOLLOW, _followFlightMode);
}

void ArduRoverFirmwarePlugin::updateAvailableFlightModes(FlightModeList &modeList)
{
    for (FirmwareFlightMode &mode: modeList) {
        mode.fixedWing = false;
        mode.multiRotor = true;
    }

    _updateFlightModeList(modeList);
}

uint32_t ArduRoverFirmwarePlugin::_convertToCustomFlightModeEnum(uint32_t val) const
{
    switch (val) {
    case APMCustomMode::AUTO:
        return APMRoverMode::AUTO;
    case APMCustomMode::GUIDED:
        return APMRoverMode::GUIDED;
    case APMCustomMode::RTL:
        return APMRoverMode::RTL;
    case APMCustomMode::SMART_RTL:
        return APMRoverMode::SMART_RTL;
    default:
        return UINT32_MAX;
    }
}

bool ArduRoverFirmwarePlugin::adjustIncomingMavlinkMessage(Vehicle *vehicle, mavlink_message_t *message)
{
    if (message->msgid == MAVLINK_MSG_ID_NAMED_VALUE_FLOAT) {
        _handleNamedValueFloat(vehicle, message);
    }
    return APMFirmwarePlugin::adjustIncomingMavlinkMessage(vehicle, message);
}

void ArduRoverFirmwarePlugin::_handleNamedValueFloat(Vehicle *vehicle, mavlink_message_t *message)
{
    mavlink_named_value_float_t value;
    mavlink_msg_named_value_float_decode(message, &value);

    int len = qstrnlen(value.name, 10);
    QString name = QString::fromLocal8Bit(value.name, len);

    APMRoverFactGroup* group = qobject_cast<APMRoverFactGroup*>(vehicle->apmRoverInfo());
    if (!group) return;

    if (name == "TRIM_ANG") {
        group->trimAngle()->setRawValue(value.value);
    } else if (name == "RUDD_ANG") {
        group->rudderAngle()->setRawValue(value.value);
    } else if (name == "BAT_VOLT") {
        group->batteryVolt()->setRawValue(value.value);
    } else if (name == "FUEL_LVL") {
        group->fuelLevel()->setRawValue(value.value);
    } else if (name == "LGT_STAT") {
        group->lightsStat()->setRawValue(value.value);
    } else if (name == "TRM_STAT") {
        group->trimStat()->setRawValue(value.value);
    } else if (name == "ENG_RPM") {
        group->rpm()->setRawValue(value.value);
    }
}
