// =====================================================================
// SBA BIOMETRIC ATTENDANCE SYSTEM  -  ESP32 Firmware
// =====================================================================
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Fingerprint.h>
#include <LiquidCrystal_I2C.h>
#include <FirebaseESP32.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>
#include <Preferences.h>

// --- Pin Definitions ---
#define FINGER_RX      16
#define FINGER_TX      17
#define FINGER_TOUCH   13
#define RTC_CLK        14
#define RTC_DAT        27
#define RTC_RST        25
#define LED_SCAN       4
#define LED_WIFI       26

// --- Default Wi-Fi Fallback ---
#define WIFI_SSID_DEFAULT  "YOUR_WIFI_SSID"
#define WIFI_PASS_DEFAULT  "YOUR_WIFI_PASSWORD"

// --- Firebase ---
#define FIREBASE_HOST "sba-fingerprint-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "AIzaSyBXDD_Fzk0g3kPrh2Ee19v-lfGDAvwzayU"

// --- Objects ---
HardwareSerial mySerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
LiquidCrystal_I2C lcd(0x27, 20, 4);
WebServer server(80);
Preferences prefs;
ThreeWire myWire(RTC_DAT, RTC_CLK, RTC_RST);
RtcDS1302<ThreeWire> Rtc(myWire);
FirebaseData fbdo;
FirebaseAuth fbAuth;
FirebaseConfig fbConfig;

// --- Global State ---
String wifiMode    = "AP";
bool   sensorOk    = false;
bool   firebaseReady = false;
bool   lcdConnected  = false;

// --- LCD Safe Helpers (Prevent I2C bus hangs when LCD is not connected) ---
void lcdClear() {
    if (lcdConnected) lcd.clear();
}

void lcdPrint(int col, int row, const String &msg) {
    if (lcdConnected) {
        lcd.setCursor(col, row);
        lcd.print(msg);
    }
}

// Session
bool   sessionOpen            = false;
String activeSessionCourse    = "";
String activeSessionLecturerID= "";

// Enrollment
bool   isEnrolling    = false;
int    enrollSlot     = 0;
String enrollName     = "";
String enrollMat      = "";
String enrollClass    = "ND1";
String enrollType     = "student";
String enrollDept     = "";
String enrollGenID    = "";
int    enrollStep     = 0;
String enrollStatusMsg= "Idle";
unsigned long enrollTimer = 0;

bool pendingOnSpotEnroll = false;

// Timing
static unsigned long lastHeartbeat  = 0;
static int           lastTouchState = -1;
static unsigned long lastTouchSeen  = 0;
static unsigned long lastScanBlink  = 0;
static bool          scanLedState   = false;

// =====================================================================
// HELPERS
// =====================================================================

String getFormattedTime() {
    if (!Rtc.IsDateTimeValid()) return "Unknown";
    RtcDateTime dt = Rtc.GetDateTime();
    char buf[20];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             dt.Year(), dt.Month(), dt.Day(),
             dt.Hour(), dt.Minute(), dt.Second());
    return String(buf);
}

String padID(int n) {
    if (n < 10)  return "00" + String(n);
    if (n < 100) return "0"  + String(n);
    return String(n);
}

String nextStudentID() {
    prefs.begin("ids", false);
    int n = prefs.getInt("snext", 1);
    prefs.putInt("snext", n + 1);
    prefs.end();
    return "S" + padID(n);
}

String nextLecturerID() {
    prefs.begin("ids", false);
    int n = prefs.getInt("lnext", 1);
    prefs.putInt("lnext", n + 1);
    prefs.end();
    return "L" + padID(n);
}

// =====================================================================
// NVS: STUDENTS
// =====================================================================

void saveStudentNVS(const String &genID, const String &name,
                    const String &mat, const String &sclass, int slot) {
    prefs.begin("studs", false);
    prefs.putString(("n_" + genID).c_str(), name);
    prefs.putString(("m_" + genID).c_str(), mat);
    prefs.putString(("c_" + genID).c_str(), sclass);
    prefs.putInt(   ("f_" + genID).c_str(), slot);
    prefs.putString(("s_" + String(slot)).c_str(), genID);
    prefs.end();
}

bool lookupStudentBySlot(int slot, String &outID, String &outName,
                          String &outMat, String &outClass) {
    prefs.begin("studs", true);
    String gid = prefs.getString(("s_" + String(slot)).c_str(), "");
    if (gid.length() == 0) { prefs.end(); return false; }
    outID    = gid;
    outName  = prefs.getString(("n_" + gid).c_str(), "Unknown");
    outMat   = prefs.getString(("m_" + gid).c_str(), "N/A");
    outClass = prefs.getString(("c_" + gid).c_str(), "ND1");
    prefs.end();
    return true;
}

String buildStudentsJSON() {
    prefs.begin("ids", true);
    int sMax = prefs.getInt("snext", 1);
    prefs.end();
    String out = "[";
    bool first = true;
    for (int n = 1; n < sMax; n++) {
        String gid = "S" + padID(n);
        prefs.begin("studs", true);
        String name = prefs.getString(("n_" + gid).c_str(), "");
        String mat  = prefs.getString(("m_" + gid).c_str(), "N/A");
        String cls  = prefs.getString(("c_" + gid).c_str(), "ND1");
        int    slot = prefs.getInt(   ("f_" + gid).c_str(), 0);
        prefs.end();
        if (name.length() == 0) continue;
        if (!first) out += ",";
        out += "{\"id\":\"" + gid + "\",\"name\":\"" + name +
               "\",\"mat\":\"" + mat + "\",\"sclass\":\"" + cls +
               "\",\"slot\":" + String(slot) + "}";
        first = false;
    }
    return out + "]";
}

// =====================================================================
// NVS: LECTURERS
// =====================================================================

void saveLecturerNVS(const String &genID, const String &name,
                     const String &dept, int slot) {
    prefs.begin("lects", false);
    prefs.putString(("n_" + genID).c_str(), name);
    prefs.putString(("d_" + genID).c_str(), dept);
    prefs.putInt(   ("f_" + genID).c_str(), slot);
    prefs.putString(("l_" + String(slot)).c_str(), genID);
    prefs.end();
}

bool lookupLecturerBySlot(int slot, String &outID, String &outName,
                           String &outDept) {
    prefs.begin("lects", true);
    String gid = prefs.getString(("l_" + String(slot)).c_str(), "");
    if (gid.length() == 0) { prefs.end(); return false; }
    outID   = gid;
    outName = prefs.getString(("n_" + gid).c_str(), "Unknown");
    outDept = prefs.getString(("d_" + gid).c_str(), "N/A");
    prefs.end();
    return true;
}

String buildLecturersJSON() {
    prefs.begin("ids", true);
    int lMax = prefs.getInt("lnext", 1);
    prefs.end();
    String out = "[";
    bool first = true;
    for (int n = 1; n < lMax; n++) {
        String gid = "L" + padID(n);
        prefs.begin("lects", true);
        String name = prefs.getString(("n_" + gid).c_str(), "");
        String dept = prefs.getString(("d_" + gid).c_str(), "N/A");
        int    slot = prefs.getInt(   ("f_" + gid).c_str(), 0);
        prefs.end();
        if (name.length() == 0) continue;
        if (!first) out += ",";
        out += "{\"id\":\"" + gid + "\",\"name\":\"" + name +
               "\",\"dept\":\"" + dept + "\",\"slot\":" + String(slot) + "}";
        first = false;
    }
    return out + "]";
}

// =====================================================================
// NVS: COURSES
// =====================================================================

void saveCourseNVS(const String &code, const String &title,
                   const String &lecturers) {
    prefs.begin("courses", false);
    prefs.putString(("t_" + code).c_str(), title);
    prefs.putString(("l_" + code).c_str(), lecturers);
    prefs.end();
}

void addCourseToList(const String &code) {
    prefs.begin("courses", false);
    String list = prefs.getString("codelist", "");
    if (list.length() == 0) list = code;
    else if (list.indexOf(code) < 0) list += "," + code;
    prefs.putString("codelist", list.c_str());
    prefs.end();
}

bool isLecturerAssignedToCourse(const String &lectID, const String &code) {
    prefs.begin("courses", true);
    String lects = prefs.getString(("l_" + code).c_str(), "");
    prefs.end();
    int start = 0;
    while (start < (int)lects.length()) {
        int end = lects.indexOf(',', start);
        if (end < 0) end = lects.length();
        String token = lects.substring(start, end);
        token.trim();
        if (token == lectID) return true;
        start = end + 1;
    }
    return false;
}

