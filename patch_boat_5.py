import re

with open("src/FlyView/CustomBoatFlyView.qml", "r") as f:
    content = f.read()

# Replace all occurrences of rightPanel with leftPanel inside CustomBoatFlyView.qml EXCEPT the telemetry panel
# We want the telemetry panel to be on the top right
# We want the trim and light panels to be on the left

# Let's split the panels up.
telemetry_panel = content[content.find("// Telemetry Panel"):content.find("// Helper component for buttons")]

trim_panel = content[content.find("// Trim Control Panel"):content.find("// Light Control Panel")]

light_panel = content[content.find("// Light Control Panel"):]

# The main structure
header = content[:content.find("// Container for all left-anchored UI")]

new_content = header + """

    // Container for all left-anchored UI
    Column {
        id: leftPanel
        visible: true
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: myFontPixelWidth * 1.5
        anchors.topMargin: myFontPixelHeight * 4 // avoid top toolbar
        spacing: myFontPixelHeight * 1.5
        width: myFontPixelWidth * 12

""" + trim_panel + light_panel[:-2] + """
    }

    // Container for all right-anchored UI
    Column {
        id: rightPanel
        visible: true
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: myFontPixelWidth * 1.5
        anchors.topMargin: myFontPixelHeight * 4 // avoid top toolbar
        spacing: myFontPixelHeight * 1.5
        width: myFontPixelWidth * 12

""" + telemetry_panel + """
    }
}
"""

with open("src/FlyView/CustomBoatFlyView.qml", "w") as f:
    f.write(new_content)
