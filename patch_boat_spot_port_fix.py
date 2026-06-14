import re

with open("src/FlyView/CustomBoatFlyView.qml", "r") as f:
    content = f.read()

# Fix the Light Control Panel to match the prompt's 5 light buttons (I missed them earlier after replacing).
# Head, Nav Lt., Siren, Spot Pt., Spot Sb. and On/Off title.

lights_panel = """        // Light Control Panel
        Rectangle {
            width: myFontPixelWidth * 7
            height: myFontPixelHeight * 22
            color: Qt.rgba(0.1, 0.1, 0.1, 0.7)
            radius: myFontPixelWidth
            border.color: Qt.rgba(1, 1, 1, 0.2)

            Column {
                anchors.fill: parent
                anchors.margins: myFontPixelWidth * 0.5
                spacing: myFontPixelHeight * 0.5

                QGCLabel {
                    text: "On/Off"
                    font.pointSize: myFontPointSize * 0.6
                    color: "white"
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                // Head
                Rectangle {
                    width: parent.width
                    height: width
                    color: "transparent"
                    border.color: (lightsStat & 1) !== 0 ? "#3498db" : Qt.rgba(1,1,1,0.3)
                    radius: 4
                    Rectangle { anchors.fill: parent; color: (lightsStat & 1) !== 0 ? Qt.rgba(41/255, 128/255, 185/255, 0.4) : "transparent"; radius: 4 }
                    Column { anchors.centerIn: parent; spacing: 2
                        QGCColoredImage { anchors.horizontalCenter: parent.horizontalCenter; width: myFontPixelWidth * 2; height: width; source: (lightsStat & 1) !== 0 ? "/res/lightbulb-solid.svg" : "/res/lightbulb-outline.svg"; color: "white" }
                        QGCLabel { anchors.horizontalCenter: parent.horizontalCenter; text: "Head"; font.pointSize: myFontPointSize * 0.6; color: "white" }
                    }
                    MouseArea { anchors.fill: parent; onClicked: toggleLight(0, 1) }
                }

                // Nav.
                Rectangle {
                    width: parent.width
                    height: width
                    color: "transparent"
                    border.color: (lightsStat & 2) !== 0 ? "#3498db" : Qt.rgba(1,1,1,0.3)
                    radius: 4
                    Rectangle { anchors.fill: parent; color: (lightsStat & 2) !== 0 ? Qt.rgba(41/255, 128/255, 185/255, 0.4) : "transparent"; radius: 4 }
                    Column { anchors.centerIn: parent; spacing: 2
                        QGCColoredImage { anchors.horizontalCenter: parent.horizontalCenter; width: myFontPixelWidth * 2; height: width; source: (lightsStat & 2) !== 0 ? "/res/lightbulb-solid.svg" : "/res/lightbulb-outline.svg"; color: "white" }
                        QGCLabel { anchors.horizontalCenter: parent.horizontalCenter; text: "Nav."; font.pointSize: myFontPointSize * 0.6; color: "white" }
                    }
                    MouseArea { anchors.fill: parent; onClicked: toggleLight(1, 2) }
                }

                // Siren
                Rectangle {
                    width: parent.width
                    height: width
                    color: "transparent"
                    border.color: (lightsStat & 4) !== 0 ? "#3498db" : Qt.rgba(1,1,1,0.3)
                    radius: 4
                    Rectangle { anchors.fill: parent; color: (lightsStat & 4) !== 0 ? Qt.rgba(41/255, 128/255, 185/255, 0.4) : "transparent"; radius: 4 }
                    Column { anchors.centerIn: parent; spacing: 2
                        QGCColoredImage { anchors.horizontalCenter: parent.horizontalCenter; width: myFontPixelWidth * 2; height: width; source: (lightsStat & 4) !== 0 ? "/res/lightbulb-solid.svg" : "/res/lightbulb-outline.svg"; color: "white" }
                        QGCLabel { anchors.horizontalCenter: parent.horizontalCenter; text: "Siren"; font.pointSize: myFontPointSize * 0.6; color: "white" }
                    }
                    MouseArea { anchors.fill: parent; onClicked: toggleLight(2, 4) }
                }

                // Port
                Rectangle {
                    width: parent.width
                    height: width
                    color: "transparent"
                    border.color: (lightsStat & 8) !== 0 ? "#3498db" : Qt.rgba(1,1,1,0.3)
                    radius: 4
                    Rectangle { anchors.fill: parent; color: (lightsStat & 8) !== 0 ? Qt.rgba(41/255, 128/255, 185/255, 0.4) : "transparent"; radius: 4 }
                    Column { anchors.centerIn: parent; spacing: 2
                        QGCColoredImage { anchors.horizontalCenter: parent.horizontalCenter; width: myFontPixelWidth * 2; height: width; source: (lightsStat & 8) !== 0 ? "/res/lightbulb-solid.svg" : "/res/lightbulb-outline.svg"; color: "white" }
                        QGCLabel { anchors.horizontalCenter: parent.horizontalCenter; text: "Port"; font.pointSize: myFontPointSize * 0.6; color: "white" }
                    }
                    MouseArea { anchors.fill: parent; onClicked: toggleLight(3, 8) }
                }

                // Stbd.
                Rectangle {
                    width: parent.width
                    height: width
                    color: "transparent"
                    border.color: (lightsStat & 16) !== 0 ? "#3498db" : Qt.rgba(1,1,1,0.3)
                    radius: 4
                    Rectangle { anchors.fill: parent; color: (lightsStat & 16) !== 0 ? Qt.rgba(41/255, 128/255, 185/255, 0.4) : "transparent"; radius: 4 }
                    Column { anchors.centerIn: parent; spacing: 2
                        QGCColoredImage { anchors.horizontalCenter: parent.horizontalCenter; width: myFontPixelWidth * 2; height: width; source: (lightsStat & 16) !== 0 ? "/res/lightbulb-solid.svg" : "/res/lightbulb-outline.svg"; color: "white" }
                        QGCLabel { anchors.horizontalCenter: parent.horizontalCenter; text: "Stbd."; font.pointSize: myFontPointSize * 0.6; color: "white" }
                    }
                    MouseArea { anchors.fill: parent; onClicked: toggleLight(4, 16) }
                }
            }
        }
"""

start_idx = content.find("        // Light Control Panel")
end_idx = content.find("    // Container for all right-anchored UI", start_idx)
content = content[:start_idx] + lights_panel + content[end_idx:]

with open("src/FlyView/CustomBoatFlyView.qml", "w") as f:
    f.write(content)
