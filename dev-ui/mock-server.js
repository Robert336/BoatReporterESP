const express = require('express');
const cors = require('cors');
const app = express();
const PORT = 3000;

// Middleware
app.use(cors());
app.use(express.json());
app.use(express.urlencoded({ extended: true }));
app.use(express.static(__dirname)); // Serve static files from dev-ui folder

// Mock state data (simulates NVS storage on ESP32)
let mockState = {
    // WiFi settings
    ssid: 'MyWiFi',
    password: 'password123',
    connected: true,
    ip: '192.168.1.100',
    rssi: -45,
    
    // Sensor calibration
    zeroPoint_mv: 500,
    hasTwoPointCalibration: true,
    secondPoint_mv: 2500,
    secondPoint_cm: 50.0,
    
    // Current sensor reading (simulated)
    currentMillivolts: 1234.56,
    currentLevel_cm: 25.3,
    sensorValid: true,
    
    // Emergency settings
    emergencyWaterLevel_cm: 30.0,
    emergencyNotifFreq_ms: 900000, // 15 minutes
    urgentEmergencyWaterLevel_cm: 50.0,
    hornOnDuration_ms: 1000,
    hornOffDuration_ms: 1000,
    
    // Notification settings
    hasPhoneNumber: true,
    phoneNumber: '+15551234567',
    hasDiscordWebhook: true,
    discordWebhook: 'https://discord.com/api/webhooks/123456789/abcdefghijklmnop'
};

// Simulate sensor readings that change over time
setInterval(() => {
    // Add some random variation to simulate real sensor
    mockState.currentMillivolts = 1200 + Math.random() * 200;
    
    // Calculate water level based on calibration
    if (mockState.hasTwoPointCalibration) {
        const slope = (mockState.secondPoint_cm - 0) / (mockState.secondPoint_mv - mockState.zeroPoint_mv);
        mockState.currentLevel_cm = slope * (mockState.currentMillivolts - mockState.zeroPoint_mv);
    } else {
        // Simple calculation if only zero point
        const defaultSlope = 0.02; // 20 cm per 1000 mV
        mockState.currentLevel_cm = (mockState.currentMillivolts - mockState.zeroPoint_mv) * defaultSlope;
    }
    
    // Keep level in reasonable range
    mockState.currentLevel_cm = Math.max(0, Math.min(100, mockState.currentLevel_cm));
}, 2000);

// ============================================================================
// WIFI CONFIGURATION ENDPOINTS
// ============================================================================

app.get('/', (req, res) => {
    res.sendFile(__dirname + '/index.html');
});

app.post('/config', (req, res) => {
    const { ssid, password } = req.body;
    console.log(`[CONFIG] WiFi credentials received: SSID=${ssid}, Password=${password}`);
    mockState.ssid = ssid;
    mockState.password = password;
    mockState.connected = true;
    res.send(`<html><body>
        <h2>Configuration Saved!</h2>
        <p>SSID: ${ssid}</p>
        <p>Attempting to connect...</p>
        <p><a href='/'>Back</a></p>
    </body></html>`);
});

app.get('/status', (req, res) => {
    res.json({
        connected: mockState.connected,
        ssid: mockState.ssid,
        ip: mockState.ip,
        rssi: mockState.rssi
    });
});

// ============================================================================
// SENSOR READING ENDPOINTS
// ============================================================================

app.get('/read', (req, res) => {
    console.log(`[READ] Current sensor reading: ${mockState.currentMillivolts.toFixed(2)} mV, ${mockState.currentLevel_cm.toFixed(2)} cm`);
    res.json({
        sensorAvailable: true,
        valid: mockState.sensorValid,
        millivolts: mockState.currentMillivolts,
        level_cm: mockState.currentLevel_cm
    });
});

// ============================================================================
// CALIBRATION ENDPOINTS
// ============================================================================

app.get('/calibration', (req, res) => {
    const response = {
        zeroPoint_mv: mockState.zeroPoint_mv,
        hasTwoPointCalibration: mockState.hasTwoPointCalibration
    };
    
    if (mockState.hasTwoPointCalibration) {
        response.secondPoint_mv = mockState.secondPoint_mv;
        response.secondPoint_cm = mockState.secondPoint_cm;
    }
    
    console.log('[CALIBRATION] Returning current calibration settings');
    res.json(response);
});

