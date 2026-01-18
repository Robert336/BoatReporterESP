#include "ConfigServer.h"
#include "Logger.h"

// ============================================================================
// CORE LIFECYCLE METHODS
// ============================================================================

ConfigServer::ConfigServer(WaterPressureSensor* sensor, SendSMS* sms, SendDiscord* discord, OTAManager* ota) 
    : server(nullptr), waterSensor(sensor), smsService(sms), discordService(discord), otaManager(ota) {
    // Generate unique AP password from chip ID
    uint64_t chipId = ESP.getEfuseMac();
    uint32_t id = (uint32_t)(chipId & 0xFFFFFFFF);
    char passwordBuf[15];
    snprintf(passwordBuf, sizeof(passwordBuf), "Boat%08X", id);
    apPassword = String(passwordBuf);
    
    // Initialize calibration preferences
    loadCalibration();
    // Initialize emergency settings
    loadEmergencySettings();
}

ConfigServer::~ConfigServer() {
    // Clean up DNS server if active
    if (dnsServer) {
        dnsServer->stop();
        delete dnsServer;
        dnsServer = nullptr;
    }
    stopSetupMode();
    calibrationPrefs.end(); // Close Preferences namespace
    emergencyPrefs.end(); // Close emergency settings namespace
}

void ConfigServer::startSetupMode() {
    LOG_INFO("\n=== Starting WiFi Setup Mode ===");

    if (setupModeActive) {
        LOG_INFO("...Already in setup mode");
        return;
    }
    
    // Step 1: Set WiFi to AP+STA mode
    WiFi.mode(WIFI_AP_STA);
    
    // Step 2: Start AP (Access Point)
    WiFi.softAP(AP_SSID, apPassword.c_str());
    
    // Get AP IP address (usually 192.168.4.1)
    IPAddress apIP = WiFi.softAPIP();
    LOG_INFO("AP IP address: %s", apIP.toString().c_str());
    LOG_INFO("Connect to SSID: %s", AP_SSID);
    LOG_INFO("Password: %s", apPassword.c_str());
    
    // Step 3: Start DNS server for captive portal
    dnsServer = new DNSServer();
    dnsServer->start(DNS_PORT, "*", apIP);
    LOG_INFO("Captive portal started - users will be automatically redirected");
    LOG_INFO("All DNS requests will redirect to config server");
    
    // Step 4: Create and start web server on port 80
    server = new WebServer(80);
    
    // Step 5: Register HTTP handlers
    // Route: GET / → serve HTML form
    server->on("/", HTTP_GET, [this]() { handleRoot(); });
    
    // Route: POST /config → save credentials
    server->on("/config", HTTP_POST, [this]() { handleSubmit(); });
    
    // Route: GET /status → return status JSON
    server->on("/status", HTTP_GET, [this]() { handleStatus(); });
    
    // Route: GET /debug → serve debug page with detailed sensor info
    server->on("/debug", HTTP_GET, [this]() { handleDebug(); });
    
    // Route: GET /wifi-config → serve WiFi configuration page
    server->on("/wifi-config", HTTP_GET, [this]() { handleWiFiConfig(); });
    
    // Route: GET /notifications-page → serve notifications configuration page
    server->on("/notifications-page", HTTP_GET, [this]() { handleNotificationsPage(); });
    
    // Route: GET /read → return current sensor reading as JSON
    server->on("/read", HTTP_GET, [this]() { handleGetReading(); });
    
    // Route: GET /calibration → return current calibration settings
    server->on("/calibration", HTTP_GET, [this]() { handleGetCalibration(); });
    
    // Route: POST /calibrate/zero → set zero calibration point
    server->on("/calibrate/zero", HTTP_POST, [this]() { handleCalibrateZero(); });
    
    // Route: POST /calibrate/point2 → set second calibration point
    server->on("/calibrate/point2", HTTP_POST, [this]() { handleCalibratePoint2(); });

    // Route: POST /calibration/emergency-level -> set emergency water level (Tier 1)
    server->on("/calibration/emergency-level", HTTP_POST, [this]() { handleSetEmergencyLevel(); });
    
    // Route: POST /emergency/urgent-level -> set urgent emergency water level (Tier 2)
    server->on("/emergency/urgent-level", HTTP_POST, [this]() { handleSetUrgentEmergencyLevel(); });
    
    // Route: POST /emergency/test-pin -> test the emergency pin output
    server->on("/emergency/test-pin", HTTP_POST, [this]() { handleTestEmergencyPin(); });
    
    // Route: GET /emergency-settings → return current emergency settings
    server->on("/emergency-settings", HTTP_GET, [this]() { 
        String json = "{";
        json += "\"emergencyWaterLevel_cm\":" + String(emergencyWaterLevel_cm, 2) + ",";
        json += "\"emergencyNotifFreq_ms\":" + String(emergencyNotifFreq_ms) + ",";
        json += "\"urgentEmergencyWaterLevel_cm\":" + String(urgentEmergencyWaterLevel_cm, 2) + ",";
        json += "\"hornOnDuration_ms\":" + String(hornOnDuration_ms) + ",";
        json += "\"hornOffDuration_ms\":" + String(hornOffDuration_ms);
        json += "}";
        server->send(200, "application/json", json);
        serverStartTime = millis();
    });
    
    // Route: GET /notifications → return current notification settings
    server->on("/notifications", HTTP_GET, [this]() { handleGetNotifications(); });

    // Route: POST /notifications/emergency-notif-freq -> set emergency notification frequency
    server->on("/notifications/emergency-freq", HTTP_POST, [this]() { handleSetEmergencyNotifFreq(); });
    
    // Route: POST /notifications/phone → set SMS phone number
    server->on("/notifications/phone", HTTP_POST, [this]() { handleSetPhoneNumber(); });
    
    // Route: POST /notifications/discord → set Discord webhook URL
    server->on("/notifications/discord", HTTP_POST, [this]() { handleSetDiscordWebhook(); });
    
    // Route: POST /notifications/test/sms → send test SMS
    server->on("/notifications/test/sms", HTTP_POST, [this]() { handleTestSMS(); });
    
    // Route: POST /notifications/test/discord → send test Discord message
    server->on("/notifications/test/discord", HTTP_POST, [this]() { handleTestDiscord(); });
    
    // Route: GET /ota-settings → serve OTA settings page
    server->on("/ota-settings", HTTP_GET, [this]() { handleOTAPage(); });
    
    // Route: GET /ota/status → return OTA status JSON
    server->on("/ota/status", HTTP_GET, [this]() { handleOTAStatus(); });
    
    // Route: GET /ota/check → manually trigger update check
    server->on("/ota/check", HTTP_GET, [this]() { handleOTACheck(); });
    
    // Route: POST /ota/update → start firmware update
    server->on("/ota/update", HTTP_POST, [this]() { handleOTAUpdate(); });
    
    // Route: POST /ota/settings → configure OTA settings
    server->on("/ota/settings", HTTP_POST, [this]() { handleOTASettings(); });
    
    // Handle 404 and captive portal detection
    // Redirect all unknown paths to root for better captive portal detection
    server->onNotFound([this]() {
        // Serve the root page for captive portal detection
        // Many devices try to access specific URLs to detect captive portals
        handleRoot();
    });
    
    // Start the server
    server->begin();
    setupModeActive = true;
    serverStartTime = millis();
    
    LOG_INFO("Setup mode started. Open browser and navigate to 192.168.4.1");
    LOG_INFO("Or simply open any website - captive portal will redirect you!");
}

void ConfigServer::stopSetupMode() {
    if (server) {
        server->stop();
        delete server;
        server = nullptr;
    }
    
    // Stop captive portal DNS server
    if (dnsServer) {
        dnsServer->stop();
        delete dnsServer;
        dnsServer = nullptr;
        LOG_INFO("Captive portal stopped");
    }
    
    // Stop AP, keep STA mode
    WiFi.mode(WIFI_STA);
    setupModeActive = false;

    LOG_INFO("\n=== Setup mode stopped, resuming normal WiFi ===");
}

bool ConfigServer::isSetupModeActive() {
    return setupModeActive;
}

void ConfigServer::handleClient() {
    // Guard clause: Only continue if server exists and setup mode is active
    if (!server || !setupModeActive) return;

    // Process captive portal DNS requests
    if (dnsServer) {
        dnsServer->processNextRequest();
    }
    
    // Handle web server requests
    server->handleClient();

    // Handle server timeout
    if ((millis() - serverStartTime) >= SERVER_TIMEOUT_MS) {
        stopSetupMode();
    }   
}

// ============================================================================
// WIFI CONFIGURATION HANDLERS
// ============================================================================

void ConfigServer::handleRoot() {
    String html = getConfigPage();
    server->send(200, "text/html", html);
    serverStartTime = millis(); // Reset the timeout on page load
}

void ConfigServer::handleWiFiConfig() {
    String html = getWiFiConfigPage();
    server->send(200, "text/html", html);
    serverStartTime = millis();
}

void ConfigServer::handleNotificationsPage() {
    String html = getNotificationsPageHTML();
    server->send(200, "text/html", html);
    serverStartTime = millis();
}

void ConfigServer::handleSubmit() {
    // Check if SSID and password were submitted
    if (server->hasArg("ssid") && server->hasArg("password")) {
        String ssid = server->arg("ssid");
        String password = server->arg("password");
        
        LOG_INFO("\nConfiguration received!");
        LOG_INFO("SSID: %s", ssid.c_str());
        LOG_INFO("Password: %s", password.c_str());
        
        // Save to NVS via WiFiManager
        WiFiManager& wifiMgr = WiFiManager::getInstance();
        wifiMgr.addNetwork(ssid.c_str(), password.c_str());
        
        // Send success response
        String response = "<html><body>";
        response += "<h2>Configuration Saved!</h2>";
        response += "<p>SSID: " + ssid + "</p>";
        response += "<p>Attempting to connect...</p>";
        response += "<p><a href='/'>Back</a></p>";
        response += "</body></html>";
        
        server->send(200, "text/html", response);
    } else {
        server->send(400, "text/plain", "Missing SSID or password");
    }
}

