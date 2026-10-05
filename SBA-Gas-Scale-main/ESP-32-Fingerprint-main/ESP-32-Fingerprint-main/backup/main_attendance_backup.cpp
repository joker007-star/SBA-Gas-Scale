#include <Arduino.h>
#include <Adafruit_Fingerprint.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>

// --- Pin Definitions ---
#define FINGER_RX      16
#define FINGER_TX      17
#define FINGER_TOUCH   13

#define SD_CS          5

#define RTC_CLK        14
#define RTC_DAT        27
#define RTC_RST        25


#define LED_SCAN       4
#define LED_SUCCESS    26

// --- Component Objects ---
HardwareSerial mySerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
LiquidCrystal_I2C lcd(0x27, 20, 4);
WebServer server(80);

ThreeWire myWire(RTC_DAT, RTC_CLK, RTC_RST); // IO, SCLK, CE
RtcDS1302<ThreeWire> Rtc(myWire);

// --- State Variables ---
bool isEnrolling = false;
int enrollID = 0;
String enrollName = "";
String enrollMat = "";
int enrollStep = 0;
String enrollStatusMsg = "Idle";
unsigned long enrollTimer = 0;

// --- HTML / CSS / JS Single Page Application Dashboard ---
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Attendance System</title>
    <style>
        :root {
            --bg-color: #0b0f19;
            --card-bg: rgba(255, 255, 255, 0.05);
            --border-color: rgba(255, 255, 255, 0.1);
            --primary: #4f46e5;
            --primary-hover: #4338ca;
            --accent-green: #10b981;
            --accent-cyan: #06b6d4;
            --text-main: #f3f4f6;
            --text-muted: #9ca3af;
        }
        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
            font-family: 'Inter', -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
        }
        body {
            background-color: var(--bg-color);
            color: var(--text-main);
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            align-items: center;
            padding: 2rem 1rem;
        }
        header {
            margin-bottom: 2rem;
            text-align: center;
        }
        header h1 {
            font-size: 2.5rem;
            font-weight: 800;
            background: linear-gradient(135deg, #a5b4fc, #6366f1, #38bdf8);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            margin-bottom: 0.5rem;
        }
        header p {
            color: var(--text-muted);
            font-size: 1rem;
        }
        .container {
            width: 100%;
            max-width: 960px;
            display: flex;
            flex-direction: column;
            gap: 2rem;
        }
        .tabs {
            display: flex;
            background: var(--card-bg);
            padding: 0.25rem;
            border-radius: 12px;
            border: 1px solid var(--border-color);
        }
        .tab-btn {
            flex: 1;
            background: none;
            border: none;
            color: var(--text-muted);
            padding: 0.75rem 1rem;
            font-size: 1rem;
            font-weight: 600;
            cursor: pointer;
            border-radius: 8px;
            transition: all 0.3s ease;
        }
        .tab-btn.active {
            background: var(--primary);
            color: white;
            box-shadow: 0 4px 12px rgba(79, 70, 229, 0.3);
        }
        .card {
            background: var(--card-bg);
            border-radius: 16px;
            border: 1px solid var(--border-color);
            padding: 2rem;
            backdrop-filter: blur(12px);
            box-shadow: 0 8px 32px rgba(0, 0, 0, 0.3);
        }
        .card-title {
            font-size: 1.5rem;
            font-weight: 700;
            margin-bottom: 1.5rem;
            display: flex;
            align-items: center;
            gap: 0.5rem;
        }
        .form-group {
            margin-bottom: 1.25rem;
        }
        .form-group label {
            display: block;
            font-size: 0.875rem;
            font-weight: 600;
            color: var(--text-muted);
            margin-bottom: 0.5rem;
        }
        .form-group input, .form-group select {
            width: 100%;
            padding: 0.75rem 1rem;
            background: rgba(0, 0, 0, 0.2);
            border: 1px solid var(--border-color);
            border-radius: 8px;
            color: white;
            font-size: 1rem;
            transition: all 0.3s ease;
        }
        .form-group input:focus, .form-group select:focus {
            outline: none;
            border-color: var(--primary);
            box-shadow: 0 0 0 2px rgba(79, 70, 229, 0.2);
        }
        .btn {
            width: 100%;
            padding: 0.75rem 1.5rem;
            background: var(--primary);
            color: white;
            border: none;
            border-radius: 8px;
            font-size: 1rem;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.3s ease;
        }
        .btn:hover {
            background: var(--primary-hover);
        }
        .btn:disabled {
            background: var(--text-muted);
            cursor: not-allowed;
        }
        .status-box {
            background: rgba(0, 0, 0, 0.3);
            border-radius: 8px;
            padding: 1rem;
            margin-top: 1rem;
            border-left: 4px solid var(--accent-cyan);
            display: none;
        }
        .status-box.active {
            display: block;
        }
        .status-text {
            font-weight: 600;
        }
        table {
            width: 100%;
            border-collapse: collapse;
            margin-top: 1rem;
            text-align: left;
        }
        th, td {
            padding: 1rem;
            border-bottom: 1px solid var(--border-color);
        }
        th {
            color: var(--text-muted);
            font-weight: 600;
            text-transform: uppercase;
            font-size: 0.75rem;
            letter-spacing: 0.05em;
        }
        tr:hover td {
            background: rgba(255, 255, 255, 0.02);
        }
        .tab-content {
            display: none;
        }
        .tab-content.active {
            display: block;
        }
        .badge {
            display: inline-block;
            padding: 0.25rem 0.5rem;
            border-radius: 6px;
            font-size: 0.75rem;
            font-weight: 700;
        }
        .badge-success {
            background: rgba(16, 185, 129, 0.2);
            color: var(--accent-green);
        }
    </style>
</head>
<body>
    <header>
        <h1>Smart Attendance</h1>
        <p>ESP32 Fingerprint & RTC Test Dashboard</p>
    </header>
    <div class="container">
        <div class="tabs">
            <button class="tab-btn active" onclick="switchTab('attendance')">Attendance Logs</button>
            <button class="tab-btn" onclick="switchTab('enroll')">Enroll Student</button>
            <button class="tab-btn" onclick="switchTab('courses')">Register Course</button>
        </div>

        <!-- Attendance Logs Tab -->
        <div id="tab-attendance" class="tab-content active">
            <div class="card">
                <div class="card-title">Live Attendance Logs</div>
                <div style="overflow-x: auto;">
                    <table>
                        <thead>
                            <tr>
                                <th>Student Name</th>
                                <th>Matric No</th>
                                <th>Course Code</th>
                                <th>Date & Time</th>
                            </tr>
                        </thead>
                        <tbody id="logs-table">
                            <tr>
                                <td colspan="4" style="text-align: center; color: var(--text-muted);">Loading logs...</td>
                            </tr>
                        </tbody>
                    </table>
                </div>
            </div>
        </div>

        <!-- Enroll Student Tab -->
        <div id="tab-enroll" class="tab-content">
            <div class="card">
                <div class="card-title">Enroll Student Fingerprint</div>
                <form id="enroll-form" onsubmit="startEnrollment(event)">
                    <div class="form-group">
                        <label for="student-name">Full Name</label>
                        <input type="text" id="student-name" required placeholder="e.g. John Doe">
                    </div>
                    <div class="form-group">
                        <label for="student-mat">Matric Number</label>
                        <input type="text" id="student-mat" required placeholder="e.g. EEE/2021/045">
                    </div>
                    <div class="form-group">
                        <label for="finger-id">Fingerprint ID (1 - 127)</label>
                        <input type="number" id="finger-id" min="1" max="127" required placeholder="Select a slot">
                    </div>
                    <button type="submit" class="btn" id="enroll-submit-btn">Start Enrollment</button>
                </form>
                <div id="enroll-status-box" class="status-box">
                    <p>Status: <span id="enroll-status-text" class="status-text">Ready</span></p>
                </div>
            </div>
        </div>

        <!-- Register Course Tab -->
        <div id="tab-courses" class="tab-content">
            <div class="card">
                <div class="card-title">Register Course Details</div>
                <form id="course-form" onsubmit="registerCourse(event)">
                    <div class="form-group">
                        <label for="course-code">Course Code</label>
                        <input type="text" id="course-code" required placeholder="e.g. EEC 431">
                    </div>
                    <div class="form-group">
                        <label for="course-name">Course Name</label>
                        <input type="text" id="course-name" required placeholder="e.g. Microcomputer Technology">
                    </div>
                    <div class="form-group">
                        <label for="lecturer-name">Lecturer(s) Name</label>
                        <input type="text" id="lecturer-name" required placeholder="e.g. Dr. A. Smith">
                    </div>
                    <button type="submit" class="btn">Register Course</button>
                </form>
            </div>
        </div>
    </div>

    <script>
        let enrollInterval = null;

        function switchTab(tabId) {
            document.querySelectorAll('.tab-btn').forEach(btn => btn.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
            
            // Activate clicked tab
            event.target.classList.add('active');
            document.getElementById('tab-' + tabId).classList.add('active');

            if (tabId === 'attendance') {
                loadLogs();
            }
        }

        function loadLogs() {
            fetch('/attendance-logs')
                .then(r => r.json())
                .then(logs => {
                    const tbody = document.getElementById('logs-table');
                    tbody.innerHTML = '';
                    if (logs.length === 0) {
                        tbody.innerHTML = '<tr><td colspan="4" style="text-align: center; color: var(--text-muted);">No logs recorded yet.</td></tr>';
                        return;
                    }
                    logs.forEach(log => {
                        const tr = document.createElement('tr');
                        tr.innerHTML = `
                            <td>${log.name || 'Unknown'}</td>
                            <td>${log.mat || 'Unknown'}</td>
                            <td>${log.course || 'N/A'}</td>
                            <td>${log.time || 'N/A'}</td>
                        `;
                        tbody.appendChild(tr);
                    });
                })
                .catch(() => {
                    document.getElementById('logs-table').innerHTML = '<tr><td colspan="4" style="text-align: center; color: red;">Failed to load logs.</td></tr>';
                });
        }

        function startEnrollment(event) {
            event.preventDefault();
            const name = document.getElementById('student-name').value;
            const mat = document.getElementById('student-mat').value;
            const id = document.getElementById('finger-id').value;

            document.getElementById('enroll-submit-btn').disabled = true;
            const statusBox = document.getElementById('enroll-status-box');
            const statusText = document.getElementById('enroll-status-text');
            statusBox.className = 'status-box active';
            statusText.innerText = 'Initializing...';

            fetch('/enroll', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: `id=${id}&name=${encodeURIComponent(name)}&mat=${encodeURIComponent(mat)}`
            })
            .then(res => res.text())
            .then(() => {
                // Poll status
                if (enrollInterval) clearInterval(enrollInterval);
                enrollInterval = setInterval(checkEnrollStatus, 1000);
            });
        }

        function checkEnrollStatus() {
            fetch('/enroll-status')
                .then(r => r.json())
                .then(status => {
                    const statusText = document.getElementById('enroll-status-text');
                    statusText.innerText = status.msg;
                    
                    if (status.step === 0) {
                        clearInterval(enrollInterval);
                        document.getElementById('enroll-submit-btn').disabled = false;
                        if (status.msg.includes('successful')) {
                            document.getElementById('enroll-form').reset();
                        }
                    }
                });
        }

        function registerCourse(event) {
            event.preventDefault();
            const code = document.getElementById('course-code').value;
            const name = document.getElementById('course-name').value;
            const lecturer = document.getElementById('lecturer-name').value;

            fetch('/register-course', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: `code=${encodeURIComponent(code)}&name=${encodeURIComponent(name)}&lecturer=${encodeURIComponent(lecturer)}`
            })
            .then(res => res.text())
            .then(msg => {
                alert(msg);
                document.getElementById('course-form').reset();
            });
        }

        // Load initially
        loadLogs();
    </script>
