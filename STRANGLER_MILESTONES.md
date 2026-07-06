# แผนงาน M0–M6: Strangler Fig Port (QGC → Ghost Core + Tauri/Web)
## ฉบับ Ghost / AI Agents (Micro-Jobs ขนานสูงสุด)

> ยุทธศาสตร์: **additive-only + ปิด QML ด้วย build flag (ไม่ลบ)** เพื่อ merge upstream ได้ตลอด
> เอกสารนี้แตกงานเป็น Micro-Job สำหรับ AI Agents ("Ghosts") ขนาด XS (1-2 ไฟล์, เสร็จใน 1-2 prompt) / S (เพิ่มลอจิกเฉพาะส่วนย่อย)
> ออกแบบให้วิ่งขนานกันได้เป็น 10-20 agents พร้อมกัน โดยไม่มี conflict หรือ merge conflict

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

| Job | งาน | ไฟล์/ขอบเขตเป้าหมาย | ขึ้นกับ | ขนาด |
|-----|-----|-------------------|--------|------|
| G1 | เพิ่ม `option(QGC_ENABLE_QML "Enable QML UI" ON)` | `CMakeLists.txt` (root) | – | XS |
| G2 | หุ้ม `qt_add_qml_module` และ Directory UI ด้วย `if(QGC_ENABLE_QML)` | `src/CMakeLists.txt` | G1 | S |
| G3 | สร้าง shim header `qgcSetCppOwnership()` แบบ no-op เมื่อ OFF | `src/Utilities/QGCQmlCompat.h` | – | XS |
| G4a | `#ifdef` ปิด QML ใน MissionManager | `src/MissionManager/CameraCalc.cc`, `CameraSpec.cc` | G3 | XS |
| G4b | `#ifdef` ปิด QML ใน Comms | `src/Comm/LinkInterface.cc` | G3 | XS |
| G4c | `#ifdef` ปิด QML ใน Settings | `src/Settings/SettingsGroup.cc` | G3 | XS |
| G4d | `#ifdef` ปิด QML ใน Logging | `src/QGCLoggingCategoryManager.cc`, `LogManager.cc` | G3 | XS |
| G4e | `#ifdef` ปิด QML ใน Camera | `src/Camera/VehicleCameraControl.cc` | G3 | XS |
| G4f | `#ifdef` ปิด QML ใน API & Plugins | `src/api/QGCCorePlugin.cc` | G3 | XS |
| G5a | แยก build VehicleSetup (FirmwareUpgradeController) | `src/Vehicle/CMakeLists.txt` | G1 | XS |
| G5b | แยก build Actuators (GeometryImage) | `src/Vehicle/CMakeLists.txt` หรือย้ายไฟล์ | G1 | XS |
| G5c | แยก build FactControls (FactPanelController) | `src/FactSystem/CMakeLists.txt` | G1 | XS |
| G6 | เพิ่ม flag `--headless` ลงใน parser | `src/Utilities/QGCCommandLineParser.cc` | – | XS |
| G7 | ข้าม `QQmlApplicationEngine` ในโหมด headless | `src/QGCApplication.cc` (~L275) | G6 | S |
| G8 | Linker Sweep: แก้ Linker Error ทีละตัว จนกว่าจะ build ผ่าน | ทั่วไป | G4*, G5* | S |
| I1 | เพิ่ม GitHub Action job `build-headless` | `.github/workflows/headless.yml` | G8 | XS |

**การทำงานขนาน:** G3, G4a-G4f, G5a-G5c, G6 สามารถโยนให้ Agent 10 ตัวทำงานพร้อมกันได้เลย!

---

## M1 — WebSocket Bridge + Mock (ปลดล็อก UI)

| Job | งาน | ขอบเขตเป้าหมาย | ขึ้นกับ | ขนาด |
|-----|-----|----------------|--------|------|
| B0 | เขียน schema `PROTOCOL.md` (JSON structure) | `src/WebBridge/PROTOCOL.md` | – | S |
| B1a | สร้าง WebBridge class พื้นฐาน | `src/WebBridge/WebBridge.{h,cc}` | B0 | S |
| B1b | เพิ่ม WebSocket Server รับ connection | `src/WebBridge/WebBridgeServer.{h,cc}` | B1a | S |
| B2a | สร้าง TelemetryChannel พื้นฐาน | `src/WebBridge/TelemetryChannel.{h,cc}` | B1a | XS |
| B2b | เชื่อม Fact system เข้า TelemetryChannel | `src/WebBridge/TelemetryChannel.cc` | B2a | S |
| W0a | โครงโปรเจกต์ React + Vite/Bun | `web/package.json` | – | XS |
| W0b | สร้าง Zustand state store อิง `PROTOCOL.md` | `web/src/store/` | B0, W0a | S |
| W1a | สร้าง mock WS server ด้วย Bun | `web/mock/server.ts` | B0, W0a | S |
| W1b | เขียน Fixture Data จำลอง Telemetry | `web/mock/fixtures/telemetry.json` | W1a | XS |
| W2a | UI: Telemetry Panel (Attitude) | `web/src/components/telemetry/Attitude.tsx` | W0b | XS |
| W2b | UI: Telemetry Panel (Battery, GPS) | `web/src/components/telemetry/Status.tsx` | W0b | XS |