void ConfigServer::handleStatus() {
    // Return connection status as JSON
    String json = "{";
    json += "\"connected\":" + String(WiFiManager::getInstance().isConnected() ? "true" : "false") + ",";
    json += "\"ssid\":\"" + WiFi.SSID() + "\",";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI());
    json += "}";
    
    server->send(200, "application/json", json);
}

String ConfigServer::getConfigPage() {
    String html = R"HTML(<!DOCTYPE html>
<html><head><meta name="viewport" content="width=device-width, initial-scale=1"><title>Boat Monitor</title>
<style>
body{font-family:Arial,sans-serif;margin:0;padding:10px;max-width:600px;margin:0 auto;}
h1{text-align:center;font-size:1.5em;margin:10px 0;}
.card{border:1px solid #ccc;padding:15px;margin:10px 0;}
.card h2{font-size:1.1em;margin:0 0 10px 0;border-bottom:1px solid #ddd;padding-bottom:5px;}
.row{display:flex;justify-content:space-between;padding:5px 0;border-bottom:1px solid #eee;}
.row:last-child{border-bottom:none;}
.label{font-weight:bold;}
.level{font-size:2em;text-align:center;margin:10px 0;}
.thresh{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:10px;}
.thresh div{border:1px solid #ccc;padding:10px;text-align:center;}
.thresh .label{font-size:0.8em;display:block;margin-bottom:5px;}
button{width:100%;padding:12px;margin:5px 0;border:1px solid #333;background:#fff;font-size:1em;cursor:pointer;}
button:active{background:#eee;}
</style>
<script>
function load(){
fetch('/status').then(r=>r.json()).then(d=>{
document.getElementById('wifi_status').textContent=(d.connected ? d.ssid : 'Disconnected');
document.getElementById('wifi_ip').textContent=d.ip||'N/A';
document.getElementById('wifi_rssi').textContent=(d.rssi||'N/A')+' dBm';
}).catch(e=>console.error(e));
fetch('/read').then(r=>r.json()).then(d=>{
document.getElementById('water_level').textContent=(d.sensorAvailable&&d.valid)?d.level_cm.toFixed(1):'--';
}).catch(e=>console.error(e));
fetch('/emergency-settings').then(r=>r.json()).then(d=>{
document.getElementById('tier1').value=d.emergencyWaterLevel_cm.toFixed(1);
document.getElementById('tier2').value=d.urgentEmergencyWaterLevel_cm.toFixed(1);
}).catch(e=>console.error(e));
}
window.onload=load;
function updateThresholds(){
var tier1=parseFloat(document.getElementById('tier1').value);
var tier2=parseFloat(document.getElementById('tier2').value);
var status=document.getElementById('threshold-status');
if(isNaN(tier1)||isNaN(tier2)){status.textContent='⚠️ Please enter valid numbers';status.style.color='red';return;}
if(tier1>=tier2){status.textContent='⚠️ Tier 1 must be less than Tier 2';status.style.color='red';return;}
if(tier1<5||tier1>100||tier2<5||tier2>100){status.textContent='⚠️ Values must be between 5 and 100 cm';status.style.color='red';return;}
status.textContent='⏳ Updating...';status.style.color='#666';
Promise.all([
fetch('/calibration/emergency-level',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'level_cm='+tier1}),
fetch('/emergency/urgent-level',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'level_cm='+tier2})
]).then(responses=>Promise.all(responses.map(r=>r.json()))).then(results=>{
if(results[0].success&&results[1].success){
status.textContent='✓ Thresholds updated successfully';status.style.color='green';
setTimeout(()=>{status.textContent='';},3000);
}else{
status.textContent='⚠️ Update failed: '+(results[0].error||results[1].error||'Unknown error');status.style.color='red';
}
}).catch(e=>{status.textContent='⚠️ Error: '+e.message;status.style.color='red';console.error(e);});
}
</script>
</head><body>
<h1>Bilge Buddy</h1>
<div class="card"><h2>WiFi Connection</h2>
<div class="row"><span class="label">Status</span><span id="wifi_status">Loading...</span></div>
<div class="row"><span class="label">IP Address</span><span id="wifi_ip">Loading...</span></div>
<div class="row"><span class="label">Signal</span><span id="wifi_rssi">Loading...</span></div>
</div>
<div class="card"><h2>Current Water Level</h2>
<div class="level"><span id="water_level">--</span> cm</div>
</div>
<div class="card"><h2>Emergency Thresholds</h2>
<div class="thresh">
<div><span class="label">TIER 1</span><input type="number" id="tier1" step="0.1" min="5" max="100" style="width:100%;padding:5px;margin-top:5px;box-sizing:border-box;"> cm</div>
<div><span class="label">TIER 2</span><input type="number" id="tier2" step="0.1" min="5" max="100" style="width:100%;padding:5px;margin-top:5px;box-sizing:border-box;"> cm</div>
</div>
<button onclick="updateThresholds()" style="margin-top:10px;">Update Thresholds</button>
<div id="threshold-status" style="text-align:center;margin-top:10px;font-size:0.9em;"></div>
</div>
<button onclick="location.href='/notifications-page'">Notification Settings</button>
<button onclick="location.href='/wifi-config'">WiFi Networks</button>
<button onclick="location.href='/ota-settings'">Firmware Updates (OTA)</button>
<button onclick="location.href='/debug'">Advanced Debug & Calibration</button>
</body></html>)HTML";
    return html;
}

// ============================================================================
// SENSOR CALIBRATION HANDLERS
// ============================================================================

void ConfigServer::handleCalibrateZero() {
    serverStartTime = millis();
    
    if (!waterSensor) {
        server->send(503, "application/json", "{\"error\":\"Sensor not available\"}");
        return;
    }
    
    if (server->hasArg("millivolts")) {
        int millivolts = server->arg("millivolts").toInt();
        float level_cm = server->hasArg("level_cm") ? server->arg("level_cm").toFloat() : 0.0f;
        
        waterSensor->setCalibrationPoint(0, millivolts, level_cm);
        saveCalibration();
        
        String json = "{";
        json += "\"success\":true,";
        json += "\"message\":\"Zero point calibrated\",";
        json += "\"millivolts\":" + String(millivolts) + ",";
        json += "\"level_cm\":" + String(level_cm, 2);
        json += "}";
        
        server->send(200, "application/json", json);
    } else {
        server->send(400, "application/json", "{\"error\":\"Missing millivolts parameter\"}");
    }
}

void ConfigServer::handleCalibratePoint2() {
    serverStartTime = millis();
    
    if (!waterSensor) {
        server->send(503, "application/json", "{\"error\":\"Sensor not available\"}");
        return;
    }
    
    if (server->hasArg("millivolts") && server->hasArg("level_cm")) {
        int millivolts = server->arg("millivolts").toInt();
        float level_cm = server->arg("level_cm").toFloat();
        
        waterSensor->setCalibrationPoint(1, millivolts, level_cm);
        saveCalibration();
        
        String json = "{";
        json += "\"success\":true,";
        json += "\"message\":\"Second calibration point set\",";
        json += "\"millivolts\":" + String(millivolts) + ",";
        json += "\"level_cm\":" + String(level_cm, 2);
        json += "}";
        
        server->send(200, "application/json", json);
    } else {
        server->send(400, "application/json", "{\"error\":\"Missing millivolts or level_cm parameter\"}");
    }
}

void ConfigServer::handleGetCalibration() {
    serverStartTime = millis();
    
    if (!waterSensor) {
        server->send(503, "application/json", "{\"error\":\"Sensor not available\"}");
        return;
    }
    
    String json = "{";
    json += "\"zeroPoint_mv\":" + String(waterSensor->getZeroPointMilliVolts()) + ",";
    json += "\"hasTwoPointCalibration\":" + String(waterSensor->hasTwoPointCalibration() ? "true" : "false");
    
    if (waterSensor->hasTwoPointCalibration()) {
        json += ",\"secondPoint_mv\":" + String(waterSensor->getSecondPointMilliVolts()) + ",";
        json += "\"secondPoint_cm\":" + String(waterSensor->getSecondPointLevelCm(), 2);
    }
    json += "}";
    
    server->send(200, "application/json", json);
}

void ConfigServer::loadCalibration() {
    
    if (!waterSensor) return;

    if (!calibrationPrefs.begin(SENSOR_CALIBRATION_NAMESPACE, true)) {
        LOG_CRITICAL("Failed to load the calibration NVS storage in read mode");
    }
    
    int zero_mv = calibrationPrefs.getInt("zero_mv", -1);
    if (zero_mv >= 0) {
        waterSensor->setCalibrationPoint(0, zero_mv, 0.0f);
        LOG_INFO("[CALIBRATION] Loaded zero point from NVS: %d mV", zero_mv);
    } else {
        LOG_INFO("[CALIBRATION] No zero point calibration found in NVS, using default");
    }
    
    int point2_mv = calibrationPrefs.getInt("point2_mv", -1);
    float point2_cm = calibrationPrefs.getFloat("point2_cm", -1.0f);
    if (point2_mv >= 0 && point2_cm >= 0) {
        waterSensor->setCalibrationPoint(1, point2_mv, point2_cm);
        LOG_INFO("[CALIBRATION] Loaded second point from NVS: %d mV = %.2f cm (2-point calibration active)", 
                      point2_mv, point2_cm);
    } else {
        LOG_INFO("[CALIBRATION] No second calibration point found in NVS");
    }

    calibrationPrefs.end();
}

void ConfigServer::saveCalibration() {
    if (!waterSensor) return;

    if (!calibrationPrefs.begin(SENSOR_CALIBRATION_NAMESPACE, false)) {
        LOG_CRITICAL("Failed to load the calibration NVS storage in write mode");
    }
    
    int zero_mv = waterSensor->getZeroPointMilliVolts();
    calibrationPrefs.putInt("zero_mv", zero_mv);
    LOG_INFO("[CALIBRATION] Saved zero point to NVS: %d mV", zero_mv);
    
    if (waterSensor->hasTwoPointCalibration()) {
        int point2_mv = waterSensor->getSecondPointMilliVolts();
        float point2_cm = waterSensor->getSecondPointLevelCm();
        calibrationPrefs.putInt("point2_mv", point2_mv);
        calibrationPrefs.putFloat("point2_cm", point2_cm);
        LOG_INFO("[CALIBRATION] Saved second point to NVS: %d mV = %.2f cm (2-point calibration)", 
                      point2_mv, point2_cm);
    } else {
        calibrationPrefs.remove("point2_mv");
        calibrationPrefs.remove("point2_cm");
        LOG_INFO("[CALIBRATION] Removed second calibration point from NVS (single-point mode)");
    }

    calibrationPrefs.end();
}

// ============================================================================
// EMERGENCY SETTINGS HANDLERS
// ============================================================================

void ConfigServer::handleSetEmergencyLevel() {
    serverStartTime = millis();
    
    if (server->hasArg("level_cm")) {
        float level_cm = server->arg("level_cm").toFloat();
        
        // Validate the input against sensor usable range
        if (level_cm < MIN_EMERGENCY_WATER_LEVEL_CM || level_cm > MAX_EMERGENCY_WATER_LEVEL_CM) {
            String errorMsg = "{\"error\":\"Invalid level. Must be between ";
            errorMsg += String(MIN_EMERGENCY_WATER_LEVEL_CM, 1) + " and ";
            errorMsg += String(MAX_EMERGENCY_WATER_LEVEL_CM, 1) + " cm\"}";
            server->send(400, "application/json", errorMsg);
            return;
        }
        
        // Validate that Tier 1 threshold is less than Tier 2 threshold
        if (level_cm >= urgentEmergencyWaterLevel_cm) {
            String errorMsg = "{\"error\":\"Tier 1 threshold must be less than Tier 2 threshold (";
            errorMsg += String(urgentEmergencyWaterLevel_cm, 2) + " cm)\"}";
            server->send(400, "application/json", errorMsg);
            return;
        }
        
        emergencyWaterLevel_cm = level_cm;
        saveEmergencySettings();
        
        String json = "{";
        json += "\"success\":true,";
        json += "\"message\":\"Emergency water level (Tier 1) updated\",";
        json += "\"level_cm\":" + String(level_cm, 2);
        json += "}";
        
        server->send(200, "application/json", json);
        LOG_INFO("[CONFIG] Emergency water level (Tier 1) updated: %.2f cm", level_cm);
    } else {
        server->send(400, "application/json", "{\"error\":\"Missing level_cm parameter\"}");
    }
}

void ConfigServer::handleSetEmergencyNotifFreq() {
    serverStartTime = millis();
    
    if (server->hasArg("freq_ms")) {
        int freq_ms = server->arg("freq_ms").toInt();
        
        // Validate the input
        if (freq_ms < MIN_EMERGENCY_NOTIF_FREQ_MS || freq_ms > MAX_EMERGENCY_NOTIF_FREQ_MS) {
            String errorMsg = "{\"error\":\"Invalid frequency. Must be between ";
            errorMsg += String(MIN_EMERGENCY_NOTIF_FREQ_MS) + "ms (" + String(MIN_EMERGENCY_NOTIF_FREQ_MS / 1000) + "s) and ";
            errorMsg += String(MAX_EMERGENCY_NOTIF_FREQ_MS) + "ms (" + String(MAX_EMERGENCY_NOTIF_FREQ_MS / 1000) + "s)\"}";
            server->send(400, "application/json", errorMsg);
            return;
        }
        
        emergencyNotifFreq_ms = freq_ms;
        saveEmergencySettings();
        
        String json = "{";
        json += "\"success\":true,";
        json += "\"message\":\"Emergency notification frequency updated\",";
        json += "\"freq_ms\":" + String(freq_ms) + ",";
        json += "\"freq_seconds\":" + String(freq_ms / 1000);
        json += "}";
        
        server->send(200, "application/json", json);
        LOG_INFO("[CONFIG] Emergency notification frequency updated: %d ms (%d seconds)", freq_ms, freq_ms / 1000);
    } else {
        server->send(400, "application/json", "{\"error\":\"Missing freq_ms parameter\"}");
    }
}

void ConfigServer::handleSetUrgentEmergencyLevel() {
    serverStartTime = millis();
    
    if (server->hasArg("level_cm")) {
        float level_cm = server->arg("level_cm").toFloat();
        
        // Validate the input against sensor usable range
        if (level_cm < MIN_EMERGENCY_WATER_LEVEL_CM || level_cm > MAX_EMERGENCY_WATER_LEVEL_CM) {
            String errorMsg = "{\"error\":\"Invalid level. Must be between ";
            errorMsg += String(MIN_EMERGENCY_WATER_LEVEL_CM, 1) + " and ";
            errorMsg += String(MAX_EMERGENCY_WATER_LEVEL_CM, 1) + " cm\"}";
            server->send(400, "application/json", errorMsg);
            return;
        }
        
        // Validate that Tier 2 threshold is greater than Tier 1 threshold
        if (level_cm <= emergencyWaterLevel_cm) {
            String errorMsg = "{\"error\":\"Tier 2 threshold must be greater than Tier 1 threshold (";
            errorMsg += String(emergencyWaterLevel_cm, 2) + " cm)\"}";
            server->send(400, "application/json", errorMsg);
            return;
        }
        
        urgentEmergencyWaterLevel_cm = level_cm;
        saveEmergencySettings();
        
        String json = "{";
        json += "\"success\":true,";
        json += "\"message\":\"Urgent emergency water level (Tier 2) updated\",";
        json += "\"level_cm\":" + String(level_cm, 2);
        json += "}";
        
        server->send(200, "application/json", json);
        LOG_INFO("[CONFIG] Urgent emergency water level (Tier 2) updated: %.2f cm", level_cm);
    } else {
        server->send(400, "application/json", "{\"error\":\"Missing level_cm parameter\"}");
    }
}

void ConfigServer::handleTestEmergencyPin() {
    serverStartTime = millis();
    
    LOG_INFO("[TEST] Testing emergency pin output...");
    
    // Set the pin HIGH for 2 seconds to test the connected device
    const int ALERT_PIN = 19; // GPIO 19 as defined in main.cpp
    digitalWrite(ALERT_PIN, HIGH);
    LOG_INFO("[TEST] Emergency pin set HIGH");
    
    delay(2000); // 2 second test pulse
    
    digitalWrite(ALERT_PIN, LOW);
    LOG_INFO("[TEST] Emergency pin set LOW - test complete");
    
    String json = "{";
    json += "\"success\":true,";
    json += "\"message\":\"Emergency pin test completed (2 second pulse)\"";
    json += "}";
    
    server->send(200, "application/json", json);
}

void ConfigServer::loadEmergencySettings() {
    // Set defaults first
    emergencyWaterLevel_cm = DEFAULT_EMERGENCY_WATER_LEVEL_CM;
    emergencyNotifFreq_ms = DEFAULT_EMERGENCY_NOTIF_FREQ_MS;
    urgentEmergencyWaterLevel_cm = DEFAULT_URGENT_EMERGENCY_WATER_LEVEL_CM;
    hornOnDuration_ms = DEFAULT_HORN_ON_DURATION_MS;
    hornOffDuration_ms = DEFAULT_HORN_OFF_DURATION_MS;
    
    if (!emergencyPrefs.begin(EMERGENCY_SETTINGS_NAMESPACE, true)) {
        LOG_CRITICAL("[EMERGENCY] Failed to load emergency settings NVS storage in read mode");
        return;
    }
    
    float saved_level = emergencyPrefs.getFloat("level_cm", -1.0f);
    if (saved_level >= 0) {
        emergencyWaterLevel_cm = saved_level;
        LOG_INFO("[EMERGENCY] Loaded emergency water level (Tier 1) from NVS: %.2f cm", emergencyWaterLevel_cm);
    } else {
        LOG_INFO("[EMERGENCY] No saved emergency water level found, using default: %.2f cm", emergencyWaterLevel_cm);
    }
    
    int saved_freq = emergencyPrefs.getInt("notif_freq_ms", -1);
    if (saved_freq >= 0) {
        emergencyNotifFreq_ms = saved_freq;
        LOG_INFO("[EMERGENCY] Loaded emergency notification frequency from NVS: %d ms (%d seconds)", 
                      emergencyNotifFreq_ms, emergencyNotifFreq_ms / 1000);
    } else {
        LOG_INFO("[EMERGENCY] No saved notification frequency found, using default: %d ms (%d seconds)",
                      emergencyNotifFreq_ms, emergencyNotifFreq_ms / 1000);
    }
    
    float saved_urgent_level = emergencyPrefs.getFloat("urgent_level_cm", -1.0f);
    if (saved_urgent_level >= 0) {
        urgentEmergencyWaterLevel_cm = saved_urgent_level;
        LOG_INFO("[EMERGENCY] Loaded urgent emergency water level (Tier 2) from NVS: %.2f cm", urgentEmergencyWaterLevel_cm);
    } else {
        LOG_INFO("[EMERGENCY] No saved urgent emergency water level found, using default: %.2f cm", urgentEmergencyWaterLevel_cm);
    }
    
    int saved_horn_on = emergencyPrefs.getInt("horn_on_ms", -1);
    if (saved_horn_on >= 0) {
        hornOnDuration_ms = saved_horn_on;
        LOG_INFO("[EMERGENCY] Loaded horn ON duration from NVS: %d ms", hornOnDuration_ms);
    } else {
        LOG_INFO("[EMERGENCY] No saved horn ON duration found, using default: %d ms", hornOnDuration_ms);
    }
    
    int saved_horn_off = emergencyPrefs.getInt("horn_off_ms", -1);
    if (saved_horn_off >= 0) {
        hornOffDuration_ms = saved_horn_off;
        LOG_INFO("[EMERGENCY] Loaded horn OFF duration from NVS: %d ms", hornOffDuration_ms);
    } else {
        LOG_INFO("[EMERGENCY] No saved horn OFF duration found, using default: %d ms", hornOffDuration_ms);
    }
    
    emergencyPrefs.end();
}

void ConfigServer::saveEmergencySettings() {
    if (!emergencyPrefs.begin(EMERGENCY_SETTINGS_NAMESPACE, false)) {
        LOG_CRITICAL("[EMERGENCY] Failed to load emergency settings NVS storage in write mode");
        return;
    }
    
    emergencyPrefs.putFloat("level_cm", emergencyWaterLevel_cm);
    emergencyPrefs.putInt("notif_freq_ms", emergencyNotifFreq_ms);
    emergencyPrefs.putFloat("urgent_level_cm", urgentEmergencyWaterLevel_cm);
    emergencyPrefs.putInt("horn_on_ms", hornOnDuration_ms);
    emergencyPrefs.putInt("horn_off_ms", hornOffDuration_ms);
    
    LOG_INFO("[EMERGENCY] Saved emergency settings to NVS: Tier1=%.2f cm, Tier2=%.2f cm, freq=%d ms, horn=%d/%d ms", 
                  emergencyWaterLevel_cm, urgentEmergencyWaterLevel_cm, emergencyNotifFreq_ms, hornOnDuration_ms, hornOffDuration_ms);
    
    emergencyPrefs.end();
}

// ============================================================================
// NOTIFICATION SETTINGS HANDLERS
// ============================================================================

void ConfigServer::handleGetNotifications() {
    serverStartTime = millis();
    
    String json = "{";
    
    // SMS phone number
    json += "\"hasPhoneNumber\":";
    if (smsService && smsService->hasPhoneNumber()) {
        char phoneBuf[32];
        if (smsService->getPhoneNumber(phoneBuf, sizeof(phoneBuf)) == 0) {
            json += "true,\"phoneNumber\":\"" + String(phoneBuf) + "\"";
        } else {
            json += "false";
        }
    } else {
        json += "false";
    }
    
    // Discord webhook
    json += ",\"hasDiscordWebhook\":";
    if (discordService && discordService->hasWebhookUrl()) {
        char webhookBuf[256];
        if (discordService->getWebhookUrl(webhookBuf, sizeof(webhookBuf)) == 0) {
            json += "true,\"discordWebhook\":\"" + String(webhookBuf) + "\"";
        } else {
            json += "false";
        }
    } else {
        json += "false";
    }
    
    json += "}";
    server->send(200, "application/json", json);
}

void ConfigServer::handleSetPhoneNumber() {
    serverStartTime = millis();
    
    if (!smsService) {
        server->send(503, "application/json", "{\"error\":\"SMS service not available\"}");
        return;
    }
    
    if (server->hasArg("phone")) {
        String phone = server->arg("phone");
        smsService->updatePhoneNumber(phone.c_str());
        
        String json = "{\"success\":true,\"message\":\"Phone number updated\",\"phoneNumber\":\"" + phone + "\"}";
        server->send(200, "application/json", json);
        LOG_INFO("[CONFIG] Phone number updated: %s", phone.c_str());
    } else {
        server->send(400, "application/json", "{\"error\":\"Missing phone parameter\"}");
    }
}

void ConfigServer::handleSetDiscordWebhook() {
    serverStartTime = millis();
    
    if (!discordService) {
        server->send(503, "application/json", "{\"error\":\"Discord service not available\"}");
        return;
    }
    
    if (server->hasArg("webhook")) {
        String webhook = server->arg("webhook");
        discordService->updateWebhookUrl(webhook.c_str());
        
        String json = "{\"success\":true,\"message\":\"Discord webhook updated\"}";
        server->send(200, "application/json", json);
        LOG_INFO("[CONFIG] Discord webhook updated: %s", webhook.c_str());
    } else {
        server->send(400, "application/json", "{\"error\":\"Missing webhook parameter\"}");
    }
}

void ConfigServer::handleTestSMS() {
    serverStartTime = millis();
    
    if (!smsService) {
        server->send(503, "application/json", "{\"error\":\"SMS service not available\"}");
        return;
    }
    
    if (!smsService->hasPhoneNumber()) {
        server->send(400, "application/json", "{\"error\":\"No phone number configured. Please save a phone number first.\"}");
        return;
    }
    
    if (!WiFi.isConnected()) {
        server->send(503, "application/json", "{\"error\":\"WiFi not connected. Cannot send SMS.\"}");
        return;
    }
    
    LOG_INFO("[TEST] Sending test SMS...");
    bool success = smsService->send("Boat Monitor Test: This is a test message from your ESP32 boat monitor.");
    
    if (success) {
        LOG_INFO("[TEST] Test SMS sent successfully!");
        server->send(200, "application/json", "{\"success\":true,\"message\":\"Test SMS sent successfully!\"}");
    } else {
        LOG_INFO("[TEST] Test SMS failed to send");
        server->send(500, "application/json", "{\"success\":false,\"error\":\"Failed to send test SMS. Check serial log for details.\"}");
    }
}

void ConfigServer::handleTestDiscord() {
    serverStartTime = millis();
    
    if (!discordService) {
        server->send(503, "application/json", "{\"error\":\"Discord service not available\"}");
        return;
    }
    
    if (!discordService->hasWebhookUrl()) {
        server->send(400, "application/json", "{\"error\":\"No Discord webhook configured. Please save a webhook URL first.\"}");
        return;
    }
    
    if (!WiFi.isConnected()) {
        server->send(503, "application/json", "{\"error\":\"WiFi not connected. Cannot send Discord message.\"}");
        return;
    }
    
    LOG_INFO("[TEST] Sending test Discord message...");
    bool success = discordService->send("🚤 **Boat Monitor Test** - This is a test message from your ESP32 boat monitor.");
    
    if (success) {
        LOG_INFO("[TEST] Test Discord message sent successfully!");
        server->send(200, "application/json", "{\"success\":true,\"message\":\"Test Discord message sent successfully!\"}");
    } else {
        LOG_INFO("[TEST] Test Discord message failed to send");
        server->send(500, "application/json", "{\"success\":false,\"error\":\"Failed to send test Discord message. Check serial log for details.\"}");
    }
}

String ConfigServer::getWiFiConfigPage() {
    // Minimal WiFi configuration page
    String html = R"(<!DOCTYPE html>
<html><head><meta name="viewport" content="width=device-width, initial-scale=1"><title>WiFi Config</title>
<style>
body{font-family:Arial,sans-serif;margin:0;padding:10px;max-width:600px;margin:0 auto;}
h1{font-size:1.5em;margin:10px 0;}
.card{border:1px solid #ccc;padding:15px;margin:10px 0;}
.card h2{font-size:1.1em;margin:0 0 10px 0;border-bottom:1px solid #ddd;padding-bottom:5px;}
.row{display:flex;justify-content:space-between;padding:5px 0;border-bottom:1px solid #eee;}
.row:last-child{border-bottom:none;}
label{display:block;margin:10px 0 5px;font-weight:bold;}
input{width:100%;padding:8px;border:1px solid #ccc;font-size:1em;box-sizing:border-box;}
button{width:100%;padding:12px;margin:5px 0;border:1px solid #333;background:#fff;font-size:1em;cursor:pointer;}
button:active{background:#eee;}
.help{font-size:0.85em;color:#666;margin-top:3px;}
</style>
<script>
function load(){
fetch('/status').then(r=>r.json()).then(d=>{
var s='<div class="row"><span style="font-weight:bold">Status</span><span>'+(d.connected?'Connected':'Disconnected')+'</span></div>';
if(d.connected){
s+='<div class="row"><span style="font-weight:bold">Network</span><span>'+(d.ssid||'Unknown')+'</span></div>';
s+='<div class="row"><span style="font-weight:bold">IP</span><span>'+(d.ip||'N/A')+'</span></div>';
s+='<div class="row"><span style="font-weight:bold">Signal</span><span>'+(d.rssi||'N/A')+' dBm</span></div>';
}
document.getElementById('status').innerHTML=s;
}).catch(e=>console.error(e));
}
function save(e){
e.preventDefault();
var s=document.getElementById('ssid').value;
var p=document.getElementById('password').value;
if(!s||!p){alert('Enter SSID and password');return;}
var btn=document.getElementById('btn');
btn.disabled=true;
btn.textContent='Saving...';
fetch('/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'ssid='+encodeURIComponent(s)+'&password='+encodeURIComponent(p)})
.then(r=>{
if(r.ok){
alert('Saved! Connecting...');
document.getElementById('form').reset();
setTimeout(load,3000);
}else{alert('Failed to save');}
}).catch(e=>alert('Error: '+e.message))
.finally(()=>{btn.disabled=false;btn.textContent='Save & Connect';});
}
window.onload=function(){
load();
document.getElementById('form').addEventListener('submit',save);
};
</script>
</head><body>
<a href="/" style="text-decoration:none;color:#000;">< Back</a>
<h1>WiFi Configuration</h1>
<div class="card"><h2>Current Status</h2><div id="status"><div class="row"><span>Loading...</span></div></div></div>
<div class="card"><h2>Add Network</h2>
<form id="form">
<label>WiFi Network (SSID)</label>
<input type="text" id="ssid" name="ssid" required>
<div class="help">Network name</div>
<label>Password</label>
<input type="password" id="password" name="password" required>
<div class="help">Network password</div>
<button type="submit" id="btn">Save & Connect</button>
</form>
</div>
<div class="card"><p style="margin:0;font-size:0.9em;"><strong>Tip:</strong> ESP32 auto-connects to saved networks.</p></div>
</body></html>)";
    return html;
}

String ConfigServer::getNotificationsPageHTML() {
    String html = R"HTML(<!DOCTYPE html>
<html><head><meta name="viewport" content="width=device-width, initial-scale=1"><title>Notifications</title>
<style>
body{font-family:Arial,sans-serif;margin:0;padding:10px;max-width:700px;margin:0 auto;}
h1{font-size:1.5em;margin:10px 0;}
.card{border:1px solid #ccc;padding:15px;margin:10px 0;}
.card h2{font-size:1.1em;margin:0 0 10px 0;border-bottom:1px solid #ddd;padding-bottom:5px;}
h3{font-size:1em;margin:15px 0 10px;}
label{display:block;margin:10px 0 5px;font-weight:bold;}
input{width:100%;padding:8px;border:1px solid #ccc;font-size:1em;box-sizing:border-box;}
button{padding:10px 15px;margin:5px 5px 5px 0;border:1px solid #333;background:#fff;font-size:0.95em;cursor:pointer;}
button:active{background:#eee;}
.row{display:flex;justify-content:space-between;padding:5px 0;}
.badge{display:inline-block;padding:4px 8px;border:1px solid #666;font-size:0.85em;margin-top:5px;}
.badge.active{background:#4CAF50;color:#fff;border-color:#4CAF50;}
.badge.inactive{background:#333;color:#fff;border-color:#333;}
.help{font-size:0.85em;color:#666;margin-top:3px;}
.info{border:1px solid #ccc;padding:10px;margin-top:10px;}
</style>
<script>
async function loadNotif(){
try{
const r=await fetch('/notifications');
const d=await r.json();
document.getElementById('phone').value=d.phoneNumber||'';
const smsEl=document.getElementById('sms_status');
smsEl.textContent=d.hasPhoneNumber?'SMS: Configured':'SMS: Not Configured';
smsEl.className=d.hasPhoneNumber?'badge active':'badge inactive';
document.getElementById('webhook').value=d.discordWebhook||'';
const discEl=document.getElementById('disc_status');
discEl.textContent=d.hasDiscordWebhook?'Discord: Configured':'Discord: Not Configured';
discEl.className=d.hasDiscordWebhook?'badge active':'badge inactive';
}catch(e){console.error(e);}
}
async function loadEmerg(){
try{
const r=await fetch('/emergency-settings');
const d=await r.json();
document.getElementById('tier1_lv').value=d.emergencyWaterLevel_cm.toFixed(1);
document.getElementById('tier2_lv').value=d.urgentEmergencyWaterLevel_cm.toFixed(1);
document.getElementById('freq').value=(d.emergencyNotifFreq_ms/1000).toFixed(0);
document.getElementById('cur_tier1').textContent=d.emergencyWaterLevel_cm.toFixed(1)+' cm';
document.getElementById('cur_tier2').textContent=d.urgentEmergencyWaterLevel_cm.toFixed(1)+' cm';
document.getElementById('cur_freq').textContent=(d.emergencyNotifFreq_ms/1000)+' seconds';
}catch(e){console.error(e);}
}
async function savePhone(){
const phone=document.getElementById('phone').value;
if(!phone){alert('Enter phone number');return;}
try{
const r=await fetch('/notifications/phone',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'phone='+encodeURIComponent(phone)});
const d=await r.json();
alert(d.success?'Saved!':d.error);
if(d.success)loadNotif();
}catch(e){alert('Error: '+e.message);}
}
async function saveWebhook(){
const wh=document.getElementById('webhook').value;
if(!wh){alert('Enter webhook URL');return;}
try{
const r=await fetch('/notifications/discord',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'webhook='+encodeURIComponent(wh)});
const d=await r.json();
alert(d.success?'Saved!':d.error);
if(d.success)loadNotif();
}catch(e){alert('Error: '+e.message);}
}
async function testSMS(){
if(!confirm('Send test SMS?'))return;
const btn=document.getElementById('test_sms');
btn.disabled=true;
btn.textContent='Sending...';
try{
const r=await fetch('/notifications/test/sms',{method:'POST'});
const d=await r.json();
alert(d.success?d.message:d.error);
}catch(e){alert('Error: '+e.message);}finally{
btn.disabled=false;
btn.textContent='Test';
}
}
async function testDisc(){
if(!confirm('Send test Discord?'))return;
const btn=document.getElementById('test_disc');
btn.disabled=true;
btn.textContent='Sending...';
try{
const r=await fetch('/notifications/test/discord',{method:'POST'});
const d=await r.json();
alert(d.success?d.message:d.error);
}catch(e){alert('Error: '+e.message);}finally{
btn.disabled=false;
btn.textContent='Test';
}
}
async function saveTier1(){
const lv=document.getElementById('tier1_lv').value;
if(!lv){alert('Enter level');return;}
try{
const r=await fetch('/calibration/emergency-level',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'level_cm='+lv});
const d=await r.json();
alert(d.success?'Saved!':d.error);
if(d.success)loadEmerg();
}catch(e){alert('Error: '+e.message);}
}
async function saveTier2(){
const lv=document.getElementById('tier2_lv').value;
if(!lv){alert('Enter level');return;}
try{
const r=await fetch('/emergency/urgent-level',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'level_cm='+lv});
const d=await r.json();
alert(d.success?'Saved!':d.error);
if(d.success)loadEmerg();
}catch(e){alert('Error: '+e.message);}
}
async function saveFreq(){
const f=document.getElementById('freq').value;
if(!f){alert('Enter frequency');return;}
try{
const r=await fetch('/notifications/emergency-freq',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'freq_ms='+(f*1000)});
const d=await r.json();
alert(d.success?'Saved!':d.error);
if(d.success)loadEmerg();
}catch(e){alert('Error: '+e.message);}
}
window.onload=function(){loadNotif();loadEmerg();};
</script>
</head><body>
<a href="/" style="text-decoration:none;color:#000;">< Back</a>
<h1>Notification Settings</h1>
<div class="card"><h2>SMS Notifications (Twilio)</h2>
<span id="sms_status" class="badge">Loading...</span>
<label>Phone Number</label>
<input type="tel" id="phone" placeholder="+1234567890">
<div class="help">Include country code (e.g., +1 for US/Canada)</div>
<button onclick="savePhone()">Save</button>
<button id="test_sms" onclick="testSMS()">Test</button>
</div>
<div class="card"><h2>Discord Notifications</h2>
<span id="disc_status" class="badge">Loading...</span>
<label>Webhook URL</label>
<input type="url" id="webhook" placeholder="https://discord.com/api/webhooks/...">
<div class="help">From Discord: Server Settings → Integrations → Webhooks</div>
<button onclick="saveWebhook()">Save</button>
<button id="test_disc" onclick="testDisc()">Test</button>
</div>
<div class="card"><h2>Emergency Settings</h2>
<h3>Tier 1: Message Notifications</h3>
<label>Emergency Water Level (cm)</label>
<input type="number" id="tier1_lv" min="5" max="100" step="1">
<div class="help">Triggers SMS/Discord when water reaches this level</div>
<button onclick="saveTier1()">Save Tier 1 Level</button>
<h3>Tier 2: Horn Alarm (Urgent)</h3>
<label>Urgent Emergency Level (cm)</label>
<input type="number" id="tier2_lv" min="5" max="100" step="1">
<div class="help">Triggers horn alarm (must be higher than Tier 1)</div>
<button onclick="saveTier2()">Save Tier 2 Level</button>
<h3>Notification Frequency</h3>
<label>How often to send (seconds)</label>
<input type="number" id="freq" min="5" max="3600" step="1">
<div class="help">Frequency while in emergency state</div>
<button onclick="saveFreq()">Save Frequency</button>
<div class="info">
<strong>Current Settings:</strong><br>
Tier 1: <span id="cur_tier1">--</span><br>
Tier 2: <span id="cur_tier2">--</span><br>
Frequency: <span id="cur_freq">--</span>
</div>
</div>
</body></html>)HTML";
    return html;
}

// ============================================================================
// DEBUG AND MONITORING HANDLERS
// ============================================================================

void ConfigServer::handleGetReading() {
    serverStartTime = millis();

    if (!waterSensor) {
        String json = "{";
        json += "\"sensorAvailable\":false,";
        json += "\"error\":\"Water sensor not connected\"";
        json += "}";
        server->send(503, "application/json", json);
        return;
    }

    SensorReading reading = waterSensor->readLevel();
    String json = "{";
    json += "\"sensorAvailable\":true,";
    json += "\"valid\":" + String(reading.valid ? "true" : "false") + ",";
    json += "\"millivolts\":" + String(reading.millivolts, 2);
    if (reading.valid) {
        json += ",\"level_cm\":" + String(reading.level_cm, 2);
    }
    json += "}";

    server->send(200, "application/json", json);
}

void ConfigServer::handleDebug() {
    serverStartTime = millis();
    String html = getDebugPage();
    server->send(200, "text/html", html);
}

String ConfigServer::getDebugPage() {
    if (!waterSensor) {
        return "<html><body><h1>Debug Page</h1><p>Sensor not available</p></body></html>";
    }
    
    SensorReading reading = waterSensor->readLevel();
    
    String html = R"HTML(
        <!DOCTYPE html>
        <html>
        <head>
            <title>System Debug</title>
            <meta name="viewport" content="width=device-width, initial-scale=1">
            <style>
                body { font-family: Arial, sans-serif; max-width: 800px; margin: 20px auto; padding: 10px; }
                h1, h2 { padding-bottom: 5px; }
                table { width: 100%; border-collapse: collapse; margin: 10px 0; }
                td { padding: 8px; }
                .section { margin: 20px 0; padding: 10px; }
                label { display: block; margin: 10px 0 5px 0; }
                input { width: 100%; padding: 5px; box-sizing: border-box; }
                button { padding: 8px 15px; margin: 5px 5px 5px 0; cursor: pointer; }
                .nav { margin: 20px 0; }
                .nav a { margin: 0 10px; }
            </style>
            <script>
                function calibrateZero() {
                    const mv = document.getElementById('zero_mv').value;
                    const level = document.getElementById('zero_level').value || 0;
                    fetch('/calibrate/zero', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                        body: 'millivolts=' + mv + '&level_cm=' + level
                    }).then(r => r.json()).then(data => {
                        alert(data.success ? 'Zero point calibrated!' : data.error);
                        location.reload();
                    });
                }
                function calibratePoint2() {
                    const mv = document.getElementById('point2_mv').value;
                    const level = document.getElementById('point2_level').value;
                    if (!level) { alert('Please enter the water level in cm'); return; }
                    fetch('/calibrate/point2', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                        body: 'millivolts=' + mv + '&level_cm=' + level
                    }).then(r => r.json()).then(data => {
                        alert(data.success ? 'Second point calibrated!' : data.error);
                        location.reload();
                    });
                }
                function saveEmergencyLevel() {
                    const level = document.getElementById('emergency_level').value;
                    if (!level) { alert('Please enter emergency water level'); return; }
                    fetch('/calibration/emergency-level', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                        body: 'level_cm=' + level
                    }).then(r => r.json()).then(data => {
                        alert(data.success ? 'Emergency level saved!' : data.error);
                        location.reload();
                    });
                }
                function saveEmergencyFreq() {
                    const freq_sec = document.getElementById('emergency_freq').value;
                    if (!freq_sec) { alert('Please enter notification frequency in seconds'); return; }
                    const freq_ms = freq_sec * 1000;
                    fetch('/notifications/emergency-freq', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                        body: 'freq_ms=' + freq_ms
                    }).then(r => r.json()).then(data => {
                        alert(data.success ? 'Emergency frequency saved!' : data.error);
                        location.reload();
                    });
                }
                function savePhoneNumber() {
                    const phone = document.getElementById('phone_number').value;
                    if (!phone) { alert('Please enter a phone number'); return; }
                    fetch('/notifications/phone', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                        body: 'phone=' + encodeURIComponent(phone)
                    }).then(r => r.json()).then(data => {
                        alert(data.success ? 'Phone number saved!' : data.error);
                        location.reload();
                    });
                }
                function saveDiscordWebhook() {
                    const webhook = document.getElementById('discord_webhook').value;
                    if (!webhook) { alert('Please enter a Discord webhook URL'); return; }
                    fetch('/notifications/discord', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                        body: 'webhook=' + encodeURIComponent(webhook)
                    }).then(r => r.json()).then(data => {
                        alert(data.success ? 'Discord webhook saved!' : data.error);
                        location.reload();
                    });
                }
                function testSMS() {
                    if (!confirm('Send a test SMS message?')) return;
                    document.getElementById('sms_test_btn').disabled = true;
                    document.getElementById('sms_test_btn').textContent = 'Sending...';
                    fetch('/notifications/test/sms', {
                        method: 'POST'
                    }).then(r => r.json()).then(data => {
                        alert(data.success ? data.message : ('Error: ' + data.error));
                        document.getElementById('sms_test_btn').disabled = false;
                        document.getElementById('sms_test_btn').textContent = 'Test SMS';
                    }).catch(err => {
                        alert('Request failed: ' + err);
                        document.getElementById('sms_test_btn').disabled = false;
                        document.getElementById('sms_test_btn').textContent = 'Test SMS';
                    });
                }
                function testDiscord() {
                    if (!confirm('Send a test Discord message?')) return;
                    document.getElementById('discord_test_btn').disabled = true;
                    document.getElementById('discord_test_btn').textContent = 'Sending...';
                    fetch('/notifications/test/discord', {
                        method: 'POST'
                    }).then(r => r.json()).then(data => {
                        alert(data.success ? data.message : ('Error: ' + data.error));
                        document.getElementById('discord_test_btn').disabled = false;
                        document.getElementById('discord_test_btn').textContent = 'Test Discord';
                    }).catch(err => {
                        alert('Request failed: ' + err);
                        document.getElementById('discord_test_btn').disabled = false;
                        document.getElementById('discord_test_btn').textContent = 'Test Discord';
                    });
                }
                function saveUrgentEmergencyLevel() {
                    const level = document.getElementById('urgent_emergency_level').value;
                    if (!level) { alert('Please enter urgent emergency water level'); return; }
                    fetch('/emergency/urgent-level', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                        body: 'level_cm=' + level
                    }).then(r => r.json()).then(data => {
                        alert(data.success ? 'Urgent emergency level saved!' : data.error);
                        location.reload();
                    });
                }
                function testEmergencyPin() {
                    if (!confirm('Test the emergency pin output device? This will activate it for 2 seconds.')) return;
                    document.getElementById('pin_test_btn').disabled = true;
                    document.getElementById('pin_test_btn').textContent = 'Testing...';
                    fetch('/emergency/test-pin', {
                        method: 'POST'
                    }).then(r => r.json()).then(data => {
                        alert(data.success ? data.message : ('Error: ' + data.error));
                        document.getElementById('pin_test_btn').disabled = false;
                        document.getElementById('pin_test_btn').textContent = 'Test Emergency Pin';
                    }).catch(err => {
                        alert('Request failed: ' + err);
                        document.getElementById('pin_test_btn').disabled = false;
                        document.getElementById('pin_test_btn').textContent = 'Test Emergency Pin';
                    });
                }
            </script>
        </head>
        <body>
            <h1>System Debug & Calibration</h1>
            
            <h2>Current Sensor Reading</h2>
            <table>
                <tr><td>Status</td><td>)HTML";
    html += reading.valid ? "Valid" : "Invalid";
    html += R"HTML(</td></tr>
                <tr><td>Millivolts (Raw ADC)</td><td>)HTML";
    html += String(reading.millivolts, 2);
    html += R"HTML( mV</td></tr>
                <tr><td>Water Level</td><td>)HTML";
    if (reading.valid) {
        html += String(reading.level_cm, 2) + " cm";
    } else {
        html += "N/A";
    }
    html += R"HTML(</td></tr>
            </table>
            
            <h2>Calibration Settings</h2>
            <div class="section">
                <h3>Zero Point Calibration</h3>
                <p>Current Reading: <strong>)HTML";
    html += String(reading.millivolts, 0);
    html += R"HTML( mV</strong></p>
                <label for="zero_mv">Zero Point Millivolts:</label>
                <input type="number" id="zero_mv" min="0" max="3300">)HTML";
    html += "<script>document.getElementById('zero_mv').value=";
    html += String(waterSensor->getZeroPointMilliVolts());
    html += R"HTML(;</script>
                <label for="zero_level">Reference Level (cm) - optional:</label>
                <input type="number" id="zero_level" value="0" step="0.1" min="0">
                <button onclick="calibrateZero()">Set Zero Point</button>
                
                <h3>Second Point Calibration (2-Point)</h3>
                <p>Current Reading: <strong>)HTML";
    html += String(reading.millivolts, 0);
    html += R"HTML( mV</strong></p>
                <label for="point2_mv">Second Point Millivolts:</label>
                <input type="number" id="point2_mv" min="0" max="3300">)HTML";
    html += "<script>document.getElementById('point2_mv').value=";
    if (waterSensor->hasTwoPointCalibration()) {
        html += String(waterSensor->getSecondPointMilliVolts());
    } else {
        html += String(reading.millivolts, 0);
    }
    html += R"HTML(;</script>
                <label for="point2_level">Water Level at Second Point (cm):</label>
                <input type="number" id="point2_level" step="0.1" min="0" required>)HTML";
    if (waterSensor->hasTwoPointCalibration()) {
        html += "<script>document.getElementById('point2_level').value=";
        html += String(waterSensor->getSecondPointLevelCm(), 1);
        html += ";</script>";
    }
    html += R"HTML(
                <button onclick="calibratePoint2()">Set Second Point</button>
                
                <p><strong>Current Calibration:</strong><br>
                Zero Point: )HTML";
    html += String(waterSensor->getZeroPointMilliVolts());
    html += " mV = 0 cm<br>";
    if (waterSensor->hasTwoPointCalibration()) {
        html += "Second Point: " + String(waterSensor->getSecondPointMilliVolts()) + 
                " mV = " + String(waterSensor->getSecondPointLevelCm(), 2) + " cm<br>";
        html += "<em>2-point calibration is active</em>";
    } else {
        html += "<em>Single-point calibration</em>";
    }
    html += R"HTML(</p>
            </div>
            
            <h2>Emergency Settings</h2>
            <div class="section">
                <h3>Tier 1 Emergency - Message Notifications</h3>
                <p>Set the water level (in cm) that triggers emergency message alerts (SMS/Discord).</p>
                <label for="emergency_level">Tier 1 Emergency Level (cm):</label>
                <input type="number" id="emergency_level" step="1" min=")HTML";
    html += String(MIN_EMERGENCY_WATER_LEVEL_CM, 1);
    html += R"HTML(" max=")HTML";
    html += String(MAX_EMERGENCY_WATER_LEVEL_CM, 1);
    html += R"HTML(">)HTML";
    html += "<script>document.getElementById('emergency_level').value=";
    html += String(emergencyWaterLevel_cm, 1);
    html += R"HTML(;</script>
                <button onclick="saveEmergencyLevel()">Save Tier 1 Level</button>
                
                <h3>Tier 2 Emergency - Horn Alarm (Urgent)</h3>
                <p>Set the water level (in cm) that triggers the horn alarm. Must be higher than Tier 1.</p>
                <label for="urgent_emergency_level">Tier 2 Emergency Level (cm):</label>
                <input type="number" id="urgent_emergency_level" step="1" min=")HTML";
    html += String(MIN_EMERGENCY_WATER_LEVEL_CM, 1);
    html += R"HTML(" max=")HTML";
    html += String(MAX_EMERGENCY_WATER_LEVEL_CM, 1);
    html += R"HTML(">)HTML";
    html += "<script>document.getElementById('urgent_emergency_level').value=";
    html += String(urgentEmergencyWaterLevel_cm, 1);
    html += R"HTML(;</script>
                <button onclick="saveUrgentEmergencyLevel()">Save Tier 2 Level</button>
                <button id="pin_test_btn" onclick="testEmergencyPin()" style="background-color:#FF9800;color:white;">Test Emergency Pin</button>
                
                <h3>Emergency Notification Frequency</h3>
                <p>Set how often (in seconds) emergency notifications should be sent while in emergency state.</p>
                <label for="emergency_freq">Notification Frequency (seconds):</label>
                <input type="number" id="emergency_freq" min=")HTML";
    html += String(MIN_EMERGENCY_NOTIF_FREQ_MS / 1000);
    html += R"HTML(" max=")HTML";
    html += String(MAX_EMERGENCY_NOTIF_FREQ_MS / 1000);
    html += R"HTML(">)HTML";
    html += "<script>document.getElementById('emergency_freq').value=";
    html += String(emergencyNotifFreq_ms / 1000);
    html += R"HTML(;</script>
                <button onclick="saveEmergencyFreq()">Save Notification Frequency</button>
                
                <p><strong>Current Settings:</strong><br>
                <strong>Tier 1 (Messages):</strong> )HTML";
    html += String(emergencyWaterLevel_cm, 2);
    html += " cm<br><strong>Tier 2 (Horn Alarm):</strong> ";
    html += String(urgentEmergencyWaterLevel_cm, 2);
    html += " cm<br><strong>Notification Frequency:</strong> ";
    html += String(emergencyNotifFreq_ms / 1000);
    html += " seconds<br><strong>Horn Pattern:</strong> ";
    html += String(hornOnDuration_ms / 1000.0, 1);
    html += "s ON / ";
    html += String(hornOffDuration_ms / 1000.0, 1);
    html += R"HTML(s OFF</p>
            </div>
            
            <h2>Notification Settings</h2>
            <div class="section">
                <h3>SMS Notifications (Twilio)</h3>
                <label for="phone_number">Phone Number (with country code, e.g. +1234567890):</label>
                <input type="tel" id="phone_number" placeholder="+1234567890">)HTML";
    
    // Pre-fill phone number if available
    if (smsService && smsService->hasPhoneNumber()) {
        char phoneBuf[32];
        if (smsService->getPhoneNumber(phoneBuf, sizeof(phoneBuf)) == 0) {
            html += "<script>document.getElementById('phone_number').value='";
            html += String(phoneBuf);
            html += "';</script>";
        }
    }
    
    html += R"HTML(
                <button onclick="savePhoneNumber()">Save Phone Number</button>
                <button id="sms_test_btn" onclick="testSMS()" style="background-color:#4CAF50;color:white;">Test SMS</button>
                
                <h3>Discord Notifications</h3>
                <label for="discord_webhook">Discord Webhook URL:</label>
                <input type="url" id="discord_webhook" placeholder="https://discord.com/api/webhooks/...">)HTML";
    
    // Pre-fill webhook URL if available
    if (discordService && discordService->hasWebhookUrl()) {
        char webhookBuf[256];
        if (discordService->getWebhookUrl(webhookBuf, sizeof(webhookBuf)) == 0) {
            html += "<script>document.getElementById('discord_webhook').value='";
            html += String(webhookBuf);
            html += "';</script>";
        }
    }
    
    html += R"HTML(
                <button onclick="saveDiscordWebhook()">Save Discord Webhook</button>
                <button id="discord_test_btn" onclick="testDiscord()" style="background-color:#5865F2;color:white;">Test Discord</button>
                
                <p><strong>Current Status:</strong><br>)HTML";
    
    // Show current notification status
    if (smsService && smsService->hasPhoneNumber()) {
        html += "SMS: Configured<br>";
    } else {
        html += "SMS: Not configured<br>";
    }
    
    if (discordService && discordService->hasWebhookUrl()) {
        html += "Discord: Configured";
    } else {
        html += "Discord: Not configured";
    }
    
    html += R"HTML(</p>
            </div>
            
            <div class="nav">
                <a href="/">WiFi Config</a>
                <a href="/read">JSON Reading</a>
                <a href="/calibration">Calibration JSON</a>
                <a href="/notifications">Notifications JSON</a>
                <a href="/ota-settings">OTA Updates</a>
            </div>
        </body>
        </html>
    )HTML";
    return html;
}

