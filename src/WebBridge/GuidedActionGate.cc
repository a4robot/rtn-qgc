#include "GuidedActionGate.h"

#include <QtCore/QtNumeric>
#include <QtPositioning/QGeoCoordinate>

#include "AppSettings.h"
#include "Fact.h"
#include "HealthAndArmingCheckReport.h"
#include "MAVLinkLib.h"
#include "QGCCorePlugin.h"
#include "QGCLoggingCategory.h"
#include "QGCOptions.h"
#include "SettingsManager.h"
#include "Vehicle.h"
#include "VehicleSupports.h"

QGC_LOGGING_CATEGORY(GuidedActionGateLog, "WebBridge.GuidedActionGate")

namespace {

/// QML: ScreenTools.isDebug — mirrors ScreenToolsController::isDebug() (QmlControls) without
/// pulling in that QML-facing header.
constexpr bool kIsDebugBuild =
#ifdef QT_DEBUG
    true;
#else
    false;
#endif

} // namespace

GuidedActionGate::GuidedActionGate(QObject *parent)
    : QObject(parent)
{
    qCDebug(GuidedActionGateLog) << this;
}

GuidedActionGate::~GuidedActionGate()
{
    qCDebug(GuidedActionGateLog) << this;
}

void GuidedActionGate::setVehicle(Vehicle *vehicle)
{
    if (vehicle == _vehicle) {
        return;
    }

    if (_vehicle) {
        disconnect(_vehicle, nullptr, this, nullptr);
    }

    _vehicle = vehicle;
    qCDebug(GuidedActionGateLog) << "setVehicle" << static_cast<void*>(_vehicle);

    if (_vehicle) {
        connect(_vehicle, &Vehicle::flyingChanged, this, &GuidedActionGate::_updateVehicleWasFlying);
        connect(_vehicle, &QObject::destroyed, this, [this] { _vehicle = nullptr; });
    }

    _updateVehicleWasFlying();
}

/// QML: on_VehicleFlyingChanged — "_vehicleWasFlying" latches to true when the vehicle
/// transitions from flying to not flying. The QML comment: "We use _vehicleWasFLying to help
/// trigger Resume Mission only if the vehicle actually flew and came back down. Otherwise it
/// may trigger during the Start Mission sequence due to signal ordering or armed and resume
/// mission index."
void GuidedActionGate::_updateVehicleWasFlying()
{
    const bool flying = _vehicleFlying();
    if (_lastVehicleFlying && !flying) {
        _vehicleWasFlying = true;
    }
    _lastVehicleFlying = flying;
}

/*===========================================================================*/
/* Vehicle/plugin state helpers ported from GuidedActionsController.qml      */
/*===========================================================================*/

// QML: _guidedActionsEnabled
//   (!ScreenTools.isDebug && _corePluginOptions.guidedActionsRequireRCRSSI && _activeVehicle) ? _rcRSSIAvailable : _activeVehicle
bool GuidedActionGate::_guidedActionsEnabled() const
{
    if (!kIsDebugBuild && QGCCorePlugin::instance()->options()->guidedActionsRequireRCRSSI() && _vehicle) {
        return _rcRSSIAvailable();
    }
    return (_vehicle != nullptr);
}

// QML: _rcRSSIAvailable
//   _activeVehicle ? _activeVehicle.rcRSSI.rawValue > 0 && _activeVehicle.rcRSSI.rawValue <= 100 : false
bool GuidedActionGate::_rcRSSIAvailable() const
{
    if (!_vehicle) {
        return false;
    }
    const double rcRSSI = _vehicle->rcRSSI()->rawValue().toDouble();
    return (rcRSSI > 0) && (rcRSSI <= 100);
}

// QML: _vehicleArmed — _activeVehicle ? _activeVehicle.armed : false
bool GuidedActionGate::_vehicleArmed() const
{
    return _vehicle ? _vehicle->armed() : false;
}

