#include <Arduino.h>
#include <WiFi.h>
#include <nvs_flash.h>
#include "Logger.h"
#include "MQTTService.h"
#include "TimeManagement.h"
#include "WiFiManager.h"
#include "ConfigServer.h"
#include "LightCode.h"
#include "WaterPressureSensor.h"
#include "SendSMS.h"
#include "SendDiscord.h"
#include "OTAManager.h"
#include "Version.h"

// System States
enum State {
    ERROR,
    NORMAL,
    EMERGENCY,
    CONFIG
};

// Forward declarations
void handleButtonPress();
const char* stateToString(State state);

struct SystemState {
    State currentState;
    // Timers
    uint32_t lastStateChangeTime;
    uint32_t emergencyConditionsTrueTime;  // When emergency conditions became true (Tier 1)
    uint32_t emergencyConditionsFalseTime; // When emergency conditions became false (Tier 1)
    uint32_t lastEmergencyMessageTime;
    uint32_t lastHornToggleTime;           // For horn pulsing pattern (Tier 2)
    
    // Event flags
    bool emergencyConditions;              // Tier 1 threshold exceeded
    bool urgentEmergencyConditions;        // Tier 2 threshold exceeded
    bool hornCurrentlyOn;                  // Tracks horn state for pulsing
    bool sensorError;
    volatile bool configCommandReceived;
    bool notificationsSilenced;            // Tracks if emergency alerts are silenced
};

SystemState systemState; // Tracks current device state

// Status logging
static constexpr uint32_t STATUS_LOG_INTERVAL_MS = 10000; // Log status every 10 seconds
uint32_t lastStatusLogTime = 0;

static constexpr int BUTTON_PIN = 23; // GPIO
static constexpr int ALERT_PIN = 19; // GPIO
static constexpr int SENSOR_PIN = 32; // Water sensor analog pin ADC1 because wifi is required
#ifdef ENABLE_MOCK_MODE
static constexpr bool USE_MOCK = true;
#else
static constexpr bool USE_MOCK = false;
#endif
static constexpr int LIGHT_PIN = 12;

// Emergency timeout before transitioning to EMERGENCY state
static constexpr int EMERGENCY_TIMEOUT_MS = 5000;

volatile bool buttonPressed = false;
volatile unsigned long lastButtonPress = 0;
volatile unsigned long buttonPressStartTime = 0;
volatile bool buttonCurrentlyPressed = false;

// Message tracing - unique ID for each notification
static uint32_t messageTraceId = 0;

// Create easier references to the singleton objects
ConfigServer* configServer = nullptr;
OTAManager* otaManager = nullptr;
LightCode light(LIGHT_PIN);
TimeManagement& rtc = TimeManagement::getInstance();
WiFiManager& wifiMgr = WiFiManager::getInstance();
WaterPressureSensor waterSensor(USE_MOCK); // false = use real sensor, not mock data
SendSMS sms;
SendDiscord discord;
MQTTService mqtt;

// Logger publishes logs via MQTT; Discord kept for OTA / direct emergency sends
SendDiscord* g_discord = &discord;

// Helper function to convert state enum to string
const char* stateToString(State state) {
    switch (state) {
        case ERROR: return "ERROR";
        case NORMAL: return "NORMAL";
        case EMERGENCY: return "EMERGENCY";
        case CONFIG: return "CONFIG";
        default: return "UNKNOWN";
    }
}

