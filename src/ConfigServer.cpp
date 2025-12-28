#include "ConfigServer.h"

// ============================================================================
// CORE LIFECYCLE METHODS
// ============================================================================

ConfigServer::ConfigServer(WaterPressureSensor* sensor, SendSMS* sms, SendDiscord* discord) 
    : server(nullptr), waterSensor(sensor), smsService(sms), discordService(discord) {
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
    Serial.println("\n=== Starting WiFi Setup Mode ===");

    if (setupModeActive) {
        Serial.println("...Already in setup mode");
        return;
    }
    
    // Step 1: Set WiFi to AP+STA mode
    WiFi.mode(WIFI_AP_STA);
    
    // Step 2: Start AP (Access Point)
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    
    // Get AP IP address (usually 192.168.4.1)
    IPAddress apIP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(apIP);
    Serial.print("Connect to SSID: ");
    Serial.println(AP_SSID);
    Serial.print("Password: ");
    Serial.println(AP_PASSWORD);
    
    // Step 3: Start DNS server for captive portal
    dnsServer = new DNSServer();
    dnsServer->start(DNS_PORT, "*", apIP);
    Serial.println("Captive portal started - users will be automatically redirected");
    Serial.println("All DNS requests will redirect to config server");
    
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
    
    // Route: GET /read → return current sensor reading as JSON
    server->on("/read", HTTP_GET, [this]() { handleGetReading(); });
    
    // Route: GET /calibration → return current calibration settings
    server->on("/calibration", HTTP_GET, [this]() { handleGetCalibration(); });
    
    // Route: POST /calibrate/zero → set zero calibration point
    server->on("/calibrate/zero", HTTP_POST, [this]() { handleCalibrateZero(); });
    
    // Route: POST /calibrate/point2 → set second calibration point
    server->on("/calibrate/point2", HTTP_POST, [this]() { handleCalibratePoint2(); });

    // Route: POST /calibration/emergency-level -> set emergency water level
    server->on("/calibration/emergency-level", HTTP_POST, [this]() { handleSetEmergencyLevel(); });
    
    // Route: GET /emergency-settings → return current emergency settings
    server->on("/emergency-settings", HTTP_GET, [this]() { 
        String json = "{";
        json += "\"emergencyWaterLevel_cm\":" + String(emergencyWaterLevel_cm, 2) + ",";
        json += "\"emergencyNotifFreq_ms\":" + String(emergencyNotifFreq_ms);
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
    
    Serial.println("Setup mode started. Open browser and navigate to 192.168.4.1");
    Serial.println("Or simply open any website - captive portal will redirect you!");
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
        Serial.println("Captive portal stopped");
    }
    
    // Stop AP, keep STA mode
    WiFi.mode(WIFI_STA);
    setupModeActive = false;

    Serial.println("\n=== Setup mode stopped, resuming normal WiFi ===");
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

void ConfigServer::handleSubmit() {
    // Check if SSID and password were submitted
    if (server->hasArg("ssid") && server->hasArg("password")) {
        String ssid = server->arg("ssid");
        String password = server->arg("password");
        
        Serial.println("\nConfiguration received!");
        Serial.print("SSID: ");
        Serial.println(ssid);
        Serial.print("Password: ");
        Serial.println(password);
        
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
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI());
    json += "}";
    
    server->send(200, "application/json", json);
}

String ConfigServer::getConfigPage() {
    String html = R"(
        <!DOCTYPE html>
        <html>
        <head>
            <title>ESP32 WiFi Setup</title>
            <meta name="viewport" content="width=device-width, initial-scale=1">
            <style>
                body { font-family: Arial, sans-serif; max-width: 500px; margin: 20px auto; padding: 10px; }
                h1 { text-align: center; }
                form { margin: 20px 0; }
                label { display: block; margin: 10px 0 5px 0; }
                input { width: 100%; padding: 8px; box-sizing: border-box; }
                button { width: 100%; padding: 10px; margin: 5px 0; cursor: pointer; }
                .info { padding: 10px; margin: 10px 0; }
            </style>
        </head>
        <body>
            <h1>ESP32 WiFi Setup</h1>
            <div class="info">Configure your WiFi credentials below.</div>
            <form method="POST" action="/config">
                <label for="ssid">WiFi Network (SSID)</label>
                <input type="text" id="ssid" name="ssid" placeholder="Enter WiFi name" required>
                <label for="password">Password</label>
                <input type="password" id="password" name="password" placeholder="Enter WiFi password" required>
                <button type="submit">Save & Connect</button>
            </form>
            <form method="GET" action="/status"><button type="submit">Check WiFi Status</button></form>
            <form method="GET" action="/read"><button type="submit">Read Water Sensor</button></form>
            <form method="GET" action="/debug"><button type="submit">Debug & Calibration</button></form>
        </body>
        </html>
    )";
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
        Serial.print("Failed to load the calibration NVS storage in read mode");
    }
    
    int zero_mv = calibrationPrefs.getInt("zero_mv", -1);
    if (zero_mv >= 0) {
        waterSensor->setCalibrationPoint(0, zero_mv, 0.0f);
        Serial.printf("[CALIBRATION] Loaded zero point from NVS: %d mV\n", zero_mv);
    } else {
        Serial.println("[CALIBRATION] No zero point calibration found in NVS, using default");
    }
    
    int point2_mv = calibrationPrefs.getInt("point2_mv", -1);
    float point2_cm = calibrationPrefs.getFloat("point2_cm", -1.0f);
    if (point2_mv >= 0 && point2_cm >= 0) {
        waterSensor->setCalibrationPoint(1, point2_mv, point2_cm);
        Serial.printf("[CALIBRATION] Loaded second point from NVS: %d mV = %.2f cm (2-point calibration active)\n", 
                      point2_mv, point2_cm);
    } else {
        Serial.println("[CALIBRATION] No second calibration point found in NVS");
    }

    calibrationPrefs.end();
}

