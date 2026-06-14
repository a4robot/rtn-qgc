import re

with open("src/FlyView/CustomBoatFlyView.qml", "r") as f:
    content = f.read()

# Make it use a fixed position below the first toolstrip
content = content.replace("anchors.topMargin: myFontPixelHeight * 4 // avoid top toolbar", "anchors.topMargin: parentToolInsets ? parentToolInsets.topEdgeLeftInset + myFontPixelHeight * 1 : myFontPixelHeight * 4", 1)
content = content.replace("anchors.topMargin: myFontPixelHeight * 4 // avoid top toolbar", "anchors.topMargin: parentToolInsets ? parentToolInsets.topEdgeRightInset + myFontPixelHeight * 1 : myFontPixelHeight * 4", 1)
content = content.replace("id: _root", "id: _root\n    property var parentToolInsets", 1)

with open("src/FlyView/CustomBoatFlyView.qml", "w") as f:
    f.write(content)
