import re

with open("src/FlyView/CustomBoatFlyView.qml", "r") as f:
    content = f.read()

content = content.replace("sendCommand(mavCmdDoSetServo, 8, 2000, 0, 0, 0, 0, 0)", "sendCommand(1001, 1, 0, 0, 0, 0, 0, 0)")
content = content.replace("sendCommand(mavCmdDoSetServo, 8, 1000, 0, 0, 0, 0, 0)", "sendCommand(1001, 2, 0, 0, 0, 0, 0, 0)")

with open("src/FlyView/CustomBoatFlyView.qml", "w") as f:
    f.write(content)
