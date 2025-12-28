#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "WiFiManager.h"
#include "WaterPressureSensor.h"
#include "SendSMS.h"
#include "SendDiscord.h"

/**
 * ConfigServer - Web-based configuration server for ESP32 boat monitoring system
 * 
 * This class manages a comprehensive web server that provides HTTP endpoints for:
 * - WiFi network configuration (SSID/password management)
 * - Water sensor calibration (2-point calibration with NVS persistence)
 * - Notification settings (SMS via Twilio, Discord webhooks)
 * - System debugging and real-time monitoring
 * - Test functionality for notifications
 * 
 * The server runs in AP mode (Access Point) on 192.168.4.1 and provides
 * a user-friendly web interface for all system configuration needs.
 */
constexpr const char SENSOR_CALIBRATION_NAMESPACE[] = "sensor_cal";
constexpr const char AP_SSID[] = "ESP32-BoatMonitor-Setup";
constexpr const char AP_PASSWORD[] = "12345678";
constexpr unsigned long SERVER_TIMEOUT_MS = 240000;
constexpr int DNS_PORT = 53; // Standard DNS port for captive portal 

class ConfigServer {
private:
    // === Core Members ===
    WebServer* server;
    DNSServer* dnsServer = nullptr;         // DNS server for captive portal
    WaterPressureSensor* waterSensor;
    SendSMS* smsService;
    SendDiscord* discordService;
    Preferences calibrationPrefs;           // NVS storage for calibration data
    unsigned long serverStartTime;
    bool setupModeActive = false;
    
    // === WiFi Configuration Handlers ===
    void handleRoot();                      // Serve main configuration page
    void handleSubmit();                    // Process WiFi configuration submission
    void handleStatus();                    // Return WiFi connection status JSON
    String getConfigPage();                 // Generate HTML for main config page
    
    // === Sensor Calibration Handlers ===
    void handleCalibrateZero();             // POST: Set zero calibration point
    void handleCalibratePoint2();           // POST: Set second calibration point
    void handleGetCalibration();            // GET: Return current calibration settings
    void loadCalibration();                 // Load calibration from NVS
    void saveCalibration();                 // Save calibration to NVS
    
    // === Notification Settings Handlers ===
    void handleGetNotifications();          // GET: Return current notification settings
    void handleSetPhoneNumber();            // POST: Set SMS phone number
    void handleSetDiscordWebhook();         // POST: Set Discord webhook URL
    void handleTestSMS();                   // POST: Send a test SMS message
    void handleTestDiscord();               // POST: Send a test Discord message
    
    // === Debug and Monitoring Handlers ===
    void handleDebug();                     // Serve debug page with detailed sensor information
    void handleGetReading();                // Return current sensor reading as JSON
    String getDebugPage();                  // Generate HTML for debug/calibration page
    
public:
    ConfigServer(WaterPressureSensor* sensor = nullptr, SendSMS* sms = nullptr, SendDiscord* discord = nullptr);
    ~ConfigServer();
    
    // Start AP + Web server
    void startSetupMode();
    
    // Stop AP and web server
    void stopSetupMode();
    
    // Check if in setup mode
    bool isSetupModeActive();
    
    // Should be called in main loop
    void handleClient();
};


