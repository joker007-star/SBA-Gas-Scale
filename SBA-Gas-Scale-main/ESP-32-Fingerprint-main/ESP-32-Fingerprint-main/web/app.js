// =====================================================================
// SBA BIOMETRIC ATTENDANCE CLOUD PORTAL — app.js
// Firebase Realtime DB + ESP32 Local REST API
// =====================================================================

// --- Firebase Config ---
const firebaseConfig = {
    apiKey:            "AIzaSyBXDD_Fzk0g3kPrh2Ee19v-lfGDAvwzayU",
    authDomain:        "sba-fingerprint.firebaseapp.com",
    projectId:         "sba-fingerprint",
    storageBucket:     "sba-fingerprint.firebasestorage.app",
    messagingSenderId: "104489956286",
    appId:             "1:104489956286:web:5a9c6ef5341caccc03bfef",
    measurementId:     "G-KSYXNMN16G",
    databaseURL:       "https://sba-fingerprint-default-rtdb.firebaseio.com"
};

// --- State ---
let firebaseActive = false;
let db = null;

let studentData  = {};   // key = S-ID
let lecturerData = {};   // key = L-ID
let courseData   = [];   // array of {code, title, lecturers:[L-IDs]}
let logsData     = [];   // array of log objects

let currentActiveTab = 'dashboard-tab';

// Detect if page is loaded from the ESP32 local IP
const isLocalEsp32 = (
    window.location.hostname === '192.168.4.1' ||
    window.location.hostname.startsWith('10.') ||
    (window.location.hostname.startsWith('192.168.') && window.location.hostname !== '192.168.4.1')
);

// Enrollment polling
let stuEnrollInterval  = null;
let lectEnrollInterval = null;
let spEnrollInterval   = null;
let spotPollInterval   = null;

// =====================================================================
// INIT
// =====================================================================

document.addEventListener('DOMContentLoaded', () => {
    initFirebase();
    pollDeviceStatus();
    setInterval(pollDeviceStatus, 4000);

    if (isLocalEsp32) {
        // When running from local ESP32 page, also sync local REST data
        setInterval(() => {
            if (currentActiveTab === 'students-tab')  loadStudentsLocal();
            if (currentActiveTab === 'lecturers-tab') loadLecturersLocal();
            if (currentActiveTab === 'courses-tab')   loadCoursesLocal();
        }, 8000);
        // Poll for on-spot enrolment flag
        spotPollInterval = setInterval(pollOnSpotEnrollFlag, 2000);
    }
});

// =====================================================================
// FIREBASE INIT
// =====================================================================

function initFirebase() {
    try {
        if (typeof firebase === 'undefined') throw new Error('Firebase SDK not loaded');
        firebase.initializeApp(firebaseConfig);
        db = firebase.database();
        firebaseActive = true;

        const dot = document.getElementById('firebaseDot');
        const txt = document.getElementById('firebaseStatusText');
        if (dot) dot.className = 'dot online';
        if (txt) txt.innerText  = 'Firebase: Online 🔥';

        listenCloudData();
    } catch (e) {
        console.warn('[Firebase] Init failed:', e);
        const dot = document.getElementById('firebaseDot');
        const txt = document.getElementById('firebaseStatusText');
        if (dot) dot.className = 'dot offline';
        if (txt) txt.innerText  = 'Firebase: Offline';
    }
}

// =====================================================================
// FIREBASE REALTIME LISTENERS
// =====================================================================

function listenCloudData() {
    if (!firebaseActive || !db) return;

    // Students
    db.ref('students').on('value', snap => {
        studentData = snap.val() || {};
        renderStudents();
        updateDashboardStats();
    });

    // Lecturers
    db.ref('lecturers').on('value', snap => {
        lecturerData = snap.val() || {};
        renderLecturers();
        renderCourseCheckboxes();
        updateDashboardStats();
    });

    // Courses
    db.ref('courses').on('value', snap => {
        const val = snap.val() || {};
        courseData = Object.entries(val).map(([code, data]) => ({
            code,
            title:     data.title     || code,
            lecturers: data.lecturers ? String(data.lecturers).split(',').map(s => s.trim()) : []
        }));
        renderCourses();
        populateCourseDropdown();
        populateLogCourseFilter();
        updateDashboardStats();
    });

    // Attendance Logs
    db.ref('attendance_logs').on('value', snap => {
        const val = snap.val() || {};
        logsData = Object.values(val);
        renderCloudLogs();
        updateActivityFeed();
        updateDashboardStats();
    });

    // Session State
    db.ref('session_state').on('value', snap => {
        const val = snap.val();
        if (val) updateSessionUI(val);
    });
}

// =====================================================================
// DEVICE STATUS POLLING (ESP32 REST)
// =====================================================================