app.post('/calibrate/zero', (req, res) => {
    const millivolts = parseInt(req.body.millivolts);
    const level_cm = parseFloat(req.body.level_cm) || 0.0;
    
    mockState.zeroPoint_mv = millivolts;
    console.log(`[CALIBRATION] Zero point set: ${millivolts} mV = ${level_cm} cm`);
    
    res.json({
        success: true,
        message: 'Zero point calibrated',
        millivolts: millivolts,
        level_cm: level_cm
    });
});

app.post('/calibrate/point2', (req, res) => {
    const millivolts = parseInt(req.body.millivolts);
    const level_cm = parseFloat(req.body.level_cm);
    
    if (!level_cm) {
        res.status(400).json({ error: 'Missing level_cm parameter' });
        return;
    }
    
    mockState.secondPoint_mv = millivolts;
    mockState.secondPoint_cm = level_cm;
    mockState.hasTwoPointCalibration = true;
    
    console.log(`[CALIBRATION] Second point set: ${millivolts} mV = ${level_cm} cm`);
    
    res.json({
        success: true,
        message: 'Second calibration point set',
        millivolts: millivolts,
        level_cm: level_cm
    });
});

// ============================================================================
// EMERGENCY SETTINGS ENDPOINTS
// ============================================================================

app.get('/emergency-settings', (req, res) => {
    res.json({
        emergencyWaterLevel_cm: mockState.emergencyWaterLevel_cm,
        emergencyNotifFreq_ms: mockState.emergencyNotifFreq_ms,
        urgentEmergencyWaterLevel_cm: mockState.urgentEmergencyWaterLevel_cm,
        hornOnDuration_ms: mockState.hornOnDuration_ms,
        hornOffDuration_ms: mockState.hornOffDuration_ms
    });
});

app.post('/calibration/emergency-level', (req, res) => {
    const level_cm = parseFloat(req.body.level_cm);
    
    if (level_cm < 5 || level_cm > 100) {
        res.status(400).json({ error: 'Invalid level. Must be between 5 and 100 cm' });
        return;
    }
    
    if (level_cm >= mockState.urgentEmergencyWaterLevel_cm) {
        res.status(400).json({ 
            error: `Tier 1 threshold must be less than Tier 2 threshold (${mockState.urgentEmergencyWaterLevel_cm} cm)` 
        });
        return;
    }
    
    mockState.emergencyWaterLevel_cm = level_cm;
    console.log(`[EMERGENCY] Tier 1 emergency level updated: ${level_cm} cm`);
    
    res.json({
        success: true,
        message: 'Emergency water level (Tier 1) updated',
        level_cm: level_cm
    });
});

app.post('/emergency/urgent-level', (req, res) => {
    const level_cm = parseFloat(req.body.level_cm);
    
    if (level_cm < 5 || level_cm > 100) {
        res.status(400).json({ error: 'Invalid level. Must be between 5 and 100 cm' });
        return;
    }
    
    if (level_cm <= mockState.emergencyWaterLevel_cm) {
        res.status(400).json({ 
            error: `Tier 2 threshold must be greater than Tier 1 threshold (${mockState.emergencyWaterLevel_cm} cm)` 
        });
        return;
    }
    
    mockState.urgentEmergencyWaterLevel_cm = level_cm;
    console.log(`[EMERGENCY] Tier 2 urgent emergency level updated: ${level_cm} cm`);
    
    res.json({
        success: true,
        message: 'Urgent emergency water level (Tier 2) updated',
        level_cm: level_cm
    });
});

app.post('/emergency/test-pin', (req, res) => {
    console.log('[TEST] Emergency pin test requested - simulating 2 second pulse');
    res.json({
        success: true,
        message: 'Emergency pin test completed (2 second pulse)'
    });
});

// ============================================================================
// NOTIFICATION SETTINGS ENDPOINTS
// ============================================================================

app.get('/notifications', (req, res) => {
    const response = {
        hasPhoneNumber: mockState.hasPhoneNumber
    };
    
    if (mockState.hasPhoneNumber) {
        response.phoneNumber = mockState.phoneNumber;
    }
    
    response.hasDiscordWebhook = mockState.hasDiscordWebhook;
    if (mockState.hasDiscordWebhook) {
        response.discordWebhook = mockState.discordWebhook;
    }
    
    res.json(response);
});