</body>
</html>
)rawliteral";

// --- Helper Functions ---

String getFormattedTime() {
    if (!Rtc.IsDateTimeValid()) {
        return "Unknown Time";
    }
    RtcDateTime dt = Rtc.GetDateTime();
    char buf[20];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             dt.Year(), dt.Month(), dt.Day(),
             dt.Hour(), dt.Minute(), dt.Second());
    return String(buf);
}

// Custom split helper to parse CSV lines without complex string tokenizer library overhead
int split(String data, char separator, String* temp, int maxItems) {
    int index = 0;
    int from = 0;
    while (index < maxItems) {
        int to = data.indexOf(separator, from);
        if (to == -1) {
            temp[index++] = data.substring(from);
            break;
        }
        temp[index++] = data.substring(from, to);
        from = to + 1;
    }
    return index;
}

// Lookup Student Details by Matric number or ID (using simple lookup)
void lookupStudent(int fingerId, String &outName, String &outMat) {
    outName = "Unknown Student";
    outMat = "N/A";
    
    File file = SD.open("/students.csv", FILE_READ);
    if (!file) return;

    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        
        String parts[3];
        int numParts = split(line, ',', parts, 3);
        if (numParts >= 3 && parts[0].toInt() == fingerId) {
            outName = parts[1];
            outMat = parts[2];
            break;
        }
    }
    file.close();
}

