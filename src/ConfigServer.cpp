#ifndef UNIT_TESTING

#include "ConfigServer.h"
#include "BoardPins.h"   // ALERT_PIN (handleTestEmergencyPin)
#include "JsonResponder.h"
#include "Logger.h"
#include "Version.h"
#include "compressed_pages.h"

// ============================================================================
// NETWORK STACK INSTRUMENTATION (dev build only)
//
// Logs per-request wall-clock time and free-heap delta for the hot-path
// handlers (static pages + polled JSON endpoints) so the WebServer/AP-mode
// network stack can be profiled without a debugger attached. Compiled out
// entirely under PRODUCTION_BUILD — not just silenced via LOG_DEBUG — since
// micros()/ESP.getFreeHeap() run on every request and PRODUCTION_BUILD
// firmware shouldn't pay for that on a path this hot.
// ============================================================================
#ifndef PRODUCTION_BUILD
class ScopedRequestProfiler {
public:
    explicit ScopedRequestProfiler(const char* label)
        : label_(label), startUs_(micros()), startHeap_(ESP.getFreeHeap()) {}
    ~ScopedRequestProfiler() {
        uint32_t elapsedUs = micros() - startUs_;
        uint32_t endHeap = ESP.getFreeHeap();
        LOG_DEBUG("[PERF] %-18s %6lu us  heap %u -> %u (%ld)",
                  label_, (unsigned long)elapsedUs, startHeap_, endHeap,
                  (long)endHeap - (long)startHeap_);
    }
private:
    const char* label_;
    uint32_t startUs_;
    uint32_t startHeap_;
};
#define PROFILE_REQUEST(label) ScopedRequestProfiler _reqProfiler(label)
#else
#define PROFILE_REQUEST(label) ((void)0)
#endif

// ============================================================================
// CORE LIFECYCLE METHODS
// ============================================================================

ConfigServer::ConfigServer(WaterPressureSensor* sensor,
                           SmsChannel*          sms,
                           DiscordChannel*      discord,
                           CustomChannel*       custom,
                           OTAManager*          ota,
                           MQTTService*         mqtt,
                           SettingsStore*       settings)
    : server(nullptr), waterSensor(sensor), smsService(sms), discordService(discord),
      customService(custom), otaManager(ota), mqttService(mqtt), settingsStore(settings) {
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
        float el  = settingsStore ? settingsStore->getEmergencyWaterLevel()       : SETTINGS_DEFAULTS().emergencyWaterLevel_cm;
        int   ef  = settingsStore ? settingsStore->getEmergencyNotifFreq()        : SETTINGS_DEFAULTS().emergencyNotifFreq_ms;
        float ul  = settingsStore ? settingsStore->getUrgentEmergencyWaterLevel() : SETTINGS_DEFAULTS().urgentEmergencyWaterLevel_cm;
        JsonResponder().num("emergencyWaterLevel_cm", el, 2)
                       .num("emergencyNotifFreq_ms", ef)
                       .num("urgentEmergencyWaterLevel_cm", ul, 2)
                       .send(server);
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

    // Route: POST /notifications/twilio → set Twilio account SID / auth token / messaging service SID
    server->on("/notifications/twilio", HTTP_POST, [this]() { handleSetTwilioCreds(); });
    
    // Route: POST /notifications/discord → set Discord webhook URL
    server->on("/notifications/discord", HTTP_POST, [this]() { handleSetDiscordWebhook(); });
    
    // Route: POST /notifications/test/sms → send test SMS
    server->on("/notifications/test/sms", HTTP_POST, [this]() { handleTestSMS(); });
    
    // Route: POST /notifications/test/discord → send test Discord message
    server->on("/notifications/test/discord", HTTP_POST, [this]() { handleTestDiscord(); });

    // Route: POST /notifications/custom → configure custom HTTP channel
    server->on("/notifications/custom", HTTP_POST, [this]() { handleSetCustomChannel(); });

    // Route: POST /notifications/test/custom → send test custom channel message
    server->on("/notifications/test/custom", HTTP_POST, [this]() { handleTestCustom(); });

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

    // Ask WiFiManager to reconnect on the next maintainConnection() call
    // (which runs at the top of the next loop() iteration). We intentionally
    // do NOT call connectToBestNetwork() synchronously here because:
    //   - WiFi.mode(WIFI_STA) just tore down the AP and switched radio mode,
    //     which takes 1-2s of the 10s task-watchdog budget.
    //   - connectToBestNetwork() then calls WiFi.scanNetworks(), a blocking
    //     call that can take 5-10s+ on a crowded 2.4GHz band.
    //   - The combined blocking exceeds WDT_TIMEOUT_S (10s) and triggers a
    //     panic reboot mid-scan.
    // requestImmediateReconnect() resets the throttle so maintainConnection()
    // attempts a reconnect immediately on the next loop tick, with the radio
    // already settled in STA mode.
    WiFiManager::getInstance().requestImmediateReconnect();
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
#ifndef PRODUCTION_BUILD
    uint32_t dispatchStartUs = micros();
    server->handleClient();
    uint32_t dispatchUs = micros() - dispatchStartUs;
    // Idle polls with no client return in a handful of microseconds; only log
    // when a real request was actually read/parsed/dispatched/written, so this
    // captures pure socket+routing overhead (handler body time is logged
    // separately by PROFILE_REQUEST and is a subset of this number).
    if (dispatchUs > 1000) {
        LOG_DEBUG("[PERF] %-18s %6lu us", "handleClient()", (unsigned long)dispatchUs);
    }
#else
    server->handleClient();
#endif

    // Handle server timeout
    if ((millis() - serverStartTime) >= SERVER_TIMEOUT_MS) {
        LOG_INFO("Setup mode timed out after %lu ms with no client activity", (unsigned long)SERVER_TIMEOUT_MS);
        stopSetupMode();
    }   
}