void ConfigServer::saveCalibration() {
    if (!waterSensor) return;

    if (!calibrationPrefs.begin(SENSOR_CALIBRATION_NAMESPACE, false)) {
        Serial.print("Failed to load the calibration NVS storage in write mode");
    }
    
    int zero_mv = waterSensor->getZeroPointMilliVolts();
    calibrationPrefs.putInt("zero_mv", zero_mv);
    Serial.printf("[CALIBRATION] Saved zero point to NVS: %d mV\n", zero_mv);
    
    if (waterSensor->hasTwoPointCalibration()) {
        int point2_mv = waterSensor->getSecondPointMilliVolts();
        float point2_cm = waterSensor->getSecondPointLevelCm();
        calibrationPrefs.putInt("point2_mv", point2_mv);
        calibrationPrefs.putFloat("point2_cm", point2_cm);
        Serial.printf("[CALIBRATION] Saved second point to NVS: %d mV = %.2f cm (2-point calibration)\n", 
                      point2_mv, point2_cm);
    } else {
        calibrationPrefs.remove("point2_mv");
        calibrationPrefs.remove("point2_cm");
        Serial.println("[CALIBRATION] Removed second calibration point from NVS (single-point mode)");
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
        
        emergencyWaterLevel_cm = level_cm;
        saveEmergencySettings();
        
        String json = "{";
        json += "\"success\":true,";
        json += "\"message\":\"Emergency water level updated\",";
        json += "\"level_cm\":" + String(level_cm, 2);
        json += "}";
        
        server->send(200, "application/json", json);
        Serial.printf("[CONFIG] Emergency water level updated: %.2f cm\n", level_cm);
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
        Serial.printf("[CONFIG] Emergency notification frequency updated: %d ms (%d seconds)\n", freq_ms, freq_ms / 1000);
    } else {
        server->send(400, "application/json", "{\"error\":\"Missing freq_ms parameter\"}");
    }
}

