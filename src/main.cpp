#include <Arduino.h>
#include <WiFi.h>
#include <nvs_flash.h>
#include <esp_task_wdt.h>
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
#include "NotificationWorker.h"
#include "StateMachine.h"
#include "SettingsStore.h"
#include "Version.h"

// Forward declarations
void handleButtonPress();

// The canonical state machine context. All state lives here; loop() is a thin
// dispatcher that calls updateStateMachine(), reads the output, and executes
// side effects (horn GPIO, MQTT enqueue, LED pattern).
StateMachineContext smCtx;

// Status logging
static constexpr uint32_t STATUS_LOG_INTERVAL_MS = 10000; // Log status every 10 seconds
uint32_t lastStatusLogTime = 0;

// Structured telemetry publishing (MQTT <baseTopic>/telemetry, for Grafana/HA)
static constexpr uint32_t TELEMETRY_INTERVAL_MS = 60000; // Publish telemetry every 60 seconds
uint32_t lastTelemetryTime = 0;

static constexpr int BUTTON_PIN = 23; // GPIO
static constexpr int ALERT_PIN = 19; // GPIO
static constexpr int SENSOR_PIN = 32; // Water sensor analog pin ADC1 because wifi is required
#ifdef ENABLE_MOCK_MODE
static constexpr bool USE_MOCK = true;
#else
static constexpr bool USE_MOCK = false;
#endif
static constexpr int LIGHT_PIN = 12;

// Task watchdog: tightened now that checkForUpdates() runs off-loop on an OTA task.
// The longest blocking call remaining in loop() is <1 s, so 10 s gives ample margin.
static constexpr uint32_t WDT_TIMEOUT_S = 10;

volatile bool buttonPressed = false;
volatile unsigned long lastButtonPress = 0;
volatile unsigned long buttonPressStartTime = 0;
volatile bool buttonCurrentlyPressed = false;
volatile bool emergencyLongHoldDetected = false; // Flag for 5-second hold in emergency

// Message tracing - unique ID for each notification
static uint32_t messageTraceId = 0;

// Create easier references to the singleton objects
SettingsStore settingsStore;
ConfigServer* configServer = nullptr;
OTAManager* otaManager = nullptr;
NotificationWorker notifier;
LightCode light(LIGHT_PIN);
TimeManagement& rtc = TimeManagement::getInstance();
WiFiManager& wifiMgr = WiFiManager::getInstance();
WaterPressureSensor waterSensor(USE_MOCK); // false = use real sensor, not mock data
SendSMS sms;
SendDiscord discord;
MQTTService mqtt;

// Logger publishes logs via MQTT; Discord kept for OTA / direct emergency sends
SendDiscord* g_discord = &discord;

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

    // Load alarm threshold settings from NVS (before ConfigServer construction)
    settingsStore.load();

    // Initialize state machine context with current time
    smCtx.lastStateChangeTime           = millis();
    smCtx.emergencyConditionsTrueTime   = millis();
    smCtx.emergencyConditionsFalseTime  = millis();
    smCtx.lastEmergencyMessageTime      = 0;
    smCtx.lastHornToggleTime            = 0;
    smCtx.sensorErrorTrueTime           = millis();
    smCtx.lastSensorErrorNotifyTime     = 0;

    // Initialize MQTT early so all subsequent LOG_* calls can be queued for delivery
    g_mqtt = &mqtt;
    mqtt.begin();
    LOG_SETUP("[SETUP] MQTTService initialized");

    // Start notification worker on Core 0 — SMS/Discord HTTP calls no longer block Core 1
    notifier.begin(&sms, &discord, USE_MOCK);
    LOG_SETUP("[SETUP] NotificationWorker started on Core 0%s", USE_MOCK ? " (dry-run mode)" : "");

    // Initialize OTAManager early to check for rollback/first boot after update
    otaManager = new OTAManager(&notifier);
    otaManager->begin();
    LOG_SETUP("[SETUP] OTAManager initialized - version %s", FIRMWARE_VERSION);

    // Initialize ConfigServer early to load calibration from NVS
    // This ensures saved calibration is applied before first sensor reading
    configServer = new ConfigServer(&waterSensor, &sms, &discord, otaManager, &mqtt, &settingsStore);
    LOG_SETUP("[SETUP] ConfigServer initialized - calibration loaded from NVS");

    // Print unique device AP password for easy access
    LOG_SETUP("========================================");
    LOG_SETUP("Device Configuration Access Point:");
    LOG_SETUP("  SSID: ESP32-BilgeRise-Setup");
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
        smCtx.currentState = CONFIG;
        LOG_STATE("[STATE] Initial state: %s (no WiFi credentials found)", stateToString(smCtx.currentState));
        light.setPattern(PATTERN_SLOW_BLINK); // CONFIG state pattern
    } else {
        smCtx.currentState = NORMAL;
        LOG_STATE("[STATE] Initial state: %s", stateToString(smCtx.currentState));
        light.setPattern(PATTERN_OFF); // NORMAL state pattern
        LOG_SETUP("WiFi credentials found, connecting...");
        delay(2000);

        if (wifiMgr.isConnected()) {
            LOG_SETUP("IP address: %s", WiFi.localIP().toString().c_str());
        }
    }

    // Enable the task watchdog last, once boot-time blocking work (WiFi connect)
    // is done. panic=true reboots the device if loop() ever stops feeding it.
    esp_task_wdt_init(WDT_TIMEOUT_S, true);
    esp_task_wdt_add(NULL); // Register the loop task (setup/loop share one task)
    LOG_SETUP("[SETUP] Task watchdog armed (%us timeout)", WDT_TIMEOUT_S);
}

