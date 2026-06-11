#include "ConfigServer.h"
#include "Logger.h"
#include "Version.h"
#include "compressed_pages.h"

// ============================================================================
// CORE LIFECYCLE METHODS
// ============================================================================

ConfigServer::ConfigServer(WaterPressureSensor* sensor, SendSMS* sms, SendDiscord* discord,
                           OTAManager* ota, MQTTService* mqtt, SettingsStore* settings)
    : server(nullptr), waterSensor(sensor), smsService(sms), discordService(discord),
      otaManager(ota), mqttService(mqtt), settingsStore(settings) {
    // Generate unique AP password from chip ID
    uint64_t chipId = ESP.getEfuseMac();
    uint32_t id = (uint32_t)(chipId & 0xFFFFFFFF);
    char passwordBuf[15];
    snprintf(passwordBuf, sizeof(passwordBuf), "Boat%08X", id);
    apPassword = String(passwordBuf);

    // Initialize calibration preferences
    loadCalibration();
    // Emergency settings are owned by SettingsStore; caller must call
    // settingsStore->load() before constructing ConfigServer.
}

ConfigServer::~ConfigServer() {
    // Clean up DNS server if active
    if (dnsServer) {
        dnsServer->stop();
        delete dnsServer;
        dnsServer = nullptr;
    }
    stopSetupMode();
    calibrationPrefs.end(); // Close calibration NVS namespace
    // Emergency settings NVS is managed by SettingsStore
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

    // Route: GET /init → merged JSON for main page load (wifi + sensor + thresholds)
    server->on("/init", HTTP_GET, [this]() { handleInit(); });

    // Route: GET /settings/init → merged JSON for settings page load
    server->on("/settings/init", HTTP_GET, [this]() { handleSettingsInit(); });

    // Route: GET /debug/init → merged JSON for debug page load (reading + calibration)
    server->on("/debug/init", HTTP_GET, [this]() { handleDebugInit(); });

    // Route: POST /config → save credentials
    server->on("/config", HTTP_POST, [this]() { handleSubmit(); });
    
    // Route: GET /status → return status JSON
    server->on("/status", HTTP_GET, [this]() { handleStatus(); });
    
    // Route: GET /debug → serve debug page with detailed sensor info
    server->on("/debug", HTTP_GET, [this]() { handleDebug(); });
    
    // Route: GET /settings → serve settings hub page
    server->on("/settings", HTTP_GET, [this]() { handleSettings(); });

    // Route: GET /wifi-config → serve WiFi configuration page
    server->on("/wifi-config", HTTP_GET, [this]() { handleWiFiConfig(); });

    // Route: GET /wifi/networks → return stored SSID list as JSON
    server->on("/wifi/networks", HTTP_GET, [this]() { handleWiFiNetworks(); });

    // Route: POST /wifi/remove → remove a stored network by SSID
    server->on("/wifi/remove", HTTP_POST, [this]() { handleWiFiRemove(); });
    
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
        float el  = settingsStore ? settingsStore->getEmergencyWaterLevel()       : 30.0f;
        int   ef  = settingsStore ? settingsStore->getEmergencyNotifFreq()        : 900000;
        float ul  = settingsStore ? settingsStore->getUrgentEmergencyWaterLevel() : 50.0f;
        int   hon = settingsStore ? settingsStore->getHornOnDuration()            : 1000;
        int   hof = settingsStore ? settingsStore->getHornOffDuration()           : 1000;
        String json = "{";
        json += "\"emergencyWaterLevel_cm\":" + String(el, 2) + ",";
        json += "\"emergencyNotifFreq_ms\":" + String(ef) + ",";
        json += "\"urgentEmergencyWaterLevel_cm\":" + String(ul, 2) + ",";
        json += "\"hornOnDuration_ms\":" + String(hon) + ",";
        json += "\"hornOffDuration_ms\":" + String(hof);
        json += "}";
        server->send(200, "application/json", json);
        serverStartTime = millis();
    });
    
    // Route: GET /notifications → return current notification settings
    server->on("/notifications", HTTP_GET, [this]() { handleGetNotifications(); });

    // Route: GET /notifications/status → lean status-only JSON (booleans, no secrets) for polling
    server->on("/notifications/status", HTTP_GET, [this]() { handleNotificationsStatus(); });

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

    // Route: POST /notifications/mqtt → configure MQTT broker
    server->on("/notifications/mqtt", HTTP_POST, [this]() { handleSetMqttConfig(); });

    // Route: POST /notifications/test/mqtt → send test MQTT message
    server->on("/notifications/test/mqtt", HTTP_POST, [this]() { handleTestMqtt(); });

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
    
    // Handle 404 and captive portal detection.
    // Captive portal probes (Apple/Android/Windows/ChromeOS) all hit unknown paths to
    // decide if a portal exists. Returning a tiny 302 here triggers the OS portal
    // popup AND frees the single-connection slot fast (vs. serving the full ~5 KB
    // index page to every probe).
    server->onNotFound([this]() { handleCaptivePortalProbe(); });
    
    // Enable reading If-None-Match for ETag-based caching
    const char* headersToCollect[] = {"If-None-Match"};
    server->collectHeaders(headersToCollect, 1);

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

    // Reconnect using the full scan-and-pick path so any newly added network
    // is tried immediately rather than waiting for maintainConnection() to fire.
    WiFiManager::getInstance().connectToBestNetwork();
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

