import re

with open("src/FlyView/CustomBoatFlyView.qml", "r") as f:
    content = f.read()

# Fix the missing `}` for `leftPanel` container which was apparently omitted when I rebuilt the tabs.

content = content.replace("""        }
    // Container for all right-anchored UI""", """        }
    }
    // Container for all right-anchored UI""")

with open("src/FlyView/CustomBoatFlyView.qml", "w") as f:
    f.write(content)
