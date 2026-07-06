#!/usr/bin/env sh
# Dummy stand-in for the headless QGC "ghost" core.
#
# Honors the shell's spawn contract superficially: accepts (and ignores)
#   --headless --bridge-port 8877
# prints the readiness line the shell log expects, then sleeps forever so
# the watchdog sees a healthy long-running process.
#
# To wire it up for `cargo tauri dev`, copy it next to the shell sources
# under the target-triple name Tauri resolves sidecars by:
#
#   TRIPLE=$(rustc -vV | sed -n 's/^host: //p')
#   cp tauri/dummy-ghost/dummy_ghost.sh "tauri/src-tauri/binaries/ghost-$TRIPLE"
#   chmod +x "tauri/src-tauri/binaries/ghost-$TRIPLE"
#
# See tauri/src-tauri/binaries/README.md for details.

echo "ghost: listening on 8877"

# Sleep forever (in chunks — plain `sleep infinity` is not POSIX).
while true; do
    sleep 3600
done
