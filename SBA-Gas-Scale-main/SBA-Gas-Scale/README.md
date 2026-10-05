# SBA GAS DETECTOR - Android App Development Handoff

## 1. System Overview

The SBA Gas Detector app serves as the local Bluetooth Low Energy (BLE) dashboard and configuration tool for an ESP32-C3 powered LPG safety appliance. The app has two primary responsibilities:

1. **Live Dashboard:** Read and display real-time gas percentages, net weight, and alarm states.
2. **System Configuration:** Send strings to the ESP32 to set phone numbers, select cylinder limits (3kg, 5kg, 6kg), and trigger live tare calibrations for different cylinder steel weights.

## 2. BLE Network Architecture

The app must scan for and connect to the following BLE parameters.

* **Device Name:** `SBA GAS DETECTOR`
* **Service UUID:** `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
* **Characteristic UUID:** `beb5483e-36e1-4688-b7f5-ea07361b26a8`
* *Properties:* `READ`, `NOTIFY` (for live data), `WRITE` (for configuration).

---

## 3. The Live Data Stream (ESP32 → App)

Once connected, the ESP32 pushes a string payload every 500ms via `NOTIFY`.

**Format:** `DATA:<GasPercentage>,<NetWeight>,<AlarmState>`  
**Example String:** `DATA:14.5,4.25,0`

### Parsing the Array:

* **Index 0 (`14.5`):** MQ-5 Combustible Gas level (Percentage).  
  * *UI Logic:* 0-20% (Green/Safe), 21-59% (Yellow/Warning), >60% (Red/Critical).
* **Index 1 (`4.25`):** Current net weight of the liquid gas in kilograms.  
* **Index 2 (`0`):** System State.  
  * `0` = Normal.  
  * `1` = Low Fuel Warning (Net weight < 1.0kg).  
  * `2` = CRITICAL GAS LEAK (Trigger aggressive UI strobing/vibration).

---

## 4. Configuration Protocol (App → ESP32)

To configure the appliance, the app must send specific, exact text strings (via `WRITE` property) to the Characteristic UUID.

### A. Live Tare Calibration (Zeroing the Scale)

Because different brands of cylinders have different empty (tare) weights, the user must be able to place a completely empty cylinder on the scale and press a "Calibrate Empty Cylinder" button in the app.

* **App Sends String:** `"TARE"`
* **Hardware Action:** The ESP32 instantly zeros the load cell, locking in the new tare weight in its local memory.

### B. Cylinder Capacity Selection

The user must select the size of the cylinder they are using so the app can accurately calculate the fuel percentage UI (e.g., a progress ring). The hardware supports up to 6kg.

* **App Sends String:** `"MAX:3"` (For a 3kg cylinder)  
* **App Sends String:** `"MAX:5"` (For a 5kg cylinder)  
* **App Sends String:** `"MAX:6"` (For a 6kg cylinder)  
* **UI Logic:** The app should store this selected maximum locally. To display the fuel ring percentage on the dashboard, the app calculates: `(NetWeight / Max_Selected) * 100`.

### C. Update Emergency SMS/Call Number

The user must be able to change the phone number the SIM800L module contacts during a critical leak or 24-hour report.

* **App Sends String:** `"NUM:+2348012345678"`
* **String Constraint:** The string must start with `NUM:` followed strictly by the international dialing code format (e.g., `+234`).
* **Hardware Action:** The ESP32 parses the string, strips the `NUM:` prefix, and updates the global emergency contact variable used for AT commands.

---

## 5. UI/UX Minimum Requirements

The application should be structured with two main views:

1. **The Dashboard (Main Fragment):**
* Large circular progress bar showing Gas Weight remaining (based on selected cylinder max).
* Digital readout of actual Kg remaining.
* Gas Level indicator (Safe/Warning/Danger).
* Connection status icon (BLE linked/unlinked).

2. **Hardware Setup (Settings Fragment):**
* Dropdown/Radio buttons for Cylinder Size (3kg, 5kg, 6kg).
* Text Input field for Emergency Phone Number (with a "Save to Device" button).
* A prominent "Calibrate Tare" button with a warning prompt: *"Ensure only a completely EMPTY cylinder is on the scale before proceeding."*
