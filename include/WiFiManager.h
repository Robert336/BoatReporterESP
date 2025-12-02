#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <vector>

struct WiFiCredential {
    char* ssid;
    char* password;
};

class WiFiManager {
private:
    Preferences preferences;
    std::vector<WiFiCredential> storedNetworks;
    static const int MAX_NETWORKS = 10; // Max amount of networks we're storing info for
    static const int CONNECT_TIMEOUT_MS = 15000; // 15 secs
    bool isWiFiConnected = false;
    WiFiManager(); // Singleton pattern - hide the constructor
    void loadCredentials(); // Load from NVS
    void saveCredentials(); // Save to NVS
public:
    // Singleton pattern
    static WiFiManager& getInstance();

    // Delete copy constructor and assignment operator
    WiFiManager(const WiFiManager&) = delete;
    WiFiManager& operator=(const WiFiManager&) = delete;
    ~WiFiManager();

    void begin(); // Initialize the WiFiManager
    void addNetwork(const char* ssid, const char* password);
    void removeNetwork(const char* ssid);
    void connectToBestNetwork(); // Scan and connect to the best network available
    std::vector<char*> getStoredSSIDs(); // Get the saved network names
    bool isConnected();
    void disconnect();
};

