#pragma once

#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QString>

class Vehicle;

Q_DECLARE_LOGGING_CATEGORY(GuidedActionGateLog)

/// Pure C++ port of the guided action gating logic from src/FlyView/GuidedActionsController.qml.
///
/// This class decides which guided actions (arm, disarm, takeoff, land, RTL, pause, ...) are
/// currently available given vehicle state, and provides the per-action title/message strings.
/// It intentionally contains no QML/Quick dependencies so it can back non-QML frontends.
///
/// Each ported predicate cites the QML property it was ported from so the logic can be
/// reviewed 1:1 against GuidedActionsController.qml.
///
/// Inputs:
///   - Vehicle state comes from the Vehicle set via setVehicle() (mirrors the QML
///     `_activeVehicle`; may be null, all accessors are null-safe).
///   - Mission state came from the QML-context `missionController` in the original code and
///     must be fed in through the setMission*() methods.
///   - The forward flight goto map circle visibility (UI state used by showChangeLoiterRadius)
///     must be fed in through setFwdFlightGotoMapCircleVisible().
class GuidedActionGate : public QObject
{
    Q_OBJECT

public:
    /// Guided actions. Numeric values match the readonly `action*` int constants in
    /// GuidedActionsController.qml (15 was `_actionUnused` and 23 was never assigned there,
    /// so both are skipped here).
    enum class GuidedAction {
        RTL                     = 1,    // actionRTL
        Land                    = 2,    // actionLand
        Takeoff                 = 3,    // actionTakeoff
        Arm                     = 4,    // actionArm
        Disarm                  = 5,    // actionDisarm
        EmergencyStop           = 6,    // actionEmergencyStop
        ChangeAlt               = 7,    // actionChangeAlt
        Goto                    = 8,    // actionGoto
        SetWaypoint             = 9,    // actionSetWaypoint
        Orbit                   = 10,   // actionOrbit
        LandAbort               = 11,   // actionLandAbort
        StartMission            = 12,   // actionStartMission
        ContinueMission         = 13,   // actionContinueMission
        ResumeMission           = 14,   // actionResumeMission
        ResumeMissionUploadFail = 16,   // actionResumeMissionUploadFail
        Pause                   = 17,   // actionPause
        MVPause                 = 18,   // actionMVPause
        MVStartMission          = 19,   // actionMVStartMission
        ROI                     = 20,   // actionROI
        ForceArm                = 21,   // actionForceArm
        ChangeSpeed             = 22,   // actionChangeSpeed
        SetHome                 = 24,   // actionSetHome
        SetEstimatorOrigin      = 25,   // actionSetEstimatorOrigin
        SetFlightMode           = 26,   // actionSetFlightMode
        ChangeHeading           = 27,   // actionChangeHeading
        MVArm                   = 28,   // actionMVArm
        MVDisarm                = 29,   // actionMVDisarm
        ChangeLoiterRadius      = 30,   // actionChangeLoiterRadius
    };
    Q_ENUM(GuidedAction)

    explicit GuidedActionGate(QObject *parent = nullptr);
    ~GuidedActionGate() override;

    /// Sets the vehicle whose state gates the actions. Mirrors the QML `_activeVehicle`.
    /// May be null (no vehicle connected); all gating then evaluates as in QML with a
    /// null `_activeVehicle`.
    void setVehicle(Vehicle *vehicle);
    Vehicle *vehicle() const { return _vehicle; }

    // Mission state inputs. In the QML original these were read from the FlyView
    // `missionController` instance which only exists in QML context.
    void setMissionAvailable(bool available) { _missionAvailable = available; }    ///< QML: _missionAvailable (missionController.containsItems)
    void setVisualItemsCount(int count) { _visualItemsCount = count; }              ///< QML: _visualItemsCount (missionController.visualItems.count)
    void setCurrentMissionIndex(int index) { _currentMissionIndex = index; }        ///< QML: _currentMissionIndex (missionController.currentMissionIndex)
    void setResumeMissionIndex(int index) { _resumeMissionIndex = index; }          ///< QML: _resumeMissionIndex (missionController.resumeMissionIndex)

    /// UI state input. QML: fwdFlightGotoMapCircle.visible, used only by showChangeLoiterRadius.
    void setFwdFlightGotoMapCircleVisible(bool visible) { _fwdFlightGotoMapCircleVisible = visible; }

    /// Returns true if the given action is currently available given vehicle/mission state.
    /// Ports the QML show* boolean property for each action (see the per-predicate helpers).
    bool isActionAvailable(GuidedAction action) const;

    /// Returns the user-facing confirmation title for the action.
    /// Ports the readonly *Title string properties from GuidedActionsController.qml.
    QString actionTitle(GuidedAction action) const;