void ConfigServer::loadEmergencySettings() {
    // Set defaults first
    emergencyWaterLevel_cm = DEFAULT_EMERGENCY_WATER_LEVEL_CM;
    emergencyNotifFreq_ms = DEFAULT_EMERGENCY_NOTIF_FREQ_MS;
    
    if (!emergencyPrefs.begin(EMERGENCY_SETTINGS_NAMESPACE, true)) {
        Serial.println("[EMERGENCY] Failed to load emergency settings NVS storage in read mode");
        return;
    }
    
    float saved_level = emergencyPrefs.getFloat("level_cm", -1.0f);
    if (saved_level >= 0) {
        emergencyWaterLevel_cm = saved_level;
        Serial.printf("[EMERGENCY] Loaded emergency water level from NVS: %.2f cm\n", emergencyWaterLevel_cm);
    } else {
        Serial.printf("[EMERGENCY] No saved emergency water level found, using default: %.2f cm\n", emergencyWaterLevel_cm);
    }
    
    int saved_freq = emergencyPrefs.getInt("notif_freq_ms", -1);
    if (saved_freq >= 0) {
        emergencyNotifFreq_ms = saved_freq;
        Serial.printf("[EMERGENCY] Loaded emergency notification frequency from NVS: %d ms (%d seconds)\n", 
                      emergencyNotifFreq_ms, emergencyNotifFreq_ms / 1000);
    } else {
        Serial.printf("[EMERGENCY] No saved notification frequency found, using default: %d ms (%d seconds)\n",
                      emergencyNotifFreq_ms, emergencyNotifFreq_ms / 1000);
    }
    
    emergencyPrefs.end();
}

void ConfigServer::saveEmergencySettings() {
    if (!emergencyPrefs.begin(EMERGENCY_SETTINGS_NAMESPACE, false)) {
        Serial.println("[EMERGENCY] Failed to load emergency settings NVS storage in write mode");
        return;
    }
    
    emergencyPrefs.putFloat("level_cm", emergencyWaterLevel_cm);
    emergencyPrefs.putInt("notif_freq_ms", emergencyNotifFreq_ms);
    
    Serial.printf("[EMERGENCY] Saved emergency settings to NVS: level=%.2f cm, freq=%d ms\n", 
                  emergencyWaterLevel_cm, emergencyNotifFreq_ms);
    
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
        Serial.printf("[CONFIG] Phone number updated: %s\n", phone.c_str());
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
        Serial.printf("[CONFIG] Discord webhook updated: %s\n", webhook.c_str());
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
    
    Serial.println("[TEST] Sending test SMS...");
    bool success = smsService->send("Boat Monitor Test: This is a test message from your ESP32 boat monitor.");
    
    if (success) {
        Serial.println("[TEST] Test SMS sent successfully!");
        server->send(200, "application/json", "{\"success\":true,\"message\":\"Test SMS sent successfully!\"}");
    } else {
        Serial.println("[TEST] Test SMS failed to send");
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
    
    Serial.println("[TEST] Sending test Discord message...");
    bool success = discordService->send("🚤 **Boat Monitor Test** - This is a test message from your ESP32 boat monitor.");
    
    if (success) {
        Serial.println("[TEST] Test Discord message sent successfully!");
        server->send(200, "application/json", "{\"success\":true,\"message\":\"Test Discord message sent successfully!\"}");
    } else {
        Serial.println("[TEST] Test Discord message failed to send");
        server->send(500, "application/json", "{\"success\":false,\"error\":\"Failed to send test Discord message. Check serial log for details.\"}");
    }
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
                <h3>Emergency Water Level Threshold</h3>
                <p>Set the water level (in cm) that triggers emergency alerts.</p>
                <label for="emergency_level">Emergency Level (cm):</label>
                <input type="number" id="emergency_level" step="1" min=")HTML";
    html += String(MIN_EMERGENCY_WATER_LEVEL_CM, 1);
    html += R"HTML(" max=")HTML";
    html += String(MAX_EMERGENCY_WATER_LEVEL_CM, 1);
    html += R"HTML(">)HTML";
    html += "<script>document.getElementById('emergency_level').value=";
    html += String(emergencyWaterLevel_cm, 1);
    html += R"HTML(;</script>
                <button onclick="saveEmergencyLevel()">Save Emergency Level</button>
                
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
                Emergency Level: )HTML";
    html += String(emergencyWaterLevel_cm, 2);
    html += " cm<br>Notification Frequency: ";
    html += String(emergencyNotifFreq_ms / 1000);
    html += R"HTML( seconds</p>
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
            </div>
        </body>
        </html>
    )HTML";
    return html;
}

