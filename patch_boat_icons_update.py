import re

with open("src/FlyView/CustomBoatFlyView.qml", "r") as f:
    content = f.read()

# Update buttons to use QGCColoredImage and Text instead of just Text
# Wait, for QGCColoredImage, I can just use it.
content = content.replace('color: parent.fpvToggle ? Qt.rgba(41/255, 128/255, 185/255, 0.4) : "transparent"', 'color: parent.fpvToggle ? Qt.rgba(41/255, 128/255, 185/255, 0.4) : "transparent"')
content = content.replace("""
            QGCLabel {
                anchors.centerIn: parent
                text: "FPV"
                font.pointSize: myFontPointSize * 0.6
                color: "white"
            }""", """
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
            }""")

content = content.replace("""
                    QGCLabel {
                        anchors.centerIn: parent
                        text: "UP"
                        font.pointSize: myFontPointSize * 0.6
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                    }""", """
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
                    }""")

content = content.replace("""
                    QGCLabel {
                        anchors.centerIn: parent
                        text: "DN"
                        font.pointSize: myFontPointSize * 0.6
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                    }""", """
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
                    }""")

# Helper to replace light labels
def replace_light(content, text, bitmask):
    return content.replace(f"""
                    QGCLabel {{
                        anchors.centerIn: parent
                        text: "{text}"
                        font.pointSize: myFontPointSize * 0.6
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                    }}""", f"""
                    Column {{
                        anchors.centerIn: parent
                        spacing: 2
                        QGCColoredImage {{
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: myFontPixelWidth * 2
                            height: width
                            source: (lightsStat & {bitmask}) !== 0 ? "/res/lightbulb-solid.svg" : "/res/lightbulb-outline.svg"
                            color: "white"
                        }}
                        QGCLabel {{
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "{text.replace('PORT\\nSTBD', 'Spot Port\\nSpot Stbd').replace('NAV', 'Nav Lt.').replace('HEAD', 'All Around')}"
                            font.pointSize: myFontPointSize * 0.6
                            color: "white"
                            horizontalAlignment: Text.AlignHCenter
                        }}
                    }}""")

content = replace_light(content, "NAV", 1)
content = replace_light(content, "SIREN", 2)
content = replace_light(content, "HEAD", 4)
content = replace_light(content, "PORT\\nSTBD", 16)


with open("src/FlyView/CustomBoatFlyView.qml", "w") as f:
    f.write(content)