String buildCoursesJSON() {
    prefs.begin("courses", true);
    String list = prefs.getString("codelist", "");
    prefs.end();
    String out = "[";
    bool first = true;
    int start  = 0;
    while (start < (int)list.length()) {
        int end = list.indexOf(',', start);
        if (end < 0) end = list.length();
        String code = list.substring(start, end);
        code.trim();
        start = end + 1;
        if (code.length() == 0) continue;
        prefs.begin("courses", true);
        String title = prefs.getString(("t_" + code).c_str(), "");
        String lects = prefs.getString(("l_" + code).c_str(), "");
        prefs.end();
        if (!first) out += ",";
        out += "{\"code\":\"" + code + "\",\"title\":\"" + title +
               "\",\"lecturers\":\"" + lects + "\"}";
        first = false;
    }
    return out + "]";
}

// =====================================================================
// ATTENDANCE BUFFER (Preferences)
// =====================================================================

void bufferAttendanceRecord(const String &sid, const String &name,
                             const String &mat, const String &cls,
                             const String &course, const String &ts) {
    prefs.begin("attbuf", false);
    int count = prefs.getInt("count", 0);
    String px = "r_" + String(count) + "_";
    prefs.putString((px + "id").c_str(), sid);
    prefs.putString((px + "nm").c_str(), name);
    prefs.putString((px + "mt").c_str(), mat);
    prefs.putString((px + "cl").c_str(), cls);
    prefs.putString((px + "co").c_str(), course);
    prefs.putString((px + "ts").c_str(), ts);
    prefs.putInt("count", count + 1);
    prefs.end();
}

void flushAttendanceBuffer() {
    prefs.begin("attbuf", true);
    int count = prefs.getInt("count", 0);
    prefs.end();
    if (count == 0 || !firebaseReady) return;
    for (int i = 0; i < count; i++) {
        prefs.begin("attbuf", true);
        String px  = "r_" + String(i) + "_";
        String sid = prefs.getString((px + "id").c_str(), "");
        String nm  = prefs.getString((px + "nm").c_str(), "");
        String mt  = prefs.getString((px + "mt").c_str(), "");
        String cl  = prefs.getString((px + "cl").c_str(), "");
        String co  = prefs.getString((px + "co").c_str(), "");
        String ts  = prefs.getString((px + "ts").c_str(), "");
        prefs.end();
        if (sid.length() == 0) continue;
        FirebaseJson fbj;
        fbj.set("student_id",   sid);
        fbj.set("student_name", nm);
        fbj.set("matric_no",    mt);
        fbj.set("sclass",       cl);
        fbj.set("course",       co);
        fbj.set("timestamp",    ts);
        fbj.set("source",       "buffer_flush");
        Firebase.pushJSON(fbdo, "/attendance_logs", fbj);
    }
    prefs.begin("attbuf", false);
    prefs.clear();
    prefs.end();
    Serial.println("[Session] Buffer flushed to Firebase.");
}

void clearAttendanceBuffer() {
    prefs.begin("attbuf", false);
    prefs.clear();
    prefs.end();
}

// =====================================================================
// WI-FI
// =====================================================================

void loadWifiAndConnect() {
    prefs.begin("wifi", true);
    String ssid = prefs.getString("ssid", WIFI_SSID_DEFAULT);
    String pass = prefs.getString("pass", WIFI_PASS_DEFAULT);
    prefs.end();

    Serial.printf("[Wi-Fi] Connecting to: %s\n", ssid.c_str());
    lcdClear(); lcdPrint(0, 0, "Connecting WiFi");

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("ESP32-Attendance");
    Serial.printf("[Wi-Fi] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    lcdPrint(0, 1, "AP: 192.168.4.1");

    WiFi.begin(ssid.c_str(), pass.c_str());
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) {
        delay(400); Serial.print("."); tries++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        wifiMode = "STA";
        digitalWrite(LED_WIFI, HIGH);
        Serial.printf("[Wi-Fi] Connected to '%s'! IP: %s | RSSI: %d dBm\n",
                      WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
        lcdClear(); lcdPrint(0, 0, "WiFi Connected!");
        lcdPrint(0, 1, WiFi.localIP().toString());
    } else {
        wifiMode = "AP";
        Serial.println("[Wi-Fi] STA connection timed out. Operating in AP Mode (192.168.4.1).");
        Serial.printf("[Wi-Fi] To connect to your router, go to http://192.168.4.1 in browser -> Settings tab.\n");
        lcdClear(); lcdPrint(0, 0, "AP Mode Active");
        lcdPrint(0, 1, "192.168.4.1");
        for (int i = 0; i < 4; i++) { digitalWrite(LED_WIFI, i % 2); delay(200); }
        digitalWrite(LED_WIFI, LOW);
    }
}

// =====================================================================
// FIREBASE HELPERS
// =====================================================================

void logAttendanceToFirebase(const String &sid, const String &name,
                              const String &mat, const String &cls,
                              const String &course, const String &ts) {
    if (!firebaseReady) return;
    FirebaseJson fbj;
    fbj.set("student_id",   sid);
    fbj.set("student_name", name);
    fbj.set("matric_no",    mat);
    fbj.set("sclass",       cls);
    fbj.set("course",       course);
    fbj.set("timestamp",    ts);
    fbj.set("source",       "realtime");
    if (Firebase.pushJSON(fbdo, "/attendance_logs", fbj)) {
        Serial.println("[Firebase] Attendance logged.");
    } else {
        Serial.printf("[Firebase] Log failed: %s\n", fbdo.errorReason().c_str());
    }
}

// =====================================================================
// WEB SERVER HANDLERS
// =====================================================================

void handleRoot();  // forward declaration

void handleSystemStatus() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    bool conn = sensorOk;
    uint16_t tmpl = 0;
    if (conn) { finger.getTemplateCount(); tmpl = finger.templateCount; }
    String ip = (wifiMode == "STA") ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    bool touching = (digitalRead(FINGER_TOUCH) == HIGH) || (millis() - lastTouchSeen < 2000);
    String j  = "{";
    j += "\"sensorConnected\":"  + String(conn ? "true":"false") + ",";
    j += "\"templateCount\":"    + String(tmpl) + ",";
    j += "\"wifiIP\":\""         + ip + "\",";
    j += "\"wifiMode\":\""       + wifiMode + "\",";
    j += "\"wifiClients\":"      + String(WiFi.softAPgetStationNum()) + ",";
    j += "\"firebaseReady\":"    + String(firebaseReady ? "true":"false") + ",";
    j += "\"sessionOpen\":"      + String(sessionOpen ? "true":"false") + ",";
    j += "\"activeCourse\":\""   + activeSessionCourse + "\",";
    j += "\"activeLecturer\":\"" + activeSessionLecturerID + "\",";
    j += "\"rtcTime\":\""        + getFormattedTime() + "\",";
    j += "\"touchActive\":"      + String(touching ? "true":"false");
    j += "}";
    server.send(200, "application/json", j);
}

void handleWifiConfig() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (server.method() == HTTP_GET) {
        prefs.begin("wifi", true);
        String ssid = prefs.getString("ssid", WIFI_SSID_DEFAULT);
        prefs.end();
        server.send(200, "application/json",
                    "{\"ssid\":\"" + ssid + "\",\"mode\":\"" + wifiMode + "\"}");
    } else {
        if (!server.hasArg("ssid")) { server.send(400, "text/plain", "Missing ssid"); return; }
        prefs.begin("wifi", false);
        prefs.putString("ssid", server.arg("ssid"));
        prefs.putString("pass", server.arg("pass"));
        prefs.end();
        server.send(200, "text/plain", "OK: Credentials saved. Rebooting now...");
        delay(300);
        ESP.restart();
    }
}

void handleStudentsList() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", buildStudentsJSON());
}

void handleLecturersList() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", buildLecturersJSON());
}

void handleCoursesList() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", buildCoursesJSON());
}

void handleCoursesAdd() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (!server.hasArg("code") || !server.hasArg("title")) {
        server.send(400, "text/plain", "Missing code or title"); return;
    }
    String code  = server.arg("code");
    String title = server.arg("title");
    String lects = server.arg("lecturers");
    saveCourseNVS(code, title, lects);
    addCourseToList(code);
    if (firebaseReady) {
        FirebaseJson fbj;
        fbj.set("title",     title);
        fbj.set("lecturers", lects);
        String path = "/courses/" + code;
        Firebase.setJSON(fbdo, path, fbj);
    }
    Serial.printf("[Course] %s (%s) | %s\n", code.c_str(), title.c_str(), lects.c_str());
    server.send(200, "text/plain", "Course registered: " + code);
}

