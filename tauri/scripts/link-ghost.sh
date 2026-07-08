#!/usr/bin/env sh
# Copy a built ghost (headless QGC core) binary into
# tauri/src-tauri/binaries/ under the target-triple name Tauri's
# externalBin resolution expects.
#
# tauri.conf.json declares `bundle.externalBin: ["binaries/ghost"]`, and
# tauri-build validates that `binaries/ghost-<target-triple>[.exe]` exists
# at *compile* time (not just when bundling) — so this file must be in
# place before `cargo check` / `cargo build` / `cargo tauri dev` will
# succeed. See src-tauri/binaries/README.md for the full contract.
#
# Usage:
#   tauri/scripts/link-ghost.sh <path-to-ghost-binary> [target-triple]
#
# If <target-triple> is omitted, the host triple (`rustc -vV`) is used.
# This script only copies a binary you already built (or the dev dummy
# at tauri/dummy-ghost/dummy_ghost.sh) — it never builds or commits one.
#
# Examples:
#   # Wire the dev dummy for the host triple:
#   tauri/scripts/link-ghost.sh tauri/dummy-ghost/dummy_ghost.sh
#
#   # Wire a real headless QGC build for a specific triple:
#   tauri/scripts/link-ghost.sh build/ghost x86_64-pc-windows-msvc

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DEST_DIR="$SCRIPT_DIR/../src-tauri/binaries"

SRC="${1:?usage: link-ghost.sh <path-to-ghost-binary> [target-triple]}"
TRIPLE="${2:-$(rustc -vV | sed -n 's/^host: //p')}"

if [ -z "$TRIPLE" ]; then
    echo "error: could not determine target triple; pass it explicitly" >&2
    exit 1
fi

EXT=""
case "$TRIPLE" in
    *windows*) EXT=".exe" ;;
esac

DEST="$DEST_DIR/ghost-$TRIPLE$EXT"

mkdir -p "$DEST_DIR"
cp "$SRC" "$DEST"
chmod +x "$DEST"

echo "linked $SRC -> $DEST"