// Web Server API Endpoints
void handleRoot() {
    server.send_P(200, "text/html", INDEX_HTML);
}

void handleEnroll() {
    if (!server.hasArg("id") || !server.hasArg("name") || !server.hasArg("mat")) {
        server.send(400, "text/plain", "Missing arguments");
        return;
    }
    enrollID = server.arg("id").toInt();
    enrollName = server.arg("name");
    enrollMat = server.arg("mat");

    isEnrolling = true;
    enrollStep = 1;
    enrollStatusMsg = "Please place your finger on the sensor...";
    enrollTimer = millis();

    server.send(200, "text/plain", "Enrollment started");
}

void handleEnrollStatus() {
    String json = "{\"step\":" + String(enrollStep) + ",\"msg\":\"" + enrollStatusMsg + "\"}";
    server.send(200, "application/json", json);
}

void handleRegisterCourse() {
    if (!server.hasArg("code") || !server.hasArg("name") || !server.hasArg("lecturer")) {
        server.send(400, "text/plain", "Missing arguments");
        return;
    }
    String code = server.arg("code");
    String name = server.arg("name");
    String lecturer = server.arg("lecturer");

    File file = SD.open("/courses.csv", FILE_APPEND);
    if (file) {
        file.print(code);
        file.print(",");
        file.print(name);
        file.print(",");
        file.println(lecturer);
        file.close();
        server.send(200, "text/plain", "Course registered successfully!");
    } else {
        server.send(500, "text/plain", "Failed to write to SD card");
    }
}

