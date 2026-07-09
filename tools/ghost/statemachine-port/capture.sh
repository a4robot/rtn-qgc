#!/bin/bash
# StateMachine transition-log capture harness (Wave 15 / M8 Q8a).
#
# Boots the headless ("OFF"/no-QML) ghost against MockLink with the
# QGCStateMachine framework's built-in structured logging turned on
# (QGC_STATEMACHINE_LOG=1) and the Utilities.QGCStateMachine /
# *StateMachine* / ParameterManager logging categories unmuted
# (QGC_LOG_LEVEL=debug), drives capture-probe.ts through the connect +
# comp-info + param-set flows, then greps+normalizes the container log
# into a deterministic transition-log baseline.
#
# No source changes are required: QGCStateMachine.cc already wires
# QGC_STATEMACHINE_HISTORY / QGC_STATEMACHINE_PROFILE / QGC_STATEMACHINE_LOG
# env vars to StateHistoryRecorder / StateMachineProfiler / StateMachineLogger,
# and LogManager::applyEnvironmentLogLevel() wires QGC_LOG_LEVEL to
# QLoggingCategory::setFilterRules(). See AUDIT.md for details.
#
# Usage:
#   ./capture.sh [output-file] [run-label]
#
# Reuses the existing headless "OFF" ghost binary built for wave 14
# (qgc-build-w14gui-off/Release/QGroundControl) rather than rebuilding -
# HEAD at capture time is a docs-only commit on top of that build, i.e.
# byte-identical src/ tree. If you need a fresh build, point BUILDDIR at
# a directory produced by one of the scratchpad build-w*-off.sh scripts.
set -uo pipefail

SCRATCH="/tmp/claude-1001/-home-bpasu-git-rtn-qgc/d73f5ca7-17e3-4db7-92b0-4f406367507b/scratchpad"
REPO="/home/bpasu/git/rtn-qgc"
BUILDDIR="${BUILDDIR:-$SCRATCH/qgc-build-w14gui-off}"
OUT="${1:-$SCRATCH/statemachine-capture-raw.log}"
LABEL="${2:-run}"
NAME="w15sm-capture-$LABEL-$$"
PORT="${PORT:-8879}"

cleanup() { docker rm -f "$NAME" >/dev/null 2>&1; }
trap cleanup EXIT
docker rm -f "$NAME" >/dev/null 2>&1

# NOTE on two dead ends, kept as comments so nobody re-walks them:
#  1. QGC_LOG_LEVEL=debug (LogManager::applyEnvironmentLogLevel) is a
#     blanket "*.debug=true" QLoggingCategory::setFilterRules() call - it
#     unmutes every debug category in the process (qt.network.http2,
#     gstreamer, plugin loader, ...). That firehose stalls the main-thread
#     event loop long enough that MockLink's tick timer and the WebBridge
#     never get a look-in within a reasonable capture deadline (observed:
#     40s+ with zero ticks delivered).
#  2. QT_LOGGING_RULES (Qt's native env var) has NO effect on any
#     QGC_LOGGING_CATEGORY-declared category. QGCLoggingCategoryManager::
#     installFilter() (called from QGCApplication::init(), always, GUI or
#     headless) replaces Qt's category filter wholesale with its own
#     _categoryFilter(), which for any non-"qt.*" category decides
#     enabled/disabled purely from QGC's own _categoryLevels map (populated
#     from QSettings + the --logging CLI flag) - it never consults
#     QLoggingCategory's rules engine at all for those categories.
#
# The supported, no-source-change hook is the --logging CLI flag
# (QGCCommandLineParser's kOptLogging), which feeds
# QGCLoggingCategoryManager::installFilter(commandLineLoggingOptions) and
# unmutes exactly the categories named, at QtDebugMsg, regardless of the
# other two mechanisms above.
STATEMACHINE_LOG_CATEGORIES="Utilities.QGCStateMachine,Vehicle.InitialConnectStateMachine,ComponentInformation.ComponentInformationManager,ComponentInformation.RequestMetaDataTypeStateMachine,FactSystem.ParameterManager"

docker run -d --name "$NAME" --network host --user 1001:1001 \
  -e HOME=/tmp -e QT_QPA_PLATFORM=offscreen \
  -e QGC_STATEMACHINE_LOG=1 \
  --entrypoint /build/Release/QGroundControl \
  -v "$BUILDDIR":/build:ro \
  qgc-build-ubuntu --headless --bridge-port "$PORT" --mock-link --logging "$STATEMACHINE_LOG_CATEGORIES" || { echo "CAPTURE_FAIL: container start"; exit 1; }

for i in $(seq 1 30); do
  if ss -tln | grep -q ":$PORT "; then break; fi
  sleep 1
done
ss -tln | grep -q ":$PORT " || { echo "CAPTURE_FAIL: port never opened"; docker logs "$NAME" 2>&1 | tail -30; exit 1; }

PROBE_URL="ws://127.0.0.1:$PORT" bun "$REPO/tools/ghost/statemachine-port/capture-probe.ts"
rc=$?

# Let straggler logs (finished()/deleteLater() teardown) land before we pull.
sleep 1
docker logs "$NAME" > "$OUT.raw" 2>&1

if [ $rc -ne 0 ]; then
  echo "CAPTURE_FAIL: probe rc=$rc - see $OUT.raw"
  exit 1
fi

# Keep only lines from the state-machine-relevant logging categories.
grep -E \
  'Utilities\.QGCStateMachine|Vehicle\.InitialConnectStateMachine|ComponentInformation\.ComponentInformationManager|ComponentInformation\.RequestMetaDataTypeStateMachine|FactSystem\.ParameterManager' \
  "$OUT.raw" > "$OUT.filtered"

# Normalize: strip the leading process-relative timestamp
# ("%{time process}" from LogManager's message pattern), raw object
# pointer addresses (qCDebug(...) << this), and this run's container name
# - none of these are meaningful to a transition-order comparison and all
# vary run to run.
sed -E \
  -e 's/^[[:space:]]*[0-9]+\.[0-9]+ +//' \
  -e 's/\[\+ *[0-9]+\.[0-9]+\] //' \
  -e 's/0x[0-9a-fA-F]+/0xADDR/g' \
  -e "s/$NAME/CONTAINER/g" \
  "$OUT.filtered" > "$OUT"

rm -f "$OUT.filtered"
echo "CAPTURE_OK: normalized log at $OUT ($(wc -l < "$OUT") lines), raw at $OUT.raw"