// ============================================================================
// OTA UPDATE HANDLERS
// ============================================================================

void ConfigServer::handleOTAPage() {
    serverStartTime = millis();
    String html = getOTAPage();
    server->send(200, "text/html", html);
}

void ConfigServer::handleOTAStatus() {
    serverStartTime = millis();
    
    if (!otaManager) {
        server->send(503, "application/json", "{\"error\":\"OTA manager not available\"}");
        return;
    }
    
    String json = "{";
    json += "\"currentVersion\":\"" + otaManager->getCurrentVersion() + "\",";
    json += "\"availableVersion\":\"" + otaManager->getAvailableVersion() + "\",";
    json += "\"updateAvailable\":" + String(otaManager->isUpdateAvailable() ? "true" : "false") + ",";
    json += "\"state\":\"";
    
    switch(otaManager->getState()) {
        case OTAState::IDLE: json += "idle"; break;
        case OTAState::CHECKING: json += "checking"; break;
        case OTAState::UPDATE_AVAILABLE: json += "update_available"; break;
        case OTAState::DOWNLOADING: json += "downloading"; break;
        case OTAState::INSTALLING: json += "installing"; break;
        case OTAState::SUCCESS: json += "success"; break;
        case OTAState::FAILED: json += "failed"; break;
    }
    
    json += "\",";
    json += "\"lastError\":\"" + otaManager->getLastError() + "\",";
    json += "\"autoCheckEnabled\":" + String(otaManager->isAutoCheckEnabled() ? "true" : "false") + ",";
    json += "\"autoInstallEnabled\":" + String(otaManager->isAutoInstallEnabled() ? "true" : "false") + ",";
    json += "\"notificationsEnabled\":" + String(otaManager->areNotificationsEnabled() ? "true" : "false") + ",";
    json += "\"githubRepo\":\"" + otaManager->getGitHubRepo() + "\",";
    json += "\"checkIntervalHours\":" + String(otaManager->getCheckIntervalMs() / 3600000) + ",";
    json += "\"timeSinceLastCheckHours\":" + String(otaManager->getTimeSinceLastCheck() / 3600000, 1);
    json += "}";
    
    server->send(200, "application/json", json);
}

