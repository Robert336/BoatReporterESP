#ifndef UNIT_TESTING

#include "MQTTService.h"
#include <WiFi.h>

static constexpr const char* MQTT_PREFS_NAMESPACE = "mqtt";
static constexpr uint16_t    DEFAULT_MQTT_PORT     = 1883;
static constexpr uint32_t    RECONNECT_INITIAL_MS  = 5000;
static constexpr uint32_t    RECONNECT_MAX_MS      = 30000;
static constexpr uint32_t    DRAIN_BUDGET_MS        = 50;   // max time to spend publishing per loop()

MQTTService* MQTTService::s_instance = nullptr;


// =============================================================================
// Constructor
// =============================================================================

MQTTService::MQTTService()
    : client(wifiClient)
    , brokerPort(DEFAULT_MQTT_PORT)
    , m_initialized(false)
    , cachedBrokerConfigExists(false)
    , recheckBrokerConfig(true)
    , lastReconnectAttempt(0)
    , reconnectBackoffMs(RECONNECT_INITIAL_MS)
    , logQueueHead(0)
    , logQueueTail(0)
    , logQueueCount(0)
    , logsDropped(0)
    , logQueueMux(portMUX_INITIALIZER_UNLOCKED)
    , inMqttCall(false)
{
    brokerHost[0]         = '\0';
    username[0]           = '\0';
    password[0]           = '\0';
    clientId[0]           = '\0';
    baseTopic[0]          = '\0';
    logTopic[0]           = '\0';
    availabilityTopic[0]  = '\0';
}


// =============================================================================
// Lifecycle
// =============================================================================

void MQTTService::begin() {
    buildClientIdAndDefaults();
    
    // Hardcoded MQTT configuration for logging
    strcpy(brokerHost, "10.0.0.205");
    brokerPort = 1883;
    username[0] = '\0';  // No username
    password[0] = '\0';  // No password
    
    applyServerConfig();
    s_instance    = this;
    m_initialized = true;
    // Force broker config to be considered available
    cachedBrokerConfigExists = true;
    recheckBrokerConfig = false;
}

void MQTTService::loop() {
    if (!m_initialized) return;
    if (!WiFi.isConnected()) return;
    
    // Recheck broker config if flag is set (after begin() or config update)
    if (recheckBrokerConfig) {
        cachedBrokerConfigExists = hasBrokerConfig();
        recheckBrokerConfig = false;
    }
    
    if (!cachedBrokerConfigExists) return;

    if (!client.connected()) {
        uint32_t now = millis();
        if (now - lastReconnectAttempt >= reconnectBackoffMs) {
            tryReconnect();
        }
    } else {
        client.loop();
        drainLogQueue();
    }
}

bool MQTTService::isConnected() {
    return m_initialized && client.connected();
}

void MQTTService::reloadConfig() {
    if (client.connected()) {
        client.disconnect();
    }
    readNvs();
    applyServerConfig();
    // Reset backoff so reconnect happens quickly after a config change
    lastReconnectAttempt = 0;
    reconnectBackoffMs   = RECONNECT_INITIAL_MS;
    // Mark for recheck on next loop
    recheckBrokerConfig = true;
}


// =============================================================================
// Publish
// =============================================================================

bool MQTTService::publish(const char* topic, const char* payload, bool retained) {
    if (!m_initialized || inMqttCall) return false;
    if (!client.connected()) return false;
    inMqttCall = true;
    bool ok = client.publish(topic, payload, retained);
    inMqttCall = false;
    return ok;
}

bool MQTTService::publishLog(const char* message) {
    if (!m_initialized || !message) return false;

    portENTER_CRITICAL(&logQueueMux);
    if (logQueueCount >= LOG_QUEUE_SIZE) {
        logQueueTail = (logQueueTail + 1) % LOG_QUEUE_SIZE;
        logQueueCount--;
        logsDropped++;
    }
    strncpy(logQueue[logQueueHead], message, LOG_MSG_MAX - 1);
    logQueue[logQueueHead][LOG_MSG_MAX - 1] = '\0';
    logQueueHead = (logQueueHead + 1) % LOG_QUEUE_SIZE;
    logQueueCount++;
    portEXIT_CRITICAL(&logQueueMux);
    return true;
}


