#pragma once
class MQTTService;

/*
    MQTTService.h

    MQTT transport layer for the ESP32 boat monitoring system.

    Immediate use: log sink (replaces Discord webhook in Logger).
    Future use: Home Assistant integration via generic publish/subscribe API.

    Features:
    - Persistent connection to a configurable Mosquitto broker (NVS-backed).
    - Non-blocking reconnect with exponential backoff.
    - Internal ring-buffer queue for log messages so LOG_* macros never block
      the main loop (critical during EMERGENCY alert-pulsing state).
    - LWT on <baseTopic>/availability ("online"/"offline") for HA availability.
    - Generic publish() and subscribe() for future HA sensor/command topics.
    - Subscriber fan-out (PubSubClient supports only one global callback).
    - Optional username/password auth; empty strings = anonymous.
*/

#ifndef UNIT_TESTING

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <freertos/portmacro.h>
#include <functional>
#include <vector>

class MQTTService {
public:
    MQTTService();

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    // Load NVS config, configure PubSubClient. Does NOT connect — loop() does.
    void begin();

    // Must be called from main loop. Drains log queue, manages reconnect, polls client.
    void loop();

    bool isConnected();

    // Re-read NVS and trigger a fresh connect. Call after any update*() method.
    void reloadConfig();

    // -------------------------------------------------------------------------
    // Publish API — general purpose (use for HA sensor/state/discovery topics)
    // -------------------------------------------------------------------------

    // Publish to an arbitrary topic. Returns false if not connected.
    bool publish(const char* topic, const char* payload, bool retained = false);

    // Enqueue a log message for async delivery to <baseTopic>/log.
    // Safe to call from LOG_* macros — never blocks or touches the wire.
    bool publishLog(const char* message);

    // Publish structured sensor telemetry (JSON) to <baseTopic>/telemetry.
    // Retained by default so a freshly-connected consumer (Grafana/Telegraf,
    // Home Assistant) immediately sees the last known reading.
    bool publishTelemetry(const char* json, bool retained = true);

    // -------------------------------------------------------------------------
    // Subscribe API — forward-looking (HA commands, config-over-MQTT, etc.)
    // -------------------------------------------------------------------------

    typedef std::function<void(const char* topic, const char* payload)> MessageCallback;

    // Subscribe to a topic filter. Callback is fanned out in loop().
    bool subscribe(const char* topic, MessageCallback cb);

    // -------------------------------------------------------------------------
    // NVS-backed configuration (mirrors SendDiscord / SendSMS pattern)
    // -------------------------------------------------------------------------

    void updateBroker(const char* host, uint16_t port);
    int  getBroker(char* hostBuf, size_t hostSize, uint16_t* portOut);

    // Stores the username always. An empty/null pass leaves the stored password
    // unchanged ("leave blank to keep current") so an unrelated config save
    // cannot wipe credentials the UI never re-populates.
    void updateCredentials(const char* user, const char* pass);
    int  getUsername(char* outBuf, size_t bufferSize);

    void updateBaseTopic(const char* topic);
    int  getBaseTopic(char* outBuf, size_t bufferSize);

    // Enable TLS (port 8883) vs. plaintext (port 1883). When enabled the broker
    // certificate is validated against the bundled CA (Let's Encrypt roots).
    // Call reloadConfig() afterwards to apply.
    void updateTls(bool enabled);
    bool getTls() const { return useTls; }

    bool hasBrokerConfig();

    // -------------------------------------------------------------------------
    // Status
    // -------------------------------------------------------------------------

    uint32_t getLogsDropped() const { return logsDropped; }

private:
    Preferences      preferences;
    WiFiClient       wifiClient;     // plaintext transport (port 1883)
    WiFiClientSecure secureClient;   // TLS transport (port 8883)
    PubSubClient     client;

    // Cached config — fixed char[] to avoid long-lived String heap fragmentation
    char     brokerHost[64];
    uint16_t brokerPort;
    char     username[32];
    char     password[64];
    char     clientId[24];           // "boat-XXXXXX" derived from MAC
    char     baseTopic[64];          // default "boat/<6hex-mac>"
    char     logTopic[80];           // baseTopic + "/log"
    char     availabilityTopic[80];  // baseTopic + "/availability" (LWT)
    char     telemetryTopic[80];     // baseTopic + "/telemetry" (structured JSON)
    bool     useTls;                 // true = TLS (8883), false = plaintext (1883)

    bool m_initialized;
    bool cachedBrokerConfigExists;   // Cached result to avoid NVS check every loop
    bool recheckBrokerConfig;        // Flag to recheck after config changes

    // Non-blocking reconnect state
    uint32_t lastReconnectAttempt;
    uint32_t reconnectBackoffMs;     // 5s → 10s → 20s → 30s cap

    // Outbound log ring buffer (~4 KB RAM: 16 × 256)
    // logQueueMux guards the ring buffer across cores (NotificationWorker on Core 0,
    // main loop on Core 1 both call LOG_* → publishLog).
    static constexpr size_t LOG_QUEUE_SIZE = 16;
    static constexpr size_t LOG_MSG_MAX    = 256;
    char         logQueue[LOG_QUEUE_SIZE][LOG_MSG_MAX];
    uint8_t      logQueueHead;
    uint8_t      logQueueTail;
    uint8_t      logQueueCount;
    uint32_t     logsDropped;
    portMUX_TYPE logQueueMux;

    // Subscriber fan-out (PubSubClient only supports one global callback)
    struct Subscription {
        String          filter;
        MessageCallback cb;
    };
    std::vector<Subscription> subscriptions;

    // Reentrancy guard — prevents MQTTService internal code (e.g. reconnect
    // log messages) from recursing back into publish() and corrupting client state.
    bool inMqttCall;

    // Static trampoline for PubSubClient's single callback slot
    static MQTTService* s_instance;
    static void onMessageTrampoline(char* topic, byte* payload, unsigned int length);
    void        dispatchMessage(const char* topic, const char* payload);

    void tryReconnect();
    void drainLogQueue();
    void buildClientIdAndDefaults();  // derive clientId + default baseTopic from MAC
    void readNvs();
    void applyServerConfig();
};

// Global pointer used by Logger (defined in main.cpp, declared here for Logger.h)
extern MQTTService* g_mqtt;

// Called by Logger macros — enqueues a message; never blocks.
void sendMqttLog(const char* message);

#endif // UNIT_TESTING