---

## M2 — Video Plane (WebCodecs)

แยก C++ ฝั่งดึงภาพออกจากฝั่ง UI ฝั่ง UI สามารถใช้ไฟล์ MP4 ดิบ mock ไปก่อนได้

| Job | งาน | ขอบเขตเป้าหมาย | ขึ้นกับ | ขนาด |
|-----|-----|----------------|--------|------|
| B5a | GStreamer tap pipeline สำหรับ H.264 | `src/WebBridge/VideoStreamServer.cc` | B1a | S |
| B5b | ส่ง raw binary frame ลง WebSocket | `src/WebBridge/VideoStreamServer.cc` | B5a | S |
| W3a | WebCodecs API: Setup VideoDecoder | `web/src/video/Decoder.ts` | W0a | S |
| W3b | Render Canvas สำหรับวิดีโอ 1 กล้อง | `web/src/components/video/VideoPlayer.tsx`| W3a | S |
| W4a | UI: Dual-cam layout (Side-by-side) | `web/src/components/video/DualCam.tsx` | W3b | XS |
| W4b | UI: Video latency overlay | `web/src/components/video/Overlay.tsx` | W4a | XS |

---

## M3 — Tauri Shell + Commands

| Job | งาน | ขอบเขตเป้าหมาย | ขึ้นกับ | ขนาด |
|-----|-----|----------------|--------|------|
| T1a | Init Tauri v2 project | `tauri/tauri.conf.json` | – | XS |
| T1b | สร้าง Dummy Sidecar Script (Bash/Bat) | `tauri/scripts/dummy_ghost.sh` | – | XS |
| T1c | Tauri setup Sidecar spawn logic | `tauri/src-tauri/src/main.rs` | T1a, T1b | S |
| B7a | CommandChannel: Arm/Disarm | `src/WebBridge/CommandChannel.cc` | B1a | XS |
| B7b | CommandChannel: Takeoff/Land/RTL | `src/WebBridge/CommandChannel.cc` | B7a | XS |
| B8a | Port `GuidedActionGate` (Preconditions) | `src/WebBridge/GuidedActionGate.{h,cc}`| – | S |
| W5a | UI: Command Buttons (Arm, RTL, etc.) | `web/src/components/guided/Buttons.tsx`| W0b | XS |
| W5b | UI: Action Confirm Slider | `web/src/components/guided/Slider.tsx` | W0b | XS |

---

## M4 — Fact/Parameter Bridge + Map

| Job | งาน | ขอบเขตเป้าหมาย | ขึ้นกับ | ขนาด |
|-----|-----|----------------|--------|------|
| B9 | FactChannel: Get/Set Parameters | `src/WebBridge/FactChannel.{h,cc}` | B1a | S |
| B10 | ส่ง FactMetaData (min, max, type) | `src/WebBridge/FactChannel.cc` | B9 | XS |
| W6a | UI: Parameter Browser Table | `web/src/components/params/ParamTable.tsx`| W0b | S |
| W6b | UI: Parameter Edit Modal | `web/src/components/params/ParamEdit.tsx` | W6a | XS |
| W7a | MapLibre: Init Map | `web/src/components/map/CoreMap.tsx` | W0a | S |
| W7b | MapLibre: Vehicle Marker & Trail | `web/src/components/map/VehicleLayer.tsx`| W7a | XS |

---

## M5 — Mission + FlyView Parity

| Job | งาน | ขอบเขตเป้าหมาย | ขึ้นกับ | ขนาด |
|-----|-----|----------------|--------|------|
| B11a| MissionChannel: Upload | `src/WebBridge/MissionChannel.cc` | B1a | S |
| B11b| MissionChannel: Download | `src/WebBridge/MissionChannel.cc` | B11a| XS |
| W8a | UI: Map Click to Add Waypoint | `web/src/components/plan/WaypointAdder.tsx`| W7a | S |
| W8b | UI: Waypoint List Panel | `web/src/components/plan/WaypointList.tsx` | W0b | S |
| W9a | UI: FlyView Instrument Panel | `web/src/components/flyview/Instruments.tsx`| W2a | S |

---

## M6 — Hardening + Upstream Sync

| Job | งาน | ขอบเขตเป้าหมาย | ขึ้นกับ | ขนาด |
|-----|-----|----------------|--------|------|
| I6a | Tauri Windows Bundle Config | `tauri/tauri.conf.json` | T1c | XS |
| I6b | Tauri Linux Bundle Config | `tauri/tauri.conf.json` | T1c | XS |
| B14a| Security: WebBridge Bind Localhost Only| `src/WebBridge/WebBridgeServer.cc` | B1b | XS |
| B14b| Security: Auth Token Exchange | `src/WebBridge/WebBridgeServer.cc` | B14a| S |

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