void handleEnrollStudent() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (!server.hasArg("name") || !server.hasArg("mat") || !server.hasArg("slot")) {
        server.send(400, "text/plain", "Missing name, mat or slot"); return;
    }
    enrollName   = server.arg("name");
    enrollMat    = server.arg("mat");
    enrollClass  = server.hasArg("sclass") ? server.arg("sclass") : "ND1";
    enrollSlot   = server.arg("slot").toInt();
    enrollType   = "student";
    enrollGenID  = nextStudentID();
    isEnrolling  = true;
    enrollStep   = 1;
    enrollStatusMsg = "Place your finger on the sensor...";
    enrollTimer  = millis();
    Serial.printf("[Enroll] Student: %s | ID: %s | Slot: %d\n",
                  enrollName.c_str(), enrollGenID.c_str(), enrollSlot);
    server.send(200, "application/json",
                "{\"ok\":true,\"genID\":\"" + enrollGenID + "\",\"msg\":\"Enrollment started\"}");
}

void handleEnrollLecturer() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (!server.hasArg("name") || !server.hasArg("slot")) {
        server.send(400, "text/plain", "Missing name or slot"); return;
    }
    enrollName   = server.arg("name");
    enrollDept   = server.hasArg("dept") ? server.arg("dept") : "General";
    enrollSlot   = server.arg("slot").toInt();
    enrollType   = "lecturer";
    enrollGenID  = nextLecturerID();
    isEnrolling  = true;
    enrollStep   = 1;
    enrollStatusMsg = "Place your finger on the sensor...";
    enrollTimer  = millis();
    Serial.printf("[Enroll] Lecturer: %s | ID: %s | Slot: %d\n",
                  enrollName.c_str(), enrollGenID.c_str(), enrollSlot);
    server.send(200, "application/json",
                "{\"ok\":true,\"genID\":\"" + enrollGenID + "\",\"msg\":\"Enrollment started\"}");
}

void handleEnrollStatus() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json",
                "{\"step\":" + String(enrollStep) + ",\"msg\":\"" + enrollStatusMsg +
                "\",\"genID\":\"" + enrollGenID + "\",\"enrolling\":" +
                String(isEnrolling ? "true":"false") + "}");
}

String getFirstCourseForLecturer(const String &lectID) {
    prefs.begin("courses", true);
    String list = prefs.getString("codelist", "");
    prefs.end();
    int start = 0;
    while (start < (int)list.length()) {
        int end = list.indexOf(',', start);
        if (end < 0) end = list.length();
        String code = list.substring(start, end);
        code.trim();
        start = end + 1;
        if (code.length() > 0 && isLecturerAssignedToCourse(lectID, code)) {
            return code;
        }
    }
    // Fallback to first available course if not explicitly assigned
    start = 0;
    while (start < (int)list.length()) {
        int end = list.indexOf(',', start);
        if (end < 0) end = list.length();
        String code = list.substring(start, end);
        code.trim();
        if (code.length() > 0) return code;
        start = end + 1;
    }
    return "ATTENDANCE";
}

void openSessionFor(const String &course, const String &lectID, const String &lectName) {
    sessionOpen             = true;
    activeSessionCourse     = course;
    activeSessionLecturerID = lectID;
    prefs.begin("session", false);
    prefs.putString("course", course);
    prefs.putString("lectID", lectID);
    prefs.putBool("open", true);
    prefs.end();
    clearAttendanceBuffer();
    if (firebaseReady) {
        FirebaseJson fbj;
        fbj.set("state",       "open");
        fbj.set("course",      course);
        fbj.set("lecturer_id", lectID);
        fbj.set("timestamp",   getFormattedTime());
        Firebase.setJSON(fbdo, "/session_state", fbj);
    }
    Serial.printf("[Session] *** OPENED *** Course: %s | Lecturer: %s (%s)\n",
                  course.c_str(), lectID.c_str(), lectName.c_str());
    lcdClear();
    lcdPrint(0, 0, "Session OPEN");
    lcdPrint(0, 1, course.substring(0, 20));
    lcdPrint(0, 2, "Students: Scan Now");
}

void closeActiveSession() {
    flushAttendanceBuffer();
    sessionOpen             = false;
    String prevCourse       = activeSessionCourse;
    activeSessionCourse     = "";
    activeSessionLecturerID = "";
    prefs.begin("session", false);
    prefs.putBool("open", false);
    prefs.end();
    if (firebaseReady) {
        FirebaseJson fbj;
        fbj.set("state",     "closed");
        fbj.set("course",    "");
        fbj.set("timestamp", getFormattedTime());
        Firebase.setJSON(fbdo, "/session_state", fbj);
    }
    lcdClear();
    lcdPrint(0, 0, "Session CLOSED");
    lcdPrint(0, 1, "Records Uploaded");
    Serial.printf("[Session] *** CLOSED *** Course was: %s. Attendance records uploaded.\n", prevCourse.c_str());
}

void handleSessionState() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json",
                "{\"open\":" + String(sessionOpen ? "true":"false") +
                ",\"course\":\"" + activeSessionCourse + "\"" +
                ",\"lecturer\":\"" + activeSessionLecturerID + "\"}");
}

void handleSessionOpen() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (!server.hasArg("course") || !server.hasArg("lecturerID")) {
        server.send(400, "text/plain", "Missing course or lecturerID"); return;
    }
    String course = server.arg("course");
    String lectID = server.arg("lecturerID");

    // Auto-assign lecturer if not yet assigned to avoid 403 blocks
    if (!isLecturerAssignedToCourse(lectID, course)) {
        prefs.begin("courses", false);
        String existingLects = prefs.getString(("l_" + course).c_str(), "");
        if (existingLects.length() == 0) existingLects = lectID;
        else existingLects += "," + lectID;
        prefs.putString(("l_" + course).c_str(), existingLects.c_str());
        prefs.end();
        addCourseToList(course);
        Serial.printf("[Course] Auto-assigned lecturer %s to course %s\n", lectID.c_str(), course.c_str());
    }

    prefs.begin("lects", true);
    String lName = prefs.getString(("n_" + lectID).c_str(), lectID);
    prefs.end();

    openSessionFor(course, lectID, lName);
    server.send(200, "text/plain", "Session opened for " + course);
}

void handleSessionClose() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (!sessionOpen) { server.send(400, "text/plain", "No session is open"); return; }
    closeActiveSession();
    server.send(200, "text/plain", "Session closed and records uploaded.");
}

void handleGetLogs() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (!firebaseReady) { server.send(200, "application/json", "[]"); return; }
    if (Firebase.getJSON(fbdo, "/attendance_logs")) {
        FirebaseJson &json = fbdo.jsonObject();
        String out = "[";
        bool first = true;
        size_t len = json.iteratorBegin();
        for (size_t i = 0; i < len; i++) {
            int type = 0; String key, value;
            json.iteratorGet(i, type, key, value);
            if (!first) out += ",";
            out += value;
            first = false;
        }
        json.iteratorEnd();
        out += "]";
        server.send(200, "application/json", out);
    } else {
        server.send(200, "application/json", "[]");
    }
}

void handleSpotEnrollStatus() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json",
                "{\"pending\":" + String(pendingOnSpotEnroll ? "true":"false") +
                ",\"course\":\"" + activeSessionCourse + "\"}");
}

void handleSpotEnrollDismiss() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    pendingOnSpotEnroll = false;
    server.send(200, "text/plain", "OK");
}

bool initFingerprintSensor(); // forward declaration

void handleSensorReconnect() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    bool ok = initFingerprintSensor();
    if (ok) {
        server.send(200, "application/json",
                    "{\"ok\":true,\"msg\":\"Sensor connected! Templates: " + String(finger.templateCount) + "\"}");
    } else {
        server.send(200, "application/json",
                    "{\"ok\":false,\"msg\":\"Sensor not responding. Check TX/RX swap & 5V power.\"}");
    }
}

// =====================================================================
// ENROLLMENT STATE MACHINE
// =====================================================================