void ConfigServer::handleOTACheck() {
    serverStartTime = millis();
    
    if (!otaManager) {
        server->send(503, "application/json", "{\"error\":\"OTA manager not available\"}");
        return;
    }
    
    bool updateFound = otaManager->manualCheckForUpdates();
    
    String json = "{";
    json += "\"success\":true,";
    json += "\"updateAvailable\":" + String(updateFound ? "true" : "false");
    if (updateFound) {
        json += ",\"version\":\"" + otaManager->getAvailableVersion() + "\"";
    }
    json += "}";
    
    server->send(200, "application/json", json);
}

void ConfigServer::handleOTAUpdate() {
    serverStartTime = millis();
    
    if (!otaManager) {
        server->send(503, "application/json", "{\"error\":\"OTA manager not available\"}");
        return;
    }
    
    // Get optional password from POST data
    const char* password = nullptr;
    if (server->hasArg("password")) {
        password = server->arg("password").c_str();
    }
    
    bool success = otaManager->startUpdate(password);
    
    if (!success) {
        String json = "{\"success\":false,\"error\":\"" + otaManager->getLastError() + "\"}";
        server->send(400, "application/json", json);
    } else {
        // This will likely not be received as ESP32 will reboot
        server->send(200, "application/json", "{\"success\":true,\"message\":\"Update started, device will reboot\"}");
    }
}

