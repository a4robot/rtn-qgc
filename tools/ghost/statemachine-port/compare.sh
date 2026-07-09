#!/bin/bash
# Replay-compare for Q8b (STRANGLER_MILESTONES.md M8): diffs a capture.sh
# output against baseline-mocklink-connect.log, with one additional
# normalization on top of capture.sh's own: strips the trailing
# " - (Class::Method:line)" context suffix (LogManager's %{function}:%{line}
# message pattern) for lines in the Utilities.QGCStateMachine category only.
#
# Why: that category's backing source is exactly what the portable/ port
# rewrites (src/Utilities/StateMachine/portable/, selected by
# QGC_ENABLE_QT_STATEMACHINE=OFF). __PRETTY_FUNCTION__/line-number metadata
# necessarily differs between two independently-written implementations of
# the same behavior, even when class name, method name, category, message
# text, and -- critically -- the *order* of every line are identical.
# Verifying that suffix word-for-word would be verifying source-file
# layout, not transition semantics. Every other category (the five
# unchanged consumer files: InitialConnectStateMachine,
# ComponentInformationManager, RequestMetaDataTypeStateMachine,
# ParameterManager) is held to full byte-for-byte fidelity, context suffix
# included, since those files are untouched by this port and their line
# numbers don't move.
#
# See AUDIT.md's "Q8b results" section for the full rationale and the
# ordering bug this tolerance rule was validated against.
#
# Usage: ./compare.sh <capture-output.log> [baseline.log]
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CAPTURE="${1:?usage: compare.sh <capture-output.log> [baseline.log]}"
BASELINE="${2:-$SCRIPT_DIR/baseline-mocklink-connect.log}"

normalize() {
    awk -F' - ' '
        $0 ~ / - Utilities\.QGCStateMachine - / {
            # Rejoin only the first two " - "-delimited fields (message,
            # category), dropping the third (the context suffix).
            n = split($0, parts, " - ");
            printf "%s - %s\n", parts[1], parts[2];
            next;
        }
        { print }
    ' "$1"
}

TMP_BASE="$(mktemp)"
TMP_CAP="$(mktemp)"
trap 'rm -f "$TMP_BASE" "$TMP_CAP"' EXIT

normalize "$BASELINE" > "$TMP_BASE"
normalize "$CAPTURE" > "$TMP_CAP"

if diff -u "$TMP_BASE" "$TMP_CAP"; then
    echo "COMPARE_OK: $CAPTURE matches $BASELINE (byte-identical after Utilities.QGCStateMachine context-suffix normalization)"
    exit 0
else
    echo "COMPARE_FAIL: $CAPTURE diverges from $BASELINE -- see diff above (this is a REAL divergence, not a context-suffix artifact; the normalization above already strips those)"
    exit 1
fi
