# แผนงาน M0–M6: Strangler Fig Port (QGC → Ghost Core + Tauri/Web)
## ฉบับ Ghost / AI Agents (Micro-Jobs ขนานสูงสุด)

> ยุทธศาสตร์: **additive-only + ปิด QML ด้วย build flag (ไม่ลบ)** เพื่อ merge upstream ได้ตลอด
> เอกสารนี้แตกงานเป็น Micro-Job สำหรับ AI Agents ("Ghosts") ขนาด XS (1-2 ไฟล์, เสร็จใน 1-2 prompt) / S (เพิ่มลอจิกเฉพาะส่วนย่อย)
> ออกแบบให้วิ่งขนานกันได้เป็น 10-20 agents พร้อมกัน โดยไม่มี conflict หรือ merge conflict

---

## สถานะจริง (Reality) — 12 waves shipped

> อัปเดต 2026-07-09 บนสาขา `strangler-wave12` (stacked: wave1 → wave12, ทุก branch push แล้วยกเว้น wave12 เอง —
> ดู [[one-branch-per-wave]]). เอกสารด้านล่างนี้เขียนไว้ตอน M0 ยังไม่เริ่ม; ของจริงไปไกลกว่าแผนเดิมมากแล้ว
> ทั้งความกว้าง (mission/param/plan-mode UI ครบ) และความลึก (QML ถูกถอดออกจาก ghost binary จริง ไม่ใช่แค่ compile flag)

| Wave | Branch | หัวข้อหลัก |
|------|--------|-----------|
| 1 | `strangler-wave1` | Scaffolding: Tauri v2 shell + sidecar watchdog, web frontend (Bun+React), `QGC_ENABLE_QML` flag + `QGCQmlCompat` shim, `--headless`/`--bridge-port` CLI flags, PROTOCOL.md v0.1, GuidedActionGate ported from FlyView QML |
| 2 | `strangler-wave2` | First QML-OFF boot: headless build skips `QQmlApplicationEngine`, `src/CMakeLists.txt` gates QML compilation, MapLibre CoreMap, WebCodecs H.264 decoder, Bun mock bridge server + scripted flight fixture, Zustand stores bound to the protocol |
| 3 | `strangler-wave3` | Live web UI + real C++ WS bridge: full cockpit shell assembled (session bootstrap + all panels), `WebBridgeServer` websocket listener, `TelemetryChannel`, VehicleLayer/VideoPlayer/Attitude/Status panels, bridge types reconciled against PROTOCOL.md |
| 4 | `strangler-wave4` | Video + command + param channels live: `VideoStreamServer` H.264 pipeline tap, binary video frames end-to-end, `CommandChannel` + `FactChannel` (get/set param, metadata min/max/type — job B10) with per-client routing, ParamTable + ActionsPanel UI, mock bridge simulates command lifecycle |
| 5 | `strangler-wave5` | Cockpit rides real ghosts: `QGC_ENABLE_MOCKLINK` embeds a simulated vehicle in non-Debug ghost builds, commandAck correlated with real `MAV_RESULT`, tick `vehicleIds` fix, §4 null-telemetry-field fix, DualCam + per-stream latency overlay, map click-to-goto |
| 6 | `strangler-wave6` | Mission channel + video attach: §7 `MissionChannel` upload/download/clear, `GhostVideoSource` bridge-owned GStreamer UDP attach, mission store + on-map MissionLayer, vehicle selector, tick-liveness watchdog + seq-gap resubscribe — all e2e-proven against a real MockLink ghost |
| 7 | `strangler-wave7` | Plan mode + instruments + bridge hardening: W8a/b mission plan mode (draft store, click-to-add, upload/clear panel), W9a FlyView instrument strip, W6b param-edit modal completed, B14a/b bridge localhost-bind override + auth token |
| 8 | `strangler-wave8` | Tauri shell first compile (T1c/I6a/I6b, installers untested), W5b slide-to-confirm guided actions, W1b recorded-survey mock fixture, QML-OFF link diet round 1 (drops Qt6Graphs/Quick3D/ShaderTools + 11 QML-only archives) |
| 9 | `strangler-wave9` | Tauri live run proven (`cargo tauri dev` opens the window, sidecar lifecycle bug fixed), tabbed side panel (FLY\|PLAN\|PARAMS), `FactValueGridModel` split out of its `QQuickItem` view — first core/view split; Qt6Location cut audited and found blocked (documented, not done) |
| 10 | `strangler-wave10` | Qt6Location cut: in-repo GeoJSON importer, `TerrainTileFetcher`, VideoManager `QQuickVideoOutput` gated, `QGCLocationCore`/QML plugin split — ghost drops to 22 Qt libs; **first real installers** (.deb + AppImage) via `cargo tauri build` |
| 11 | `strangler-wave11` | **Ghost goes QML-free**: UI-registration tier + video tier gated, `qgcSetCppOwnership` shim adopted repo-wide, root `qt_add_qml_module` gated, `Qt6::Quick` dropped from `src/CMakeLists.txt` — **16 Qt libs, zero `libQt6Qml*`/`libQt6Quick*`** (down from 27 pre-strangler); full MockLink + live-RTP video e2e green |
| 12 | `strangler-wave12` | Host-portable ghost bundle (Docker-free, `/home/bpasu/ghost-bundle/`, not in git), CI invariant gate `.github/workflows/ghost-strangler.yml` (OFF build + `objdump` `DT_NEEDED` assert + MockLink e2e probe + web checks), Qt diet round 2 — `QGC_ENABLE_BLUETOOTH`/`QGC_ENABLE_SENSORS`/`QGC_ENABLE_TEXTTOSPEECH` capability flags + Svg QML-gated → **12 Qt libs** with everything off; wave-13 wiring plan documented |

---

## กฎทองสำหรับ Ghost Agents (ทุก PR ต้องผ่าน)

1. **Atomic Files** — โค้ดใหม่อยู่ใน directory ใหม่ และทำงานจบในไฟล์เดียวให้มากที่สุด (เช่น `src/WebBridge/`, `web/`, `tauri/`)
2. **Flag, not delete** — QML ปิดด้วย `QGC_ENABLE_QML=OFF` ห้ามลบไฟล์ของ upstream
3. **Mock First** — ฝั่ง Web ต้องมี Mock Data ก่อนเสมอ AI ฝั่ง Web ต้องไม่รอ C++
4. **State-Driven** — UI ไม่เก็บ State เอง รอรับ Snapshot จาก C++ เท่านั้น

## 5 Lanes สำหรับ Agents

