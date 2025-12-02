#include "WiFiManager.h"



std::vector<WiFiCredential> storedNetworks;

// Singelton instance getter
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
    
    Serial.println("WiFiManager: Memory cleaned up");
}

void WiFiManager::begin() {
    preferences.begin("wifi", false);
    loadCredentials();
    WiFi.mode(WIFI_STA);
    
    // Auto-connect to the best available network
    connectToBestNetwork();
}

void WiFiManager::loadCredentials() {
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
            
            Serial.print("Loaded network: ");
            Serial.println(cred.ssid);
        }
    }
}

void WiFiManager::saveCredentials() {
    // Clear old data
    preferences.clear();
    
    // Save count of networks
    preferences.putInt("count", storedNetworks.size());
    
    // Save each network
    for (int i = 0; i < storedNetworks.size(); i++) {
        char key_ssid[16], key_pass[16];
        snprintf(key_ssid, sizeof(key_ssid), "ssid_%d", i);
        snprintf(key_pass, sizeof(key_pass), "pass_%d", i);
        
        preferences.putString(key_ssid, storedNetworks[i].ssid);
        preferences.putString(key_pass, storedNetworks[i].password);
        
        Serial.print("Saved network: ");
        Serial.println(storedNetworks[i].ssid);
    }
}

void WiFiManager::addNetwork(const char* ssid, const char* password) {
    // Check if network already exists
    for (auto& cred : storedNetworks) {
        if (strcmp(cred.ssid, ssid) == 0) {
            Serial.println("Network already exists, updating password...");
            delete[] cred.password;
            // Allocate new buffer with correct size for the new password
            cred.password = new char[strlen(password) + 1];
            strcpy(cred.password, password);
            saveCredentials();
            return;
        }
    }
    
    // Add new network if not at max
    if (storedNetworks.size() < MAX_NETWORKS) {
        WiFiCredential cred;
        cred.ssid = new char[strlen(ssid) + 1];
        cred.password = new char[strlen(password) + 1];
        strcpy(cred.ssid, ssid);
        strcpy(cred.password, password);
        storedNetworks.push_back(cred);
        
        saveCredentials();
        Serial.print("Added network: ");
        Serial.println(ssid);
    } else {
        Serial.println("Max networks reached!");
    }
}

void WiFiManager::removeNetwork(const char* ssid) {
    for (auto it = storedNetworks.begin(); it != storedNetworks.end(); ++it) {
        if (strcmp(it->ssid, ssid) == 0) {
            delete[] it->ssid;
            delete[] it->password;
            storedNetworks.erase(it);
            saveCredentials();
            Serial.print("Removed network: ");
            Serial.println(ssid);
            return;
        }
    }
    Serial.println("Network not found!");
}

void WiFiManager::connectToBestNetwork() {
    if (storedNetworks.empty()) {
        Serial.println("No stored networks available!");
        return;
    }
    
    Serial.println("Scanning for available networks...");
    int numNetworks = WiFi.scanNetworks();
    
    int bestNetwork = -1;
    int bestRSSI = -120; // Worst possible signal strength
    
    // Find the best signal from stored networks
    for (int i = 0; i < numNetworks; i++) {
        String scannedSSID = WiFi.SSID(i);
        int scannedRSSI = WiFi.RSSI(i);
        
        for (int j = 0; j < storedNetworks.size(); j++) {
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
        Serial.print("Connecting to: ");
        Serial.println(storedNetworks[bestNetwork].ssid);
        Serial.print("Signal strength: ");
        Serial.print(bestRSSI);
        Serial.println(" dBm");
        
        WiFi.begin(storedNetworks[bestNetwork].ssid, storedNetworks[bestNetwork].password);
        
        // Wait for connection with timeout
        unsigned long startTime = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < CONNECT_TIMEOUT_MS) {
            delay(500);
            Serial.print(".");
        }
        Serial.println();
        
        if (WiFi.status() == WL_CONNECTED) {
            isWiFiConnected = true;
            Serial.println("✓ Connected!");
            Serial.print("IP address: ");
            Serial.println(WiFi.localIP());
        } else {
            isWiFiConnected = false;
            Serial.println("✗ Connection failed");
        }
    } else {
        Serial.println("No stored networks found in scan results!");
    }
    
    WiFi.scanDelete();
}

std::vector<char*> WiFiManager::getStoredSSIDs() {
    std::vector<char*> ssids;
    for (auto& cred : storedNetworks) {
        ssids.push_back(cred.ssid);
    }
    return ssids;
}

bool WiFiManager::isConnected() {
    return (WiFi.status() == WL_CONNECTED);
}

void WiFiManager::disconnect() {
    WiFi.disconnect(true); // true = turn off WiFi radio
    isWiFiConnected = false;
    Serial.println("WiFi disconnected");
}