const express = require('express');
const cors = require('cors');
const fs = require('fs');
const zlib = require('zlib');
const app = express();
const PORT = process.env.PORT || 3000;

// Middleware
app.use(cors());
app.use(express.json());
app.use(express.urlencoded({ extended: true }));

// Pre-gzip each page once at startup, mirroring scripts/compress_html.py + the
// const uint8_t arrays in src/compressed_pages.h. Sending a pre-computed buffer
// (instead of piping through a streaming compressor) lets us set a real
// Content-Length instead of Transfer-Encoding: chunked, matching ConfigServer.cpp's
// send_P(), which always knows the final gzipped length up front.
function gzipPage(filename) {
    return zlib.gzipSync(fs.readFileSync(__dirname + '/' + filename), { level: 9 });
}

const gzippedPages = {
    index: gzipPage('index.html'),
    settings: gzipPage('settings.html'),
    notifications: gzipPage('notifications.html'),
    wifiConfig: gzipPage('wifi-config.html'),
    debug: gzipPage('debug.html'),
    ota: gzipPage('ota.html'),
};

function sendGzippedPage(res, buf) {
    res.set('Content-Type', 'text/html');
    res.set('Content-Encoding', 'gzip');
    res.set('Content-Length', buf.length);
    res.end(buf);
}

// Mock state data (simulates NVS storage on ESP32)
let mockState = {
    // WiFi settings
    ssid: 'MyWiFi',
    password: 'password123',
    connected: true,
    ip: '192.168.1.100',
    rssi: -45,
    storedNetworks: ['MyWiFi', 'BoatWiFi'], // mirrors WiFiManager::getStoredSSIDs()

    // Captive portal simulation: 'online' | 'portal' | 'unknown'
    // Set to 'portal' to exercise the sign-in banner + assist flow.
    portalState: 'online',
    portalLoginUrl: '',
    portalAssistActive: false,

    // Sensor calibration
    zeroPoint_mv: 500,
    hasTwoPointCalibration: true,
    secondPoint_mv: 2500,
    secondPoint_cm: 50.0,

    // Current sensor reading (simulated)
    currentMillivolts: 1234.56,
    currentLevel_cm: 25.3,
    sensorValid: true,
    rate_cm_30min: 1.2,  // pre-seeded — real device needs 5+ min to compute this

    // Emergency settings
    emergencyWaterLevel_cm: 30.0,
    emergencyNotifFreq_ms: 900000, // 15 minutes
    urgentEmergencyWaterLevel_cm: 50.0,

    // SMS / Discord
    hasPhoneNumber: true,
    phoneNumber: '+15551234567',
    hasDiscordWebhook: true,
    discordWebhook: 'https://discord.com/api/webhooks/123456789/abcdefghijklmnop',

    // MQTT
    mqttConfigured: false,
    mqttConnected: false,
    mqttHost: '',
    mqttPort: 1883,
    mqttUser: '',
    mqttBaseTopic: 'boat/aabbcc',
    mqttTls: false,
};

// Simulate sensor readings that change over time
setInterval(() => {
    mockState.currentMillivolts = 1200 + Math.random() * 200;
    if (mockState.hasTwoPointCalibration) {
        const slope = (mockState.secondPoint_cm - 0) / (mockState.secondPoint_mv - mockState.zeroPoint_mv);
        mockState.currentLevel_cm = slope * (mockState.currentMillivolts - mockState.zeroPoint_mv);
    } else {
        const defaultSlope = 0.02;
        mockState.currentLevel_cm = (mockState.currentMillivolts - mockState.zeroPoint_mv) * defaultSlope;
    }
    mockState.currentLevel_cm = Math.max(0, Math.min(100, mockState.currentLevel_cm));

    // Slowly drift rate-of-change to simulate realistic trend changes
    mockState.rate_cm_30min += (Math.random() - 0.5) * 0.2;
    mockState.rate_cm_30min = Math.max(-5.0, Math.min(5.0, mockState.rate_cm_30min));
    mockState.rate_cm_30min = Math.round(mockState.rate_cm_30min * 100) / 100;
}, 2000);

