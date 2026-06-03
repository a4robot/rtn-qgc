# Custom Boat (Rover) System Integration Architecture

This document outlines the hardware integration and ground station UI specifications for the custom autonomous boat platform. It serves as the primary technical reference bridging the custom forks of **ArduPilot** (firmware/hardware handling) and **QGroundControl** (UI/telemetry visualization).

## 1. Core Hardware
* **Flight Controller:** Holybro Pixhawk 4 (running custom ArduPilot Rover firmware)
* **Vehicle Frame Class:** Boat (Rover)

## 2. Serial Ports, GPS, & Marine Electronics

### 2.1 Pixhawk 4 UART / Serial Port Mapping
The hardware serial ports are strictly mapped to the following functions and peripherals:

| Pixhawk 4 Port | ArduPilot Serial | Assigned Function / Device |
| :--- | :--- | :--- |
| **TELEM1** | `SERIAL1` | Primary Telemetry Communication (GCS) |
| **TELEM2** | `SERIAL2` | AIS Data (Haiyang HIS-50A via AIVDM / NMEA0183) |
| **GPS Module** | `SERIAL3` | Primary GPS (u-blox M8N) |
| **UART & I2C B** | `SERIAL4` | NMEA2000 Sensors (AIRMAR 150WX & P39 via NMEA2K2 gateway) |
| **Debug Port** | `SERIAL5` | Reserved (Additional Arbitrary NMEA0183 Sensor) |

### 2.2 Primary Navigation (GPS Wiring)
The u-blox M8N GPS module connects to the Pixhawk 4 GPS port (`SERIAL3`):

| M8N GPS Pin | Pixhawk 4 GPS Port Pin | Description |
| :--- | :--- | :--- |
| Pin 1 | Pin 1 | VCC (5V) |
| Pin 2 | Pin 2 | TX (Data Out) |
| Pin 3 | Pin 3 | RX (Data In) |
| Pin 4 | Pin 4 | SCL (I2C Clock) |
| Pin 5 | Pin 5 | SDA (I2C Data) |
| Pin 6 | GND Pin | Ground |

*Note: The safety switch and buzzer are physically decoupled from the main GPS harness and are wired to dedicated switch/buzzer circuits on the boat.*

## 3. RC Remote PWM Channel Mapping
The system utilizes 16 PWM channels from the RC remote, mapped to core controls, payloads (Gimbal Cameras Z10TL & A10T), I2C actuation, and direct AUX outputs:

| PWM Channel | Function Assigned | Destination / Sub-System |
| :---: | :--- | :--- |
| **1** | Z10TL Camera Pan | Payload (Gimbal 1) |
| **2** | Z10TL Camera Tilt | Payload (Gimbal 1) |
| **3** | Throttle Control | ArduPilot Core (Motor ESC) |
| **4** | Rudder Control | ArduPilot Core (Steering) |
| **5** | Z10TL Camera Zoom | Payload (Gimbal 1) |
| **6** | Autopilot Mode Selection | ArduPilot Core (Flight Mode) |
| **7** | FPV Camera Selection | `AUX6` Output (Front/Back switch) |
| **8 (Positive)** | Trim Up | I2C TCA9534 (`001b`) - Ch 0 |
| **8 (Negative)** | Trim Down | I2C TCA9534 (`001b`) - Ch 1 |
| **9** | A10T Camera Pan | Payload (Gimbal 2) |
| **10** | A10T Camera Tilt | Payload (Gimbal 2) |
| **11** | A10T Camera Zoom | Payload (Gimbal 2) |
| **12** | A10T Picture Mode | Payload (Gimbal 2) |
| **13** | Navigation Light Toggle | `AUX1` Output |
| **14** | Siren (Emergency) Light Toggle| `AUX2` Output |
| **15** | All-Around Light Toggle | `AUX3` Output |
| **16** | Spotlight Toggle | `AUX4` Output |

