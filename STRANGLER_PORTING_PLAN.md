# ยุทธศาสตร์การพอร์ต QGroundControl ด้วย Strangler Fig Pattern (Tauri + Web)

เอกสารนี้คือกำหนดการและแผนยุทธศาสตร์ (Strategic Plan) ในการพอร์ต QGroundControl (QGC) จากระบบ UI เดิม (Qt/QML) ไปสู่สถาปัตยกรรมใหม่ (Tauri + WebCodecs) โดยประยุกต์ใช้ **Strangler Fig Pattern** เพื่อลดความเสี่ยง ป้องกันไม่ให้การพัฒนาฟีเจอร์ปัจจุบันต้องหยุดชะงัก และสามารถทยอยนำโค้ดใหม่ขึ้น Production ได้ทันทีโดยไม่ต้องรอให้เสร็จทั้งหมด 100%

---

## 1. แนวคิดของ Strangler Fig Pattern ในบริบทของ QGC

แนวคิดหลักคือการ **"ปลูกต้นไม้ใหม่คลุมต้นไม้เก่า"** เราจะไม่ Rewrite QGC ใหม่ทั้งหมดในรวดเดียว (Big Bang Rewrite) ซึ่งมักจะล้มเหลว แต่เราจะทำตามขั้นตอนดังนี้:
1. **แยกส่วน Core กับ UI:** ทำให้ C++ Core ของ QGC (Vehicle, FactSystem, MAVLink) สามารถทำงานได้แบบ Headless (ไม่มีหน้าจอ QML)
2. **สร้างเปลือกใหม่ (The Strangler):** เอา Tauri มาครอบเป็นแอปพลิเคชันหลัก (Main Shell)
3. **ทะยอยย้ายทีละฟีเจอร์:** เริ่มย้าย UI ทีละส่วนจาก QML มาเขียนด้วย Web Technologies (React/Vue/Vanilla) โดยให้คุยกับ QGC Core ผ่าน WebSocket/RPC
4. **ตัดเนื้อร้าย (Strangle):** เมื่อ UI ส่วนไหนถูกเขียนใหม่บน Web แล้ว เราจะลบโค้ด QML ส่วนนั้นทิ้ง จนสุดท้ายไม่เหลือ Qt/QML ในโปรเจกต์เลย

---

## 2. โครงสร้างสถาปัตยกรรมระหว่างการพอร์ต (Transition Architecture)

ระหว่างการพอร์ต ระบบจะมีหน้าตาแบบนี้:

```text
┌──────────────────────── Tauri (The New Shell) ───────────────────────┐
│                                                                    │
│  ┌────────── Web Frontend (UI) ──────────┐                         │
│  │                                       │                         │
│  │  ▶ Video Receiver (WebCodecs)         │◀═══ [Video Stream] ═══╗ │
│  │  ▶ Telemetry & Map                    │                       ║ │
│  │  ▶ Settings & Parameters              │◀═══ [State Sync] ═══╗ ║ │
│  └───────────────────────────────────────┘                     ║ ║ │
│                      │                                         ║ ║ │
│               Tauri IPC / RPC                                  ║ ║ │
│                      ▼                                         ║ ║ │
│  ┌───────── Rust Process Management ─────┐                     ║ ║ │
│  │  spawn / monitor / kill QGC daemon    │                     ║ ║ │
│  └───────────────────────────────────────┘                     ║ ║ │
└───────────────────────┬────────────────────────────────────────────┘
                        │ (Spawn Sidecar)                        ║ ║
                        ▼                                        ║ ║
┌──────────────── QGC Core (The Legacy System) ──────────────────┐ ║ ║
│                                                                │ ║ ║
│  [Vehicle] [FactSystem] [MissionManager] [FirmwarePlugin]      │ ║ ║
│                                                                │ ║ ║
│  ┌─────────────────┐           ┌────────────────────────────┐  │ ║ ║
│  │ MAVLink Network │           │ WebSocket Bridge (New API) │══╝ ║ │
│  └─────────────────┘           └────────────────────────────┘    ║ │
│                                                                  ║ │
│  ┌──────────────────────────────┐  ┌──────────────────────────┐  ║ │
│  │ Video Pipeline (GStreamer)   │──│ Video WS Server (POC)    │══╝ │
│  └──────────────────────────────┘  └──────────────────────────┘    │
└────────────────────────────────────────────────────────────────────┘
```

---

## 3. แผนการดำเนินงาน (Phased Migration)

### Phase 1: Headless QGC & The Tauri Facade (สร้างเปลือก)
*เป้าหมาย: ทำให้ Tauri กลายเป็นตัวรัน QGC แบบซ่อนหน้าต่าง Qt ได้สำเร็จ*
- **Action:** แก้ไข `main.cc` ของ QGC ให้มีโหมด `--headless` หรือ `--ws-only` ที่จะไม่สร้าง `QQmlApplicationEngine` แต่จะเปิดเฉพาะ Core Components (Vehicle Manager, MAVLink)
- **Action:** สร้างโปรเจกต์ Tauri ใหม่ กำหนดให้ QGC C++ เป็น `externalBin` (Sidecar)
- **Outcome:** เมื่อเปิดแอป Tauri มันจะไปปลุก QGC ให้อยู่เบื้องหลังอย่างเงียบๆ