void ConfigServer::sendCachedPage(const char* data, size_t len, const char* contentType) {
    serverStartTime = millis();
    if (server->hasHeader("If-None-Match") && server->header("If-None-Match") == FIRMWARE_VERSION) {
        server->send(304);
        return;
    }
    server->sendHeader("Cache-Control", "max-age=86400, must-revalidate");
    server->sendHeader("ETag", FIRMWARE_VERSION);
    server->sendHeader("Content-Encoding", "gzip");
    server->send_P(200, contentType, data, len);
}

void ConfigServer::handleRoot() {
    sendCachedPage((const char*)INDEX_HTML_GZ, INDEX_HTML_GZ_LEN, "text/html");
}

void ConfigServer::handleWiFiConfig() {
    sendCachedPage((const char*)WIFI_CONFIG_HTML_GZ, WIFI_CONFIG_HTML_GZ_LEN, "text/html");
}

void ConfigServer::handleNotificationsPage() {
    sendCachedPage((const char*)NOTIFICATIONS_HTML_GZ, NOTIFICATIONS_HTML_GZ_LEN, "text/html");
}

void ConfigServer::handleSettings() {
    sendCachedPage((const char*)SETTINGS_HTML_GZ, SETTINGS_HTML_GZ_LEN, "text/html");
}

void ConfigServer::handleInit() {
    String json = "{";

    bool connected = WiFiManager::getInstance().isConnected();
    json += "\"wifi\":{";
    json += "\"connected\":" + String(connected ? "true" : "false") + ",";
    json += "\"ssid\":\"" + WiFi.SSID() + "\",";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI());
    json += "},";

    json += "\"sensor\":{";
    if (!waterSensor) {
        json += "\"sensorAvailable\":false";
    } else {
        SensorReading reading = waterSensor->readLevel();
        json += "\"sensorAvailable\":true,";
        json += "\"valid\":" + String(reading.valid ? "true" : "false");
        if (reading.valid) {
            json += ",\"level_cm\":" + String(reading.level_cm, 2);
        }
        float rate = waterSensor->getRateOfChange_cm30min();
        if (!isnan(rate)) {
            json += ",\"rate_cm_30min\":" + String(rate, 2);
        }
    }
    json += "},";

    json += "\"thresholds\":{";
    json += "\"emergencyWaterLevel_cm\":" + String(getEmergencyWaterLevel(), 2) + ",";
    json += "\"urgentEmergencyWaterLevel_cm\":" + String(getUrgentEmergencyWaterLevel(), 2);
    json += "}";

    json += "}";
    server->send(200, "application/json", json);
    serverStartTime = millis();
}

