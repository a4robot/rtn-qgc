import re

with open("src/FlyView/CustomBoatFlyView.qml", "r") as f:
    content = f.read()

# Change left panel width to match the prompt specifications ("matching width with original top corner button", which is usually 7 font widths).
# The current is myFontPixelWidth * 12. Let's make it match the button width inside which is `myFontPixelWidth * 7`.

# only replace the first occurrence which is for the leftPanel
content = content.replace("width: myFontPixelWidth * 12", "width: myFontPixelWidth * 7", 1)

with open("src/FlyView/CustomBoatFlyView.qml", "w") as f:
    f.write(content)
