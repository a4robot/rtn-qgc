import re

with open("src/FlyView/CustomBoatFlyView.qml", "r") as f:
    content = f.read()

# I see `MAV_CMD_USER_1` is 31010 in mavlink protocol, or the value from `MAV_CMD_USER_1 = 31010`. Let's add it as property.
content = content.replace("readonly property int mavCmdDoSetServo: 183", "readonly property int mavCmdDoSetServo: 183\n    readonly property int mavCmdUser1: 31010")

# Update Trim controls to use mavCmdUser1
trim_panel_index = content.find("// Trim Control Panel")
content = content.replace("sendCommand(1001, 1, 0, 0, 0, 0, 0, 0)", "sendCommand(mavCmdUser1, 1, 0, 0, 0, 0, 0, 0)")
content = content.replace("sendCommand(1001, 2, 0, 0, 0, 0, 0, 0)", "sendCommand(mavCmdUser1, 2, 0, 0, 0, 0, 0, 0)")

with open("src/FlyView/CustomBoatFlyView.qml", "w") as f:
    f.write(content)