// =============================================================================
// Subscribe
// =============================================================================

bool MQTTService::subscribe(const char* topic, MessageCallback cb) {
    if (!topic || !cb) return false;
    subscriptions.push_back({String(topic), cb});
    if (client.connected()) {
        client.subscribe(topic);
    }
    return true;
}


// =============================================================================
// NVS-backed configuration
// =============================================================================

void MQTTService::updateBroker(const char* host, uint16_t port) {
    if (!host) return;
    if (!preferences.begin(MQTT_PREFS_NAMESPACE, false)) {
        return;
    }
    preferences.putString("host", host);
    preferences.putUShort("port", port);
    preferences.end();
    // Mark for recheck on next loop
    recheckBrokerConfig = true;
}

int MQTTService::getBroker(char* hostBuf, size_t hostSize, uint16_t* portOut) {
    if (!hostBuf || hostSize == 0) return -1;
    // Return hardcoded broker configuration
    const char* hardcodedHost = "192.168.2.41";
    size_t hostLen = strlen(hardcodedHost);
    if (hostLen + 1 > hostSize) return -1;
    strcpy(hostBuf, hardcodedHost);
    if (portOut) *portOut = 1883;
    return 0;
}

void MQTTService::updateCredentials(const char* user, const char* pass) {
    if (!preferences.begin(MQTT_PREFS_NAMESPACE, false)) return;
    preferences.putString("user", user ? user : "");
    preferences.putString("pass", pass ? pass : "");
    preferences.end();
}

int MQTTService::getUsername(char* outBuf, size_t bufferSize) {
    if (!outBuf || bufferSize == 0) return -1;
    // No username configured
    if (1 > bufferSize) return -1;
    outBuf[0] = '\0';
    return 0;
}

void MQTTService::updateBaseTopic(const char* topic) {
    if (!topic) return;
    if (!preferences.begin(MQTT_PREFS_NAMESPACE, false)) return;
    preferences.putString("topic", topic);
    preferences.end();
}

int MQTTService::getBaseTopic(char* outBuf, size_t bufferSize) {
    if (!outBuf || bufferSize == 0) return -1;
    // Return the default base topic (built in buildClientIdAndDefaults)
    size_t topicLen = strlen(baseTopic);
    if (topicLen + 1 > bufferSize) return -1;
    strcpy(outBuf, baseTopic);
    return 0;
}

bool MQTTService::hasBrokerConfig() {
    // Hardcoded configuration is always available
    return true;
}


// =============================================================================
// Private helpers
// =============================================================================

void MQTTService::buildClientIdAndDefaults() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(clientId,  sizeof(clientId),  "boat-%02x%02x%02x", mac[3], mac[4], mac[5]);
    snprintf(baseTopic, sizeof(baseTopic), "boat/%02x%02x%02x", mac[3], mac[4], mac[5]);
    snprintf(logTopic,          sizeof(logTopic),          "%s/log",          baseTopic);
    snprintf(availabilityTopic, sizeof(availabilityTopic), "%s/availability", baseTopic);
}

void MQTTService::readNvs() {
    if (!preferences.begin(MQTT_PREFS_NAMESPACE, true)) return;
    String h = preferences.getString("host", "");
    brokerPort = preferences.getUShort("port", DEFAULT_MQTT_PORT);
    String u = preferences.getString("user", "");
    String p = preferences.getString("pass", "");
    String t = preferences.getString("topic", "");
    preferences.end();

    strncpy(brokerHost, h.c_str(), sizeof(brokerHost) - 1); brokerHost[sizeof(brokerHost)-1] = '\0';
    strncpy(username,   u.c_str(), sizeof(username)   - 1); username[sizeof(username)-1]     = '\0';
    strncpy(password,   p.c_str(), sizeof(password)   - 1); password[sizeof(password)-1]     = '\0';

    // Use stored baseTopic only if non-empty; otherwise keep the MAC-derived default
    if (t.length() > 0) {
        strncpy(baseTopic, t.c_str(), sizeof(baseTopic) - 1); baseTopic[sizeof(baseTopic)-1] = '\0';
    }
    snprintf(logTopic,          sizeof(logTopic),          "%s/log",          baseTopic);
    snprintf(availabilityTopic, sizeof(availabilityTopic), "%s/availability", baseTopic);
}

