#include <Arduino.h>
#include "config.h"

// =================================================================
// RUNTIME STATE VARIABLES
// =================================================================
// Concurrency mutex for thread-safe access between ISRs and main loop
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;

// Volatile state arrays accessed in both interrupt and main contexts
volatile bool lightState[NUM_LIGHTS]       = {false, false, false, false};
volatile bool manualOverride[NUM_LIGHTS]   = {false, false, false, false};
volatile uint32_t lastButtonPress[NUM_LIGHTS] = {0, 0, 0, 0};

// Motion tracking variables
uint32_t lastMotionTime = 0;
bool previousMotionDetected = false;

// =================================================================
// HARDWARE DRIVER HELPERS
// =================================================================

/**
 * @brief Sets the physical relay pin according to active-low logic.
 * @param index Light channel index (0 to NUM_LIGHTS - 1)
 * @param turnOn True to turn light ON, false to turn light OFF
 */
inline void writeRelay(uint8_t index, bool turnOn) {
  if (index < NUM_LIGHTS) {
    digitalWrite(RELAY_PINS[index], turnOn ? RELAY_ON : RELAY_OFF);
  }
}

/**
 * @brief Checks whether either PIR sensor is actively reporting motion.
 * @return true if motion is detected on PIR 1 or PIR 2
 */
inline bool isMotionDetected() {
  return (digitalRead(PIR_PIN_1) == HIGH) || (digitalRead(PIR_PIN_2) == HIGH);
}

// =================================================================
// INTERRUPT SERVICE ROUTINES (ISRs)
// =================================================================

/**
 * @brief Shared button handler executed on falling-edge pin interrupts.
 * @param index Button channel index (0 to NUM_LIGHTS - 1)
 */
void IRAM_ATTR handleButtonPress(uint8_t index) {
  uint32_t currentTime = millis();

  // Software debounce filter
  if ((currentTime - lastButtonPress[index]) > DEBOUNCE_DELAY_MS) {
    portENTER_CRITICAL_ISR(&stateMux);
    lightState[index] = !lightState[index];
    manualOverride[index] = true;
    lastButtonPress[index] = currentTime;
    
    // Switch physical relay immediately
    writeRelay(index, lightState[index]);
    portEXIT_CRITICAL_ISR(&stateMux);
  }
}

// Individual ISR entry points routed to the common handler
void IRAM_ATTR isr0() { handleButtonPress(0); }
void IRAM_ATTR isr1() { handleButtonPress(1); }
void IRAM_ATTR isr2() { handleButtonPress(2); }
void IRAM_ATTR isr3() { handleButtonPress(3); }

// =================================================================
// SETUP & INITIALIZATION
// =================================================================

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  delay(200); // Allow serial line stabilization
  Serial.println("\n==============================================");
  Serial.println(" ESP32 MotionLights Controller Initializing   ");
  Serial.println("==============================================");

  // Initialize PIR sensor pins as digital inputs
  pinMode(PIR_PIN_1, INPUT);
  pinMode(PIR_PIN_2, INPUT);

  // Initialize relay outputs and button inputs
  for (uint8_t i = 0; i < NUM_LIGHTS; i++) {
    // Default relay to OFF prior to driving output to avoid power-on click
    writeRelay(i, false);
    pinMode(RELAY_PINS[i], OUTPUT);

    // Pushbuttons with internal pullup
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
  }

  // Attach hardware interrupts on FALLING edge (active-low button push)
  attachInterrupt(digitalPinToInterrupt(BUTTON_PINS[0]), isr0, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PINS[1]), isr1, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PINS[2]), isr2, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PINS[3]), isr3, FALLING);

  Serial.println("[SYSTEM] Setup completed successfully. Entering main loop.");
}

// =================================================================
// MAIN LOOP
// =================================================================

void loop() {
  const uint32_t currentTime = millis();
  const bool motionNow = isMotionDetected();

  // Log state transition on motion start
  if (motionNow && !previousMotionDetected) {
    Serial.println("[PIR] Motion detected! Activating automatic lights.");
  }
  previousMotionDetected = motionNow;

  if (motionNow) {
    lastMotionTime = currentTime;

    // Turn on lights that are not manually overridden
    for (uint8_t i = 0; i < NUM_LIGHTS; i++) {
      portENTER_CRITICAL(&stateMux);
      const bool isOverridden = manualOverride[i];
      const bool isCurrentlyOn = lightState[i];

      if (!isOverridden && !isCurrentlyOn) {
        lightState[i] = true;
        writeRelay(i, true);
        Serial.printf("[AUTO] Channel %u turned ON by motion.\n", i + 1);
      }
      portEXIT_CRITICAL(&stateMux);
    }
  } else {
    // Inactivity timeout: turn off lights after motion timeout expires
    if ((currentTime - lastMotionTime) > MOTION_TIMEOUT_MS) {
      for (uint8_t i = 0; i < NUM_LIGHTS; i++) {
        portENTER_CRITICAL(&stateMux);
        const bool isOverridden = manualOverride[i];
        const bool isCurrentlyOn = lightState[i];

        if (!isOverridden && isCurrentlyOn) {
          lightState[i] = false;
          writeRelay(i, false);
          Serial.printf("[AUTO] Channel %u turned OFF after inactivity.\n", i + 1);
        }
        portEXIT_CRITICAL(&stateMux);
      }
    }
  }

  // Room vacant reset: re-arm automation when room is quiet for timeout + reset delay
  const uint32_t totalResetWindow = MOTION_TIMEOUT_MS + OVERRIDE_RESET_DELAY_MS;
  if (!motionNow && ((currentTime - lastMotionTime) > totalResetWindow)) {
    bool hadActiveOverride = false;

    portENTER_CRITICAL(&stateMux);
    for (uint8_t i = 0; i < NUM_LIGHTS; i++) {
      if (manualOverride[i]) {
        manualOverride[i] = false;
        hadActiveOverride = true;
      }
    }
    portEXIT_CRITICAL(&stateMux);

    if (hadActiveOverride) {
      Serial.println("[AUTO] Room vacancy detected. All manual overrides cleared.");
    }
  }

  delay(LOOP_POLL_INTERVAL_MS);
}
