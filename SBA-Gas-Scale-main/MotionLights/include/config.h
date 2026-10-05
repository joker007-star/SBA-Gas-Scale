#pragma once

#include <Arduino.h>

// =================================================================
// SYSTEM & CHANNEL CONFIGURATION
// =================================================================
constexpr uint8_t NUM_LIGHTS = 4;

// =================================================================
// PIN ASSIGNMENTS
// =================================================================
// Pushbuttons: Configured with internal pull-ups (INPUT_PULLUP).
// Switch connects pin to GND when pressed (active LOW).
constexpr uint8_t BUTTON_PINS[NUM_LIGHTS] = {13, 14, 26, 27};

// Relays: Configured as digital outputs.
// Standard pre-built relay boards are Active-Low.
constexpr uint8_t RELAY_PINS[NUM_LIGHTS]  = {16, 17, 18, 19};

// PIR Sensors: Connected to ESP32 Input-Only pins (GPIO 34 & 35).
// PIR modules actively drive high (3.3V) when motion is detected.
constexpr uint8_t PIR_PIN_1 = 34;
constexpr uint8_t PIR_PIN_2 = 35;

// =================================================================
// ELECTRICAL POLARITY
// =================================================================
constexpr uint8_t RELAY_ON  = LOW;   // Active-Low relay ON state
constexpr uint8_t RELAY_OFF = HIGH;  // Active-Low relay OFF state

// =================================================================
// TIMING & DELAYS (milliseconds)
// =================================================================
constexpr uint32_t DEBOUNCE_DELAY_MS       = 50;     // Switch debounce window
constexpr uint32_t MOTION_TIMEOUT_MS       = 10000;  // 10s inactivity before auto-off
constexpr uint32_t OVERRIDE_RESET_DELAY_MS = 5000;   // 5s additional quiet time before clearing manual override
constexpr uint32_t LOOP_POLL_INTERVAL_MS   = 50;     // Main loop tick delay

// Serial communications
constexpr uint32_t SERIAL_BAUD_RATE        = 115200;