void runEnrollmentStateMachine() {
    if (!isEnrolling) return;
    if (millis() - enrollTimer > 30000) {
        enrollStatusMsg = "Enrollment timed out.";
        isEnrolling = false; enrollStep = 0;
        Serial.println("[Enroll] Timeout.");
        return;
    }
    int p = -1;
    switch (enrollStep) {
        case 1:
            p = finger.getImage();
            if (p == FINGERPRINT_OK) {
                p = finger.image2Tz(1);
                if (p == FINGERPRINT_OK) {
                    enrollStatusMsg = "Image 1 captured. Remove finger...";
                    enrollStep = 2; enrollTimer = millis();
                } else { enrollStatusMsg = "Image error."; isEnrolling = false; enrollStep = 0; }
            } else if (p != FINGERPRINT_NOFINGER) {
                enrollStatusMsg = "Sensor error."; isEnrolling = false; enrollStep = 0;
            }
            break;
        case 2:
            p = finger.getImage();
            if (p == FINGERPRINT_NOFINGER) {
                enrollStatusMsg = "Place the same finger again...";
                enrollStep = 3; enrollTimer = millis();
            }
            break;
        case 3:
            p = finger.getImage();
            if (p == FINGERPRINT_OK) {
                p = finger.image2Tz(2);
                if (p == FINGERPRINT_OK) {
                    p = finger.createModel();
                    if (p == FINGERPRINT_OK) {
                        p = finger.storeModel(enrollSlot);
                        if (p == FINGERPRINT_OK) {
                            digitalWrite(LED_SCAN, HIGH);
                            if (enrollType == "student") {
                                saveStudentNVS(enrollGenID, enrollName, enrollMat, enrollClass, enrollSlot);
                                if (firebaseReady) {
                                    FirebaseJson fbj;
                                    fbj.set("id",     enrollGenID);
                                    fbj.set("name",   enrollName);
                                    fbj.set("mat",    enrollMat);
                                    fbj.set("sclass", enrollClass);
                                    fbj.set("slot",   enrollSlot);
                                    String path = "/students/" + enrollGenID;
                                    Firebase.setJSON(fbdo, path, fbj);
                                }
                                enrollStatusMsg = "Student enrolled! ID: " + enrollGenID;
                            } else {
                                saveLecturerNVS(enrollGenID, enrollName, enrollDept, enrollSlot);
                                if (firebaseReady) {
                                    FirebaseJson fbj;
                                    fbj.set("id",   enrollGenID);
                                    fbj.set("name", enrollName);
                                    fbj.set("dept", enrollDept);
                                    fbj.set("slot", enrollSlot);
                                    String path = "/lecturers/" + enrollGenID;
                                    Firebase.setJSON(fbdo, path, fbj);
                                }
                                enrollStatusMsg = "Lecturer enrolled! ID: " + enrollGenID;
                            }
                            Serial.printf("[Enroll OK] %s | ID:%s | Slot:%d\n",
                                          enrollName.c_str(), enrollGenID.c_str(), enrollSlot);
                            delay(1500);
                            digitalWrite(LED_SCAN, LOW);
                        } else { enrollStatusMsg = "Failed to store fingerprint."; }
                    } else { enrollStatusMsg = "Fingerprint mismatch. Try again."; }
                } else { enrollStatusMsg = "Image 2 error."; }
                isEnrolling = false; enrollStep = 0;
            } else if (p != FINGERPRINT_NOFINGER) {
                enrollStatusMsg = "Sensor error on scan 2.";
                isEnrolling = false; enrollStep = 0;
            }
            break;
    }
}