app.post('/notifications/emergency-freq', (req, res) => {
    const freq_ms = parseInt(req.body.freq_ms);
    
    if (freq_ms < 5000 || freq_ms > 3600000) {
        res.status(400).json({ 
            error: `Invalid frequency. Must be between 5000ms (5s) and 3600000ms (3600s)` 
        });
        return;
    }
    
    mockState.emergencyNotifFreq_ms = freq_ms;
    console.log(`[NOTIFICATION] Emergency notification frequency updated: ${freq_ms} ms (${freq_ms / 1000} seconds)`);
    
    res.json({
        success: true,
        message: 'Emergency notification frequency updated',
        freq_ms: freq_ms,
        freq_seconds: freq_ms / 1000
    });
});

app.post('/notifications/phone', (req, res) => {
    const phone = req.body.phone;
    mockState.phoneNumber = phone;
    mockState.hasPhoneNumber = true;
    
    console.log(`[NOTIFICATION] Phone number updated: ${phone}`);
    
    res.json({
        success: true,
        message: 'Phone number updated',
        phoneNumber: phone
    });
});

app.post('/notifications/discord', (req, res) => {
    const webhook = req.body.webhook;
    mockState.discordWebhook = webhook;
    mockState.hasDiscordWebhook = true;
    
    console.log(`[NOTIFICATION] Discord webhook updated: ${webhook}`);
    
    res.json({
        success: true,
        message: 'Discord webhook updated'
    });
});

app.post('/notifications/test/sms', (req, res) => {
    if (!mockState.hasPhoneNumber) {
        res.status(400).json({ error: 'No phone number configured. Please save a phone number first.' });
        return;
    }
    
    console.log(`[TEST] Sending test SMS to ${mockState.phoneNumber}`);
    
    // Simulate SMS sending (always succeeds in mock)
    res.json({
        success: true,
        message: 'Test SMS sent successfully!'
    });
});

app.post('/notifications/test/discord', (req, res) => {
    if (!mockState.hasDiscordWebhook) {
        res.status(400).json({ error: 'No Discord webhook configured. Please save a webhook URL first.' });
        return;
    }
    
    console.log(`[TEST] Sending test Discord message to ${mockState.discordWebhook}`);
    
    // Simulate Discord webhook (always succeeds in mock)
    res.json({
        success: true,
        message: 'Test Discord message sent successfully!'
    });
});

// ============================================================================
// DEBUG ENDPOINT
// ============================================================================

app.get('/debug', (req, res) => {
    res.sendFile(__dirname + '/debug.html');
});

app.get('/debug.html', (req, res) => {
    res.sendFile(__dirname + '/debug.html');
});

app.get('/wifi-config.html', (req, res) => {
    res.sendFile(__dirname + '/wifi-config.html');
});

app.get('/notifications.html', (req, res) => {
    res.sendFile(__dirname + '/notifications.html');
});

// Start server
app.listen(PORT, () => {
    console.log('='.repeat(60));
    console.log('ESP32 Boat Monitor - Development Mock Server');
    console.log('='.repeat(60));
    console.log(`Server running at: http://localhost:${PORT}`);
    console.log('');
    console.log('Available pages:');
    console.log(`  Dashboard:         http://localhost:${PORT}/`);
    console.log(`  Debug/Calibration: http://localhost:${PORT}/debug.html`);
    console.log(`  WiFi Config:       http://localhost:${PORT}/wifi-config.html`);
    console.log(`  Notifications:     http://localhost:${PORT}/notifications.html`);
    console.log('');
    console.log('Mock API Endpoints:');
    console.log('  GET  /read                     - Current sensor reading');
    console.log('  GET  /calibration              - Calibration settings');
    console.log('  GET  /emergency-settings       - Emergency thresholds');
    console.log('  GET  /notifications            - Notification settings');
    console.log('  POST /calibrate/zero           - Set zero calibration');
    console.log('  POST /calibrate/point2         - Set second calibration point');
    console.log('  POST /notifications/phone      - Update phone number');
    console.log('  POST /notifications/discord    - Update Discord webhook');
    console.log('');
    console.log('Sensor readings update every 2 seconds with random variation');
    console.log('='.repeat(60));
});