void handleGetLogs() {
    File file = SD.open("/attendance.csv", FILE_READ);
    String json = "[";
    
    if (file) {
        bool first = true;
        while (file.available()) {
            String line = file.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            String parts[3];
            int numParts = split(line, ',', parts, 3);
            if (numParts >= 3) {
                int fingerId = parts[0].toInt();
                String courseCode = parts[1];
                String timestamp = parts[2];

                String name, mat;
                lookupStudent(matchId, name, mat);

                if (!first) json += ",";
                json += "{\"name\":\"" + name + "\",\"mat\":\"" + mat + "\",\"course\":\"" + courseCode + "\",\"time\":\"" + timestamp + "\"}";
                first = false;
            }
        }
        file.close();
    }
    json += "]";
    server.send(200, "application/json", json);
}

// Non-blocking Fingerprint Enrollment Process
void runEnrollmentStateMachine() {
    if (!isEnrolling) return;

    // Timeout enrollment if inactive for 30 seconds
    if (millis() - enrollTimer > 30000) {
        enrollStatusMsg = "Enrollment timed out.";
        Serial.println("[Enrollment] Enrollment timed out after 30 seconds.");
        isEnrolling = false;
        enrollStep = 0;
        return;
    }

    int p = -1;
    switch (enrollStep) {
        case 1: // Get first image
            p = finger.getImage();
            if (p == FINGERPRINT_OK) {
                Serial.println("[Enrollment] Fingerprint sensed! Image 1 captured.");
                p = finger.image2Tz(1);
                if (p == FINGERPRINT_OK) {
                    enrollStatusMsg = "Finger image captured. Remove your finger...";
                    Serial.println("[Enrollment] Image 1 converted successfully. Please remove finger.");
                    enrollStep = 2;
                    enrollTimer = millis();
                } else {
                    enrollStatusMsg = "Image conversion error. Try again.";
                    Serial.println("[Enrollment] Image 1 conversion error.");
                    isEnrolling = false;
                    enrollStep = 0;
                }
            } else if (p != FINGERPRINT_NOFINGER) {
                enrollStatusMsg = "Sensor error. Try again.";
                Serial.printf("[Enrollment] Sensor error code: %d\n", p);
                isEnrolling = false;
                enrollStep = 0;
            }
            break;

        case 2: // Wait for finger removal
            p = finger.getImage();
            if (p == FINGERPRINT_NOFINGER) {
                enrollStatusMsg = "Place the same finger again...";
                Serial.println("[Enrollment] Finger removed. Please place the same finger again...");
                enrollStep = 3;
                enrollTimer = millis();
            }
            break;

        case 3: // Get second image
            p = finger.getImage();
            if (p == FINGERPRINT_OK) {
                Serial.println("[Enrollment] Fingerprint sensed! Image 2 captured.");
                p = finger.image2Tz(2);
                if (p == FINGERPRINT_OK) {
                    // Create model
                    p = finger.createModel();
                    if (p == FINGERPRINT_OK) {
                        // Store model
                        p = finger.storeModel(enrollID);
                        if (p == FINGERPRINT_OK) {
                            // Success! Save to SD card
                            File file = SD.open("/students.csv", FILE_APPEND);
                            if (file) {
                                file.print(enrollID);
                                file.print(",");
                                file.print(enrollName);
                                file.print(",");
                                file.println(enrollMat);
                                file.close();
                                enrollStatusMsg = "Enrollment successful!";
                                Serial.printf("[Enrollment] SUCCESS! Fingerprint stored with ID #%d (%s, %s)\n", enrollID, enrollName.c_str(), enrollMat.c_str());
                            } else {
                                enrollStatusMsg = "Saved to sensor, but SD card write failed.";
                                Serial.printf("[Enrollment] Stored in sensor ID #%d, but SD write failed.\n", enrollID);
                            }
                        } else {
                            enrollStatusMsg = "Failed to store fingerprint model.";
                            Serial.printf("[Enrollment] Store model failed with error: %d\n", p);
                        }
                    } else {
                        enrollStatusMsg = "Fingerprint mismatch. Try again.";
                        Serial.println("[Enrollment] Fingerprint mismatch between samples.");
                    }
                    isEnrolling = false;
                    enrollStep = 0;
                } else {
                    enrollStatusMsg = "Image conversion error. Try again.";
                    Serial.println("[Enrollment] Image 2 conversion error.");
                    isEnrolling = false;
                    enrollStep = 0;
                }
            } else if (p != FINGERPRINT_NOFINGER) {
                enrollStatusMsg = "Sensor error. Try again.";
                Serial.printf("[Enrollment] Sensor error code: %d\n", p);
                isEnrolling = false;
                enrollStep = 0;
            }
            break;
    }
}