## 4. Custom I2C & Direct Actuation (Sensors & Actuators)
To optimize bus traffic, all I2C read polling operates at a unified **1 Hz loop**.

### 4.1 Analog Sensor Array (ADS1115)
* **I2C Address:** `1001000b`
*(Calibration: Final_Value = (Raw_Input * Scale [a]) + Offset [b])*

| ADS1115 Ch | Sensor Assigned | Scale Param (`a`) | Offset Param (`b`) |
| :---: | :--- | :--- | :--- |
| **0** | Trim Angle | `CUST_TRIM_MULT` | `CUST_TRIM_OFF` |
| **1** | Battery Voltage | `CUST_BAT_MULT` | `CUST_BAT_OFF` |
| **2** | Rudder Angle | `CUST_RUDD_MULT` | `CUST_RUDD_OFF` |
| **3** | Fuel Level | `CUST_FUEL_MULT` | `CUST_FUEL_OFF` |

### 4.2 Engine RPM Sensor (ABLIC S-35770)
* **I2C Address:** `0110010b` 
* **Calculation:** Read command every 1s, followed by reset. Count per 1s converted to RPM (Count * 60).

### 4.3 Trim Control & Status (TCA9534 Expander A)
* **I2C Address:** `001b`
* **Trigger:** RC PWM Channel 8 (Positive/Negative logic).

| TCA9534 Ch | Function | I/O Direction |
| :--- | :--- | :--- |
| **0** | Trim Up Command | Output |
| **1** | Trim Down Command | Output |
| **4** | Trim Up Status | Input |
| **5** | Trim Down Status | Input |

### 4.4 Lighting Control & Status 
Lighting actuation has been moved to direct Pixhawk 4 `AUX` channel outputs. The TCA9534 Expander B is retained strictly for reading the active operational status of the lights.

* **Status Read I2C Address:** `000b`

| Light Assigned | Actuation Output | Control Source(s) | Status Read (TCA9534 `000b`) |
| :--- | :--- | :--- | :--- |
| **Navigation Light** | `AUX1` | RC PWM 13 / QGC UI | Channel 0 |
| **Siren (Emergency) Light** | `AUX2` | RC PWM 14 / QGC UI | Channel 1 |
| **All-Around Light** | `AUX3` | RC PWM 15 / QGC UI | Channel 2 |
| **Spotlight** | `AUX4` | RC PWM 16 / QGC UI | Channel 3 |
| **Port/Starboard Light** | `AUX5` | **QGC UI Only** (No RC mapping) | Channel 4 |

---

## 5. QGroundControl UI/UX Specifications
To display the custom telemetry, the QGroundControl fork must implement custom QML overlays injected into the primary "Fly View" page. 

### 5.1 Telemetry Indicators
* **Engine RPM:** Circular Dial Gauge with dynamic needle mapping.
* **Rudder Angle:** Horizontal Scale (Left-to-Right mapping with center-zero orientation).
* **Trim Angle:** Vertical Scale.
* **Fuel Level:** Vertical Scale (Fills from bottom to top).
* **Battery Voltage:** Vertical Scale.

### 5.2 Interactive Control Panel (Left Side Overlay)
A dedicated interaction pane anchored to the left side of the Fly Page.

* **Trim Controls:** * Two clickable icons for **Trim Up** and **Trim Down**.
  * **Dynamic Feedback:** Icon illuminates when active status is verified (TCA9534 `001b` Ch4 or Ch5 reads active).
* **Lighting Controls:**
  * Five clickable icons for toggling lighting circuits (including the exclusive Port/Starboard control which sends a MAVLink command to trigger `AUX5`).
  * **Dynamic Feedback:** Each icon visually highlights when its active status is verified by the hardware (TCA9534 `000b` reads active on its respective channel).

### 5.3 System Alerts & Logic
* **Low Fuel Warning:** QGC actively monitors incoming MAVLink fuel level packet. If value drops below defined threshold, QGC triggers a visual/auditory alert.