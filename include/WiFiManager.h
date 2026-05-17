#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <vector>

constexpr const char* WIFI_PREFERENCES_NAMESPACE = "wifi";
static constexpr int MAX_NETWORKS = 10;
static constexpr int CONNECT_TIMEOUT_MS = 15000; // 15 secs
static constexpr uint32_t RECONNECT_INTERVAL_MS = 30000; // 30 secs between retry attempts

struct WiFiCredential {
    char* ssid;
    char* password;
};

class WiFiManager {
private:
    Preferences preferences;
    std::vector<WiFiCredential> storedNetworks;
    bool isWiFiConnected = false;

    // Connection health tracking
    uint32_t _connectedSince = 0;
    uint32_t _disconnectedSince = 0;
    volatile uint8_t _lastDisconnectReason = 0; // written by WiFi event task
    uint32_t _reconnectAttemptCount = 0;
    uint32_t _lastReconnectAttempt = 0;

    WiFiManager();
    void loadCredentials();
    void saveCredentials();
    static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
    static const char* reasonToString(uint8_t reason);

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