void MQTTService::applyServerConfig() {
    if (strlen(brokerHost) > 0) {
        client.setServer(brokerHost, brokerPort);
    }
    client.setBufferSize(MQTT_MAX_PACKET_SIZE);
    client.setKeepAlive(MQTT_KEEPALIVE);
    client.setSocketTimeout(MQTT_SOCKET_TIMEOUT);
    client.setCallback(&MQTTService::onMessageTrampoline);
}

void MQTTService::tryReconnect() {
    lastReconnectAttempt = millis();

    const char* user = (strlen(username) > 0) ? username : nullptr;
    const char* pass = (strlen(password) > 0) ? password : nullptr;

    bool ok = client.connect(clientId, user, pass,
                             availabilityTopic, /*qos*/0, /*retain*/true, "offline");

    if (ok) {
        // Publish online status retained so HA picks it up immediately
        inMqttCall = true;
        client.publish(availabilityTopic, "online", true);
        inMqttCall = false;

        // Resubscribe to all registered topic filters
        for (auto& sub : subscriptions) {
            client.subscribe(sub.filter.c_str());
        }

        reconnectBackoffMs = RECONNECT_INITIAL_MS;
    } else {
        // Exponential backoff, capped at RECONNECT_MAX_MS
        reconnectBackoffMs = min(reconnectBackoffMs * 2, RECONNECT_MAX_MS);
    }
}

void MQTTService::drainLogQueue() {
    uint32_t start = millis();
    char msgCopy[LOG_MSG_MAX];
    while ((millis() - start) < DRAIN_BUDGET_MS) {
        if (!client.connected()) break;

        // Pull one item under the lock, then publish outside it so client.publish()
        // is never called while the spinlock is held.
        portENTER_CRITICAL(&logQueueMux);
        if (logQueueCount == 0) {
            portEXIT_CRITICAL(&logQueueMux);
            break;
        }
        memcpy(msgCopy, logQueue[logQueueTail], LOG_MSG_MAX);
        logQueueTail = (logQueueTail + 1) % LOG_QUEUE_SIZE;
        logQueueCount--;
        portEXIT_CRITICAL(&logQueueMux);

        inMqttCall = true;
        client.publish(logTopic, msgCopy, false);
        inMqttCall = false;
    }
}

void MQTTService::onMessageTrampoline(char* topic, byte* payload, unsigned int length) {
    if (!s_instance) return;

    // Null-terminate into a stack buffer (truncate if oversized)
    static constexpr size_t MAX_PAYLOAD = 512;
    char buf[MAX_PAYLOAD + 1];
    size_t copyLen = (length < MAX_PAYLOAD) ? length : MAX_PAYLOAD;
    memcpy(buf, payload, copyLen);
    buf[copyLen] = '\0';

    s_instance->dispatchMessage(topic, buf);
}

void MQTTService::dispatchMessage(const char* topic, const char* payload) {
    for (auto& sub : subscriptions) {
        // Simple prefix/exact match; PubSubClient handles wildcard at broker level
        if (sub.filter.equals(topic)) {
            sub.cb(topic, payload);
        }
    }
}


// =============================================================================
// Global Logger hook (called from Logger.h macros via sendMqttLog)
// =============================================================================

MQTTService* g_mqtt = nullptr;

void sendMqttLog(const char* message) {
    if (!g_mqtt) return;
    g_mqtt->publishLog(message);
}

#endif // UNIT_TESTING
