<!-- BEGIN vegito:claude-memory (auto-synced, do not edit) -->
# Claude Code memory — shared by ออม → หลิงหลิง

## de-qt-testing-protocol.md

---
name: de-qt-testing-protocol
description: "vegito's standing order (2026-07-09): detailed tests at EVERY de-Qt step — especially StateMachine, Network, WebSockets replacements"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: d73f5ca7-17e3-4db7-92b0-4f406367507b
---

vegito's instruction during wave 13 planning: "ทำเทสต์อย่างละเอียดทุกขั้นตอนโดยเฉพาะ StateMachine, Network, WebSockets" — every M7/M8 replacement step must carry detailed tests, with extra rigor on the three riskiest swaps.

**Why:** StateMachine drives the vehicle connect flow (safety-critical, 90-file framework); Network carries MAVLink links + terrain HTTP; WebSockets IS the bridge every client depends on. A silent behavior drift in any of these is worse than a build break.

**How to apply (per replacement job):**
1. **Characterization tests FIRST** — pin the current Qt-based behavior with tests written and passing BEFORE the swap (same discipline as the GeoCoordinate plan: generate expected values from the Qt implementation, then hold the replacement to them).
2. **Equivalence phase** — where feasible run old and new side by side (flag-switched) and diff observable behavior (state transition sequences, wire bytes, timing tolerances).
3. **Full e2e regression per step** — mockghostprobe + video e2e + selftest must pass after every job lands, not just at wave end; CI's ghost-strangler workflow is the backstop.
4. **StateMachine specifically:** capture transition logs of InitialConnectStateMachine/RequestMetaData flows against MockLink before porting; replay-compare after.
5. **WebSockets specifically:** protocol conformance suite against PROTOCOL.md §1-§14 (the mock selftest is the model — build the same for the real bridge before swapping QWebSocketServer out).
6. **Network specifically:** MAVLink link-layer tests (UDP/TCP echo + reconnect + partial-frame) and terrain tile fetch tests before replacing QNetworkAccessManager.

Include this protocol in every M7/M8 agent brief. Related: [[zero-qt-decision]].

## discord-bridge-attachments.md

---
name: discord-bridge-attachments
description: How to actually send images/files to Discord through the vegito bridge — files param is dropped; embed file:/// URIs in the reply text
metadata: 
  node_type: memory
  type: reference
  originSessionId: d73f5ca7-17e3-4db7-92b0-4f406367507b
---

The Discord bridge for this project is vegito's own broker (`bun` process serving `~/.vegito/broker.sock`, source `/home/bpasu/git/vegito/src/broker.ts` + `discord-live.ts`), NOT the official discord plugin broker (which also runs, on a different sock). vegito's broker **silently drops the `files` parameter** of the `reply` tool — replies return "ok"/"captured" but attachments never reach Discord.

To attach a file: embed `file:///abs/path.png` as a bare token in the reply TEXT. `discord-live.ts` `collectAttachments()` scans the text for file:// refs, strips each token, and uploads the file as a native Discord attachment (must exist, non-empty, within Discord's upload cap; unreadable/oversized refs are left as text and linkified).

Other quirks: `fetch_messages` is not implemented in this broker ("not implemented in vegito broker yet"); claude.ai Artifact links don't work for the user (page not found — artifacts are private to the login). Reply result "captured" means the text was captured by a pending awaitReply (e.g. fork/worker coordination), "ok" means delivered to the public Discord sink.

Related: [[qgc-build-environment]]

## MEMORY.md

# Memory Index

- [One branch per wave](one-branch-per-wave.md) — vegito's rule: each strangler wave on its own stacked strangler-waveN branch; no pushing until ordered
- [Zero-Qt decision](zero-qt-decision.md) — FINAL (2026-07-09): Option A — ghost is a QtCore headless daemon; M7+M8 committed, M9 zero-Qt PARKED as future work; QtCore-family OK, GUI-family coupling still avoided
- [De-Qt testing protocol](de-qt-testing-protocol.md) — vegito's order: characterization tests BEFORE each M7/M8 swap + e2e after every job; extra rigor on StateMachine/Network/WebSockets; include in every de-Qt agent brief

- [Strangler Wave 1 state](strangler-wave1-state.md) — what landed 2026-07-06 (committed to strangler-wave1), G8 blockers list
- [Strangler Wave 2 state](strangler-wave2-state.md) — first QML-OFF ghost boots (2026-07-07), remaining Qml-linkage debt, next wave jobs
- [Strangler Wave 3 state](strangler-wave3-state.md) — live web UI + real C++ WS bridge (2026-07-07), types-vs-protocol fix, WebGL/TTM + screenshot-in-Docker recipe
- [Strangler Wave 4 state](strangler-wave4-state.md) — video decode 15fps + command channel live (2026-07-07), concurrent param workstream merged, wave-5 candidates
- [Strangler Wave 5 state](strangler-wave5-state.md) — MockLink ghost e2e arm→accepted (2026-07-07), tick-vehicleIds + null-telemetry fixes, boot-test skip caveat
- [Strangler Wave 6 state](strangler-wave6-state.md) — mission channel + GhostVideoSource + multi-vehicle UI all e2e-proven (2026-07-08), maplibre load-event deadlock fix, ghost/screenshot recipes
- [Strangler Wave 7 state](strangler-wave7-state.md) — plan mode + instruments + param edit + bridge auth all verified (2026-07-08), wave-8 candidates, awaiting push/next order
- [Strangler Wave 8 state](strangler-wave8-state.md) — Tauri compiles + sliders + survey fixture + QML-OFF link diet (2026-07-08), Qt6Location/FactValueGrid structural leftovers, pkill self-kill gotcha
- [Strangler Wave 9 state](strangler-wave9-state.md) — Tauri live run + orphan fix, tabbed panel, FactValueGridModel split, Location/MultimediaQuick cut blocked with exact refactor path (2026-07-08)
- [Strangler Wave 10 state](strangler-wave10-state.md) — Qt6Location CUT (22 Qt libs left), first .deb/AppImage installers, wave-11 = final QQuick tier list (2026-07-09)
- [Strangler Wave 11 state](strangler-wave11-state.md) — GHOST IS QML-FREE: 16 Qt libs, zero Qml/Quick, all e2e green (2026-07-09); link-hunt recipe, next-wave candidates
- [Strangler Wave 12 state](strangler-wave12-state.md) — host-portable ghost bundle at /home/bpasu/ghost-bundle/ runs with ZERO Docker (2026-07-09): only Qt6+ICU bundled (host system Qt6 is ABI-incompatible 6.4.2), GStreamer left on host (versions match exactly), telemetry+video e2e both green, wave-13 sidecar-wiring plan left in tauri/src-tauri/binaries/README.md
- [Strangler Wave 13 state](strangler-wave13-state.md) — clean-machine installer proof (.deb 84M → fresh ubuntu e2e 6/6), §14 notifications bridge→browser speech, roadmap finalized (2026-07-09); waves 12+13 await push order
- [Strangler Wave 14 state](strangler-wave14-state.md) — M7 start: ghost 9 Qt libs (−Multimedia −Sql), conformance baseline 69 assertions + 5 pinned gaps, Q7d demoted with Qt-vendor evidence (2026-07-09); wave-15 = Q7e WS swap + gap resolutions
- [Strangler Wave 15 state](strangler-wave15-state.md) — ghost 7 Qt libs (IXWebSocket swap −WebSockets, QXmlStreamReader −Xml), conformance 74/0/0, Q8a flat-machine audit + baseline, rtn_tauri-v0.1 tag (2026-07-10); loop mode active, wave 16 = Q8b port
- [Strangler Wave 16 state](strangler-wave16-state.md) — ghost 6 Qt libs (portable StateMachine, replay ×5 byte-identical), §15 image channel, swap baselines green, v0.1 GitHub Release published (2026-07-09); wave 17 = QVector3D cut + Q8e
- [Strangler Wave 17 state](strangler-wave17-state.md) — ghost 4→3 Qt libs (Gui+DBus dropped via linker-enumeration + qtkeychain finding; Q8e SerialPort via termios+libudev compat), conformance 81/0/0 (2026-07-10); branch re-split: wave17 = Gui/DBus+Q8e, wave18 = Q8f
- [Strangler Wave 18 state](strangler-wave18-state.md) — **M8 COMPLETE: ghost = Core + Positioning ONLY** (Q8f Network via in-house POSIX compat; TLS/HTTP tier = documented OFF-capability gap; PBKDF2 on QtCore, zero new deps; UDP data-path e2e with live MAVLink peer; conformance 81/0/0 fully provisioned) (2026-07-10); loop exited, waves 14-18 PUSHED, v0.1 release + wave-18 installers delivered
- [Strangler Wave 19 state](strangler-wave19-state.md) — §16 settings channel + SETTINGS tab (video live-apply via bridge, link CRUD, autoconnect), conformance 105/0/0, 170 web tests, ops lessons from vegito's real install (2026-07-10); unpushed
- [QGC build environment](qgc-build-environment.md) — no local Qt; build via qgc-build-ubuntu Docker image, AppImage/FUSE failure is benign
- [Discord bridge attachments](discord-bridge-attachments.md) — files param dropped; embed file:///abs/path in reply text instead; no fetch_messages; artifact links 404 for user
- [Theme Ingress branch state](theme-ingress-state.md) — PUSHED 2026-07-13; chamfer ring CSS technique (border/box-shadow silently break under clip-path); agy minions edit the same tree concurrently

## one-branch-per-wave.md

---
name: one-branch-per-wave
description: "vegito requires each strangler wave committed on its own branch (strangler-waveN), stacked — never pile waves onto one branch"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: d73f5ca7-17e3-4db7-92b0-4f406367507b
---

vegito twice corrected me for committing a new wave onto the previous wave's branch (wave 2 onto strangler-wave1, then wave 4 onto strangler-wave3). Both times the fix was a pure pointer split: `git switch -c strangler-waveN` at HEAD, then `git branch -f strangler-wave(N-1) <prev-wave-tip>`.

**Why:** they review/merge waves as stacked PRs, one per wave.

**How to apply:** BEFORE committing a new wave's work, create `strangler-waveN` branched from the previous wave's tip and commit there. Naming: strangler-wave1, strangler-wave2, ... Also: never push any branch until explicitly ordered.

Related: [[strangler-wave4-state]]

## qgc-build-environment.md

---
name: qgc-build-environment
description: How to build/verify QGC on this machine — no local Qt; use the qgc-build-ubuntu Docker image
metadata: 
  node_type: memory
  type: project
  originSessionId: d73f5ca7-17e3-4db7-92b0-4f406367507b
---

This machine has NO local Qt 6.10 install, no ~/Qt, no `just` binary (system Qt is 6.4.2, too old). QGC builds are done via Docker: image `qgc-build-ubuntu` (built 2026-07-06 from deploy/docker/Dockerfile-build-ubuntu, contains Qt 6.10.3 via aqt at /opt/Qt).

