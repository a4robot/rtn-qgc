import QGroundControl
import QGroundControl.FlyView

GuidedToolStripAction {
    text:       _guidedController.showArm ? _guidedController.armTitle : _guidedController.disarmTitle
    iconSource: _guidedController.showArm ? "/src/Toolbar/Images/Disarmed.svg" : "/src/Toolbar/Images/Armed.svg"
    visible:    _guidedController.showArm || _guidedController.showDisarm
    enabled:    _guidedController.showArm || _guidedController.showDisarm
    actionID:   _guidedController.showArm ? _guidedController.actionArm : _guidedController.actionDisarm
}
