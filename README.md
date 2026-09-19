# AquaIntel
A Water Tank Monitoring System 

# 💧 AquaIntel — Smart Autonomous Water Tank System

[![Platform](https://img.shields.io/badge/Platform-ESP32-blue.svg)](https://www.espressif.com/)
[![Language](https://img.shields.io/badge/Language-C%2B%2B%20%2F%20Arduino-orange.svg)]()
[![Hardware](https://img.shields.io/badge/Hardware-Custom%202--Layer%20PCB-green.svg)]()
[![License](https://img.shields.io/badge/License-MIT-lightgrey.svg)](LICENSE)

AquaIntel is an end-to-end, IoT-enabled autonomous water tank telemetry and water quality management system powered by the **ESP32**. It performs real-time continuous monitoring of water level, pH quality, temperature, and cumulative consumption flow rates while providing automated leak and overflow hazard detection.

Telemetry is exposed via a **dual-link wireless pipeline**: a local neumorphic Web Dashboard over Wi-Fi and a native companion Android application over Bluetooth Serial.

---

## 🌟 Key Features

- **Multi-Sensor Environmental Telemetry:**
  - Ultrasonic depth sensing for real-time tank level & volume calculation (1,000 L capacity model).
  - Analog pH sensor module for water acidity/alkalinity quality evaluation.
  - Waterproof DS18B20 digital 1-Wire temperature sensing.
  - Interrupt-driven (ISR) Hall-effect flow sensor for instantaneous flow rate (L/min) and totalized consumption tracking.
- **Dual Telemetry Architecture:**
  - **Local Web Server (Wi-Fi):** Embedded neumorphic HTTP dashboard with animated liquid fill gauges and live status cards.
  - **Mobile Companion App (Bluetooth):** Real-time monitoring, diagnostic feedback, and push notification triggers.
- **Automated Alert Engine:** Multi-threshold detection for:
  - 🚨 Pipeline Leaks (unusual sustained consumption or level drop)
  - ⚠️ Tank Overflow / Spill risk
  - 🧪 Water Contamination (pH deviations beyond safe potable limits)
- **Custom Hardware Design:** Dedicated 2-layer FR-4 PCB (80 × 60 mm) with integrated power rail decoupling, low-noise ground plane, and tailored 3D-printed enclosure.

---

## 📐 System Architecture

![System Block Diagram](assets/IEEE_System_Block_Diagram.png)

### Pinout Configuration

| Component / Sensor | Interface / Signal | ESP32 GPIO | Operating Voltage |
| :--- | :--- | :--- | :--- |
| **Ultrasonic Sensor (HC-SR04)** | Trigger | `GPIO 5` | 5V |
| **Ultrasonic Sensor (HC-SR04)** | Echo | `GPIO 18` | 5V (Level-shifted) |
| **Analog pH Sensor Module** | Analog Output ($P_o$) | `GPIO 34` (ADC1) | 5V |
| **DS18B20 Temp Sensor** | 1-Wire Data | `GPIO 4` (4.7kΩ Pull-up) | 3.3V |
| **Hall-Effect Flow Sensor** | Pulse Output (ISR) | `GPIO 16` | 5V |
| **System Status Indicator** | LED | `GPIO 2` | 3.3V |

---

## 🛠️ Hardware & PCB Specs

- **Board Dimensions:** 80.0 mm × 60.0 mm (2-layer FR-4, 1.6 mm thickness, 1 oz Cu)
- **Power Supply:** 5V DC Jack onboard input with dedicated filtered 3.3V power rails.
- **Ground Architecture:** Solid bottom-layer copper pour ground plane for analog sensor noise reduction.
- **Enclosure:** Custom modeled snap-fit housing (`AQUAINTEL_Enclosure.stl`) designed for standard tank lid/wall mount.

---

## 🚀 Getting Started

### Prerequisites & Libraries
Install the following libraries in your Arduino IDE / PlatformIO:
- `WiFi.h` & `WebServer.h` (ESP32 Core)
- `BluetoothSerial.h` (ESP32 Core)
- `OneWire` by Jim Studt, Paul Stoffregen
- `DallasTemperature` by Miles Burton

### Firmware Flashing
1. Clone this repository:
   ```bash
   git clone https://github.com/your-username/AquaIntel.git
