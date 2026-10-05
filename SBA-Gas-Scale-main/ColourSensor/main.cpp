#include <Arduino.h>
#include <Wire.h>
#include "Adafruit_TCS34725.h"

// ============================================================================
// HARDWARE PIN ASSIGNMENTS
// ============================================================================
// I2C Bus 0 (Sensor 1)
#define I2C0_SDA 21
#define I2C0_SCL 22

// I2C Bus 1 (Sensor 2)
#define I2C1_SDA 18
#define I2C1_SCL 19

// Unified Relay Outputs to PLC Inputs (Active-LOW modules)
#define RELAY_ACTIVE     LOW
#define RELAY_INACTIVE   HIGH

#define PLC_OUT_RED      13
#define PLC_OUT_GREEN    12
#define PLC_OUT_BLUE     14
#define PLC_OUT_YELLOW   27

// ============================================================================
// ENUMS & SENSOR OBJECTS
// ============================================================================
enum ColorResult { NONE, RED_OBJ, GREEN_OBJ, BLUE_OBJ, YELLOW_OBJ };

Adafruit_TCS34725 tcs1 = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X);
Adafruit_TCS34725 tcs2 = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X);

unsigned long lastSampleTime = 0;
const unsigned long SAMPLE_INTERVAL_MS = 60;

// ============================================================================
// UNIFIED COLOR DETECTION LOGIC
// ============================================================================
ColorResult detectUnifiedColor(int R, int G, int B, uint16_t C) {
  if (C < 280) return NONE;

  // 1. YELLOW OBJECT (High Red + High Green, low Blue)
  if (R >= 110 && G >= 85 && B <= 60 && abs(R - G) < 45) {
    return YELLOW_OBJ;
  }

  // 2. RED OBJECT (Dominant Red, low Green and Blue)
  if (R >= 160 && G <= 55 && B <= 55) {
    return RED_OBJ;
  }

  // 3. GREEN OBJECT (Dominant Green over Red, low Blue)
  if (G > R && G >= 100 && B <= 55) {
    return GREEN_OBJ;
  }

  // 4. BLUE OBJECT (Dominant Blue, high differential over Red)
  if (R <= 65 && (B - R) >= 30 && B > R && G > R) {
    return BLUE_OBJ;
  }

  return NONE;
}

// ============================================================================
// UNIFIED PLC RELAY OUTPUT CONTROLLER
// ============================================================================
void setPlcOutputs(ColorResult res) {
  // De-energize all coils first
  digitalWrite(PLC_OUT_RED, RELAY_INACTIVE);
  digitalWrite(PLC_OUT_GREEN, RELAY_INACTIVE);
  digitalWrite(PLC_OUT_BLUE, RELAY_INACTIVE);
  digitalWrite(PLC_OUT_YELLOW, RELAY_INACTIVE);

  // Energize designated line
  switch (res) {
    case RED_OBJ:
      digitalWrite(PLC_OUT_RED, RELAY_ACTIVE);
      break;

    case GREEN_OBJ:
      digitalWrite(PLC_OUT_GREEN, RELAY_ACTIVE);
      break;

    case BLUE_OBJ:
      digitalWrite(PLC_OUT_BLUE, RELAY_ACTIVE);
      break;

    case YELLOW_OBJ:
      digitalWrite(PLC_OUT_YELLOW, RELAY_ACTIVE);
      break;

    case NONE:
    default:
      break;
  }
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);

  // Configure Relay Pins
  uint8_t outPins[] = { PLC_OUT_RED, PLC_OUT_GREEN, PLC_OUT_BLUE, PLC_OUT_YELLOW };

  for (uint8_t pin : outPins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, RELAY_INACTIVE);
  }

  // Init Sensor 1 on I2C Bus 0
  Wire.begin(I2C0_SDA, I2C0_SCL, 100000);
  if (tcs1.begin(TCS34725_ADDRESS, &Wire)) {
    Serial.println(F("[INIT] TCS34725 #1 Online (Bus 0)"));
  } else {
    Serial.println(F("[ERROR] TCS34725 #1 Not Found on Bus 0"));
  }

  // Init Sensor 2 on I2C Bus 1
  Wire1.begin(I2C1_SDA, I2C1_SCL, 100000);
  if (tcs2.begin(TCS34725_ADDRESS, &Wire1)) {
    Serial.println(F("[INIT] TCS34725 #2 Online (Bus 1)"));
  } else {
    Serial.println(F("[ERROR] TCS34725 #2 Not Found on Bus 1"));
  }
}

// ============================================================================
// MAIN LOOP
// ============================================================================
void loop() {
  if (millis() - lastSampleTime >= SAMPLE_INTERVAL_MS) {
    lastSampleTime = millis();

    uint16_t r1, g1, b1, c1;
    uint16_t r2, g2, b2, c2;

    // Read raw data from both I2C buses
    tcs1.getRawData(&r1, &g1, &b1, &c1);
    tcs2.getRawData(&r2, &g2, &b2, &c2);

    // Dynamic Sensor Fusion: Average only valid active sensors
    uint16_t avgR, avgG, avgB, avgC;

    if (c1 > 50 && c2 > 50) {
      avgR = (r1 + r2) / 2;
      avgG = (g1 + g2) / 2;
      avgB = (b1 + b2) / 2;
      avgC = (c1 + c2) / 2;
    } else if (c1 > 50) {
      avgR = r1; avgG = g1; avgB = b1; avgC = c1;
    } else {
      avgR = r2; avgG = g2; avgB = b2; avgC = c2;
    }

    // Normalize merged channels against total clear intensity
    int nR = 0, nG = 0, nB = 0;
    if (avgC > 0) {
      nR = (avgR * 255) / avgC;
      nG = (avgG * 255) / avgC;
      nB = (avgB * 255) / avgC;
    }

    // Evaluate blended values
    ColorResult unifiedColor = detectUnifiedColor(nR, nG, nB, avgC);

    // Drive the PLC Relays
    setPlcOutputs(unifiedColor);

    // Diagnostics
    if (unifiedColor != NONE) {
      const char* names[] = {"NONE", "RED", "GREEN", "BLUE", "YELLOW"};
      Serial.printf("[FUSION RESULT] >> %s << | nR:%d nG:%d nB:%d | C:%d\n",
                    names[unifiedColor], nR, nG, nB, avgC);
    }
  }
}