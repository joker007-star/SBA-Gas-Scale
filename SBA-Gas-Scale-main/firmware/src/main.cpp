#include <Arduino.h>
#include <HX711.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <HardwareSerial.h>
#include <Preferences.h> // Added for permanent memory saving

// ==========================================
// 1. HARDWARE PIN MAPPING (ESP32-C3)
// ==========================================
#define DT_PIN 0          
#define SCK_PIN 1         
#define MQ5_PIN 3         
#define BUZZER_PIN 4      
#define BLE_LED 5         
#define ALARM_LED 7       
#define SIM_TX 20         
#define SIM_RX 21         

// ==========================================
// 2. SYSTEM GLOBALS & OBJECTS
// ==========================================
HX711 scale;
HardwareSerial SIM800(1);
BLECharacteristic *pCharacteristic;
Preferences prefs; // Memory object

// Hardware Calibration Variables
float loadcell_calibration_factor = 2280.0; 
float maxCylinderWeight = 6.0;         // Default, adjustable via App
String emergencyPhone = "+2340000000000"; // Default, adjustable via App

// State Tracking
bool deviceConnected = false;
bool isLeaking = false;
bool smsSent = false;
bool callEscalated = false;
bool alarmAcknowledged = false;

// Timers
unsigned long lastSensorPoll = 0;
unsigned long leakStartTime = 0;
unsigned long lastDailyReport = 0;

const unsigned long THREE_MINUTES = 180000;    
const unsigned long TWENTY_FOUR_HOURS = 86400000; 

// ==========================================
// 3. BLE CALLBACKS & COMMAND PARSER
// ==========================================
class ServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        digitalWrite(BLE_LED, HIGH);
        ledcWriteTone(1, 2500); delay(150); ledcWriteTone(1, 0); 
        Serial.println("[BLE] Dashboard Connected");
    }
    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        digitalWrite(BLE_LED, LOW);
        BLEDevice::startAdvertising();
        Serial.println("[BLE] Dashboard Disconnected");
    }
};

class CharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        std::string rxValue = pCharacteristic->getValue();
        if (rxValue.length() > 0) {
            String cmd = String(rxValue.c_str());
            cmd.trim(); // Remove invisible whitespace

            // A. Execute Tare Calibration
            if (cmd == "TARE") {
                scale.tare(20); // Average 20 readings for deep accuracy
                prefs.putLong("tare_offset", scale.get_offset()); // Save to permanent memory
                Serial.println("[BLE] Scale Zeroed and Saved to NVS.");
            } 
            // B. Set Maximum Cylinder Capacity
            else if (cmd.startsWith("MAX:")) {
                float newMax = cmd.substring(4).toFloat();
                if(newMax > 0) {
                    maxCylinderWeight = newMax;
                    prefs.putFloat("max_weight", maxCylinderWeight);
                    Serial.print("[BLE] Max Weight Updated: "); Serial.println(maxCylinderWeight);
                }
            } 
            // C. Set Emergency Phone Number
            else if (cmd.startsWith("NUM:")) {
                String newPhone = cmd.substring(4);
                if(newPhone.startsWith("+")) {
                    emergencyPhone = newPhone;
                    prefs.putString("phone", emergencyPhone);
                    Serial.print("[BLE] Emergency Number Updated: "); Serial.println(emergencyPhone);
                }
            }
        }
    }
};

// ==========================================
// 4. GSM CONTROL FUNCTIONS
// ==========================================
void sendSMS(String message) {
    SIM800.println("AT+CMGF=1"); 
    delay(200);
    // Dynamically insert the phone number saved in memory
    SIM800.println("AT+CMGS=\"" + emergencyPhone + "\""); 
    delay(200);
    SIM800.print(message);
    delay(200);
    SIM800.write(26); // Send Ctrl+Z
}

void makeEmergencyCall() {
    // Dynamically dial the saved phone number
    SIM800.println("ATD" + emergencyPhone + ";"); 
    
    unsigned long callTimer = millis();
    while(millis() - callTimer < 45000) { 
        if(SIM800.available()) {
            String response = SIM800.readString();
            if(response.indexOf("OK") != -1 || response.indexOf("CONNECT") != -1) {
                alarmAcknowledged = true; 
                break;
            }
            if(response.indexOf("NO CARRIER") != -1 || response.indexOf("BUSY") != -1) break; 
        }
    }
    SIM800.println("ATH"); 
}

void sendDailyStatusReport(float currentWeight, String sensorState) {
    SIM800.println("AT+CUSD=1,\"*310#\",15");
    String ussdResponse = "Balance Unavailable";
    
    unsigned long ussdWait = millis();
    while (millis() - ussdWait < 15000) {
        if (SIM800.available()) {
            String response = SIM800.readString();
            if (response.indexOf("+CUSD:") != -1) {
                ussdResponse = response;
                break;
            }
        }
    }

    String report = "SBA GAS DETECTOR - 24H REPORT\n";
    report += "Weight: " + String(currentWeight, 2) + " kg\n";
    report += "Sensor: " + sensorState + "\n";
    report += "Net: " + ussdResponse;
    sendSMS(report);
}

