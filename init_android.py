import pexpect
import sys

print("Starting tauri android init...")
child = pexpect.spawn("cargo tauri android init", cwd="tauri", encoding="utf-8")
child.logfile = sys.stdout

try:
    while True:
        index = child.expect(["(?i)Accept\? \(y/N\):", "\? Do you want to install.*\(y/n\) ›", pexpect.EOF], timeout=300)
        if index == 0:
            child.sendline("y")
        elif index == 1:
            # It expects y to be selected. Maybe we need to send "y"
            child.send("y")
        elif index == 2:
            print("Done!")
            break
except Exception as e:
    print(f"Error: {e}")
