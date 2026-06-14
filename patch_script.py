import re

with open('src/FlyView/CustomBoatFlyView.qml', 'r') as f:
    content = f.read()

# Replace left anchors with right anchors for the main panel
content = content.replace("id: leftPanel", "id: rightPanel")
content = content.replace("anchors.left: parent.left", "anchors.right: parent.right")
content = content.replace("anchors.leftMargin: myFontPixelWidth * 1.5", "anchors.rightMargin: myFontPixelWidth * 1.5")
content = content.replace("anchors.topMargin: myFontPixelHeight * 4 // avoid top toolbar", "anchors.topMargin: myFontPixelHeight * 1 // Match bottom right map/video top edge, maybe standard margin")

# We want to remove the Trim and Light Control Rectangles
# Find the start of Trim Control Panel
trim_start = content.find("// Trim Control Panel")
if trim_start != -1:
    content = content[:trim_start]

# Add the closing bracket for Column
content += "    }\n}\n"

# Stylistic changes
# color: Qt.rgba(0.1, 0.1, 0.1, 0.7) -> qgcPal.windowShadeDark or qgcPal.window
content = content.replace("color: Qt.rgba(0.1, 0.1, 0.1, 0.7)", "color: qgcPal.windowShade")
content = content.replace("border.color: Qt.rgba(1, 1, 1, 0.2)", "border.color: qgcPal.text")

# Needles/Lines: "white" -> qgcPal.text, "gray" -> qgcPal.colorGrey
content = content.replace('"white"', 'qgcPal.text')
content = content.replace('"gray"', 'qgcPal.colorGrey')

# Wait, `qgcPal` is not defined in the scope if we don't declare it.
# We must add `property var qgcPal: QGroundControl.globalPalette` or just use `QGroundControl.globalPalette.text`
# Let's inject QGCPalette
qgcpal_decl = """    property real myFontPointSize: 10
    property var qgcPal: QGroundControl.globalPalette
"""
content = content.replace("property real myFontPointSize: 10", qgcpal_decl)

content = content.replace('''                    QGCLabel {
                        anchors.bottom: parent.bottom
                        anchors.right: parent.right
                        text: "L"''', '''                    QGCLabel {
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        text: "L"''')

content = content.replace('color: "#ccc"', 'color: qgcPal.text')

with open('src/FlyView/CustomBoatFlyView.qml', 'w') as f:
    f.write(content)