// =====================================================================
// EMBEDDED HTML (local IP dashboard)
// =====================================================================

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>SBA Attendance — ESP32 Local</title>
    <style>
        :root{--bg:#07090e;--card:rgba(18,24,38,.9);--border:rgba(255,255,255,.1);
              --primary:#6366f1;--green:#10b981;--amber:#f59e0b;--red:#ef4444;
              --cyan:#06b6d4;--muted:#94a3b8;--white:#f8fafc;}
        *{box-sizing:border-box;margin:0;padding:0;font-family:'Segoe UI',sans-serif;}
        body{background:var(--bg);color:var(--white);min-height:100vh;padding:1rem;}
        h1{font-size:1.4rem;font-weight:800;background:linear-gradient(135deg,#fff,#a5b4fc);
           -webkit-background-clip:text;-webkit-text-fill-color:transparent;}
        .header{display:flex;justify-content:space-between;align-items:center;
                flex-wrap:wrap;gap:.5rem;margin-bottom:1rem;padding:.75rem 1rem;
                background:var(--card);border:1px solid var(--border);border-radius:12px;}
        .badges{display:flex;gap:.5rem;flex-wrap:wrap;}
        .badge{padding:.3rem .8rem;border-radius:20px;font-size:.8rem;font-weight:700;
               border:1px solid var(--border);display:flex;align-items:center;gap:.4rem;}
        .dot{width:8px;height:8px;border-radius:50%;display:inline-block;}
        .dot.green{background:var(--green);box-shadow:0 0 8px var(--green);}
        .dot.amber{background:var(--amber);box-shadow:0 0 8px var(--amber);}
        .dot.red{background:var(--red);}
        @keyframes pulse{0%,100%{transform:scale(.9);opacity:.8;}50%{transform:scale(1.1);opacity:1;}}
        .tabs{display:flex;gap:.4rem;flex-wrap:wrap;margin-bottom:1rem;}
        .tab-btn{background:transparent;border:1px solid var(--border);color:var(--muted);
                 padding:.5rem 1rem;border-radius:8px;cursor:pointer;font-weight:600;font-size:.85rem;transition:all .2s;}
        .tab-btn:hover,.tab-btn.active{background:var(--primary);color:white;border-color:var(--primary);}
        .tab{display:none;}.tab.active{display:block;}
        .card{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:1.25rem;margin-bottom:1rem;}
        .card-title{font-weight:700;font-size:1rem;margin-bottom:1rem;}
        label{font-size:.82rem;color:var(--muted);display:block;margin-bottom:.25rem;font-weight:600;}
        input,select{width:100%;padding:.55rem .9rem;background:rgba(0,0,0,.4);
                     border:1px solid var(--border);border-radius:8px;color:white;font-size:.9rem;margin-bottom:.75rem;outline:none;}
        input:focus,select:focus{border-color:var(--primary);}
        .btn{padding:.6rem 1.2rem;border:none;border-radius:8px;cursor:pointer;font-weight:700;font-size:.88rem;transition:all .2s;}
        .btn-primary{background:var(--primary);color:white;}
        .btn-green{background:var(--green);color:white;}
        .btn-red{background:var(--red);color:white;}
        .btn-amber{background:var(--amber);color:#000;}
        .btn:hover{opacity:.85;transform:translateY(-1px);}
        .btn:disabled{opacity:.5;cursor:not-allowed;transform:none;}
        .row{display:flex;gap:.75rem;flex-wrap:wrap;}.row>*{flex:1;min-width:180px;}
        .session-badge{display:inline-block;padding:.4rem 1rem;border-radius:20px;font-weight:800;font-size:.9rem;margin-bottom:1rem;}
        .session-closed{background:rgba(239,68,68,.2);color:var(--red);}
        .session-open{background:rgba(16,185,129,.2);color:var(--green);}
        .prog-bg{height:6px;background:rgba(255,255,255,.08);border-radius:3px;overflow:hidden;margin:.75rem 0;}
        .prog-fill{height:100%;width:0%;background:linear-gradient(90deg,var(--cyan),var(--primary));transition:width .4s;}
        .info-row{display:flex;justify-content:space-between;font-size:.85rem;margin-bottom:.4rem;}
        .info-row span:first-child{color:var(--muted);}
        .alert{padding:.8rem 1rem;border-radius:8px;margin-bottom:.75rem;font-size:.88rem;font-weight:600;}
        .alert-green{background:rgba(16,185,129,.15);border:1px solid var(--green);color:var(--green);}
        .alert-amber{background:rgba(245,158,11,.15);border:1px solid var(--amber);color:var(--amber);}
        .alert-red{background:rgba(239,68,68,.15);border:1px solid var(--red);color:var(--red);}
        table{width:100%;border-collapse:collapse;font-size:.85rem;}
        th{color:var(--muted);padding:.5rem .75rem;text-align:left;font-size:.75rem;text-transform:uppercase;border-bottom:1px solid var(--border);}
        td{padding:.65rem .75rem;border-bottom:1px solid rgba(255,255,255,.04);}
        .toast{position:fixed;bottom:1.5rem;right:1.5rem;padding:.9rem 1.4rem;background:var(--card);
               border:1px solid var(--green);border-radius:12px;color:white;font-weight:600;display:none;z-index:999;}
    </style>
</head>
<body>
<div class="header">
    <div><h1>SBA Attendance System</h1><small style="color:var(--muted)">ESP32 Local Dashboard</small></div>
    <div class="badges">
        <span class="badge"><span class="dot red" id="sensorDot"></span><span id="sensorTxt">Sensor: --</span></span>
        <span class="badge"><span class="dot" id="touchDot" style="background:#64748b"></span><span id="touchTxt">Touch: Idle</span></span>
        <span class="badge"><span class="dot amber" id="modeDot"></span><span id="modeTxt">Mode: --</span></span>
        <span class="badge">📡 <span id="ipTxt">--</span></span>
        <span class="badge">⏰ <span id="clockTxt">--:--</span></span>
    </div>
</div>
<div class="tabs">
    <button class="tab-btn active" onclick="showTab('attend',this)">🎓 Attendance</button>
    <button class="tab-btn" onclick="showTab('students',this)">👤 Students</button>
    <button class="tab-btn" onclick="showTab('lecturers',this)">🧑‍🏫 Lecturers</button>
    <button class="tab-btn" onclick="showTab('courses',this)">📚 Courses</button>
    <button class="tab-btn" onclick="showTab('settings',this)">⚙️ Settings</button>
</div>

<!-- ATTEND -->
<div id="tab-attend" class="tab active">
  <div class="card">
    <div class="card-title">🎓 Attendance Session</div>
    <div id="sessionBadge" class="session-badge session-closed">CLOSED</div>
    <div class="info-row"><span>Active Course:</span><span id="activeCourse">—</span></div>
    <div class="info-row"><span>Lecturer:</span><span id="activeLecturer">—</span></div>
    <hr style="border-color:var(--border);margin:.75rem 0;">
    <div id="openArea">
        <label>Select Course</label><select id="attendCourse"></select>
        <label>Lecturer ID</label><input type="text" id="attendLectID" placeholder="e.g. L001">
        <button class="btn btn-green" onclick="openSession()">▶ Lecturer: Open Attendance</button>
    </div>
    <div id="closeArea" style="display:none;">
        <label>Lecturer ID to close</label><input type="text" id="closeLectID" placeholder="e.g. L001">
        <button class="btn btn-red" onclick="closeSession()">⏹ Close Attendance</button>
    </div>
  </div>
  <div class="card" id="onSpotCard" style="display:none;border-color:var(--amber);">
    <div class="card-title" style="color:var(--amber)">⚠️ Unknown Finger — Enroll on the Spot</div>
    <div class="row">
        <div><label>Full Name</label><input id="spName" type="text"></div>
        <div><label>Matric No.</label><input id="spMat" type="text"></div>
    </div>
    <div class="row">
        <div><label>Class</label><select id="spClass"><option>ND1</option><option>ND2</option><option>HND1</option><option>HND2</option></select></div>
        <div><label>Slot (1–127)</label><input id="spSlot" type="number" min="1" max="127"></div>
    </div>
    <div style="display:flex;gap:.5rem;">
        <button class="btn btn-primary" onclick="submitOnSpotEnroll()">📷 Start Enrollment</button>
        <button class="btn" style="background:rgba(255,255,255,.1);" onclick="dismissOnSpot()">✕ Dismiss</button>
    </div>
    <div class="prog-bg" id="spPB" style="display:none;"><div class="prog-fill" id="spPF"></div></div>
    <div id="spST" style="font-size:.85rem;color:var(--cyan);margin-top:.5rem;"></div>
  </div>
</div>

<!-- STUDENTS -->
<div id="tab-students" class="tab">
  <div class="card">
    <div class="card-title">👤 Enroll New Student</div>
    <form onsubmit="enrollStudent(event)">
        <div class="row">
            <div><label>Full Name</label><input id="stuName" type="text" required></div>
            <div><label>Matric Number</label><input id="stuMat" type="text" required></div>
        </div>
        <div class="row">
            <div><label>Class</label><select id="stuClass"><option>ND1</option><option>ND2</option><option>HND1</option><option>HND2</option></select></div>
            <div><label>Slot (1–100)</label><input id="stuSlot" type="number" min="1" max="100" required></div>
        </div>
        <button class="btn btn-primary" type="submit" id="stuBtn">☝️ Start Enrollment</button>
    </form>
    <div class="prog-bg" id="stuPB" style="display:none;"><div class="prog-fill" id="stuPF"></div></div>
    <div id="stuST" style="font-size:.85rem;color:var(--cyan);margin-top:.5rem;"></div>
    <div id="stuGID" style="font-size:.9rem;color:var(--green);margin-top:.4rem;font-weight:700;"></div>
  </div>
  <div class="card"><div class="card-title">Students List</div>
    <div style="overflow-x:auto;"><table><thead><tr><th>ID</th><th>Name</th><th>Matric</th><th>Class</th><th>Slot</th></tr></thead>
    <tbody id="stuTB"><tr><td colspan="5" style="color:var(--muted);text-align:center">Loading...</td></tr></tbody></table></div>
  </div>
</div>

<!-- LECTURERS -->
<div id="tab-lecturers" class="tab">
  <div class="card">
    <div class="card-title">🧑‍🏫 Enroll New Lecturer</div>
    <form onsubmit="enrollLecturer(event)">
        <div class="row">
            <div><label>Full Name</label><input id="lectName" type="text" required></div>
            <div><label>Department</label><input id="lectDept" type="text"></div>
        </div>
        <div><label>Slot (101–127)</label><input id="lectSlot" type="number" min="101" max="127" required></div>
        <button class="btn btn-primary" type="submit" id="lectBtn">☝️ Start Enrollment</button>
    </form>
    <div class="prog-bg" id="lectPB" style="display:none;"><div class="prog-fill" id="lectPF"></div></div>
    <div id="lectST" style="font-size:.85rem;color:var(--cyan);margin-top:.5rem;"></div>
    <div id="lectGID" style="font-size:.9rem;color:var(--green);margin-top:.4rem;font-weight:700;"></div>
  </div>
  <div class="card"><div class="card-title">Lecturers List</div>
    <div style="overflow-x:auto;"><table><thead><tr><th>ID</th><th>Name</th><th>Dept</th><th>Slot</th></tr></thead>
    <tbody id="lectTB"><tr><td colspan="4" style="color:var(--muted);text-align:center">Loading...</td></tr></tbody></table></div>
  </div>
</div>

<!-- COURSES -->
<div id="tab-courses" class="tab">
  <div class="card">
    <div class="card-title">📚 Register Course</div>
    <form onsubmit="registerCourse(event)">
        <div class="row">
            <div><label>Course Code</label><input id="cCode" type="text" required></div>
            <div><label>Course Title</label><input id="cTitle" type="text" required></div>
        </div>
        <label>Assigned Lecturers (comma-separated IDs e.g. L001,L002)</label>
        <input id="cLects" type="text" placeholder="L001,L002">
        <button class="btn btn-primary" type="submit">💾 Register</button>
    </form>
  </div>
  <div class="card"><div class="card-title">Courses List</div>
    <div style="overflow-x:auto;"><table><thead><tr><th>Code</th><th>Title</th><th>Lecturers</th></tr></thead>
    <tbody id="courseTB"><tr><td colspan="3" style="color:var(--muted);text-align:center">Loading...</td></tr></tbody></table></div>
  </div>
</div>

<!-- SETTINGS -->
<div id="tab-settings" class="tab">
  <div class="card">
    <div class="card-title">📶 Wi-Fi Settings & Diagnostics</div>
    <div class="info-row"><span>Current Mode:</span><span id="sModeD" style="font-weight:700;color:var(--amber)">—</span></div>
    <div class="info-row"><span>IP Address:</span><span id="sIPD">—</span></div>
    <div class="info-row"><span>Sensor:</span><span id="sSensD" style="font-weight:700;">—</span></div>
    <div class="info-row"><span>Touch (GPIO 13):</span><span id="sTouchD" style="font-weight:700;">Idle</span></div>
    <div style="margin:.75rem 0;">
        <button class="btn btn-primary" type="button" onclick="reconnectSensor()">🔄 Retry / Scan Sensor</button>
    </div>
    <hr style="border-color:var(--border);margin:.75rem 0;">
    <form onsubmit="saveWifi(event)">
        <label>New SSID</label><input id="wSSID" type="text" required>
        <label>Password (blank = open)</label><input id="wPass" type="password">
        <div style="display:flex;gap:.5rem;flex-wrap:wrap;">
            <button class="btn btn-primary" type="submit">💾 Save & Reboot</button>
            <button class="btn btn-amber" type="button" onclick="testConn()">🔗 Test</button>
        </div>
    </form>
    <div id="wAlert" style="margin-top:.75rem;display:none;"></div>
  </div>
</div>

<div id="toast" class="toast"></div>
<script>
let spI=null,stuI=null,lectI=null,spotI=null;
function showTab(n,btn){
    document.querySelectorAll('.tab').forEach(t=>t.classList.remove('active'));
    document.querySelectorAll('.tab-btn').forEach(b=>b.classList.remove('active'));
    document.getElementById('tab-'+n).classList.add('active');
    if(btn)btn.classList.add('active');
    if(n==='students')loadList('/students/list','stuTB',['id','name','mat','sclass','slot'],5);
    if(n==='lecturers')loadList('/lecturers/list','lectTB',['id','name','dept','slot'],4);
    if(n==='courses')loadList('/courses/list','courseTB',['code','title','lecturers'],3);
    if(n==='settings')loadWifi();
}
function toast(m,d){const t=document.getElementById('toast');t.innerText=m;t.style.display='block';setTimeout(()=>t.style.display='none',d||3500);}
function pollStatus(){
    fetch('/system-status').then(r=>r.json()).then(st=>{
        const sd=document.getElementById('sensorDot'),st2=document.getElementById('sensorTxt');
        if(st.sensorConnected){sd.className='dot green';st2.innerText='Sensor: OK ('+st.templateCount+')';}
        else{sd.className='dot red';st2.innerText='Sensor: Offline';}
        const td=document.getElementById('touchDot'),tt=document.getElementById('touchTxt');
        if(td&&tt){
            if(st.touchActive){
                td.className='dot green';td.style.background='#10b981';td.style.boxShadow='0 0 8px #10b981';
                tt.innerText='👆 Touch: Active';tt.style.color='#10b981';
            }else{
                td.className='dot';td.style.background='#64748b';td.style.boxShadow='none';
                tt.innerText='Touch: Idle';tt.style.color='';
            }
        }
        const md=document.getElementById('modeDot'),mt=document.getElementById('modeTxt');
        if(st.wifiMode==='STA'){md.className='dot green';mt.innerText='STA Mode';}
        else{md.className='dot amber';mt.innerText='AP Mode';}
        document.getElementById('ipTxt').innerText=st.wifiIP;
        if(st.rtcTime)document.getElementById('clockTxt').innerText=st.rtcTime.split(' ')[1]||st.rtcTime;
        const sm=document.getElementById('sModeD');if(sm){sm.innerText=st.wifiMode;sm.style.color=st.wifiMode==='STA'?'#10b981':'#f59e0b';}
        const si=document.getElementById('sIPD');if(si)si.innerText=st.wifiIP;
        const ss=document.getElementById('sSensD');if(ss){ss.innerText=st.sensorConnected?'Connected ('+st.templateCount+')':'OFFLINE';ss.style.color=st.sensorConnected?'#10b981':'#ef4444';}
        const stouch=document.getElementById('sTouchD');if(stouch){stouch.innerText=st.touchActive?'👆 FINGER TOUCHED':'Idle';stouch.style.color=st.touchActive?'#10b981':'#94a3b8';}
        updateSessionUI(st);
    }).catch(()=>{});
}
function updateSessionUI(st){
    const b=document.getElementById('sessionBadge'),oa=document.getElementById('openArea'),ca=document.getElementById('closeArea');
    const ac=document.getElementById('activeCourse'),al=document.getElementById('activeLecturer');
    if(!b)return;
    if(st.sessionOpen){b.className='session-badge session-open';b.innerText='OPEN';oa.style.display='none';ca.style.display='block';
        if(ac)ac.innerText=st.activeCourse;if(al)al.innerText=st.activeLecturer;
        if(!spotI)spotI=setInterval(pollSpot,2000);
    }else{b.className='session-badge session-closed';b.innerText='CLOSED';oa.style.display='block';ca.style.display='none';
        if(ac)ac.innerText='—';if(al)al.innerText='—';
        if(spotI){clearInterval(spotI);spotI=null;}
        document.getElementById('onSpotCard').style.display='none';
    }
}
function loadAttendCourses(){
    fetch('/courses/list').then(r=>r.json()).then(list=>{
        const s=document.getElementById('attendCourse');if(!s)return;
        s.innerHTML='';list.forEach(c=>{const o=document.createElement('option');o.value=c.code;o.innerText=c.code+' — '+c.title;s.appendChild(o);});
    }).catch(()=>{});
}
function openSession(){
    const course=document.getElementById('attendCourse').value;
    const lect=document.getElementById('attendLectID').value.trim();
    if(!course||!lect)return toast('Select course and enter Lecturer ID!');
    fetch('/session/open',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
        body:'course='+encodeURIComponent(course)+'&lecturerID='+encodeURIComponent(lect)})
    .then(r=>r.text()).then(m=>toast(m)).catch(()=>toast('Error opening session'));
}
function closeSession(){
    const lect=document.getElementById('closeLectID').value.trim();
    fetch('/session/close',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
        body:'lecturerID='+encodeURIComponent(lect)})
    .then(r=>r.text()).then(m=>toast(m)).catch(()=>toast('Error closing session'));
}
function pollSpot(){
    fetch('/spot-enroll-status').then(r=>r.json()).then(st=>{
        document.getElementById('onSpotCard').style.display=st.pending?'block':'none';
    }).catch(()=>{});
}
function submitOnSpotEnroll(){
    const name=document.getElementById('spName').value.trim();
    const mat=document.getElementById('spMat').value.trim();
    const cls=document.getElementById('spClass').value;
    const slot=document.getElementById('spSlot').value;
    if(!name||!mat||!slot)return toast('Fill all fields!');
    document.getElementById('spPB').style.display='block';
    document.getElementById('spST').innerText='Sending...';
    fetch('/enroll/student',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
        body:'name='+encodeURIComponent(name)+'&mat='+encodeURIComponent(mat)+'&sclass='+encodeURIComponent(cls)+'&slot='+slot})
    .then(r=>r.json()).then(res=>{
        document.getElementById('spST').innerText='ID: '+res.genID+' — Place finger!';
        fetch('/spot-enroll-status/dismiss',{method:'POST'});
        if(spI)clearInterval(spI);
        spI=setInterval(()=>pollEnroll('sp'),1000);
    });
}
function dismissOnSpot(){fetch('/spot-enroll-status/dismiss',{method:'POST'});document.getElementById('onSpotCard').style.display='none';}
function loadList(url,tbId,cols,colN){
    fetch(url).then(r=>r.json()).then(list=>{
        const tb=document.getElementById(tbId);if(!tb)return;
        if(!list||list.length===0){tb.innerHTML='<tr><td colspan="'+colN+'" style="color:#94a3b8;text-align:center">None yet.</td></tr>';return;}
        tb.innerHTML='';
        list.forEach(item=>{
            const tr=document.createElement('tr');
            tr.innerHTML=cols.map(c=>'<td>'+(item[c]||'—')+'</td>').join('');
            tb.appendChild(tr);
        });
    }).catch(()=>{});
}
function enrollStudent(e){
    e.preventDefault();
    const name=document.getElementById('stuName').value.trim();
    const mat=document.getElementById('stuMat').value.trim();
    const cls=document.getElementById('stuClass').value;
    const slot=document.getElementById('stuSlot').value;
    document.getElementById('stuBtn').disabled=true;
    document.getElementById('stuPB').style.display='block';
    document.getElementById('stuST').innerText='Sending...';
    fetch('/enroll/student',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
        body:'name='+encodeURIComponent(name)+'&mat='+encodeURIComponent(mat)+'&sclass='+encodeURIComponent(cls)+'&slot='+slot})
    .then(r=>r.json()).then(res=>{
        document.getElementById('stuGID').innerText='Generated ID: '+res.genID;
        document.getElementById('stuST').innerText='Place finger on sensor!';
        if(stuI)clearInterval(stuI);stuI=setInterval(()=>pollEnroll('stu'),1000);
    }).catch(()=>{document.getElementById('stuBtn').disabled=false;toast('Error');});
}
function enrollLecturer(e){
    e.preventDefault();
    const name=document.getElementById('lectName').value.trim();
    const dept=document.getElementById('lectDept').value.trim()||'General';
    const slot=document.getElementById('lectSlot').value;
    document.getElementById('lectBtn').disabled=true;
    document.getElementById('lectPB').style.display='block';
    document.getElementById('lectST').innerText='Sending...';
    fetch('/enroll/lecturer',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
        body:'name='+encodeURIComponent(name)+'&dept='+encodeURIComponent(dept)+'&slot='+slot})
    .then(r=>r.json()).then(res=>{
        document.getElementById('lectGID').innerText='Generated ID: '+res.genID;
        document.getElementById('lectST').innerText='Place finger on sensor!';
        if(lectI)clearInterval(lectI);lectI=setInterval(()=>pollEnroll('lect'),1000);
    }).catch(()=>{document.getElementById('lectBtn').disabled=false;toast('Error');});
}
function pollEnroll(p){
    fetch('/enroll-status').then(r=>r.json()).then(st=>{
        let pct=25;if(st.step===2)pct=55;else if(st.step===3)pct=80;
        const fill=document.getElementById(p+'PF'),txt=document.getElementById(p+'ST');
        if(fill)fill.style.width=pct+'%';if(txt)txt.innerText=st.msg;
        if(!st.enrolling&&st.step===0){
            if(fill)fill.style.width=st.msg.toLowerCase().includes('enroll')?'100%':pct+'%';
            if(p==='stu'){clearInterval(stuI);document.getElementById('stuBtn').disabled=false;loadList('/students/list','stuTB',['id','name','mat','sclass','slot'],5);}
            if(p==='lect'){clearInterval(lectI);document.getElementById('lectBtn').disabled=false;loadList('/lecturers/list','lectTB',['id','name','dept','slot'],4);}
            if(p==='sp'){clearInterval(spI);}
            toast(st.msg);
        }
    }).catch(()=>{});
}
function registerCourse(e){
    e.preventDefault();
    const code=document.getElementById('cCode').value.trim();
    const title=document.getElementById('cTitle').value.trim();
    const lects=document.getElementById('cLects').value.trim();
    fetch('/courses/add',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
        body:'code='+encodeURIComponent(code)+'&title='+encodeURIComponent(title)+'&lecturers='+encodeURIComponent(lects)})
    .then(r=>r.text()).then(m=>{toast(m);loadList('/courses/list','courseTB',['code','title','lecturers'],3);loadAttendCourses();})
    .catch(()=>toast('Error'));
}
function loadWifi(){
    fetch('/wifi-config').then(r=>r.json()).then(cfg=>{
        const i=document.getElementById('wSSID');if(i&&cfg.ssid)i.value=cfg.ssid;
    }).catch(()=>{});
}
function saveWifi(e){
    e.preventDefault();
    const ssid=document.getElementById('wSSID').value.trim();
    const pass=document.getElementById('wPass').value;
    const al=document.getElementById('wAlert');
    al.className='alert alert-amber';al.innerText='⏳ Saving & rebooting...';al.style.display='block';
    fetch('/wifi-config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
        body:'ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(pass)})
    .then(r=>r.text()).then(m=>{al.className='alert alert-green';al.innerText='✅ '+m;})
    .catch(()=>{al.className='alert alert-red';al.innerText='❌ Could not reach ESP32.';});
}
function testConn(){
    fetch('/system-status').then(r=>r.json()).then(st=>{
        toast('✅ Online! Sensor:'+(st.sensorConnected?'OK':'Offline')+' | IP:'+st.wifiIP,4000);
    }).catch(()=>toast('❌ Cannot reach ESP32',4000));
}
function reconnectSensor(){
    toast('🔍 Scanning for sensor (57600, 9600, 115200)...', 3000);
    fetch('/sensor/reconnect',{method:'POST'}).then(r=>r.json()).then(res=>{
        toast(res.ok ? '✅ ' + res.msg : '❌ ' + res.msg, 4500);
        pollStatus();
    }).catch(()=>toast('❌ Cannot reach ESP32', 3000));
}
document.addEventListener('DOMContentLoaded',()=>{
    pollStatus();loadAttendCourses();setInterval(pollStatus,1000);
});
</script>
</body>
</html>
)rawliteral";

