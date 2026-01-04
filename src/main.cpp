#include <Arduino.h>
#include <WiFi.h>
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

// Emergency timeout before transitioning to EMERGENCY state
static constexpr int EMERGENCY_TIMEOUT_MS = 1000;

volatile bool buttonPressed = false;
volatile unsigned long lastButtonPress = 0;
volatile unsigned long buttonPressStartTime = 0;
volatile bool buttonCurrentlyPressed = false;

// Create easier references to the singleton objects
ConfigServer* configServer = nullptr;
LightCode light(LED_BUILTIN);
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
    Serial.println("[SETUP] ConfigServer initialized - calibration loaded from NVS");

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
        Serial.printf("[STATE] Initial state: %s (no WiFi credentials found)\n", stateToString(systemState.currentState));
        light.setPattern(PATTERN_SLOW_BLINK); // CONFIG state pattern
    } else {
        systemState.currentState = NORMAL;
        Serial.printf("[STATE] Initial state: %s\n", stateToString(systemState.currentState));
        light.setPattern(PATTERN_OFF); // NORMAL state pattern
        Serial.println("WiFi credentials found, connecting...");
        delay(2000);
        
        if (wifiMgr.isConnected()) {
            Serial.print("IP address: ");
            Serial.println(WiFi.localIP());
        }
    }
}

