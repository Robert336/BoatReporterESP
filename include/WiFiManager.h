#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <vector>

constexpr const char* WIFI_PREFERENCES_NAMESPACE = "wifi";
static constexpr int MAX_NETWORKS = 10;
static constexpr int CONNECT_TIMEOUT_MS = 15000; // 15 secs
static constexpr uint32_t RECONNECT_INTERVAL_MS = 30000; // 30 secs between retry attempts

// H1/H2: after this many consecutive failed WiFi.reconnect() attempts, fall
// back to a full connectToBestNetwork() scan-and-pick cycle instead of retrying
// the same (possibly permanently-gone) AP forever.
static constexpr uint32_t RECONNECT_FALLBACK_ATTEMPTS = 6; // 6 * 30s = 3 min
// H2: disconnect reasons known to sometimes need a full teardown
// (WiFi.disconnect(true) + rescan) rather than a lightweight WiFi.reconnect()
// escalate after fewer attempts.
static constexpr uint32_t RECONNECT_ESCALATION_ATTEMPTS_STICKY = 2; // 2 * 30s = 1 min

struct WiFiCredential {
    char* ssid;
    char* password;
};

class WiFiManager {
private:
    Preferences preferences;
    std::vector<WiFiCredential> storedNetworks;
    bool isWiFiConnected = false;

    // Connection health tracking.
    // H6: session-start timestamps are captured in esp_timer_get_time()
    // microseconds (int64, monotonic, never wraps in practice) rather than
    // millis() (uint32, wraps every ~49.7 days). The millis()-based timing
    // *comparisons* in this class are already wrap-safe (unsigned subtraction),
    // but the *logged* session durations ("was up %lus") were computed from
    // millis() and went wrong for any single WiFi session lasting across a
    // rollover. These fields are used solely for logging durations.
    int64_t _connectedSinceUs = 0;
    int64_t _disconnectedSinceUs = 0;
    volatile uint8_t _lastDisconnectReason = 0; // written by WiFi event task
    uint32_t _reconnectAttemptCount = 0;
    uint32_t _lastReconnectAttempt = 0;

    WiFiManager();
    void loadCredentials();
    static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
    static const char* reasonToString(uint8_t reason);
    // H2: true for disconnect reasons (4WAY_HANDSHAKE_TIMEOUT, BEACON_TIMEOUT,
    // HANDSHAKE_TIMEOUT, AUTH_FAIL) that are known to sometimes require a full
    // disconnect+rescan rather than a plain WiFi.reconnect().
    static bool isStickyDisconnectReason(uint8_t reason);

public:
    static WiFiManager& getInstance();

    WiFiManager(const WiFiManager&) = delete;
    WiFiManager& operator=(const WiFiManager&) = delete;
    ~WiFiManager();

    void begin();
    void addNetwork(const char* ssid, const char* password);
    void removeNetwork(const char* ssid);
    void connectToBestNetwork();
    void maintainConnection(); // Call from main loop — non-blocking reconnect with backoff
    std::vector<String> getStoredSSIDs();
    bool isConnected();
    int  getRSSI(); // Returns current RSSI in dBm, 0 if not connected
    void disconnect();
};

