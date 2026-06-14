import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

Item {
    id: _root
    anchors.fill: parent

    Text {
        id: _measureText
        text: "X"
        font.pointSize: 10
        opacity: 0
    }
    // Provide a solid fallback in case contentWidth evaluates to 0 momentarily
    property real myFontPixelWidth: Math.max(_measureText.contentWidth, 10)
    property real myFontPixelHeight: Math.max(_measureText.contentHeight, 15)
        property real myFontPointSize: 10
    property var qgcPal: QGroundControl.globalPalette






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

    onRpmValueChanged: if (rpmCanvas) rpmCanvas.requestPaint()
    onRudderValueChanged: if (rudderCanvas) rudderCanvas.requestPaint()
    onTrimValueChanged: if (trimCanvas) trimCanvas.requestPaint()
    onFuelValueChanged: if (fuelCanvas) fuelCanvas.requestPaint()
    onBattValueChanged: if (battCanvas) battCanvas.requestPaint()

    // MAV_CMD definitions
    readonly property int mavCmdDoSetServo: 183
    readonly property int mavCmdDoSetRelay: 181

    // Command sending helper
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

    // Toggle lighting via AUX channels (using DO_SET_SERVO as standard for AUX out in APM)
    // The exact channel number might be 9 for AUX1, 10 for AUX2 etc.
    function toggleLight(auxIndex, bitMask) {
        var isCurrentlyOn = (lightsStat & bitMask) !== 0
        var pwm = isCurrentlyOn ? 1000 : 2000
        sendCommand(mavCmdDoSetServo, auxIndex, pwm, 0, 0, 0, 0, 0)
    }

    // Container for all left-anchored UI
    Column {
        id: rightPanel
        visible: true
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: myFontPixelWidth * 1.5
        anchors.topMargin: myFontPixelHeight * 1 // Match bottom right map/video top edge, maybe standard margin
        spacing: myFontPixelHeight * 1.5
        width: myFontPixelWidth * 12

        // Telemetry Panel
        Rectangle {
            width: parent.width
            height: myFontPixelHeight * 28
            color: qgcPal.windowShade
            radius: myFontPixelWidth
            border.color: qgcPal.text

            Column {
                anchors.fill: parent
                anchors.margins: myFontPixelWidth
                spacing: myFontPixelHeight * 1.5

                // RPM Gauge
                Item {
                    width: parent.width
                    height: myFontPixelHeight * 6

                    QGCLabel {
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: (rpmValue / 1000).toFixed(1) + "k"
                        font.pointSize: myFontPointSize * 1.2
                        font.bold: true
                        color: qgcPal.text
                    }

                    Canvas {
                        id: rpmCanvas
                        anchors.fill: parent
                        onPaint: {
                            var ctx = getContext("2d");
                            ctx.clearRect(0, 0, width, height);
                            var cx = width / 2;
                            var cy = height * 0.8;
                            var radius = width * 0.4;

                            // Scale Arc
                            ctx.beginPath();
                            ctx.arc(cx, cy, radius, Math.PI, 0);
                            ctx.strokeStyle = qgcPal.text;
                            ctx.lineWidth = 2;
                            ctx.stroke();

                            // Ticks
                            for (var i=0; i<=4; i++) {
                                var angle = Math.PI - i * (Math.PI/4);
                                var x1 = cx + radius * Math.cos(angle);
                                var y1 = cy - radius * Math.sin(angle);
                                var x2 = cx + (radius-5) * Math.cos(angle);
                                var y2 = cy - (radius-5) * Math.sin(angle);
                                ctx.beginPath();
                                ctx.moveTo(x1, y1);
                                ctx.lineTo(x2, y2);
                                ctx.stroke();
                            }

                            // Needle
                            var maxRpm = 4000;
                            var isError = (rpmValue <= -999.0);
                            ctx.strokeStyle = isError ? qgcPal.colorGrey : qgcPal.text;
                            ctx.fillStyle = isError ? qgcPal.colorGrey : qgcPal.text;
                            var displayRpmValue = isError ? 0 : rpmValue;
                            var mappedVal = Math.max(0, Math.min(displayRpmValue, maxRpm));
                            var needleAngle = Math.PI - (mappedVal/maxRpm)*Math.PI;
                            ctx.beginPath();
                            ctx.moveTo(cx, cy);
                            ctx.lineTo(cx + (radius-2) * Math.cos(needleAngle), cy - (radius-2) * Math.sin(needleAngle));
                            ctx.stroke();

                            ctx.beginPath();
                            ctx.arc(cx, cy, 3, 0, 2*Math.PI);
                            ctx.fillStyle = isError ? qgcPal.colorGrey : qgcPal.text;
                            ctx.fill();
                        }
                    }
                }

                // Rudder
                Item {
                    width: parent.width
                    height: myFontPixelHeight * 3

                    Canvas {
                        id: rudderCanvas
                        anchors.fill: parent
                        onPaint: {
                            var ctx = getContext("2d");
                            ctx.clearRect(0, 0, width, height);
                            var cy = height/2;

                            var isError = (rudderValue <= -999.0);
                            ctx.strokeStyle = isError ? qgcPal.colorGrey : qgcPal.text;
                            ctx.fillStyle = isError ? qgcPal.colorGrey : qgcPal.text;
                            ctx.lineWidth = 2;

                            // Line
                            ctx.beginPath();
                            ctx.moveTo(5, cy);
                            ctx.lineTo(width-5, cy);
                            ctx.stroke();

                            // Center tick
                            ctx.beginPath();
                            ctx.moveTo(width/2, cy-5);
                            ctx.lineTo(width/2, cy+5);
                            ctx.stroke();

                            // Indicator
                            var maxRudder = 45; // 45 deg

                            var displayRudderValue = isError ? 0 : rudderValue;
                            var mappedRudd = Math.max(-maxRudder, Math.min(displayRudderValue, maxRudder));
                            var indX = width/2 + (mappedRudd/maxRudder) * (width/2 - 5);

                            ctx.beginPath();
                            ctx.moveTo(indX, cy-5);
                            ctx.lineTo(indX-5, cy+5);
                            ctx.lineTo(indX+5, cy+5);
                            ctx.fillStyle = isError ? qgcPal.colorGrey : qgcPal.text;
                            ctx.fill();
                        }
                    }

                    QGCLabel {
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        text: "L"
                        font.pointSize: myFontPointSize * 0.7
                        color: qgcPal.text
                    }
                    QGCLabel {
                        anchors.bottom: parent.bottom
                        anchors.right: parent.right
                        text: "R"
                        font.pointSize: myFontPointSize * 0.7
                        color: qgcPal.text
                    }
                }

                // Vertical Scales Container
                Row {
                    width: parent.width
                    height: parent.height - y // fill remaining
                    spacing: (width - 3*myFontPixelWidth * 2.5) / 2

                    // Trim Scale
                    Column {
                        width: myFontPixelWidth * 2.5
                        height: parent.height

                        Item {
                            width: parent.width
                            height: parent.height - trimLbl.height - 5
                            Canvas {
                                id: trimCanvas
                                anchors.fill: parent
                                onPaint: {
                                    var ctx = getContext("2d");
                                    ctx.clearRect(0, 0, width, height);
                                    var isError = (trimValue <= -999.0);
                                    ctx.strokeStyle = isError ? qgcPal.colorGrey : qgcPal.text;
                                    ctx.fillStyle = isError ? qgcPal.colorGrey : qgcPal.text;
                                    ctx.lineWidth = 2;
                                    var cx = width/2;

                                    ctx.beginPath();
                                    ctx.moveTo(cx, 5);
                                    ctx.lineTo(cx, height-5);
                                    ctx.stroke();

                                    // Ticks
                                    ctx.beginPath(); ctx.moveTo(cx-5, 5); ctx.lineTo(cx+5, 5); ctx.stroke();
                                    ctx.beginPath(); ctx.moveTo(cx-5, height/2); ctx.lineTo(cx+5, height/2); ctx.stroke();
                                    ctx.beginPath(); ctx.moveTo(cx-5, height-5); ctx.lineTo(cx+5, height-5); ctx.stroke();

                                    // Indicator
                                    var maxTrim = 10;
                                    var displayTrimValue = isError ? 0 : trimValue;
                                    var mapped = Math.max(-maxTrim, Math.min(displayTrimValue, maxTrim));
                                    // negative trim = down, positive = up
                                    var indY = height/2 - (mapped/maxTrim) * (height/2 - 5);

                                    ctx.beginPath();
                                    ctx.moveTo(cx-8, indY-5);
                                    ctx.lineTo(cx, indY);
                                    ctx.lineTo(cx-8, indY+5);
                                    ctx.fillStyle = isError ? qgcPal.colorGrey : qgcPal.text;
                                    ctx.fill();
                                }
                            }
                        }
                        QGCLabel {
                            id: trimLbl
                            text: "TRM"
                            font.pointSize: myFontPointSize * 0.7
                            color: qgcPal.text
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                    }

                    // Fuel Scale
                    Column {
                        width: myFontPixelWidth * 2.5
                        height: parent.height

                        Item {
                            width: parent.width
                            height: parent.height - fuelLbl.height - 5
                            Canvas {
                                id: fuelCanvas
                                anchors.fill: parent
                                onPaint: {
                                    var ctx = getContext("2d");
                                    ctx.clearRect(0, 0, width, height);
                                    var isError = (fuelValue <= -999.0);
                                    ctx.strokeStyle = isError ? qgcPal.colorGrey : qgcPal.text;
                                    ctx.fillStyle = isError ? qgcPal.colorGrey : qgcPal.text;
                                    ctx.lineWidth = 2;
                                    var cx = width/2;

                                    ctx.beginPath();
                                    ctx.moveTo(cx, 5);
                                    ctx.lineTo(cx, height-5);
                                    ctx.stroke();

                                    // Ticks
                                    ctx.beginPath(); ctx.moveTo(cx-5, 5); ctx.lineTo(cx+5, 5); ctx.stroke();
                                    ctx.beginPath(); ctx.moveTo(cx-3, height/2); ctx.lineTo(cx+3, height/2); ctx.stroke();
                                    ctx.beginPath(); ctx.moveTo(cx-5, height-5); ctx.lineTo(cx+5, height-5); ctx.stroke();

                                    // Fill
                                    var displayFuelValue = isError ? 0 : fuelValue;
                                    var mapped = Math.max(0, Math.min(displayFuelValue, 100));
                                    var fillH = (mapped/100) * (height-10);

                                    ctx.beginPath();
                                    ctx.moveTo(cx, height-5);
                                    ctx.lineTo(cx, height-5 - fillH);
                                    ctx.strokeStyle = isError ? qgcPal.colorGrey : qgcPal.text;
                                    ctx.lineWidth = 4;
                                    ctx.stroke();
                                }
                            }
                        }
                        QGCLabel {
                            id: fuelLbl
                            text: "FUL"
                            font.pointSize: myFontPointSize * 0.7
                            color: qgcPal.text
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                    }

                    // Battery Scale
                    Column {
                        width: myFontPixelWidth * 2.5
                        height: parent.height

                        Item {
                            width: parent.width
                            height: parent.height - battLbl.height - 5
                            Canvas {
                                id: battCanvas
                                anchors.fill: parent
                                onPaint: {
                                    var ctx = getContext("2d");
                                    ctx.clearRect(0, 0, width, height);
                                    var isError = (battValue <= -999.0);
                                    ctx.strokeStyle = isError ? qgcPal.colorGrey : qgcPal.text;
                                    ctx.fillStyle = isError ? qgcPal.colorGrey : qgcPal.text;
                                    ctx.lineWidth = 2;
                                    var cx = width/2;

                                    ctx.beginPath();
                                    ctx.moveTo(cx, 5);
                                    ctx.lineTo(cx, height-5);
                                    ctx.stroke();

                                    // Ticks
                                    ctx.beginPath(); ctx.moveTo(cx-5, 5); ctx.lineTo(cx+5, 5); ctx.stroke();
                                    ctx.beginPath(); ctx.moveTo(cx-3, height/2); ctx.lineTo(cx+3, height/2); ctx.stroke();
                                    ctx.beginPath(); ctx.moveTo(cx-5, height-5); ctx.lineTo(cx+5, height-5); ctx.stroke();

                                    // Fill
                                    var maxBatt = 16.8;
                                    var minBatt = 10.0;
                                    var displayBattValue = isError ? minBatt : battValue;
                                    var mapped = Math.max(0, Math.min((displayBattValue-minBatt)/(maxBatt-minBatt)*100, 100));
                                    var fillH = (mapped/100) * (height-10);

                                    ctx.beginPath();
                                    ctx.moveTo(cx, height-5);
                                    ctx.lineTo(cx, height-5 - fillH);
                                    ctx.strokeStyle = isError ? qgcPal.colorGrey : qgcPal.text;
                                    ctx.lineWidth = 4;
                                    ctx.stroke();
                                }
                            }
                        }
                        QGCLabel {
                            id: battLbl
                            text: "BAT"
                            font.pointSize: myFontPointSize * 0.7
                            color: qgcPal.text
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                    }
                }
            }
        }

        // Helper component for buttons


            }
}
