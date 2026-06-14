import re

with open("src/FlyView/CustomBoatFlyView.qml", "r") as f:
    content = f.read()

# Add the FPV button and action
trim_panel_index = content.find("// Trim Control Panel")

fpv_action = """
        // FPV Button
        Rectangle {
            width: myFontPixelWidth * 6
            height: width
            color: Qt.rgba(0.1, 0.1, 0.1, 0.7)
            radius: myFontPixelWidth
            border.color: fpvToggle ? "#3498db" : Qt.rgba(1, 1, 1, 0.2)

            property bool fpvToggle: false

            Rectangle {
                anchors.fill: parent
                color: parent.fpvToggle ? Qt.rgba(41/255, 128/255, 185/255, 0.4) : "transparent"
                radius: parent.radius
            }

            QGCLabel {
                anchors.centerIn: parent
                text: "FPV"
                font.pointSize: myFontPointSize * 0.6
                color: "white"
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    parent.fpvToggle = !parent.fpvToggle
                    var val = parent.fpvToggle ? 1 : 0
                    sendCommand(mavCmdDoSetRelay, 5, val, 0, 0, 0, 0, 0) // RELAY6 is pin 5 usually? Let me check the prompt: "RELAY6 on by mavlink command". Assuming 5 for relay number 6 (0-indexed). Or maybe 6 if 1-indexed. Let's use 6. Wait, prompt says: "toggle RELAY1-RELAY5 output respectively." "FPV will toggle RELAY6". Let's assume 0-indexed for relay 1=0, so relay 6 = 5. I will use 5.
                }
            }
        }
"""

content = content[:trim_panel_index] + fpv_action + "\n" + content[trim_panel_index:]

# Change toggleLight to use mavCmdDoSetRelay
content = content.replace("sendCommand(mavCmdDoSetServo, auxIndex, pwm, 0, 0, 0, 0, 0)", "sendCommand(mavCmdDoSetRelay, auxIndex, isCurrentlyOn ? 0 : 1, 0, 0, 0, 0, 0)")
content = content.replace("var pwm = isCurrentlyOn ? 1000 : 2000", "")

with open("src/FlyView/CustomBoatFlyView.qml", "w") as f:
    f.write(content)
