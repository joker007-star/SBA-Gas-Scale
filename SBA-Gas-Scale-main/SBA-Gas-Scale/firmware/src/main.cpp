#include <Arduino.h>
#include <HardwareSerial.h>
#include "HX711.h"
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// ==========================================
// 1. HARDWARE PIN MAPPING
// ==========================================
#define SIM_TX 20         
#define SIM_RX 21         
#define MQ5_PIN 1         
#define HX711_DT 6
#define HX711_SCK 7
#define LED_PIN 8         
#define BUZZER_PIN 10     

// ==========================================
// 2. CALIBRATION & CONFIGURATION
// ==========================================
float F_WEIGHT = 230000.0;     
long  ADC_TARE = 8450000;      
int   MQ5_BASE = 255;          
int   MQ5_PEAK = 3800;         
const float GAS_ALARM_THRESHOLD = 30.0; 

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// ==========================================
// 3. SYSTEM OBJECTS & GLOBALS
// ==========================================
HardwareSerial SIM800(1);
HX711 scale;
Preferences preferences; 

String targetPhone = "+2348167952300"; // UPDATED NEW NUMBER
volatile bool alarmTriggered = false;     
volatile bool manualSirenState = false;   
unsigned long lastTelemetryTime = 0;

float smoothedGasADC = 0.0;
const float alpha = 0.15;

NimBLEServer* pServer = NULL;
NimBLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;

// ==========================================
// 4. FREERTOS HARDWARE ALARM TASK (UNSTOPPABLE)
// ==========================================
void alarmTaskCode(void * parameter) {
    for(;;) {
        if (alarmTriggered) {
            digitalWrite(BUZZER_PIN, LOW); 
            digitalWrite(LED_PIN, LOW);    
            vTaskDelay(250 / portTICK_PERIOD_MS);
            
            digitalWrite(BUZZER_PIN, LOW); 
            digitalWrite(LED_PIN, HIGH);   
            vTaskDelay(250 / portTICK_PERIOD_MS);
        } else {
            if (manualSirenState) {
                digitalWrite(BUZZER_PIN, LOW); 
            } else {
                digitalWrite(BUZZER_PIN, HIGH); 
            }
            digitalWrite(LED_PIN, HIGH); 
            vTaskDelay(100 / portTICK_PERIOD_MS); 
        }
    }
}

// ==========================================
// 5. GSM HELPER FUNCTIONS
// ==========================================
void clearSIMBuffer() {
    unsigned long start = millis();
    while(SIM800.available() && (millis() - start < 1000)) {
        SIM800.read();
        delay(1);
    }
}

void triggerEmergencyEscalation() {
    Serial.println("\n=============================================");
    Serial.println("[ALARM] INITIATING GSM ESCALATION SEQUENCE!");
    Serial.println("=============================================");
    
    Serial.println(">>> STEP 1: DISPATCHING SMS to " + targetPhone + "...");
    SIM800.println("AT+CMGF=1");
    delay(500);
    clearSIMBuffer();
    
    SIM800.println("AT+CMGS=\"" + targetPhone + "\"");
    delay(500);
    SIM800.print("CRITICAL: LPG Gas Leak Detected! Immediate Action Required.");
    delay(500);
    SIM800.write(26); 
    
    unsigned long smsTimer = millis();
    while(millis() - smsTimer < 10000) {
        if(SIM800.available()) { Serial.write(SIM800.read()); }
        delay(1); 
    }
    Serial.println("\n[SYSTEM] SMS Dispatch Complete.");
    
    Serial.print("[SYSTEM] Recharging internal GSM capacitor");
    for(int i = 0; i < 5; i++) { delay(1000); Serial.print("."); }
    Serial.println(" READY.");

    Serial.println(">>> STEP 2: INITIATING VOICE CALL...");
    SIM800.println("ATD" + targetPhone + ";");
    
    bool callAnswered = false;
    unsigned long callTimer = millis();
    int lastState = -1;
    
    while(millis() - callTimer < 45000) {
        SIM800.println("AT+CLCC"); 
        delay(1000); 
        
        String response = "";
        while(SIM800.available()) {
            char c = SIM800.read();
            response += c;
            delay(1); 
        }
        
        if(response.indexOf("+CLCC:") != -1) {
            if(response.indexOf(",0,2,0,0,") != -1 && lastState != 2) {
                Serial.println("[GSM STATUS] Dialing Network...");
                lastState = 2;
            }
            else if(response.indexOf(",0,3,0,0,") != -1 && lastState != 3) {
                Serial.println("[GSM STATUS] Phone is RINGING...");
                lastState = 3;
            }
            else if(response.indexOf(",0,0,0,0,") != -1) {
                Serial.println("\n>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
                Serial.println(">>> SUCCESS: CALL ANSWERED! <<<");
                Serial.println("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
                callAnswered = true;
                break; 
            }
        }
        
        if(response.indexOf("NO CARRIER") != -1 || response.indexOf("BUSY") != -1) {
            Serial.println("\n[GSM STATUS] Call was rejected, busy, or timed out.");
            break;
        }
    }

    if(callAnswered) {
        Serial.println("[SYSTEM] Keeping the line open to transmit background noise...");
        delay(5000);
    }
    
    Serial.println("[SYSTEM] Hanging up...");
    SIM800.println("ATH");
    Serial.println("=== ESCALATION PROTOCOL COMPLETE ===");
}

// ==========================================
// 6. BLUETOOTH APP COMMAND LISTENER
// ==========================================
class ServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) {
        Serial.println("[BLE] App Connected!");
        deviceConnected = true;
    };
    void onDisconnect(NimBLEServer* pServer) {
        Serial.println("[BLE] App Disconnected. Broadcasting...");
        deviceConnected = false;
        NimBLEDevice::startAdvertising();
    }
};

class CharacteristicCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar) {
        String payload = pChar->getValue().c_str();
        Serial.println("[BLE-RX] " + payload);

        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, payload);
        
        if (!error) {
            String cmd = doc["cmd"].as<String>();
            
            if (cmd == "TARE") {
                Serial.println("[SYSTEM] TARE Command Received.");
                if(scale.is_ready()) { ADC_TARE = scale.read_average(10); }
            } 
            else if (cmd == "SIREN") {
                manualSirenState = doc["state"].as<bool>();
                Serial.println(manualSirenState ? "[SYSTEM] Siren ENABLED via App" : "[SYSTEM] Siren MUTED via App");
            }
            else if (cmd == "PHONE") {
                String newPhone = doc["num"].as<String>();
                if (newPhone.length() > 5) {
                    targetPhone = newPhone;
                    preferences.putString("phone", targetPhone);
                    Serial.println("[SYSTEM] Recipient Phone Number Updated & Saved: " + targetPhone);
                }
            }
        }
    }
};

void setup() {
    Serial.begin(115200);
    delay(3000); 

    // Initialize Flash Preferences
    preferences.begin("sba-app", false);
    
    // WIPE OLD MEMORY ONCE TO FORCE THE NEW NUMBER
    preferences.clear(); 

    // Load the new target phone number
    targetPhone = preferences.getString("phone", "+2348167952300");

    Serial.println("\n=== SBA GAS DETECTOR: PRODUCTION FIRMWARE ===");
    Serial.println("[SYSTEM] Active Target Phone: " + targetPhone);

    // Init Sensors
    analogReadResolution(12);
    pinMode(MQ5_PIN, INPUT);
    
    pinMode(BUZZER_PIN, OUTPUT_OPEN_DRAIN);
    pinMode(LED_PIN, OUTPUT_OPEN_DRAIN);
    digitalWrite(BUZZER_PIN, HIGH); 
    digitalWatchLED: digitalWrite(LED_PIN, HIGH);    
    
    xTaskCreate(alarmTaskCode, "AlarmTask", 2048, NULL, 1, NULL);
    
    smoothedGasADC = analogRead(MQ5_PIN); 
    scale.begin(HX711_DT, HX711_SCK);

    // Init Bluetooth with Power Boost and explicit Name
    NimBLEDevice::init("SBA GAS DETECTOR");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); 
    
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());
    
    NimBLEService* pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
    );
    pCharacteristic->setCallbacks(new CharacteristicCallbacks());
    pService->start();
    
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->setName("SBA GAS DETECTOR");
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->start();
    
    // Init SIM800L
    SIM800.begin(9600, SERIAL_8N1, SIM_RX, SIM_TX); 
    
    Serial.print("[SYSTEM] Pinging SIM module...");
    bool simResponds = false;
    for(int i = 0; i < 10; i++) {
        clearSIMBuffer();
        SIM800.println("AT");
        delay(500);
        if(SIM800.find("OK")) { simResponds = true; break; }
        Serial.print(".");
    }
    
    if(!simResponds) { Serial.println("\n[ERROR] SIM DEAD."); } 
    else { Serial.println(" ALIVE."); }

    Serial.print("[SYSTEM] Waiting 15s for network lock");
    for(int i = 0; i < 15; i++) { delay(1000); Serial.print("."); }
    Serial.println(" DONE.\n");
}

void loop() {
    int rawGasADC = analogRead(MQ5_PIN);
    smoothedGasADC = (alpha * rawGasADC) + ((1.0 - alpha) * smoothedGasADC);
    
    float gasPercent = ((smoothedGasADC - MQ5_BASE) / (MQ5_PEAK - MQ5_BASE)) * 100.0;
    if(gasPercent < 0.0) gasPercent = 0.0;
    if(gasPercent > 100.0) gasPercent = 100.0;

    float liveWeightKg = 0.0;
    if (scale.is_ready()) {
        long rawWeightADC = scale.read_average(3);
        liveWeightKg = (float)(rawWeightADC - ADC_TARE) / F_WEIGHT;
        if(liveWeightKg < 0.0) liveWeightKg = 0.0; 
    }

    if (millis() - lastTelemetryTime > 2000) {
        StaticJsonDocument<128> txDoc;
        txDoc["g_pct"] = (int)(gasPercent * 10) / 10.0; 
        txDoc["w_kg"]  = (int)(liveWeightKg * 100) / 100.0; 
        txDoc["alrm"]  = alarmTriggered ? 1 : 0;
        
        String jsonOutput;
        serializeJson(txDoc, jsonOutput);
        Serial.println("[TX] " + jsonOutput);
        
        if (deviceConnected) {
            pCharacteristic->setValue(jsonOutput.c_str());
            pCharacteristic->notify();
        }
        lastTelemetryTime = millis();
    }

    if (gasPercent >= GAS_ALARM_THRESHOLD && !alarmTriggered) {
        alarmTriggered = true; 
        triggerEmergencyEscalation();
    }
    
    if (gasPercent < 15.0 && alarmTriggered) {
        alarmTriggered = false; 
        Serial.println("[SYSTEM] Gas cleared. System reset to SAFE state.");
    }

    delay(10); 
}