void setup() {
    Serial.begin(115200);

    // GPIO Setup
    pinMode(LED_SCAN, OUTPUT);
    pinMode(LED_SUCCESS, OUTPUT);

    // LCD Setup
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("System Setup...");

    // RTC Setup
    Rtc.Begin();
    RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
    if (!Rtc.IsDateTimeValid()) {
        Rtc.SetDateTime(compiled);
    }
    if (Rtc.GetIsWriteProtected()) {
        Rtc.SetIsWriteProtected(false);
    }
    if (!Rtc.GetIsRunning()) {
        Rtc.SetIsRunning(true);
    }
    // Set system time to compile time if the RTC was reset
    RtcDateTime now = Rtc.GetDateTime();
    if (now < compiled) {
        Rtc.SetDateTime(compiled);
    }

    // SD Card Setup
    if (!SD.begin(SD_CS)) {
        lcd.setCursor(0, 1);
        lcd.print("SD Card Fail!");
        Serial.println("SD Card initialization failed!");
    } else {
        Serial.println("SD Card ready.");
        // Ensure necessary CSV files exist
        if (!SD.exists("/students.csv")) {
            File f = SD.open("/students.csv", FILE_WRITE);
            f.close();
        }
        if (!SD.exists("/courses.csv")) {
            File f = SD.open("/courses.csv", FILE_WRITE);
            f.close();
        }
        if (!SD.exists("/attendance.csv")) {
            File f = SD.open("/attendance.csv", FILE_WRITE);
            f.close();
        }
    }

    // Fingerprint Sensor Setup
    finger.begin(57600);
    if (!finger.verifyPassword()) {
        lcd.setCursor(0, 2);
        lcd.print("Sensor Fail!");
        Serial.println("[ERROR] Fingerprint sensor NOT found! Check RX(16) / TX(17) wiring.");
    } else {
        Serial.println("[SUCCESS] Fingerprint sensor initialized.");
        finger.getTemplateCount();
        Serial.print("[Info] Enrolled fingerprint templates in sensor memory: ");
        Serial.println(finger.templateCount);
    }

    // Wi-Fi Access Point Mode (SoftAP)
    WiFi.softAP("ESP32-Attendance-System", "12345678");
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP Address: ");
    Serial.println(IP);

    lcd.clear();
    lcd.print("Connect Wi-Fi:");
    lcd.setCursor(0, 1);
    lcd.print("SSID: ESP32-Attend...");
    lcd.setCursor(0, 2);
    lcd.print("IP: 192.168.4.1");

    // Web Server Endpoints
    server.on("/", HTTP_GET, handleRoot);
    server.on("/enroll", HTTP_POST, handleEnroll);
    server.on("/enroll-status", HTTP_GET, handleEnrollStatus);
    server.on("/register-course", HTTP_POST, handleRegisterCourse);
    server.on("/attendance-logs", HTTP_GET, handleGetLogs);
    
    server.begin();
    Serial.println("Web server started.");
    Serial.println("==================================================");
    Serial.println("   ESP32 FINGERPRINT SYSTEM READY & LISTENING     ");
    Serial.println("==================================================");
}

