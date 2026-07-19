#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "WiFiManager.h"
#include "WaterPressureSensor.h"
#include "SmsChannel.h"
#include "DiscordChannel.h"
#include "CustomChannel.h"
#include "OTAManager.h"
#include "MQTTService.h"
#include "SettingsStore.h"

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
constexpr const char AP_SSID[] = "ESP32-BilgeRise-Setup";
constexpr unsigned long SERVER_TIMEOUT_MS = 240000;
constexpr int DNS_PORT = 53; // Standard DNS port for captive portal

// Emergency settings validation limits
constexpr float MIN_EMERGENCY_WATER_LEVEL_CM = WATER_LEVEL_RANGE_MIN_CM; // 5.0 cm
constexpr float MAX_EMERGENCY_WATER_LEVEL_CM = WATER_LEVEL_RANGE_MAX_CM; // 100 cm (max sensor range)
constexpr int MIN_EMERGENCY_NOTIF_FREQ_MS = 5000;      // 5 seconds
constexpr int MAX_EMERGENCY_NOTIF_FREQ_MS = 3600000;   // 1 hour
constexpr int MIN_HORN_DURATION_MS = 100;              // 0.1 seconds
constexpr int MAX_HORN_DURATION_MS = 10000;            // 10 seconds 

class ConfigServer {
private:
    // === Core Members ===
    WebServer* server;
    DNSServer* dnsServer = nullptr;         // DNS server for captive portal
    WaterPressureSensor* waterSensor;
    SmsChannel*     smsService;
    DiscordChannel* discordService;
    CustomChannel*  customService;
    OTAManager* otaManager;
    MQTTService* mqttService;
    SettingsStore* settingsStore;           // Single source of truth for alarm thresholds
    Preferences calibrationPrefs;           // NVS storage for calibration data
    unsigned long serverStartTime;
    bool setupModeActive = false;
    String apPassword;                      // Unique per-device AP password generated from chip ID
    
    // === WiFi Configuration Handlers ===
    void handleRoot();                      // Serve main dashboard page (gzipped)
    void handleWiFiConfig();                // Serve WiFi configuration page (gzipped)
    void handleNotificationsPage();         // Serve notifications configuration page (gzipped)
    void handleSettings();                  // Serve settings hub page (gzipped)
    void handleInit();                      // GET /init — merged JSON for main page load
    void handleSettingsInit();              // GET /settings/init — merged JSON for settings page load
    void handleDebugInit();                 // GET /debug/init — merged JSON for debug page load
    void handleCaptivePortalProbe();        // 302 redirect for captive-portal probe URLs
    void handleSubmit();                    // Process WiFi configuration submission
    void handleStatus();                    // Return WiFi connection status JSON
    void handleWiFiNetworks();              // GET /wifi/networks — stored SSID list JSON
    void handleWiFiRemove();               // POST /wifi/remove — remove a stored network
    
    // === Sensor Calibration Handlers ===
    void handleCalibrateZero();             // POST: Set zero calibration point
    void handleCalibratePoint2();           // POST: Set second calibration point
    void handleGetCalibration();            // GET: Return current calibration settings
    void loadCalibration();                 // Load calibration from NVS
    void saveCalibration();                 // Save calibration to NVS
    
    // === Emergency Settings Handlers ===
    void handleSetEmergencyLevel();         // POST: Set emergency water level threshold (Tier 1)
    void handleSetEmergencyNotifFreq();     // POST: Set emergency notification frequency
    void handleSetUrgentEmergencyLevel();   // POST: Set urgent emergency water level threshold (Tier 2)
    void handleTestEmergencyPin();          // POST: Test the emergency pin output device
    bool parseAndValidateLevel(float level_cm, bool isTier1); // Shared range + cross-tier validation (sends 400 on failure)
    
    // === Notification Settings Handlers ===
    void handleGetNotifications();          // GET: Return current notification settings
    void handleNotificationsStatus();       // GET: Lean status-only JSON (booleans, no secrets) for polling
    void handleSetPhoneNumber();            // POST: Set SMS phone number
    void handleSetTwilioCreds();            // POST: Set Twilio account SID / auth token / svc SID
    void handleSetDiscordWebhook();         // POST: Set Discord webhook URL
    void handleSetCustomChannel();          // POST: Set custom HTTP channel config
    void handleTestSMS();                   // POST: Send a test SMS message
    void handleTestDiscord();               // POST: Send a test Discord message
    void handleTestCustom();                // POST: Send a test custom channel message
    void handleSetMqttConfig();             // POST: Configure MQTT broker
    void handleTestMqtt();                  // POST: Send a test MQTT message
    
    // === Page Serving Helper ===
    void sendCachedPage(const char* data, size_t len, const char* contentType);

    // === Debug and Monitoring Handlers ===
    void handleDebug();                     // Serve debug page (gzipped)
    void handleGetReading();                // Return current sensor reading as JSON
    
    // === OTA Update Handlers ===
    void handleOTAPage();                   // Serve OTA settings page (gzipped)
    void handleOTAStatus();                 // GET: Return OTA status JSON
    void handleOTACheck();                  // GET: Manually trigger update check
    void handleOTAUpdate();                 // POST: Start firmware update
    void handleOTASettings();               // POST: Configure OTA settings
    
public:
    ConfigServer(WaterPressureSensor* sensor  = nullptr,
                 SmsChannel*          sms     = nullptr,
                 DiscordChannel*      discord = nullptr,
                 CustomChannel*       custom  = nullptr,
                 OTAManager*          ota     = nullptr,
                 MQTTService*         mqtt    = nullptr,
                 SettingsStore*       settings = nullptr);
    ~ConfigServer();
    
    // Start AP + Web server
    void startSetupMode();
    
    // Stop AP and web server
    void stopSetupMode();
    
    // Check if in setup mode
    bool isSetupModeActive();
    
    // Should be called in main loop
    void handleClient();
    
    // === Emergency Settings Getters — delegate to SettingsStore ===
    float getEmergencyWaterLevel()       const { return settingsStore ? settingsStore->getEmergencyWaterLevel()       : SETTINGS_DEFAULTS().emergencyWaterLevel_cm; }
    int   getEmergencyNotifFreq()        const { return settingsStore ? settingsStore->getEmergencyNotifFreq()        : SETTINGS_DEFAULTS().emergencyNotifFreq_ms; }
    float getUrgentEmergencyWaterLevel() const { return settingsStore ? settingsStore->getUrgentEmergencyWaterLevel() : SETTINGS_DEFAULTS().urgentEmergencyWaterLevel_cm; }
    int   getHornOnDuration()            const { return settingsStore ? settingsStore->getHornOnDuration()            : SETTINGS_DEFAULTS().hornOnDuration_ms; }
    int   getHornOffDuration()           const { return settingsStore ? settingsStore->getHornOffDuration()           : SETTINGS_DEFAULTS().hornOffDuration_ms; }
    
    // === AP Password Getter ===
    String getAPPassword() const { return apPassword; }
    
    // === OTA Manager Setter ===
    void setOTAManager(OTAManager* ota) { otaManager = ota; }
};


