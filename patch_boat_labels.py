import re

with open("src/FlyView/CustomBoatFlyView.qml", "r") as f:
    content = f.read()

# Update Trim Control Panel text to match FPV tab style
content = content.replace('text: "TRIM"', 'text: "TRIM"', 1) # Keep TRIM but maybe remove the label or hide it? The prompt image doesn't seem to have "TRIM" title text in the new tabs, it just has the buttons directly. Wait, I should probably remove the "TRIM" and "LIGHT" titles so it looks exactly like the Arm tab.

content = content.replace("""                QGCLabel {
                    text: "TRIM"
                    font.pointSize: myFontPointSize * 0.6
                    color: "#aaa"
                }""", "")

content = content.replace("""                QGCLabel {
                    text: "LIGHT"
                    font.pointSize: myFontPointSize * 0.6
                    color: "#aaa"
                }""", "")

# Wait, the prompt says "Replace Take-off button with Arm button... Add another tab with the same style and theme with the Arm&Return tab. This tab contains 3 buttons which is FPV , Trim up and Trim down. Add another tab below the second tab with the same style and theme. This tab contains 5 light buttons... On/Off, Siren, All Around, Nav Lt., Spot Port, Spot Stbd".
# The buttons are:
# Tab 1: Arm/Disarm, Return
# Tab 2: FPV, Trim Up, Trim Dn
# Tab 3: On/Off, Siren, All Around, Nav Lt., Spot Port, Spot Stbd
# Wait, "Spot Pt." "Spot Sb." in the image?

# Let's adjust Tab 2: FPV, Trim Up, Trim Dn. Let's make sure the width matches the Arm tab.
content = content.replace("width: myFontPixelWidth * 6", "width: myFontPixelWidth * 7")

with open("src/FlyView/CustomBoatFlyView.qml", "w") as f:
    f.write(content)