void handleRoot() {
    server.send_P(200, "text/html", INDEX_HTML);
}

// =====================================================================
// SENSOR HELPER
// =====================================================================

bool initFingerprintSensor() {
    uint32_t bauds[] = {57600, 9600, 115200, 19200, 38400};
    for (uint32_t b : bauds) {
        mySerial.begin(b, SERIAL_8N1, FINGER_RX, FINGER_TX);
        finger.begin(b);
        delay(40);
        if (finger.verifyPassword()) {
            sensorOk = true;
            finger.getTemplateCount();
            Serial.printf("[FP] Sensor OK at %u baud. Templates: %d\n", b, finger.templateCount);
            return true;
        }
    }
    sensorOk = false;
    Serial.println("[ERROR] Fingerprint sensor not responding on any baud rate.");
    return false;
}

// =====================================================================
// SETUP
// =====================================================================

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n================================================");
    Serial.println("  SBA BIOMETRIC ATTENDANCE SYSTEM — BOOT       ");
    Serial.println("================================================");

    pinMode(FINGER_TOUCH, INPUT_PULLDOWN);
    pinMode(LED_SCAN, OUTPUT);
    pinMode(LED_WIFI, OUTPUT);
    digitalWrite(LED_SCAN, LOW);
    digitalWrite(LED_WIFI, LOW);

    // Initialize I2C and probe for 20x4 LCD at 0x27
    Wire.begin(21, 22);
    Wire.beginTransmission(0x27);
    if (Wire.endTransmission() == 0) {
        lcdConnected = true;
        lcd.init();
        lcd.backlight();
        lcdPrint(0, 0, "SBA Attendance");
        lcdPrint(0, 1, "Booting...");
        Serial.println("[LCD] I2C 20x4 LCD detected at 0x27.");
    } else {
        lcdConnected = false;
        Serial.println("[LCD] Not detected at 0x27. Operating in headless mode without I2C delays.");
    }
    delay(300);

    // RTC
    Rtc.Begin();
    RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
    if (!Rtc.IsDateTimeValid())      Rtc.SetDateTime(compiled);
    if (Rtc.GetIsWriteProtected())   Rtc.SetIsWriteProtected(false);
    if (!Rtc.GetIsRunning())         Rtc.SetIsRunning(true);
    if (Rtc.GetDateTime() < compiled) Rtc.SetDateTime(compiled);
    Serial.printf("[RTC] %s\n", getFormattedTime().c_str());

    // Fingerprint
    if (initFingerprintSensor()) {
        lcdPrint(0, 2, "Sensor: OK      ");
    } else {
        lcdPrint(0, 2, "Sensor FAIL!    ");
    }

    // Restore session
    prefs.begin("session", true);
    sessionOpen             = prefs.getBool("open", false);
    activeSessionCourse     = prefs.getString("course", "");
    activeSessionLecturerID = prefs.getString("lectID", "");
    prefs.end();
    if (sessionOpen)
        Serial.printf("[Session] Restored: %s by %s\n",
                      activeSessionCourse.c_str(), activeSessionLecturerID.c_str());

    // Wi-Fi
    loadWifiAndConnect();

    // Firebase
    if (wifiMode == "STA") {
        fbConfig.host = FIREBASE_HOST;
        fbConfig.signer.tokens.legacy_token = FIREBASE_AUTH;
        Firebase.begin(&fbConfig, &fbAuth);
        Firebase.reconnectWiFi(true);
        firebaseReady = true;
        Serial.println("[Firebase] Initialized.");
    }

    // REST endpoints
    server.on("/",                           HTTP_GET,  handleRoot);
    server.on("/system-status",              HTTP_GET,  handleSystemStatus);
    server.on("/wifi-config",                HTTP_GET,  handleWifiConfig);
    server.on("/wifi-config",                HTTP_POST, handleWifiConfig);
    server.on("/students/list",              HTTP_GET,  handleStudentsList);
    server.on("/lecturers/list",             HTTP_GET,  handleLecturersList);
    server.on("/courses/list",               HTTP_GET,  handleCoursesList);
    server.on("/courses/add",                HTTP_POST, handleCoursesAdd);
    server.on("/enroll/student",             HTTP_POST, handleEnrollStudent);
    server.on("/enroll/lecturer",            HTTP_POST, handleEnrollLecturer);
    server.on("/enroll-status",              HTTP_GET,  handleEnrollStatus);
    server.on("/session/open",               HTTP_POST, handleSessionOpen);
    server.on("/session/close",              HTTP_POST, handleSessionClose);
    server.on("/session/state",              HTTP_GET,  handleSessionState);
    server.on("/attendance-logs",            HTTP_GET,  handleGetLogs);
    server.on("/spot-enroll-status",         HTTP_GET,  handleSpotEnrollStatus);
    server.on("/spot-enroll-status/dismiss", HTTP_POST, handleSpotEnrollDismiss);
    server.on("/sensor/reconnect",           HTTP_POST, handleSensorReconnect);

    server.begin();
    Serial.println("[Web] HTTP server active on port 80.");

    lcdClear();
    lcdPrint(0, 0, wifiMode == "STA" ? "STA:" + WiFi.localIP().toString().substring(0,14) : "AP: 192.168.4.1");
    lcdPrint(0, 1, sensorOk ? "Sensor: OK" : "Sensor: FAIL");
    lcdPrint(0, 2, sessionOpen ? "Open:" + activeSessionCourse.substring(0,14) : "Ready");
    lcdPrint(0, 3, getFormattedTime().substring(0,20));
    Serial.println("================================================\n[READY]");
}