// QML: _vehicleFlying — _activeVehicle ? _activeVehicle.flying : false
bool GuidedActionGate::_vehicleFlying() const
{
    return _vehicle ? _vehicle->flying() : false;
}

// QML: _vehicleLanding — _activeVehicle ? _activeVehicle.landing : false
bool GuidedActionGate::_vehicleLanding() const
{
    return _vehicle ? _vehicle->landing() : false;
}

// QML: _vehiclePaused (recomputed in on_FlightModeChanged)
//   _activeVehicle ? _flightMode === _activeVehicle.pauseFlightMode : false
bool GuidedActionGate::_vehiclePaused() const
{
    return _vehicle ? (_vehicle->flightMode() == _vehicle->pauseFlightMode()) : false;
}

// QML: _vehicleInRTLMode (recomputed in on_FlightModeChanged)
//   _activeVehicle ? _flightMode === _activeVehicle.rtlFlightMode || _flightMode === _activeVehicle.smartRTLFlightMode : false
bool GuidedActionGate::_vehicleInRTLMode() const
{
    if (!_vehicle) {
        return false;
    }
    const QString flightMode = _vehicle->flightMode();
    return (flightMode == _vehicle->rtlFlightMode()) || (flightMode == _vehicle->smartRTLFlightMode());
}

// QML: _vehicleInLandMode (recomputed in on_FlightModeChanged)
//   _activeVehicle ? _flightMode === _activeVehicle.landFlightMode : false
bool GuidedActionGate::_vehicleInLandMode() const
{
    return _vehicle ? (_vehicle->flightMode() == _vehicle->landFlightMode()) : false;
}

// QML: _vehicleInMissionMode (recomputed in on_FlightModeChanged)
//   _activeVehicle ? _flightMode === _activeVehicle.missionFlightMode : false
bool GuidedActionGate::_vehicleInMissionMode() const
{
    return _vehicle ? (_vehicle->flightMode() == _vehicle->missionFlightMode()) : false;
}

// QML: _vehicleInFwdFlight — _activeVehicle ? _activeVehicle.inFwdFlight : false
bool GuidedActionGate::_vehicleInFwdFlight() const
{
    return _vehicle ? _vehicle->inFwdFlight() : false;
}

// QML: _fixedWingOnApproach — _activeVehicle ? _activeVehicle.fixedWing && _vehicleLanding : false
bool GuidedActionGate::_fixedWingOnApproach() const
{
    return _vehicle ? (_vehicle->fixedWing() && _vehicleLanding()) : false;
}

// QML: _speedLimitsAvailable
//   _activeVehicle && ((_vehicleInFwdFlight && _activeVehicle.haveFWSpeedLimits) || (!_vehicleInFwdFlight && _activeVehicle.haveMRSpeedLimits))
bool GuidedActionGate::_speedLimitsAvailable() const
{
    if (!_vehicle) {
        return false;
    }
    return (_vehicleInFwdFlight() && _vehicle->haveFWSpeedLimits()) || (!_vehicleInFwdFlight() && _vehicle->haveMRSpeedLimits());
}

// QML: _missionActive
//   _activeVehicle ? _vehicleArmed && (_vehicleInLandMode || _vehicleInRTLMode || _vehicleInMissionMode) : false
bool GuidedActionGate::_missionActive() const
{
    if (!_vehicle) {
        return false;
    }
    return _vehicleArmed() && (_vehicleInLandMode() || _vehicleInRTLMode() || _vehicleInMissionMode());
}

// QML: _useChecklist
//   QGroundControl.settingsManager.appSettings.useChecklist.rawValue && QGroundControl.corePlugin.options.preFlightChecklistUrl.toString().length
bool GuidedActionGate::_useChecklist() const
{
    return SettingsManager::instance()->appSettings()->useChecklist()->rawValue().toBool()
        && !QGCCorePlugin::instance()->options()->preFlightChecklistUrl().toString().isEmpty();
}