| Lane | ขอบเขต | ทักษะ (AI Persona) |
|------|--------|-------------------|
| **G** Ghost | carve C++ core ให้ headless ได้ | C++ / CMake Agent |
| **B** Bridge | `src/WebBridge/` — สร้าง WS server และ Channel ย่อย | C++ / Qt Agent |
| **W** Web | `web/` — UI Components พัฒนากับ **mock server** | TS / React Agent |
| **T** Tauri | `tauri/` — shell, sidecar lifecycle | Rust / Tauri Agent |
| **I** Infra | CI, benchmark, e2e testing | DevOps Agent |

---

## M0 — Ghost Core (`--headless` ไม่ลิงก์ Qt Quick)

แยกงานเป็นระดับแก้ทีละไฟล์ เพื่อให้ AI หลายตัวทำงานพร้อมกันได้โดยไม่ Git Conflict

| Job | งาน | ไฟล์/ขอบเขตเป้าหมาย | ขึ้นกับ | ขนาด | สถานะ |
|-----|-----|-------------------|--------|------|-------|
| G1 | เพิ่ม `option(QGC_ENABLE_QML "Enable QML UI" ON)` | `CMakeLists.txt` (root) | – | XS | ✅ Wave 1 |
| G2 | หุ้ม `qt_add_qml_module` และ Directory UI ด้วย `if(QGC_ENABLE_QML)` | `src/CMakeLists.txt` | G1 | S | ✅ Wave 2 |
| G3 | สร้าง shim header `qgcSetCppOwnership()` แบบ no-op เมื่อ OFF | `src/Utilities/QGCQmlCompat.h` | – | XS | ✅ Wave 1 (adoption completed repo-wide Wave 11) |
| G4a | `#ifdef` ปิด QML ใน MissionManager | `src/MissionManager/CameraCalc.cc`, `CameraSpec.cc` | G3 | XS | ✅ Wave 1–2 (initial sweep) |
| G4b | `#ifdef` ปิด QML ใน Comms | `src/Comm/LinkInterface.cc` | G3 | XS | ✅ Wave 1–2 (initial sweep) |
| G4c | `#ifdef` ปิด QML ใน Settings | `src/Settings/SettingsGroup.cc` | G3 | XS | ✅ Wave 1–2 (initial sweep) |
| G4d | `#ifdef` ปิด QML ใน Logging | `src/QGCLoggingCategoryManager.cc`, `LogManager.cc` | G3 | XS | ✅ Wave 1–2 (initial sweep) |
| G4e | `#ifdef` ปิด QML ใน Camera | `src/Camera/VehicleCameraControl.cc` | G3 | XS | ✅ Wave 1–2 (initial sweep) |
| G4f | `#ifdef` ปิด QML ใน API & Plugins | `src/api/QGCCorePlugin.cc` | G3 | XS | ✅ Wave 1–2 (initial sweep) |
| G5a | แยก build VehicleSetup (FirmwareUpgradeController) | `src/Vehicle/CMakeLists.txt` | G1 | XS | ✅ Wave 1 |
| G5b | แยก build Actuators (GeometryImage) | `src/Vehicle/CMakeLists.txt` หรือย้ายไฟล์ | G1 | XS | ✅ Wave 11 (QQuickImageProvider tier gated) |
| G5c | แยก build FactControls (FactPanelController) | `src/FactSystem/CMakeLists.txt` | G1 | XS | ✅ Wave 11 (migrated to `qgcSetCppOwnership` shim) |
| G6 | เพิ่ม flag `--headless` ลงใน parser | `src/Utilities/QGCCommandLineParser.cc` | – | XS | ✅ Wave 1 |
| G7 | ข้าม `QQmlApplicationEngine` ในโหมด headless | `src/QGCApplication.cc` (~L275) | G6 | S | ✅ Wave 2 |
| G8 | Linker Sweep: แก้ Linker Error ทีละตัว จนกว่าจะ build ผ่าน | ทั่วไป | G4*, G5* | S | ✅ Wave 2 (first green OFF build); diet continued through Wave 8, 10, 11, 12 (27 → 16 → 12 Qt libs) |
| I1 | เพิ่ม GitHub Action job `build-headless` | `.github/workflows/headless.yml` | G8 | XS | ✅ Wave 12, shipped as `.github/workflows/ghost-strangler.yml` (OFF build + `objdump` DT_NEEDED assert + MockLink e2e + web checks) — path differs from plan |

**การทำงานขนาน:** G3, G4a-G4f, G5a-G5c, G6 สามารถโยนให้ Agent 10 ตัวทำงานพร้อมกันได้เลย! *(ของจริง: ทำครบตามแผนนี้ตั้งแต่ Wave 1–2 ส่วน G5b/G5c ที่เหลือ ทำเสร็จช้ากว่าที่วางแผนไว้มาก — ไปเสร็จที่ Wave 11 ตอนถอด QML ออกจริงจัง)*

---

## M1 — WebSocket Bridge + Mock (ปลดล็อก UI)

| Job | งาน | ขอบเขตเป้าหมาย | ขึ้นกับ | ขนาด | สถานะ |
|-----|-----|----------------|--------|------|-------|
| B0 | เขียน schema `PROTOCOL.md` (JSON structure) | `src/WebBridge/PROTOCOL.md` | – | S | ✅ Wave 1 (now 13 sections: transport/auth, telemetry, command, fact, mission, adsb, video, error envelope, heartbeat, mock conformance, versioning) |
| B1a | สร้าง WebBridge class พื้นฐาน | `src/WebBridge/WebBridge.{h,cc}` | B0 | S | ✅ Wave 2 |
| B1b | เพิ่ม WebSocket Server รับ connection | `src/WebBridge/WebBridgeServer.{h,cc}` | B1a | S | ✅ Wave 3 |
| B2a | สร้าง TelemetryChannel พื้นฐาน | `src/WebBridge/TelemetryChannel.{h,cc}` | B1a | XS | ✅ Wave 3 |
| B2b | เชื่อม Fact system เข้า TelemetryChannel | `src/WebBridge/TelemetryChannel.cc` | B2a | S | ✅ Wave 3 (landed together with B2a) |
| W0a | โครงโปรเจกต์ React + Bun | `web/package.json` | – | XS | ✅ Wave 1 — **correction**: Bun's native HTML-import bundler, not Vite (plan text said Vite/Bun; repo uses `Bun.serve`/`bun build`, no Vite dep) |
| W0b | สร้าง Zustand state store อิง `PROTOCOL.md` | `web/src/store/` | B0, W0a | S | ✅ Wave 2 |
| W1a | สร้าง mock WS server ด้วย Bun | `web/mock/server.ts` | B0, W0a | S | ✅ Wave 2 |
| W1b | เขียน Fixture Data จำลอง Telemetry | `web/mock/fixtures/telemetry.json` | W1a | XS | ✅ Wave 2 (basic fixture) → ✅ Wave 8 (recorded 120-sample Zurich survey flight + 6-item mission, opt-in `MOCK_FIXTURE=1`, path also includes `telemetry-survey.json`/`mission-survey.json`/`generate-survey.ts`) |
| W2a | UI: Telemetry Panel (Attitude) | `web/src/components/telemetry/Attitude.tsx` | W0b | XS | ✅ Wave 3 |
| W2b | UI: Telemetry Panel (Battery, GPS) | `web/src/components/telemetry/Status.tsx` | W0b | XS | ✅ Wave 3 |

