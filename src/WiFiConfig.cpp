#include "WiFiConfig.h"

const char* WiFiConfig::AP_SSID = "ESP32-BoatMonitor-Setup";
const char* WiFiConfig::AP_PASSWORD = "12345678";

WiFiConfig::WiFiConfig(WaterPressureSensor* sensor) : server(nullptr), waterSensor(sensor) {
    // Initialize calibration preferences
    calibrationPrefs.begin("sensor_cal", false);
    loadCalibration();
}

WiFiConfig::~WiFiConfig() {
    stopSetupMode();
}

void WiFiConfig::startSetupMode() {
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
    
    // Step 3: Create and start web server on port 80
    server = new WebServer(80);
    
    // Step 4: Register HTTP handlers
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
    
    // Handle 404
    server->onNotFound([this]() {
        server->send(404, "text/plain", "Not Found");
    });
    
    // Start the server
    server->begin();
    setupModeActive = true;
    serverStartTime = millis();
    
    Serial.println("Setup mode started. Open browser and navigate to 192.168.4.1");
}

void WiFiConfig::stopSetupMode() {
    if (server) {
        server->stop();
        delete server;
        server = nullptr;
    }
    
    // Stop AP, keep STA mode
    WiFi.mode(WIFI_STA);
    setupModeActive = false;

    Serial.println("\n=== Setup mode stopped, resuming normal WiFi ===");
}

bool WiFiConfig::isSetupModeActive() {
    return setupModeActive;
}

void WiFiConfig::handleClient() {
    // Guard clause: Only continue if server exists and setup mode is active
    if (!server || !setupModeActive) return;

    server->handleClient();

    // Handle server timeout
    if ((millis() - serverStartTime) >= SERVER_TIMEOUT_MS) {
        stopSetupMode();
    }   
}

void WiFiConfig::handleRoot() {
    String html = getConfigPage();
    server->send(200, "text/html", html);
    serverStartTime = millis(); // Reset the timeout on page load
}

void WiFiConfig::handleSubmit() {
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

void WiFiConfig::handleStatus() {
    // Return connection status as JSON
    String json = "{";
    json += "\"connected\":" + String(WiFiManager::getInstance().isConnected() ? "true" : "false") + ",";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI());
    json += "}";
    
    server->send(200, "application/json", json);
}

String WiFiConfig::getConfigPage() {
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

void WiFiConfig::handleGetReading() {
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

void WiFiConfig::handleDebug() {
    serverStartTime = millis();
    String html = getDebugPage();
    server->send(200, "text/html", html);
}

void WiFiConfig::handleCalibrateZero() {
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

void WiFiConfig::handleCalibratePoint2() {
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

void WiFiConfig::handleGetCalibration() {
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

void WiFiConfig::loadCalibration() {
    if (!waterSensor) return;
    
    int zero_mv = calibrationPrefs.getInt("zero_mv", -1);
    if (zero_mv >= 0) {
        waterSensor->setCalibrationPoint(0, zero_mv, 0.0f);
    }
    
    int point2_mv = calibrationPrefs.getInt("point2_mv", -1);
    float point2_cm = calibrationPrefs.getFloat("point2_cm", -1.0f);
    if (point2_mv >= 0 && point2_cm >= 0) {
        waterSensor->setCalibrationPoint(1, point2_mv, point2_cm);
    }
}

void WiFiConfig::saveCalibration() {
    if (!waterSensor) return;
    
    calibrationPrefs.putInt("zero_mv", waterSensor->getZeroPointMilliVolts());
    
    if (waterSensor->hasTwoPointCalibration()) {
        calibrationPrefs.putInt("point2_mv", waterSensor->getSecondPointMilliVolts());
        calibrationPrefs.putFloat("point2_cm", waterSensor->getSecondPointLevelCm());
    } else {
        calibrationPrefs.remove("point2_mv");
        calibrationPrefs.remove("point2_cm");
    }
}

String WiFiConfig::getDebugPage() {
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
            
            <div class="nav">
                <a href="/">WiFi Config</a>
                <a href="/read">JSON Reading</a>
                <a href="/calibration">Calibration JSON</a>
            </div>
        </body>
        </html>
    )HTML";
    return html;
}