// QML: _enforceChecklist
//   _useChecklist && QGroundControl.settingsManager.appSettings.enforceChecklist.rawValue
bool GuidedActionGate::_enforceChecklist() const
{
    return _useChecklist() && SettingsManager::instance()->appSettings()->enforceChecklist()->rawValue().toBool();
}

// QML: _checklistPassed
//   _activeVehicle ? (_useChecklist ? (_enforceChecklist ? _activeVehicle.checkListState === Vehicle.CheckListPassed : true) : true) : true
bool GuidedActionGate::_checklistPassed() const
{
    if (!_vehicle) {
        return true;
    }
    if (!_useChecklist() || !_enforceChecklist()) {
        return true;
    }
    return (_vehicle->checkListState() == Vehicle::CheckListPassed);
}

// QML: _canArm
//   _activeVehicle ? (_checklistPassed && (!_activeVehicle.healthAndArmingCheckReport.supported || _activeVehicle.healthAndArmingCheckReport.canArm)) : false
bool GuidedActionGate::_canArm() const
{
    if (!_vehicle) {
        return false;
    }
    const HealthAndArmingCheckReport *report = _vehicle->healthAndArmingCheckReport();
    return _checklistPassed() && (!report->supported() || report->canArm());
}

// QML: _canTakeoff
//   _activeVehicle ? (_checklistPassed && (!_activeVehicle.healthAndArmingCheckReport.supported || _activeVehicle.healthAndArmingCheckReport.canTakeoff)) : false
bool GuidedActionGate::_canTakeoff() const
{
    if (!_vehicle) {
        return false;
    }
    const HealthAndArmingCheckReport *report = _vehicle->healthAndArmingCheckReport();
    return _checklistPassed() && (!report->supported() || report->canTakeoff());
}

// QML: _canStartMission
//   _activeVehicle ? (_checklistPassed && (!_activeVehicle.healthAndArmingCheckReport.supported || _activeVehicle.healthAndArmingCheckReport.canStartMission)) : false
bool GuidedActionGate::_canStartMission() const
{
    if (!_vehicle) {
        return false;
    }
    const HealthAndArmingCheckReport *report = _vehicle->healthAndArmingCheckReport();
    return _checklistPassed() && (!report->supported() || report->canStartMission());
}

// QML: _hideEmergenyStop (typo is the QML property name)
//   !_corePluginOptions.flyView.guidedBarShowEmergencyStop
// The QGCFlyViewOptions read accessors are protected (exposed only through Q_PROPERTY), so we
// read through the Qt property system exactly as the QML binding did.
bool GuidedActionGate::_hideEmergencyStop() const
{
    const QGCFlyViewOptions *flyViewOptions = QGCCorePlugin::instance()->options()->flyViewOptions();
    return !flyViewOptions->property("guidedBarShowEmergencyStop").toBool();
}

// QML: _hideOrbit — !_corePluginOptions.flyView.guidedBarShowOrbit
bool GuidedActionGate::_hideOrbit() const
{
    const QGCFlyViewOptions *flyViewOptions = QGCCorePlugin::instance()->options()->flyViewOptions();
    return !flyViewOptions->property("guidedBarShowOrbit").toBool();
}

// QML: _hideROI — !_corePluginOptions.flyView.guidedBarShowROI
bool GuidedActionGate::_hideROI() const
{
    const QGCFlyViewOptions *flyViewOptions = QGCCorePlugin::instance()->options()->flyViewOptions();
    return !flyViewOptions->property("guidedBarShowROI").toBool();
}

// QML: __guidedModeSupported — _activeVehicle ? _activeVehicle.supports.guidedMode : false
bool GuidedActionGate::_guidedModeSupported() const
{
    return _vehicle ? _vehicle->supports()->guidedMode() : false;
}

// QML: __pauseVehicleSupported — _activeVehicle ? _activeVehicle.supports.pauseVehicle : false
bool GuidedActionGate::_pauseVehicleSupported() const
{
    return _vehicle ? _vehicle->supports()->pauseVehicle() : false;
}