async function pollDeviceStatus() {
    try {
        const res = await fetch('/system-status');
        if (!res.ok) throw new Error('HTTP ' + res.status);
        const st = await res.json();

        // Device sidebar dot
        const devDot = document.getElementById('deviceDot');
        const devTxt = document.getElementById('deviceStatusText');
        if (devDot) devDot.className = 'dot online';
        if (devTxt) devTxt.innerText  = 'ESP32: Online';

        // Sensor sidebar
        const ssDot = document.getElementById('sensorSidebarDot');
        const ssTxt = document.getElementById('sensorSidebarText');
        if (ssDot) ssDot.className = st.sensorConnected ? 'dot online' : 'dot offline';
        if (ssTxt) ssTxt.innerText = st.sensorConnected ? `Sensor: OK (${st.templateCount})` : 'Sensor: OFFLINE';

        // Header badges
        const wBadge = document.getElementById('wifiModeBadge');
        if (wBadge) {
            if (st.wifiMode === 'STA') {
                wBadge.style.background    = 'rgba(16,185,129,.15)';
                wBadge.style.borderColor   = 'rgba(16,185,129,.4)';
                wBadge.style.color         = '#10b981';
                wBadge.innerText           = '📶 STA — ' + st.wifiIP;
            } else {
                wBadge.style.background    = 'rgba(245,158,11,.15)';
                wBadge.style.borderColor   = 'rgba(245,158,11,.4)';
                wBadge.style.color         = '#f59e0b';
                wBadge.innerText           = '📡 AP — ' + st.wifiIP;
            }
        }

        const sBadge = document.getElementById('sensorTopBadge');
        if (sBadge) {
            if (st.sensorConnected) {
                sBadge.style.background    = 'rgba(16,185,129,.15)';
                sBadge.style.borderColor   = 'rgba(16,185,129,.4)';
                sBadge.style.color         = '#10b981';
                sBadge.innerText           = `☝️ Sensor: OK (${st.templateCount})`;
            } else {
                sBadge.style.background    = 'rgba(239,68,68,.15)';
                sBadge.style.borderColor   = 'rgba(239,68,68,.4)';
                sBadge.style.color         = '#ef4444';
                sBadge.innerText           = '☝️ Sensor: OFFLINE';
            }
        }

        // Clock
        if (st.rtcTime) {
            const clk = document.getElementById('liveClockTop');
            if (clk) clk.innerText = st.rtcTime.split(' ')[1] || st.rtcTime;
        }

        // Settings tab live data
        const sMode  = document.getElementById('settingsWifiMode');
        const sIP    = document.getElementById('settingsWifiIP');
        const sSens  = document.getElementById('settingsSensorStatus');
        const sFB    = document.getElementById('settingsFirebase');
        const sSess  = document.getElementById('settingsSession');
        const sTmpl  = document.getElementById('settingsTemplates');
        const sRTC   = document.getElementById('settingsRTC');
        if (sMode) { sMode.innerText = st.wifiMode; sMode.style.color = st.wifiMode === 'STA' ? '#10b981' : '#f59e0b'; }
        if (sIP)   sIP.innerText   = st.wifiIP;
        if (sSens) { sSens.innerText = st.sensorConnected ? `Connected (${st.templateCount} templates)` : 'OFFLINE'; sSens.style.color = st.sensorConnected ? '#10b981' : '#ef4444'; }
        if (sFB)   { sFB.innerText = st.firebaseReady ? 'Connected 🔥' : 'Offline'; sFB.style.color = st.firebaseReady ? '#10b981' : '#ef4444'; }
        if (sSess) { sSess.innerText = st.sessionOpen ? ('OPEN — ' + st.activeCourse) : 'CLOSED'; sSess.style.color = st.sessionOpen ? '#10b981' : '#ef4444'; }
        if (sTmpl) sTmpl.innerText = st.templateCount;
        if (sRTC)  sRTC.innerText  = st.rtcTime;

        // Dashboard session card
        const dss   = document.getElementById('dashSessionState');
        const dsc   = document.getElementById('dashSessionCourse');
        const dsl   = document.getElementById('dashSessionLect');
        if (dss) { dss.innerText = st.sessionOpen ? 'OPEN' : 'CLOSED'; dss.style.color = st.sessionOpen ? '#10b981' : '#ef4444'; }
        if (dsc) dsc.innerText = st.sessionOpen ? st.activeCourse : 'No active session';
        if (dsl) dsl.innerText = st.sessionOpen ? ('Lecturer: ' + st.activeLecturer) : '';

    } catch (e) {
        const devDot = document.getElementById('deviceDot');
        const devTxt = document.getElementById('deviceStatusText');
        if (devDot) devDot.className = 'dot offline';
        if (devTxt) devTxt.innerText  = 'ESP32: Offline';
    }
}

// =====================================================================
// NAVIGATION
// =====================================================================