---

## M2 — Video Plane (WebCodecs)

แยก C++ ฝั่งดึงภาพออกจากฝั่ง UI ฝั่ง UI สามารถใช้ไฟล์ MP4 ดิบ mock ไปก่อนได้

| Job | งาน | ขอบเขตเป้าหมาย | ขึ้นกับ | ขนาด | สถานะ |
|-----|-----|----------------|--------|------|-------|
| B5a | GStreamer tap pipeline สำหรับ H.264 | `src/WebBridge/VideoStreamServer.cc` | B1a | S | ✅ Wave 4 |
| B5b | ส่ง raw binary frame ลง WebSocket | `src/WebBridge/VideoStreamServer.cc` | B5a | S | ✅ Wave 4 (binary frames end-to-end + §9 protocol type extensions) |
| W3a | WebCodecs API: Setup VideoDecoder | `web/src/video/Decoder.ts` | W0a | S | ✅ Wave 2 |
| W3b | Render Canvas สำหรับวิดีโอ 1 กล้อง | `web/src/components/video/VideoPlayer.tsx`| W3a | S | ✅ Wave 3 |
| W4a | UI: Dual-cam layout (Side-by-side) | `web/src/components/video/DualCam.tsx` | W3b | XS | ✅ Wave 5 |
| W4b | UI: Video latency overlay | `web/src/components/video/Overlay.tsx` | W4a | XS | ✅ Wave 5 (landed with W4a) |

> Bridge-side follow-on not in the original plan: `GhostVideoSource.{h,cc}` (Wave 5–6) — bridge-owned `GstVideoReceiver`
> attach on `streamingChanged(true)`, driven by `Video/videoSource` + `Video/udpUrl` settings, with 5s→30s reconnect backoff.

---

## M3 — Tauri Shell + Commands

| Job | งาน | ขอบเขตเป้าหมาย | ขึ้นกับ | ขนาด | สถานะ |
|-----|-----|----------------|--------|------|-------|
| T1a | Init Tauri v2 project | `tauri/tauri.conf.json` | – | XS | ✅ Wave 1 |
| T1b | สร้าง Dummy Sidecar Script (Bash/Bat) | `tauri/scripts/dummy_ghost.sh` | – | XS | ✅ Wave 1 — **correction**: shipped at `tauri/dummy-ghost/dummy_ghost.sh`, superseded by `tauri/scripts/docker-ghost-sidecar.sh` (Wave 9, dev-only) and `tauri/scripts/link-ghost.sh` (Wave 8) |
| T1c | Tauri setup Sidecar spawn logic | `tauri/src-tauri/src/main.rs` | T1a, T1b | S | ✅ Wave 8 first compile (`src-tauri/src/lib.rs`, not `main.rs`); ✅ Wave 9 live-run proven (`cargo tauri dev`, orphaned-sidecar bug fixed) |
| B7a | CommandChannel: Arm/Disarm | `src/WebBridge/CommandChannel.cc` | B1a | XS | ✅ Wave 4 |
| B7b | CommandChannel: Takeoff/Land/RTL | `src/WebBridge/CommandChannel.cc` | B7a | XS | ✅ Wave 4 (landed together with B7a, single commit e4ee7107a) |
| B8a | Port `GuidedActionGate` (Preconditions) | `src/WebBridge/GuidedActionGate.{h,cc}`| – | S | ✅ Wave 1 |
| W5a | UI: Command Buttons (Arm, RTL, etc.) | `web/src/components/guided/Buttons.tsx`| W0b | XS | ✅ Wave 4 — **correction**: no standalone `Buttons.tsx` was built; command buttons live in `web/src/components/actions/ActionsPanel.tsx` alongside command-lifecycle UX (`useCommandRequests.ts`) |
| W5b | UI: Action Confirm Slider | `web/src/components/guided/Slider.tsx` | W0b | XS | ✅ Wave 8 (slide-to-confirm, pointer-capture drag ≥90% or hold-Space; path as planned, plus `sliderLogic.ts`) |

---

## M4 — Fact/Parameter Bridge + Map

| Job | งาน | ขอบเขตเป้าหมาย | ขึ้นกับ | ขนาด | สถานะ |
|-----|-----|----------------|--------|------|-------|
| B9 | FactChannel: Get/Set Parameters | `src/WebBridge/FactChannel.{h,cc}` | B1a | S | ✅ Wave 4 |
| B10 | ส่ง FactMetaData (min, max, type) | `src/WebBridge/FactChannel.cc` | B9 | XS | ✅ Wave 4 (landed together with B9, same commit e4ee7107a — comment in code literally tags itself "Task B10") |
| W6a | UI: Parameter Browser Table | `web/src/components/params/ParamTable.tsx`| W0b | S | ✅ Wave 4 |
| W6b | UI: Parameter Edit Modal | `web/src/components/params/ParamEdit.tsx` | W6a | XS | ✅ Wave 4 (initial) → ✅ Wave 7 (completed: `validateParamValue` pure helper, id-matched setParam round-trip, 5s timeout) |
| W7a | MapLibre: Init Map | `web/src/components/map/CoreMap.tsx` | W0a | S | ✅ Wave 2 |
| W7b | MapLibre: Vehicle Marker & Trail | `web/src/components/map/VehicleLayer.tsx`| W7a | XS | ✅ Wave 3 |

---

## M5 — Mission + FlyView Parity