void loop() {

    esp_task_wdt_reset(); // Feed the watchdog; a stalled loop will trigger reboot

    mqtt.loop();
    rtc.sync();
    light.update();
    wifiMgr.maintainConnection();

    // OTA version checks now run on their own Core 0 task (see OTAManager::begin()),
    // so loop() only needs to handle auto-install when a check has already found an update.
    if (otaManager && smCtx.currentState != CONFIG && smCtx.currentState != EMERGENCY) {
        otaManager->loopInstallOnly();
    }

    // Monitor WiFi connection status for critical events
    static bool wasWiFiConnected = false;
    bool isWiFiConnected = wifiMgr.isConnected();

    if (wasWiFiConnected && !isWiFiConnected) {
        LOG_EVENT("[WIFI] Connection lost - internet disconnected");
        if (smCtx.currentState == EMERGENCY) {
            LOG_EVENT("[WIFI_EMERGENCY] WiFi lost during EMERGENCY state - emergency notifications may be delayed!");
        }
        // Update LED if in NORMAL state to show WiFi disconnection
        if (smCtx.currentState == NORMAL) {
            light.setPattern(PATTERN_DOUBLE_BLINK);
            LOG_DEBUG("[LIGHT] WiFi disconnected - switching to double blink pattern");
        }
    } else if (!wasWiFiConnected && isWiFiConnected) {
        LOG_EVENT("[WIFI] Connection restored - internet connected");
        // Update LED if in NORMAL state to show WiFi reconnection
        if (smCtx.currentState == NORMAL) {
            light.setPattern(PATTERN_OFF);
            LOG_DEBUG("[LIGHT] WiFi reconnected - switching to off pattern");
        }
    }
    wasWiFiConnected = isWiFiConnected;

    // Track state before processing to detect changes
    State previousState = smCtx.currentState;

    // Refresh threshold config from SettingsStore on every loop iteration so
    // settings changed via the web UI take effect without a reboot.
    // SettingsStore::get() returns the in-RAM copy — no NVS I/O on this path.
    {
        const SettingsValues& sv = settingsStore.get();
        smCtx.emergencyWaterLevel_cm       = sv.emergencyWaterLevel_cm;
        smCtx.urgentEmergencyWaterLevel_cm = sv.urgentEmergencyWaterLevel_cm;
        smCtx.emergencyNotifFreq_ms        = sv.emergencyNotifFreq_ms;
        smCtx.hornOnDuration_ms            = sv.hornOnDuration_ms;
        smCtx.hornOffDuration_ms           = sv.hornOffDuration_ms;
    }

    SensorReading currentReading = waterSensor.readLevel();

    // Check for 5-second button hold to toggle silence in EMERGENCY state
    static bool silenceToggleHandled = false;
    static bool lastButtonState = false;

    if (emergencyLongHoldDetected && smCtx.currentState == EMERGENCY && !silenceToggleHandled) {
        silenceToggleHandled = true;
        emergencyLongHoldDetected = false;
        StateMachineOutput silenceOut = handleSilenceToggle(smCtx);
        if (silenceOut.sendSilenceConfirmation) {
            LOG_EVENT("[EVENT] Emergency notifications SILENCED by button hold");
            messageTraceId++;
            char buf[100];
            snprintf(buf, sizeof(buf), "[MSG:%u] BilgeRise: Emergency alerts silenced", messageTraceId);
            notifier.enqueue(buf);
        } else if (silenceOut.sendUnsilenceConfirmation) {
            LOG_EVENT("[EVENT] Emergency notifications RE-ENABLED by button hold - WiFi: %d", wifiMgr.isConnected());
        }
        if (silenceOut.setHornState) {
            digitalWrite(ALERT_PIN, silenceOut.hornOn ? HIGH : LOW);
        }
    }

    // Reset silence toggle flag when button is released
    if (lastButtonState && !buttonCurrentlyPressed) {
        silenceToggleHandled = false;
    }
    lastButtonState = buttonCurrentlyPressed;

    // Handle CONFIG state server calls (must happen before updateStateMachine so
    // configCommandReceived is consumed correctly)
    if (smCtx.currentState == CONFIG) {
        if (!configServer->isSetupModeActive()) {
            LOG_STATE("[STATE] Starting configuration server mode");
            configServer->startSetupMode();
        } else {
            configServer->handleClient();
        }
    }

    // Check for I2C bus unrecoverable — one-shot alert
    static bool busUnrecoverableNotified = false;
    if (!busUnrecoverableNotified && waterSensor.isBusUnrecoverable()) {
        busUnrecoverableNotified = true;
        LOG_CRITICAL("[SENSOR] I2C bus permanently unrecoverable after %d attempts", BUS_RECOVERY_MAX_ATTEMPTS);
        messageTraceId++;
        char busMsg[120];
        snprintf(busMsg, sizeof(busMsg), "[MSG:%u] BilgeRise: I2C sensor bus unrecoverable. Device requires inspection.", messageTraceId);
        notifier.enqueue(busMsg);
    }

    // Build the sensor reading adapter for the state machine
    StateMachineSensorReading smReading;
    smReading.valid    = currentReading.valid;
    smReading.level_cm = currentReading.level_cm;

    // Get rate-of-change for emergency messages (NaN if not enough history)
    float rateOfChange = waterSensor.getRateOfChange_cm30min();

    // Config server active flag for state machine
    bool configActive = configServer->isSetupModeActive();

    // Run the unified state machine
    StateMachineOutput out = updateStateMachine(smCtx, smReading, millis(), rateOfChange, configActive);

    // ------------------------------------------------------------------
    // Execute side effects from state machine output
    // ------------------------------------------------------------------

    // Horn GPIO
    if (out.setHornState) {
        digitalWrite(ALERT_PIN, out.hornOn ? HIGH : LOW);
        LOG_DEBUG("[HORN] Horn %s", out.hornOn ? "ON" : "OFF");
    }

    // Sensor recovery notification
    if (out.sendSensorRecoveryNotification) {
        LOG_EVENT("[EVENT] Sensor error cleared");
        messageTraceId++;
        char recoverMsg[120];
        snprintf(recoverMsg, sizeof(recoverMsg),
                 "[MSG:%u] BilgeRise: Sensor recovered — water-level monitoring restored.", messageTraceId);
        notifier.enqueue(recoverMsg);
    }

    // Sustained sensor failure notification (skip if I2C bus is unrecoverable — separate alert)
    if (out.sendSustainedSensorFailureNotification && !waterSensor.isBusUnrecoverable()) {
        LOG_CRITICAL("[SENSOR] Sustained sensor failure (%us) — notifying owner", out.sensorDownSeconds);
        messageTraceId++;
        char failMsg[150];
        snprintf(failMsg, sizeof(failMsg),
                 "[MSG:%u] BilgeRise: Sensor failure — no valid reading for %us. Flood detection is OFFLINE; device needs inspection.",
                 messageTraceId, out.sensorDownSeconds);
        notifier.enqueue(failMsg);
    }

    // Emergency notification (latest-wins mailbox for coalescing during WiFi outages)
    if (out.sendEmergencyNotification) {
        messageTraceId++;
        char ratePart[28] = "";
        if (!out.sensorFaultActive && !isnan(out.rateOfChange_cm30min)) {
            snprintf(ratePart, sizeof(ratePart), " (%+.1f cm/30min)", out.rateOfChange_cm30min);
        }
        const char* sensorNote = out.sensorFaultActive ? " — SENSOR FAULT (level stale)" : "";
        char emergMessageBuf[180];
        if (smCtx.urgentEmergencyConditions) {
            snprintf(emergMessageBuf, sizeof(emergMessageBuf),
                     "[MSG:%u] BilgeRise URGENT Alert: Tier 2 Emergency Level %.2f cm%s%s",
                     messageTraceId, out.displayLevel_cm, ratePart, sensorNote);
        } else {
            snprintf(emergMessageBuf, sizeof(emergMessageBuf),
                     "[MSG:%u] BilgeRise Alert: Emergency Level %.2f cm%s%s",
                     messageTraceId, out.displayLevel_cm, ratePart, sensorNote);
        }
        LOG_EVENT("[STATE] EMERGENCY: Sending alert message: %s", emergMessageBuf);
        // Latest-wins mailbox: replaces any older unsent snapshot during WiFi outage
        notifier.enqueueEmergency(emergMessageBuf);
    }

    // State change side effects
    if (out.stateChanged) {
        LOG_STATE("[STATE] Transitioning to %s", stateToString(smCtx.currentState));

        if (smCtx.currentState == EMERGENCY && !previousState) {
            // Log transition details
            LOG_EVENT("[STATE] Transitioning to EMERGENCY state - WiFi connected: %d, IP: %s, water level: %.2f cm",
                      wifiMgr.isConnected(), WiFi.localIP().toString().c_str(), currentReading.level_cm);
        }

        // Update LED pattern on state change
        switch (smCtx.currentState) {
            case NORMAL:
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

        // When config mode ends, transition was already applied by SM
        if (smCtx.currentState == NORMAL && previousState == CONFIG) {
            LOG_STATE("[STATE] Config mode ended - returning to NORMAL");
        }
    }

    // Periodic status logging
    if (millis() - lastStatusLogTime >= STATUS_LOG_INTERVAL_MS) {
        LOG_STATUS("[STATUS] State=%s, WaterLevel=%.2f cm, SensorError=%d, EmergencyConditions=%d",
                      stateToString(smCtx.currentState),
                      currentReading.level_cm,
                      smCtx.sensorError,
                      smCtx.emergencyConditions);
        LOG_STATUS("[WIFI] Connected=%d, RSSI=%d dBm",
                      wifiMgr.isConnected(), wifiMgr.getRSSI());
        LOG_STATUS("[HEAP] Free=%u, MinFree=%u, MaxBlock=%u",
                      ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
        LOG_STATUS("[NOTIFIER] Pending=%u, Dropped=%u",
                      notifier.getPendingCount(), notifier.getDropCount());
        lastStatusLogTime = millis();
    }

    // Periodic structured telemetry — numeric JSON for time-series consumers
    // (Grafana via Telegraf/InfluxDB, Home Assistant). Retained so a freshly
    // connected consumer immediately sees the last reading. publishTelemetry()
    // is a no-op when MQTT is disconnected, so this never blocks the loop.
    if (millis() - lastTelemetryTime >= TELEMETRY_INTERVAL_MS) {
        // ArduinoJson builder — NaN-proof by construction (item A3)
        StaticJsonDocument<256> doc;
        if (isnan(currentReading.level_cm)) {
            doc["level_cm"] = nullptr;
        } else {
            doc["level_cm"] = serialized(String(currentReading.level_cm, 2));
        }
        float rate = waterSensor.getRateOfChange_cm30min();
        if (isnan(rate)) {
            doc["rate_cm_30min"] = nullptr;
        } else {
            doc["rate_cm_30min"] = serialized(String(rate, 2));
        }
        doc["state"]        = stateToString(smCtx.currentState);
        doc["sensor_error"] = smCtx.sensorError;
        doc["valid"]        = currentReading.valid;
        doc["rssi"]         = wifiMgr.getRSSI();

        char payload[160];
        serializeJson(doc, payload, sizeof(payload));
        mqtt.publishTelemetry(payload);
        lastTelemetryTime = millis();
    }

    // Yield 10 ms at end of loop so the FreeRTOS idle task (light-sleep) can run.
    // All inputs are interrupt-driven (button) or time-based (>=100 ms granularity),
    // so this delay is safe and does not affect responsiveness.
    vTaskDelay(pdMS_TO_TICKS(10));
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

        // Only process config command if NOT in emergency
        // If in emergency, the hold duration will be checked for silence toggle
        if (smCtx.currentState != EMERGENCY) {
            // Short press (< 5 seconds) = config command (only when not in emergency)
            if (holdDuration < 5000) {
                smCtx.configCommandReceived = true;
            }
        } else if (holdDuration >= 5000) {
            // Long press during emergency = silence toggle signal for main loop
            emergencyLongHoldDetected = true;
        }
    }
}