function switchNav(tabId, el) {
    currentActiveTab = tabId;
    document.querySelectorAll('.nav-item').forEach(item => item.classList.remove('active'));
    document.querySelectorAll('.tab-content').forEach(tab  => tab.classList.remove('active'));

    if (el) el.classList.add('active');
    const tab = document.getElementById(tabId);
    if (tab) tab.classList.add('active');

    const labels = {
        'dashboard-tab':  ['System Overview',           'Real-time biometric attendance cloud control dashboard'],
        'attend-tab':     ['Attendance Session',         'Open / close lecture attendance and monitor live scans'],
        'students-tab':   ['Student Registry',           'Enroll and manage student biometric fingerprint profiles'],
        'lecturers-tab':  ['Lecturer Registry',          'Register and manage authorized lecturer fingerprints'],
        'courses-tab':    ['Course Management',          'Register courses and assign authorized lecturers'],
        'logs-tab':       ['Attendance Logs',            'Historical class records with CSV export and Firebase cloud sync'],
        'settings-tab':   ['Wi-Fi & System Settings',   'Configure ESP32 network credentials and view system status']
    };
    if (labels[tabId]) {
        const pt = document.getElementById('pageTitle');
        const ps = document.getElementById('pageSubtitle');
        if (pt) pt.innerText = labels[tabId][0];
        if (ps) ps.innerText = labels[tabId][1];
    }

    // Load Wi-Fi settings when Settings tab is opened
    if (tabId === 'settings-tab') loadWifiConfig();
}

// =====================================================================
// SESSION CONTROL
// =====================================================================

function updateSessionUI(data) {
    const badge     = document.getElementById('sessionBadge');
    const setupArea = document.getElementById('sessionSetupArea');
    const activeArea= document.getElementById('sessionActiveArea');
    const authArea  = document.getElementById('sessionAuthPendingArea');
    if (!badge) return;

    const state = data.state || 'closed';

    if (state === 'closed') {
        badge.className = 'badge badge-closed';
        badge.innerText = 'CLOSED';
        if (setupArea)  setupArea.style.display  = 'block';
        if (activeArea) activeArea.style.display = 'none';
        if (authArea)   authArea.style.display   = 'none';
    } else if (state === 'open') {
        badge.className = 'badge badge-open';
        badge.innerText = 'OPEN';
        if (setupArea)  setupArea.style.display  = 'none';
        if (activeArea) activeArea.style.display = 'block';
        if (authArea)   authArea.style.display   = 'none';
        const acn = document.getElementById('activeCourseName');
        const aln = document.getElementById('activeLecturerName');
        if (acn) acn.innerText = 'Active Course: ' + (data.course || 'N/A');
        if (aln) aln.innerText = 'Opened by Lecturer ID: ' + (data.lecturer_id || 'N/A');

        // Poll for on-spot flag when session is open
        if (!spotPollInterval) {
            spotPollInterval = setInterval(pollOnSpotEnrollFlag, 2000);
        }
    } else if (state.startsWith('waiting')) {
        badge.className = 'badge badge-waiting';
        badge.innerText = 'WAITING';
        if (setupArea)  setupArea.style.display  = 'none';
        if (activeArea) activeArea.style.display = 'none';
        if (authArea)   authArea.style.display   = 'block';
        const at = document.getElementById('authPendingTitle');
        const ad = document.getElementById('authPendingDesc');
        if (at) at.innerText = state === 'waiting_start' ? 'Waiting: Lecturer to Open' : 'Waiting: Lecturer to Close';
        if (ad) ad.innerText = 'Lecturer should place finger on the physical ESP32 sensor for course: ' + (data.course || '');
    }

    // Live status message
    if (data.lastScan && data.lastScan.type !== 'none') {
        const msgEl = document.getElementById('liveStatusMsg');
        if (msgEl) {
            if (data.lastScan.success) {
                msgEl.innerHTML = `<span style="color:var(--success);">✅ Verified: ${data.lastScan.name} (${data.lastScan.matNo})</span>`;
            } else {
                msgEl.innerHTML = `<span style="color:var(--danger);">❌ Denied: ${data.lastScan.message}</span>`;
            }
        }
    }
}

async function requestSessionOpen() {
    const course = document.getElementById('courseSelect').value;
    if (!course) return alert('Please select a course first!');

    if (firebaseActive && db) {
        db.ref('session_state').set({
            state:       'waiting_start',
            course:      course,
            timestamp:   Math.floor(Date.now() / 1000)
        });
        alert(`Session open requested for "${course}". Assigned lecturer should now scan their finger on the physical sensor!`);
    } else {
        // Local fallback (requires lecturer ID typed)
        const lectID = prompt('Enter your Lecturer ID to open session:');
        if (!lectID) return;
        try {
            const res  = await fetch('/session/open', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `course=${encodeURIComponent(course)}&lecturerID=${encodeURIComponent(lectID)}`
            });
            alert(await res.text());
        } catch (e) { alert('Could not connect to ESP32'); }
    }
}

async function requestSessionClose() {
    if (firebaseActive && db) {
        db.ref('session_state').set({ state: 'waiting_end', course: '' });
        alert('Session close requested. Lecturer should scan finger on physical sensor to confirm!');
    } else {
        const lectID = prompt('Enter your Lecturer ID to close session:');
        if (!lectID) return;
        try {
            const res = await fetch('/session/close', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `lecturerID=${encodeURIComponent(lectID)}`
            });
            alert(await res.text());
        } catch (e) { alert('Could not connect to ESP32'); }
    }
    if (spotPollInterval) { clearInterval(spotPollInterval); spotPollInterval = null; }
    const onSpotCard = document.getElementById('onSpotCard');
    if (onSpotCard) onSpotCard.style.display = 'none';
}