// ============================================================================
// WIFI CONFIGURATION HANDLERS
// ============================================================================

void ConfigServer::sendCachedPage(const char* data, size_t len, const char* contentType) {
    serverStartTime = millis();
    // ETag includes the build timestamp so that flashing new firmware (even at
    // the same FIRMWARE_VERSION) busts the browser cache. Without this, a reflash
    // at the same version would get a 304 and the browser would serve stale HTML.
    String etag = String(FIRMWARE_VERSION) + "-" + BUILD_TIMESTAMP;
    if (server->hasHeader("If-None-Match") && server->header("If-None-Match") == etag) {
        server->send(304);
        return;
    }
    server->sendHeader("Cache-Control", "max-age=86400, must-revalidate");
    server->sendHeader("ETag", etag);
    server->sendHeader("Content-Encoding", "gzip");
    server->send_P(200, contentType, data, len);
}

void ConfigServer::handleRoot() {
    PROFILE_REQUEST("GET /");
    sendCachedPage((const char*)INDEX_HTML_GZ, INDEX_HTML_GZ_LEN, "text/html");
}

void ConfigServer::handleWiFiConfig() {
    PROFILE_REQUEST("GET /wifi-config");
    sendCachedPage((const char*)WIFI_CONFIG_HTML_GZ, WIFI_CONFIG_HTML_GZ_LEN, "text/html");
}

void ConfigServer::handleNotificationsPage() {
    PROFILE_REQUEST("GET /notifications");
    sendCachedPage((const char*)NOTIFICATIONS_HTML_GZ, NOTIFICATIONS_HTML_GZ_LEN, "text/html");
}

void ConfigServer::handleSettings() {
    PROFILE_REQUEST("GET /settings");
    sendCachedPage((const char*)SETTINGS_HTML_GZ, SETTINGS_HTML_GZ_LEN, "text/html");
}

void ConfigServer::handleInit() {
    PROFILE_REQUEST("GET /init");
    JsonResponder r(512);

    // Nested objects are assembled as strings and attached raw (JsonResponder is flat-only).
    bool connected = WiFiManager::getInstance().isConnected();
    JsonResponder wifiR;
    wifiR.boolean("connected", connected)
        .str("ssid", WiFi.SSID())
        .str("ip", WiFi.localIP().toString())
        .num("rssi", (int)WiFi.RSSI());
    r.raw("wifi", wifiR.body().c_str());

    String sensorObj = "{";
    if (!waterSensor) {
        sensorObj += "\"sensorAvailable\":false";
    } else {
        SensorReading reading = waterSensor->readLevel();
        sensorObj += "\"sensorAvailable\":true,";
        sensorObj += "\"valid\":" + String(reading.valid ? "true" : "false");
        if (reading.valid) {
            sensorObj += ",\"level_cm\":" + String(reading.level_cm, 2);
        }
        float rate = waterSensor->getRateOfChange_cm30min();
        if (!isnan(rate)) {
            sensorObj += ",\"rate_cm_30min\":" + String(rate, 2);
        }
    }
    sensorObj += "}";
    r.raw("sensor", sensorObj.c_str());

    String thresholdsObj = "{";
    thresholdsObj += "\"emergencyWaterLevel_cm\":" + String(getEmergencyWaterLevel(), 2) + ",";
    thresholdsObj += "\"urgentEmergencyWaterLevel_cm\":" + String(getUrgentEmergencyWaterLevel(), 2);
    thresholdsObj += "}";
    r.raw("thresholds", thresholdsObj.c_str());

    r.send(server);
    serverStartTime = millis();
}

