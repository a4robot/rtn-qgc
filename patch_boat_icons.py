import re

with open("src/FlyView/CustomBoatFlyView.qml", "r") as f:
    content = f.read()

# I need to use the SVG icons as specified in the prompt:
# FPV: camera to the right
# Trim up/down: thick up/down arrow. "When the button is not pressed, the arrow icon has only edge, when pressed, the arrow icon is filled and becomes solid color."
# Lights: "Each button is represented by a light-bulb icon with its name below". "The icon can be toggled between ON/OFF when off the light-bulb icon has only edge, when on it becomes solid."

# But wait, QGC has existing SVGs. I should use QGCColoredImage. Let's see what SVGs are available for camera, arrow up/down, and lightbulbs.
# Since I may not have these exact icons, I'll need to create SVG files or use text. The prompt provided an image which shows the layout.
# Can I use text or must I use SVG? The prompt says "For the FPV use a camera (to the right) icon... For the trim up and trim down use thick up and down arrow. When the button is not pressed... use a light-bulb icon..."