// ==========================================
// 5. SYSTEM SETUP
// ==========================================
void setup() {
    Serial.begin(115200);
    
    // 1. Initialize NVS Memory and Load Settings
    prefs.begin("sba_app", false);
    emergencyPhone = prefs.getString("phone", "+2340000000000"); // Load saved number
    maxCylinderWeight = prefs.getFloat("max_weight", 6.0);       // Load saved capacity
    long savedTareOffset = prefs.getLong("tare_offset", 0);      // Load saved tare

    // 2. Initialize Hardware Pins
    pinMode(MQ5_PIN, INPUT);
    pinMode(BLE_LED, OUTPUT);
    pinMode(ALARM_LED, OUTPUT);
    ledcAttachPin(BUZZER_PIN, 1);

    // 3. Initialize GSM
    SIM800.begin(9600, SERIAL_8N1, SIM_RX, SIM_TX); 
    
    // 4. Initialize Scale
    scale.begin(DT_PIN, SCK_PIN);
    scale.set_scale(loadcell_calibration_factor);
    
    // Apply the saved tare offset if it exists, otherwise tare now
    if (savedTareOffset != 0) {
        scale.set_offset(savedTareOffset);
    } else {
        scale.tare(); 
    }

    // 5. Initialize BLE
    BLEDevice::init("SBA GAS DETECTOR");
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());
    BLEService *pService = pServer->createService("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
    pCharacteristic = pService->createCharacteristic("beb5483e-36e1-4688-b7f5-ea07361b26a8", 
                      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_WRITE);
    pCharacteristic->setCallbacks(new CharacteristicCallbacks());
    pService->start();
    BLEDevice::startAdvertising();

    lastDailyReport = millis();
    Serial.println("[BOOT] System Ready. Awaiting Sensor Data...");
}

// ==========================================
// 6. MAIN SUPER-LOOP
// ==========================================
void loop() {
    unsigned long currentMillis = millis();

    // -- A. 24-HOUR AUTOMATED REPORTING --
    if (currentMillis - lastDailyReport >= TWENTY_FOUR_HOURS) {
        float netWeight = scale.get_units(5);
        if (netWeight < 0) netWeight = 0;
        sendDailyStatusReport(netWeight, isLeaking ? "CRITICAL LEAK" : "NORMAL");
        lastDailyReport = currentMillis; 
    }

    // -- B. SENSOR POLLING (500ms) --
    if (currentMillis - lastSensorPoll >= 500) {
        lastSensorPoll = currentMillis;

        // Gas Math
        int gasRaw = analogRead(MQ5_PIN); 
        float gasPercentage = (gasRaw / 4095.0) * 100.0; 

        // Weight Math
        float netWeight = scale.get_units(5); 
        if (netWeight < 0) netWeight = 0; 

        // -- C. ESCALATION LOGIC --
        if (gasPercentage > 60.0) { // THRESHOLD EXCEEDED
            
            if (!isLeaking) {
                isLeaking = true;
                leakStartTime = currentMillis;
                smsSent = false;
                callEscalated = false;
                alarmAcknowledged = false;
                Serial.println("[ALARM] Gas Threshold Exceeded! Initiating Escalation.");
            }

            digitalWrite(ALARM_LED, HIGH);
            if (!alarmAcknowledged) ledcWriteTone(1, 2500); 
            else ledcWriteTone(1, 0); 

            if (!smsSent) {
                sendSMS("GAS LEVEL THRESHOLD EXCEEDED");
                smsSent = true;
            }

            if (!callEscalated && (currentMillis - leakStartTime >= THREE_MINUTES)) {
                Serial.println("[ALARM] 3 Minutes Elapsed. Initiating Voice Call.");
                makeEmergencyCall(); 
                callEscalated = true;
            }

        } else { 
            // GAS DROPPED BELOW THRESHOLD
            isLeaking = false;
            smsSent = false;
            callEscalated = false;
            alarmAcknowledged = false;
            
            digitalWrite(ALARM_LED, LOW);
            ledcWriteTone(1, 0); 
        }

        // -- D. BLE TELEMETRY STREAM --
        if (deviceConnected) {
            String payload = "DATA:" + String(gasPercentage, 1) + "," + String(netWeight, 2) + "," + String(isLeaking ? 2 : 0);
            pCharacteristic->setValue(payload.c_str());
            pCharacteristic->notify();
        }
    }

    // -- E. GSM SERIAL PASSTHROUGH FOR DEBUGGING --
    while (SIM800.available()) { Serial.write(SIM800.read()); }
    while (Serial.available()) { SIM800.write(Serial.read()); }
}
