#include <Arduino.h>
#include <WiFi.h>
#include "TimeManagement.h"
#include "WiFiManager.h"
#include "WiFiConfig.h"
#include "LightCode.h"
#include "WaterPressureSensor.h"
#include "SendSMS.h"

// Forward declaration
void handleButtonPress();

// System States
enum State {
    ERROR,
    NORMAL,
    EMERGENCY,
    CONFIG
};

struct SystemState {
    State currentState;

    // Event flags
    bool emergencyConditions;
    bool sensorError;
    bool configCommandReceived;
    
    // Timers
    uint32_t lastStateChangeTime;
    uint32_t emergencyStateFalseTime;
    uint32_t lastEmergencyMessageTime;

};

SystemState systemState; // Tracks current device state

const int BUTTON_PIN = 23; // GPIO 23
const int ALERT_PIN = 13; // GPIO 13
bool buttonPressed = false;
int EMERGENCY_WATER_LEVEL_CM = 15;
int EMERGENCY_WATER_CM_PER_HR = 5;
const int SENSOR_PIN = 32; // Water sensor analog pin ADC1 because wifi is required
const int EMERGENCY_TIMEOUT_MS = 60000;
const int EMERGENCY_MESSAGE_TIMEOUT_MS = 60000 * 30; // 30mins

// Create easier references to the singleton objects
WiFiConfig* wifiConfig = nullptr;
LightCode light(LED_BUILTIN);
TimeManagement& rtc = TimeManagement::getInstance(); 
WiFiManager& wifiMgr = WiFiManager::getInstance();
WaterPressureSensor waterSensor(SENSOR_PIN);
SendSMS sms;

void setup() {
    Serial.begin(115200);
    light.setPattern(PATTERN_SLOW_BLINK);
    waterSensor.init();
    

    pinMode(ALERT_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(BUTTON_PIN, handleButtonPress, FALLING);

    // Initialize WiFiManager
    wifiMgr.begin();
    
    // Check if we have stored credentials
    // If not, or if user holds a button, start setup mode
    std::vector<char*> ssids = wifiMgr.getStoredSSIDs();

    if (ssids.empty()) {
        systemState.currentState = CONFIG;
    } else {
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

    SensorReading currentReading = waterSensor.readLevel();

    // Handle potential events and set appropriate flags
    systemState.sensorError = waterSensor.isHealthy();

    if (currentReading.level_cm >= EMERGENCY_WATER_LEVEL_CM ||
        waterSensor.getRollingRateOfChange() >= EMERGENCY_WATER_CM_PER_HR) {
        systemState.emergencyConditions = true;
    } else {
        systemState.emergencyConditions = false;
    }


    // Handle state-specific operations
    switch (systemState.currentState) {
        case ERROR:
            if (systemState.sensorError == false) {
                systemState.currentState = NORMAL;
                systemState.lastStateChangeTime = millis();
            }
            // send message about sensor failure?
            break;
        case NORMAL:
            if (systemState.sensorError == true) {
                systemState.currentState = ERROR;
                systemState.lastStateChangeTime = millis();
            }
            else if (systemState.emergencyConditions && 
                (millis() - systemState.emergencyStateFalseTime) >= EMERGENCY_TIMEOUT_MS) {
                systemState.currentState = EMERGENCY;
                systemState.lastStateChangeTime = millis();
            }
            else if (systemState.configCommandReceived) {
                systemState.currentState = CONFIG;
                systemState.lastStateChangeTime = millis();
            } 
            break;
        case CONFIG:
            if (!wifiConfig) {
                // This should only be executed once per state change
                wifiConfig = new WiFiConfig(&waterSensor);
                wifiConfig->startSetupMode();
            }
            else if (wifiConfig->isSetupModeActive()) {
                wifiConfig->handleClient();
            } else {
                systemState.currentState = NORMAL;
                systemState.configCommandReceived = false;
                systemState.lastStateChangeTime = millis();
            }
            break;
        case EMERGENCY:
            if (systemState.emergencyConditions = false && (millis() - systemState.emergencyStateFalseTime) >= EMERGENCY_TIMEOUT_MS) {
                systemState.currentState = NORMAL;
                systemState.emergencyStateFalseTime = millis();
                systemState.lastStateChangeTime = millis();
            } 

            if (millis() - systemState.lastEmergencyMessageTime >= EMERGENCY_MESSAGE_TIMEOUT_MS) {
                char emergMessageBuf[120];
                snprintf(emergMessageBuf, sizeof(emergMessageBuf), "Boat Monitor Alert: Emergency Level %.2f cm", currentReading.level_cm);
                systemState.lastEmergencyMessageTime = millis();
                sms.send(emergMessageBuf);
            }
            break;
    }
}


/*
    Button press interrupt handler
*/
void handleButtonPress() {
    static unsigned long lastPress = 0;
    unsigned long now = millis();
    if (now - lastPress <= 50) return; // Debounce 50ms 
    lastPress = now;
    systemState.configCommandReceived = true;
}