async function cancelSessionAuth() {
    if (firebaseActive && db) {
        db.ref('session_state').set({ state: 'closed', course: '' });
    } else {
        try {
            await fetch('/session/close', { method: 'POST' });
        } catch (e) {}
    }
}

// =====================================================================
// ON-SPOT ENROLMENT
// =====================================================================

async function pollOnSpotEnrollFlag() {
    try {
        const res = await fetch('/spot-enroll-status');
        if (!res.ok) return;
        const st  = await res.json();
        const card = document.getElementById('onSpotCard');
        if (!card) return;
        card.style.display = st.pending ? 'block' : 'none';
    } catch (e) {}
}

async function submitOnSpotEnroll(e) {
    e.preventDefault();
    const name  = document.getElementById('spName').value.trim();
    const mat   = document.getElementById('spMat').value.trim();
    const cls   = document.getElementById('spClass').value;
    const slot  = document.getElementById('spSlot').value;
    if (!name || !mat || !slot) return alert('Fill all fields!');

    const progressArea = document.getElementById('spProgressArea');
    if (progressArea) progressArea.style.display = 'block';
    _updateProgress('sp', 10, 'Sending enrollment request...');

    try {
        const res  = await fetch('/enroll/student', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `name=${encodeURIComponent(name)}&mat=${encodeURIComponent(mat)}&sclass=${encodeURIComponent(cls)}&slot=${slot}`
        });
        const data = await res.json();
        _updateProgress('sp', 25, 'ID: ' + data.genID + ' — Place finger on sensor!');

        // Dismiss the flag from firmware
        await fetch('/spot-enroll-status/dismiss', { method: 'POST' });

        if (spEnrollInterval) clearInterval(spEnrollInterval);
        spEnrollInterval = setInterval(() => _pollEnrollProgress('sp'), 1000);
    } catch (err) {
        _updateProgress('sp', 0, 'Error: Could not reach ESP32');
    }
}

async function dismissOnSpot() {
    try { await fetch('/spot-enroll-status/dismiss', { method: 'POST' }); } catch (e) {}
    const card = document.getElementById('onSpotCard');
    if (card) card.style.display = 'none';
}

// =====================================================================
// STUDENTS
// =====================================================================

function renderStudents() {
    const tbody    = document.getElementById('studentTableBody');
    const countEl  = document.getElementById('stuCountBadge');
    const statEl   = document.getElementById('statStudents');
    if (!tbody) return;

    const keys = Object.keys(studentData);
    if (countEl) countEl.innerText = keys.length;
    if (statEl)  statEl.innerText  = keys.length;

    if (keys.length === 0) {
        tbody.innerHTML = '<tr><td colspan="6" style="text-align:center;color:var(--text-secondary);padding:2rem;">No students enrolled yet.</td></tr>';
        return;
    }

    tbody.innerHTML = '';
    keys.sort().forEach(id => {
        const s  = studentData[id];
        const tr = document.createElement('tr');
        tr.innerHTML = `
            <td><strong style="color:#a5b4fc;">${id}</strong></td>
            <td>${s.name || '—'}</td>
            <td><code style="font-size:.8rem;">${s.mat || s.matNo || '—'}</code></td>
            <td><span style="padding:.2rem .5rem;background:rgba(6,182,212,.15);color:#22d3ee;border-radius:5px;font-size:.8rem;font-weight:700;">${s.sclass || 'ND1'}</span></td>
            <td>${s.slot || s.id || '—'}</td>
            <td><button class="btn btn-danger" style="padding:.2rem .6rem;font-size:.75rem;" onclick="deleteStudent('${id}')">Delete</button></td>
        `;
        tbody.appendChild(tr);
    });
}

async function enrollStudent(e) {
    e.preventDefault();
    const name  = document.getElementById('stuName').value.trim();
    const mat   = document.getElementById('stuMat').value.trim();
    const cls   = document.getElementById('stuClass').value;
    const slot  = parseInt(document.getElementById('stuId').value);
    const btn   = document.getElementById('stuSubmitBtn');

    btn.disabled = true;
    const prog = document.getElementById('stuEnrollProgress');
    if (prog) prog.style.display = 'block';
    _updateProgress('stu', 10, 'Sending enrollment request to ESP32...');

    try {
        const res  = await fetch('/enroll/student', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `name=${encodeURIComponent(name)}&mat=${encodeURIComponent(mat)}&sclass=${encodeURIComponent(cls)}&slot=${slot}`
        });
        const data = await res.json();
        const genIDEl = document.getElementById('stuGenIDDisplay');
        if (genIDEl) genIDEl.innerText = '🎫 Auto-Generated ID: ' + data.genID;
        _updateProgress('stu', 25, 'Place finger on ESP32 sensor!');

        if (stuEnrollInterval) clearInterval(stuEnrollInterval);
        stuEnrollInterval = setInterval(() => _pollEnrollProgress('stu'), 1000);
    } catch (err) {
        _updateProgress('stu', 0, '❌ Could not reach ESP32. Saving to Firebase only...');
        if (firebaseActive && db) {
            const genID = 'S_PENDING_' + Date.now();
            db.ref('students/' + genID).set({ name, mat, sclass: cls, slot });
            alert('Student saved to Firebase! Fingerprint enrollment requires ESP32 connection.');
        }
        btn.disabled = false;
    }
}

