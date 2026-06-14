import re

with open("src/FlyView/CustomBoatFlyView.qml", "r") as f:
    content = f.read()

# Change leftPanel to rightPanel
content = content.replace("id: leftPanel", "id: rightPanel")
content = content.replace("anchors.left: parent.left", "anchors.right: parent.right", 1) # Only first one for the panel
content = content.replace("anchors.leftMargin: myFontPixelWidth * 1.5", "anchors.rightMargin: myFontPixelWidth * 1.5", 1)


with open("src/FlyView/CustomBoatFlyView.qml", "w") as f:
    f.write(content)