Build: `docker run --rm -v /home/bpasu/git/rtn-qgc:/project/source -v <builddir>:/project/build qgc-build-ubuntu Release`. The entrypoint runs configure+build+install; the install step's AppImage packaging ALWAYS fails in-container ("fuse: device not found") — that failure is environmental, not a code error; compile success = ninja reaching "Linking CXX executable Release/QGroundControl".

Smoke test: run as non-root or QGC refuses to start — `docker run --rm --user 1001:1001 -e HOME=/tmp --entrypoint /bin/bash ... -lc 'QT_QPA_PLATFORM=offscreen /project/build/Release/QGroundControl --simple-boot-test'`.

Related: [[strangler-wave1-state]]

## strangler-wave1-state.md

---
name: strangler-wave1-state
description: "Strangler-fig Wave 1 (M0 start) completed 2026-07-06 — what landed, what's uncommitted, known blockers for G8"
metadata: 
  node_type: memory
  type: project
  originSessionId: d73f5ca7-17e3-4db7-92b0-4f406367507b
---

Wave 1 of STRANGLER_MILESTONES.md landed 2026-07-06 on branch rtn-2025, ALL UNCOMMITTED as of that date. Delivered: QGC_ENABLE_QML CMake option (cmake/CustomOptions.cmake) + global compile definition (root CMakeLists.txt); shim src/Utilities/QGCQmlCompat.h (qgcSetCppOwnership); G4a-f shims applied in MissionManager/Comms/Settings/Logging(LogManager+QGCLoggingCategoryManager)/Camera/API-QGCCorePlugin; G5 gated FirmwareUpgradeController only; G6 --headless + --bridge-port + AppMode::Headless in QGCCommandLineParser; main.cc has a Headless case that still boots full app (G7 pending); src/WebBridge/{PROTOCOL.md,GuidedActionGate.h/cc,CMakeLists.txt} (NOT wired into src/CMakeLists.txt yet); web/ Bun scaffold; tauri/ v2 scaffold with dummy sidecar.

Verified: full Release build (QML ON) compiles clean + `--simple-boot-test` rc=0 in Docker.

Known blockers for G8 (headless OFF build): RemoteControlCalibrationController (used by Joystick core, base of JoystickConfigController), GeometryImage (used by Actuators core), FactPanelController (base of RemoteControlCalibrationController) — cannot be CMake-gated, need #ifdef treatment. QGCApplication.cc:274,281 call createQmlApplicationEngine/createRootWindow (now #ifdef'd in QGCCorePlugin) — G7 must guard those call sites. FirmwareUpgradeControllerTest links unconditionally → QGC_BUILD_TESTING=ON + QML OFF will fail. WebBridgeModule needs mavlink include dirs forwarded when wired.

## strangler-wave10-state.md

---
name: strangler-wave10-state
description: "Wave 10 done 2026-07-09 — Qt6Location CUT from ghost (−4 more libs, 22 remain), first .deb/AppImage installers, GeoJSON/Terrain/VideoManager de-QML splits; on strangler-wave10 (4 commits, NOT pushed)"
metadata: 
  node_type: memory
  type: project
  originSessionId: d73f5ca7-17e3-4db7-92b0-4f406367507b
---

Strangler Wave 10 completed 2026-07-09 (~01:00) on branch `strangler-wave10` (stacked on wave9; 4 commits 38108a8b9..1679a464d, NOT pushed — [[one-branch-per-wave]]; waves 1–9 pushed). 4 parallel Sonnet subagents + my integration; the QGCLocation split ran as a resumed follow-up to the Terrain agent.

**Landed:** in-repo GeoJSON importer (QtCore+Positioning only, behavior/error-strings pinned by tests + 40-assertion harness; holes dropped — consumers only read perimeter()); TerrainTileFetcher (plain QNetworkReply cache-first path, fixes latent timeout-wedge in Downloading state); VideoManager QQuickVideoOutput glue #ifdef-gated (MultimediaQuickPrivate genex-gated); QGCLocation split into QGCLocationCore (Location-free, feeds headless terrain) + QML-gated QGeoServiceProvider plugin; root gates Qt6::Location/LocationPrivate. FIRST INSTALLERS: `/home/bpasu/rtn-qgc-shell_0.1.0_amd64.deb` (3.5MB) + `.AppImage` (79MB) via `bun build ./index.html --outdir ../tauri/dist --production` (build:tauri script) + `cargo tauri build` (bundler auto-downloads linuxdeploy; dist embedded into binary by tauri-codegen; tauri/dist gitignored). Installer gaps: sidecar is dev docker-wrapper, unsigned, no CI, NSIS untested.

**Ghost ldd score:** OFF binary dropped libQt6Location, MultimediaQuick, PositioningQuick, QuickShapes → **22 Qt libs**. Still linked: core Qml/Quick family (Qml, QmlMeta, QmlModels, QmlWorkerScript, Quick) because headless-compiled objects reference QQuick* symbols — the final tier, documented at root link line: QGCImageProvider/ColoredSvgImageProvider/GeometryImage inherit QQuickImageProvider; VideoManager+GStreamer use QQuickWindow; QGCCorePlugin/VehicleComponent use QQuickItem. That's the wave-11 split list if vegito wants Quick fully gone.

**Verified:** all builds green in isolated dirs, OFF smoke clean, full MockLink e2e PASS on the Location-free OFF ghost, video e2e PASS (frames post-MultimediaQuick-cut), ON keeps map plugin (ldd has Location), web tsc+109 tests green.