### Phase 2: The Video Plane (ดึงผลลัพธ์จาก POC มาใช้)
*เป้าหมาย: นำผลลัพธ์จาก `screen-mirror-poc` มาใส่ใน QGC*
- **Action:** ย้ายโค้ด GStreamer WebSocket (จาก POC) ไปฝังไว้แทนที่ `VideoReceiver` เดิมของ QGC
- **Action:** สร้าง Video Widget บน Web Frontend ให้รับภาพผ่าน WebCodecs 
- **Outcome:** หน้าจอ Tauri จะแสดงวิดีโอจากโดรนได้แบบ Low-latency ส่วน UI อื่นๆ ยังไม่ต้องมี (หรือทำปุ่มจำลองไปก่อน)

### Phase 3: Telemetry & The Bridge (สร้างสะพานข้อมูล)
*เป้าหมาย: นำข้อมูลการบินพื้นฐานขึ้นไปโผล่บน Web*
- **Action:** เขียน C++ WebSocket Server ภายใน QGC (QWebSocketServer) เพื่อทำตัวเป็น RPC Bridge 
- **Action:** ดักจับ Signal/Slot จากระบบ `Vehicle` (เช่น Attitude, Altitude, Battery) แล้ว Publish เป็น JSON ผ่าน Bridge
- **Action:** ฝั่ง Web Subscribe ข้อมูลเหล่านี้เพื่อนำไปวาดหน้าปัด (HUD) และดึงแผนที่ (Leaflet/Mapbox) มาแสดง
- **Outcome:** ตอนนี้ Tauri จะมีวิดีโอ + แผนที่ + หน้าปัดการบินที่ใช้งานได้จริง

### Phase 4: FactSystem & Two-way Binding (ย้ายความซับซ้อน)
*เป้าหมาย: ทำให้ Web สามารถอ่าน/เขียน Parameters ต่างๆ ได้เหมือน QGC เดิม*
- **Action:** ขยายความสามารถของ Bridge ให้รองรับ `FactSystem` (ตัวจัดการ Parameter ของ QGC)
- **Action:** ฝั่ง Web สร้าง Component ที่ทำ "Two-way binding" กับ QGC Facts (เช่น เมื่อเปลี่ยนค่าใน Web ต้องยิงคำสั่งไปแก้ C++ Fact ซึ่งจะส่ง MAVLink ไปที่โดรนอัตโนมัติ)
- **Outcome:** สามารถรื้อระบบหน้าต่าง Settings, Vehicle Setup ออกจาก QML มาลง Web ได้ทั้งหมด

### Phase 5: The Strangler Closes (สิ้นสุดการพอร์ต)
*เป้าหมาย: ลบ Qt ออกจากการพึ่งพาของ UI อย่างสมบูรณ์*
- **Action:** เมื่อ UI ทุกชิ้นถูกเขียนใหม่บน Web แล้ว ให้ลบโฟลเดอร์ `src/QmlControls` และไฟล์ `.qml` ทั้งหมดทิ้ง
- **Action:** ทำความสะอาด C++ Core ให้กลายเป็น Native Library / Daemon อย่างแท้จริง 
- **Outcome:** QGC โฉมใหม่ที่เบา, เร็ว, UI สวยงามตามฉบับ Web Modern และบำรุงรักษาง่าย

---

## 4. กฎเหล็กระหว่างการพอร์ต (Golden Rules)

1. **ห้ามหยุดพัฒนาฟีเจอร์ใหม่:** ทีมพัฒนา C++ สามารถเขียนโค้ดเพิ่มใน QGC Core ได้ตามปกติ (เช่น เพิ่ม Firmware ใหม่) โค้ดส่วนนั้นจะถูก Web Frontend ดึงไปใช้ผ่าน RPC อัตโนมัติ (ตราบใดที่ยังอิงกับระบบ FactSystem เดิม)
2. **ห้ามเพิ่ม Logic ใน UI:** Frontend บน Tauri ต้อง "โง่" (Dumb UI) มีหน้าที่แค่วาดภาพตาม State ที่ C++ Core ส่งมา หากมี Logic การคำนวณซับซ้อน ต้องเขียนไว้ใน C++ Core เท่านั้น
3. **ส่ง Data, ไม่ใช่ Event:** WebSocket Bridge ควรส่ง State ปัจจุบัน (เช่น "Battery = 50%") แทนที่จะส่ง Event ("Battery ลดลง 1%") เพื่อให้ Frontend สามารถ Reconnect แล้วดึง State ล่าสุดไปวาดต่อได้ทันที (Stateless UI)