void ConfigServer::handleOTASettings() {
    serverStartTime = millis();
    
    if (!otaManager) {
        server->send(503, "application/json", "{\"error\":\"OTA manager not available\"}");
        return;
    }
    
    bool updated = false;
    
    // GitHub repository
    if (server->hasArg("github_owner") && server->hasArg("github_repo")) {
        String owner = server->arg("github_owner");
        String repo = server->arg("github_repo");
        otaManager->setGitHubRepo(owner.c_str(), repo.c_str());
        updated = true;
    }
    
    // GitHub token (for private repos)
    if (server->hasArg("github_token")) {
        String token = server->arg("github_token");
        otaManager->setGitHubToken(token.c_str());
        updated = true;
    }
    
    // Update password
    if (server->hasArg("update_password")) {
        String password = server->arg("update_password");
        otaManager->setUpdatePassword(password.c_str());
        updated = true;
    }
    
    // Auto-check settings
    if (server->hasArg("auto_check")) {
        bool enabled = server->arg("auto_check") == "true";
        unsigned long intervalMs = DEFAULT_CHECK_INTERVAL_MS;
        
        if (server->hasArg("check_interval_hours")) {
            int hours = server->arg("check_interval_hours").toInt();
            if (hours > 0 && hours <= 168) { // Max 1 week
                intervalMs = hours * 3600000UL;
            }
        }
        
        otaManager->setAutoCheck(enabled, intervalMs);
        updated = true;
    }
    
    // Auto-install setting
    if (server->hasArg("auto_install")) {
        bool enabled = server->arg("auto_install") == "true";
        otaManager->setAutoInstall(enabled);
        updated = true;
    }
    
    // Notifications enabled
    if (server->hasArg("notifications_enabled")) {
        bool enabled = server->arg("notifications_enabled") == "true";
        otaManager->setNotificationsEnabled(enabled);
        updated = true;
    }
    
    if (updated) {
        server->send(200, "application/json", "{\"success\":true,\"message\":\"OTA settings updated\"}");
    } else {
        server->send(400, "application/json", "{\"error\":\"No valid settings provided\"}");
    }
}

