import re

with open("src/FlyView/CustomBoatFlyView.qml", "r") as f:
    content = f.read()

# Make it align left instead
content = content.replace("id: rightPanel", "id: leftPanel")
content = content.replace("anchors.right: parent.right", "anchors.left: parent.left", 1)
content = content.replace("anchors.rightMargin: myFontPixelWidth * 1.5", "anchors.leftMargin: myFontPixelWidth * 1.5", 1)

with open("src/FlyView/CustomBoatFlyView.qml", "w") as f:
    f.write(content)
