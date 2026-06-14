import re

with open("src/FlyView/CustomBoatFlyView.qml", "r") as f:
    content = f.read()

# Update the relay numbers
content = content.replace("toggleLight(9, 1)", "toggleLight(0, 1)")
content = content.replace("toggleLight(10, 2)", "toggleLight(1, 2)")
content = content.replace("toggleLight(11, 4)", "toggleLight(2, 4)")
content = content.replace("toggleLight(13, 16)", "toggleLight(3, 16)")

with open("src/FlyView/CustomBoatFlyView.qml", "w") as f:
    f.write(content)