// ============================================================================
// PAGE ROUTES — match firmware paths exactly
// ============================================================================

app.get('/', (req, res) => sendGzippedPage(res, gzippedPages.index));
app.get('/settings', (req, res) => sendGzippedPage(res, gzippedPages.settings));
app.get('/notifications-page', (req, res) => sendGzippedPage(res, gzippedPages.notifications));
app.get('/wifi-config', (req, res) => sendGzippedPage(res, gzippedPages.wifiConfig));
app.get('/debug', (req, res) => sendGzippedPage(res, gzippedPages.debug));
app.get('/ota-settings', (req, res) => sendGzippedPage(res, gzippedPages.ota));

// ============================================================================
// SETTINGS INIT ENDPOINT (combined load for settings page)
// ============================================================================

app.get('/settings/init', (req, res) => {
    res.json({
        notifications: {
            hasPhoneNumber: mockState.hasPhoneNumber,
            hasDiscordWebhook: mockState.hasDiscordWebhook,
            mqttConfigured: mockState.mqttConfigured,
            mqttConnected: mockState.mqttConnected,
        },
        emergencyNotifFreq_ms: mockState.emergencyNotifFreq_ms,
        wifi: {
            connected: mockState.connected,
            ssid: mockState.ssid,
        },
        hasTwoPointCalibration: mockState.hasTwoPointCalibration,
    });
});

// ============================================================================
// INIT ENDPOINT (combined load for dashboard)
// ============================================================================

app.get('/init', (req, res) => {
    res.json({
        wifi: {
            connected: mockState.connected,
            ssid: mockState.ssid,
            ip: mockState.ip,
            rssi: mockState.rssi,
            portalState: mockState.portalState,
            portalLoginUrl: mockState.portalLoginUrl,
        },
        sensor: {
            sensorAvailable: true,
            valid: mockState.sensorValid,
            level_cm: mockState.currentLevel_cm,
            rate_cm_30min: mockState.rate_cm_30min,
        },
        thresholds: {
            emergencyWaterLevel_cm: mockState.emergencyWaterLevel_cm,
            urgentEmergencyWaterLevel_cm: mockState.urgentEmergencyWaterLevel_cm,
        },
    });
});

// ============================================================================
// WIFI CONFIGURATION ENDPOINTS
// ============================================================================

app.get('/status', (req, res) => {
    res.json({
        connected: mockState.connected,
        ssid: mockState.ssid,
        ip: mockState.ip,
        rssi: mockState.rssi,
        portalState: mockState.portalState,
        portalLoginUrl: mockState.portalLoginUrl,
    });
});

app.post('/config', (req, res) => {
    const { ssid, password } = req.body;
    console.log(`[CONFIG] WiFi credentials received: SSID=${ssid}`);
    mockState.ssid = ssid;
    mockState.password = password;
    mockState.connected = true;
    if (!mockState.storedNetworks.includes(ssid)) {
        mockState.storedNetworks.push(ssid);
    }
    res.json({ success: true });
});

// GET /wifi/networks — stored SSID list (matches WiFiManager::getStoredSSIDs())
app.get('/wifi/networks', (req, res) => {
    res.json(mockState.storedNetworks);
});

// POST /wifi/remove — remove a stored network by SSID
app.post('/wifi/remove', (req, res) => {
    const ssid = req.body.ssid;
    if (!ssid) {
        res.status(400).json({ success: false, message: 'Missing ssid' });
        return;
    }
    mockState.storedNetworks = mockState.storedNetworks.filter(s => s !== ssid);
    console.log(`[WIFI] Removed stored network: ${ssid}`);
    res.json({ success: true });
});