void ConfigServer::handleSettingsInit() {
    String json = "{";

    // notifications block (mirrors fields settings.html reads from /notifications)
    bool mqttCfg = mqttService && mqttService->hasBrokerConfig();
    json += "\"notifications\":{";
    json += "\"hasPhoneNumber\":";
    json += (smsService && smsService->hasPhoneNumber()) ? "true" : "false";
    json += ",\"hasDiscordWebhook\":";
    json += (discordService && discordService->hasWebhookUrl()) ? "true" : "false";
    json += ",\"mqttConfigured\":";
    json += mqttCfg ? "true" : "false";
    json += ",\"mqttConnected\":";
    json += (mqttCfg && mqttService->isConnected()) ? "true" : "false";
    json += "},";

    // emergency freq (the only /emergency-settings field settings.html uses)
    json += "\"emergencyNotifFreq_ms\":" + String(getEmergencyNotifFreq()) + ",";

    // wifi (mirrors fields settings.html reads from /status)
    bool connected = WiFiManager::getInstance().isConnected();
    json += "\"wifi\":{";
    json += "\"connected\":" + String(connected ? "true" : "false") + ",";
    json += "\"ssid\":\"" + WiFi.SSID() + "\"";
    json += "},";

    // calibration (only the hasTwoPointCalibration flag is used)
    json += "\"hasTwoPointCalibration\":";
    json += (waterSensor && waterSensor->hasTwoPointCalibration()) ? "true" : "false";

    json += "}";
    server->send(200, "application/json", json);
    serverStartTime = millis();
}

void ConfigServer::handleDebugInit() {
    String json = "{\"reading\":";

    if (!waterSensor) {
        json += "{\"sensorAvailable\":false},\"calibration\":null}";
        server->send(200, "application/json", json);
        serverStartTime = millis();
        return;
    }

    SensorReading reading = waterSensor->readLevel();
    json += "{\"sensorAvailable\":true,";
    json += "\"valid\":" + String(reading.valid ? "true" : "false") + ",";
    json += "\"millivolts\":" + String(reading.millivolts, 2);
    if (reading.valid) {
        json += ",\"level_cm\":" + String(reading.level_cm, 2);
    }
    json += "},\"calibration\":{";
    json += "\"zeroPoint_mv\":" + String(waterSensor->getZeroPointMilliVolts()) + ",";
    json += "\"hasTwoPointCalibration\":";
    json += waterSensor->hasTwoPointCalibration() ? "true" : "false";
    if (waterSensor->hasTwoPointCalibration()) {
        json += ",\"secondPoint_mv\":" + String(waterSensor->getSecondPointMilliVolts());
        json += ",\"secondPoint_cm\":" + String(waterSensor->getSecondPointLevelCm(), 2);
    }
    json += "}}";

    server->send(200, "application/json", json);
    serverStartTime = millis();
}

