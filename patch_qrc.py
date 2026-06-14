import re

with open("qgcresources.qrc", "r") as f:
    content = f.read()

# Add the new SVGs to qgcresources.qrc under /res
svgs_to_add = [
    "camera-right.svg",
    "camera-left.svg",
    "arrow-up-outline.svg",
    "arrow-up-solid.svg",
    "arrow-down-outline.svg",
    "arrow-down-solid.svg",
    "lightbulb-outline.svg",
    "lightbulb-solid.svg"
]

to_insert = ""
for svg in svgs_to_add:
    to_insert += f"        <file alias=\"{svg}\">resources/{svg}</file>\n"

# Find a good place to insert, e.g., after `<qresource prefix="/res">`
prefix_index = content.find('<qresource prefix="/res">')
if prefix_index != -1:
    insert_pos = content.find('>', prefix_index) + 1
    content = content[:insert_pos] + "\n" + to_insert + content[insert_pos:]

with open("qgcresources.qrc", "w") as f:
    f.write(content)
