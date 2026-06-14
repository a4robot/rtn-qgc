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
                        property real imageVerticalOffset: (lightsStat & 1) ? -0.5 : 0
                        onIsPressedChanged: setTrimActive(1, isPressed)
                        text: "Trim Up"
                        iconSource: "/InstrumentValueIcons/cheveron-up.svg"
                    },
                    ToolStripAction {
                        property bool isPressed: false
                        property real imageVerticalOffset: (lightsStat & 2) ? 0.5 : 0
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
                        iconSource: "/InstrumentValueIcons/light-bulb.svg"
                        onTriggered: toggleLight(0)
                    },
                    ToolStripAction {
                        property bool isOn: relayStates[1]
                        text: "Nav."
                        iconSource: "/InstrumentValueIcons/light-bulb.svg"
                        onTriggered: toggleLight(1)
                    },
                    ToolStripAction {
                        property bool isOn: relayStates[2]
                        text: "Siren"
                        iconSource: "/InstrumentValueIcons/light-bulb.svg"
                        onTriggered: toggleLight(2)
                    },
                    ToolStripAction {
                        property bool isOn: relayStates[3]
                        text: "Port"
                        iconSource: "/InstrumentValueIcons/light-bulb.svg"
                        onTriggered: toggleLight(3)
                    },
                    ToolStripAction {
                        property bool isOn: relayStates[4]
                        text: "Stbd."
                        iconSource: "/InstrumentValueIcons/light-bulb.svg"
                        onTriggered: toggleLight(4)
                    }
                ]
            }
            model: lightsActionList.model
        }
    }

    // Top Right Info UI - Matches HTML gauge-panel design
    Rectangle {
        id: rightInfoPanel
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: ScreenTools.defaultFontPixelWidth * 0.75
        anchors.topMargin: ScreenTools.defaultFontPixelHeight * 0.75
        width: infoPanelColumn.width + _panelPadding * 2
        height: infoPanelColumn.height + _panelPadding * 2
        color: Qt.rgba(qgcPal.windowTransparent.r, qgcPal.windowTransparent.g, qgcPal.windowTransparent.b, qgcPal.windowTransparent.a)
        radius: ScreenTools.defaultFontPixelWidth / 2

        property real _panelPadding: ScreenTools.defaultFontPixelWidth * 0.4

        property bool isFuelValid: _fuelFact && fuelValue > -999.0
        property bool fuelBelow20: isFuelValid && fuelValue < 20.0 && fuelValue >= 10.0
        property bool fuelBelow10: isFuelValid && fuelValue < 10.0

        SoundEffect {
            id: alertPlayer
            source: "qrc:///res/audio/beep.wav"
            volume: 1.0
        }

        SequentialAnimation {
            id: warn20Anim
            ColorAnimation { target: rightInfoPanel; property: "color"; to: "yellow"; duration: 250 }
            ColorAnimation { target: rightInfoPanel; property: "color"; to: Qt.rgba(qgcPal.windowTransparent.r, qgcPal.windowTransparent.g, qgcPal.windowTransparent.b, qgcPal.windowTransparent.a); duration: 250 }
        }

        SequentialAnimation {
            id: warn10Anim
            ColorAnimation { target: rightInfoPanel; property: "color"; to: "red"; duration: 250 }
            ColorAnimation { target: rightInfoPanel; property: "color"; to: Qt.rgba(qgcPal.windowTransparent.r, qgcPal.windowTransparent.g, qgcPal.windowTransparent.b, qgcPal.windowTransparent.a); duration: 250 }
            PauseAnimation { duration: 400 }
            ColorAnimation { target: rightInfoPanel; property: "color"; to: "red"; duration: 250 }
            ColorAnimation { target: rightInfoPanel; property: "color"; to: Qt.rgba(qgcPal.windowTransparent.r, qgcPal.windowTransparent.g, qgcPal.windowTransparent.b, qgcPal.windowTransparent.a); duration: 250 }
        }

        Timer {
            id: warn20Timer
            interval: 10000
            repeat: true
            running: rightInfoPanel.fuelBelow20
            onTriggered: {
                alertPlayer.play();
                warn20Anim.start();
            }
            onRunningChanged: {
                if (running) triggered();
            }
        }

        Timer {
            id: warn10Timer
            interval: 5000
            repeat: true
            running: rightInfoPanel.fuelBelow10
            onTriggered: {
                alertPlayer.play();
                warn10Anim.start();
                warn10SecondBeepTimer.start();
            }
            onRunningChanged: {
                if (running) triggered();
            }
        }

        Timer {
            id: warn10SecondBeepTimer
            interval: 700
            repeat: false
            onTriggered: {
                alertPlayer.play();
            }
        }

        DeadMouseArea { anchors.fill: parent }

        Column {
            id: infoPanelColumn
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: rightInfoPanel._panelPadding
            width: Math.max(barRow.width, rudderGaugeItem.width)
            spacing: 1

            // === TOP: TRIM Header ===
            QGCLabel {
                anchors.horizontalCenter: parent.horizontalCenter
                text: {
                    var isErr = (trimValue <= -999.0)
                    return "RDR " + (isErr ? "--" : (trimValue.toFixed(0) + "%"))
                }
                font.pointSize: ScreenTools.largeFontPointSize * 0.7
                font.bold: true
                color: "white"
            }

            // === MIDDLE: Rudder Gauge (SVG-style semicircle) ===
            Item {
                id: rudderGaugeItem
                width: barRow.width * 1.3
                height: width * 0.6
                anchors.horizontalCenter: parent.horizontalCenter

                Canvas {
                    id: gaugeCanvas
                    anchors.fill: parent
                    onPaint: {
                        var ctx = getContext("2d");
                        ctx.clearRect(0, 0, width, height);

                        // Match SVG viewBox proportions: 200x120, pivot at (100,20)
                        var scale = width / 200;
                        var pivotX = 100 * scale;
                        var pivotY = 20 * scale;
                        var arcRadius = 80 * scale;

                        ctx.strokeStyle = "white";
                        ctx.fillStyle = "white";
                        ctx.lineWidth = 3 * scale;
                        ctx.lineCap = "butt";

                        // Draw the arc (120 degree arc opening downward)
                        // SVG path: M 31,60 A 80,80 0 0,0 169,60
                        // This is an arc from angle 30deg to 150deg (measured from pivot)
                        // In canvas terms from pivot: start angle = 30deg, end angle = 150deg
                        var startAngle = 30 * Math.PI / 180;  // 30 degrees
                        var endAngle = 150 * Math.PI / 180;   // 150 degrees
                        ctx.beginPath();
                        ctx.arc(pivotX, pivotY, arcRadius, startAngle, endAngle, false);
                        ctx.stroke();

                        // Draw 5 tick marks at -60, -30, 0, 30, 60 degrees from vertical
                        // In canvas angle: 90-60=30, 90-30=60, 90, 90+30=120, 90+60=150
                        var tickAngles = [30, 60, 90, 120, 150];
                        var tickInnerR = (98.5 - 10) * scale;  // inner edge
                        var tickOuterR = 98.5 * scale;         // outer edge (near arc)
                        // Actually from SVG: tick line from y=88 to y=98.5, rotated around (100,20)
                        // Distance from pivot: 88-20=68 to 98.5-20=78.5
                        tickInnerR = 68 * scale;
                        tickOuterR = 78.5 * scale;

                        ctx.lineWidth = 3 * scale;
                        for (var i = 0; i < tickAngles.length; i++) {
                            var a = tickAngles[i] * Math.PI / 180;
                            var x1 = pivotX + tickInnerR * Math.cos(a);
                            var y1 = pivotY + tickInnerR * Math.sin(a);
                            var x2 = pivotX + tickOuterR * Math.cos(a);
                            var y2 = pivotY + tickOuterR * Math.sin(a);
                            ctx.beginPath();
                            ctx.moveTo(x1, y1);
                            ctx.lineTo(x2, y2);
                            ctx.stroke();
                        }

                        // Pivot circle (r=8 in SVG)
                        ctx.beginPath();
                        ctx.arc(pivotX, pivotY, 8 * scale, 0, 2 * Math.PI);
                        ctx.fillStyle = "white";
                        ctx.fill();

                        // Needle - triangular, points from pivot down toward arc
                        // SVG: polygon points="97,20 103,20 100,80" rotated by rudder angle
                        var isErr = (rudderValue <= -999.0);
                        var maxRudder = 45;
                        var displayVal = isErr ? 0 : rudderValue;
                        var mappedVal = Math.max(-maxRudder, Math.min(displayVal, maxRudder));
                        // Map rudder to rotation: 0 = straight down (90deg), +-45 = +-60deg
                        var needleRotDeg = (mappedVal / maxRudder) * 60;
                        var needleRotRad = needleRotDeg * Math.PI / 180;

                        // Triangle vertices in SVG coords (relative to pivot)
                        var halfBase = 3 * scale;
                        var needleLen = 60 * scale;  // from pivot to tip

                        ctx.save();
                        ctx.translate(pivotX, pivotY);
                        ctx.rotate(needleRotRad);

                        ctx.beginPath();
                        ctx.moveTo(-halfBase, 0);
                        ctx.lineTo(halfBase, 0);
                        ctx.lineTo(0, needleLen);
                        ctx.closePath();
                        ctx.fillStyle = isErr ? "#666666" : "white";
                        ctx.fill();

                        ctx.restore();
                    }
                }
            }

            // === BOTTOM: 3 Bar Gauges ===
            Row {
                id: barRow
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: ScreenTools.defaultFontPixelWidth * 0.5

                property real barWidth: ScreenTools.defaultFontPixelWidth * 3.2
                property real barHeight: ScreenTools.defaultFontPixelHeight * 7

                Repeater {
                    model: [
                        { title: "FUEL",  val: fuelValue,  min: 0,   max: 100,   unit: "%",  decimals: 0, hasScale: true,  scaleTop: "F", scaleBot: "E" },
                        { title: "VOLTS", val: battValue,   min: 10,  max: 15,    unit: "V",  decimals: 1, hasScale: false, scaleTop: "",  scaleBot: "" },
                        { title: "RPM",   val: rpmValue,   min: 0,   max: 5000,  unit: "",   decimals: 0, hasScale: false, scaleTop: "",  scaleBot: "" }
                    ]

                    Column {
                        spacing: ScreenTools.defaultFontPixelWidth * 0.25

                        // Title
                        QGCLabel {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData.title
                            font.pointSize: ScreenTools.smallFontPointSize
                            font.bold: true
                            color: "white"
                        }

                        // Bar with optional scale markers
                        Item {
                            width: barRow.barWidth + (modelData.hasScale ? ScreenTools.defaultFontPixelWidth * 1.5 : 0)
                            height: barRow.barHeight
                            anchors.horizontalCenter: parent.horizontalCenter

                            // Scale marker top (e.g. "F")
                            QGCLabel {
                                visible: modelData.hasScale
                                anchors.right: barRect.left
                                anchors.rightMargin: ScreenTools.defaultFontPixelWidth * 0.3
                                anchors.top: barRect.top
                                text: modelData.scaleTop
                                font.pointSize: ScreenTools.smallFontPointSize * 0.9
                                font.bold: true
                                color: "#cccccc"
                            }

                            // Scale marker bottom (e.g. "E")
                            QGCLabel {
                                visible: modelData.hasScale
                                anchors.right: barRect.left
                                anchors.rightMargin: ScreenTools.defaultFontPixelWidth * 0.3
                                anchors.bottom: barRect.bottom
                                text: modelData.scaleBot
                                font.pointSize: ScreenTools.smallFontPointSize * 0.9
                                font.bold: true
                                color: "#cccccc"
                            }

                            // The bar itself
                            Rectangle {
                                id: barRect
                                anchors.right: parent.right
                                width: barRow.barWidth
                                height: parent.height
                                color: "#1a1a1a"
                                border.color: "#555555"
                                border.width: 2
                                radius: ScreenTools.defaultFontPixelWidth * 0.3

                                property bool isErr: modelData.val <= -999.0
                                property real mapped: isErr ? 0 : Math.max(modelData.min, Math.min(modelData.val, modelData.max))
                                property real fillFrac: (mapped - modelData.min) / (modelData.max - modelData.min)

                                // White fill from bottom
                                Rectangle {
                                    visible: !barRect.isErr
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.margins: parent.border.width
                                    height: Math.max(0, barRect.fillFrac * (parent.height - parent.border.width * 2))
                                    color: "white"
                                    radius: parent.radius > 0 ? parent.radius - parent.border.width : 0
                                }
                            }
                        }

                        // Value below bar
                        QGCLabel {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: {
                                if (modelData.val <= -999.0) return "--"
                                return modelData.val.toFixed(modelData.decimals) + modelData.unit
                            }
                            font.pointSize: ScreenTools.smallFontPointSize
                            font.bold: true
                            color: "white"
                        }
                    }
                }
            }
        }
    }
}