| Job | งาน | ขอบเขตเป้าหมาย | ขึ้นกับ | ขนาด | สถานะ |
|-----|-----|----------------|--------|------|-------|
| B11a| MissionChannel: Upload | `src/WebBridge/MissionChannel.cc` | B1a | S | ✅ Wave 6 |
| B11b| MissionChannel: Download | `src/WebBridge/MissionChannel.cc` | B11a| XS | ✅ Wave 6 (landed together with B11a, plus `clear` — one commit df2b2b3b8 covers the whole §7 channel) |
| W8a | UI: Map Click to Add Waypoint | `web/src/components/plan/WaypointAdder.tsx`| W7a | S | ✅ Wave 7 (path as planned) |
| W8b | UI: Waypoint List Panel | `web/src/components/plan/WaypointList.tsx` | W0b | S | ✅ Wave 7 (path as planned; landed together with W8a, one commit 8019a08ff) |
| W9a | UI: FlyView Instrument Panel | `web/src/components/flyview/Instruments.tsx`| W2a | S | ✅ Wave 7 (path as planned) |

---

## M6 — Hardening + Upstream Sync

| Job | งาน | ขอบเขตเป้าหมาย | ขึ้นกับ | ขนาด | สถานะ |
|-----|-----|----------------|--------|------|-------|
| I6a | Tauri Windows Bundle Config | `tauri/tauri.conf.json` | T1c | XS | ⏳ partial — bundle target configured Wave 8, but **NSIS/.exe build is still untested** as of Wave 12 (no Windows host exercised) |
| I6b | Tauri Linux Bundle Config | `tauri/tauri.conf.json` | T1c | XS | ✅ Wave 8 (config) → ✅ Wave 10 (proven: first real `.deb` + `.AppImage` produced via `cargo tauri build`) |
| B14a| Security: WebBridge Bind Localhost Only| `src/WebBridge/WebBridgeServer.cc` | B1b | XS | ✅ Wave 7 (`--bridge-host`, localhost-default preserved) |
| B14b| Security: Auth Token Exchange | `src/WebBridge/WebBridgeServer.cc` | B14a| S | ✅ Wave 7 (`--bridge-token`; wrong token → `AUTH_FAILED` + close; unset = unchanged; token redacted from logs) |

**Open (never in the original plan, found by later-wave audits):** signing/release pipeline, aarch64 build, `strip`/RPATH hardening of the ghost binary — see "Wave 13+" below.

---

## สรุป Wave สำหรับการปล่อย Agent แบบ Swarm

**Day 1 (Swarm 1: แกะโค้ด C++ & ตั้งโครง)**
- ปล่อย C++ Agents ลุย **G1, G3, G4a, G4b, G4c, G4d, G4e, G4f, G5a, G5b, G5c, G6, B0, B8a** พร้อมกัน (14 Agents)
- ปล่อย Web Agents ลุย **W0a, T1a, T1b** (3 Agents)

**Day 2 (Swarm 2: เชื่อมต่อ C++ และ Web Mock)**
- ปล่อย C++ Agents ลุย **G2, G7, B1a**
- ปล่อย Web Agents ลุย **W0b, W1a, W3a, W7a**

**Day 3+ (Swarm 3: สร้างฟีเจอร์ย่อยแบบกระจายตัว)**
- ปล่อย C++ Agents ลุย **B**-jobs ทั้งหมด (ไฟล์แยกกัน ไม่ชน)
- ปล่อย Web Agents ลุย **W**-jobs ที่เหลือ (Components แยกย่อย ไฟล์ใครไฟล์มัน)

---

## Wave 13+ (candidates, not commitments)

> M0–M6 above are effectively done — all G/B/W/T/I jobs from the original plan have landed (see status
> columns) except the two Windows/aarch64 gaps called out in M6. This section lists what's actually queued
> up or being discussed for the next waves, per the wave-12 close-out and its wave-13 wiring doc
> (`4b2b5248d docs(tauri): wave-13 wiring plan for the host-portable ghost bundle`). Nothing here is
> promised — these are candidates pending vegito's order, same as every prior wave.

**In flight / immediately next:**

- **Native-sidecar installers + clean-container proof.** Wave 12 built a Docker-free, host-portable ghost
  bundle (`/home/bpasu/ghost-bundle/`, 112M — Qt 6.10 + ICU closure + plugins + launcher; GStreamer/GLib/X11
  resolve from the host since it matches the build image's Ubuntu 24.04 package versions). Wave 13's job is
  to wire that bundle into Tauri for real: ship `lib/`+`plugins/` as `bundle.resources`, ship
  `bin/QGroundControl` as the `externalBin` sidecar (renamed `ghost-<triple>`), and set
  `LD_LIBRARY_PATH`/`QT_PLUGIN_PATH`/`QT_QPA_PLATFORM=offscreen` via `Command::sidecar(...).env(...)` in
  `tauri/src-tauri/src/lib.rs` instead of the current `docker-ghost-sidecar.sh` dev-only wrapper. Proof target:
  a clean container (no build image, no repo) that runs the shipped bundle standalone.
- **Notification channel: bridge → browser speech.** Wave 12's TTS decision (`QGC_ENABLE_TEXTTOSPEECH`
  degrades `AudioOutput` to a silent no-op singleton headless) comes with a recommendation, adopted but not
  yet built: route `AudioOutput::say()` through the WebBridge as a notification message so the *browser*
  speaks (Web Speech API), rather than trying to make the ghost process itself produce audio.

**Open items (no evidence of work started):**

- Signing/release pipeline for Tauri bundles (installers today are unsigned, built ad hoc on a dev host).
- aarch64 ghost build (x86_64 only through Wave 12; no aarch64 attempt on record).
- NSIS/Windows installer — bundle target exists in `tauri.conf.json` since Wave 8 but has never been built
  or run on a Windows host.
- Further TTS routing follow-ons once the browser-speech notification channel above lands (e.g. per-message
  priority/interrupt semantics — unscoped).
- Multimedia/Gui structural audit: `Qt6::Multimedia` and `Qt6::Gui` remain unaudited in the wave-11/12 Qt-diet
  passes (Sql/StateMachine/Concurrent were explicitly classified as core-and-kept; DBus is Qt-transitive and
  uncuttable from QGC's CMake as-is; Multimedia/Gui were not reached).
- Real-vehicle (non-mock) validation — all e2e proof through Wave 12 runs against `QGC_ENABLE_MOCKLINK`'s
  simulated vehicle; no real autopilot/hardware-in-the-loop run is on record.
- Param/mission UI polish — wave 7's actions-column layout debt (ActionsPanel + WaypointList + ParamTable
  sharing one scroll column) is still open as of Wave 12.