void loop() {
    
    rtc.sync();
    light.update();

    // Track state before processing to detect changes
    State previousState = systemState.currentState;

    SensorReading currentReading = waterSensor.readLevel();
    Serial.printf("SensorReading: valid=%d, level_cm=%.2f, millivolts=%.2f, timestamp={isNTPSynced=%d, unixTime=%ld, timeSinceBoot=%u}\n",
                  currentReading.valid, currentReading.level_cm, currentReading.millivolts,
                  currentReading.timestamp.isNTPSynced, static_cast<long>(currentReading.timestamp.unixTime), currentReading.timestamp.timeSinceBoot);

    // Handle potential events and set appropriate flags
    bool previousSensorError = systemState.sensorError;
    systemState.sensorError = !currentReading.valid;
    
    if (systemState.sensorError && !previousSensorError) {
        Serial.println("[EVENT] Sensor error detected!");
    } else if (!systemState.sensorError && previousSensorError) {
        Serial.println("[EVENT] Sensor error cleared");
    }

    static bool lastConfigCommandReceived = false;
    if (systemState.configCommandReceived && !lastConfigCommandReceived) {
        Serial.println("[EVENT] Button pressed - config command received");
    }
    lastConfigCommandReceived = systemState.configCommandReceived;

    // Check for 5-second button hold to toggle silence in EMERGENCY state
    if (buttonCurrentlyPressed && systemState.currentState == EMERGENCY) {
        unsigned long holdDuration = millis() - buttonPressStartTime;
        if (holdDuration >= 5000) {
            static bool silenceToggleHandled = false;
            if (!silenceToggleHandled) {
                // Toggle silence state
                systemState.notificationsSilenced = !systemState.notificationsSilenced;
                silenceToggleHandled = true;
                
                if (systemState.notificationsSilenced) {
                    Serial.println("[EVENT] Emergency notifications SILENCED by button hold");
                    
                    // Send confirmation notification
                    const char* silenceMessage = "Boat Monitor: Emergency alerts have been temporarily silenced";
                    if (!sms.send(silenceMessage)) {
                        Serial.println("[SMS] Failed to send silence confirmation SMS");
                    }
                    if (!discord.send(silenceMessage)) {
                        Serial.println("[Discord] Failed to send silence confirmation to Discord");
                    }
                } else {
                    Serial.println("[EVENT] Emergency notifications RE-ENABLED by button hold");
                }
            }
            // Reset flag when button is released
            if (!buttonCurrentlyPressed) {
                silenceToggleHandled = false;
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
            Serial.printf("[EVENT] Tier 1 Emergency conditions detected! level=%.2f cm (threshold=%.2f cm)\n",
                          currentReading.level_cm, emergencyThreshold);
        }
    } else {
        systemState.emergencyConditions = false;
        if (previousEmergencyConditions) {
            systemState.emergencyConditionsFalseTime = millis(); // Update timer when conditions CLEAR
            Serial.printf("[EVENT] Tier 1 Emergency conditions cleared. level=%.2f cm\n", currentReading.level_cm);
        }
    }
    
    // Check Tier 2 urgent emergency conditions (horn alarm)
    bool previousUrgentEmergencyConditions = systemState.urgentEmergencyConditions;
    float urgentEmergencyThreshold = configServer->getUrgentEmergencyWaterLevel();
    if (currentReading.level_cm >= urgentEmergencyThreshold) {
        systemState.urgentEmergencyConditions = true;
        if (!previousUrgentEmergencyConditions) {
            Serial.printf("[EVENT] Tier 2 URGENT Emergency conditions detected! level=%.2f cm (threshold=%.2f cm)\n",
                          currentReading.level_cm, urgentEmergencyThreshold);
        }
    } else {
        systemState.urgentEmergencyConditions = false;
        if (previousUrgentEmergencyConditions) {
            Serial.printf("[EVENT] Tier 2 URGENT Emergency conditions cleared. level=%.2f cm\n", currentReading.level_cm);
        }
    }


    // Handle state-specific operations
    
    switch (systemState.currentState) {
        case ERROR:
            if (systemState.sensorError == false) {
                Serial.printf("[STATE] Transitioning from %s to NORMAL (sensor recovered)\n", stateToString(systemState.currentState));
                systemState.currentState = NORMAL;
                systemState.lastStateChangeTime = millis();
            }
            else if (systemState.configCommandReceived) {
                Serial.printf("[STATE] Transitioning from %s to CONFIG (button pressed)\n", stateToString(systemState.currentState));
                systemState.currentState = CONFIG;
                systemState.lastStateChangeTime = millis();
            }
            
            // send message about sensor failure?
            break;
        case NORMAL:
            if (systemState.sensorError == true) {
                Serial.printf("[STATE] Transitioning from %s to ERROR (sensor error detected)\n", stateToString(systemState.currentState));
                systemState.currentState = ERROR;
                systemState.lastStateChangeTime = millis();
            }
            else if (systemState.emergencyConditions && 
                (millis() - systemState.emergencyConditionsTrueTime) >= EMERGENCY_TIMEOUT_MS) {
                Serial.printf("[STATE] Transitioning from %s to EMERGENCY (water level=%.2f cm)\n", 
                              stateToString(systemState.currentState), currentReading.level_cm);
                systemState.currentState = EMERGENCY;
                systemState.lastStateChangeTime = millis();
            }
            else if (systemState.configCommandReceived) {
                Serial.printf("[STATE] Transitioning from %s to CONFIG (button pressed)\n", stateToString(systemState.currentState));
                systemState.currentState = CONFIG;
                systemState.lastStateChangeTime = millis();
            } 
            break;
        case CONFIG:
            if (!configServer->isSetupModeActive()) {
                // Start setup mode (configServer already exists from setup())
                Serial.println("[STATE] Starting configuration server mode");
                configServer->startSetupMode();
            }
            else {
                configServer->handleClient();
                
                // Check if setup mode has ended (timeout or manual stop)
                if (!configServer->isSetupModeActive()) {
                    Serial.printf("[STATE] Transitioning from %s to NORMAL (config completed)\n", stateToString(systemState.currentState));
                    systemState.currentState = NORMAL;
                    systemState.configCommandReceived = false;
                    systemState.lastStateChangeTime = millis();
                }
            }
            break;
        case EMERGENCY:
            
            if (systemState.emergencyConditions == false && (millis() - systemState.emergencyConditionsFalseTime) >= EMERGENCY_TIMEOUT_MS) {
                Serial.printf("[STATE] Transitioning from %s to NORMAL (emergency cleared)\n", stateToString(systemState.currentState));
                systemState.currentState = NORMAL;
                systemState.lastStateChangeTime = millis();
                // Ensure horn is OFF when exiting EMERGENCY state
                digitalWrite(ALERT_PIN, LOW);
                systemState.hornCurrentlyOn = false;
                // Auto-clear silence flag when emergency ends (safety feature)
                if (systemState.notificationsSilenced) {
                    Serial.println("[STATE] Auto-clearing notification silence (emergency cleared)");
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
                        char emergMessageBuf[120];
                        if (systemState.urgentEmergencyConditions) {
                            snprintf(emergMessageBuf, sizeof(emergMessageBuf), "Boat Monitor URGENT Alert: Critical Level %.2f cm - HORN ACTIVATED!", currentReading.level_cm);
                        } else {
                            snprintf(emergMessageBuf, sizeof(emergMessageBuf), "Boat Monitor Alert: Emergency Level %.2f cm", currentReading.level_cm);
                        }
                        Serial.printf("[STATE] EMERGENCY: Sending alert message: %s\n", emergMessageBuf);

                        if (!sms.send(emergMessageBuf)){
                            Serial.println("[SMS] Emergency SMS failed to send");
                        }

                        if (!discord.send(emergMessageBuf)){
                            Serial.println("[Discord] Emergency Discord webhook failed to send");
                        }
                    } else {
                        Serial.println("[STATE] EMERGENCY: Notifications silenced, skipping alert message");
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
                        Serial.printf("[HORN] Horn %s\n", systemState.hornCurrentlyOn ? "ON" : "OFF");
                    }
                } else {
                    // Not urgent emergency or notifications silenced - ensure horn is OFF
                    if (systemState.hornCurrentlyOn) {
                        digitalWrite(ALERT_PIN, LOW);
                        systemState.hornCurrentlyOn = false;
                        if (systemState.notificationsSilenced) {
                            Serial.println("[HORN] Horn deactivated (notifications silenced)");
                        } else {
                            Serial.println("[HORN] Horn deactivated (Tier 2 conditions cleared)");
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
        Serial.printf("[STATUS] State=%s, WaterLevel=%.2f cm, SensorError=%d, EmergencyConditions=%d\n",
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
