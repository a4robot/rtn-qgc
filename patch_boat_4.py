import re

with open("src/FlyView/CustomBoatFlyView.qml", "r") as f:
    content = f.read()

# Make it align left instead for the left panel container
content = content.replace("id: rightPanel", "id: rightPanelContainer", 1) # Just temporary
content = content.replace("anchors.right: parent.right\n        anchors.top: parent.top\n        anchors.rightMargin: myFontPixelWidth * 1.5", "anchors.right: parent.right\n        anchors.top: parent.top\n        anchors.rightMargin: myFontPixelWidth * 1.5", 1)

with open("src/FlyView/CustomBoatFlyView.qml", "w") as f:
    f.write(content)