// QML: __roiSupported — _activeVehicle ? !_hideROI && _activeVehicle.supports.roiMode : false
bool GuidedActionGate::_roiSupported() const
{
    return _vehicle ? (!_hideROI() && _vehicle->supports()->roiMode()) : false;
}

// QML: __orbitSupported — _activeVehicle ? !_hideOrbit && _activeVehicle.supports.orbitMode : false
bool GuidedActionGate::_orbitSupported() const
{
    return _vehicle ? (!_hideOrbit() && _vehicle->supports()->orbitMode()) : false;
}

/*===========================================================================*/
/* Per-action availability predicates (QML show* properties)                 */
/*===========================================================================*/

// QML: showEmergenyStop (typo is the QML property name)
//   _guidedActionsEnabled && !_hideEmergenyStop && _vehicleArmed && _vehicleFlying
bool GuidedActionGate::_showEmergencyStop() const
{
    return _guidedActionsEnabled() && !_hideEmergencyStop() && _vehicleArmed() && _vehicleFlying();
}

// QML: showArm — _guidedActionsEnabled && !_vehicleArmed && _canArm
bool GuidedActionGate::_showArm() const
{
    return _guidedActionsEnabled() && !_vehicleArmed() && _canArm();
}

// QML: showForceArm — _guidedActionsEnabled && !_vehicleArmed
bool GuidedActionGate::_showForceArm() const
{
    return _guidedActionsEnabled() && !_vehicleArmed();
}

// QML: showDisarm
//   _guidedActionsEnabled && _vehicleArmed && (!_vehicleFlying || (_activeVehicle && (_activeVehicle.rover || _activeVehicle.sub)))
bool GuidedActionGate::_showDisarm() const
{
    return _guidedActionsEnabled() && _vehicleArmed()
        && (!_vehicleFlying() || (_vehicle && (_vehicle->rover() || _vehicle->sub())));
}

// QML: showRTL
//   _guidedActionsEnabled && _vehicleArmed && _activeVehicle.supports.guidedMode && _vehicleFlying && !_vehicleInRTLMode
bool GuidedActionGate::_showRTL() const
{
    // The QML binding dereferenced _activeVehicle unguarded; it relied on _guidedActionsEnabled
    // being false when there is no active vehicle. Guard explicitly here.
    if (!_vehicle) {
        return false;
    }
    return _guidedActionsEnabled() && _vehicleArmed() && _vehicle->supports()->guidedMode()
        && _vehicleFlying() && !_vehicleInRTLMode();
}

// QML: showTakeoff
//   _guidedActionsEnabled && (_activeVehicle.supports.guidedTakeoffWithAltitude || _activeVehicle.supports.guidedTakeoffWithoutAltitude) && !_vehicleFlying && _canTakeoff
bool GuidedActionGate::_showTakeoff() const
{
    if (!_vehicle) {
        return false;
    }
    return _guidedActionsEnabled()
        && (_vehicle->supports()->guidedTakeoffWithAltitude() || _vehicle->supports()->guidedTakeoffWithoutAltitude())
        && !_vehicleFlying() && _canTakeoff();
}

// QML: showLand
//   _guidedActionsEnabled && _activeVehicle.supports.guidedMode && _vehicleArmed && !_activeVehicle.fixedWing && !_vehicleInLandMode
bool GuidedActionGate::_showLand() const
{
    if (!_vehicle) {
        return false;
    }
    return _guidedActionsEnabled() && _vehicle->supports()->guidedMode() && _vehicleArmed()
        && !_vehicle->fixedWing() && !_vehicleInLandMode();
}

// QML: showStartMission
//   _guidedActionsEnabled && _missionAvailable && !_missionActive && !_vehicleFlying && _canStartMission
bool GuidedActionGate::_showStartMission() const
{
    return _guidedActionsEnabled() && _missionAvailable && !_missionActive()
        && !_vehicleFlying() && _canStartMission();
}