void ConfigServer::handleSettingsInit() {
    PROFILE_REQUEST("GET /settings/init");
    JsonResponder r(384);

    // notifications block (mirrors fields settings.html reads from /notifications)
    bool mqttCfg = mqttService && mqttService->hasBrokerConfig();
    String notifObj = "{";
    notifObj += "\"hasPhoneNumber\":";
    notifObj += (smsService && smsService->hasPhoneNumber()) ? "true" : "false";
    notifObj += ",\"hasDiscordWebhook\":";
    notifObj += (discordService && discordService->hasWebhookUrl()) ? "true" : "false";
    notifObj += ",\"mqttConfigured\":";
    notifObj += mqttCfg ? "true" : "false";
    notifObj += ",\"mqttConnected\":";
    notifObj += (mqttCfg && mqttService->isConnected()) ? "true" : "false";
    notifObj += "}";
    r.raw("notifications", notifObj.c_str());

    // emergency freq (the only /emergency-settings field settings.html uses)
    r.num("emergencyNotifFreq_ms", getEmergencyNotifFreq());

    // wifi (mirrors fields settings.html reads from /status)
    bool connected = WiFiManager::getInstance().isConnected();
    JsonResponder wifiR;
    wifiR.boolean("connected", connected).str("ssid", WiFi.SSID());
    r.raw("wifi", wifiR.body().c_str());

    // calibration (only the hasTwoPointCalibration flag is used)
    r.boolean("hasTwoPointCalibration", waterSensor && waterSensor->hasTwoPointCalibration());

    r.send(server);
    serverStartTime = millis();
}

void ConfigServer::handleDebugInit() {
    PROFILE_REQUEST("GET /debug/init");

    if (!waterSensor) {
        JsonResponder().raw("reading", "{\"sensorAvailable\":false}")
                       .raw("calibration", "null")
                       .send(server);
        serverStartTime = millis();
        return;
    }

    SensorReading reading = waterSensor->readLevel();
    String readingObj = "{";
    readingObj += "\"sensorAvailable\":true,";
    readingObj += "\"valid\":" + String(reading.valid ? "true" : "false") + ",";
    readingObj += "\"millivolts\":" + String(reading.millivolts, 2);
    if (reading.valid) {
        readingObj += ",\"level_cm\":" + String(reading.level_cm, 2);
    }
    readingObj += "}";

    String calObj = "{";
    calObj += "\"zeroPoint_mv\":" + String(waterSensor->getZeroPointMilliVolts()) + ",";
    calObj += "\"hasTwoPointCalibration\":";
    calObj += waterSensor->hasTwoPointCalibration() ? "true" : "false";
    if (waterSensor->hasTwoPointCalibration()) {
        calObj += ",\"secondPoint_mv\":" + String(waterSensor->getSecondPointMilliVolts());
        calObj += ",\"secondPoint_cm\":" + String(waterSensor->getSecondPointLevelCm(), 2);
    }
    calObj += "}";

    JsonResponder().raw("reading", readingObj.c_str())
                   .raw("calibration", calObj.c_str())
                   .send(server);
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
    PROFILE_REQUEST("GET /status");
    // Return connection status as JSON
    JsonResponder().boolean("connected", WiFiManager::getInstance().isConnected())
                   .str("ssid", WiFi.SSID())
                   .str("ip", WiFi.localIP().toString())
                   .num("rssi", (int)WiFi.RSSI())
                   .send(server);
}

void ConfigServer::handleWiFiNetworks() {
    std::vector<String> ssids = WiFiManager::getInstance().getStoredSSIDs();
    // Response is a bare JSON array (["ssid1",...]) — the dev-ui mock server
    // (res.json(storedNetworks)) and any client expecting a list depend on
    // that shape, so it is intentionally NOT wrapped in an object.
    String networksArr = "[";
    for (int i = 0; i < (int)ssids.size(); i++) {
        if (i > 0) networksArr += ",";
        networksArr += "\"" + ssids[i] + "\"";
    }
    networksArr += "]";
    server->send(200, "application/json", networksArr);
    serverStartTime = millis();
}

void ConfigServer::handleWiFiRemove() {
    if (!server->hasArg("ssid") || server->arg("ssid").isEmpty()) {
        JsonResponder().boolean("success", false).str("message", "Missing ssid").send(server, 400);
        return;
    }
    String ssid = server->arg("ssid");
    WiFiManager::getInstance().removeNetwork(ssid.c_str());
    JsonResponder().boolean("success", true).send(server);
    serverStartTime = millis();
}


// ============================================================================
// SENSOR CALIBRATION HANDLERS
// ============================================================================

void ConfigServer::handleCalibrateZero() {
    serverStartTime = millis();
    
    if (!waterSensor) {
        JsonResponder::sendError(server, 503, "Sensor not available");
        return;
    }
    
    if (server->hasArg("millivolts")) {
        int millivolts = server->arg("millivolts").toInt();
        float level_cm = server->hasArg("level_cm") ? server->arg("level_cm").toFloat() : 0.0f;
        
        waterSensor->setCalibrationPoint(0, millivolts, level_cm);
        saveCalibration();
        
        JsonResponder::success("Zero point calibrated")
            .num("millivolts", millivolts)
            .num("level_cm", level_cm, 2)
            .send(server);
    } else {
        JsonResponder::sendError(server, 400, "Missing millivolts parameter");
    }
}