async function deleteStudent(id) {
    if (!confirm(`Delete student ${id}?`)) return;
    if (firebaseActive && db) db.ref('students/' + id).remove();
    try {
        await fetch('/students/delete', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `id=${encodeURIComponent(id)}`
        });
    } catch (e) {}
}

// Load students from local ESP32 (when at local IP)
async function loadStudentsLocal() {
    try {
        const res  = await fetch('/students/list');
        const list = await res.json();
        list.forEach(s => { studentData[s.id] = s; });
        renderStudents();
    } catch (e) {}
}

// =====================================================================
// LECTURERS
// =====================================================================

function renderLecturers() {
    const tbody   = document.getElementById('lecturerTableBody');
    const countEl = document.getElementById('lectCountBadge');
    const statEl  = document.getElementById('statLecturers');
    if (!tbody) return;

    const keys = Object.keys(lecturerData);
    if (countEl) countEl.innerText = keys.length;
    if (statEl)  statEl.innerText  = keys.length;

    if (keys.length === 0) {
        tbody.innerHTML = '<tr><td colspan="5" style="text-align:center;color:var(--text-secondary);padding:2rem;">No lecturers registered yet.</td></tr>';
        return;
    }

    tbody.innerHTML = '';
    keys.sort().forEach(id => {
        const l  = lecturerData[id];
        const tr = document.createElement('tr');
        tr.innerHTML = `
            <td><strong style="color:#22d3ee;">${id}</strong></td>
            <td>${l.name || '—'}</td>
            <td>${l.dept || '—'}</td>
            <td>${l.slot || '—'}</td>
            <td><button class="btn btn-danger" style="padding:.2rem .6rem;font-size:.75rem;" onclick="deleteLecturer('${id}')">Delete</button></td>
        `;
        tbody.appendChild(tr);
    });

    renderCourseCheckboxes();
}

async function enrollLecturer(e) {
    e.preventDefault();
    const name  = document.getElementById('lectName').value.trim();
    const dept  = document.getElementById('lectDept').value.trim();
    const slot  = parseInt(document.getElementById('lectId').value);
    const btn   = document.getElementById('lectSubmitBtn');

    btn.disabled = true;
    const prog = document.getElementById('lectEnrollProgress');
    if (prog) prog.style.display = 'block';
    _updateProgress('lect', 10, 'Sending enrollment request to ESP32...');

    try {
        const res  = await fetch('/enroll/lecturer', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `name=${encodeURIComponent(name)}&dept=${encodeURIComponent(dept)}&slot=${slot}`
        });
        const data = await res.json();
        const genIDEl = document.getElementById('lectGenIDDisplay');
        if (genIDEl) genIDEl.innerText = '🎫 Auto-Generated ID: ' + data.genID;
        _updateProgress('lect', 25, 'Place finger on ESP32 sensor!');

        if (lectEnrollInterval) clearInterval(lectEnrollInterval);
        lectEnrollInterval = setInterval(() => _pollEnrollProgress('lect'), 1000);
    } catch (err) {
        _updateProgress('lect', 0, '❌ Could not reach ESP32. Saving to Firebase only...');
        if (firebaseActive && db) {
            const genID = 'L_PENDING_' + Date.now();
            db.ref('lecturers/' + genID).set({ name, dept, slot });
            alert('Lecturer saved to Firebase! Fingerprint enrollment requires ESP32 connection.');
        }
        btn.disabled = false;
    }
}

async function deleteLecturer(id) {
    if (!confirm(`Delete lecturer ${id}?`)) return;
    if (firebaseActive && db) db.ref('lecturers/' + id).remove();
}

async function loadLecturersLocal() {
    try {
        const res  = await fetch('/lecturers/list');
        const list = await res.json();
        list.forEach(l => { lecturerData[l.id] = l; });
        renderLecturers();
    } catch (e) {}
}

// =====================================================================
// COURSES
// =====================================================================

function renderCourseCheckboxes() {
    const container = document.getElementById('lecturerCheckboxes');
    if (!container) return;

    const keys = Object.keys(lecturerData);
    if (keys.length === 0) {
        container.innerHTML = '<span style="color:var(--text-secondary);font-size:.85rem;">No lecturers registered yet. Register a lecturer first.</span>';
        return;
    }

    container.innerHTML = '';
    keys.sort().forEach(id => {
        const l   = lecturerData[id];
        const div = document.createElement('div');
        div.style.cssText = 'display:flex;align-items:center;gap:.5rem;margin-bottom:.4rem;font-size:.88rem;';
        div.innerHTML = `
            <input type="checkbox" id="lcb_${id}" value="${id}" style="width:auto;margin:0;">
            <label for="lcb_${id}" style="cursor:pointer;">${id} — ${l.name} (${l.dept})</label>
        `;
        container.appendChild(div);
    });
}