// QML: showContinueMission
//   _guidedActionsEnabled && _missionAvailable && !_missionActive && _vehicleArmed && _vehicleFlying && (_currentMissionIndex < _visualItemsCount - 1)
bool GuidedActionGate::_showContinueMission() const
{
    return _guidedActionsEnabled() && _missionAvailable && !_missionActive()
        && _vehicleArmed() && _vehicleFlying() && (_currentMissionIndex < (_visualItemsCount - 1));
}

// QML: showPause
//   _guidedActionsEnabled && _vehicleArmed && _activeVehicle.supports.pauseVehicle && _vehicleFlying && !_vehiclePaused && !_fixedWingOnApproach
bool GuidedActionGate::_showPause() const
{
    if (!_vehicle) {
        return false;
    }
    return _guidedActionsEnabled() && _vehicleArmed() && _vehicle->supports()->pauseVehicle()
        && _vehicleFlying() && !_vehiclePaused() && !_fixedWingOnApproach();
}

// QML: showChangeAlt
//   _guidedActionsEnabled && _vehicleFlying && _activeVehicle.supports.guidedMode && _vehicleArmed && !_missionActive
bool GuidedActionGate::_showChangeAlt() const
{
    if (!_vehicle) {
        return false;
    }
    return _guidedActionsEnabled() && _vehicleFlying() && _vehicle->supports()->guidedMode()
        && _vehicleArmed() && !_missionActive();
}

// QML: showChangeLoiterRadius
//   _guidedActionsEnabled && _vehicleFlying && _activeVehicle.supports.guidedMode && _vehicleArmed && !_missionActive && _vehicleInFwdFlight && fwdFlightGotoMapCircle.visible
bool GuidedActionGate::_showChangeLoiterRadius() const
{
    if (!_vehicle) {
        return false;
    }
    return _guidedActionsEnabled() && _vehicleFlying() && _vehicle->supports()->guidedMode()
        && _vehicleArmed() && !_missionActive() && _vehicleInFwdFlight() && _fwdFlightGotoMapCircleVisible;
}

// QML: showChangeSpeed
//   _guidedActionsEnabled && _vehicleFlying && _activeVehicle.supports.guidedMode && _vehicleArmed && !_missionActive && _speedLimitsAvailable
bool GuidedActionGate::_showChangeSpeed() const
{
    if (!_vehicle) {
        return false;
    }
    return _guidedActionsEnabled() && _vehicleFlying() && _vehicle->supports()->guidedMode()
        && _vehicleArmed() && !_missionActive() && _speedLimitsAvailable();
}

// QML: showOrbit
//   _guidedActionsEnabled && _vehicleFlying && __orbitSupported && !_missionActive && _activeVehicle.homePosition.isValid && !isNaN(_activeVehicle.homePosition.altitude)
bool GuidedActionGate::_showOrbit() const
{
    if (!_vehicle) {
        return false;
    }
    const QGeoCoordinate homePosition = _vehicle->homePosition();
    return _guidedActionsEnabled() && _vehicleFlying() && _orbitSupported() && !_missionActive()
        && homePosition.isValid() && !qIsNaN(homePosition.altitude());
}

// QML: showROI — _guidedActionsEnabled && _vehicleFlying && __roiSupported
bool GuidedActionGate::_showROI() const
{
    return _guidedActionsEnabled() && _vehicleFlying() && _roiSupported();
}

// QML: showLandAbort — _guidedActionsEnabled && _vehicleFlying && _fixedWingOnApproach
bool GuidedActionGate::_showLandAbort() const
{
    return _guidedActionsEnabled() && _vehicleFlying() && _fixedWingOnApproach();
}

// QML: showGotoLocation — _guidedActionsEnabled && _vehicleFlying
bool GuidedActionGate::_showGotoLocation() const
{
    return _guidedActionsEnabled() && _vehicleFlying();
}

// QML: showSetHome — _guidedActionsEnabled
bool GuidedActionGate::_showSetHome() const
{
    return _guidedActionsEnabled();
}

