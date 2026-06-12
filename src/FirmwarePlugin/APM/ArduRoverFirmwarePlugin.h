#pragma once

#include "APMFirmwarePlugin.h"

#include "FactGroup.h"
#include "Fact.h"
#include <QMap>

class APMRoverFactGroup : public FactGroup
{
    Q_OBJECT
    Q_PROPERTY(Fact *trimAngle   READ trimAngle   CONSTANT)
    Q_PROPERTY(Fact *rudderAngle READ rudderAngle CONSTANT)
    Q_PROPERTY(Fact *batteryVolt READ batteryVolt CONSTANT)
    Q_PROPERTY(Fact *fuelLevel   READ fuelLevel   CONSTANT)
    Q_PROPERTY(Fact *lightsStat  READ lightsStat  CONSTANT)
    Q_PROPERTY(Fact *trimStat    READ trimStat    CONSTANT)
    Q_PROPERTY(Fact *rpm         READ rpm         CONSTANT)

public:
    explicit APMRoverFactGroup(QObject *parent = nullptr);
    ~APMRoverFactGroup() override {}

    Fact *trimAngle() { return &_trimAngleFact; }
    Fact *rudderAngle() { return &_rudderAngleFact; }
    Fact *batteryVolt() { return &_batteryVoltFact; }
    Fact *fuelLevel() { return &_fuelLevelFact; }
    Fact *lightsStat() { return &_lightsStatFact; }
    Fact *trimStat() { return &_trimStatFact; }
    Fact *rpm() { return &_rpmFact; }

private:
    Fact _trimAngleFact{0, QStringLiteral("trimAngle"), FactMetaData::valueTypeDouble};
    Fact _rudderAngleFact{0, QStringLiteral("rudderAngle"), FactMetaData::valueTypeDouble};
    Fact _batteryVoltFact{0, QStringLiteral("batteryVolt"), FactMetaData::valueTypeDouble};
    Fact _fuelLevelFact{0, QStringLiteral("fuelLevel"), FactMetaData::valueTypeDouble};
    Fact _lightsStatFact{0, QStringLiteral("lightsStat"), FactMetaData::valueTypeDouble};
    Fact _trimStatFact{0, QStringLiteral("trimStat"), FactMetaData::valueTypeDouble};
    Fact _rpmFact{0, QStringLiteral("rpm"), FactMetaData::valueTypeDouble};
};
struct APMRoverMode
{
    enum Mode : uint32_t{
        MANUAL          = 0,
        ACRO            = 1,
        LEARNING        = 2, // Deprecated
        STEERING        = 3,
        HOLD            = 4,
        LOITER          = 5,
        FOLLOW          = 6,
        SIMPLE          = 7,
        DOCK            = 8,
        CIRCLE          = 9,
        AUTO            = 10,
        RTL             = 11,
        SMART_RTL       = 12,
        GUIDED          = 15,
        INITIALIZING    = 16
    };
};

class ArduRoverFirmwarePlugin : public APMFirmwarePlugin
{
    Q_OBJECT

public:
    explicit ArduRoverFirmwarePlugin(QObject *parent = nullptr);
    ~ArduRoverFirmwarePlugin();

    void initializeVehicle(Vehicle* vehicle) override;
    void guidedModeChangeAltitude(Vehicle* vehicle, double altitudeChange, bool pauseVehicle) override;
    int remapParamNameHigestMinorVersionNumber(int majorVersionNumber) const override;
    const FirmwarePlugin::remapParamNameMajorVersionMap_t& paramNameRemapMajorVersionMap() const override { return _remapParamName; }
    bool supportsNegativeThrust(Vehicle*) const override { return true; }
    bool supportsSmartRTL() const override { return true; }
    QString offlineEditingParamFile(Vehicle *vehicle) const override { Q_UNUSED(vehicle); return QStringLiteral(":/FirmwarePlugin/APM/Rover.OfflineEditing.params"); }

    QString pauseFlightMode() const override;
    QString followFlightMode() const override;
    QString stabilizedFlightMode() const override;
    void updateAvailableFlightModes(FlightModeList &modeList) override;

    bool adjustIncomingMavlinkMessage(Vehicle* vehicle, mavlink_message_t* message) override;

protected:
    void _handleNamedValueFloat(Vehicle* vehicle, mavlink_message_t* message);


protected:
    uint32_t _convertToCustomFlightModeEnum(uint32_t val) const override;

    const QString _manualFlightMode = tr("Manual");
    const QString _acroFlightMode = tr("Acro");
    const QString _learningFlightMode = tr("Learning");
    const QString _steeringFlightMode = tr("Steering");
    const QString _holdFlightMode = tr("Hold");
    const QString _loiterFlightMode = tr("Loiter");
    const QString _followFlightMode = tr("Follow");
    const QString _simpleFlightMode = tr("Simple");
    const QString _dockFlightMode = tr("Dock");
    const QString _circleFlightMode = tr("Circle");
    const QString _autoFlightMode = tr("Auto");
    const QString _rtlFlightMode = tr("RTL");
    const QString _smartRtlFlightMode = tr("Smart RTL");
    const QString _guidedFlightMode = tr("Guided");
    const QString _initializingFlightMode = tr("Initializing");

private:
    static bool _remapParamNameIntialized;
    static FirmwarePlugin::remapParamNameMajorVersionMap_t _remapParamName;
};

Q_DECLARE_METATYPE(APMRoverFactGroup*)
