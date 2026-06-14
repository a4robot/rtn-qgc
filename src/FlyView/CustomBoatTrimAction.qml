import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView

ToolStripAction {
    id: _root
    text: qsTr("Trim & FPV")
    iconSource: "/src/Toolbar/Images/CameraIcon.svg"

    dropDownComponent: Component {
        Rectangle {
            width: ScreenTools.defaultFontPixelWidth * 12
            height: ScreenTools.defaultFontPixelHeight * 10
            color: qgcPal.window
            border.color: qgcPal.text
            radius: ScreenTools.defaultFontPixelWidth / 2

            property var _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
            property var _roverInfo: _activeVehicle ? _activeVehicle.apmRoverInfo : null
            property var _trimStatFact: _roverInfo ? _roverInfo.getFact("trimStat") : null
            property int trimStat: _trimStatFact ? _trimStatFact.value : 0

            property bool fpvToggle: false

            function sendCommand(cmd, param1, param2, param3, param4, param5, param6, param7) {
                if (_activeVehicle) {
                    _activeVehicle.sendMavCommand(
                        _activeVehicle.defaultComponentId,
                        cmd,
                        true, // showError
                        param1, param2, param3, param4, param5, param6, param7
                    )
                }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: ScreenTools.defaultFontPixelWidth

                QGCButton {
                    text: qsTr("FPV Toggle")
                    Layout.fillWidth: true
                    onClicked: {
                        fpvToggle = !fpvToggle
                        sendCommand(181, 5, fpvToggle ? 1 : 0, 0, 0, 0, 0, 0)
                    }
                }

                QGCButton {
                    text: qsTr("Trim UP")
                    Layout.fillWidth: true
                    highlighted: (trimStat & 1) !== 0
                    onClicked: {
                        sendCommand(1089, 1, 0, 0, 0, 0, 0, 0)
                    }
                }

                QGCButton {
                    text: qsTr("Trim DOWN")
                    Layout.fillWidth: true
                    highlighted: (trimStat & 2) !== 0
                    onClicked: {
                        sendCommand(1089, 2, 0, 0, 0, 0, 0, 0)
                    }
                }
            }
        }
    }
}