// QML: showSetEstimatorOrigin
//   _activeVehicle && !(_activeVehicle.sensorsPresentBits & MAVLinkEnums.MAV_SYS_STATUS_SENSOR_GPS)
bool GuidedActionGate::_showSetEstimatorOrigin() const
{
    return _vehicle && !(static_cast<uint32_t>(_vehicle->sensorsPresentBits()) & MAV_SYS_STATUS_SENSOR_GPS);
}

// QML: showChangeHeading — _guidedActionsEnabled && _vehicleFlying
bool GuidedActionGate::_showChangeHeading() const
{
    return _guidedActionsEnabled() && _vehicleFlying();
}

// QML: showResumeMission
//   _activeVehicle && !_vehicleArmed && _vehicleWasFlying && _missionAvailable && _resumeMissionIndex > 0 && (_resumeMissionIndex < _visualItemsCount - 2)
// QML comment: "The '_visualItemsCount - 2' is a hack to not trigger resume mission when a
// mission ends with an RTL item"
bool GuidedActionGate::_showResumeMission() const
{
    return _vehicle && !_vehicleArmed() && _vehicleWasFlying && _missionAvailable
        && (_resumeMissionIndex > 0) && (_resumeMissionIndex < (_visualItemsCount - 2));
}

/*===========================================================================*/
/* Public API                                                                 */
/*===========================================================================*/

bool GuidedActionGate::isActionAvailable(GuidedAction action) const
{
    switch (action) {
    case GuidedAction::RTL:
        return _showRTL();
    case GuidedAction::Land:
        return _showLand();
    case GuidedAction::Takeoff:
        return _showTakeoff();
    case GuidedAction::Arm:
        // QML confirmAction(actionArm) additionally rejected when (_vehicleFlying || !_guidedActionsEnabled);
        // both conditions are already implied by showArm.
        return _showArm();
    case GuidedAction::Disarm:
        // QML confirmAction(actionDisarm) additionally rejected when (_vehicleFlying && !(rover || sub));
        // that condition is already implied by showDisarm.
        return _showDisarm();
    case GuidedAction::EmergencyStop:
        return _showEmergencyStop();
    case GuidedAction::ChangeAlt:
        return _showChangeAlt();
    case GuidedAction::Goto:
        return _showGotoLocation();
    case GuidedAction::SetWaypoint:
        // QML has no show* predicate for Set Waypoint; the confirm dialog was shown
        // unconditionally. Gate only on vehicle presence since execution requires _activeVehicle.
        return (_vehicle != nullptr);
    case GuidedAction::Orbit:
        return _showOrbit();
    case GuidedAction::LandAbort:
        return _showLandAbort();
    case GuidedAction::StartMission:
        return _showStartMission();
    case GuidedAction::ContinueMission:
        return _showContinueMission();
    case GuidedAction::ResumeMission:
    case GuidedAction::ResumeMissionUploadFail:
        return _showResumeMission();
    case GuidedAction::Pause:
        return _showPause();
    case GuidedAction::MVPause:
    case GuidedAction::MVStartMission:
    case GuidedAction::MVArm:
    case GuidedAction::MVDisarm:
        // QML has no show* predicates for the multi-vehicle actions; they act on
        // multiVehicleManager.selectedVehicles and their confirm dialogs were shown
        // unconditionally.
        return true;
    case GuidedAction::ROI:
        return _showROI();
    case GuidedAction::ForceArm:
        return _showForceArm();
    case GuidedAction::ChangeSpeed:
        return _showChangeSpeed();
    case GuidedAction::SetHome:
        return _showSetHome();
    case GuidedAction::SetEstimatorOrigin:
        return _showSetEstimatorOrigin();
    case GuidedAction::SetFlightMode:
        // QML has no show* predicate for Set Flight Mode; the confirm dialog was shown
        // unconditionally. Gate only on vehicle presence since execution requires _activeVehicle.
        return (_vehicle != nullptr);
    case GuidedAction::ChangeHeading:
        return _showChangeHeading();
    case GuidedAction::ChangeLoiterRadius:
        return _showChangeLoiterRadius();
    }

    qCWarning(GuidedActionGateLog) << "Unknown action" << static_cast<int>(action);
    return false;
}