// GET/POST /wifi/mac — custom STA MAC override (matches ConfigServer::handleWiFiMac)
let mockCustomMac = '';
app.get('/wifi/mac', (req, res) => {
    res.json({ mac: mockCustomMac || '24:6F:28:A1:B2:C3', custom: mockCustomMac });
});
app.post('/wifi/mac', (req, res) => {
    const mac = (req.body.mac || '').trim();
    if (mac && !/^([0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}$/.test(mac)) {
        res.status(400).json({ success: false, message: 'Invalid MAC' });
        return;
    }
    mockCustomMac = mac.toUpperCase().replace(/-/g, ':');
    console.log(`[WIFI] Custom STA MAC: ${mockCustomMac || '(cleared)'}`);
    res.json({ success: true });
});

// ============================================================================
// CAPTIVE PORTAL ASSIST ENDPOINTS
// ============================================================================

app.get('/portal/status', (req, res) => {
    res.json({
        state: mockState.portalState,
        loginUrl: mockState.portalLoginUrl,
        assistActive: mockState.portalAssistActive,
        ssid: mockState.ssid,
    });
});

app.post('/portal/assist', (req, res) => {
    if (!mockState.connected) {
        res.status(409).json({ error: 'Not associated to a network' });
        return;
    }
    mockState.portalAssistActive = true;
    console.log('[PORTAL] Assist mode started');
    res.json({ success: true, loginUrl: mockState.portalLoginUrl });
});

// In the mock, the relay shows a fake marina splash page; POSTing the form
// simulates the portal accepting the sign-in and opening the network.
app.get('/portal/relay', (req, res) => {
    if (!mockState.portalAssistActive) {
        res.status(409).json({ error: 'Portal assist mode not active' });
        return;
    }
    res.send(`<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Marina Guest Wi-Fi (mock)</title></head>
<body style="font-family:sans-serif;margin:0">
<div style="position:sticky;top:0;background:#1c1917;color:#fff;font-size:14px;padding:10px 14px">
<b>BilgeRise</b> · Marina sign-in (mock bar would poll status here)</div>
<div style="max-width:420px;margin:40px auto;padding:0 16px">
<h2>Harbor View Marina</h2>
<p>Mock splash page served via /portal/relay</p>
<form method="POST" action="/portal/relay">
<label><input type="checkbox" required> I accept the terms of service</label><br><br>
<button type="submit" style="padding:10px 20px">Connect</button>
</form></div></body></html>`);
});

app.post('/portal/relay', (req, res) => {
    mockState.portalState = 'online';
    mockState.portalAssistActive = false;
    mockState.portalLoginUrl = '';
    console.log('[PORTAL] Mock sign-in accepted - network now ONLINE');
    res.redirect('/portal/done');
});

app.get('/portal/done', (req, res) => {
    res.send(`<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"></head>
<body style="font-family:sans-serif;max-width:420px;margin:60px auto;padding:0 20px;text-align:center">
<div style="font-size:48px">✓</div><h1>You're online</h1>
<p style="color:#78716c">The marina network accepted the sign-in (mock).</p>
<p><a href="/wifi-config">Back to Wi-Fi settings</a></p></body></html>`);
});

// ============================================================================
// DEBUG INIT ENDPOINT (combined load for debug/calibration page)
// ============================================================================

app.get('/debug/init', (req, res) => {
    const calibration = {
        zeroPoint_mv: mockState.zeroPoint_mv,
        hasTwoPointCalibration: mockState.hasTwoPointCalibration,
    };
    if (mockState.hasTwoPointCalibration) {
        calibration.secondPoint_mv = mockState.secondPoint_mv;
        calibration.secondPoint_cm = mockState.secondPoint_cm;
    }
    res.json({
        reading: {
            sensorAvailable: true,
            valid: mockState.sensorValid,
            millivolts: mockState.currentMillivolts,
            level_cm: mockState.currentLevel_cm,
        },
        calibration,
    });
});

// ============================================================================
// SENSOR READING ENDPOINTS
// ============================================================================

app.get('/read', (req, res) => {
    res.json({
        sensorAvailable: true,
        valid: mockState.sensorValid,
        millivolts: mockState.currentMillivolts,
        level_cm: mockState.currentLevel_cm,
        rate_cm_30min: mockState.rate_cm_30min,
    });
});

// ============================================================================
// CALIBRATION ENDPOINTS
// ============================================================================

app.get('/calibration', (req, res) => {
    const response = {
        zeroPoint_mv: mockState.zeroPoint_mv,
        hasTwoPointCalibration: mockState.hasTwoPointCalibration,
    };
    if (mockState.hasTwoPointCalibration) {
        response.secondPoint_mv = mockState.secondPoint_mv;
        response.secondPoint_cm = mockState.secondPoint_cm;
    }
    res.json(response);
});

app.post('/calibrate/zero', (req, res) => {
    const millivolts = parseInt(req.body.millivolts);
    const level_cm = parseFloat(req.body.level_cm) || 0.0;
    mockState.zeroPoint_mv = millivolts;
    console.log(`[CALIBRATION] Zero point set: ${millivolts} mV = ${level_cm} cm`);
    res.json({ success: true, millivolts, level_cm });
});

app.post('/calibrate/point2', (req, res) => {
    const millivolts = parseInt(req.body.millivolts);
    const level_cm = parseFloat(req.body.level_cm);
    if (!level_cm) { res.status(400).json({ error: 'Missing level_cm parameter' }); return; }
    mockState.secondPoint_mv = millivolts;
    mockState.secondPoint_cm = level_cm;
    mockState.hasTwoPointCalibration = true;
    console.log(`[CALIBRATION] Second point set: ${millivolts} mV = ${level_cm} cm`);
    res.json({ success: true, millivolts, level_cm });
});

// ============================================================================
// EMERGENCY SETTINGS ENDPOINTS
// ============================================================================

app.get('/emergency-settings', (req, res) => {
    res.json({
        emergencyWaterLevel_cm: mockState.emergencyWaterLevel_cm,
        emergencyNotifFreq_ms: mockState.emergencyNotifFreq_ms,
        urgentEmergencyWaterLevel_cm: mockState.urgentEmergencyWaterLevel_cm,
    });
});

app.post('/calibration/emergency-level', (req, res) => {
    const level_cm = parseFloat(req.body.level_cm);
    if (level_cm < 5 || level_cm > 100) { res.status(400).json({ error: 'Invalid level. Must be between 5 and 100 cm' }); return; }
    if (level_cm >= mockState.urgentEmergencyWaterLevel_cm) {
        res.status(400).json({ error: `Tier 1 threshold must be less than Tier 2 (${mockState.urgentEmergencyWaterLevel_cm} cm)` });
        return;
    }
    mockState.emergencyWaterLevel_cm = level_cm;
    console.log(`[EMERGENCY] Tier 1 level updated: ${level_cm} cm`);
    res.json({ success: true, level_cm });
});

app.post('/emergency/urgent-level', (req, res) => {
    const level_cm = parseFloat(req.body.level_cm);
    if (level_cm < 5 || level_cm > 100) { res.status(400).json({ error: 'Invalid level. Must be between 5 and 100 cm' }); return; }
    if (level_cm <= mockState.emergencyWaterLevel_cm) {
        res.status(400).json({ error: `Tier 2 threshold must be greater than Tier 1 (${mockState.emergencyWaterLevel_cm} cm)` });
        return;
    }
    mockState.urgentEmergencyWaterLevel_cm = level_cm;
    console.log(`[EMERGENCY] Tier 2 level updated: ${level_cm} cm`);
    res.json({ success: true, level_cm });
});

app.post('/emergency/test-pin', (req, res) => {
    console.log('[TEST] Emergency pin test requested');
    res.json({ success: true, message: 'Emergency pin test completed (2 second pulse)' });
});

// ============================================================================
// NOTIFICATION SETTINGS ENDPOINTS
// ============================================================================

app.get('/notifications', (req, res) => {
    res.json({
        hasPhoneNumber: mockState.hasPhoneNumber,
        phoneNumber: mockState.hasPhoneNumber ? mockState.phoneNumber : '',
        hasDiscordWebhook: mockState.hasDiscordWebhook,
        discordWebhook: mockState.hasDiscordWebhook ? mockState.discordWebhook : '',
        mqttConfigured: mockState.mqttConfigured,
        mqttConnected: mockState.mqttConnected,
        mqttHost: mockState.mqttHost,
        mqttPort: mockState.mqttPort,
        mqttUser: mockState.mqttUser,
        mqttBaseTopic: mockState.mqttBaseTopic,
        mqttTls: mockState.mqttTls,
    });
});

// Lean status-only endpoint (booleans, no secrets) used by the live MQTT pill poll
app.get('/notifications/status', (req, res) => {
    res.json({
        hasPhoneNumber: mockState.hasPhoneNumber,
        hasDiscordWebhook: mockState.hasDiscordWebhook,
        mqttConfigured: mockState.mqttConfigured,
        mqttConnected: mockState.mqttConnected,
    });
});

app.post('/notifications/emergency-freq', (req, res) => {
    const freq_ms = parseInt(req.body.freq_ms);
    if (freq_ms < 5000 || freq_ms > 3600000) {
        res.status(400).json({ error: 'Invalid frequency. Must be between 5000ms and 3600000ms' });
        return;
    }
    mockState.emergencyNotifFreq_ms = freq_ms;
    console.log(`[NOTIFICATION] Emergency frequency updated: ${freq_ms} ms`);
    res.json({ success: true, freq_ms });
});

app.post('/notifications/phone', (req, res) => {
    const phone = req.body.phone;
    mockState.phoneNumber = phone;
    mockState.hasPhoneNumber = true;
    console.log(`[NOTIFICATION] Phone number updated: ${phone}`);
    res.json({ success: true, phoneNumber: phone });
});

app.post('/notifications/discord', (req, res) => {
    const webhook = req.body.webhook;
    mockState.discordWebhook = webhook;
    mockState.hasDiscordWebhook = true;
    console.log(`[NOTIFICATION] Discord webhook updated`);
    res.json({ success: true });
});

app.post('/notifications/mqtt', (req, res) => {
    const { host, port, user, pass, topic, tls } = req.body;
    if (!host) { res.status(400).json({ error: 'Missing broker host' }); return; }
    mockState.mqttHost = host;
    mockState.mqttPort = parseInt(port) || 1883;
    mockState.mqttUser = user || '';
    mockState.mqttBaseTopic = topic || 'boat/aabbcc';
    mockState.mqttTls = (tls === '1' || tls === 'true' || tls === 'on');
    mockState.mqttConfigured = true;
    mockState.mqttConnected = true; // simulate immediate connect
    console.log(`[MQTT] Broker configured: ${host}:${mockState.mqttPort}`);
    res.json({ success: true });
});

app.post('/notifications/test/sms', (req, res) => {
    if (!mockState.hasPhoneNumber) { res.status(400).json({ error: 'No phone number configured' }); return; }
    console.log(`[TEST] Sending test SMS to ${mockState.phoneNumber}`);
    res.json({ success: true, message: 'Test SMS sent successfully!' });
});

app.post('/notifications/test/discord', (req, res) => {
    if (!mockState.hasDiscordWebhook) { res.status(400).json({ error: 'No Discord webhook configured' }); return; }
    console.log(`[TEST] Sending test Discord message`);
    res.json({ success: true, message: 'Test Discord message sent successfully!' });
});

app.post('/notifications/test/mqtt', (req, res) => {
    if (!mockState.mqttConfigured) { res.status(400).json({ error: 'MQTT not configured' }); return; }
    if (!mockState.mqttConnected) { res.status(400).json({ error: 'MQTT broker not connected' }); return; }
    console.log(`[TEST] Sending test MQTT message to ${mockState.mqttHost}`);
    res.json({ success: true, message: 'Test MQTT message sent successfully!' });
});

// ============================================================================
// OTA / FIRMWARE UPDATE ENDPOINTS
// ============================================================================

// OTA mock state
let otaState = {
    currentVersion: '2.1.0',
    githubRepo: 'owner/bilgerise-firmware',
    autoCheckEnabled: true,
    autoInstallEnabled: false,
    checkIntervalHours: 24,
    notificationsEnabled: true,
    hasGithubToken: false,
    hasUpdatePassword: false,
    updateAvailable: true,
    availableVersion: '2.2.0',
    timeSinceLastCheckHours: 3.5,
    state: 'idle',
    lastError: null,
};

app.get('/ota/status', (req, res) => {
    res.json(otaState);
});

app.post('/ota/check', (req, res) => {
    otaState.state = 'checking';
    console.log('[OTA] Checking for updates...');
    // Simulate async check completing after a short delay
    setTimeout(() => {
        otaState.state = 'idle';
        otaState.timeSinceLastCheckHours = 0;
        console.log('[OTA] Check complete. Update available:', otaState.updateAvailable);
    }, 1500);
    res.json({ success: true });
});

app.post('/ota/update', (req, res) => {
    const password = req.body.password;
    if (otaState.hasUpdatePassword && !password) {
        res.status(401).json({ success: false, error: 'Password required' });
        return;
    }
    otaState.state = 'installing';
    console.log('[OTA] Installing update...');
    // Simulate install + reboot
    setTimeout(() => {
        otaState.currentVersion = otaState.availableVersion;
        otaState.updateAvailable = false;
        otaState.availableVersion = null;
        otaState.state = 'idle';
        console.log('[OTA] Update installed. New version:', otaState.currentVersion);
    }, 3000);
    res.json({ success: true });
});

app.post('/ota/settings', (req, res) => {
    const { github_owner, github_repo, github_token, update_password,
            auto_check, auto_install, check_interval_hours, notifications_enabled } = req.body;
    if (github_owner && github_repo) {
        otaState.githubRepo = github_owner + '/' + github_repo;
    }
    if (github_token) {
        otaState.hasGithubToken = true;
        console.log('[OTA] GitHub token saved');
    }
    if (update_password) {
        otaState.hasUpdatePassword = true;
        console.log('[OTA] Update password saved');
    }
    if (auto_check !== undefined) otaState.autoCheckEnabled = (auto_check === 'true');
    if (auto_install !== undefined) otaState.autoInstallEnabled = (auto_install === 'true');
    if (check_interval_hours) otaState.checkIntervalHours = parseInt(check_interval_hours) || 24;
    if (notifications_enabled !== undefined) otaState.notificationsEnabled = (notifications_enabled === 'true');
    console.log('[OTA] Settings saved');
    res.json({ success: true });
});

// ============================================================================
// START
// ============================================================================

app.listen(PORT, () => {
    console.log('='.repeat(60));
    console.log('ESP32 BilgeRise - Development Mock Server');
    console.log('='.repeat(60));
    console.log(`Server running at: http://localhost:${PORT}`);
    console.log('');
    console.log('Pages (matching firmware routes):');
    console.log(`  Dashboard:         http://localhost:${PORT}/`);
    console.log(`  Settings:          http://localhost:${PORT}/settings`);
    console.log(`  Notifications:     http://localhost:${PORT}/notifications-page`);
    console.log(`  Wi-Fi Config:      http://localhost:${PORT}/wifi-config`);
    console.log(`  Calibration:       http://localhost:${PORT}/debug`);
    console.log(`  OTA / Firmware:    http://localhost:${PORT}/ota-settings`);
    console.log('');
    console.log('Sensor readings update every 2 seconds with random variation');
    console.log('='.repeat(60));
});
