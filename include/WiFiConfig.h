#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <Preferences.h>
#include "WiFiManager.h"
#include "WaterPressureSensor.h"

/**
 * WiFiConfig - Web server for system configuration and debugging
 * 
 * This class manages a web server that provides:
 * - WiFi network configuration
 * - Sensor calibration (2-point calibration)
 * - System debugging and monitoring
 * - Real-time sensor readings with millivolt data
 */
constexpr char* SENSOR_CALIBRATION_NAMESPACE = "sensor_cal";
constexpr char* AP_SSID = "ESP32-BoatMonitor-Setup";
constexpr char* AP_PASSWORD = "12345678";
static constexpr unsigned long SERVER_TIMEOUT_MS = 240000; 

class WiFiConfig {
private:
    WebServer* server;
    WaterPressureSensor* waterSensor;
    Preferences calibrationPrefs; // NVS storage for calibration data
    
    unsigned long serverStartTime;
    bool setupModeActive = false;
    static const char* AP_SSID;
    static const char* AP_PASSWORD;
    
    // HTTP handlers
    void handleRoot();              // Serve main configuration page
    void handleSubmit();            // Process WiFi configuration submission
    void handleStatus();            // Return WiFi connection status JSON
    void handleDebug();             // Serve debug page with detailed sensor information
    void handleGetReading();        // Return current sensor reading as JSON
    void handleCalibrateZero();     // POST: Set zero calibration point
    void handleCalibratePoint2();   // POST: Set second calibration point
    void handleGetCalibration();    // GET: Return current calibration settings
    
    // HTML page content
    String getConfigPage();
    String getDebugPage();
    
    // Calibration persistence
    void loadCalibration();         // Load calibration from NVS
    void saveCalibration();         // Save calibration to NVS
    
public:
    WiFiConfig(WaterPressureSensor* sensor = nullptr);
    ~WiFiConfig();
    
    // Start AP + Web server
    void startSetupMode();
    
    // Stop AP and web server
    void stopSetupMode();
    
    // Check if in setup mode
    bool isSetupModeActive();
    
    // Should be called in main loop
    void handleClient();
};

