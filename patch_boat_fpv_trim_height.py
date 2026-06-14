import re

with open("src/FlyView/CustomBoatFlyView.qml", "r") as f:
    content = f.read()

# Make the tab height depend on its children instead of a fixed size so we don't have overlapping or extra spacing issues
content = content.replace("height: myFontPixelHeight * 15", "height: childrenRect.height + myFontPixelWidth")
content = content.replace("height: myFontPixelHeight * 22", "height: childrenRect.height + myFontPixelWidth")

with open("src/FlyView/CustomBoatFlyView.qml", "w") as f:
    f.write(content)
