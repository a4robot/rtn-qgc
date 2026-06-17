import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia

import QGroundControl
import QGroundControl.Controls

Item {
    id: _root
    anchors.fill: parent

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    // Configuration
    property real tabWidth: ScreenTools.defaultFontPixelWidth * 7
    property real panelSpacing: ScreenTools.defaultFontPixelHeight * 0.5
    property real panelRadius: ScreenTools.defaultFontPixelWidth / 2

    property var _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
    property var _roverInfo: _activeVehicle ? _activeVehicle.apmRoverInfo : null

    // Safe getters for telemetry with fallback
    property var _rpmFact: _roverInfo ? _roverInfo.getFact("rpm") : null
    property real rpmValue: _rpmFact ? _rpmFact.value : 0
    property var _rudderFact: _roverInfo ? _roverInfo.getFact("rudderAngle") : null
    property real rudderValue: _rudderFact ? _rudderFact.value : 0
    property var _trimFact: _roverInfo ? _roverInfo.getFact("trimAngle") : null
    property real trimValue: _trimFact ? _trimFact.value : 0
    property var _fuelFact: _roverInfo ? _roverInfo.getFact("fuelLevel") : null
    property real fuelValue: _fuelFact ? _fuelFact.value : 0
    property var _battFact: _roverInfo ? _roverInfo.getFact("batteryVolt") : null
    property real battValue: _battFact ? _battFact.value : 0
    property var _lightsStatFact: _roverInfo ? _roverInfo.getFact("lightsStat") : null
    property int lightsStat: _lightsStatFact ? _lightsStatFact.value : 0
    property var _trimStatFact: _roverInfo ? _roverInfo.getFact("trimStat") : null
    property int trimStat: _trimStatFact ? _trimStatFact.value : 0

    onRpmValueChanged: if (gaugeCanvas) gaugeCanvas.requestPaint()
    onRudderValueChanged: if (gaugeCanvas) gaugeCanvas.requestPaint()
    onTrimValueChanged: if (gaugeCanvas) gaugeCanvas.requestPaint()
    onFuelValueChanged: if (gaugeCanvas) gaugeCanvas.requestPaint()
    onBattValueChanged: if (gaugeCanvas) gaugeCanvas.requestPaint()

    // Command sending helper
    function sendCommand(cmd, param1, param2, param3, param4, param5, param6, param7) {
        if (_activeVehicle) {
            _activeVehicle.sendCommand(
                1, // MAV_COMP_ID_AUTOPILOT1
                cmd,
                true, // showError
                param1, param2, param3, param4, param5, param6, param7
            )
        }
    }

    property var relayStates: [false, false, false, false, false]

    function toggleLight(relayIndex) {
        var isCurrentlyOn = relayStates[relayIndex]
        var newState = !isCurrentlyOn
        var newArray = []
        for(var i = 0; i < 5; i++) {
            newArray.push(i === relayIndex ? newState : relayStates[i])
        }
        relayStates = newArray
        sendCommand(181, relayIndex, newState ? 1 : 0, 0, 0, 0, 0, 0)
    }

    property bool fpvState: false
    function toggleFpv() {
        fpvState = !fpvState
        sendCommand(181, 5, fpvState ? 1 : 0, 0, 0, 0, 0, 0)
    }

    Timer {
        id: trimNoneTimer
        interval: 1000
        repeat: true
        running: true
        onTriggered: {
            sendCommand(31010, 0, 0, 0, 0, 0, 0, 0)
        }
    }

    Timer {
        id: trimActiveTimer
        interval: 500
        repeat: true
        property int activeDirection: 0
        onTriggered: {
            if (activeDirection !== 0) {
                sendCommand(31010, activeDirection, 0, 0, 0, 0, 0, 0)
            }
        }
    }

    function setTrimActive(direction, active) {
        if (active) {
            trimNoneTimer.stop()
            trimActiveTimer.activeDirection = direction
            trimActiveTimer.start()
            sendCommand(31010, direction, 0, 0, 0, 0, 0, 0)
        } else {
            if (trimActiveTimer.activeDirection === direction) {
                trimActiveTimer.stop()
                trimActiveTimer.activeDirection = 0
                trimNoneTimer.restart()
                sendCommand(31010, 0, 0, 0, 0, 0, 0, 0)
            }
        }
    }

    property var parentToolInsets: parent && parent.parent ? parent.parent.parentToolInsets : null

    // Left Panel Tabs
    Column {
        id: leftTabs
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.topMargin: (parentToolInsets ? parentToolInsets.topEdgeLeftInset : 0) + panelSpacing
        spacing: panelSpacing

        scale: 0.8
        transformOrigin: Item.TopLeft

        // FPV and Trim Tab
        ToolStrip {
            maxHeight: _root.height
            ToolStripActionList {
                id: fpvTrimActionList
                model: [
                    ToolStripAction {
                        property bool mirrorIcon: fpvState
                        text: "FPV"
                        iconSource: "/InstrumentValueIcons/video-camera.svg"
                        onTriggered: toggleFpv()
                    },
                    ToolStripAction {
                        property bool isPressed: false
                        property real imageVerticalOffset: (trimStat & 1) ? -0.25 : 0
                        onIsPressedChanged: setTrimActive(1, isPressed)
                        text: "Trim Up"
                        iconSource: "/InstrumentValueIcons/cheveron-up.svg"
                    },
                    ToolStripAction {
                        property bool isPressed: false
                        property real imageVerticalOffset: (trimStat & 2) ? 0.25 : 0
                        onIsPressedChanged: setTrimActive(2, isPressed)
                        text: "Trim Dn"
                        iconSource: "/InstrumentValueIcons/cheveron-down.svg"
                    }
                ]
            }
            model: fpvTrimActionList.model
        }

        // Lights Tab
        ToolStrip {
            maxHeight: _root.height
            ToolStripActionList {
                id: lightsActionList
                model: [
                    ToolStripAction {
                        property bool isOn: relayStates[0]
                        text: "Head"
                        iconSource: (lightsStat & 1) ? "/InstrumentValueIcons/light-bulb-solid.svg" : "/InstrumentValueIcons/light-bulb.svg"
                        onTriggered: toggleLight(0)
                    },
                    ToolStripAction {
                        property bool isOn: relayStates[1]
                        text: "Nav."
                        iconSource: (lightsStat & 2) ? "/InstrumentValueIcons/light-bulb-solid.svg" : "/InstrumentValueIcons/light-bulb.svg"
                        onTriggered: toggleLight(1)
                    },
                    ToolStripAction {
                        property bool isOn: relayStates[2]
                        text: "Siren"
                        iconSource: (lightsStat & 4) ? "/InstrumentValueIcons/light-bulb-solid.svg" : "/InstrumentValueIcons/light-bulb.svg"
                        onTriggered: toggleLight(2)
                    },
                    ToolStripAction {
                        property bool isOn: relayStates[3]
                        text: "Port"
                        iconSource: (lightsStat & 8) ? "/InstrumentValueIcons/light-bulb-solid.svg" : "/InstrumentValueIcons/light-bulb.svg"
                        onTriggered: toggleLight(3)
                    },
                    ToolStripAction {
                        property bool isOn: relayStates[4]
                        text: "Stbd."
                        iconSource: (lightsStat & 16) ? "/InstrumentValueIcons/light-bulb-solid.svg" : "/InstrumentValueIcons/light-bulb.svg"
                        onTriggered: toggleLight(4)
                    }
                ]
            }
            model: lightsActionList.model
        }
    }

}
