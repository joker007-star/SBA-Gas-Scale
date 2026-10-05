# ESP32 Motion & Manual Override Lighting Controller

A robust 4-channel smart lighting automation system built on the ESP32 microcontroller using PlatformIO and the Arduino framework.

The system combines dual PIR motion detection with hardware-interrupted pushbutton overrides, featuring automatic room-vacancy reset logic.

---

## Features

- **4 Independent Light Channels**: Drives 4 active-low relay outputs.
- **Dual PIR Motion Sensing**: High-coverage detection using two motion sensors (logical OR).
- **Physical Pushbutton Overrides**: Instant tactile response via hardware interrupts (`FALLING` edge) with 50ms software debounce.
- **Channel Isolation**: Manually toggling any switch locks out automation for that specific channel without interrupting the others.
- **Auto-Reset on Room Vacancy**: Once the room is completely empty (no motion) for the full timeout period, manual overrides clear automatically, re-arming automatic motion control.
- **Thread-Safe Concurrency**: Critical sections (`portENTER_CRITICAL` / `portENTER_CRITICAL_ISR`) safeguard state variables between FreeRTOS interrupt routines and the main execution loop.
- **Modular Architecture**: Separate configuration header (`include/config.h`) and application logic (`src/main.cpp`).

---

## Hardware Pinout & Wiring

### 1. Pushbuttons (Manual Control)
Configured with internal pull-ups (`INPUT_PULLUP`). One side connects to the ESP32 pin, the other side connects to **GND**.

| Channel | ESP32 GPIO | Switch Type | Active Edge |
| :--- | :--- | :--- | :--- |
| Channel 1 | `GPIO 13` | Momentary / Push | `FALLING` (GND) |
| Channel 2 | `GPIO 14` | Momentary / Push | `FALLING` (GND) |
| Channel 3 | `GPIO 26` | Momentary / Push | `FALLING` (GND) |
| Channel 4 | `GPIO 27` | Momentary / Push | `FALLING` (GND) |

### 2. Relay Modules (Lights / Loads)
Pre-configured for standard **Active-Low** optocoupled relay modules (`LOW = ON`, `HIGH = OFF`).

| Channel | ESP32 GPIO | Logic State | Default at Boot |
| :--- | :--- | :--- | :--- |
| Relay 1 | `GPIO 16` | Active-Low | `HIGH` (OFF) |
| Relay 2 | `GPIO 17` | Active-Low | `HIGH` (OFF) |
| Relay 3 | `GPIO 18` | Active-Low | `HIGH` (OFF) |
| Relay 4 | `GPIO 19` | Active-Low | `HIGH` (OFF) |

### 3. PIR Motion Sensors
Connected to ESP32 **Input-Only** pins (GPIs). Standard PIR modules (e.g., AM312, HC-SR501) output 3.3V active HIGH on motion.

| Sensor | ESP32 GPIO | Signal Level |
| :--- | :--- | :--- |
| PIR Sensor 1 | `GPIO 34` | High (3.3V) = Motion |
| PIR Sensor 2 | `GPIO 35` | High (3.3V) = Motion |

---

## System Timing & Configuration

All system parameters can be customized in [`include/config.h`](include/config.h):

```cpp
constexpr uint32_t DEBOUNCE_DELAY_MS       = 50;     // Switch debounce window
constexpr uint32_t MOTION_TIMEOUT_MS       = 10000;  // Inactivity timeout (e.g., 10 seconds)
constexpr uint32_t OVERRIDE_RESET_DELAY_MS = 5000;   // Additional delay before clearing overrides
constexpr uint32_t SERIAL_BAUD_RATE        = 115200; // Debug monitor baud
```

---

## Project Structure

```
MotionLights/
├── include/
│   └── config.h         # Hardware pinout, active levels, and timing constants
├── src/
│   └── main.cpp         # Main application logic, ISRs, and state machine
├── platformio.ini       # PlatformIO environment & board configuration
├── .gitignore           # Git ignore list for build artifacts & cache
└── README.md            # Project documentation and wiring guide
```

---

## Building and Flashing

### Using PlatformIO CLI

```powershell
# Build firmware
pio run

# Upload to ESP32 board
pio run --target upload

# Open serial monitor
pio device monitor -b 115200
```

### Using Visual Studio Code
1. Open the `MotionLights` folder in VS Code with the **PlatformIO IDE** extension installed.
2. Click the **PlatformIO Build** button (check icon) in the status bar to compile.
3. Click the **PlatformIO Upload** button (arrow icon) to flash to your connected ESP32.
4. Click the **Serial Monitor** button (plug icon) at 115200 baud to view runtime diagnostic logs.