// =====================================================================
// LOOP
// =====================================================================

void loop() {
    server.handleClient();
    runEnrollmentStateMachine();

    // Heartbeat
    if (!isEnrolling && millis() - lastHeartbeat > 6000) {
        lastHeartbeat = millis();
        Serial.printf("[HB] Sensor:%s | Mode:%s | Session:%s | %s\n",
                      sensorOk ? "OK":"FAIL", wifiMode.c_str(),
                      sessionOpen ? activeSessionCourse.c_str() : "closed",
                      getFormattedTime().c_str());
    }

    // Touch pin
    int touchState = digitalRead(FINGER_TOUCH);
    if (touchState == HIGH) {
        lastTouchSeen = millis();
    }
    if (touchState != lastTouchState) {
        lastTouchState = touchState;
        if (touchState == HIGH) Serial.println("[TOUCH] Finger on GPIO 13.");
    }

    // LED_SCAN blink while enrolling
    if (isEnrolling && millis() - lastScanBlink > 150) {
        lastScanBlink = millis();
        scanLedState  = !scanLedState;
        digitalWrite(LED_SCAN, scanLedState ? HIGH : LOW);
    }

    // LED_WIFI
    if (wifiMode == "STA") {
        digitalWrite(LED_WIFI, HIGH);
    } else if (!isEnrolling && millis() - lastScanBlink > 1000) {
        lastScanBlink = millis();
        digitalWrite(LED_WIFI, !digitalRead(LED_WIFI));
    }

    // Wi-Fi Auto-Reconnect & Dynamic Firebase Init
    static unsigned long lastWifiCheck = 0;
    if (millis() - lastWifiCheck > 10000) {
        lastWifiCheck = millis();
        if (WiFi.status() != WL_CONNECTED) {
            if (wifiMode == "STA") {
                wifiMode = "AP";
                digitalWrite(LED_WIFI, LOW);
                Serial.println("[Wi-Fi] Lost STA connection. Retrying in background...");
            }
            prefs.begin("wifi", true);
            String savedSSID = prefs.getString("ssid", WIFI_SSID_DEFAULT);
            String savedPass = prefs.getString("pass", WIFI_PASS_DEFAULT);
            prefs.end();
            if (savedSSID != "YOUR_WIFI_SSID" && savedSSID.length() > 0) {
                WiFi.begin(savedSSID.c_str(), savedPass.c_str());
            }
        } else {
            if (wifiMode != "STA") {
                wifiMode = "STA";
                digitalWrite(LED_WIFI, HIGH);
                Serial.printf("[Wi-Fi] Reconnected to STA! IP: %s | RSSI: %d dBm\n",
                              WiFi.localIP().toString().c_str(), WiFi.RSSI());
                if (!firebaseReady) {
                    fbConfig.host = FIREBASE_HOST;
                    fbConfig.signer.tokens.legacy_token = FIREBASE_AUTH;
                    Firebase.begin(&fbConfig, &fbAuth);
                    Firebase.reconnectWiFi(true);
                    firebaseReady = true;
                    Serial.println("[Firebase] Initialized on STA connection.");
                    flushAttendanceBuffer();
                }
            }
        }
    }

    // Remote Firebase /session_state Sync
    static unsigned long lastSessionPoll = 0;
    if (firebaseReady && millis() - lastSessionPoll > 4000) {
        lastSessionPoll = millis();
        if (Firebase.getJSON(fbdo, "/session_state")) {
            FirebaseJson &json = fbdo.jsonObject();
            FirebaseJsonData dState, dCourse, dLect;
            json.get(dState, "state");
            json.get(dCourse, "course");
            json.get(dLect, "lecturer_id");
            if (dState.success) {
                String remoteState  = dState.stringValue;
                String remoteCourse = dCourse.success ? dCourse.stringValue : "";
                String remoteLect   = dLect.success ? dLect.stringValue : "L001";

                if (remoteState == "open" && !sessionOpen && remoteCourse.length() > 0) {
                    Serial.printf("[Firebase] Remote Session OPEN received for: %s\n", remoteCourse.c_str());
                    openSessionFor(remoteCourse, remoteLect, "Cloud Sync");
                } else if (remoteState == "closed" && sessionOpen) {
                    Serial.println("[Firebase] Remote Session CLOSE received.");
                    closeActiveSession();
                } else if (remoteState == "waiting_start" && !sessionOpen && remoteCourse.length() > 0) {
                    activeSessionCourse = remoteCourse;
                }
            }
        }
    }

    // ============ ATTENDANCE SCAN ============
    if (!isEnrolling && sensorOk) {
        bool triggered = false;
        int touchPinVal = digitalRead(FINGER_TOUCH);
        uint8_t p = finger.getImage();
        if (touchPinVal == HIGH || p == FINGERPRINT_OK) {
            triggered = true;
            if (p != FINGERPRINT_OK) {
                delay(30);
                p = finger.getImage();
            }
        }

        if (triggered && p == FINGERPRINT_OK) {
            p = finger.image2Tz();
            if (p == FINGERPRINT_OK) {
                // SFM-V1.7 capacitive sensor uses standard fingerSearch (not fingerFastSearch)
                p = finger.fingerSearch();

                if (p == FINGERPRINT_OK) {
                    int    slot      = finger.fingerID;
                    int    score     = finger.confidence;
                    String sID, sName, sMat, sCls, lID, lName, lDept;
                    bool   isStu  = lookupStudentBySlot(slot, sID, sName, sMat, sCls);
                    bool   isLect = lookupLecturerBySlot(slot, lID, lName, lDept);

                    Serial.printf("[SCAN] Slot:%d Score:%d Stu:%s Lect:%s\n",
                                  slot, score, isStu ? sID.c_str() : "no", isLect ? lID.c_str() : "no");
                    digitalWrite(LED_SCAN, HIGH);

                    if (isLect && !sessionOpen) {
                        String courseToOpen = activeSessionCourse;
                        if (courseToOpen.length() == 0) {
                            courseToOpen = getFirstCourseForLecturer(lID);
                        }
                        Serial.printf("[SCAN] Lecturer %s scanned. Opening session for %s\n", lName.c_str(), courseToOpen.c_str());
                        openSessionFor(courseToOpen, lID, lName);
                        delay(2000);
                    }
                    else if (isLect && sessionOpen) {
                        Serial.printf("[SCAN] Lecturer %s scanned while session open. Closing session...\n", lName.c_str());
                        closeActiveSession();
                        delay(2500);
                    }
                    else if (isStu && sessionOpen) {
                        String ts = getFormattedTime();
                        Serial.printf("[ATTENDANCE] Recorded: %s (%s, %s) for course %s at %s\n",
                                      sName.c_str(), sMat.c_str(), sCls.c_str(), activeSessionCourse.c_str(), ts.c_str());
                        logAttendanceToFirebase(sID, sName, sMat, sCls, activeSessionCourse, ts);
                        bufferAttendanceRecord(sID, sName, sMat, sCls, activeSessionCourse, ts);
                        lcdClear();
                        lcdPrint(0, 0, "Welcome!");
                        lcdPrint(0, 1, sName.substring(0, 20));
                        lcdPrint(0, 2, sMat.substring(0, 20));
                        lcdPrint(0, 3, activeSessionCourse.substring(0, 20));
                        delay(2000);
                    }
                    else if (!isStu && !isLect && sessionOpen) {
                        Serial.printf("[SCAN] Fingerprint matched slot %d but student not registered in NVS!\n", slot);
                        pendingOnSpotEnroll = true;
                        lcdClear();
                        lcdPrint(0, 0, "Unknown Finger!");
                        lcdPrint(0, 1, "Enroll via Web");
                        delay(2000);
                    }
                    else {
                        Serial.println("[SCAN] Scan ignored: Attendance session is currently CLOSED. Lecturer must scan first to open.");
                        lcdClear();
                        lcdPrint(0, 0, "No Active Session");
                        lcdPrint(0, 1, "Lecturer Scan 1st");
                        delay(2000);
                    }

                    digitalWrite(LED_SCAN, LOW);
                    lcdClear();
                    lcdPrint(0, 0, wifiMode == "STA" ? "STA:" + WiFi.localIP().toString().substring(0, 14) : "AP: 192.168.4.1");
                    lcdPrint(0, 1, sensorOk ? "Sensor: OK" : "Sensor: FAIL");
                    lcdPrint(0, 2, sessionOpen ? "Open:" + activeSessionCourse.substring(0, 14) : "Ready");
                }
                else if (p == FINGERPRINT_NOTFOUND) {
                    Serial.println("[SCAN] Fingerprint not recognized in sensor database.");
                    if (sessionOpen) {
                        pendingOnSpotEnroll = true;
                        lcdClear();
                        lcdPrint(0, 0, "Unknown! Enroll");
                        lcdPrint(0, 1, "via Web UI");
                    }
                    delay(1500);
                }
                else {
                    Serial.printf("[SCAN] Finger search error: 0x%02X\n", p);
                    delay(800);
                }
            }
            else {
                Serial.printf("[SCAN] Feature generation error (image2Tz): 0x%02X\n", p);
                delay(400);
            }
        }
    }

    delay(50);
}