void ConfigServer::handleCaptivePortalProbe() {
    // Tiny 302 → triggers OS captive-portal popup and frees the connection slot.
    // Apple, Android, Windows, ChromeOS all treat any 3xx on their probe URL as
    // "portal present" and open a mini-browser to the Location target.
    server->sendHeader("Location", "http://192.168.4.1/", true);
    server->sendHeader("Cache-Control", "no-store");
    server->send(302, "text/plain", "");
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

void ConfigServer::handleWiFiNetworks() {
    std::vector<String> ssids = WiFiManager::getInstance().getStoredSSIDs();
    String json = "[";
    for (int i = 0; i < (int)ssids.size(); i++) {
        if (i > 0) json += ",";
        json += "\"" + ssids[i] + "\"";
    }
    json += "]";
    server->send(200, "application/json", json);
    serverStartTime = millis();
}

void ConfigServer::handleWiFiRemove() {
    if (!server->hasArg("ssid") || server->arg("ssid").isEmpty()) {
        server->send(400, "application/json", "{\"success\":false,\"message\":\"Missing ssid\"}");
        return;
    }
    String ssid = server->arg("ssid");
    WiFiManager::getInstance().removeNetwork(ssid.c_str());
    server->send(200, "application/json", "{\"success\":true}");
    serverStartTime = millis();
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
        if (level_cm >= getUrgentEmergencyWaterLevel()) {
            String errorMsg = "{\"error\":\"Tier 1 threshold must be less than Tier 2 threshold (";
            errorMsg += String(getUrgentEmergencyWaterLevel(), 2) + " cm)\"}";
            server->send(400, "application/json", errorMsg);
            return;
        }

        if (settingsStore) {
            SettingsValues v = settingsStore->get();
            v.emergencyWaterLevel_cm = level_cm;
            settingsStore->save(v);
        }
        
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
        
        if (settingsStore) {
            SettingsValues v = settingsStore->get();
            v.emergencyNotifFreq_ms = freq_ms;
            settingsStore->save(v);
        }
        
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
        if (level_cm <= getEmergencyWaterLevel()) {
            String errorMsg = "{\"error\":\"Tier 2 threshold must be greater than Tier 1 threshold (";
            errorMsg += String(getEmergencyWaterLevel(), 2) + " cm)\"}";
            server->send(400, "application/json", errorMsg);
            return;
        }

        if (settingsStore) {
            SettingsValues v = settingsStore->get();
            v.urgentEmergencyWaterLevel_cm = level_cm;
            settingsStore->save(v);
        }
        
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

// loadEmergencySettings() and saveEmergencySettings() removed — emergency
// settings are now owned by SettingsStore (see include/SettingsStore.h).
// ConfigServer reads via get*() getters and writes via settingsStore->save().

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

    // MQTT broker
    json += ",\"mqttConfigured\":";
    if (mqttService && mqttService->hasBrokerConfig()) {
        char hostBuf[64];
        uint16_t port = 1883;
        char userBuf[32];
        char topicBuf[64];
        mqttService->getBroker(hostBuf, sizeof(hostBuf), &port);
        mqttService->getUsername(userBuf, sizeof(userBuf));
        mqttService->getBaseTopic(topicBuf, sizeof(topicBuf));
        json += "true";
        json += ",\"mqttConnected\":" + String(mqttService->isConnected() ? "true" : "false");
        json += ",\"mqttHost\":\"" + String(hostBuf) + "\"";
        json += ",\"mqttPort\":" + String(port);
        json += ",\"mqttUser\":\"" + String(userBuf) + "\"";
        json += ",\"mqttBaseTopic\":\"" + String(topicBuf) + "\"";
        json += ",\"mqttTls\":" + String(mqttService->getTls() ? "true" : "false");
    } else {
        json += "false";
        json += ",\"mqttConnected\":false";
    }

    json += "}";
    server->send(200, "application/json", json);
}

void ConfigServer::handleNotificationsStatus() {
    serverStartTime = millis();

    // Lean, secret-free status for periodic polling (e.g. the live MQTT pill).
    // Booleans only — no host/phone/webhook values — so it stays cheap to fetch.
    bool hasPhone   = smsService && smsService->hasPhoneNumber();
    bool hasWebhook = discordService && discordService->hasWebhookUrl();
    bool mqttCfg    = mqttService && mqttService->hasBrokerConfig();
    bool mqttConn   = mqttCfg && mqttService->isConnected();

    String json = "{";
    json += "\"hasPhoneNumber\":";    json += hasPhone   ? "true" : "false";
    json += ",\"hasDiscordWebhook\":"; json += hasWebhook ? "true" : "false";
    json += ",\"mqttConfigured\":";    json += mqttCfg    ? "true" : "false";
    json += ",\"mqttConnected\":";     json += mqttConn   ? "true" : "false";
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
    bool success = smsService->send("BilgeRise Test: This is a test message from your ESP32 boat monitor.");
    
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
    bool success = discordService->send("🚤 **BilgeRise Test** - This is a test message from your ESP32 boat monitor.");
    
    if (success) {
        LOG_INFO("[TEST] Test Discord message sent successfully!");
        server->send(200, "application/json", "{\"success\":true,\"message\":\"Test Discord message sent successfully!\"}");
    } else {
        LOG_INFO("[TEST] Test Discord message failed to send");
        server->send(500, "application/json", "{\"success\":false,\"error\":\"Failed to send test Discord message. Check serial log for details.\"}");
    }
}

void ConfigServer::handleSetMqttConfig() {
    serverStartTime = millis();

    if (!mqttService) {
        server->send(503, "application/json", "{\"error\":\"MQTT service not available\"}");
        return;
    }

    bool changed = false;

    if (server->hasArg("host") && server->hasArg("port")) {
        String host = server->arg("host");
        uint16_t port = (uint16_t)server->arg("port").toInt();
        if (port == 0) port = 1883;
        mqttService->updateBroker(host.c_str(), port);
        LOG_INFO("[CONFIG] MQTT broker updated: %s:%d", host.c_str(), port);
        changed = true;
    }

    if (server->hasArg("user") || server->hasArg("pass")) {
        String user = server->arg("user");
        String pass = server->arg("pass");
        mqttService->updateCredentials(user.c_str(), pass.c_str());
        LOG_INFO("[CONFIG] MQTT credentials updated");
        changed = true;
    }

    if (server->hasArg("topic")) {
        String topic = server->arg("topic");
        if (topic.length() > 0) {
            mqttService->updateBaseTopic(topic.c_str());
            LOG_INFO("[CONFIG] MQTT base topic updated: %s", topic.c_str());
            changed = true;
        }
    }

    if (server->hasArg("tls")) {
        String tlsArg = server->arg("tls");
        bool tls = (tlsArg == "1" || tlsArg == "true" || tlsArg == "on");
        mqttService->updateTls(tls);
        LOG_INFO("[CONFIG] MQTT TLS %s", tls ? "enabled" : "disabled");
        changed = true;
    }

    if (changed) {
        mqttService->reloadConfig();
        server->send(200, "application/json", "{\"success\":true,\"message\":\"MQTT configuration updated\"}");
    } else {
        server->send(400, "application/json", "{\"error\":\"No valid parameters provided\"}");
    }
}

void ConfigServer::handleTestMqtt() {
    serverStartTime = millis();

    if (!mqttService) {
        server->send(503, "application/json", "{\"error\":\"MQTT service not available\"}");
        return;
    }

    if (!mqttService->hasBrokerConfig()) {
        server->send(400, "application/json", "{\"error\":\"No MQTT broker configured. Please save broker settings first.\"}");
        return;
    }

    if (!WiFi.isConnected()) {
        server->send(503, "application/json", "{\"error\":\"WiFi not connected. Cannot reach MQTT broker.\"}");
        return;
    }

    if (!mqttService->isConnected()) {
        server->send(503, "application/json", "{\"error\":\"MQTT broker not connected. Check host/port and broker status.\"}");
        return;
    }

    char testTopic[96];
    char topicBuf[64];
    mqttService->getBaseTopic(topicBuf, sizeof(topicBuf));
    snprintf(testTopic, sizeof(testTopic), "%s/test", topicBuf);

    LOG_INFO("[TEST] Publishing test MQTT message to %s ...", testTopic);
    bool success = mqttService->publish(testTopic, "BilgeRise Test: This is a test message from your ESP32 boat monitor.", false);

    if (success) {
        LOG_INFO("[TEST] Test MQTT message published successfully!");
        server->send(200, "application/json", "{\"success\":true,\"message\":\"Test MQTT message published successfully!\"}");
    } else {
        LOG_INFO("[TEST] Test MQTT message failed to publish");
        server->send(500, "application/json", "{\"success\":false,\"error\":\"Failed to publish test MQTT message.\"}");
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
    float rate = waterSensor->getRateOfChange_cm30min();
    if (!isnan(rate)) {
        json += ",\"rate_cm_30min\":" + String(rate, 2);
    }
    json += "}";

    server->send(200, "application/json", json);
}

void ConfigServer::handleDebug() {
    sendCachedPage((const char*)DEBUG_HTML_GZ, DEBUG_HTML_GZ_LEN, "text/html");
}

// ============================================================================
// OTA UPDATE HANDLERS
// ============================================================================

void ConfigServer::handleOTAPage() {
    sendCachedPage((const char*)OTA_HTML_GZ, OTA_HTML_GZ_LEN, "text/html");
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