function renderCourses() {
    const tbody   = document.getElementById('courseTableBody');
    const countEl = document.getElementById('courseCountBadge');
    const statEl  = document.getElementById('statCourses');
    if (!tbody) return;

    if (countEl) countEl.innerText = courseData.length;
    if (statEl)  statEl.innerText  = courseData.length;

    if (courseData.length === 0) {
        tbody.innerHTML = '<tr><td colspan="4" style="text-align:center;color:var(--text-secondary);padding:2rem;">No courses registered yet.</td></tr>';
        return;
    }

    tbody.innerHTML = '';
    courseData.forEach(c => {
        const lectNames = c.lecturers.map(lid => {
            const l = lecturerData[lid];
            return l ? `${lid} (${l.name})` : lid;
        }).join(', ') || '—';

        const tr = document.createElement('tr');
        tr.innerHTML = `
            <td><strong>${c.code}</strong></td>
            <td>${c.title}</td>
            <td style="font-size:.82rem;">${lectNames}</td>
            <td><button class="btn btn-danger" style="padding:.2rem .6rem;font-size:.75rem;" onclick="deleteCourse('${c.code}')">Delete</button></td>
        `;
        tbody.appendChild(tr);
    });
}

function populateCourseDropdown() {
    const select = document.getElementById('courseSelect');
    if (!select) return;
    const current = select.value;
    select.innerHTML = '<option value="">— Select a course —</option>';
    courseData.forEach(c => {
        const opt = document.createElement('option');
        opt.value   = c.code;
        opt.innerText = `${c.code} — ${c.title}`;
        if (c.code === current) opt.selected = true;
        select.appendChild(opt);
    });
}

function populateLogCourseFilter() {
    const sel = document.getElementById('logCourseFilter');
    if (!sel) return;
    sel.innerHTML = '<option value="ALL">All Courses</option>';
    courseData.forEach(c => {
        const opt = document.createElement('option');
        opt.value   = c.code;
        opt.innerText = c.code;
        sel.appendChild(opt);
    });
}

async function registerCourse(e) {
    e.preventDefault();
    const code  = document.getElementById('courseCode').value.trim().toUpperCase();
    const title = document.getElementById('courseTitle').value.trim();
    const selected = [];
    document.querySelectorAll('#lecturerCheckboxes input:checked').forEach(cb => selected.push(cb.value));
    const lecturersStr = selected.join(',');

    if (!code || !title) return alert('Course code and title are required!');

    // Save to Firebase
    if (firebaseActive && db) {
        db.ref('courses/' + code).set({ title, lecturers: lecturersStr });
    }

    // Save to ESP32 local
    try {
        await fetch('/courses/add', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `code=${encodeURIComponent(code)}&title=${encodeURIComponent(title)}&lecturers=${encodeURIComponent(lecturersStr)}`
        });
    } catch (e2) {}

    alert(`Course "${code} — ${title}" registered!`);
    document.getElementById('courseForm').reset();
    renderCourseCheckboxes();
}

async function deleteCourse(code) {
    if (!confirm(`Delete course ${code}?`)) return;
    if (firebaseActive && db) db.ref('courses/' + code).remove();
}

async function loadCoursesLocal() {
    try {
        const res  = await fetch('/courses/list');
        const list = await res.json();
        courseData = list.map(c => ({
            code:      c.code,
            title:     c.title,
            lecturers: c.lecturers ? c.lecturers.split(',').map(s => s.trim()) : []
        }));
        renderCourses();
        populateCourseDropdown();
    } catch (e) {}
}

// =====================================================================
// ATTENDANCE LOGS
// =====================================================================

function renderCloudLogs(filtered) {
    const tbody   = document.getElementById('logsTableBody');
    const statEl  = document.getElementById('statLogs');
    if (!tbody) return;

    const display = filtered || logsData;
    if (statEl && !filtered) statEl.innerText = logsData.length;

    if (display.length === 0) {
        tbody.innerHTML = '<tr><td colspan="6" style="text-align:center;color:var(--text-secondary);padding:2rem;">No attendance records found.</td></tr>';
        return;
    }

    tbody.innerHTML = '';
    display.slice().reverse().forEach(item => {
        const tr  = document.createElement('tr');
        const cls = (item.sclass || item.student_class || 'ND1').toUpperCase();
        tr.innerHTML = `
            <td><strong style="color:#a5b4fc;">${item.student_id || item.id || 'N/A'}</strong></td>
            <td>${item.student_name || item.name || 'Unknown'}</td>
            <td><code style="font-size:.8rem;">${item.matric_no || item.matNo || item.mat || 'N/A'}</code></td>
            <td><span style="padding:.2rem .5rem;background:rgba(6,182,212,.15);color:#22d3ee;border-radius:5px;font-size:.8rem;font-weight:700;">${cls}</span></td>
            <td>${item.course || 'N/A'}</td>
            <td style="font-size:.8rem;">${item.timestamp || item.time || 'N/A'}</td>
        `;
        tbody.appendChild(tr);
    });
}

