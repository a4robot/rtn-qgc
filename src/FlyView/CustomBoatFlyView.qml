import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

Item {
    id: _root
    anchors.fill: parent

    property var _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
    property var _roverInfo: _activeVehicle ? _activeVehicle.apmRoverInfo : null

    // Safe getters for telemetry with fallback
    property real rpmValue: _roverInfo && _roverInfo.getFact("rpm") ? _roverInfo.getFact("rpm").value : 0
    property real rudderValue: _roverInfo && _roverInfo.getFact("rudderAngle") ? _roverInfo.getFact("rudderAngle").value : 0
    property real trimValue: _roverInfo && _roverInfo.getFact("trimAngle") ? _roverInfo.getFact("trimAngle").value : 0
    property real fuelValue: _roverInfo && _roverInfo.getFact("fuelLevel") ? _roverInfo.getFact("fuelLevel").value : 0
    property real battValue: _roverInfo && _roverInfo.getFact("batteryVolt") ? _roverInfo.getFact("batteryVolt").value : 0
    property int lightsStat: _roverInfo && _roverInfo.getFact("lightsStat") ? _roverInfo.getFact("lightsStat").value : 0
    property int trimStat: _roverInfo && _roverInfo.getFact("trimStat") ? _roverInfo.getFact("trimStat").value : 0

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
        id: leftPanel
        visible: true
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: (Qt.application.font.pixelSize / 2) * 1.5
        anchors.topMargin: Qt.application.font.pixelSize * 4 // avoid top toolbar
        spacing: Qt.application.font.pixelSize * 1.5
        width: (Qt.application.font.pixelSize / 2) * 12

        // Telemetry Panel
        Rectangle {
            width: parent.width
            height: Qt.application.font.pixelSize * 28
            color: Qt.rgba(0.1, 0.1, 0.1, 0.7)
            radius: (Qt.application.font.pixelSize / 2)
            border.color: Qt.rgba(1, 1, 1, 0.2)

            Column {
                anchors.fill: parent
                anchors.margins: (Qt.application.font.pixelSize / 2)
                spacing: Qt.application.font.pixelSize * 1.5

                // RPM Gauge
                Item {
                    width: parent.width
                    height: Qt.application.font.pixelSize * 6

                    QGCLabel {
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: (rpmValue / 1000).toFixed(1) + "k"
                        font.pointSize: Qt.application.font.pointSize * 1.2
                        font.bold: true
                        color: "white"
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
                            ctx.strokeStyle = "white";
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
                            var mappedVal = Math.max(0, Math.min(rpmValue, maxRpm));
                            var needleAngle = Math.PI - (mappedVal/maxRpm)*Math.PI;
                            ctx.beginPath();
                            ctx.moveTo(cx, cy);
                            ctx.lineTo(cx + (radius-2) * Math.cos(needleAngle), cy - (radius-2) * Math.sin(needleAngle));
                            ctx.stroke();

                            ctx.beginPath();
                            ctx.arc(cx, cy, 3, 0, 2*Math.PI);
                            ctx.fillStyle = "white";
                            ctx.fill();
                        }
                    }
                }

                // Rudder
                Item {
                    width: parent.width
                    height: Qt.application.font.pixelSize * 3

                    Canvas {
                        id: rudderCanvas
                        anchors.fill: parent
                        onPaint: {
                            var ctx = getContext("2d");
                            ctx.clearRect(0, 0, width, height);
                            var cy = height/2;

                            ctx.strokeStyle = "white";
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
                            var mappedRudd = Math.max(-maxRudder, Math.min(rudderValue, maxRudder));
                            var indX = width/2 + (mappedRudd/maxRudder) * (width/2 - 5);

                            ctx.beginPath();
                            ctx.moveTo(indX, cy-5);
                            ctx.lineTo(indX-5, cy+5);
                            ctx.lineTo(indX+5, cy+5);
                            ctx.fillStyle = "white";
                            ctx.fill();
                        }
                    }

                    QGCLabel {
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        text: "L"
                        font.pointSize: Qt.application.font.pointSize * 0.7
                        color: "white"
                    }
                    QGCLabel {
                        anchors.bottom: parent.bottom
                        anchors.right: parent.right
                        text: "R"
                        font.pointSize: Qt.application.font.pointSize * 0.7
                        color: "white"
                    }
                }

                // Vertical Scales Container
                Row {
                    width: parent.width
                    height: parent.height - y // fill remaining
                    spacing: (width - 3*(Qt.application.font.pixelSize / 2) * 2.5) / 2

                    // Trim Scale
                    Column {
                        width: (Qt.application.font.pixelSize / 2) * 2.5
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
                                    ctx.strokeStyle = "white";
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
                                    var mapped = Math.max(-maxTrim, Math.min(trimValue, maxTrim));
                                    // negative trim = down, positive = up
                                    var indY = height/2 - (mapped/maxTrim) * (height/2 - 5);

                                    ctx.beginPath();
                                    ctx.moveTo(cx-8, indY-5);
                                    ctx.lineTo(cx, indY);
                                    ctx.lineTo(cx-8, indY+5);
                                    ctx.fillStyle = "white";
                                    ctx.fill();
                                }
                            }
                        }
                        QGCLabel {
                            id: trimLbl
                            text: "TRM"
                            font.pointSize: Qt.application.font.pointSize * 0.7
                            color: "#ccc"
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                    }

                    // Fuel Scale
                    Column {
                        width: (Qt.application.font.pixelSize / 2) * 2.5
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
                                    ctx.strokeStyle = "white";
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
                                    var mapped = Math.max(0, Math.min(fuelValue, 100));
                                    var fillH = (mapped/100) * (height-10);

                                    ctx.beginPath();
                                    ctx.moveTo(cx, height-5);
                                    ctx.lineTo(cx, height-5 - fillH);
                                    ctx.strokeStyle = "white";
                                    ctx.lineWidth = 4;
                                    ctx.stroke();
                                }
                            }
                        }
                        QGCLabel {
                            id: fuelLbl
                            text: "FUL"
                            font.pointSize: Qt.application.font.pointSize * 0.7
                            color: "#ccc"
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                    }

                    // Battery Scale
                    Column {
                        width: (Qt.application.font.pixelSize / 2) * 2.5
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
                                    ctx.strokeStyle = "white";
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
                                    var mapped = Math.max(0, Math.min((battValue-minBatt)/(maxBatt-minBatt)*100, 100));
                                    var fillH = (mapped/100) * (height-10);

                                    ctx.beginPath();
                                    ctx.moveTo(cx, height-5);
                                    ctx.lineTo(cx, height-5 - fillH);
                                    ctx.strokeStyle = "white";
                                    ctx.lineWidth = 4;
                                    ctx.stroke();
                                }
                            }
                        }
                        QGCLabel {
                            id: battLbl
                            text: "BAT"
                            font.pointSize: Qt.application.font.pointSize * 0.7
                            color: "#ccc"
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                    }
                }
            }
        }

        // Helper component for buttons


        // Trim Control Panel
        Rectangle {
            width: (Qt.application.font.pixelSize / 2) * 6
            height: Qt.application.font.pixelSize * 8
            color: Qt.rgba(0.1, 0.1, 0.1, 0.7)
            radius: (Qt.application.font.pixelSize / 2)
            border.color: Qt.rgba(1, 1, 1, 0.2)

            Column {
                anchors.fill: parent
                anchors.margins: (Qt.application.font.pixelSize / 2) * 0.5
                spacing: Qt.application.font.pixelSize * 0.5

                QGCLabel {
                    text: "TRIM"
                    font.pointSize: Qt.application.font.pointSize * 0.6
                    color: "#aaa"
                }


                Rectangle {
                    width: parent.width
                    height: width
                    color: "transparent"
                    border.color: (trimStat & 1) !== 0 ? "#3498db" : Qt.rgba(1,1,1,0.3)
                    radius: 4

                    Rectangle {
                        anchors.fill: parent
                        color: (trimStat & 1) !== 0 ? Qt.rgba(41/255, 128/255, 185/255, 0.4) : "transparent"
                        radius: 4
                    }

                    QGCLabel {
                        anchors.centerIn: parent
                        text: "UP"
                        font.pointSize: Qt.application.font.pointSize * 0.6
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: sendCommand(mavCmdDoSetServo, 8, 2000, 0, 0, 0, 0, 0)
                    }
                }


                Rectangle {
                    width: parent.width
                    height: width
                    color: "transparent"
                    border.color: (trimStat & 2) !== 0 ? "#3498db" : Qt.rgba(1,1,1,0.3)
                    radius: 4

                    Rectangle {
                        anchors.fill: parent
                        color: (trimStat & 2) !== 0 ? Qt.rgba(41/255, 128/255, 185/255, 0.4) : "transparent"
                        radius: 4
                    }

                    QGCLabel {
                        anchors.centerIn: parent
                        text: "DN"
                        font.pointSize: Qt.application.font.pointSize * 0.6
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: sendCommand(mavCmdDoSetServo, 8, 1000, 0, 0, 0, 0, 0)
                    }
                }
            }
        }

        // Light Control Panel
        Rectangle {
            width: (Qt.application.font.pixelSize / 2) * 6
            height: Qt.application.font.pixelSize * 15
            color: Qt.rgba(0.1, 0.1, 0.1, 0.7)
            radius: (Qt.application.font.pixelSize / 2)
            border.color: Qt.rgba(1, 1, 1, 0.2)

            Column {
                anchors.fill: parent
                anchors.margins: (Qt.application.font.pixelSize / 2) * 0.5
                spacing: Qt.application.font.pixelSize * 0.5

                QGCLabel {
                    text: "LIGHT"
                    font.pointSize: Qt.application.font.pointSize * 0.6
                    color: "#aaa"
                }


                Rectangle {
                    width: parent.width
                    height: width
                    color: "transparent"
                    border.color: (lightsStat & 1) !== 0 ? "#3498db" : Qt.rgba(1,1,1,0.3)
                    radius: 4

                    Rectangle {
                        anchors.fill: parent
                        color: (lightsStat & 1) !== 0 ? Qt.rgba(41/255, 128/255, 185/255, 0.4) : "transparent"
                        radius: 4
                    }

                    QGCLabel {
                        anchors.centerIn: parent
                        text: "NAV"
                        font.pointSize: Qt.application.font.pointSize * 0.6
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: toggleLight(9, 1)
                    }
                }

                Rectangle {
                    width: parent.width
                    height: width
                    color: "transparent"
                    border.color: (lightsStat & 2) !== 0 ? "#3498db" : Qt.rgba(1,1,1,0.3)
                    radius: 4

                    Rectangle {
                        anchors.fill: parent
                        color: (lightsStat & 2) !== 0 ? Qt.rgba(41/255, 128/255, 185/255, 0.4) : "transparent"
                        radius: 4
                    }

                    QGCLabel {
                        anchors.centerIn: parent
                        text: "SIREN"
                        font.pointSize: Qt.application.font.pointSize * 0.6
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: toggleLight(10, 2)
                    }
                }

                Rectangle {
                    width: parent.width
                    height: width
                    color: "transparent"
                    border.color: (lightsStat & 4) !== 0 ? "#3498db" : Qt.rgba(1,1,1,0.3)
                    radius: 4

                    Rectangle {
                        anchors.fill: parent
                        color: (lightsStat & 4) !== 0 ? Qt.rgba(41/255, 128/255, 185/255, 0.4) : "transparent"
                        radius: 4
                    }

                    QGCLabel {
                        anchors.centerIn: parent
                        text: "HEAD"
                        font.pointSize: Qt.application.font.pointSize * 0.6
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: toggleLight(11, 4)
                    }
                }

                Rectangle {
                    width: parent.width
                    height: width
                    color: "transparent"
                    border.color: (lightsStat & 16) !== 0 ? "#3498db" : Qt.rgba(1,1,1,0.3)
                    radius: 4

                    Rectangle {
                        anchors.fill: parent
                        color: (lightsStat & 16) !== 0 ? Qt.rgba(41/255, 128/255, 185/255, 0.4) : "transparent"
                        radius: 4
                    }

                    QGCLabel {
                        anchors.centerIn: parent
                        text: "PORT\nSTBD"
                        font.pointSize: Qt.application.font.pointSize * 0.6
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: toggleLight(13, 16)
                    }
                }
            }
        }
    }
}