> Beyond Wave 13, the long arc is the **M7+ De-Qt Roadmap** below — the phase M0–M6 never planned for:
> shrinking the ghost from ~11 Qt libs to the Option A daemon target (committed scope M7+M8; the zero-Qt
> M9 tier is parked). (The "Multimedia/Gui structural audit" open item above is folded into it as Q7a/Q7d.)

---

# M7+ De-Qt Roadmap (เฟสใหม่ — เพิ่ม 2026-07-09)

> M0–M6 วางแผนแค่ "ปิด QML" — ไม่เคยวางแผน "เอา Qt ออกทั้งหมด" เฟสนี้เติมช่องว่างนั้น
> **Baseline วันนี้:** diet OFF ghost (wave-13 in-flight build, วัดจาก `ldd` จริง — scratchpad `ldd-w13notif-off.txt`)
> ลิงก์ Qt **11 ตัว**: Core, DBus, Gui, Multimedia, Network, Positioning, SerialPort, Sql, StateMachine, WebSockets, Xml
> (Concurrent หลุดไปแล้วระหว่าง wave 13; TextToSpeech/Bluetooth/Sensors/Svg หลุดตั้งแต่ wave 12 ด้วย capability flags)
>
> **การตัดสินใจ (final, 2026-07-09): Option A — ghost เป็น QtCore-family headless daemon.**
> committed scope = **M7 + M8**; ชั้น M9 (zero-Qt) **PARKED** เป็น future work — ไม่อยู่ใน committed path
> (ประวัติการตัดสินใจเต็ม ๆ รวมทั้งช่วงที่เคยเลือก Option B แล้ว revise อยู่ใน Endgame ด้านล่าง —
> เหตุผลที่ revise: Qt runtime deps เป็น modular per-lib packages อยู่แล้ว การถือ non-GUI Qt libs
> ~2–6 ตัวเป็น server dependency ปกติจึงไม่มี packaging cost ที่ต้องหนี)
>
> **หลักการเรียงลำดับ (ข้อบังคับเพิ่มเติมจาก vegito):** ทุก milestone เรียงตาม **upstream-sync friction
> จากน้อยไปมาก** — งานที่ diverge ถาวรต้องอยู่ท้ายสุดของ roadmap เสมอ ทุก job มีระดับ friction กำกับ:
> **①** = ทำแบบ additive/flag-gated ในไฟล์ shared ได้ (pattern `#ifdef`/`option()` เดิม — merge upstream ยังง่าย)
> **②** = rewrite ไฟล์ upstream-shared หนัก แต่ยัง reconcile กับ upstream ได้ในหลักการ
> **③** = diverge ถาวร (point of no return) — ข้ามเส้นนี้แล้ว rebase upstream ของ ghost core ไม่ได้อีก

## M7 — Qt Shrink: friction ① (additive/flag-gated — upstream merge ยังง่าย)

ตัดทีละ lib แบบเดียวกับ audit ของ wave 9–12: นับ include/link site จริงก่อน แล้วค่อยตัด
ตัวเลข evidence ด้านล่างมาจาก grep บน tree ปัจจุบัน (strangler-wave12) — ไม่ใช่การเดา
ทุก job ใน M7 ใช้ pattern `#ifdef`/`option()`/QML-gate ที่พิสูจน์แล้ว 12 waves หรือแตะเฉพาะไฟล์ของเราเอง

| Job | งาน | Evidence (นับจริง) | Friction | ขนาด | Qt lib ที่หายไป |
|-----|-----|--------------------|----------|------|----------------|
| Q7a | **Multimedia**: ghost วิ่งบน raw GStreamer ล้วน — gate `QtMultimediaReceiver`/`UVCReceiver` (backend ทางเลือกที่ headless ไม่ใช้), gate `GstAppSinkAdapter` (แปลง GstBuffer → `QVideoFrame`/`QVideoFrameFormat` เพื่อ render เข้า Qt UI เท่านั้น — path ของ ghost คือ `GstVideoReceiver` → `VideoStreamServer` H.264 tap ไม่แตะ `QVideoSink`), gate ลิงก์ `Qt6::Multimedia` ที่ `src/CMakeLists.txt:163` + `VideoManager/VideoReceiver/GStreamer/CMakeLists.txt:38` | ผู้ใช้ QtMultimedia = 16 ไฟล์; ส่วน HwBuffers (`MultimediaPrivate`) compile out headless แล้วตั้งแต่ wave 11; `GhostVideoSource.{h,cc}` เองสะอาด — include แค่ QtCore + `GstVideoReceiver`; wave-12 bundle พิสูจน์แล้วว่า ffmpegmediaplugin เป็นของ INERT ใน ghost (dlopen fail แบบ harmless) | ① | S–M | `libQt6Multimedia` (+ FFmpeg media plugin ออกจาก bundle) |
| Q7b | **Sql**: terrain tile cache เป็นผู้ใช้เดียว — เพิ่ม mode flag: no-cache (re-fetch terrain) หรือ flat-file cache สำหรับ ghost | ผู้ใช้ QSql = **4 ไฟล์** เท่านั้น: `QtLocationPlugin/QGCTileCacheDatabase.h`, `QGCTileCacheWorker.h`, `Utilities/Database/QGCSqlHelper.{h,cc}` — wave-12 audit จัดเป็น "core (terrain tile cache)" แต่ ghost ยอมแลก cache persistence ได้ | ① | S (no-cache flag) / M (flat-file cache) | `libQt6Sql` + sqldrivers plugin ออกจาก bundle |
| Q7c | **Xml ครึ่งแรก — QML-gate ฝั่ง KML**: `Utilities/Geo/Formats/KML*`, `MissionManager/KMLPlanDomDocument`, `QmlControls/QGCMapPolygon` เป็น plan-file import/export ฝั่ง UI → gate ออกจาก headless ได้เลย (lib ยังไม่หลุดจนกว่า Q8c จะจัดการผู้ใช้ headless ตัวสุดท้าย) | `QDomDocument` = **6 ไฟล์**: 5 ตัวเป็น KML/UI (gate ได้), 1 ตัว headless-จำเป็น (`Camera/VehicleCameraControl.cc` — ไป Q8c) | ① | XS–S | – (ปลดล็อก Q8c) |
| Q7d | **Gui → Core (ทาง gate)**: `QGCApplication : public QGuiApplication` (`src/QGCApplication.h:50`) → `#ifdef` ให้ ghost สืบทอด `QCoreApplication` — pattern เดียวกับที่ใช้มา 12 waves; gate `QFontDatabase` font loading + `setWindowIcon(QIcon)` ใน `QGCApplication.cc` (UI-only ชัดเจน) และ gate site ที่ใช้ `QImage` headless (`MAVLink/ImageProtocolManager`, `Vehicle.cc`) — **ยอมเสีย MAVLink image protocol ใน ghost ชั่วคราว** จนกว่า Q8d จะเขียน raw-bytes path คืน | ผู้ใช้ `QGuiApplication` ตรง ๆ = **10 ไฟล์** (ส่วนใหญ่ QML/video/HwBuffers ที่ gate แล้วตั้งแต่ wave 11); `QImage` = **10 ไฟล์** (headless-จำเป็นจริง 2: `ImageProtocolManager`, `Vehicle.cc`); โบนัสใหญ่: ghost ไม่ต้องมี QPA platform plugin เลย (offscreen ก็ไม่ต้อง) → bundle เล็กลง และ **`libQt6DBus` หลุดตามอัตโนมัติ** (DBus เป็น Qt-transitive ผ่าน Gui — wave-12 audit ยืนยันว่าตัดตรง ๆ จาก QGC CMake ไม่ได้) — *caveat:* ต้องพิสูจน์ด้วย build จริงแบบ wave-9 audit; ถ้าเจอ blocker ชนิด MultimediaQuickPrivate ให้ตกชั้นไป ② | ① (ต้องพิสูจน์) | M | `libQt6Gui` + `libQt6DBus` + platform plugins ออกจาก bundle |
| Q7e | **WebSockets**: `QWebSocketServer`/`QWebSocket` → C++ WS lib (เช่น uWebSockets / Boost.Beast) — แตะเฉพาะ `src/WebBridge/` ซึ่งเป็นโค้ดของเราเองล้วน ไม่มีไฟล์ upstream เลย (จึงเป็น friction ① ทั้งที่เป็นการ swap dependency); ระวังเรื่อง thread/event-loop bridge ระหว่าง WS lib กับ Qt event loop ที่ยังอยู่ | `WebBridgeServer.{h,cc}` + channel classes — 100% additive-era code ของ strangler เอง | ① (ไฟล์เราเอง) | M | `libQt6WebSockets` |

