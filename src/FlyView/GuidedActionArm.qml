import QGroundControl
import QGroundControl.FlyView

GuidedToolStripAction {
    property bool armed: _activeVehicle ? _activeVehicle.armed : false

    text:       armed ? qsTr("Disarm") : qsTr("Arm")
    iconSource: armed ? "/InstrumentValueIcons/pause-outline.svg" : "/InstrumentValueIcons/play-outline.svg"
    visible:    true
    enabled:    _activeVehicle ? true : false
    actionID:   armed ? _guidedController.actionDisarm : _guidedController.actionArm
}