#ifndef UNIT_TESTING
// Exclude setup() and loop() when building unit tests to avoid conflicts with test harness
void setup() {
    Serial.begin(115200);
    
    // Initialize NVS FIRST (before any Preferences usage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Erase corrupted NVS and reinitialize
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    waterSensor.init();
    
    // Initialize system state timers
    systemState.lastStateChangeTime = millis();
    systemState.emergencyConditionsTrueTime = millis();
    systemState.emergencyConditionsFalseTime = millis();
    systemState.lastEmergencyMessageTime = 0;
    systemState.lastHornToggleTime = 0;
    
    // Initialize system state flags
    systemState.emergencyConditions = false;
    systemState.urgentEmergencyConditions = false;
    systemState.hornCurrentlyOn = false;
    systemState.notificationsSilenced = false;
    
    // Initialize MQTT early so all subsequent LOG_* calls can be queued for delivery
    g_mqtt = &mqtt;
    mqtt.begin();
    LOG_SETUP("[SETUP] MQTTService initialized");

    // Initialize OTAManager early to check for rollback/first boot after update
    otaManager = new OTAManager(&sms, &discord);
    otaManager->begin();
    LOG_SETUP("[SETUP] OTAManager initialized - version %s", FIRMWARE_VERSION);
    
    // Initialize ConfigServer early to load calibration from NVS
    // This ensures saved calibration is applied before first sensor reading
    configServer = new ConfigServer(&waterSensor, &sms, &discord, otaManager, &mqtt);
    LOG_SETUP("[SETUP] ConfigServer initialized - calibration loaded from NVS");
    
    // Print unique device AP password for easy access
    LOG_SETUP("========================================");
    LOG_SETUP("Device Configuration Access Point:");
    LOG_SETUP("  SSID: ESP32-BoatMonitor-Setup");
    LOG_SETUP("  Password: %s", configServer->getAPPassword());
    LOG_SETUP("  Firmware: v%s", FIRMWARE_VERSION);
    LOG_SETUP("========================================");

    pinMode(ALERT_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(BUTTON_PIN, handleButtonPress, CHANGE);

    // Initialize WiFiManager
    wifiMgr.begin();
    
    // Check if we have stored credentials
    // If not, or if user holds a button, start setup mode
    std::vector<String> ssids = wifiMgr.getStoredSSIDs();

    if (ssids.empty()) {
        systemState.currentState = CONFIG;
        LOG_STATE("[STATE] Initial state: %s (no WiFi credentials found)", stateToString(systemState.currentState));
        light.setPattern(PATTERN_SLOW_BLINK); // CONFIG state pattern
    } else {
        systemState.currentState = NORMAL;
        LOG_STATE("[STATE] Initial state: %s", stateToString(systemState.currentState));
        light.setPattern(PATTERN_OFF); // NORMAL state pattern
        LOG_SETUP("WiFi credentials found, connecting...");
        delay(2000);
        
        if (wifiMgr.isConnected()) {
            LOG_SETUP("IP address: %s", WiFi.localIP().toString().c_str());
        }
    }
}

void loop() {

    mqtt.loop();
    rtc.sync();
    light.update();

    // Run OTA update checks (only when not in CONFIG or EMERGENCY states)
    if (otaManager && systemState.currentState != CONFIG && systemState.currentState != EMERGENCY) {
        otaManager->loop();
    }

    // Monitor WiFi connection status for critical events
    static bool wasWiFiConnected = false;
    bool isWiFiConnected = wifiMgr.isConnected();
    
    if (wasWiFiConnected && !isWiFiConnected) {
        LOG_EVENT("[WIFI] Connection lost - internet disconnected");
        if (systemState.currentState == EMERGENCY) {
            LOG_EVENT("[WIFI_EMERGENCY] WiFi lost during EMERGENCY state - emergency notifications may be delayed!");
        }
        // Update LED if in NORMAL state to show WiFi disconnection
        if (systemState.currentState == NORMAL) {
            light.setPattern(PATTERN_DOUBLE_BLINK);
            LOG_DEBUG("[LIGHT] WiFi disconnected - switching to double blink pattern");
        }
    } else if (!wasWiFiConnected && isWiFiConnected) {
        LOG_EVENT("[WIFI] Connection restored - internet connected");
        // Update LED if in NORMAL state to show WiFi reconnection
        if (systemState.currentState == NORMAL) {
            light.setPattern(PATTERN_OFF);
            LOG_DEBUG("[LIGHT] WiFi reconnected - switching to off pattern");
        }
    }
    wasWiFiConnected = isWiFiConnected;

    // Track state before processing to detect changes
    State previousState = systemState.currentState;

    SensorReading currentReading = waterSensor.readLevel();
    // LOG_SENSOR("SensorReading: valid=%d, level_cm=%.2f, millivolts=%.2f, timestamp={isNTPSynced=%d, unixTime=%ld, timeSinceBoot=%u}",
    //               currentReading.valid, currentReading.level_cm, currentReading.millivolts,
    //               currentReading.timestamp.isNTPSynced, static_cast<long>(currentReading.timestamp.unixTime), currentReading.timestamp.timeSinceBoot);

    // Handle potential events and set appropriate flags
    bool previousSensorError = systemState.sensorError;
    systemState.sensorError = !currentReading.valid;
    
    if (systemState.sensorError && !previousSensorError) {
        LOG_EVENT("[EVENT] Sensor error detected!");
    } else if (!systemState.sensorError && previousSensorError) {
        LOG_EVENT("[EVENT] Sensor error cleared");
    }

    static bool lastConfigCommandReceived = false;
    if (systemState.configCommandReceived && !lastConfigCommandReceived) {
        LOG_EVENT("[EVENT] Button pressed - config command received");
    }
    lastConfigCommandReceived = systemState.configCommandReceived;

    // Check for 5-second button hold to toggle silence in EMERGENCY state
    static bool silenceToggleHandled = false;
    static bool lastButtonState = false;
    
    // Reset flag when button is released (detect transition from pressed to not pressed)
    if (lastButtonState && !buttonCurrentlyPressed) {
        silenceToggleHandled = false;
    }
    lastButtonState = buttonCurrentlyPressed;
    
    if (buttonCurrentlyPressed && systemState.currentState == EMERGENCY) {
        unsigned long holdDuration = millis() - buttonPressStartTime;
        if (holdDuration >= 5000 && !silenceToggleHandled) {
            // Toggle silence state
            systemState.notificationsSilenced = !systemState.notificationsSilenced;
            silenceToggleHandled = true;
            
            if (systemState.notificationsSilenced) {
                LOG_EVENT("[EVENT] Emergency notifications SILENCED by button hold");
                
                // Send confirmation notification
                if (!USE_MOCK) {
                    messageTraceId++;
                    char silenceMessage[100];
                    snprintf(silenceMessage, sizeof(silenceMessage), "[MSG:%u] Boat Monitor: Emergency alerts silenced", messageTraceId);
                    sms.send(silenceMessage);
                    discord.send(silenceMessage);
                }
            } else {
                LOG_EVENT("[EVENT] Emergency notifications RE-ENABLED by button hold - WiFi: %d", wifiMgr.isConnected());
            }
        }
    }

    // Check Tier 1 emergency conditions (message notifications)
    bool previousEmergencyConditions = systemState.emergencyConditions;
    float emergencyThreshold = configServer->getEmergencyWaterLevel();
    if (currentReading.level_cm >= emergencyThreshold) {
        systemState.emergencyConditions = true;
        if (!previousEmergencyConditions) {
            systemState.emergencyConditionsTrueTime = millis(); // Update timer when conditions START
            LOG_EVENT("[EVENT] Tier 1 Emergency conditions detected! level=%.2f cm (threshold=%.2f cm)",
                          currentReading.level_cm, emergencyThreshold);
        }
    } else {
        systemState.emergencyConditions = false;
        if (previousEmergencyConditions) {
            systemState.emergencyConditionsFalseTime = millis(); // Update timer when conditions CLEAR
            LOG_EVENT("[EVENT] Tier 1 Emergency conditions cleared. level=%.2f cm", currentReading.level_cm);
        }
    }
    
    // Check Tier 2 urgent emergency conditions (horn alarm)
    bool previousUrgentEmergencyConditions = systemState.urgentEmergencyConditions;
    float urgentEmergencyThreshold = configServer->getUrgentEmergencyWaterLevel();
    if (currentReading.level_cm >= urgentEmergencyThreshold) {
        systemState.urgentEmergencyConditions = true;
        if (!previousUrgentEmergencyConditions) {
            LOG_EVENT("[EVENT] Tier 2 URGENT Emergency conditions detected! level=%.2f cm (threshold=%.2f cm)",
                          currentReading.level_cm, urgentEmergencyThreshold);
        }
    } else {
        systemState.urgentEmergencyConditions = false;
        if (previousUrgentEmergencyConditions) {
            LOG_EVENT("[EVENT] Tier 2 URGENT Emergency conditions cleared. level=%.2f cm", currentReading.level_cm);
        }
    }


    // Handle state-specific operations
    
    switch (systemState.currentState) {
        case ERROR:
            if (systemState.sensorError == false) {
                LOG_STATE("[STATE] Transitioning from %s to NORMAL (sensor recovered)", stateToString(systemState.currentState));
                systemState.currentState = NORMAL;
                systemState.lastStateChangeTime = millis();
            }
            else if (systemState.configCommandReceived) {
                LOG_STATE("[STATE] Transitioning from %s to CONFIG (button pressed)", stateToString(systemState.currentState));
                systemState.currentState = CONFIG;
                systemState.lastStateChangeTime = millis();
            }
            
            // send message about sensor failure?
            break;
        case NORMAL:
            if (systemState.sensorError == true) {
                LOG_EVENT("[STATE] Transitioning from %s to ERROR (sensor error detected)", stateToString(systemState.currentState));
                systemState.currentState = ERROR;
                systemState.lastStateChangeTime = millis();
            }
            else if (systemState.emergencyConditions && 
                (millis() - systemState.emergencyConditionsTrueTime) >= EMERGENCY_TIMEOUT_MS) {
                LOG_EVENT("[STATE] Transitioning to EMERGENCY state - WiFi connected: %d, IP: %s, water level: %.2f cm", 
                              wifiMgr.isConnected(), WiFi.localIP().toString().c_str(), currentReading.level_cm);
                systemState.currentState = EMERGENCY;
                systemState.lastStateChangeTime = millis();
            }
            else if (systemState.configCommandReceived) {
                LOG_STATE("[STATE] Transitioning from %s to CONFIG (button pressed)", stateToString(systemState.currentState));
                systemState.currentState = CONFIG;
                systemState.lastStateChangeTime = millis();
            } 
            break;
        case CONFIG:
            if (!configServer->isSetupModeActive()) {
                // Start setup mode (configServer already exists from setup())
                LOG_STATE("[STATE] Starting configuration server mode");
                configServer->startSetupMode();
            }
            else {
                configServer->handleClient();
                
                // Check if setup mode has ended (timeout or manual stop)
                if (!configServer->isSetupModeActive()) {
                    LOG_STATE("[STATE] Transitioning from %s to NORMAL (config completed)", stateToString(systemState.currentState));
                    systemState.currentState = NORMAL;
                    systemState.configCommandReceived = false;
                    systemState.lastStateChangeTime = millis();
                }
            }
            break;
        case EMERGENCY:
            if (systemState.emergencyConditions == false && (millis() - systemState.emergencyConditionsFalseTime) >= EMERGENCY_TIMEOUT_MS) {
                LOG_EVENT("[STATE] Transitioning from %s to NORMAL (emergency cleared)", stateToString(systemState.currentState));
                systemState.currentState = NORMAL;
                systemState.lastStateChangeTime = millis();
                // Ensure horn is OFF when exiting EMERGENCY state
                digitalWrite(ALERT_PIN, LOW);
                systemState.hornCurrentlyOn = false;
                // Auto-clear silence flag when emergency ends (safety feature)
                if (systemState.notificationsSilenced) {
                    LOG_EVENT("[STATE] Auto-clearing notification silence (emergency cleared)");
                    systemState.notificationsSilenced = false;
                }
            } else {
                // TIER 1: Send emergency message notifications (always in EMERGENCY state)
                int emergencyFreq = configServer->getEmergencyNotifFreq();
                
                if (millis() - systemState.lastEmergencyMessageTime >= emergencyFreq) {
                    // Update timer even when silenced to prevent message burst when un-silenced
                    systemState.lastEmergencyMessageTime = millis();
                    
                    // Only send notifications if not silenced
                    if (!systemState.notificationsSilenced) {
                        messageTraceId++;
                        char emergMessageBuf[120];
                        if (systemState.urgentEmergencyConditions) {
                            snprintf(emergMessageBuf, sizeof(emergMessageBuf), "[MSG:%u] Boat Monitor URGENT Alert: Tier 2 Emergency Level %.2f cm", messageTraceId, currentReading.level_cm);
                        } else {
                            snprintf(emergMessageBuf, sizeof(emergMessageBuf), "[MSG:%u] Boat Monitor Alert: Emergency Level %.2f cm", messageTraceId, currentReading.level_cm);
                        }
                        LOG_EVENT("[STATE] EMERGENCY: Sending alert message: %s", emergMessageBuf);

                        if (!USE_MOCK) {
                            sms.send(emergMessageBuf);
                            discord.send(emergMessageBuf);
                        } else {
                            LOG_EVENT("[MOCK] Skipping SMS/Discord send (TraceID:%u)", messageTraceId);
                        }
                    } else {
                        LOG_INFO("[STATE] EMERGENCY: Notifications silenced, skipping alert message");
                    }
                }
                
                // TIER 2: Horn alarm pulsing (only if urgent emergency conditions met and not silenced)
                if (systemState.urgentEmergencyConditions && !systemState.notificationsSilenced) {
                    int hornOnDuration = configServer->getHornOnDuration();
                    int hornOffDuration = configServer->getHornOffDuration();
                    uint32_t currentDuration = systemState.hornCurrentlyOn ? hornOnDuration : hornOffDuration;
                    
                    if (millis() - systemState.lastHornToggleTime >= currentDuration) {
                        systemState.hornCurrentlyOn = !systemState.hornCurrentlyOn;
                        digitalWrite(ALERT_PIN, systemState.hornCurrentlyOn ? HIGH : LOW);
                        systemState.lastHornToggleTime = millis();
                        LOG_DEBUG("[HORN] Horn %s", systemState.hornCurrentlyOn ? "ON" : "OFF");
                    }
                } else {
                    // Not urgent emergency or notifications silenced - ensure horn is OFF
                    if (systemState.hornCurrentlyOn) {
                        digitalWrite(ALERT_PIN, LOW);
                        systemState.hornCurrentlyOn = false;
                        if (systemState.notificationsSilenced) {
                            LOG_EVENT("[HORN] Horn deactivated (notifications silenced)");
                        } else {
                            LOG_EVENT("[HORN] Horn deactivated (Tier 2 conditions cleared)");
                        }
                    }
                }
            }
            break;
    }

    // Automatically update LED pattern if state changed
    if (systemState.currentState != previousState) {
        switch (systemState.currentState) {
            case NORMAL:
                // Check WiFi status when entering NORMAL state
                if (wifiMgr.isConnected()) {
                    light.setPattern(PATTERN_OFF);
                } else {
                    light.setPattern(PATTERN_DOUBLE_BLINK);
                    LOG_DEBUG("[LIGHT] Entering NORMAL state with WiFi disconnected - using double blink");
                }
                break;
            case CONFIG:
                light.setPattern(PATTERN_SLOW_BLINK);
                break;
            case ERROR:
                light.setPattern(PATTERN_FAST_BLINK);
                break;
            case EMERGENCY:
                light.setPattern(PATTERN_SOLID);
                break;
        }
    }

    // Periodic status logging
    if (millis() - lastStatusLogTime >= STATUS_LOG_INTERVAL_MS) {
        LOG_STATUS("[STATUS] State=%s, WaterLevel=%.2f cm, SensorError=%d, EmergencyConditions=%d",
                      stateToString(systemState.currentState),
                      currentReading.level_cm,
                      systemState.sensorError,
                      systemState.emergencyConditions);
        LOG_STATUS("[HEAP] Free=%u, MinFree=%u, MaxBlock=%u",
                      ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
        lastStatusLogTime = millis();
    }
}
#endif // UNIT_TESTING


/*
    Button press interrupt handler
*/
void IRAM_ATTR handleButtonPress() {
    unsigned long now = millis();
    bool currentPinState = digitalRead(BUTTON_PIN);
    
    // Button is pressed when pin is LOW (INPUT_PULLUP)
    if (currentPinState == LOW && !buttonCurrentlyPressed) {
        // Button press detected
        if (now - lastButtonPress <= 50) return; // Debounce 50ms
        buttonPressStartTime = now;
        buttonCurrentlyPressed = true;
        lastButtonPress = now;
    } else if (currentPinState == HIGH && buttonCurrentlyPressed) {
        // Button release detected
        buttonCurrentlyPressed = false;
        unsigned long holdDuration = now - buttonPressStartTime;
        
        // Short press (< 5 seconds) = config command
        if (holdDuration < 5000) {
            systemState.configCommandReceived = true;
        }
        // Long press (>= 5 seconds) will be handled in main loop for silence toggle
    }
}
