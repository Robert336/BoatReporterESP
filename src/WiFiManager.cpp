#include "WiFiManager.h"
#include "Logger.h"


// Singleton instance getter
WiFiManager& WiFiManager::getInstance() {
    static WiFiManager instance;
    return instance;
}

WiFiManager::WiFiManager() {
    // Constructor is called by getInstance() during first access
}

WiFiManager::~WiFiManager() {
    for (auto& cred : storedNetworks) {
        delete[] cred.ssid;
        delete[] cred.password;
        // After delete[], these pointers are invalid (dangling pointers)
        // But that's okay because the object is being destroyed anyway
    }
    storedNetworks.clear(); // Clear the vector (redundant but explicit)
    
    LOG_DEBUG("WiFiManager: Memory cleaned up");
}

void WiFiManager::begin() {
    loadCredentials();
    WiFi.mode(WIFI_STA);
    WiFi.onEvent(onWiFiEvent, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    connectToBestNetwork();
}

void WiFiManager::loadCredentials() {
    if (!preferences.begin(WIFI_PREFERENCES_NAMESPACE, true)) {
        LOG_CRITICAL("WiFiManager: Failed to open preferences for reading!");
        return;
    }
    // Clear existing networks
    storedNetworks.clear();
    
    // Load count of stored networks
    int networkCount = preferences.getInt("count", 0);
    
    for (int i = 0; i < networkCount && i < MAX_NETWORKS; i++) {
        char key_ssid[16], key_pass[16];
        snprintf(key_ssid, sizeof(key_ssid), "ssid_%d", i);
        snprintf(key_pass, sizeof(key_pass), "pass_%d", i);
        
        String ssid = preferences.getString(key_ssid, "");
        String password = preferences.getString(key_pass, "");
        
        if (ssid.length() > 0) {
            WiFiCredential cred;
            cred.ssid = new char[ssid.length() + 1];
            cred.password = new char[password.length() + 1];
            strcpy(cred.ssid, ssid.c_str());
            strcpy(cred.password, password.c_str());
            storedNetworks.push_back(cred);
            
            LOG_NETWORK("Loaded network: %s", cred.ssid);
        }
    }
    preferences.end();
}

void WiFiManager::addNetwork(const char* ssid, const char* password) {
    // Update password if network already exists — write only that one key
    for (int i = 0; i < (int)storedNetworks.size(); i++) {
        if (strcmp(storedNetworks[i].ssid, ssid) == 0) {
            delete[] storedNetworks[i].password;
            storedNetworks[i].password = new char[strlen(password) + 1];
            strcpy(storedNetworks[i].password, password);

            if (preferences.begin(WIFI_PREFERENCES_NAMESPACE, false)) {
                char key_pass[16];
                snprintf(key_pass, sizeof(key_pass), "pass_%d", i);
                preferences.putString(key_pass, password);
                preferences.end();
                LOG_NETWORK("Updated password for: %s", ssid);
            } else {
                LOG_CRITICAL("WiFiManager: Failed to open preferences, reloading from NVS");
                loadCredentials();
            }
            return;
        }
    }

    if ((int)storedNetworks.size() >= MAX_NETWORKS) {
        LOG_NETWORK("Max networks reached!");
        return;
    }

    // Append new entry — write only the two new keys and the updated count
    int idx = storedNetworks.size();
    WiFiCredential cred;
    cred.ssid = new char[strlen(ssid) + 1];
    cred.password = new char[strlen(password) + 1];
    strcpy(cred.ssid, ssid);
    strcpy(cred.password, password);
    storedNetworks.push_back(cred);

    if (preferences.begin(WIFI_PREFERENCES_NAMESPACE, false)) {
        char key_ssid[16], key_pass[16];
        snprintf(key_ssid, sizeof(key_ssid), "ssid_%d", idx);
        snprintf(key_pass, sizeof(key_pass), "pass_%d", idx);
        preferences.putString(key_ssid, ssid);
        preferences.putString(key_pass, password);
        preferences.putInt("count", storedNetworks.size());
        preferences.end();
        LOG_NETWORK("Added network: %s", ssid);
    } else {
        LOG_CRITICAL("WiFiManager: Failed to open preferences, reloading from NVS");
        loadCredentials();
    }

    // Attempt an immediate connection when in STA mode and not yet connected.
    // Skip in AP/AP_STA mode (CONFIG state) — connectToBestNetwork() will be
    // called by stopSetupMode() when the AP tears down.
    if (!isConnected() && WiFi.getMode() == WIFI_MODE_STA) {
        connectToBestNetwork();
    }
}

void WiFiManager::removeNetwork(const char* ssid) {
    for (auto it = storedNetworks.begin(); it != storedNetworks.end(); ++it) {
        if (strcmp(it->ssid, ssid) == 0) {
            String removed = it->ssid;
            delete[] it->ssid;
            delete[] it->password;
            storedNetworks.erase(it);

            if (preferences.begin(WIFI_PREFERENCES_NAMESPACE, false)) {
                // Rewrite the compacted list in-place (no clear needed)
                for (int i = 0; i < (int)storedNetworks.size(); i++) {
                    char key_ssid[16], key_pass[16];
                    snprintf(key_ssid, sizeof(key_ssid), "ssid_%d", i);
                    snprintf(key_pass, sizeof(key_pass), "pass_%d", i);
                    preferences.putString(key_ssid, storedNetworks[i].ssid);
                    preferences.putString(key_pass, storedNetworks[i].password);
                }
                // Delete the now-stale last slot
                char stale_ssid[16], stale_pass[16];
                snprintf(stale_ssid, sizeof(stale_ssid), "ssid_%d", (int)storedNetworks.size());
                snprintf(stale_pass, sizeof(stale_pass), "pass_%d", (int)storedNetworks.size());
                preferences.remove(stale_ssid);
                preferences.remove(stale_pass);
                preferences.putInt("count", storedNetworks.size());
                preferences.end();
                LOG_NETWORK("Removed network: %s", removed.c_str());
            } else {
                LOG_CRITICAL("WiFiManager: Failed to open preferences, reloading from NVS");
                loadCredentials();
            }
            return;
        }
    }
    LOG_NETWORK("Network not found: %s", ssid);
}

void WiFiManager::connectToBestNetwork() {
    if (storedNetworks.empty()) {
        LOG_NETWORK("No stored networks available!");
        return;
    }
    
    LOG_NETWORK("[WIFI] Scanning for available networks...");
    int numNetworks = WiFi.scanNetworks();
    LOG_NETWORK("[WIFI] Scan found %d network(s):", numNetworks);
    for (int i = 0; i < numNetworks; i++) {
        bool isStored = false;
        for (int j = 0; j < (int)storedNetworks.size(); j++) {
            if (WiFi.SSID(i) == storedNetworks[j].ssid) { isStored = true; break; }
        }
        LOG_NETWORK("  [%d] %-32s  %4d dBm  ch%-2d%s",
                    i, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i),
                    isStored ? "  *" : "");
    }

    int bestNetwork = -1;
    int bestRSSI = -120;

    for (int i = 0; i < numNetworks; i++) {
        String scannedSSID = WiFi.SSID(i);
        int scannedRSSI = WiFi.RSSI(i);

        for (int j = 0; j < (int)storedNetworks.size(); j++) {
            if (scannedSSID == storedNetworks[j].ssid) {
                if (scannedRSSI > bestRSSI) {
                    bestRSSI = scannedRSSI;
                    bestNetwork = j;
                }
                break;
            }
        }
    }

    if (bestNetwork != -1) {
        LOG_NETWORK("[WIFI] Connecting to: %s (%d dBm)", storedNetworks[bestNetwork].ssid, bestRSSI);

        WiFi.begin(storedNetworks[bestNetwork].ssid, storedNetworks[bestNetwork].password);

        unsigned long startTime = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < CONNECT_TIMEOUT_MS) {
            delay(500);
        }

        if (WiFi.status() == WL_CONNECTED) {
            isWiFiConnected = true;
            _connectedSince = millis();
            _disconnectedSince = 0;
            _reconnectAttemptCount = 0;
            LOG_NETWORK("[WIFI] Connected! IP: %s", WiFi.localIP().toString().c_str());
        } else {
            isWiFiConnected = false;
            LOG_NETWORK("[WIFI] Connection failed");
        }
    } else {
        LOG_NETWORK("[WIFI] No stored networks found in scan results!");
    }
    
    WiFi.scanDelete();
}

