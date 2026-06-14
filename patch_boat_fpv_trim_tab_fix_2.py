import re

with open("src/FlyView/CustomBoatFlyView.qml", "r") as f:
    content = f.read()

# Make sure "FPV and Trim Control Panel" tab has width instead of width: width which was parsed wrong or something. Wait, "Rectangle { width: parent.width; height: width"
# Yes, height: width is fine, it means it's a square.