QString GuidedActionGate::actionTitle(GuidedAction action) const
{
    switch (action) {
    case GuidedAction::RTL:
        return tr("Return");                                    // QML: rtlTitle
    case GuidedAction::Land:
        return tr("Land");                                      // QML: landTitle
    case GuidedAction::Takeoff:
        return tr("Takeoff");                                   // QML: takeoffTitle
    case GuidedAction::Arm:
        return tr("Arm");                                       // QML: armTitle
    case GuidedAction::Disarm:
        return tr("Disarm");                                    // QML: disarmTitle
    case GuidedAction::EmergencyStop:
        return tr("EMERGENCY STOP");                            // QML: emergencyStopTitle
    case GuidedAction::ChangeAlt:
        return tr("Change Altitude");                           // QML: changeAltTitle
    case GuidedAction::Goto:
        return tr("Go To Location");                            // QML: gotoTitle
    case GuidedAction::SetWaypoint:
        return tr("Set Waypoint");                              // QML: setWaypointTitle
    case GuidedAction::Orbit:
        return tr("Orbit");                                     // QML: orbitTitle
    case GuidedAction::LandAbort:
        return tr("Land Abort");                                // QML: landAbortTitle
    case GuidedAction::StartMission:
        return tr("Start Mission");                             // QML: startMissionTitle
    case GuidedAction::ContinueMission:
        return tr("Continue Mission");                          // QML: continueMissionTitle
    case GuidedAction::ResumeMission:
        // QML: confirmAction(actionResumeMission) returned without a dialog — "Resume Mission
        // is handled in mission end dialog", so it has no title/message.
        return QString();
    case GuidedAction::ResumeMissionUploadFail:
        return tr("Resume FAILED");                             // QML: resumeMissionUploadFailTitle
    case GuidedAction::Pause:
        return tr("Pause");                                     // QML: pauseTitle
    case GuidedAction::MVPause:
        return tr("Pause (MV)");                                // QML: mvPauseTitle
    case GuidedAction::MVStartMission:
        return tr("Start Mission (MV)");                        // QML: mvStartMissionTitle
    case GuidedAction::ROI:
        return tr("ROI");                                       // QML: roiTitle
    case GuidedAction::ForceArm:
        return tr("Force Arm");                                 // QML: forceArmTitle
    case GuidedAction::ChangeSpeed:
        // QML: changeSpeedTitle — _vehicleInFwdFlight ? changeAirspeedTitle : changeCruiseSpeedTitle
        return _vehicleInFwdFlight() ? tr("Change Airspeed") : tr("Change Max Ground Speed");
    case GuidedAction::SetHome:
        return tr("Set Home");                                  // QML: setHomeTitle
    case GuidedAction::SetEstimatorOrigin:
        return tr("Set Estimator Origin");                      // QML: setEstimatorOriginTitle
    case GuidedAction::SetFlightMode:
        return tr("Set Flight Mode");                           // QML: setFlightMode (title)
    case GuidedAction::ChangeHeading:
        return tr("Change Heading");                            // QML: changeHeadingTitle
    case GuidedAction::MVArm:
        return tr("Arm (MV)");                                  // QML: mvArmTitle
    case GuidedAction::MVDisarm:
        return tr("Disarm (MV)");                               // QML: mvDisarmTitle
    case GuidedAction::ChangeLoiterRadius:
        return tr("Change Loiter Radius");                      // QML: changeLoiterRadiusTitle
    }

    qCWarning(GuidedActionGateLog) << "Unknown action" << static_cast<int>(action);
    return QString();
}