std::vector<String> WiFiManager::getStoredSSIDs() {
    std::vector<String> ssids;
    for (auto& cred : storedNetworks) {
        ssids.push_back(String(cred.ssid));
    }
    return ssids;
}

bool WiFiManager::isConnected() {
    return (WiFi.status() == WL_CONNECTED);
}

void WiFiManager::disconnect() {
    WiFi.disconnect(true);
    isWiFiConnected = false;
    LOG_NETWORK("[WIFI] Disconnected");
}

void WiFiManager::maintainConnection() {
    if (WiFi.status() == WL_CONNECTED) {
        if (_disconnectedSince != 0) {
            // Just came back up
            LOG_NETWORK("[WIFI] Reconnected after %u attempt(s), was down %lus",
                        _reconnectAttemptCount,
                        (unsigned long)((millis() - _disconnectedSince) / 1000));
            _connectedSince = millis();
            _disconnectedSince = 0;
            _reconnectAttemptCount = 0;
            _lastDisconnectReason = 0;
        }
        return;
    }

    uint32_t now = millis();

    if (_disconnectedSince == 0) {
        _disconnectedSince = now;
        uint8_t reason = _lastDisconnectReason;
        LOG_NETWORK("[WIFI] Disconnect reason: %u (%s), was up %lus",
                    reason, reasonToString(reason),
                    _connectedSince > 0 ? (unsigned long)((now - _connectedSince) / 1000) : 0);
        _connectedSince = 0;
    }

    if (now - _lastReconnectAttempt >= RECONNECT_INTERVAL_MS) {
        _lastReconnectAttempt = now;
        _reconnectAttemptCount++;
        LOG_NETWORK("[WIFI] Reconnect attempt #%u (down %lus)",
                    _reconnectAttemptCount,
                    (unsigned long)((now - _disconnectedSince) / 1000));
        WiFi.reconnect();
    }
}

int WiFiManager::getRSSI() {
    return (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
}

void WiFiManager::onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    getInstance()._lastDisconnectReason = info.wifi_sta_disconnected.reason;
}

const char* WiFiManager::reasonToString(uint8_t reason) {
    switch (reason) {
        case 1:   return "unspecified";
        case 2:   return "auth-expire";
        case 3:   return "auth-leave";
        case 4:   return "assoc-expire";
        case 5:   return "assoc-too-many";
        case 6:   return "not-authed";
        case 7:   return "not-assoced";
        case 8:   return "assoc-leave";
        case 15:  return "4way-handshake-timeout";
        case 200: return "beacon-timeout";
        case 201: return "no-ap-found";
        case 202: return "auth-fail";
        case 203: return "assoc-fail";
        case 204: return "handshake-timeout";
        case 205: return "connection-fail";
        default:  return "unknown";
    }
}