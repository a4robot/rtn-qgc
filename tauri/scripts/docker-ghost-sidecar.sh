#!/usr/bin/env sh
# Wave 9 stopgap: run the real headless QGC ghost core via the
# qgc-build-ubuntu Docker image, because the Release binary built inside
# that image links against Qt 6.10 and isn't runnable on the host
# directly (`ldd` shows unresolved libQt6*.so + a Qt_6.10 version
# mismatch against the host's Qt6Core).
#
# This script is installed as the Tauri sidecar itself
# (tauri/src-tauri/binaries/ghost-<triple>), so it must honor the same
# spawn contract documented in tauri/src-tauri/src/lib.rs:
#   ghost --headless --bridge-port 8877
# It ignores the args it's called with (the shell always calls it with
# exactly `--headless --bridge-port 8877`) and always launches the real
# binary with --mock-link so MockLink vehicle 128 is available for the
# web UI to connect to.
#
# Lifecycle: the container is started detached under a per-invocation
# name, then followed with `docker logs -f` so stdout/stderr still reach
# the shell's log pump. The container must never outlive this script —
# and the shell kills sidecars with SIGKILL (tauri_plugin_shell's
# CommandChild::kill), which cannot be trapped. So in addition to the
# TERM/INT/EXIT trap, a detached reaper subshell watches this script's
# PID and stops the container the moment the script dies for ANY reason
# (SIGKILL included: the reaper is a separate process and survives it).

set -eu

IMAGE="qgc-build-ubuntu"
CONTAINER_NAME="ghost-sidecar-$$"
# Release build output produced by qgc-build-ubuntu earlier this session;
# bind-mounted read-only to /build inside the container, matching how the
# image's own entrypoint.sh lays out `cmake --install` output. Override
# via GHOST_BUILD_DIR if the build tree moves.
BUILD_DIR="${GHOST_BUILD_DIR:-/tmp/claude-1001/-home-bpasu-git-rtn-qgc/d73f5ca7-17e3-4db7-92b0-4f406367507b/scratchpad/qgc-build}"

docker run -d --rm \
    --name "$CONTAINER_NAME" \
    --network host \
    -u "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -e QT_QPA_PLATFORM=offscreen \
    -v "$BUILD_DIR:/build:ro" \
    --entrypoint /build/Release/QGroundControl \
    "$IMAGE" \
    --headless --bridge-port 8877 --mock-link >/dev/null

stop_container() {
    docker stop -t 2 "$CONTAINER_NAME" >/dev/null 2>&1 || true
}
trap stop_container EXIT INT TERM

# Orphan reaper: survives even SIGKILL of this script. `kill -0` on the
# parent PID starts failing once the script is gone; then stop the
# container and exit. Fully detached (setsid + closed stdio) so the
# shell's output pump doesn't wait on it.
setsid sh -c "
    while kill -0 $$ 2>/dev/null; do sleep 2; done
    docker stop -t 2 '$CONTAINER_NAME' >/dev/null 2>&1 || true
" >/dev/null 2>&1 </dev/null &

# Follow logs in the foreground — ends when the container stops. Exit
# with the container's real exit code so the watchdog's crash/backoff
# accounting stays truthful.
docker logs -f "$CONTAINER_NAME" 2>&1 || true
exit "$(docker wait "$CONTAINER_NAME" 2>/dev/null || echo 1)"