QString GuidedActionGate::actionMessage(GuidedAction action, const QString &actionData) const
{
    switch (action) {
    case GuidedAction::RTL:
        return tr("Return to the launch position of the vehicle");                                             // QML: rtlMessage
    case GuidedAction::Land:
        return tr("Land the vehicle at the current position");                                                  // QML: landMessage
    case GuidedAction::Takeoff:
        return tr("Takeoff and hold position");                                                                 // QML: takeoffMessage
    case GuidedAction::Arm:
        return tr("Arm the vehicle.");                                                                          // QML: armMessage
    case GuidedAction::Disarm:
        return tr("Disarm the vehicle");                                                                        // QML: disarmMessage
    case GuidedAction::EmergencyStop:
        return tr("WARNING: THIS WILL STOP ALL MOTORS. IF VEHICLE IS CURRENTLY IN THE AIR IT WILL CRASH.");     // QML: emergencyStopMessage
    case GuidedAction::ChangeAlt:
        return tr("Change the altitude of the vehicle up or down");                                             // QML: changeAltMessage
    case GuidedAction::Goto:
        return tr("Move the vehicle to the specified location");                                                // QML: gotoMessage
    case GuidedAction::SetWaypoint:
        return tr("Adjust current waypoint to %1").arg(actionData);                                             // QML: setWaypointMessage
    case GuidedAction::Orbit:
        return tr("Orbit the vehicle around the specified location");                                           // QML: orbitMessage
    case GuidedAction::LandAbort:
        return tr("Abort the landing sequence");                                                                // QML: landAbortMessage
    case GuidedAction::StartMission:
        return tr("Takeoff and start the current mission");                                                     // QML: startMissionMessage
    case GuidedAction::ContinueMission:
        return tr("Continue the mission from the current waypoint");                                            // QML: continueMissionMessage
    case GuidedAction::ResumeMission:
        // QML: Resume Mission is handled in the mission end dialog; it has no title/message.
        return QString();
    case GuidedAction::ResumeMissionUploadFail:
        return tr("Upload of resume mission failed. Confirm to retry upload");                                  // QML: resumeMissionUploadFailMessage
    case GuidedAction::Pause:
        return tr("Pause at current position");                                                                 // QML: pauseMessage
    case GuidedAction::MVPause:
        return tr("Pause selected vehicles at their current position");                                         // QML: mvPauseMessage
    case GuidedAction::MVStartMission:
        return tr("Takeoff and start the current mission for selected vehicles");                               // QML: mvStartMissionMessage
    case GuidedAction::ROI:
        return tr("Make the specified location a Region Of Interest");                                          // QML: roiMessage
    case GuidedAction::ForceArm:
        return tr("WARNING: This will force arming of the vehicle bypassing any safety checks.");               // QML: forceArmMessage
    case GuidedAction::ChangeSpeed:
        // QML: changeSpeedMessage — _vehicleInFwdFlight ? changeAirspeedMessage : changeCruiseSpeedMessage
        return _vehicleInFwdFlight() ? tr("Change the equivalent airspeed setpoint")
                                     : tr("Change the maximum horizontal cruise speed");
    case GuidedAction::SetHome:
        return tr("Set vehicle home as the specified location. This will affect Return to Home position");     // QML: setHomeMessage
    case GuidedAction::SetEstimatorOrigin:
        return tr("Make the specified location the estimator origin");                                          // QML: setEstimatorOriginMessage
    case GuidedAction::SetFlightMode:
        return tr("Set the vehicle flight mode to %1").arg(actionData);                                         // QML: setFlightModeMessage
    case GuidedAction::ChangeHeading:
        return tr("Set the vehicle heading towards the specified location");                                    // QML: changeHeadingMessage
    case GuidedAction::MVArm:
        return tr("Arm selected vehicles.");                                                                    // QML: mvArmMessage
    case GuidedAction::MVDisarm:
        return tr("Disarm selected vehicles.");                                                                 // QML: mvDisarmMessage
    case GuidedAction::ChangeLoiterRadius:
        return tr("Change the forward flight loiter radius");                                                   // QML: changeLoiterRadiusMessage
    }

    qCWarning(GuidedActionGateLog) << "Unknown action" << static_cast<int>(action);
    return QString();
}
