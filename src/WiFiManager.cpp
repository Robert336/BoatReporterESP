#ifndef UNIT_TESTING
#include "WiFiManager.h"
#include "Logger.h"
#include <esp_task_wdt.h>
#include <esp_timer.h>


// Singleton instance getter
WiFiManager& WiFiManager::getInstance() {
    static WiFiManager instance;
    return instance;
}

WiFiManager::WiFiManager() {
    // Constructor is called by getInstance() during first access
}

WiFiManager::~WiFiManager() {
    // Credentials are now fixed char[] members — no heap to free.
    storedNetworks.clear();
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
    // Clear existing networks (value storage — clear() is sufficient, no leak)
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
            // Bounded copies — fixed arrays can never overflow or leak.
            strncpy(cred.ssid, ssid.c_str(), sizeof(cred.ssid) - 1);
            cred.ssid[sizeof(cred.ssid) - 1] = '\0';
            strncpy(cred.password, password.c_str(), sizeof(cred.password) - 1);
            cred.password[sizeof(cred.password) - 1] = '\0';
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
            strncpy(storedNetworks[i].password, password, sizeof(storedNetworks[i].password) - 1);
            storedNetworks[i].password[sizeof(storedNetworks[i].password) - 1] = '\0';

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
    strncpy(cred.ssid, ssid, sizeof(cred.ssid) - 1);
    cred.ssid[sizeof(cred.ssid) - 1] = '\0';
    strncpy(cred.password, password, sizeof(cred.password) - 1);
    cred.password[sizeof(cred.password) - 1] = '\0';
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
            storedNetworks.erase(it); // value storage — no delete[] needed

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
    // The scan is a single blocking call (~2-5s on 2.4GHz) and runs on the
    // loop task, which is subscribed to the 10s task watchdog. Feed the dog
    // before the scan so a slow/crowded-band scan can't trip it (C1). The
    // call is a no-op if the WDT isn't armed yet (e.g. during boot, before
    // esp_task_wdt_add() runs in setup()).
    esp_task_wdt_reset();
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
            // Feed the task watchdog on every iteration. This connect loop
            // can block for up to CONNECT_TIMEOUT_MS (15s) — longer than the
            // 10s WDT — and is reachable from loop() via stopSetupMode()
            // after the config-portal idle timeout (C1). delay() does NOT
            // feed the ESP-IDF task watchdog, so an explicit reset is required.
            esp_task_wdt_reset();
            delay(500);
        }

        if (WiFi.status() == WL_CONNECTED) {
            isWiFiConnected = true;
            _connectedSinceUs = esp_timer_get_time();
            _disconnectedSinceUs = 0;
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
        if (_disconnectedSinceUs != 0) {
            // Just came back up. Duration is computed from the 64-bit monotonic
            // microsecond timer so it stays correct across a millis() rollover (H6).
            unsigned long long downSecs =
                (unsigned long long)((esp_timer_get_time() - _disconnectedSinceUs) / 1000000);
            if (_reconnectAttemptCount > 0) {
                LOG_NETWORK("[WIFI] Reconnected after %u attempt(s), was down %llus",
                            _reconnectAttemptCount, downSecs);
            } else {
                // The link recovered without any app-level attempt (driver
                // auto-reconnect, or it came back during/after a failed rescan) —
                // "Reconnected after 0 attempt(s)" would read like a bug.
                LOG_NETWORK("[WIFI] Connection restored without app-level reconnect, was down %llus",
                            downSecs);
            }
            _connectedSinceUs = esp_timer_get_time();
            _disconnectedSinceUs = 0;
            _reconnectAttemptCount = 0;
            _lastDisconnectReason = 0;
        }
        return;
    }

    uint32_t now = millis();

    if (_disconnectedSinceUs == 0) {
        _disconnectedSinceUs = esp_timer_get_time();
        uint8_t reason = _lastDisconnectReason;
        LOG_NETWORK("[WIFI] Disconnect reason: %u (%s), was up %llus",
                    reason, reasonToString(reason),
                    _connectedSinceUs > 0 ? (unsigned long long)((esp_timer_get_time() - _connectedSinceUs) / 1000000) : 0);
        _connectedSinceUs = 0;
    }

    if (now - _lastReconnectAttempt >= RECONNECT_INTERVAL_MS) {
        _lastReconnectAttempt = now;
        _reconnectAttemptCount++;

        // H1/H2: don't retry the same dead AP forever. WiFi.reconnect() only
        // re-associates with the last BSSID/SSID — it never scans, so a
        // permanently-gone AP stalls reconnection indefinitely even when another
        // stored network is in range. After RECONNECT_FALLBACK_ATTEMPTS failed
        // cycles, fall back to a full connectToBestNetwork() scan-and-pick.
        //
        // H2: for "sticky" disconnect reasons (handshake/beacon timeouts, auth
        // fail) that are known to sometimes leave the radio in a state a plain
        // reconnect() can't recover, escalate sooner — do a full
        // WiFi.disconnect(true) teardown before the rescan.
        bool sticky = isStickyDisconnectReason(_lastDisconnectReason);
        uint32_t threshold = sticky ? RECONNECT_ESCALATION_ATTEMPTS_STICKY
                                    : RECONNECT_FALLBACK_ATTEMPTS;
        if (_reconnectAttemptCount >= threshold) {
            LOG_NETWORK("[WIFI] %u reconnect attempt(s) failed (reason %u/%s) - full rescan",
                        _reconnectAttemptCount, _lastDisconnectReason,
                        reasonToString(_lastDisconnectReason));
            if (sticky) {
                WiFi.disconnect(true);
            }
            _reconnectAttemptCount = 0;
            _lastDisconnectReason = 0;
            connectToBestNetwork();
            return;
        }

        LOG_NETWORK("[WIFI] Reconnect attempt #%u (down %llus)",
                    _reconnectAttemptCount,
                    (unsigned long long)((esp_timer_get_time() - _disconnectedSinceUs) / 1000000));
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
    // Case labels use the named wifi_err_reason_t constants from
    // esp_wifi_types.h (pulled in via <WiFi.h> -> WiFiType.h) so they
    // self-document; values are identical to the raw numbers they replace.
    switch (reason) {
        case WIFI_REASON_UNSPECIFIED:            return "unspecified";
        case WIFI_REASON_AUTH_EXPIRE:            return "auth-expire";
        case WIFI_REASON_AUTH_LEAVE:             return "auth-leave";
        case WIFI_REASON_ASSOC_EXPIRE:           return "assoc-expire";
        case WIFI_REASON_ASSOC_TOOMANY:          return "assoc-too-many";
        case WIFI_REASON_NOT_AUTHED:             return "not-authed";
        case WIFI_REASON_NOT_ASSOCED:            return "not-assoced";
        case WIFI_REASON_ASSOC_LEAVE:            return "assoc-leave";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4way-handshake-timeout";
        case WIFI_REASON_BEACON_TIMEOUT:         return "beacon-timeout";
        case WIFI_REASON_NO_AP_FOUND:            return "no-ap-found";
        case WIFI_REASON_AUTH_FAIL:              return "auth-fail";
        case WIFI_REASON_ASSOC_FAIL:             return "assoc-fail";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:      return "handshake-timeout";
        case WIFI_REASON_CONNECTION_FAIL:        return "connection-fail";
        default:                                 return "unknown";
    }
}

bool WiFiManager::isStickyDisconnectReason(uint8_t reason) {
    // Reasons that, in practice on the ESP32 Arduino core, sometimes leave the
    // radio in a state a plain WiFi.reconnect() cannot recover from — a full
    // WiFi.disconnect(true) teardown followed by a fresh begin()/scan is needed.
    switch (reason) {
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_BEACON_TIMEOUT:
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return true;
        default:
            return false;
    }
}
#endif // UNIT_TESTING