String ConfigServer::getOTAPage() {
    String html = R"HTML(<!DOCTYPE html>
<html><head><meta name="viewport" content="width=device-width, initial-scale=1"><title>OTA Updates</title>
<style>
body{font-family:Arial,sans-serif;margin:0;padding:10px;max-width:700px;margin:0 auto;}
h1{font-size:1.5em;margin:10px 0;}
.card{border:1px solid #ccc;padding:15px;margin:10px 0;}
.card h2{font-size:1.1em;margin:0 0 10px 0;border-bottom:1px solid #ddd;padding-bottom:5px;}
.row{display:flex;justify-content:space-between;padding:5px 0;border-bottom:1px solid #eee;}
.row:last-child{border-bottom:none;}
.label{font-weight:bold;}
label{display:block;margin:10px 0 5px;font-weight:bold;}
input,select{width:100%;padding:8px;border:1px solid #ccc;font-size:1em;box-sizing:border-box;}
button{padding:10px 15px;margin:5px 5px 5px 0;border:1px solid #333;background:#fff;font-size:0.95em;cursor:pointer;}
button:active{background:#eee;}
button:disabled{opacity:0.5;cursor:not-allowed;}
.primary{background:#4CAF50;color:#fff;border-color:#4CAF50;}
.danger{background:#f44336;color:#fff;border-color:#f44336;}
.help{font-size:0.85em;color:#666;margin-top:3px;}
.status{padding:10px;margin:10px 0;border:1px solid #ccc;text-align:center;}
.status.success{background:#d4edda;border-color:#c3e6cb;color:#155724;}
.status.error{background:#f8d7da;border-color:#f5c6cb;color:#721c24;}
.status.warning{background:#fff3cd;border-color:#ffeaa7;color:#856404;}
.version{font-size:1.5em;text-align:center;margin:10px 0;}
</style>
<script>
let checking=false;
let updating=false;
async function loadStatus(){
try{
const r=await fetch('/ota/status');
const d=await r.json();
document.getElementById('cur_ver').textContent=d.currentVersion;
document.getElementById('gh_repo').value=d.githubRepo;
document.getElementById('auto_check').checked=d.autoCheckEnabled;
document.getElementById('auto_install').checked=d.autoInstallEnabled;
document.getElementById('check_interval').value=d.checkIntervalHours;
document.getElementById('notify').checked=d.notificationsEnabled;
document.getElementById('last_check').textContent=d.timeSinceLastCheckHours.toFixed(1)+' hours ago';
const updateDiv=document.getElementById('update_status');
if(d.updateAvailable){
updateDiv.className='status success';
updateDiv.innerHTML='<strong>Update Available!</strong><br>Version '+d.availableVersion+' is ready to install';
document.getElementById('install_btn').disabled=false;
}else if(d.state==='checking'){
updateDiv.className='status warning';
updateDiv.textContent='Checking for updates...';
}else if(d.state==='failed'){
updateDiv.className='status error';
updateDiv.textContent='Error: '+d.lastError;
}else{
updateDiv.className='status';
updateDiv.textContent='No updates available. Current version: '+d.currentVersion;
document.getElementById('install_btn').disabled=true;
}
}catch(e){console.error(e);}
}
async function checkUpdates(){
if(checking)return;
checking=true;
const btn=document.getElementById('check_btn');
btn.disabled=true;
btn.textContent='Checking...';
const statusDiv=document.getElementById('update_status');
statusDiv.className='status warning';
statusDiv.textContent='Checking GitHub for updates...';
try{
const r=await fetch('/ota/check');
const d=await r.json();
await new Promise(res=>setTimeout(res,1000));
await loadStatus();
}catch(e){
statusDiv.className='status error';
statusDiv.textContent='Error checking for updates: '+e.message;
}finally{
checking=false;
btn.disabled=false;
btn.textContent='Check for Updates';
}
}
async function installUpdate(){
if(updating)return;
const pwd=document.getElementById('update_pwd').value;
if(!confirm('Install firmware update? Device will reboot.'))return;
updating=true;
const btn=document.getElementById('install_btn');
btn.disabled=true;
btn.textContent='Installing...';
const statusDiv=document.getElementById('update_status');
statusDiv.className='status warning';
statusDiv.textContent='Downloading and installing update... Device will reboot shortly.';
try{
const formData=new URLSearchParams();
if(pwd)formData.append('password',pwd);
const r=await fetch('/ota/update',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:formData});
const d=await r.json();
if(d.success){
statusDiv.className='status success';
statusDiv.textContent='Update started! Device is rebooting...';
}else{
statusDiv.className='status error';
statusDiv.textContent='Update failed: '+(d.error||'Unknown error');
updating=false;
btn.disabled=false;
btn.textContent='Install Update';
}
}catch(e){
statusDiv.className='status warning';
statusDiv.textContent='Update may be in progress (device rebooting)...';
}
}
async function saveSettings(){
const owner=document.getElementById('gh_repo').value.split('/')[0]||'';
const repo=document.getElementById('gh_repo').value.split('/')[1]||'';
const token=document.getElementById('gh_token').value;
const pwd=document.getElementById('ota_pwd').value;
const autoCheck=document.getElementById('auto_check').checked;
const autoInstall=document.getElementById('auto_install').checked;
const interval=document.getElementById('check_interval').value;
const notify=document.getElementById('notify').checked;
if(!owner||!repo){alert('Enter GitHub repository (owner/repo)');return;}
try{
const formData=new URLSearchParams();
formData.append('github_owner',owner);
formData.append('github_repo',repo);
if(token)formData.append('github_token',token);
if(pwd)formData.append('update_password',pwd);
formData.append('auto_check',autoCheck);
formData.append('auto_install',autoInstall);
formData.append('check_interval_hours',interval);
formData.append('notifications_enabled',notify);
const r=await fetch('/ota/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:formData});
const d=await r.json();
alert(d.success?'Settings saved!':d.error);
if(d.success)loadStatus();
}catch(e){alert('Error: '+e.message);}
}
window.onload=loadStatus;
</script>
</head><body>
<a href="/" style="text-decoration:none;color:#000;">< Back</a>
<h1>Firmware Updates (OTA)</h1>
<div class="card"><h2>Current Version</h2>
<div class="version"><span id="cur_ver">--</span></div>
<div class="help">Last checked: <span id="last_check">--</span></div>
</div>
<div class="card"><h2>Update Status</h2>
<div id="update_status" class="status">Loading...</div>
<button id="check_btn" onclick="checkUpdates()">Check for Updates</button>
<div style="margin-top:15px;">
<label>Update Password (if set)</label>
<input type="password" id="update_pwd" placeholder="Leave blank if no password">
<button id="install_btn" class="primary" onclick="installUpdate()" disabled>Install Update</button>
</div>
</div>
<div class="card"><h2>OTA Settings</h2>
<label>GitHub Repository</label>
<input type="text" id="gh_repo" placeholder="owner/repository">
<div class="help">Enter the GitHub repository where firmware releases are published</div>
<label>GitHub Token (optional, for private repos)</label>
<input type="password" id="gh_token" placeholder="Leave blank for public repos">
<div class="help">Personal access token for private repositories</div>
<label>Update Password (optional)</label>
<input type="password" id="ota_pwd" placeholder="Leave blank for no password">
<div class="help">Require password to install updates</div>
<h3 style="margin-top:20px;">Automatic Updates</h3>
<div style="display:flex;align-items:center;margin:10px 0;">
<input type="checkbox" id="auto_check" style="width:auto;margin-right:10px;">
<label for="auto_check" style="margin:0;">Enable automatic update checks</label>
</div>
<label>Check interval (hours)</label>
<input type="number" id="check_interval" min="1" max="168" value="24">
<div class="help">How often to check for updates (1-168 hours)</div>
<div style="display:flex;align-items:center;margin:10px 0;">
<input type="checkbox" id="auto_install" style="width:auto;margin-right:10px;">
<label for="auto_install" style="margin:0;">Enable automatic update installation</label>
</div>
<div class="help" style="color:#d84315;">⚠️ When enabled, updates will install automatically without user confirmation. Device will reboot.</div>
<div style="display:flex;align-items:center;margin:10px 0;">
<input type="checkbox" id="notify" style="width:auto;margin-right:10px;">
<label for="notify" style="margin:0;">Enable OTA notifications</label>
</div>
<div class="help">Send SMS/Discord notifications about update status</div>
<button onclick="saveSettings()">Save Settings</button>
</div>
</body></html>)HTML";
    return html;
}

