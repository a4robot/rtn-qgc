import QGroundControl
import QGroundControl.FlyView

GuidedToolStripAction {
    text:       _guidedController._vehicleArmed ? _guidedController.disarmTitle : _guidedController.armTitle
    iconSource: _guidedController._vehicleArmed ? "/res/disarm.svg" : "/res/arm.svg"
    visible:    _guidedController.showArm || _guidedController.showDisarm
    enabled:    _guidedController.showArm || _guidedController.showDisarm
    actionID:   _guidedController._vehicleArmed ? _guidedController.actionDisarm : _guidedController.actionArm
}