void ConfigServer::handleCalibratePoint2() {
    serverStartTime = millis();
    
    if (!waterSensor) {
        JsonResponder::sendError(server, 503, "Sensor not available");
        return;
    }
    
    if (server->hasArg("millivolts") && server->hasArg("level_cm")) {
        int millivolts = server->arg("millivolts").toInt();
        float level_cm = server->arg("level_cm").toFloat();
        
        waterSensor->setCalibrationPoint(1, millivolts, level_cm);
        saveCalibration();
        
        JsonResponder::success("Second calibration point set")
            .num("millivolts", millivolts)
            .num("level_cm", level_cm, 2)
            .send(server);
    } else {
        JsonResponder::sendError(server, 400, "Missing millivolts or level_cm parameter");
    }
}

void ConfigServer::handleGetCalibration() {
    serverStartTime = millis();
    
    if (!waterSensor) {
        JsonResponder::sendError(server, 503, "Sensor not available");
        return;
    }
    
    JsonResponder r;
    r.num("zeroPoint_mv", waterSensor->getZeroPointMilliVolts());
    r.boolean("hasTwoPointCalibration", waterSensor->hasTwoPointCalibration());
    
    if (waterSensor->hasTwoPointCalibration()) {
        r.num("secondPoint_mv", waterSensor->getSecondPointMilliVolts());
        r.num("secondPoint_cm", waterSensor->getSecondPointLevelCm(), 2);
    }
    
    r.send(server);
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

// Shared validation for the two water-level threshold handlers. On failure it
// sends the 400 response itself (message text identical to the pre-consolidation
// handlers) and returns false.
bool ConfigServer::parseAndValidateLevel(float level_cm, bool isTier1) {
    // Validate the input against sensor usable range
    if (level_cm < MIN_EMERGENCY_WATER_LEVEL_CM || level_cm > MAX_EMERGENCY_WATER_LEVEL_CM) {
        String errorMsg = "Invalid level. Must be between ";
        errorMsg += String(MIN_EMERGENCY_WATER_LEVEL_CM, 1) + " and ";
        errorMsg += String(MAX_EMERGENCY_WATER_LEVEL_CM, 1) + " cm";
        JsonResponder::sendError(server, 400, errorMsg);
        return false;
    }
    
    // Validate the threshold against the other tier
    if (isTier1) {
        // Validate that Tier 1 threshold is less than Tier 2 threshold
        if (level_cm >= getUrgentEmergencyWaterLevel()) {
            String errorMsg = "Tier 1 threshold must be less than Tier 2 threshold (";
            errorMsg += String(getUrgentEmergencyWaterLevel(), 2) + " cm)";
            JsonResponder::sendError(server, 400, errorMsg);
            return false;
        }
    } else {
        // Validate that Tier 2 threshold is greater than Tier 1 threshold
        if (level_cm <= getEmergencyWaterLevel()) {
            String errorMsg = "Tier 2 threshold must be greater than Tier 1 threshold (";
            errorMsg += String(getEmergencyWaterLevel(), 2) + " cm)";
            JsonResponder::sendError(server, 400, errorMsg);
            return false;
        }
    }
    return true;
}

void ConfigServer::handleSetEmergencyLevel() {
    serverStartTime = millis();
    
    if (server->hasArg("level_cm")) {
        float level_cm = server->arg("level_cm").toFloat();
        if (!parseAndValidateLevel(level_cm, true)) return;

        if (settingsStore) {
            SettingsValues v = settingsStore->get();
            v.emergencyWaterLevel_cm = level_cm;
            settingsStore->save(v);
        }
        
        JsonResponder::success("Emergency water level (Tier 1) updated")
            .num("level_cm", level_cm, 2)
            .send(server);
        LOG_INFO("[CONFIG] Emergency water level (Tier 1) updated: %.2f cm", level_cm);
    } else {
        JsonResponder::sendError(server, 400, "Missing level_cm parameter");
    }
}

void ConfigServer::handleSetEmergencyNotifFreq() {
    serverStartTime = millis();
    
    if (server->hasArg("freq_ms")) {
        int freq_ms = server->arg("freq_ms").toInt();
        
        // Validate the input
        if (freq_ms < MIN_EMERGENCY_NOTIF_FREQ_MS || freq_ms > MAX_EMERGENCY_NOTIF_FREQ_MS) {
            String errorMsg = "Invalid frequency. Must be between ";
            errorMsg += String(MIN_EMERGENCY_NOTIF_FREQ_MS) + "ms (" + String(MIN_EMERGENCY_NOTIF_FREQ_MS / 1000) + "s) and ";
            errorMsg += String(MAX_EMERGENCY_NOTIF_FREQ_MS) + "ms (" + String(MAX_EMERGENCY_NOTIF_FREQ_MS / 1000) + "s)";
            JsonResponder::sendError(server, 400, errorMsg);
            return;
        }
        
        if (settingsStore) {
            SettingsValues v = settingsStore->get();
            v.emergencyNotifFreq_ms = freq_ms;
            settingsStore->save(v);
        }
        
        JsonResponder::success("Emergency notification frequency updated")
            .num("freq_ms", freq_ms)
            .num("freq_seconds", freq_ms / 1000)
            .send(server);
        LOG_INFO("[CONFIG] Emergency notification frequency updated: %d ms (%d seconds)", freq_ms, freq_ms / 1000);
    } else {
        JsonResponder::sendError(server, 400, "Missing freq_ms parameter");
    }
}

void ConfigServer::handleSetUrgentEmergencyLevel() {
    serverStartTime = millis();
    
    if (server->hasArg("level_cm")) {
        float level_cm = server->arg("level_cm").toFloat();
        if (!parseAndValidateLevel(level_cm, false)) return;

        if (settingsStore) {
            SettingsValues v = settingsStore->get();
            v.urgentEmergencyWaterLevel_cm = level_cm;
            settingsStore->save(v);
        }
        
        JsonResponder::success("Urgent emergency water level (Tier 2) updated")
            .num("level_cm", level_cm, 2)
            .send(server);
        LOG_INFO("[CONFIG] Urgent emergency water level (Tier 2) updated: %.2f cm", level_cm);
    } else {
        JsonResponder::sendError(server, 400, "Missing level_cm parameter");
    }
}

void ConfigServer::handleTestEmergencyPin() {
    serverStartTime = millis();
    
    LOG_INFO("[TEST] Testing emergency pin output...");
    
    // Set the pin HIGH for 2 seconds to test the connected device.
    // ALERT_PIN comes from include/BoardPins.h (shared with main.cpp).
    digitalWrite(ALERT_PIN, HIGH);
    LOG_INFO("[TEST] Emergency pin set HIGH");
    
    delay(2000); // 2 second test pulse
    
    digitalWrite(ALERT_PIN, LOW);
    LOG_INFO("[TEST] Emergency pin set LOW - test complete");
    
    JsonResponder::success("Emergency pin test completed (2 second pulse)").send(server);
}

// loadEmergencySettings() and saveEmergencySettings() removed — emergency
// settings are now owned by SettingsStore (see include/SettingsStore.h).
// ConfigServer reads via get*() getters and writes via settingsStore->save().

// ============================================================================
// NOTIFICATION SETTINGS HANDLERS
// ============================================================================

void ConfigServer::handleGetNotifications() {
    serverStartTime = millis();

    JsonResponder r(1024);

    // SMS phone number
    if (smsService && smsService->hasPhoneNumber()) {
        char phoneBuf[32];
        if (smsService->getPhoneNumber(phoneBuf, sizeof(phoneBuf)) == 0) {
            r.boolean("hasPhoneNumber", true);
            r.str("phoneNumber", phoneBuf);
        } else {
            r.boolean("hasPhoneNumber", false);
        }
    } else {
        r.boolean("hasPhoneNumber", false);
    }

    // SMS: whether Twilio API credentials are configured (no secret values returned)
    r.boolean("hasTwilioCreds", smsService && smsService->isConfigured());

    // Discord webhook
    if (discordService && discordService->hasWebhookUrl()) {
        char webhookBuf[256];
        if (discordService->getWebhookUrl(webhookBuf, sizeof(webhookBuf)) == 0) {
            r.boolean("hasDiscordWebhook", true);
            r.str("discordWebhook", webhookBuf);
        } else {
            r.boolean("hasDiscordWebhook", false);
        }
    } else {
        r.boolean("hasDiscordWebhook", false);
    }

    // Custom HTTP channel
    if (customService && customService->isConfigured()) {
        char epBuf[CUSTOM_ENDPOINT_MAX];
        char ctBuf[CUSTOM_CTYPE_MAX];
        char authBuf[CUSTOM_AUTH_MAX];
        char tmplBuf[CUSTOM_TMPL_MAX];
        customService->getConfig(epBuf, sizeof(epBuf), ctBuf, sizeof(ctBuf),
                                 authBuf, sizeof(authBuf), tmplBuf, sizeof(tmplBuf));
        r.boolean("hasCustomChannel", true);
        r.str("customEndpoint", epBuf);
        r.str("customCtype", ctBuf);
        r.str("customAuth", authBuf);
        r.str("customTmpl", tmplBuf);
    } else {
        r.boolean("hasCustomChannel", false);
    }

    // MQTT broker
    if (mqttService && mqttService->hasBrokerConfig()) {
        char hostBuf[64];
        uint16_t port = 1883;
        char userBuf[32];
        char topicBuf[64];
        mqttService->getBroker(hostBuf, sizeof(hostBuf), &port);
        mqttService->getUsername(userBuf, sizeof(userBuf));
        mqttService->getBaseTopic(topicBuf, sizeof(topicBuf));
        r.boolean("mqttConfigured", true);
        r.boolean("mqttConnected", mqttService->isConnected());
        r.str("mqttHost", hostBuf);
        r.num("mqttPort", (int)port);
        r.str("mqttUser", userBuf);
        r.str("mqttBaseTopic", topicBuf);
        r.boolean("mqttTls", mqttService->getTls());
    } else {
        r.boolean("mqttConfigured", false);
        r.boolean("mqttConnected", false);
    }

    r.send(server);
}

void ConfigServer::handleNotificationsStatus() {
    serverStartTime = millis();

    // Lean, secret-free status for periodic polling (e.g. the live MQTT pill).
    // Booleans only — no host/phone/webhook values — so it stays cheap to fetch.
    bool hasPhone   = smsService     && smsService->hasPhoneNumber();
    bool hasWebhook = discordService && discordService->hasWebhookUrl();
    bool hasCustom  = customService  && customService->isConfigured();
    bool mqttCfg    = mqttService && mqttService->hasBrokerConfig();
    bool mqttConn   = mqttCfg && mqttService->isConnected();

    JsonResponder().boolean("hasPhoneNumber", hasPhone)
                   .boolean("hasDiscordWebhook", hasWebhook)
                   .boolean("hasCustomChannel", hasCustom)
                   .boolean("mqttConfigured", mqttCfg)
                   .boolean("mqttConnected", mqttConn)
                   .send(server);
}

void ConfigServer::handleSetPhoneNumber() {
    serverStartTime = millis();
    
    if (!smsService) {
        JsonResponder::sendError(server, 503, "SMS service not available");
        return;
    }
    
    if (server->hasArg("phone")) {
        String phone = server->arg("phone");
        smsService->updatePhoneNumber(phone.c_str());
        
        JsonResponder::success("Phone number updated").str("phoneNumber", phone).send(server);
        LOG_INFO("[CONFIG] Phone number updated: %s", phone.c_str());
    } else {
        JsonResponder::sendError(server, 400, "Missing phone parameter");
    }
}

void ConfigServer::handleSetDiscordWebhook() {
    serverStartTime = millis();
    
    if (!discordService) {
        JsonResponder::sendError(server, 503, "Discord service not available");
        return;
    }
    
    if (server->hasArg("webhook")) {
        String webhook = server->arg("webhook");
        discordService->updateWebhookUrl(webhook.c_str());

        JsonResponder::success("Discord webhook updated").send(server);
        LOG_INFO("[CONFIG] Discord webhook updated");
    } else {
        JsonResponder::sendError(server, 400, "Missing webhook parameter");
    }
}

void ConfigServer::handleSetTwilioCreds() {
    serverStartTime = millis();

    if (!smsService) {
        JsonResponder::sendError(server, 503, "SMS service not available");
        return;
    }

    String sid    = server->arg("sid");
    String token  = server->arg("token");
    String svcSid = server->arg("svc_sid");

    if (sid.isEmpty() && token.isEmpty() && svcSid.isEmpty()) {
        JsonResponder::sendError(server, 400, "No Twilio credentials provided");
        return;
    }

    smsService->updateTwilioCreds(
        sid.isEmpty()    ? nullptr : sid.c_str(),
        token.isEmpty()  ? nullptr : token.c_str(),
        svcSid.isEmpty() ? nullptr : svcSid.c_str()
    );

    JsonResponder::success("Twilio credentials updated").send(server);
    LOG_INFO("[CONFIG] Twilio credentials updated");
}

void ConfigServer::handleSetCustomChannel() {
    serverStartTime = millis();

    if (!customService) {
        JsonResponder::sendError(server, 503, "Custom channel not available");
        return;
    }

    // Require at minimum an endpoint and a body template
    if (!server->hasArg("endpoint") || server->arg("endpoint").isEmpty()) {
        JsonResponder::sendError(server, 400, "Missing endpoint parameter");
        return;
    }
    if (!server->hasArg("tmpl") || server->arg("tmpl").isEmpty()) {
        JsonResponder::sendError(server, 400, "Missing tmpl (body template) parameter");
        return;
    }

    String endpoint = server->arg("endpoint");
    String ctype    = server->hasArg("ctype")    ? server->arg("ctype")    : "application/json";
    String auth     = server->hasArg("auth")     ? server->arg("auth")     : "none";
    String user     = server->hasArg("user")     ? server->arg("user")     : "";
    String secret   = server->hasArg("secret")   ? server->arg("secret")   : "";
    String tmpl     = server->arg("tmpl");

    // Sanitise auth type to one of the three accepted values
    if (auth != "basic" && auth != "bearer") auth = "none";

    customService->updateConfig(
        endpoint.c_str(),
        ctype.c_str(),
        auth.c_str(),
        user.isEmpty()   ? nullptr : user.c_str(),
        secret.isEmpty() ? nullptr : secret.c_str(),
        tmpl.c_str()
    );

    JsonResponder::success("Custom channel updated").send(server);
    LOG_INFO("[CONFIG] Custom HTTP channel updated: %s", endpoint.c_str());
}

void ConfigServer::handleTestCustom() {
    serverStartTime = millis();

    if (!customService) {
        JsonResponder::sendError(server, 503, "Custom channel not available");
        return;
    }

    if (!customService->isConfigured()) {
        JsonResponder::sendError(server, 400, "Custom channel not configured. Set endpoint and body template first.");
        return;
    }

    if (!WiFi.isConnected()) {
        JsonResponder::sendError(server, 503, "WiFi not connected. Cannot send custom notification.");
        return;
    }

    LOG_INFO("[TEST] Sending test custom HTTP notification...");
    bool success = customService->send("BilgeRise Test: This is a test message from your ESP32 boat monitor.");

    if (success) {
        LOG_INFO("[TEST] Test custom notification sent successfully!");
        JsonResponder::success("Test custom notification sent!").send(server);
    } else {
        LOG_INFO("[TEST] Test custom notification failed");
        JsonResponder().boolean("success", false)
                       .str("error", "Failed to send test notification. Check serial log for details.")
                       .send(server, 500);
    }
}

void ConfigServer::handleTestSMS() {
    serverStartTime = millis();
    
    if (!smsService) {
        JsonResponder::sendError(server, 503, "SMS service not available");
        return;
    }
    
    if (!smsService->hasPhoneNumber()) {
        JsonResponder::sendError(server, 400, "No phone number configured. Please save a phone number first.");
        return;
    }
    
    if (!WiFi.isConnected()) {
        JsonResponder::sendError(server, 503, "WiFi not connected. Cannot send SMS.");
        return;
    }
    
    LOG_INFO("[TEST] Sending test SMS...");
    bool success = smsService->send("BilgeRise Test: This is a test message from your ESP32 boat monitor.");
    
    if (success) {
        LOG_INFO("[TEST] Test SMS sent successfully!");
        JsonResponder::success("Test SMS sent successfully!").send(server);
    } else {
        LOG_INFO("[TEST] Test SMS failed to send");
        JsonResponder().boolean("success", false)
                       .str("error", "Failed to send test SMS. Check serial log for details.")
                       .send(server, 500);
    }
}

void ConfigServer::handleTestDiscord() {
    serverStartTime = millis();
    
    if (!discordService) {
        JsonResponder::sendError(server, 503, "Discord service not available");
        return;
    }
    
    if (!discordService->hasWebhookUrl()) {
        JsonResponder::sendError(server, 400, "No Discord webhook configured. Please save a webhook URL first.");
        return;
    }
    
    if (!WiFi.isConnected()) {
        JsonResponder::sendError(server, 503, "WiFi not connected. Cannot send Discord message.");
        return;
    }
    
    LOG_INFO("[TEST] Sending test Discord message...");
    bool success = discordService->send("🚤 **BilgeRise Test** - This is a test message from your ESP32 boat monitor.");
    
    if (success) {
        LOG_INFO("[TEST] Test Discord message sent successfully!");
        JsonResponder::success("Test Discord message sent successfully!").send(server);
    } else {
        LOG_INFO("[TEST] Test Discord message failed to send");
        JsonResponder().boolean("success", false)
                       .str("error", "Failed to send test Discord message. Check serial log for details.")
                       .send(server, 500);
    }
}

void ConfigServer::handleSetMqttConfig() {
    serverStartTime = millis();

    if (!mqttService) {
        JsonResponder::sendError(server, 503, "MQTT service not available");
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
        JsonResponder::success("MQTT configuration updated").send(server);
    } else {
        JsonResponder::sendError(server, 400, "No valid parameters provided");
    }
}

void ConfigServer::handleTestMqtt() {
    serverStartTime = millis();

    if (!mqttService) {
        JsonResponder::sendError(server, 503, "MQTT service not available");
        return;
    }

    if (!mqttService->hasBrokerConfig()) {
        JsonResponder::sendError(server, 400, "No MQTT broker configured. Please save broker settings first.");
        return;
    }

    if (!WiFi.isConnected()) {
        JsonResponder::sendError(server, 503, "WiFi not connected. Cannot reach MQTT broker.");
        return;
    }

    if (!mqttService->isConnected()) {
        JsonResponder::sendError(server, 503, "MQTT broker not connected. Check host/port and broker status.");
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
        JsonResponder::success("Test MQTT message published successfully!").send(server);
    } else {
        LOG_INFO("[TEST] Test MQTT message failed to publish");
        JsonResponder().boolean("success", false)
                       .str("error", "Failed to publish test MQTT message.")
                       .send(server, 500);
    }
}



// ============================================================================
// DEBUG AND MONITORING HANDLERS
// ============================================================================

void ConfigServer::handleGetReading() {
    PROFILE_REQUEST("GET /read");
    serverStartTime = millis();

    if (!waterSensor) {
        JsonResponder().boolean("sensorAvailable", false)
                       .str("error", "Water sensor not connected")
                       .send(server, 503);
        return;
    }

    SensorReading reading = waterSensor->readLevel();
    JsonResponder r;
    r.boolean("sensorAvailable", true);
    r.boolean("valid", reading.valid);
    r.num("millivolts", reading.millivolts, 2);
    if (reading.valid) {
        r.num("level_cm", reading.level_cm, 2);
    }
    float rate = waterSensor->getRateOfChange_cm30min();
    if (!isnan(rate)) {
        r.num("rate_cm_30min", rate, 2);
    }

    r.send(server);
}

void ConfigServer::handleDebug() {
    PROFILE_REQUEST("GET /debug");
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
        JsonResponder::sendError(server, 503, "OTA manager not available");
        return;
    }
    
    const char* state;
    switch(otaManager->getState()) {
        case OTAState::IDLE: state = "idle"; break;
        case OTAState::CHECKING: state = "checking"; break;
        case OTAState::UPDATE_AVAILABLE: state = "update_available"; break;
        case OTAState::DOWNLOADING: state = "downloading"; break;
        case OTAState::INSTALLING: state = "installing"; break;
        case OTAState::SUCCESS: state = "success"; break;
        case OTAState::FAILED: state = "failed"; break;
        default: state = "idle"; break;
    }
    
    // Use float division so String(val, 1) selects the decimal-places overload
    // (String(unsigned long, 1) interprets 1 as a number base and returns "").
    float hoursSinceCheck = (float)otaManager->getTimeSinceLastCheckS() / 3600.0f;
    
    JsonResponder().str("currentVersion", otaManager->getCurrentVersion())
                   .str("availableVersion", otaManager->getAvailableVersion())
                   .boolean("updateAvailable", otaManager->isUpdateAvailable())
                   .str("state", state)
                   .str("lastError", otaManager->getLastError())
                   .boolean("autoCheckEnabled", otaManager->isAutoCheckEnabled())
                   .boolean("autoInstallEnabled", otaManager->isAutoInstallEnabled())
                   .boolean("notificationsEnabled", otaManager->areNotificationsEnabled())
                   .str("githubRepo", otaManager->getGitHubRepo())
                   .boolean("hasGithubToken", otaManager->hasGitHubToken())
                   .boolean("hasUpdatePassword", otaManager->hasUpdatePassword())
                   .num("checkIntervalHours", (uint32_t)(otaManager->getCheckIntervalMs() / 3600000))
                   .num("timeSinceLastCheckHours", hoursSinceCheck, 1)
                   .send(server);
}