**ผล M7 เต็ม:** 11 → **6 libs** — Core, Network, Positioning, SerialPort, StateMachine, Xml
ทุก job แยก directory กัน → ปล่อยขนานได้ตามสูตร swarm เดิม

## M8 — De-Qt heavy tier: friction ② (rewrite ไฟล์ shared หนัก แต่ยัง reconcile ได้ในหลักการ)

ชั้นนี้เริ่มเขียนทับ implementation ในไฟล์ upstream-shared — diff ใหญ่ merge upstream เริ่มเจ็บ
แต่ยังไม่ถึงจุด point of no return: โครงสร้าง type/signature ยังตรงกับ upstream พอ reconcile ได้

| Job | งาน | Evidence (นับจริง) | Friction | ขนาด | Qt lib ที่หายไป |
|-----|-----|--------------------|----------|------|----------------|
| Q8a | **StateMachine (audit ก่อน)**: นับ API surface ของ `QStateMachine` ที่ใช้จริง (hierarchy, history/parallel states, signal transitions, event posting) แล้วออกแบบ minimal replacement | framework = `src/Utilities/StateMachine/` **90 ไฟล์** (`QGCStateMachine : public QStateMachine` + states/transitions/helpers/profiler/logger); consumer นอก dir = **11 ไฟล์**: `Vehicle.{h,cc}`, `InitialConnectStateMachine`, `ComponentInformationManager`, `RequestMetaDataTypeStateMachine`, `ParameterManager.cc` และ **`WebBridge/MissionChannel.{h,cc}` ของเราเอง** — wave-12 audit จัดเป็น core (vehicle connect flow) | ② prep | S (audit only) | – |
| Q8b | **StateMachine (port)**: reimplement `QGCStateMachine` บน QObject/std ตาม surface ที่ Q8a นับได้ แล้ว migrate consumer 11 ไฟล์ — ห้ามเริ่มก่อน audit เสร็จ | 90+11 ไฟล์ตามแถวบน; ความเสี่ยง: `InitialConnectStateMachine` คือ flow เชื่อมต่อ vehicle — พังแล้ว ghost ใช้ไม่ได้เลย ต้องมี MockLink e2e ครอบทุกขั้น | ② | L | `libQt6StateMachine` |
| Q8c | **Xml ครึ่งหลัง**: เขียน parse camera definition XML ใน `Camera/VehicleCameraControl.cc` ใหม่ด้วย `QXmlStreamReader` (อยู่ใน QtCore ไม่ใช่ QtXml) — rewrite เฉพาะจุด ไฟล์เดียว reconcile ง่าย | 1 ไฟล์ (`VehicleCameraControl.cc:888` เป็น parse site เดียว); `QXmlStream*` ใช้อยู่แล้ว 2 ไฟล์ใน repo เป็นแบบอย่าง; link site `Qt6::Xml` 2 จุด: `src/CMakeLists.txt:140`, `Utilities/Geo/Formats/CMakeLists.txt:57` (จุดหลัง gate ไปแล้วใน Q7c) | ② | S | `libQt6Xml` |
| Q8d | **ImageProtocolManager raw-bytes**: คืน MAVLink image protocol ให้ ghost โดยไม่ใช้ `QImage` — forward encoded bytes ผ่าน bridge ให้ browser decode เอง (ทางเดียวกับ video §9) | `MAVLink/ImageProtocolManager.{h,cc}` (2 ไฟล์) — capability ที่ Q7d gate ทิ้งไว้ | ② | S | – (คืน capability) |
| Q8e | **SerialPort**: `QSerialPort` → serial lib (เช่น libserialport) ใน link layer | `src/Comms/` serial link classes — ไฟล์ upstream-shared แต่ผิว API แคบ (open/read/write/close + config) | ② | M | `libQt6SerialPort` |
| Q8f | **Network**: `QNetworkAccessManager` (terrain fetch, downloads) + `QPasswordDigestor` (MAVLink signing) + UDP/TCP links → curl/cpr + std sockets หรือ asio | `src/Comms/` UDP/TCP links, `src/Terrain*`, `src/MAVLink/` signing — กระจายกว่า Q8e มาก; ทำท้ายสุดของ M8 เพราะ friction สูงสุดในชั้น ② | ② (ขอบ ③) | L | `libQt6Network` |

**ผล M8 เต็ม:** ghost เหลือ **2 libs** — Core, Positioning — **= daemon target ของ Option A ถึงแล้ว
(จบ committed path ที่ตรงนี้)**
ลำดับใน M8: Q8c ∥ Q8d ∥ Q8a ก่อน (เล็ก ขนานได้) → Q8b → Q8e → Q8f