function filterLogsByClass() {
    const classSel  = document.getElementById('logClassFilter');
    const courseSel = document.getElementById('logCourseFilter');
    const cls       = classSel  ? classSel.value  : 'ALL';
    const course    = courseSel ? courseSel.value : 'ALL';

    let filtered = logsData;
    if (cls !== 'ALL')    filtered = filtered.filter(i => (i.sclass || i.student_class || 'ND1').toUpperCase() === cls);
    if (course !== 'ALL') filtered = filtered.filter(i => (i.course || '') === course);
    renderCloudLogs(filtered);
}

function updateActivityFeed() {
    const actList = document.getElementById('recentActivityList');
    if (!actList) return;
    actList.innerHTML = '';
    if (logsData.length === 0) {
        actList.innerHTML = '<div class="empty-state">No recent activity.</div>';
        return;
    }
    logsData.slice(-10).reverse().forEach(item => {
        const div = document.createElement('div');
        div.style.cssText = 'padding:.6rem 0;border-bottom:1px solid var(--card-border);font-size:.875rem;';
        div.innerHTML = `<strong>${item.student_name || item.name || 'Student'}</strong> (${item.matric_no || item.mat || 'N/A'}) signed in for <strong>${item.course || 'N/A'}</strong> <span style="color:var(--text-secondary);">${item.timestamp || item.time || ''}</span>`;
        actList.prepend(div);
    });
}

function exportCSV() {
    if (logsData.length === 0) return alert('No attendance records to export!');
    let csv = 'Student ID,Name,Matric Number,Class,Course,Timestamp\n';
    logsData.forEach(item => {
        csv += `"${item.student_id || item.id || ''}","${item.student_name || item.name || ''}","${item.matric_no || item.mat || ''}","${(item.sclass || 'ND1').toUpperCase()}","${item.course || ''}","${item.timestamp || item.time || ''}"\n`;
    });
    const blob = new Blob([csv], { type: 'text/csv' });
    const url  = URL.createObjectURL(blob);
    const a    = document.createElement('a');
    a.href = url; a.download = `attendance_${new Date().toISOString().slice(0, 10)}.csv`; a.click();
}

function sendEmailReport() {
    const email = (document.getElementById('emailRecipient') || {}).value?.trim() || '';
    if (!email) return alert('Please enter a recipient email address!');

    const cls    = (document.getElementById('logClassFilter') || {}).value || 'ALL';
    const course = (document.getElementById('logCourseFilter') || {}).value || 'ALL';
    let filtered = logsData;
    if (cls !== 'ALL')    filtered = filtered.filter(i => (i.sclass || 'ND1').toUpperCase() === cls);
    if (course !== 'ALL') filtered = filtered.filter(i => (i.course || '') === course);

    if (filtered.length === 0) return alert('No records match the current filter!');

    const subject = encodeURIComponent(`Smart Attendance Report — ${course !== 'ALL' ? course : cls} (${new Date().toLocaleDateString()})`);
    let body = `SBA SMART ATTENDANCE REPORT\nClass: ${cls} | Course: ${course}\nDate: ${new Date().toLocaleString()}\nTotal: ${filtered.length} student(s)\n${'='.repeat(48)}\n\n`;
    filtered.forEach((item, i) => {
        body += `${i + 1}. ${item.student_name || item.name} (${item.matric_no || item.mat}) | ${item.sclass || 'ND1'} | ${item.course || 'N/A'} | ${item.timestamp || item.time}\n`;
    });
    body += `\n${'='.repeat(48)}\nGenerated by SBA Biometric Cloud Portal`;

    window.open(`mailto:${email}?subject=${subject}&body=${encodeURIComponent(body)}`, '_blank');
}

// =====================================================================
// WI-FI SETTINGS
// =====================================================================

async function loadWifiConfig() {
    try {
        const res = await fetch('/wifi-config');
        if (!res.ok) return;
        const cfg = await res.json();
        const input = document.getElementById('wifiSSID');
        if (input && cfg.ssid) input.value = cfg.ssid;
    } catch (e) {}
}

