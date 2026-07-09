#!/usr/bin/env sh
# Stage the host-portable ghost bundle (see /home/bpasu/ghost-bundle and
# the "Wave 12: host-portable ghost bundle" section of
# ../src-tauri/binaries/README.md) into the layout the Tauri build
# expects:
#
#   src-tauri/binaries/ghost-<target-triple>       <- bundle/bin/QGroundControl
#                                                      (bundle.externalBin sidecar)
#   src-tauri/binaries/ghost-resources/lib/         <- bundle/lib/*
#   src-tauri/binaries/ghost-resources/plugins/     <- bundle/plugins/*
#                                                      (bundle.resources; ../src/lib.rs
#                                                       points LD_LIBRARY_PATH /
#                                                       QT_PLUGIN_PATH at these via
#                                                       app.path().resource_dir() at
#                                                       runtime)
#
# This replaces the Wave 9-11 docker-wrapper stopgap
# (docker-ghost-sidecar.sh) that used to be installed as the sidecar
# itself — that script shelled out to `docker run qgc-build-ubuntu` on
# every spawn because, until Wave 12, the Release ghost binary only ran
# inside that image. It no longer needs to be installed as the sidecar,
# but is left in this directory as a dev fallback (see its own header).
#
# Usage: tauri/scripts/stage-ghost-bundle.sh [path-to-ghost-bundle] [target-triple]
# Defaults: /home/bpasu/ghost-bundle, host triple (`rustc -vV`).
#
# `cp -r` (not `cp -rL`) is used deliberately: it preserves the bundle's
# `.so.N -> .so.N.N.N` symlinks as symlinks in the staged copy, rather
# than materializing duplicate file content. (Tauri's own resource
# bundler flattens symlinks to real files when it copies staged
# resources into target/ or an installer — that duplication happens
# downstream, not here.)

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DEST_DIR="$SCRIPT_DIR/../src-tauri/binaries"

BUNDLE="${1:-/home/bpasu/ghost-bundle}"
TRIPLE="${2:-$(rustc -vV | sed -n 's/^host: //p')}"

if [ -z "$TRIPLE" ]; then
    echo "error: could not determine target triple; pass it explicitly" >&2
    exit 1
fi

if [ ! -x "$BUNDLE/bin/QGroundControl" ]; then
    echo "error: $BUNDLE/bin/QGroundControl not found or not executable" >&2
    exit 1
fi
if [ ! -d "$BUNDLE/lib" ] || [ ! -d "$BUNDLE/plugins" ]; then
    echo "error: $BUNDLE/lib and/or $BUNDLE/plugins missing" >&2
    exit 1
fi

mkdir -p "$DEST_DIR"

cp "$BUNDLE/bin/QGroundControl" "$DEST_DIR/ghost-$TRIPLE"
chmod +x "$DEST_DIR/ghost-$TRIPLE"

rm -rf "$DEST_DIR/ghost-resources"
mkdir -p "$DEST_DIR/ghost-resources"
cp -r "$BUNDLE/lib" "$DEST_DIR/ghost-resources/lib"
cp -r "$BUNDLE/plugins" "$DEST_DIR/ghost-resources/plugins"

# plugins/multimedia/libffmpegmediaplugin.so is bundled by the ghost-bundle
# build but is INERT here (see MANIFEST.txt): it needs libQt6Quick/Qml/
# OpenGL and Qt's private ffmpeg libs (libavformat.so.61 etc.) that are
# deliberately not part of this QML-free bundle, so Qt's plugin loader
# just fails its dlopen silently at runtime and this ghost binary's real
# video path (GstVideoReceiver, raw GStreamer) never touches it. It's
# dropped here rather than shipped dead weight because AppImage bundling
# (linuxdeploy) DOES statically resolve every .so's dependencies and
# fails the whole build on this one's unresolvable libavformat.so.61.
rm -rf "$DEST_DIR/ghost-resources/plugins/multimedia"

echo "staged $BUNDLE ->"
echo "  $DEST_DIR/ghost-$TRIPLE"
echo "  $DEST_DIR/ghost-resources/lib/"
echo "  $DEST_DIR/ghost-resources/plugins/"