**Process note:** subagents pausing on run_in_background builds sometimes never wake (terrain agent's final builds finished 16:49 but it slept until I took over at 01:00 — check binaries + take over measurement/commit myself rather than waiting). Multi-agent C++ on a shared tree: concurrent mid-edit states cause transient compile failures in OTHER agents' builds — retry after tree settles, or use worktree isolation next time.

## strangler-wave11-state.md

---
name: strangler-wave11-state
description: "Wave 11 done 2026-07-09 — GHOST IS QML-FREE (16 Qt libs, zero Qml/Quick), all e2e green; on strangler-wave11 (3 commits, NOT pushed)"
metadata: 
  node_type: memory
  type: project
  originSessionId: d73f5ca7-17e3-4db7-92b0-4f406367507b
---

Strangler Wave 11 completed 2026-07-09 on branch `strangler-wave11` (stacked on wave10; 3 commits af3c70dbe..498365f50, NOT pushed — [[one-branch-per-wave]]; waves 1–10 pushed). **THE MILESTONE: the QGC_ENABLE_QML=OFF ghost links 16 Qt libs and ZERO libQt6Qml*/Quick*** (was 27 pre-strangler). Full MockLink e2e + live-RTP video e2e pass on that exact binary.

**How:** 2 Sonnet agents (UI-registration tier: image providers QML-only, QQuickItem controller surfaces gated with include-vs-fwd-decl pattern — Qt 6.10 moc static-asserts on fwd-declared Q_PROPERTY pointers, and Q_DECLARE_OPAQUE_POINTER would alter ON metatype semantics; video tier: VideoManager QQuickWindow plumbing + VideoReceiver widget API gated, HwBuffers GPU tier compiles out via early CMake return()). Then my finale: 6 TUs migrated from QQmlEngine::setObjectOwnership to the wave-1 qgcSetCppOwnership shim → pch.h QQmlEngine include gated; QGroundControlQmlGlobal's QJSValue showMessageDialog API gated (headless keeps AltitudeFrame enum for MissionManager); ToolStripAction/List (QQmlComponent* Q_PROPERTY, no headless callers) to QML-only sources; root `qt_add_qml_module(${CMAKE_PROJECT_NAME})` in top CMakeLists gated (its generated qmltyperegistrations TU referenced QQmlModuleRegistration/QQmlPrivate::qmlregister — that was the last libQt6Qml pull); src/CMakeLists gates Qt6::Quick. Qt6::QmlIntegration stays (header-only macros, no .so).

**Link-hunt recipe that worked:** ldd → objdump -p NEEDED (direct vs transitive) → nm -D --undefined-only binary for QQml*/QJS* symbols → grep build tree objects/archives for the symbol → gate the source. QJSValue/QQmlComponent in Q_PROPERTY/signals pull real symbols via the shared mocs_compilation TU.

**Cross-agent coordination worked:** video agent detected UI agent's moc break mid-wave and messaged it directly (agents CAN SendMessage each other); one API-500 death resumed cleanly via SendMessage.

**Remaining Qt in ghost (16):** Bluetooth Concurrent Core DBus Gui Multimedia Network Positioning Sensors SerialPort Sql StateMachine Svg TextToSpeech WebSockets Xml. Plausible future trims (unaudited): Bluetooth/Sensors/TextToSpeech/DBus if features unused headless. Next-wave candidates beyond that: native ghost binary for the Tauri sidecar (host-runnable, kills the docker-wrapper), CI for OFF-build regression (ldd assert in a workflow), signing/release pipeline.

## strangler-wave12-state.md

# Strangler Wave 12 state — host-portable ghost bundle (2026-07-09)

Goal: make the QML-free ghost Release binary run on the HOST with no Docker,
as prep for a real Tauri sidecar (wave 13 swaps out
`tauri/scripts/docker-ghost-sidecar.sh`).

## Bundle

`/home/bpasu/ghost-bundle/` (NOT in git — artifact only), 112M total:
- `ghost.sh` — launcher; sets `LD_LIBRARY_PATH=<dir>/lib`,
  `QT_PLUGIN_PATH=<dir>/plugins`, `QT_QPA_PLATFORM=offscreen` (default,
  overridable), execs `bin/QGroundControl "$@"`.
- `bin/QGroundControl` (44M, not stripped) — copied straight from
  `qgc-build-off/Release/QGroundControl` (QGC_ENABLE_QML=OFF,
  QGC_ENABLE_MOCKLINK=ON build).
- `lib/` (65M, 20 libs / 40 files) — all 16 Qt6 libs the binary links
  + libQt6XcbQpa.so.6 (xcb plugin dep) + ICU 73 (icudata/icui18n/icuuc),
  pulled from the qgc-build-ubuntu image's `/opt/Qt/6.10.3/gcc_64/lib`.
  **Bundled because the host's system Qt6 is 6.4.2 (ABI-incompatible)** —
  confirmed via `dpkg -l | grep libqt6core6t64` → 6.4.2+dfsg, vs image's
  6.10.3. Without bundling, LD_LIBRARY_PATH ordering is the only thing
  standing between "works" and "loads wrong Qt6Core and segfaults/UB".
- `plugins/` (3.2M) — platforms/ (libqoffscreen.so default + libqxcb.so),
  tls/, sqldrivers/ (libqsqlite.so only), position/ (all 3), texttospeech/
  (both), multimedia/ (libffmpegmediaplugin.so — bundled per spec but
  INERT: needs Qt6Quick/Qml/OpenGL + Qt's private ffmpeg libs, none
  bundled/linked; fails dlopen silently+non-fatally, logged as "No
  QtMultimedia backends found". Harmless — actual video path is raw
  GStreamer via GhostVideoSource.cc, not QtMultimedia). qml/ skipped
  entirely (zero QML in this build).
- `MANIFEST.txt` — full inventory + rationale + host requirements.

## Key finding: GStreamer does NOT need bundling

Host and qgc-build-ubuntu image are BOTH Ubuntu 24.04 noble with
byte-identical apt package versions (libgstreamer1.0-0 1.24.2-1ubuntu0.1,
libglib2.0-0t64 2.80.0-6ubuntu3.8 on both). Confirmed via `ldconfig -p`
diff + `dpkg -l` on both sides. So GStreamer, GLib, X11, GL/EGL, DBus,
Kerberos, PulseAudio, fontconfig/freetype, compression libs, etc. are all
left as host dependencies — only the Qt6-ABI-mismatch libs + ICU needed
bundling. This is the opposite of what the task brief assumed might be
needed ("if GStreamer resolves from host and version-mismatch, bundle
image's gst libs too") — it does NOT mismatch here.

## Host requirements

- glibc >= 2.38 (checked via `objdump -T bin/QGroundControl | grep GLIBC_`,
  highest ref is GLIBC_2.38). Host has 2.39 (Ubuntu 24.04.3). NOT bundled
  (host's newer glibc/libstdc++ trusted; bundling would be a downgrade).
- GStreamer 1.x + plugins-{base,good,bad,ugly} + libav via apt.
- x86_64 only, no aarch64 build attempted.
- xcb platform plugin (optional, non-default) needs a live X/Wayland
  display; offscreen (default) needs nothing extra.

## E2E verification (host, zero Docker)

Run command: `cd /home/bpasu/ghost-bundle && ./ghost.sh --headless
--bridge-port 8885 --mock-link`

- Telemetry/bridge probe (`mockghostprobe.ts`): **PASS**, all 6
  assertions (tickVehicle, telemetryFlowing, missionSnapshot,
  armAccepted, armedTrue, missionDownload). Re-verified 3x across the
  session, consistently green.
- Video probe (`videoprobe.ts`): **PASS**, 30/30 h264 frames received via
  QSettings `[Video] udpUrl=0.0.0.0:5600` + `gst-launch-1.0 videotestsrc
  -> x264enc -> rtph264pay -> udpsink 127.0.0.1:5600`. One non-fatal
  warning: `GstVideoReceiver` logs "Could not create a buffer of
  requested 8388608 bytes (Operation not permitted)" because this host's
  `net.core.rmem_max` (208992 B) is far below the 8MiB udpsrc requests —
  kernel silently clamps SO_RCVBUF, frames still flow fine at test
  bitrate. For production/high-bitrate, host should raise
  `net.core.rmem_max` (needs root; not available/attempted here — this is
  a host kernel-tuning item, not a bundling defect).
- `$HOME/.config/QGroundControl/QGroundControl Daily.ini` was backed up
  before the video test's `[Video]` section was appended, and restored
  to its pre-test (minimal default) state afterward. No leftover backup
  files. Confirmed clean via a final re-run of ghost.sh + telemetry probe
  after restore.

## Wave 13 prep (doc only, no code)

Added a "Wave 12: host-portable ghost bundle exists — not yet wired"
section to `tauri/src-tauri/binaries/README.md` (only repo file touched
this wave) laying out the likely Wave 13 shape: tar the bundle, ship
`lib/`+`plugins/` as Tauri `bundle.resources`, ship `bin/QGroundControl`
as the `externalBin` sidecar (renamed `ghost-<triple>`), set
`LD_LIBRARY_PATH`/`QT_PLUGIN_PATH`/`QT_QPA_PLATFORM=offscreen` via
`Command::sidecar(...).env(...)` in `../src/lib.rs` instead of a shell
wrapper (a `.sh` wrapper isn't Windows-externalBin-friendly).
`docker-ghost-sidecar.sh` / `link-ghost.sh` were NOT modified — that swap
is the wave 13 job.

## What's still missing for a "proper" release build

- `bin/QGroundControl` not stripped (44M) — `strip --strip-unneeded`
  would shrink bin+libs meaningfully.
- No RPATH/RUNPATH baked in (patchelf) — bundle only works via ghost.sh's
  explicit LD_LIBRARY_PATH, not standalone invocation of bin/QGroundControl.
- No aarch64 build.
- No systemd/process-supervision packaging — that's the sidecar's job.

**Wave 12 full close-out (added by orchestrator):** committed on `strangler-wave12` (3 commits c7cf392ae..4b2b5248d, NOT pushed). Besides the bundle: (1) CI invariant gate `.github/workflows/ghost-strangler.yml` — OFF build + objdump DT_NEEDED assert (no Qml/Quick) + MockLink e2e via `tools/ghost/mockghostprobe.ts` (now in-repo) + web job (tsc/test/selftest×2); actionlint+shellcheck clean, first real run must prove Qt install timing. (2) Qt diet round 2: QGC_ENABLE_BLUETOOTH / QGC_ENABLE_SENSORS / QGC_ENABLE_TEXTTOSPEECH (all default ON, capability-preserving; OFF-all = 12 Qt libs, speechd error gone) + Svg QML-gated; Sql/StateMachine/Concurrent classified core; DBus is Qt-transitive, uncuttable from QGC CMake. Wave-13 rec adopted: route AudioOutput::say() through the bridge as a notification message so the browser speaks.

## strangler-wave13-state.md

---
name: strangler-wave13-state
description: "Wave 13 done 2026-07-09 — installers proven on clean machine (real ghost sidecar), §14 notification channel bridge→browser speech, milestones+De-Qt roadmap (Option A final, M9 parked); on strangler-wave13 (4 commits, NOT pushed)"
metadata: 
  node_type: memory
  type: project
  originSessionId: d73f5ca7-17e3-4db7-92b0-4f406367507b
---

Strangler Wave 13 completed 2026-07-09 on branch `strangler-wave13` (stacked on wave12; 4 commits d07c55327..c8f4e31f5, NOT pushed — [[one-branch-per-wave]]; waves 1–11 pushed, wave12 NOT pushed either — push order pending for both).

**Landed:** (1) §14 notification channel — AudioOutput::textAnnounced (fires in both TTS variants) + QGCApplication announcement signals → NotificationChannel (severity classify, 1s dedup, per-connection "Ghost bridge ready" welcome) → broadcastAll; e2e-proven (arm → " armed" notification, verbatim say() text). (2) Web toasts/bell/history + speechSynthesis for warning/critical (persisted mute; guarded for no-TTS envs); mock emits §14.3-compatible welcome; 130 tests, selftest 56/56 ×2. (3) Tauri real sidecar: GhostLaunchConfig resolves resource_dir → LD_LIBRARY_PATH/QT_PLUGIN_PATH/offscreen env per spawn; RTN_GHOST_MOCK=1 dev toggle (production = no --mock-link); bundle.resources map-form ships lib/+plugins/; stage-ghost-bundle.sh (drops inert ffmpeg plugin that broke linuxdeploy). **CLEAN-MACHINE PROOF: rtn-qgc_0.1.0_amd64.deb (84M, sha256 8de29b5c...) installed on fresh ubuntu:24.04 (281 deps) → installed ghost passed full bridge e2e 6/6 non-root** (QGC refuses root — remember for container tests). AppImage (129M) passes too; artifacts in /home/bpasu/. (4) STRANGLER_MILESTONES.md: reality update + M7/M8 committed roadmap + M9 parked — see [[zero-qt-decision]].

**Screenshots:** /home/bpasu/rtn-wave13-notifications.png + -critical.png.

**Gotchas learned:** linuxdeploy scans bundled libs' deps at BUILD time (needs LD_LIBRARY_PATH set during cargo tauri build); Tauri resources map-form pins destination subpaths; ghost refuses to run as root (create user in container tests); a shared-host port collision produced a false-positive e2e (agent caught it — always use fresh ports).

**Remaining release gaps:** signing, aarch64, binary stripping, GStreamer env for AppImage video (GST_PLUGIN_PATH unset by lib.rs), NSIS/Windows untested.

**Next:** wave 14 = start M7 per the roadmap (Q7a Multimedia gate, Q7b Sql cache flag, Q7c KML gate, Q7d Gui→QCoreApplication probe, Q7e WebSockets swap — all friction ①), or whatever vegito orders. Waves 12+13 await push order.

## strangler-wave14-state.md

---
name: strangler-wave14-state
description: "Wave 14 (M7 start) done 2026-07-09 — ghost at 9 Qt libs (−Multimedia −Sql), 69-assertion conformance baseline w/ 5 pinned gaps, Q7d demoted (StateMachine INTERFACE-links Gui); on strangler-wave14 (3 commits, NOT pushed)"
metadata: 
  node_type: memory
  type: project
  originSessionId: d73f5ca7-17e3-4db7-92b0-4f406367507b
---

Strangler Wave 14 = M7 start, completed 2026-07-09 on branch `strangler-wave14` (stacked on wave13; 3 commits ae861942c..6df4fae4d, NOT pushed). Full-diet OFF ghost now **9 Qt libs**: Core, DBus, Gui, Network, Positioning, SerialPort, StateMachine, WebSockets, Xml. Diet flag set is now: QML, BLUETOOTH, SENSORS, TEXTTOSPEECH, QT_MULTIMEDIA_BACKEND, TILE_CACHE (all =OFF for ghost builds; scratchpad build-off.sh updated).

**Landed:** conformance suite `tools/ghost/conformance/` (69 assertions: 67 PASS, 0 FAIL, **5 GAP pinned** with self-escalating gap() — the Q7e WS-swap baseline; also wired into CI). Q7a: −libQt6Multimedia (QGC_ENABLE_QT_MULTIMEDIA_BACKEND flag; video e2e byte-identical). Q7b: −libQt6Sql (QGC_ENABLE_TILE_CACHE; stub QGCTileCacheDatabase in-file — valid-but-empty cache semantics; sqlite unit tests flag-gated; honest note: no bridge surface exercises terrain fetch at runtime). Q7c: KML tier QML-gated after caller audit (all entry points QML-only; Xml stays for VehicleCameraControl until Q8c). Q7d: **demoted ①→②** — `Qt6::StateMachine`'s package config INTERFACE-links `Qt6::Gui` (Qt-vendor level), so Gui+DBus fall only with Q8b; but QGCApplicationBase alias LANDED: OFF ghost runs on QCoreApplication with **no QPA plugin needed**; latent ScreenToolsController bug fixed (QCursor include clobbered qApp macro).

**Wave-15 queue:** Q7e WebSockets swap (uWebSockets/Beast) held to the conformance baseline — first resolve the 5 gaps (fix bridge or amend PROTOCOL.md): unknown-vehicle subscribe acks-then-silence; no BAD_MESSAGE for omitted vehicleId; command/mission unknown-vehicle uses rejected-ack not error envelope (FactChannel inconsistency); §9.2 keyframe-on-join not honored (VideoStreamServer stop-gap comment). Then M8: Q8c XML rewrite ∥ Q8d image raw-bytes ∥ Q8a StateMachine audit → Q8b port (unblocks Gui+DBus).

**Ops gotchas added this wave:** concurrent ghosts cross-feed via UDP-14550 autoconnect — put `[AutoConnect] autoConnectUDP=false` in test inis; writing the QSettings ini BEFORE first boot gets overwritten — boot, append ini, restart (my own mistake repeated); ghost-bundle at /home/bpasu/ghost-bundle is wave-12 vintage (predates §14 notifications) — needs refresh from a current build before next demo.

## strangler-wave15-state.md

---
name: strangler-wave15-state
description: "Wave 15 done 2026-07-09/10 — ghost at 7 Qt libs (−WebSockets via IXWebSocket swap, −Xml via QXmlStreamReader), 5 gaps resolved (74/0/0 conformance), Q8a audit banked; on strangler-wave15 (5 commits, NOT pushed)"
metadata: 
  node_type: memory
  type: project
  originSessionId: d73f5ca7-17e3-4db7-92b0-4f406367507b
---

Strangler Wave 15 completed in loop mode ("while true till M8 done") on branch `strangler-wave15` (stacked on wave14; 5 commits fb07ce7dc..369a9f7c6, NOT pushed — waves 14+15 both await push; vegito hasn't answered the auto-push-per-wave question, default = accumulate). **Ghost = 7 Qt libs: Core, DBus, Gui, Network, Positioning, SerialPort, StateMachine.** Canonical diet flag set now includes -DQGC_ENABLE_QT_WEBSOCKETS=OFF (build-off.sh updated).

**Landed:** (1) 5 conformance gaps resolved — gap 1 by DOC amendment (late-binding subscribes blessed; web subscribes-before-vehicle by design), gaps 2-5 bridge fixes (BAD_MESSAGE, UNKNOWN_VEHICLE envelopes w/ client-side error→ack translation shim, per-client keyframe gate); suite = 74 PASS/0 FAIL/0 GAP, all gap()→check(). (2) Q7e: WsTransport interface + QtWsTransport/IxWsTransport selected by QGC_ENABLE_QT_WEBSOCKETS (default ON); IXWebSocket v12.0.1 CPM-vendored (TLS off, shared zlib); queued-invoke marshalling; 5s bounded send timeout (only divergence, documented); both transports 74/74; FactChannel moved off raw QWebSocket* to clientId. (3) Q8c: camera XML on QXmlStreamReader via materialized XmlNode tree (control flow preserved); characterization test passed against OLD then NEW identically. (4) Q8a audit: ALL 5 real machines are FLAT (no parallel/history/event-posting; ~12 load-bearing classes, ~24 stubbable) → drop-in QGCStateMachine replacement design at tools/ghost/statemachine-port/AUDIT.md + deterministic 223-line MockLink transition baseline (byte-identical ×3).

**Tag:** `rtn_tauri-v0.1` on wave-11 head, pushed (vegito's order; offered GitHub Release w/ artifacts).

**Free cut found:** Qt6::HttpServer linked only for 2 unused enum aliases in QGCNetworkHelper.h — replace + drop 3 link sites (queued wave 16).

**Wave 16 (dispatching):** Q8b StateMachine port (replay-compare vs baseline; unblocks Gui+DBus → ~4 libs), Q8d image raw-bytes channel, HttpServer cut + Q8e/Q8f characterization suites (link-layer + terrain fetch tests per testing protocol). M8 done = Core+Positioning.

**Process notes:** agents committing on their own initiative → re-home commits via `git branch -f` before wave branch creation (wave14/15 boundary was fixed this way). Multiple stale-monitor wakes per finished agent are normal; kill host loops, ignore harness-internal ones.

## strangler-wave16-state.md

---
name: strangler-wave16-state
description: "Wave 16 done 2026-07-09 — ghost at 6 Qt libs (portable QGCStateMachine, byte-identical replay ×5), §15 image channel (conformance 81), Q8e/Q8f baselines green; on strangler-wave16 (4 commits, NOT pushed)"
metadata: 
  node_type: memory
  type: project
  originSessionId: d73f5ca7-17e3-4db7-92b0-4f406367507b
---

Strangler Wave 16 (loop mode) completed 2026-07-09 on branch `strangler-wave16` (stacked on wave15; 4 commits 4e0667829..f8639d247, NOT pushed — waves 14/15/16 accumulate; vegito never answered the auto-push question). **Ghost = 6 Qt libs: Core, DBus, Gui, Network, Positioning, SerialPort.** Diet flags now include -DQGC_ENABLE_QT_STATEMACHINE=OFF (build-off.sh updated).

**Landed:** (1) Q8b portable QGCStateMachine (src/Utilities/StateMachine/portable/, 34 files, include-path shadowing + disjoint source lists; QGC_ENABLE_QT_STATEMACHINE default ON) — replay-compare byte-identical ×5 vs the 223-line baseline; process-wide FIFO step-queue matches QStateMachine's queued semantics; the baseline CAUGHT a real ordering bug during development (worker-thread interleave → spurious PARAM_SET timeouts). −libQt6StateMachine. NEW Gui blocker traced: QGCGeoMath links Qt6::Gui for QVector3D in QGCGeo.cc ENU/ECEF math → wave-17 struct-swap, then Gui+DBus drop. (2) Q8d §15 image channel — base64-in-JSON (1Hz doesn't earn §9 binary machinery), ImageProtocolManager QImage-free in OFF, MockLink deterministic RAW8U emission behind enableCamera, byte-identical e2e, conformance 74→81, web ImagePanel + 139 tests + selftest 64/64. (3) HttpServer cut (value-identical local enums; never in DT_NEEDED). (4) Q8e/Q8f characterization baselines 5/5 green: UDP echo/coalescing, TCP lifecycle/pass-through/refused, SerialConfiguration API, UrlFactory request shape + 10s transferTimeout, terrain success/404/timeout, PBKDF2 vs independent vector. Real contract pinned: LinkManager destroys links synchronously mid-emission — hold SharedLinkInterfacePtr. Serial data-path round-trip NOT covered (no pty tooling) — Q8e must close.

**Incidents:** disk hit 97% (build dirs; root-owned files need docker rm); canonical build pair was sacrificed and rebuilt; Q8d had a disk-full aborted build. GitHub Release v0.1 published mid-wave (vegito's order): https://github.com/a4robot/rtn-qgc/releases/tag/rtn_tauri-v0.1 with the wave-13 .deb+AppImage.

**Wave 17 (dispatching):** (A) QVector3D→own vec3 in QGCGeo (+ any other QtGui-type stragglers) → expect Gui+DBus gone = **4 libs**; (B) Q8e SerialPort swap behind flag (socat/pty in a container for the data-path test). Wave 18: Q8f Network (QNAM→curl, links→POSIX/asio, QPasswordDigestor→OpenSSL PBKDF2 — vector test exists). M8 done = Core+Positioning.

## strangler-wave17-state.md

---
name: strangler-wave17-state
description: "Wave 17 Q8g done 2026-07-09 — ghost at 4 Qt libs (Gui+DBus both gone), qtkeychain was the hidden DBus root cause; committed on strangler-wave17 (1 commit ddf2a2c05, NOT pushed)"
metadata:
  node_type: memory
  type: project
  originSessionId: <this session>
---

Wave 17 Q8g (this session, 2026-07-09) completed on branch `strangler-wave17` (stacked on wave16, 1 commit `ddf2a2c05`, NOT pushed). **Ghost = 4 Qt libs: Core, Network, Positioning, SerialPort.** libQt6Gui AND libQt6DBus are both gone — this was the M8 "daemon target" milestone minus Network/SerialPort (Q8e/Q8f still pending, tracked separately — Q8e ran in parallel this same wave on `src/Comms`, not touched by this work).

**The brief undersold the scope.** wave-16 assumed `QGCGeoMath`'s unconditional `Qt6::Gui` link (for `QVector3D` in `QGCGeo.cc`) was the *last* hop. It wasn't — that link was silently satisfying Gui-symbol needs from several OTHER unconditionally-compiled files whose own CMakeLists didn't declare Gui at all (CMake propagates a dependency's PUBLIC requirements to any consumer, and ninja/mold links at whole-.o granularity, not per-function, so one file needing Gui drags its whole translation unit's Gui usage in). The only reliable way to find them was the wave-11 recipe: cut the known link, rebuild, and read the linker's undefined-symbol list as ground truth — repeated twice (first pass found QColor/QCursor/QPolygonF/QGuiApplication survivors; after fixing those, DBus survived independently — see below).

**Full survivor list + fix (all in `git show ddf2a2c05`):**
- `QGCGeo.{h,cc}`: `QVector3D` → new `QGCGeo::Vec3` (double, not float — the old type silently truncated). Only `convertGpsToEnu` had a production caller (Viewer3D, QML-only); the other 5 conversion functions had zero production callers, only `GeoTest.cc`.
- `Joystick/{Joystick,JoystickSDL,JoystickManager}`: gyro/accel `QVector3D` → new `QGCVector3D` (`src/Utilities/Math/QGCVector3D.h`, Gui-free, `Q_DECLARE_METATYPE` + explicit `qRegisterMetaType` since Joystick runs on its own `QThread`). Zero production `connect()`s to these signals existed (only `QSignalSpy` in tests) so this was low-risk despite touching Q_INVOKABLE/signals.
- `VehicleFactGroup.cc` `_handleAttitudeQuaternion`: `QQuaternion`/`QVector3D` rotation math → local `AttitudeQuat` struct using the identical optimized cross-product rotation formula QQuaternion uses internally. No existing test covered this function, so characterization was a standalone (non-Qt) numeric check comparing the optimized formula against the mathematically-exact brute-force sandwich product (`q⊗(0,v)⊗q⁻¹`) over 200k random trials — max error ~2e-15 (`/tmp/.../scratchpad/quat_check.cc`, not committed, throwaway).
- `Vehicle/Actuators/Common.h` `ActuatorGeometry::position`: `QVector3D` → `QGCVector3D`. `GeometryImage.cc` (QML-only renderer, heavier QVector3D/QVector2D use — `.toVector2D()`, `operator[]`) updated to match.
- `QmlControls/QGCMapPolygon.cc` + `MissionManager/SurveyComplexItem.cc`: `QPolygonF` (Gui) → `QList<QPointF>` (Core, since `QPolygonF` just *is* `QList<QPointF>` plus two Gui-only algorithm methods) + two new helpers in `QGCMath`: `QGC::polygonBoundingRect`/`polygonContainsPoint` (standard PNPOLY even-odd ray-cast, verified against a square+triangle sanity check). `SurveyComplexItem.cc` has extensive polygon-decomposition code that turned out to only ever call `.boundingRect()` (×2) — everything else was already plain list ops that compiled unchanged.
- `QmlControls/InstrumentValueData`: `Q_PROPERTY(QColor currentColor...)` → `QString` (QML coerces JS strings to `color:` bindings automatically — checked all 3 consuming .qml files, all simple ternary/property bindings, no `.r/.g/.b` or `Qt.lighter()` calls found). Real headless caller: `VideoManager/SubtitleWriter.cc` constructs `InstrumentValueData` via `FactValueGridModel` but only reads `.fact()`, never the color — confirmed via grep before deciding QString was safe.
- `QGCPalette`/`QGCMapPalette`: the ~40-entry `QColor` theme table is pure QML-rendering config → gated the whole CMake target behind `QGC_ENABLE_QML`. Three headless-compiled callers adjusted: `AppSettings.cc`'s `QGCPalette::setGlobalTheme()` calls (`#ifdef QGC_ENABLE_QML`, doesn't touch QColor itself but pulled the whole .o in), `QGCCorePlugin.h`'s `paletteOverride` virtual (only ever called from `QGCPalette.h`'s own macros — safe to gate), `QGroundControlQmlGlobal` (genuine headless dependency of `MissionManager` — `SurveyComplexItem`/`CameraCalc`/`MissionController`/etc all use it — only its `_globalPalette` member is QML-only, gated that one field).
- `AutoPilotPlugins/APM/APMAirframeComponentController.cc` + `PX4/AirframeComponentController.cc`: `QCursor`/`QGuiApplication::set|restoreOverrideCursor` wait-cursor UI feedback around a firmware param download — zero headless relevance → small `qgcSetWaitCursor()`/`qgcRestoreCursor()` no-op helpers under `QGC_ENABLE_QML`.

**The DBus survivor was a genuinely separate root cause, not transitive-via-Gui as every prior wave's comments assumed.** After Gui fully dropped, `libQt6DBus.so.6` remained as a *direct* `DT_NEEDED` on the executable (confirmed via `readelf -d`; none of Core/Network/Positioning/SerialPort's own `.so`s need it). Traced via the raw ninja link line (grep for `libQt6DBus.so` context) to `libqt6keychain.a` sitting right next to it — vendored `qtkeychain` (CPM, `src/Utilities/Platform/CMakeLists.txt`) unconditionally `find_package(Qt6 COMPONENTS DBus REQUIRED)`s on Linux for its KWallet-over-D-Bus backend, with **no upstream CMake option to disable it** (confirmed by reading its actual `CMakeLists.txt` — `LIBSECRET_SUPPORT=OFF` only drops libsecret, not the KWallet/DBus code path, which sits outside that option's `if()` block). `QGCKeychain.cc` already had a QSettings-backed runtime fallback for exactly "no backend daemon reachable (headless)" — now `qt6keychain` is only linked under `QGC_ENABLE_QML`, and `QGCKeychain.cc`'s three public functions go straight to `fallbackWrite/Read/Remove` when `QGC_HAS_KEYCHAIN` isn't defined, via `#ifdef` wrapping the qtkeychain-specific helpers (verbatim code, zero logic change for the ON path).

**Build/test infra gotcha (new):** `QGC_BUILD_TESTING` is a `cmake_dependent_option` gated on Debug builds — passing `-DQGC_BUILD_TESTING=ON` with `-DCMAKE_BUILD_TYPE=Release` silently no-ops it (ctest reports "No tests were found"). Must use `-DCMAKE_BUILD_TYPE=Debug -DQGC_BUILD_TESTING=ON` in a dedicated build dir (this wave used `qgc-build-w17gui-test`) for characterization. Also: docker-built dirs are root-owned; ctest run as root gets refused by QGC's own root-check *and* can't write `Testing/Temporary/` — either build+test both as the container's default root user with a `chown -R 1001:1001` in between, or use `--user 1001:1001` consistently for both stages (mixing caused two separate failures this wave).

**Two test failures confirmed pre-existing, not regressions:** `SurveyComplexItemTest` (4/8 "Uncategorized log messages" — a `qDebug()`-not-`qCDebug()` hygiene lint in the test file itself, plus a ~56-145s runtime that's borderline against ctest's default timeout under concurrent Docker load) and `QGCKeychainTest` (8/10 fail with `"Cannot autolaunch D-Bus without X11 $DISPLAY"` — no dbus-daemon in the sandbox, and the existing `isMissingSecretService`/`indicatesMissingBackend` matching doesn't recognize this exact error string as "missing backend"). Both verified by reverting the touched files to HEAD, rebuilding, and reproducing byte-identical failures on unmodified code in the same sandbox before restoring the wave-17 versions.

**Verification:** `ldd` on `qgc-build-w17gui-off` → `libQt6Core.so.6 libQt6Network.so.6 libQt6Positioning.so.6 libQt6SerialPort.so.6` only. `mockghostprobe.ts` 6/6 PASS (port 8863, fresh container, `autoConnectUDP=false` written after first boot then restarted, per the established recipe). `tools/ghost/conformance/run.ts` 69 PASS / 0 FAIL / 0 GAP / 2 SKIP (bridge-token and video groups skipped — no second ghost/no GStreamer feed in this simple probe, both expected). ON+Debug+Testing build: GeoTest/JoystickTest/JoystickManagerTest/MockJoystickTest/FactGroupTest/QGCMapPolygonTest/APMAirframeComponentControllerTest 7/7 green both before (HEAD) and after.

**Next wave candidates:** Q8e (SerialPort → portable termios+libudev, running in parallel this same wave on `src/Comms`, not covered here) and Q8f (Network → curl/asio) are the last two libs on the M8 "2 libs: Core, Positioning" committed-path target. Build scripts for this wave: `/tmp/claude-1001/-home-bpasu-git-rtn-qgc/d73f5ca7-17e3-4db7-92b0-4f406367507b/scratchpad/{build-w17gui-off.sh,build-w17gui-test.sh,w17gui-ctest2.sh,w17gui-probe.sh,w17gui-conformance.sh}`.

**Q8e supplement (orchestrator):** SerialPort also fell this wave — ghost = **3 Qt libs (Core, Network, Positioning)**. In-house termios+libudev compat (libserialport REJECTED: LGPL-3.0 vendoring obligation + autotools-only — license discipline precedent: all vendored deps are BSD/in-house). Include-path shadowing via extension-less redirector headers `portable/QtSerialPort/*` = zero consumer #ifdefs (same pattern as Q8b). pty data-path test (posix_openpt in-test, no socat) greened against QSerialPort FIRST — corrected two wrong assumptions (portName strips /dev/; Qt buffers writes unboundedly). Covers SerialLink worker, QGCSerialPortInfo enumeration (libudev tty walk), GPSProvider blocking no-event-loop mode (poll(2)-based waitFor*), Bootloader flash protocol. Flag QGC_ENABLE_QT_SERIALPORT default ON; diet build-off.sh updated. Commit dde602225. Conformance 81/0/0 post-merge with the Gui/DBus commit. Wave 18 = Q8f Network (final M8 job): QNAM terrain fetch → ?, UDP/TCP links, QPasswordDigestor→OpenSSL, QGCNetworkHelper — baselines all exist from wave 16.

## strangler-wave18-state.md

---
name: strangler-wave18-state
description: "Wave 18 Q8f done 2026-07-10 — M8 COMPLETE: ghost = 2 Qt libs (Core, Positioning); libQt6Network out via in-house POSIX compat + HTTP-tier flag-gating; on strangler-wave17 (commit pending push)"
metadata:
  node_type: memory
  type: project
  originSessionId: d73f5ca7-17e3-4db7-92b0-4f406367507b
---

Wave 18 (this session, 2026-07-10) completed Q8f — THE FINAL M8 JOB — on branch `strangler-wave17`. **Ghost = 2 Qt libs: Core, Positioning. M8 daemon target reached.** DT_NEEDED: libQt6Core, libQt6Positioning, libudev (Q8e serial), libgcrypt (pre-existing signing), GStreamer/glib stack, libc/libstdc++. No OpenSSL added (see PBKDF2 below).

**Design — flag `QGC_ENABLE_QT_NETWORK` (default ON) splits QtNetwork in two tiers per the audit:**
1. **Socket/link tier → ported**: `src/Comms/portable/QGCNetworkCompat.{h,cc}` — in-house IPv4-only POSIX compat (QHostAddress, QAbstractSocket, QUdpSocket, QTcpSocket, QTcpServer, QNetworkDatagram, QHostInfo, QNetworkInterface::allAddresses, QNetworkProxy no-op) via include-path shadowing `portable/QtNetwork/*` (Q8e pattern; zero consumer #ifdef for this tier). Covers UDPLink/TCPLink workers (QSocketNotifier), UdpIODevice/NMEA, ADSBTCPLink (buffered-read model so canReadLine/readLine work), VideoManager Viewpro ping, APM Artoo ping, WebBridge listen address, test peers. Key semantics: TCP async connect completion (write-notifier) AND waitForConnected; failed connect emits errorOccurred+stateChanged but NOT disconnected (Qt parity, TCPLinkTest pins it); writeData poll-retry never returns 0 on EAGAIN (TCPWorker treats 0 as error); EOF drains buffer, emits readyRead, then RemoteHostClosedError+disconnected.
2. **PBKDF2**: `QPasswordDigestor::deriveKeyPbkdf2` re-implemented on QMessageAuthenticationCode (QtCore HMAC) — NO OpenSSL (image lacks openssl-dev; zero new deps). Bit-for-bit vs pinned vector + iter=2/4096 multi-block vectors (standalone docker check compiles the shipped code extracted verbatim from the .cc: w18net-pbkdf2-check.sh).
3. **HTTP(S)/QNAM tier → gated, not ported** (production endpoints are https; TLS vendoring out of scope): Terrain network fetch (cache-first path intact), QGCFileDownload/QGCCachedFileDownload + app version check, NTRIP subdir entirely (uses QSslSocket), MAVLinkLogManager upload, VehicleCameraControl definition-XML http, ComponentInformation HTTP metadata (MAVLink-FTP path unaffected), QGCCachedTileSet bulk download, UrlFactory::getTileNetworkRequest, FirmwarePlugin latest-stable check, APMAirframe frame-params download. All degrade to qCWarning + graceful-fail. `QGC_ENABLE_QT_NETWORK=OFF` FATAL_ERRORs unless `QGC_ENABLE_QML=OFF` (Qt6::Location hard-deps QtNetwork; mixing real+compat headers = ODR).

**Gotchas found:** (a) extension-less shadow headers don't cover Qt's lowercase includes (qabstractsocket.h) — any stray real-QtNetwork include (e.g. ungated QGCFileDownload.h consumer) explodes as ODR redefinitions; the two missed consumers were APMAirframeComponentController.cc and QGCApplication.h's slot decl. (b) moc property metatypes need complete types: Q_PROPERTY(NTRIPManager*) had to be gated with its subject, nullptr-degrade impossible. (c) tests can't build QML=OFF (UnitTest framework needs QML), so OFF-backend verification is e2e not ctest: probe + conformance + new UDP data-path e2e (w18net-udp-e2e.{sh,ts}: real MAVLink v2 heartbeats sysid 66 → compat RX → vehicle in tick; GCS return traffic → compat TX; REMEMBER bridge needs hello handshake before it emits anything).

**Verification:** ON ctest 13 suites before(HEAD, via stash)/after identical: 12/13, only SigningTest::_testAddRawKey fails (pre-existing dbus-keychain env, Pbkdf2 vector PASS both). OFF: build green, ldd = Core+Positioning, mockghostprobe 6/6 (port 8861), conformance **81/0/0/0 SKIP incl. video+token groups** (8863/8864), UDP e2e PASS. Canonical build-off.sh + fresh qgc-build-w18net-off carry -DQGC_ENABLE_QT_NETWORK=OFF.

**Incidents:** wave started with an audit fork overstepping read-only brief and implementing ~60% (good quality, reviewed line-by-line, kept); disk 97% again — deleted stale w17ser/w17gui-off build dirs.

Related: [[strangler-wave17-state]], [[zero-qt-decision]], [[de-qt-testing-protocol]], [[one-branch-per-wave]]

## strangler-wave19-state.md

---
name: strangler-wave19-state
description: "Wave 19 done 2026-07-10 — §16 settings channel + web SETTINGS tab (configure ghost from UI: video live-apply, link CRUD, autoconnect); conformance 105/0/0, 170 web tests; on strangler-wave19 (2 commits, NOT pushed)"
metadata: 
  node_type: memory
  type: project
  originSessionId: d73f5ca7-17e3-4db7-92b0-4f406367507b
---

Strangler Wave 19 (vegito's direct product request: "แอปตั้งค่าอะไรไม่ได้เลย") completed 2026-07-10 on branch `strangler-wave19` (stacked on wave18; commits 93ed8ef9a + a9e735960, NOT pushed — wave18 was re-homed back to a9d543d5b after the C++ agent committed on it again).

**Landed:** §16 settings surface — getSettings/setSetting against a server-enforced 18-entry whitelist (Video: videoSource+enumValues/udpUrl/rtspUrl/tcpUrl/streamEnabled/lowLatencyMode; App: mission-editing defaults; AutoConnect: UDP/Pixhawk/SiK/RTK + udpListenPort — every entry cites a real non-QML consumer), settingChanged broadcast (§14-style unsubscribed), link CRUD (getLinks/addLink/removeLink/connect/disconnect via LinkManager; name-keyed, linkAck stateless → client re-polls), new §10 codes SETTING_NOT_ALLOWED/UNKNOWN_SETTINGS_GROUP. **Video live-apply**: SettingsChannel.videoSettingChanged → GhostVideoSource::start() (idempotent, re-resolves URI) — feature e2e proved set-via-bridge → frames on an open subscription, zero restart. streamEnabled is advisory-only in v0.1 (documented §16.6). Web: SETTINGS tab (4th, mapMode-neutral), Video/AutoConnect/App/Links sections, optimistic-off pending semantics, session fetches settings+links each (re)connect; mock §16 emulation + 23 selftest checks (87/87 ×2 modes); pre-existing --fixture-flag-not-forwarded selftest bug fixed (stash-verified). Conformance **105/0/0** (81+24). 170 web tests. Screenshots /home/bpasu/rtn-wave19-settings{,-links}.png.

**Ops journal this wave:** vegito installed the wave-18 .deb and ran it: (1) "second instance" error = my demo ghost held the user-wide single-instance lock — killed; (2) "still has video" = MY leftover gst feed + [Video] ini section persisted in the shared user config — both cleaned (lesson: test residue on a shared host leaks into the user's real runs; always clean gst/ini after demos); (3) mock is runtime-opt-in (RTN_GHOST_MOCK=1) — no build change needed for production. ghost-bundle refreshed to wave-19 binary; wave-19 installers rebuilt (deb+AppImage) for delivery since his installed wave-18 ghost lacks §16.

**Next candidates:** streamEnabled real enforcement, settings persistence conformance fixture (ghost-restart harness), master merge for CI activation (still pending vegito's choice), v0.2 tag/release with wave-19 artifacts.

**Wave 20 (same session, vegito's layout feedback "ผิดที่ทางมาก"):** FlyView-parity restructure on branch `strangler-wave20` (1 commit 879d5a7f0, stacked on wave19, NOT pushed): full-viewport map (single instance, CSS class swap full↔PiP — never remounted), click-PiP video↔map swap (QGC signature; interaction layers unmount while map is PiP), top toolbar (FLY/PLAN switcher + mode/armed/battery/GPS/connection chips + bell + vehicle select + gear→PARAMS/SETTINGS right drawers), floating instrument column, bottom-center guided strip, Plan left drawer, collapsible image card. SidePanel tabs retired. Presentation-only; 170 tests + selftest ×2 green. Screenshots /home/bpasu/rtn-wave20-{fly,swap,plan,overlay}.png.

## strangler-wave2-state.md

---
name: strangler-wave2-state
description: "Strangler-fig Wave 2 completed 2026-07-07 — first QML-OFF ghost binary builds and boots; what landed, remaining debt"
metadata: 
  node_type: memory
  type: project
  originSessionId: d73f5ca7-17e3-4db7-92b0-4f406367507b
---

Wave 2 (Swarm Day 2 of STRANGLER_MILESTONES.md) landed 2026-07-07 on branch strangler-wave1, committed as 9 commits (d9fe01ccc..4d4a03ece), NOT pushed. Delivered: G2 (src/CMakeLists.txt gates qt_add_qml_module + 8 UI subdirs + Qml/Quick link libs via genex; QmlControls stays — core deps), G7 (_initForHeadlessBoot in QGCApplication; Gui falls back to headless when QML OFF; LinkManager::shutdown() called in headless app shutdown — without it ~UDPLink SIGSEGVs during static teardown, found via gdb), B1a (WebBridge base class: serverTimeUs, per-stream seq with snapshot=seq==1, 1Hz tick; wired into build — WebBridgeModule mirrors app includes via TARGET_PROPERTY genex), OFF-build fixes (ActuatorComponent #ifdef+nullcheck, QGCRhiCapture qrhi() nullptr fallback, APMSensorsComponentController includes LinkInterface.h for standalone moc), web: Zustand stores+bindBridgeToStores, Bun mock bridge (§12 conformant, 24 selftest assertions, `bun run mock`), WebCodecs decoder (17 tests), MapLibre CoreMap.

Verified in Docker (qgc-build-ubuntu): QML ON build green + --simple-boot-test rc=0 + --headless stable/clean shutdown; QML OFF build green (only 3 TUs needed fixes!) + --headless stable + Gui-mode falls back with warning.

Remaining debt: OFF binary still links libQt6Qml/Quick transitively (QmlControls + every ungated qt_add_qml_module module target, e.g. FactControlsModule, VehicleSetupModule) — M0's zero-Quick-link goal needs the QmlControls core/view split (Wave 3 / G8). Test tree (test/AnalyzeView, test/Viewer3D, FirmwareUpgradeControllerTest) breaks with QGC_BUILD_TESTING=ON + QML OFF. Runtime headless risk: Vehicle.cc:3488 derefs qgcImageProvider() (returns nullptr headless) on MAVLink IMAGE_AVAILABLE.

Next per plan (Day 3 swarm): B1b (QWebSocketServer), B2a (TelemetryChannel), W1b done, W2a/W2b telemetry panels, W3b VideoPlayer, W7b vehicle marker, T-lane integration.

Related: [[strangler-wave1-state]], [[qgc-build-environment]]

## strangler-wave3-state.md

---
name: strangler-wave3-state
description: Strangler-fig Wave 3 completed 2026-07-07 — live web UI vs mock + real C++ WS bridge serving; branch strangler-wave3 (unpushed)
metadata: 
  node_type: memory
  type: project
  originSessionId: d73f5ca7-17e3-4db7-92b0-4f406367507b
---

Wave 3 landed 2026-07-07 on branch strangler-wave3 (7 commits f42335760..8f6f152c5, NOT pushed). Built by Claude Sonnet-5 subagents (user switched away from agy mid-dispatch): B1b WebBridgeServer (localhost-only QWebSocketServer, full session/error layer), B2a TelemetryChannel (10Hz §4 payloads from Vehicle facts), W2a Attitude (SVG horizon+compass), W2b Status, W3b VideoPlayer (rAF-coalesced canvas), W7b VehicleLayer (marker+trail). My integration: bridge trio instantiated in headless boot when --bridge-port set (root CMake +WebSockets component); web session.ts handshake (hello+subscribe on reconnect, fire-and-forget); App shell assembly.

Critical fix: web types.ts had invented field names (yawDeg/voltageV/remainingPct/fixType) diverging from PROTOCOL.md §4, and modeled tick as a channel/seq message vs §11.1's envelope-free tick — runtime-empty UI despite green tsc. Protocol is normative; all aligned now. Also: seq==1 = §2.4 stream restart not a gap; maplibre's .maplibregl-map{position:relative} overrode .core-map absolute → 0x0 container (fixed via double-class specificity).

Verified: QML ON build 163/163 + OFF build green; BOTH binaries serve the bridge live (hello→helloAck→subscribeAck→1Hz ticks, probe script scratchpad/ghostprobe.ts); web tsc + 24 tests; screenshot of UI vs mock shows live horizon/battery/ORBIT+ARMED/map marker+trail (Bangkok fixture).

Env quirks (this machine): kernel TTM (GPU) kworkers stuck in D-state for days → ALL WebGL context creation hangs host-side; capture browser screenshots inside Docker (no /dev/dri) with playwright chromium mounted from ~/.cache/ms-playwright + bun mounted, --network host (see scratchpad shot2.ts pattern). Port 3000 owned by user's GANGLIA app — web dev server runs PORT=3210. Mock bridge on 8877, real-ghost tests use 8899.

Next waves: B5a GStreamer H.264 tap → binary video frames (VideoPlayer+Decoder ready, BridgeClient needs binary hook), commandAck channel + Actions panel (GuidedActionGate exists), W8a waypoints, QmlControls core/view split (kill libQt6Qml linkage), SITL e2e with real vehicle telemetry.

Related: [[strangler-wave2-state]], [[strangler-wave1-state]], [[qgc-build-environment]], [[discord-bridge-attachments]]

## strangler-wave4-state.md

---
name: strangler-wave4-state
description: Strangler-fig Wave 4 completed 2026-07-07 — video decode + command channel live; param workstream merged; branch strangler-wave3 (unpushed)
metadata: 
  node_type: memory
  type: project
  originSessionId: d73f5ca7-17e3-4db7-92b0-4f406367507b
---

Wave 4 landed 2026-07-07 on branch strangler-wave3 (8 commits 187632111..e221e7379, NOT pushed; branch now carries waves 3+4). Sonnet-5 subagents: B5a+B5b VideoStreamServer (GStreamer tee tap → §9.2 framed AUs, pImpl, inert without QGC_GST_STREAMING — I fixed its pImpl access bug: callback moved into static Impl member, anon-namespace closed before the member def), B7a+B7b CommandChannel (§5 actions via Vehicle guided API + GuidedActionGate gating) + WebBridgeServer client tokens/sendToClient/broadcastBinary, web binary hook (BridgeClient onBinaryFrame), ActionsPanel (two-step confirm, ack/progress reducer), mock H.264 streaming (fixture web/mock/fixtures/video.h264, ffmpeg testsrc2 AUD-delimited — regen: ffmpeg testsrc2 + h264_metadata=aud=insert + repeat-headers) + stateful commands.

A CONCURRENT workstream (one of vegito's other brains, unconfirmed) added the §6 param channel in the same checkout mid-wave: FactChannel.{h,cc}, web paramStore/ParamTable/BridgeContext, QGCApplication wiring. Preserved, verified with everything else, committed attributed as "(concurrent workstream)" (c73d9efcb, and FactChannel inside e4ee7107a).

Protocol fix: §9.1 video streamId unified to numeric u8 (was string "cam1" + separate streamIndex — two agents implemented opposite readings). Decoder now falls back to prefer-software when hardware decode config is rejected (needed headless/VM).

Verified: ON 89/89 + OFF builds green; ghost live e2e incl. command round-trip (arm → rejected "Unknown vehicle" — correct with no autopilot); web tsc + 35 tests; mock selftest 40/40; cockpit screenshot shows 15fps decoded test pattern + takeoff progress + ARM/TAKEOFF/LAND/RTL/PAUSE buttons + param table + live map/horizon.

Wave 5 candidates: GstVideoReceiver tee accessor + VideoStreamServer live attach + videoConfig replay-on-subscribe caching in WebBridgeServer (B5 wiring risks list in 187632111), SITL e2e (real vehicle → telemetry/commands through ghost), commandProgress from real MAV_RESULT (mavResult currently never populated), DualCam W4a/b, waypoints W8a, QmlControls split.

Related: [[strangler-wave3-state]], [[qgc-build-environment]], [[discord-bridge-attachments]]

## strangler-wave5-state.md

---
name: strangler-wave5-state
description: "Strangler-fig Wave 5 completed 2026-07-07 — MockLink ghost e2e (arm accepted, mavResult 0), DualCam/latency, click-to-goto; branch strangler-wave5 (unpushed)"
metadata: 
  node_type: memory
  type: project
  originSessionId: d73f5ca7-17e3-4db7-92b0-4f406367507b
---

Wave 5 landed 2026-07-07 on branch strangler-wave5 (7 commits c284bdb5c..313edfa85, NOT pushed; waves 1-4 pushed). Delivered: MockLink unlocked beyond Debug (option QGC_ENABLE_MOCKLINK, widened QT_DEBUG guards in LinkConfiguration/LinkManager, 2 Release-only -Werror fixes in MockLink sources, --mock-link CLI); commandAck correlated with real MAV_RESULT (register-before-send, (vehicleId,MAV_CMD) keying, 7s timeout, firmware-dependent coverage documented per action); video attach groundwork (GstVideoReceiver void* pipeline/tee accessors valid in streamingChanged bracket, WebBridgeServer videoConfig cache+replay — attach still blocked: headless boot never creates receivers, VideoManager::init needs QQuickWindow → wave-6 job); web: DualCam+LatencyOverlay (onFrameMeta additive prop, clock-offset latency), GotoOnClick, mock stream 2, App URL overrides (?bridge=ws://... &vehicle=N).

Bugs found by real data: (1) WebBridge::setVehicleIds had NO caller — tick vehicleIds always [] (fixed: TelemetryChannel syncs); (2) §4 null telemetry (MockLink pre-GPS-fix) crashed panels typed number — Telemetry leaves now number|null, null-safe rendering.

FLAGSHIP E2E (mockghostprobe.ts): ghost --headless --bridge-port 8899 --mock-link → vehicle 128 in tick, 10Hz real telemetry, arm → commandAck accepted mavResult:0, telemetry armed:true. Real-ghost cockpit screenshot: Zurich (MockLink home), ARM enabled + TAKEOFF/LAND/RTL gated off while disarmed. Mock DualCam shot: 2×15fps offset streams, latency chips 5/6ms.

Caveat: final QML-ON GUI --simple-boot-test SKIPPED — host GPU/TTM wedge worsened (load 128), GStreamer boot probe hangs; headless e2e boots the full app and wave 5 touched no GUI-boot path. Re-run boot test after host reboot. Build scripts now pass -DQGC_ENABLE_MOCKLINK=ON (cached in both scratchpad build dirs).

Wave 6 candidates: headless video receiver creation (the last video gap), GuidedActionGate live wiring into CommandChannel gating with vehicle-state reasons, mission channel (B-lane), multi-vehicle UI selection from tick, W8a waypoints/mission upload, QmlControls split.

Related: [[strangler-wave4-state]], [[one-branch-per-wave]], [[qgc-build-environment]]

## strangler-wave6-state.md

---
name: strangler-wave6-state
description: "Wave 6 done 2026-07-08 — mission channel + GhostVideoSource + multi-vehicle UI, all e2e-proven vs MockLink ghost; committed on strangler-wave6 (5 commits, NOT pushed)"
metadata: 
  node_type: memory
  type: project
  originSessionId: d73f5ca7-17e3-4db7-92b0-4f406367507b
---

Strangler Wave 6 completed 2026-07-08 on branch `strangler-wave6` (stacked on wave5; 5 commits df2b2b3b8..2f79fb0fd, NOT pushed — [[one-branch-per-wave]]).

**Landed:** C++ MissionChannel (§7 missionState stream + upload/download/clear via PlanManager; upload acks itemCount = sent−1 when PlanManager skipFirstItem drops the home item — documented, not a bug) and GhostVideoSource (bridge-owned GstVideoReceiver from VideoSettings `Video/videoSource` + `Video/udpUrl`, attach on streamingChanged(true), 5s→30s backoff). Web: mission types/store/mock, BridgeClient 5s tick-liveness watchdog + seq-gap resubscribe (2s debounce), session subscribes telemetry+mission per active vehicle, VehicleSelect header chip, MissionLayer waypoints+dashed route.

**All e2e-proven post-reboot** (fresh Docker rebuild, ON+MockLink and QML-OFF both green, first compile of new C++): arm ack mavResult:0, missionUpload → 3 waypoints, gst-launch RTP/H264 → udp 5600 → §9 binary frames magic 0x4656 + videoConfig 640x480. Screenshot `/home/bpasu/rtn-wave6-ghost.png` (web vs real ghost: video ball 15fps, waypoints 0→1→2 on map).

**Key bug found via screenshot:** CoreMap delivers the map post-`load`, so layer children gating on `once("load")` fallback deadlock when `isStyleLoaded()` is transiently false — fixed with idle-retry in MissionLayer + VehicleLayer (2f79fb0fd). Also: subscribeAck carries `channel:"mission"` — bindBridge must filter for `items[]` before storing.

**Ghost e2e recipe:** container needs `--entrypoint /build/Release/QGroundControl` (image entrypoint expects BUILD_TYPE arg). QSettings ini lives at `/tmp/.config/QGroundControl/QGroundControl Daily.ini` (HOME=/tmp). Host has gst-launch-1.0 + x264enc/rtph264pay. Playwright chromium at `~/.cache/ms-playwright/chromium-1228/chrome-linux64/chrome` (note: chrome-linux64, not chrome-linux) — works natively post-reboot, no Docker needed.

**Next (Wave 7):** mission upload UI / waypoint editing (click-to-add + upload via MissionChannel), QmlControls core/view split candidates per STRANGLER_MILESTONES.md.

## strangler-wave7-state.md

---
name: strangler-wave7-state
description: "Wave 7 done 2026-07-08 — plan mode UI (W8a/b), instruments (W9a), param edit (W6b), bridge auth+bind (B14a/b); on strangler-wave7 (5 commits, NOT pushed)"
metadata: 
  node_type: memory
  type: project
  originSessionId: d73f5ca7-17e3-4db7-92b0-4f406367507b
---

Strangler Wave 7 completed 2026-07-08 on branch `strangler-wave7` (stacked on wave6; 5 commits ba5a9f65a..7bf87a609, NOT pushed — [[one-branch-per-wave]]). Ran as 4 parallel Sonnet subagents on disjoint files + my integration pass.

**Landed:** W8a/W8b plan mode — planStore draft mission (seq==index invariant), uiStore mapMode fly|plan, BridgeClient.onMissionResponse, WaypointAdder (plan-mode map clicks → blue draft layer; fly-mode GotoOnClick conditionally mounted in App so one click never means two things), WaypointList panel (FLY/PLAN toggle, editable alt, upload/clear with single in-flight ack tracking). W9a Instruments strip (null-safe §4, SVG compass). W6b ParamEdit completed (validateParamValue pure helper, id-matched setParam round-trip, 5s timeout). B14a/b `--bridge-host` (localhost default kept) + `--bridge-token` (AUTH_FAILED + close on mismatch in _handleHello; unset = unchanged), token redacted from logs, PROTOCOL.md §1.1 updated.

**Verified:** tsc clean; 88 bun tests; 53 mock selftest assertions; ON+MockLink and QML-OFF Docker builds green; legacy e2e probe passes on new binary with no flags; auth probe: wrong token → CLOSED:AUTH_FAILED, right token → accepted. Screenshot `/home/bpasu/rtn-wave7-plan.png` (plan mode, 2+ draft waypoints blue vs amber vehicle mission, instruments live vs real ghost).

**Notes:** WaypointAdder's `client` prop unused (signature parity). Store barrel is the import convention ("components import from here") — re-exported planStore/mapMode there. Actions column is getting crowded (ActionsPanel + WaypointList + ParamTable share one scroll column) — layout debt for a future wave.

**Remaining milestone jobs (wave 8 candidates):** T1a/T1b/T1c Tauri shell + I6a/I6b bundle configs; W5b action confirm slider; W1b telemetry fixtures; G-lane QmlControls core/view split. Awaiting vegito's order before starting wave 8 (authorization covered only through wave 7).

## strangler-wave8-state.md

---
name: strangler-wave8-state
description: "Wave 8 done 2026-07-08 — Tauri shell compiles, confirm sliders, survey fixture, QML-OFF link diet (−5 shared libs, −11 archives); on strangler-wave8 (4 commits, NOT pushed)"
metadata: 
  node_type: memory
  type: project
  originSessionId: d73f5ca7-17e3-4db7-92b0-4f406367507b
---

Strangler Wave 8 completed 2026-07-08 on branch `strangler-wave8` (stacked on wave7; 4 commits 23ac69bc8..3281248a6, NOT pushed — [[one-branch-per-wave]]; waves 1–7 are all pushed as of this date). 4 parallel Sonnet subagents + integration.

**Landed:** T1c/I6a/b Tauri shell first compile (blockers were compile-time-validated assets: externalBin sidecar file + icons/icon.png; added icon set, scripts/link-ghost.sh, explicit appimage/deb/nsis bundle targets; `cargo tauri` CLI NOT installed on host, installers untested). W5b slide-to-confirm (pointer-capture drag ≥90%, hold-Space keyboard; replaced ActionsPanel's old 3s press-again state machine). W1b survey fixture — `MOCK_FIXTURE=1 bun mock/server.ts` replays a 120-sample Zurich survey flight + 6-item mission; default mode byte-identical, selftest 53/53 both ways. G-lane link diet: OFF binary dropped Qt6Graphs+Quick3D×3+ShaderTools (shared) and 11 QML resource-only static archives via pure CMake `if(QGC_ENABLE_QML)` gating; APM/PX4 VehicleConfig JSONs re-embedded via unconditional qt_add_resources (they rode in a gated qml_module RESOURCES clause but core reads them).

**Verified:** tsc clean, 104 bun tests, selftest 53/53 ×2 modes, ON+OFF Docker builds green, MockLink e2e probe passes on post-audit ON binary, OFF smoke 9s clean. Screenshot `/home/bpasu/rtn-wave8-fixture.png` (fixture flight ARMED/MISSION mid-survey, sliders visible).

**Gotcha:** `pkill -f "mock/server.ts"` from a Bash tool call kills the session's own shell (exit 144) — use the harness background-task kill or a pidfile instead.

**Structural leftovers for a future wave (from the audit):** (1) Qt6Location is ELF-NEEDed to the whole Qml/Quick family and is used everywhere via QGeoCoordinate — needs a lighter positioning dependency or a custom Qt build; (2) FactValueGrid/HorizontalFactValueGrid are QQuickItems used headlessly by SubtitleWriter — need a core/view split to finally drop Qt6Quick. Also: cargo tauri dev/build workflow untested (needs tauri-cli install + a display), actions-column layout debt persists.

## strangler-wave9-state.md

---
name: strangler-wave9-state
description: "Wave 9 done 2026-07-08 — Tauri full-chain live run + orphan fix, tabbed side panel, FactValueGridModel split, Location cut BLOCKED with precise audit; on strangler-wave9 (3 commits, NOT pushed)"
metadata: 
  node_type: memory
  type: project
  originSessionId: d73f5ca7-17e3-4db7-92b0-4f406367507b
---

Strangler Wave 9 completed 2026-07-08 on branch `strangler-wave9` (stacked on wave8; 3 commits adc49dbde..c8fb92e75, NOT pushed — [[one-branch-per-wave]]; waves 1–8 pushed).

**Landed:** (1) Tauri live run PROVEN — `cargo tauri dev` (tauri-cli 2.11.4 installed, ~44s) opened the window on a private Xvfb, docker-wrapper sidecar (`tauri/scripts/docker-ghost-sidecar.sh` — image binary needs Qt 6.10, not host-runnable) served the bridge on 8877, webview cockpit CONNECTED to vehicle 128; screenshot `/home/bpasu/rtn-wave9-tauri.png`. Run exposed an orphaned-sidecar bug → fixed both layers: lib.rs kills the CommandChild on RunEvent::ExitRequested/Exit + shutting_down flag stops the watchdog; wrapper runs the container detached with a setsid reaper subshell keyed to the script pid (survives SIGKILL, which CommandChild::kill sends). (2) Tabbed SidePanel FLY|PLAN|PARAMS with bidirectional tab⇄mapMode sync (pure helpers, tested). (3) FactValueGridModel split — base is QQuickItem under QGC_ENABLE_QML else QObject; view is a QML-only registration shell; SubtitleWriter/QGCCorePlugin/InstrumentValueData use the model; OFF build has zero QQuickItem symbols in it.

**Qt6Quick cut BLOCKED (audit, no code change):** QtLocation has genuinely headless consumers — `QGeoJson::importGeoJson` (GeoJsonHelper ← MissionManager) and tiled-terrain private types (TerrainTileManager/QtLocationPlugin ← vehicle TERRAIN protocol); libQt6Location ELF-NEEDs the whole Qml/Quick family. ALSO: Qt6::MultimediaQuickPrivate can NOT be link-gated — it provides `qquickvideooutput_p.h` which VideoManager.cc/VideoManager2.cc/QtMultimediaReceiver.cc/UVCReceiver.cc include unconditionally (I tried the one-line gate; OFF compile broke; reverted with an explanatory comment). Future-wave path: local GeoJSON parser (Positioning types only), TerrainTileManager tile-engine split off QGeoServiceProvider, QQuickVideoOutput usage split in VideoManager.

**Verified:** tsc clean, 109 bun tests, selftest 53/53 both modes (run with `MOCK_PORT=8876` when the fixture mock owns 8877 — selftest spawns its own server), ON+OFF builds green post-split, OFF smoke clean, live PLAN-tab screenshot `/home/bpasu/rtn-wave9-tabs.png`.

**Gotchas:** subagents that launch Docker builds via their own background Bash then end their turn never get woken — nudge them via SendMessage when their binaries appear. `cargo tauri build` installers still unexercised (needs appimagetool/linuxdeploy/makensis; docker-wrapper sidecar is dev-only).

## theme-ingress-state.md

---
name: theme-ingress-state
description: "feature/theme-ingress branch state — Ingress theme, two-step actions, chamfer ring technique, PUSHED 2026-07-13"
metadata: 
  node_type: memory
  type: project
  originSessionId: d73f5ca7-17e3-4db7-92b0-4f406367507b
---

`feature/theme-ingress` (branched from strangler-wave20) is PUSHED to origin as of 2026-07-13 (vegito: "เอาเลย"). Head: 995d06c66.

What's on it: 24 authentic Ingress tokens in app.css (see [[strangler-wave19-state]] for the wave baseline), arrow-thumb slide-to-confirm, two-step guided actions (buttons arm ONE confirm slider; gold pressed ring), dark-map filter compensation.

**Chamfer ring technique** (the load-bearing CSS lesson): elements with chamfered `clip-path` cannot use `border` (rectangle → amputated at every diagonal corner) or `box-shadow` (clipped away entirely — those glows silently never rendered). Two working replacements, both in the codebase:
- Opaque fills (actions.css `.actions-button`): `::after` floods the clipped silhouette with `--ring`, `::before` covers the interior with `--fill`; inner chamfer shrinks by `0.6 × --ring-w`. States restyle via `--ring`/`--ring-w`/`--fill`.
- Transparent/content-bearing frames (VehicleSelect chip, `.stage-slot--pip`, `.guided-strip`): single even-odd `clip-path: polygon(evenodd, outer…, o1, inner…, i1)` ring — repeat each loop's first vertex so the seam retraces itself. Glows = `filter: drop-shadow` (follows clip-path).

**Concurrent-editor gotcha**: vegito runs agy minions on the same working tree — files can change (and get committed, e.g. 63b8db357) between my reads. Check `git log`/`git status` before editing web CSS on this branch; reconcile their intent rather than reverting. An agy edit once added `position: relative` to `.stage-slot--pip`, overriding the base absolute and flinging the PiP to the top-left — watch for that class of regression.

Still open: merge verdict for feature/theme-ingress + [[strangler-wave20-state]]'s feature/semi3d-map into mainline; Enlightened-vs-Resistance accent question never answered.

## zero-qt-decision.md

---
name: zero-qt-decision
description: "vegito's target-state decision (final, 2026-07-09): Option A — ghost is a QtCore headless daemon; M7+M8 committed, M9 (zero-Qt) PARKED as future work"
metadata: 
  node_type: memory
  type: project
  originSessionId: d73f5ca7-17e3-4db7-92b0-4f406367507b
---

Decision history, 2026-07-09 (during wave 13):
1. Presented the fork — Option A: ghost stays a QtCore-family headless daemon (~6-10 non-GUI Qt libs as ordinary server deps, low effort, easy upstream sync) vs Option B: zero-Qt ghost (multi-month rewrite, permanent divergence). vegito first chose **Option B**.
2. He then set the sequencing rule: order de-Qt work by upstream-sync friction ascending; permanently-divergent work in the farthest milestone.
3. After learning Qt runtime packaging is already modular (per-lib apt Depends; the wave-12 ghost-bundle ships only the ~12 needed .so files — no suite), he REVISED to the final decision: **Option A is the target state. M7 (friction ① flag-gated cuts) + M8 (friction ② heavy-but-reconcilable) are the committed scope. M9 (friction ③: Positioning/GeoCoordinate replacement, moc-free object model, zero-Qt core) is PARKED as explicitly-optional future work** with an unpark checkpoint if ever revisited.

**Geo math plan (recorded for if/when Q9a unparks):** own API-identical GeoCoordinate value type; phase (a) match QGeoCoordinate behavior exactly (great-circle), pinned by tests generated from real Qt before removal; phase (b) opt-in GeographicLib geodesic precision (GeographicLib is already CPM-vendored in src/Utilities/Geo).

**Standing implications:** ghost work may use QtCore-family primitives freely (they're the accepted platform); still avoid GUI-family coupling. Upstream QGC sync remains a goal — M7/M8 stay additive/flag-gated per the friction rule. STRANGLER_MILESTONES.md records the full decision history + the M7-M9 tables with evidence.

Related: [[strangler-wave12-state]] (12-lib diet baseline), [[strangler-wave11-state]] (QML-free milestone).

**2026-07-11 addendum:** vegito asked about porting the ghost to Rust ("ส่วนที่ไม่ได้ใช้ Qt") — presented honestly as unparking M9 with Rust as the target (Qt object model is the DNA: 415 QObject classes) plus the credible incremental path (Rust ghost behind the PROTOCOL.md seam, proxy to C++ ghost, conformance-suite-gated per channel — "strangler ซ้อน strangler"; also smoother wasm story via wasm-bindgen). **vegito declined: "ยังไม่ไปทางนั้นแล้วกัน" — M9 stays parked, Rust variant recorded but not pursued.** The C++ QtCore daemon remains the committed architecture.

**Rust-in-Qt middle road (recorded 2026-07-11):** if Rust ever gets tried without unparking M9, the entry point is **cxx-qt** (KDAB, active, Qt6): write new QObjects in Rust with real signals/slots living inside the existing C++ ghost — per-module adoption, zero rewrite, no divergence. Alternatives surveyed: qmetaobject-rs (QML-focused), rust-qt/ritual (unmaintained), Slint (separate toolkit, not applicable).

**cxx-qt wholesale-migration assessment (2026-07-11, vegito asked):** converting the remaining 415 C++ classes via cxx-qt = worst-of-both-worlds — M9-scale effort PLUS permanent FFI/bridge tax, and the end state still carries QtCore + both toolchains (cxx-qt is designed to live WITH Qt, not leave it). Recommendation given: wholesale ❌; if full Rust is ever wanted → protocol-seam rewrite (same cost, Qt-free bridge-free end state); cxx-qt ✅ only selectively — new modules going forward and security-critical parse paths (MAVLink/camera-XML/GeoJSON handling untrusted vehicle input) where memory safety pays most. vegito has not ordered any Rust work; C++ daemon remains committed.
<!-- END vegito:claude-memory -->
