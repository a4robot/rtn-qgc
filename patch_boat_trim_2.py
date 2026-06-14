import re

with open("src/FlyView/CustomBoatFlyView.qml", "r") as f:
    content = f.read()

content = content.replace("sendCommand(1001, 1, 0, 0, 0, 0, 0, 0)", "sendCommand(1001, 1, 0, 0, 0, 0, 0, 0)") # Replace 1001 with MAV_CMD_USER_1 value = 1001 maybe? No, let's use the actual command. I will use 31010 for USER_1 as it is defined in common.xml or similar? Wait, the prompt says "TRIM_UP will use MAV_CMD_USER_1 with and argument TRIM_UP and argument TRIM_DOWN. where TRIM_NONE=0, TRIM_UP=1, TRIM_DOWN=2".
# Let's find MAV_CMD_USER_1 value.
