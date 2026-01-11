#include <Arduino.h>
#include <WiFi.h>
#include "Logger.h"
#include "TimeManagement.h"
#include "WiFiManager.h"
#include "ConfigServer.h"
#include "LightCode.h"
#include "WaterPressureSensor.h"
#include "SendSMS.h"
#include "SendDiscord.h"

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
static constexpr bool USE_MOCK = false; // For mocking sensor readings
static constexpr int LIGHT_PIN = 12;

// Emergency timeout before transitioning to EMERGENCY state
static constexpr int EMERGENCY_TIMEOUT_MS = 1000;

volatile bool buttonPressed = false;
volatile unsigned long lastButtonPress = 0;
volatile unsigned long buttonPressStartTime = 0;
volatile bool buttonCurrentlyPressed = false;

// Create easier references to the singleton objects
ConfigServer* configServer = nullptr;
LightCode light(LIGHT_PIN);
TimeManagement& rtc = TimeManagement::getInstance(); 
WiFiManager& wifiMgr = WiFiManager::getInstance();
WaterPressureSensor waterSensor(USE_MOCK); // false = use real sensor, not mock data
SendSMS sms;
SendDiscord discord;

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

void setup() {
    Serial.begin(115200);
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
    
    // Initialize ConfigServer early to load calibration from NVS
    // This ensures saved calibration is applied before first sensor reading
    configServer = new ConfigServer(&waterSensor, &sms, &discord);
    LOG_SETUP("[SETUP] ConfigServer initialized - calibration loaded from NVS");
    
    // Print unique device AP password for easy access
    LOG_SETUP("========================================");
    LOG_SETUP("Device Configuration Access Point:");
    LOG_SETUP("  SSID: ESP32-BoatMonitor-Setup");
    LOG_SETUP("  Password: %s", configServer->getAPPassword());
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
    
    rtc.sync();
    light.update();

    // Monitor WiFi connection status for critical events
    static bool wasWiFiConnected = false;
    bool isWiFiConnected = wifiMgr.isConnected();
    
    if (wasWiFiConnected && !isWiFiConnected) {
        LOG_EVENT("[WIFI] Connection lost - internet disconnected");
    } else if (!wasWiFiConnected && isWiFiConnected) {
        LOG_EVENT("[WIFI] Connection restored - internet connected");
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
                const char* silenceMessage = "Boat Monitor: Emergency alerts have been temporarily silenced";
                if (!sms.send(silenceMessage)) {
                    LOG_EVENT("[SMS] Failed to send silence confirmation SMS");
                }
                if (!discord.send(silenceMessage)) {
                    LOG_EVENT("[Discord] Failed to send silence confirmation to Discord");
                }
            } else {
                LOG_EVENT("[EVENT] Emergency notifications RE-ENABLED by button hold");
            }
        }
    }

    // Check Tier 1 emergency conditions (message notifications)
    bool previousEmergencyConditions = systemState.emergencyConditions;
    float emergencyThreshold = configServer->getEmergencyWaterLevel();
    // #region agent log
    Serial.printf("[DEBUG-H7] Emergency condition check: level=%.2f, threshold=%.2f, meetsCondition=%d\n", 
        currentReading.level_cm, emergencyThreshold, (currentReading.level_cm >= emergencyThreshold)?1:0);
    // #endregion
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
                // #region agent log
                Serial.printf("[DEBUG-H4] Transitioning to EMERGENCY: emergencyConditions=%d, timeoutMet=%d, waterLevel=%.2f\n", 
                    systemState.emergencyConditions?1:0, 1, currentReading.level_cm);
                // #endregion
                LOG_EVENT("[STATE] Transitioning from %s to EMERGENCY (water level=%.2f cm)", 
                              stateToString(systemState.currentState), currentReading.level_cm);
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
            // #region agent log
            Serial.printf("[DEBUG-H4] In EMERGENCY state: emergencyConditions=%d, notificationsSilenced=%d, currentTime=%lu\n", 
                systemState.emergencyConditions?1:0, systemState.notificationsSilenced?1:0, millis());
            // #endregion
            
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
                
                // #region agent log
                Serial.printf("[DEBUG-H1,H3] Emergency freq check: emergencyFreq=%d, currentTime=%lu, lastEmergencyMessageTime=%lu, timeSinceLastMsg=%lu, willSend=%d\n", 
                    emergencyFreq, millis(), systemState.lastEmergencyMessageTime, (millis()-systemState.lastEmergencyMessageTime), 
                    ((millis()-systemState.lastEmergencyMessageTime)>=emergencyFreq)?1:0);
                // #endregion
                
                if (millis() - systemState.lastEmergencyMessageTime >= emergencyFreq) {
                    // Update timer even when silenced to prevent message burst when un-silenced
                    systemState.lastEmergencyMessageTime = millis();
                    
                    // #region agent log
                    Serial.printf("[DEBUG-H2,H5] Before silence check: notificationsSilenced=%d, wifiConnected=%d\n", 
                        systemState.notificationsSilenced?1:0, WiFi.isConnected()?1:0);
                    // #endregion
                    
                    // Only send notifications if not silenced
                    if (!systemState.notificationsSilenced) {
                        char emergMessageBuf[120];
                        if (systemState.urgentEmergencyConditions) {
                            snprintf(emergMessageBuf, sizeof(emergMessageBuf), "Boat Monitor URGENT Alert: Critical Level %.2f cm - HORN ACTIVATED!", currentReading.level_cm);
                        } else {
                            snprintf(emergMessageBuf, sizeof(emergMessageBuf), "Boat Monitor Alert: Emergency Level %.2f cm", currentReading.level_cm);
                        }
                        LOG_EVENT("[STATE] EMERGENCY: Sending alert message: %s", emergMessageBuf);

                        bool smsResult = sms.send(emergMessageBuf);
                        // #region agent log
                        Serial.printf("[DEBUG-H5,H6] SMS send result: success=%d, wifiConnected=%d\n", 
                            smsResult?1:0, WiFi.isConnected()?1:0);
                        // #endregion
                        
                        if (!smsResult){
                            LOG_EVENT("[SMS] Emergency SMS failed to send");
                        }

                        bool discordResult = discord.send(emergMessageBuf);
                        // #region agent log
                        Serial.printf("[DEBUG-H5,H6] Discord send result: success=%d, wifiConnected=%d\n", 
                            discordResult?1:0, WiFi.isConnected()?1:0);
                        // #endregion
                        
                        if (!discordResult){
                            LOG_EVENT("[Discord] Emergency Discord webhook failed to send");
                        }
                    } else {
                        // #region agent log
                        Serial.printf("[DEBUG-H2] Notifications SILENCED - skipping alert\n");
                        // #endregion
                        LOG_INFO("[STATE] EMERGENCY: Notifications silenced, skipping alert message");
                    }
                } else {
                    // #region agent log
                    Serial.printf("[DEBUG-H1] Notification timing not met yet: timeSinceLastMsg=%lu < emergencyFreq=%d\n", 
                        (millis()-systemState.lastEmergencyMessageTime), emergencyFreq);
                    // #endregion
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
                light.setPattern(PATTERN_OFF);
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
        lastStatusLogTime = millis();
    }
}


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