## M9 — จุดที่ diverge ถาวร: friction ③ (point of no return) — **PARKED: future work, ไม่อยู่ใน committed path**

> **สถานะ: PARKED (vegito, 2026-07-09 — final revision).** ชั้นนี้ถูกถอดออกจาก committed scope
> เก็บเนื้อหาไว้ทั้งหมด (evidence + slicing ยังมีค่าถ้าวันหนึ่ง unpark) — **เงื่อนไข unpark:**
> M7+M8 จบ + e2e เขียวทั้งหมด + คำสั่ง unpark ชัดเจนจาก vegito อีกครั้ง เพราะข้ามเส้นนี้แล้ว
> rebase upstream ของ ghost core ไม่ได้อีก — ถาวร ทุกอย่างก่อนหน้ายัง reconcile กับ upstream
> ได้ในหลักการ; ชั้นนี้คือการเปลี่ยน type system + runtime ของโค้ดทั้งก้อน

จุดตั้งต้นที่ดี: **ตัว MAVLink C library เอง Qt-free อยู่แล้ว** (generated C headers, fetch แยกใน
`src/MAVLink/CMakeLists.txt`) แต่ wrapper layer ของ QGC ใน `src/MAVLink/` ใช้ QtCore หนัก
(**21 ไฟล์** include Qt) และทุก manager class (Vehicle/Link/Param/Mission/WebBridge) เป็น
QObject + signals/slots ทั้งหมด

**Coupling-depth ที่วัดจริง (grep บน `src/`, .h/.cc) — ตัวเลขชุดนี้คือเหตุผลที่ชั้นนี้ต้องอยู่ท้ายสุด:**

| Coupling | นับได้ |
|----------|--------|
| `QGeoCoordinate` | **152 ไฟล์** (ทุกชนิดไฟล์ใต้ `src/`) |
| `Q_OBJECT` macro | **~482 จุด** (คลาส QObject ทั้ง tree) |
| `signals:` / `emit` | **440 ไฟล์** |
| `QString` | **763 ไฟล์** |
| `QTimer` | **156 ไฟล์** |

| Job | งาน | ขอบเขต | ขึ้นกับ | Friction | ขนาด | Qt lib ที่หายไป |
|-----|-----|--------|--------|----------|------|----------------|
| Q9a | **Positioning**: geo type ของตัวเองแทน `QGeoCoordinate` — **ยุทธศาสตร์ที่ตกลงกับ vegito (2026-07-09 — ยืนตามนี้เมื่อไหร่ก็ตามที่ Q9a ถูก unpark ให้วิ่งจริง):** (1) เขียน `GeoCoordinate` value type บาง ๆ ของเราเอง **API เหมือน `QGeoCoordinate` เป๊ะโดยตั้งใจ** เพื่อให้การแทนที่ 152 ไฟล์เป็นงาน mechanical (sed-able); (2) geodesic math ใช้ **GeographicLib ที่ vendor ไว้แล้ว** ใน `src/Utilities/Geo` ผ่าน CPM (`CPMAddPackage(geographiclib)` — `QGCGeo.cc` include `GeographicLib/Geodesic.hpp` อยู่วันนี้) → **ไม่มี dependency ใหม่เลย**; (3) *phase a:* จำลอง great-circle behavior ของ Qt ให้ตรงเป๊ะ pin ด้วย unit tests ที่ expected values **generate จาก `QGeoCoordinate` จริงก่อนถอดออก** (กัน silent drift), *phase b:* เปิด opt-in ความแม่น geodesic ของ GeographicLib ทีหลัง — เปลี่ยน type ในไฟล์ upstream เป็นร้อยยังไงก็ diverge ถาวรโดยนิยาม แม้ replacement จะ mechanical | `QGeoCoordinate` = **152 ไฟล์** ใต้ `src/` (**87 ไฟล์** ใน core headless dirs: Vehicle, MissionManager, WebBridge, FirmwarePlugin, Comms, PositionManager, Terrain); type อื่นผิวเล็ก: `QGeoPositionInfo` ×4, `QGeoPositionInfoSource` ×2, `QGeoPolygon` ×2, `QGeoPath` ×2 | M8 จบ | ③ | L–XL (ลดลงจาก API-identical strategy) | `libQt6Positioning` + position plugins ออกจาก bundle |
| Q9b | **Object model + event loop**: QObject/moc/signals-slots → non-Qt (std + event loop เช่น Boost.Asio; signal → std::function/observer pattern) — long pole ของทั้ง roadmap | ทุก class ใน ghost core — เริ่มจาก layer ล่างสุด (MAVLink wrappers) ไล่ขึ้นบน (Vehicle/Managers/WebBridge channels) | M8 จบ (ขนานกับ Q9a ได้บาง layer) | ③ | XL (หน่วยเป็น**เดือน** ไม่ใช่ wave) | – (ปลดล็อก Q9c) |
| Q9c | **Core ปิดท้าย**: QString/QByteArray/QJson*/QTimer/QSettings ที่เหลือทั้งหมด → std / nlohmann-json ฯลฯ | ทั้ง ghost core | Q9a + Q9b | ③ | XL | `libQt6Core` → **zero-Qt** |

## Endgame — บันทึกการตัดสินใจ (Target State Decision)

> **ประวัติการตัดสินใจ (เก็บทั้งสองรายการไว้ตรง ๆ — เส้นทางการคิดมีค่าเท่าผลลัพธ์):**
>
> 1. **2026-07-09: เลือก Option B (zero-Qt ghost)** — สั่งเดินเต็ม M7 → M8 → M9 โดยรับ tradeoff
>    diverge-ถาวร พร้อมข้อบังคับ sequencing เรียงตาม friction ①→②→③ (งาน diverge-ถาวรท้ายสุดเสมอ)
> 2. **2026-07-09 (ภายหลัง — หลังได้ข้อเท็จจริงว่า Qt runtime deps เป็น modular per-lib packages
>    อยู่แล้ว ทั้งฝั่ง apt และใน wave-12 bundle): revise เป็น Option A — final.**
>    ghost = QtCore-family headless daemon, ยอมรับ non-GUI Qt libs ที่เหลือเป็น server dependency
>    ปกติ; **committed scope = M7+M8; M9 PARKED** เป็น future work (optional ชัดเจน ไม่อยู่ใน
>    committed path) — geo-math note ใน Q9a ยืนตามที่เขียนไว้สำหรับวันที่ Q9a ได้วิ่งจริง
>
> ข้อบังคับ friction-ordering ยังคงอยู่และสะท้อนในการจัดชั้น M7/M8/M9 ข้างบนแล้ว
> ตารางเปรียบเทียบด้านล่างเก็บไว้เป็นบันทึกว่าชั่งอะไรกันบ้าง