async function saveWifiConfig(e) {
    e.preventDefault();
    const ssid  = document.getElementById('wifiSSID').value.trim();
    const pass  = document.getElementById('wifiPass').value;
    const alertBox = document.getElementById('wifiAlertBox');

    if (!ssid) return alert('SSID cannot be empty!');

    if (alertBox) {
        alertBox.style.display    = 'block';
        alertBox.style.background = 'rgba(245,158,11,.15)';
        alertBox.style.border     = '1px solid rgba(245,158,11,.4)';
        alertBox.style.color      = '#f59e0b';
        alertBox.innerText        = '⏳ Saving Wi-Fi credentials and rebooting ESP32...';
    }

    try {
        const res = await fetch('/wifi-config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `ssid=${encodeURIComponent(ssid)}&pass=${encodeURIComponent(pass)}`
        });
        const msg = await res.text();
        if (alertBox) {
            alertBox.style.background = 'rgba(16,185,129,.15)';
            alertBox.style.border     = '1px solid rgba(16,185,129,.4)';
            alertBox.style.color      = '#10b981';
            alertBox.innerText        = '✅ ' + msg;
        }
    } catch (err) {
        if (alertBox) {
            alertBox.style.background = 'rgba(239,68,68,.15)';
            alertBox.style.border     = '1px solid rgba(239,68,68,.4)';
            alertBox.style.color      = '#ef4444';
            alertBox.innerText        = '❌ Could not reach ESP32. Are you connected to the correct network?';
        }
    }
}

async function testEsp32Connection() {
    const alertBox = document.getElementById('wifiAlertBox');
    if (alertBox) {
        alertBox.style.display    = 'block';
        alertBox.style.background = 'rgba(99,102,241,.15)';
        alertBox.style.border     = '1px solid rgba(99,102,241,.4)';
        alertBox.style.color      = '#a5b4fc';
        alertBox.innerText        = '🔗 Testing connection...';
    }
    try {
        const res = await fetch('/system-status');
        const st  = await res.json();
        if (alertBox) {
            alertBox.style.background = 'rgba(16,185,129,.15)';
            alertBox.style.border     = '1px solid rgba(16,185,129,.4)';
            alertBox.style.color      = '#10b981';
            alertBox.innerText        = `✅ ESP32 Online! Mode: ${st.wifiMode} | IP: ${st.wifiIP} | Sensor: ${st.sensorConnected ? 'Connected' : 'OFFLINE'}`;
        }
    } catch (e) {
        if (alertBox) {
            alertBox.style.background = 'rgba(239,68,68,.15)';
            alertBox.style.border     = '1px solid rgba(239,68,68,.4)';
            alertBox.style.color      = '#ef4444';
            alertBox.innerText        = '❌ ESP32 unreachable. Check your Wi-Fi connection and device power.';
        }
    }
}

// =====================================================================
// ENROLLMENT PROGRESS HELPERS
// =====================================================================

function _updateProgress(prefix, pct, msg) {
    const fill = document.getElementById(prefix + 'ProgressFill');
    const txt  = document.getElementById(prefix + 'StatusTxt');
    const pctEl= document.getElementById(prefix + 'Pct');
    if (fill)  fill.style.width = pct + '%';
    if (txt)   txt.innerText   = msg;
    if (pctEl) pctEl.innerText = pct + '%';
}

async function _pollEnrollProgress(prefix) {
    try {
        const res = await fetch('/enroll-status');
        const st  = await res.json();

        let pct = 30;
        if (st.step === 2) pct = 60;
        else if (st.step === 3) pct = 85;

        _updateProgress(prefix, pct, st.msg);

        if (!st.enrolling && st.step === 0) {
            const success = st.msg.toLowerCase().includes('success') || st.msg.toLowerCase().includes('enrolled');
            _updateProgress(prefix, success ? 100 : pct, st.msg);

            if (prefix === 'stu')  {
                clearInterval(stuEnrollInterval);
                stuEnrollInterval = null;
                const btn = document.getElementById('stuSubmitBtn');
                if (btn) btn.disabled = false;
                if (success && db) {
                    // Firebase already updated by firmware; re-render from FB
                }
            }
            if (prefix === 'lect') {
                clearInterval(lectEnrollInterval);
                lectEnrollInterval = null;
                const btn = document.getElementById('lectSubmitBtn');
                if (btn) btn.disabled = false;
            }
            if (prefix === 'sp') {
                clearInterval(spEnrollInterval);
                spEnrollInterval = null;
                const card = document.getElementById('onSpotCard');
                if (card && success) card.style.display = 'none';
            }
        }
    } catch (e) {}
}

// =====================================================================
// DASHBOARD STATS
// =====================================================================

function updateDashboardStats() {
    const stuStat  = document.getElementById('statStudents');
    const lectStat = document.getElementById('statLecturers');
    const courStat = document.getElementById('statCourses');
    const logStat  = document.getElementById('statLogs');

    if (stuStat)  stuStat.innerText  = Object.keys(studentData).length;
    if (lectStat) lectStat.innerText = Object.keys(lecturerData).length;
    if (courStat) courStat.innerText = courseData.length;
    if (logStat)  logStat.innerText  = logsData.length;
}

async function syncToFirebase() {
    alert('Firebase Realtime Database is active! All records sync automatically in real-time.');
}

// =====================================================================
// FETCH LOGS (for local mode)
// =====================================================================
async function fetchLogs() {
    try {
        const res  = await fetch('/attendance-logs');
        const data = await res.json();
        logsData   = data;
        renderCloudLogs();
    } catch (e) {}
}
