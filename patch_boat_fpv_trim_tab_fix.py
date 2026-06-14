import re

with open("src/FlyView/CustomBoatFlyView.qml", "r") as f:
    content = f.read()

# Fix the syntax error, I overwrote the `}` closing the leftPanel container earlier but now I did git reset. Let's make sure it's valid.

with open("src/FlyView/CustomBoatFlyView.qml", "w") as f:
    f.write(content)