| | **Option A — QtCore-family daemon (FINAL — เลือกแล้ว)** | **Option B — zero-Qt ghost (เลือกครั้งแรก แล้ว revise ออก; = สิ่งที่ได้ถ้า unpark M9)** |
|---|---|---|
| End state | committed path จบที่ **M8 done = เหลือ Core + Positioning** (หลัง M7 = 6 libs ก็อยู่ใน envelope ที่ยอมรับแล้ว) — ยอมรับ QtCore-family เป็น server dependency ปกติ เหมือนโปรเจกต์ server ใช้ boost | ghost ไม่ลิงก์ Qt เลยสักตัว |
| งานที่เหลือ | เฉพาะชั้น friction ①–② (ไม่มี ③ เลย) | ①+②+③ เต็ม |
| เวลาโดยประมาณ | M7 = หลัก wave (สัปดาห์); M8 = หลาย wave | ชั้น ③ อย่างเดียวหน่วยเป็น **เดือน** (Q9b = rewrite object model ของทุก manager class) |
| Binary/bundle | bundle ยังแบก Qt closure บางส่วน (~65M lib/ ใน wave-12 bundle, เล็กลงตาม lib ที่ตัด) — และเพราะ Qt packages เป็น modular per-lib การถือไว้แค่ 2–6 ตัวจึงถูกทั้ง apt-based และ bundle-based deploy | ไม่มี Qt closure — bundle เล็กลงมาก ไม่มี QPA/plugin dance |
| Upstream sync (AGENTS.md ให้ track upstream) | เสียดสีต่ำ–กลาง — ชั้น ① ไม่แตะ upstream เลย ชั้น ② ยัง reconcile ได้ | **diverge ถาวร** — ชั้น ③ แตะไฟล์ upstream เป็นร้อย (`QGeoCoordinate` 152 ไฟล์, `Q_OBJECT` ~482 จุด, `QString` 763 ไฟล์) — rebase upstream ของ ghost core เป็นไปไม่ได้ในทางปฏิบัติ |
| ความเสี่ยง | ต่ำ–กลาง — ตัดของที่พิสูจน์แล้วว่า headless ไม่ใช้ + port ที่มี e2e ครอบ | สูง — Q8b/Q9b แตะ flow เชื่อมต่อ vehicle ตรง ๆ ต้องพึ่ง e2e gate (CI wave 12) หนักมาก |

**ผลของ final decision ต่อ tradeoff:** committed path (M7+M8, ชั้น ①/②) **ไม่ต้องรับ tradeoff
diverge-ถาวรอีกต่อไป** — รูปแบบ flag/gate ยังรักษา additive-only และ upstream merge ยังทำได้ตลอด
ตามหลัก AGENTS.md; tradeoff นั้นจะกลับมาก็ต่อเมื่อ M9 ถูก unpark (ซึ่งต้องมีคำสั่งชัดเจนอีกครั้ง —
ดูเงื่อนไข unpark ที่หัว M9) การจัด "③ ท้ายสุด + parked" นี้เก็บ optionality ไว้เต็มที่:
งานชั้น ①/② ที่ทำไปแล้วไม่เสียเปล่าไม่ว่าอนาคตจะ unpark หรือไม่
*หมายเหตุผู้เขียน (บันทึกไว้ตามหน้าที่):* ตัวเลข coupling ที่วัดจริง (QGeoCoordinate 152 ไฟล์,
Q_OBJECT ~482 จุด, signals/emit 440 ไฟล์, QString 763 ไฟล์, StateMachine framework 90 ไฟล์,
Q9b หน่วยเดือน) โน้มไปทาง Option A ในแง่ cost/benefit มาตลอด — final decision สอดคล้องกับ
evidence ชุดนี้; ยุทธศาสตร์ API-identical + tests-before-removal ใน Q9a ยังเป็นกลไกหลัก
ที่จะกดความเสี่ยงของชั้น ③ ลงถ้าวันหนึ่งถูก unpark

**ลำดับการเดิน (sequencing) ของ committed path = M7+M8 เท่านั้น:**

1. **M7 (ชั้น ①) ก่อนเสมอ** — ทุก job แยก directory ปล่อยขนานได้:
   - *Wave N:* Q7a (Multimedia) ∥ Q7b (Sql) ∥ Q7c (KML gate) — สามตัวเล็ก ขนานเต็มสูตร swarm
   - *Wave N+1:* Q7d (Gui→Core gate; ต้อง build-พิสูจน์) ∥ Q7e (WebSockets swap ในไฟล์เราเอง)
2. **M8 (ชั้น ②)** — เริ่มจาก slice เล็ก ไล่ไปใหญ่ จบ = daemon target ถึง:
   - *Wave N+2:* Q8c (Xml จบ) ∥ Q8d (image raw-bytes) ∥ Q8a (StateMachine audit)
   - *Wave N+3:* Q8b (StateMachine port) เดี่ยว ๆ ทั้ง wave — ใหญ่และเสี่ยงสุดในชั้น ② ต้อง MockLink e2e ครอบทุกขั้น
   - *Wave N+4:* Q8e (SerialPort) → *Wave N+5:* Q8f (Network) → **committed path จบตรงนี้:
     ghost เหลือ Core + Positioning = Option A daemon target**
3. **M9 — PARKED, ไม่อยู่ใน sequencing นี้** — จะกลับเข้าแผนก็ต่อเมื่อครบเงื่อนไข unpark ที่หัว M9
   (M7+M8 จบ + e2e เขียว + คำสั่งชัดเจนจาก vegito) slicing ภายใน M9 ที่ร่างไว้
   (Q9a ∥ Q9b บาง layer → Q9c) เก็บไว้ใช้วันนั้น — ตัวเลขเป็น order-of-magnitude ไม่ใช่ commitment
4. **Invariant ตลอดทาง:** CI gate จาก wave 12 (`.github/workflows/ghost-strangler.yml`) ต้องโตตาม —
   ทุก wave ที่ตัด lib สำเร็จ ให้เพิ่มชื่อ lib นั้นเข้า DT_NEEDED blocklist ทันที กันถอยหลัง
   (แบบเดียวกับที่ assert `libQt6Qml*`/`libQt6Quick*` อยู่ตอนนี้)