    /// Returns the user-facing confirmation message for the action.
    /// Ports the *Message string properties from GuidedActionsController.qml.
    ///     @param actionData Interpolated into messages that used `_actionData` in QML
    ///                       (SetWaypoint: waypoint number, SetFlightMode: flight mode name).
    QString actionMessage(GuidedAction action, const QString &actionData = QString()) const;

private:
    // ---- Vehicle/plugin state helpers ported 1:1 from GuidedActionsController.qml ----

    bool _guidedActionsEnabled() const;     ///< QML: _guidedActionsEnabled
    bool _rcRSSIAvailable() const;          ///< QML: _rcRSSIAvailable
    bool _vehicleArmed() const;             ///< QML: _vehicleArmed
    bool _vehicleFlying() const;            ///< QML: _vehicleFlying
    bool _vehicleLanding() const;           ///< QML: _vehicleLanding
    bool _vehiclePaused() const;            ///< QML: _vehiclePaused (derived in on_FlightModeChanged)
    bool _vehicleInRTLMode() const;         ///< QML: _vehicleInRTLMode (derived in on_FlightModeChanged)
    bool _vehicleInLandMode() const;        ///< QML: _vehicleInLandMode (derived in on_FlightModeChanged)
    bool _vehicleInMissionMode() const;     ///< QML: _vehicleInMissionMode (derived in on_FlightModeChanged)
    bool _vehicleInFwdFlight() const;       ///< QML: _vehicleInFwdFlight
    bool _fixedWingOnApproach() const;      ///< QML: _fixedWingOnApproach
    bool _speedLimitsAvailable() const;     ///< QML: _speedLimitsAvailable
    bool _missionActive() const;            ///< QML: _missionActive
    bool _useChecklist() const;             ///< QML: _useChecklist
    bool _enforceChecklist() const;         ///< QML: _enforceChecklist
    bool _checklistPassed() const;          ///< QML: _checklistPassed
    bool _canArm() const;                   ///< QML: _canArm
    bool _canTakeoff() const;               ///< QML: _canTakeoff
    bool _canStartMission() const;          ///< QML: _canStartMission
    bool _hideEmergencyStop() const;        ///< QML: _hideEmergenyStop (typo preserved in QML only)
    bool _hideOrbit() const;                ///< QML: _hideOrbit
    bool _hideROI() const;                  ///< QML: _hideROI
    bool _guidedModeSupported() const;      ///< QML: __guidedModeSupported
    bool _pauseVehicleSupported() const;    ///< QML: __pauseVehicleSupported
    bool _roiSupported() const;             ///< QML: __roiSupported
    bool _orbitSupported() const;           ///< QML: __orbitSupported

    // ---- Per-action availability predicates ported 1:1 from the QML show* properties ----

    bool _showEmergencyStop() const;        ///< QML: showEmergenyStop
    bool _showArm() const;                  ///< QML: showArm
    bool _showForceArm() const;             ///< QML: showForceArm
    bool _showDisarm() const;               ///< QML: showDisarm
    bool _showRTL() const;                  ///< QML: showRTL
    bool _showTakeoff() const;              ///< QML: showTakeoff
    bool _showLand() const;                 ///< QML: showLand
    bool _showStartMission() const;         ///< QML: showStartMission
    bool _showContinueMission() const;      ///< QML: showContinueMission
    bool _showPause() const;                ///< QML: showPause
    bool _showChangeAlt() const;            ///< QML: showChangeAlt
    bool _showChangeLoiterRadius() const;   ///< QML: showChangeLoiterRadius
    bool _showChangeSpeed() const;          ///< QML: showChangeSpeed
    bool _showOrbit() const;                ///< QML: showOrbit
    bool _showROI() const;                  ///< QML: showROI
    bool _showLandAbort() const;            ///< QML: showLandAbort
    bool _showGotoLocation() const;         ///< QML: showGotoLocation
    bool _showSetHome() const;              ///< QML: showSetHome
    bool _showSetEstimatorOrigin() const;   ///< QML: showSetEstimatorOrigin
    bool _showChangeHeading() const;        ///< QML: showChangeHeading
    bool _showResumeMission() const;        ///< QML: showResumeMission

    void _updateVehicleWasFlying();

    Vehicle *_vehicle = nullptr;                    ///< QML: _activeVehicle
    bool _missionAvailable = false;                 ///< QML: _missionAvailable
    int _visualItemsCount = 0;                      ///< QML: _visualItemsCount
    int _currentMissionIndex = 0;                   ///< QML: _currentMissionIndex
    int _resumeMissionIndex = 0;                    ///< QML: _resumeMissionIndex
    bool _vehicleWasFlying = false;                 ///< QML: _vehicleWasFlying (latched in on_VehicleFlyingChanged)
    bool _lastVehicleFlying = false;                ///< Tracks flying transitions to latch _vehicleWasFlying
    bool _fwdFlightGotoMapCircleVisible = false;    ///< QML: fwdFlightGotoMapCircle.visible
};
