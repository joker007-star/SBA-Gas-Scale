#include <Arduino.h>
#include <HardwareSerial.h>
#include <NimBLEDevice.h> // Added BLE Library

// --- PINS & UUIDS ---
#define SIM_TX 20         
#define SIM_RX 21         

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

HardwareSerial SIM800(1);
String targetPhone = "+2348159408422";

// --- BLE SYSTEM OBJECTS ---
NimBLEServer* pServer = NULL;
NimBLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;

// BLE Connection Callbacks (Restarts broadcasting if app disconnects)
class ServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) {
        Serial.println("\n[BLE] App Connected!");
        deviceConnected = true;
    };
    void onDisconnect(NimBLEServer* pServer) {
        Serial.println("\n[BLE] App Disconnected. Broadcasting again...");
        deviceConnected = false;
        NimBLEDevice::startAdvertising();
    }
};

// Watchdog-proof buffer clearer. 
// It will forcibly exit after 1 second even if the pins pick up static noise.
void clearSIMBuffer() {
    unsigned long start = millis();
    while(SIM800.available() && (millis() - start < 1000)) {
        SIM800.read();
        delay(1);
    }
}

void setup() {
    Serial.begin(115200);
    delay(3000); 
    Serial.println("\n\n=== SBA GAS DETECTOR: COMMS & BLE SEQUENCE ===");

    // --- INITIALIZE BLE (OPEN/NO PASSWORD) ---
    Serial.println("[SYSTEM] Starting BLE Server...");
    NimBLEDevice::init("SBA GAS DETECTOR");
    NimBLEDevice::setMTU(512); // Prepped for large JSON payloads
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());
    
    NimBLEService* pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
    );
    
    pService->start();
    NimBLEDevice::getAdvertising()->start();
    Serial.println("[BLE] Broadcasting as 'SBA GAS DETECTOR' (No Password required)");


    // --- INITIALIZE SIM MODULE ---
    SIM800.begin(9600, SERIAL_8N1, SIM_RX, SIM_TX); 
    
    // --- STEP 1: CHECK IF SIM IS LISTENING ---
    Serial.print("\n[SYSTEM] Pinging SIM module...");
    bool simResponds = false;
    for(int i = 0; i < 10; i++) {
        clearSIMBuffer();
        SIM800.println("AT");
        delay(500);
        if(SIM800.find("OK")) {
            simResponds = true;
            break;
        }
        Serial.print(".");
    }
    
    if(!simResponds) {
        Serial.println("\n[ERROR] SIM DEAD.");
        return; 
    }
    Serial.println(" ALIVE.");

    // --- STEP 2: NETWORK LOCK WAIT ---
    Serial.print("[SYSTEM] Waiting 15s for network lock");
    for(int i = 0; i < 15; i++) {
        delay(1000);
        Serial.print(".");
    }
    Serial.println(" DONE.");

    // --- STEP 3: SEND SMS ---
    Serial.println("\n--- INITIATING SMS ---");
    SIM800.println("AT+CMGF=1");
    delay(500);
    clearSIMBuffer();
    
    SIM800.println("AT+CMGS=\"" + targetPhone + "\"");
    delay(500);
    SIM800.print("SBA GAS DETECTOR: System Fully Operational with BLE.");
    delay(500);
    SIM800.write(26); 
    
    unsigned long smsTimer = millis();
    while(millis() - smsTimer < 10000) {
        if(SIM800.available()) {
            Serial.write(SIM800.read()); 
        }
        delay(1); // 1ms yield prevents Watchdog panic
    }
    Serial.println("\n[SYSTEM] SMS phase complete.");

    // --- STEP 4: RECHARGE / COOLDOWN PAUSE ---
    Serial.print("\n[SYSTEM] 5-second capacitor recharge");
    for(int i = 0; i < 5; i++) {
        delay(1000);
        Serial.print(".");
    }
    Serial.println(" GO.");

    // --- STEP 5: VOICE CALL ---
    Serial.println("\n--- INITIATING VOICE CALL ---");
    SIM800.println("ATD" + targetPhone + ";");
    
    bool callAnswered = false;
    unsigned long callTimer = millis();
    
    while(millis() - callTimer < 45000) {
        SIM800.println("AT+CLCC"); 
        delay(1000); 
        
        String response = "";
        while(SIM800.available()) {
            char c = SIM800.read();
            response += c;
            Serial.write(c); 
            delay(1); // Yield while reading long strings
        }
        
        if(response.indexOf("+CLCC:") != -1 && response.indexOf(",0,0,0,") != -1) {
            Serial.println("\n\n>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
            Serial.println(">>> SUCCESS: CALL ANSWERED! <<<");
            Serial.println("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
            callAnswered = true;
            break; 
        }
        
        if(response.indexOf("NO CARRIER") != -1 || response.indexOf("BUSY") != -1) {
            Serial.println("\n[SYSTEM] Call failed, rejected, or ended.");
            break;
        }
    }

    if(callAnswered) {
        Serial.println("[SYSTEM] Keeping the line open for 5 seconds...");
        delay(5000);
    }
    
    Serial.println("[SYSTEM] Hanging up...");
    SIM800.println("ATH");
    Serial.println("=== TEST COMPLETE ===");
}

void loop() {
    if(SIM800.available()) { Serial.write(SIM800.read()); }
    if(Serial.available()) { SIM800.write(Serial.read()); }
    delay(1); 
}