void ConfigServer::handleOTACheck() {
    serverStartTime = millis();
    
    if (!otaManager) {
        JsonResponder::sendError(server, 503, "OTA manager not available");
        return;
    }
    
    bool updateFound = otaManager->manualCheckForUpdates();
    
    JsonResponder r;
    r.boolean("success", true);
    r.boolean("updateAvailable", updateFound);
    if (updateFound) {
        r.str("version", otaManager->getAvailableVersion());
    }
    
    r.send(server);
}

void ConfigServer::handleOTAUpdate() {
    serverStartTime = millis();
    
    if (!otaManager) {
        JsonResponder::sendError(server, 503, "OTA manager not available");
        return;
    }
    
    // Get optional password from POST data
    const char* password = nullptr;
    if (server->hasArg("password")) {
        password = server->arg("password").c_str();
    }
    
    bool success = otaManager->startUpdate(password);
    
    if (!success) {
        JsonResponder().boolean("success", false)
                       .str("error", otaManager->getLastError())
                       .send(server, 400);
    } else {
        // This will likely not be received as ESP32 will reboot
        JsonResponder::success("Update started, device will reboot").send(server);
    }
}

void ConfigServer::handleOTASettings() {
    serverStartTime = millis();
    
    if (!otaManager) {
        JsonResponder::sendError(server, 503, "OTA manager not available");
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
            // 12h minimum (NVS wear floor), 1 week maximum
            if (hours >= 12 && hours <= 168) {
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
        JsonResponder::success("OTA settings updated").send(server);
    } else {
        JsonResponder::sendError(server, 400, "No valid settings provided");
    }
}

#endif // UNIT_TESTING


