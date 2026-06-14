import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView

ToolStripAction {
    id: _root
    text: qsTr("Lights")
    iconSource: "/resources/InstrumentValueIcons/light-bulb.svg"

    dropDownComponent: Component {
        Rectangle {
            width: ScreenTools.defaultFontPixelWidth * 12
            height: ScreenTools.defaultFontPixelHeight * 15
            color: qgcPal.window
            border.color: qgcPal.text
            radius: ScreenTools.defaultFontPixelWidth / 2

            property var _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
            property var _roverInfo: _activeVehicle ? _activeVehicle.apmRoverInfo : null
            property var _lightsStatFact: _roverInfo ? _roverInfo.getFact("lightsStat") : null
            property int lightsStat: _lightsStatFact ? _lightsStatFact.value : 0

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

            function toggleRelay(index, bitMask) {
                var isCurrentlyOn = (lightsStat & bitMask) !== 0
                sendCommand(181, index, isCurrentlyOn ? 0 : 1, 0, 0, 0, 0, 0)
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: ScreenTools.defaultFontPixelWidth

                QGCButton {
                    text: qsTr("Nav")
                    Layout.fillWidth: true
                    highlighted: (lightsStat & 1) !== 0
                    onClicked: toggleRelay(0, 1) // RELAY1
                }
                QGCButton {
                    text: qsTr("Siren")
                    Layout.fillWidth: true
                    highlighted: (lightsStat & 2) !== 0
                    onClicked: toggleRelay(1, 2) // RELAY2
                }
                QGCButton {
                    text: qsTr("Head")
                    Layout.fillWidth: true
                    highlighted: (lightsStat & 4) !== 0
                    onClicked: toggleRelay(2, 4) // RELAY3
                }
                QGCButton {
                    text: qsTr("Port")
                    Layout.fillWidth: true
                    highlighted: (lightsStat & 8) !== 0
                    onClicked: toggleRelay(3, 8) // RELAY4
                }
                QGCButton {
                    text: qsTr("Stbd")
                    Layout.fillWidth: true
                    highlighted: (lightsStat & 16) !== 0
                    onClicked: toggleRelay(4, 16) // RELAY5
                }
            }
        }
    }
}