static unsigned long lastHeartbeat = 0;

void loop() {
    server.handleClient();
    runEnrollmentStateMachine();

    // Heartbeat status message to Serial Monitor every 5 seconds when idle
    if (!isEnrolling && millis() - lastHeartbeat > 5000) {
        lastHeartbeat = millis();
        Serial.println("[Status] System active & waiting for fingerprint...");
    }

    // Regular Mode: If we are not actively enrolling a student, look for scans
    if (!isEnrolling) {
        digitalWrite(LED_SCAN, HIGH);
        
        // Scan fingerprint
        uint8_t p = finger.getImage();
        if (p == FINGERPRINT_OK) {
            Serial.println("");
            Serial.println("==================================================");
            Serial.println("   >>> [FINGERPRINT DETECTED ON SENSOR!] <<<     ");
            Serial.println("==================================================");
            
            p = finger.image2Tz();
            if (p == FINGERPRINT_OK) {
                p = finger.fingerFastSearch();
                if (p == FINGERPRINT_OK) {
                    // Match found!
                    digitalWrite(LED_SCAN, LOW);
                    digitalWrite(LED_SUCCESS, HIGH);
                    
                    int matchId = finger.fingerID;
                    int confidence = finger.confidence;
                    String name, mat;
                    lookupStudent(matchId, name, mat);

                    Serial.println("STATUS: MATCH FOUND!");
                    Serial.printf("  Finger ID  : #%d\n", matchId);
                    Serial.printf("  Confidence : %d\n", confidence);
                    Serial.printf("  Name       : %s\n", name.c_str());
                    Serial.printf("  Matric No  : %s\n", mat.c_str());
                    Serial.printf("  Timestamp  : %s\n", getFormattedTime().c_str());
                    Serial.println("--------------------------------------------------");

                    lcd.clear();
                    lcd.print("Welcome!");
                    lcd.setCursor(0, 1);
                    lcd.print(name.substring(0, 20));
                    lcd.setCursor(0, 2);
                    lcd.print(mat.substring(0, 20));

                    // Log to SD Card
                    File file = SD.open("/attendance.csv", FILE_APPEND);
                    if (file) {
                        file.print(matchId);
                        file.print(",");
                        file.print("EEC 431"); // Hardcoded fallback for default class testing
                        file.print(",");
                        file.println(getFormattedTime());
                        file.close();
                        Serial.println("[SD Log] Attendance record saved successfully.");
                    } else {
                        Serial.println("[SD Log] ERROR: Failed to open /attendance.csv on SD card!");
                    }

                    delay(2500);
                    digitalWrite(LED_SUCCESS, LOW);
                    
                    // Reset LCD
                    lcd.clear();
                    lcd.print("AP IP: 192.168.4.1");
                    lcd.setCursor(0, 1);
                    lcd.print("Place Finger...");
                    Serial.println("==================================================");
                    Serial.println("[Ready] System reset. Waiting for next finger...");
                    Serial.println("==================================================");
                } else if (p == FINGERPRINT_NOTFOUND) {
                    Serial.println("STATUS: FINGER SENSED, BUT NO MATCH FOUND (Unknown Finger)");
                    Serial.println("This fingerprint is not registered in sensor memory.");
                    Serial.println("--------------------------------------------------");
                    delay(1500);
                } else {
                    Serial.printf("STATUS: SEARCH ERROR (Code: 0x%02X)\n", p);
                    delay(1500);
                }
            } else {
                Serial.println("STATUS: FINGER SENSED, BUT IMAGE CONVERSION FAILED");
                Serial.println("Please press finger flatly and firmly on sensor.");
                Serial.println("--------------------------------------------------");
                delay(1500);
            }
        } else if (p != FINGERPRINT_NOFINGER) {
            Serial.printf("[Sensor Communication] Read code: 0x%02X\n", p);
            delay(1000);
        }
    }
    
    delay(50);
}
