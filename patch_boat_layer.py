import re

with open("src/FlyView/FlyViewCustomLayer.qml", "r") as f:
    content = f.read()

content = content.replace("CustomBoatFlyView { }", "CustomBoatFlyView { parentToolInsets: _root.parentToolInsets }")

with open("src/FlyView/FlyViewCustomLayer.qml", "w") as f:
    f.write(content)
