import re

with open("src/FlyView/CustomBoatFlyView.qml", "r") as f:
    content = f.read()

# Replace all of FPV and Trim Control Panel with a single combined tab

start_fpv = content.find("        // FPV Button")
end_trim = content.find("        // Light Control Panel")

combined_tab = """        // FPV and Trim Control Panel
        Rectangle {
            width: myFontPixelWidth * 7
            height: myFontPixelHeight * 15
            color: Qt.rgba(0.1, 0.1, 0.1, 0.7)
            radius: myFontPixelWidth
            border.color: Qt.rgba(1, 1, 1, 0.2)

            Column {
                anchors.fill: parent
                anchors.margins: myFontPixelWidth * 0.5
                spacing: myFontPixelHeight * 0.5

                // FPV Button
                Rectangle {
                    width: parent.width
                    height: width
                    color: "transparent"
                    border.color: fpvToggle ? "#3498db" : Qt.rgba(1, 1, 1, 0.2)
                    radius: 4

                    property bool fpvToggle: false

                    Rectangle {
                        anchors.fill: parent
                        color: parent.fpvToggle ? Qt.rgba(41/255, 128/255, 185/255, 0.4) : "transparent"
                        radius: parent.radius
                    }

                    Column {
                        anchors.centerIn: parent
                        spacing: 2
                        QGCColoredImage {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: myFontPixelWidth * 2
                            height: width
                            source: parent.parent.fpvToggle ? "/res/camera-left.svg" : "/res/camera-right.svg"
                            color: "white"
                        }
                        QGCLabel {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "FPV"
                            font.pointSize: myFontPointSize * 0.6
                            color: "white"
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            parent.fpvToggle = !parent.fpvToggle
                            var val = parent.fpvToggle ? 1 : 0
                            sendCommand(mavCmdDoSetRelay, 5, val, 0, 0, 0, 0, 0)
                        }
                    }
                }

                // Trim Up
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

                    Column {
                        anchors.centerIn: parent
                        spacing: 2
                        QGCColoredImage {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: myFontPixelWidth * 2
                            height: width
                            source: (trimStat & 1) !== 0 ? "/res/arrow-up-solid.svg" : "/res/arrow-up-outline.svg"
                            color: "white"
                        }
                        QGCLabel {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "Trim Up"
                            font.pointSize: myFontPointSize * 0.6
                            color: "white"
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: sendCommand(mavCmdUser1, 1, 0, 0, 0, 0, 0, 0)
                    }
                }

                // Trim Down
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

                    Column {
                        anchors.centerIn: parent
                        spacing: 2
                        QGCColoredImage {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: myFontPixelWidth * 2
                            height: width
                            source: (trimStat & 2) !== 0 ? "/res/arrow-down-solid.svg" : "/res/arrow-down-outline.svg"
                            color: "white"
                        }
                        QGCLabel {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "Trim Dn"
                            font.pointSize: myFontPointSize * 0.6
                            color: "white"
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: sendCommand(mavCmdUser1, 2, 0, 0, 0, 0, 0, 0)
                    }
                }
            }
        }
"""

content = content[:start_fpv] + combined_tab + content[end_trim:]

with open("src/FlyView/CustomBoatFlyView.qml", "w") as f:
    f.write(content)
