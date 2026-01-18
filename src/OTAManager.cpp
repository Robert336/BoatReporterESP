#include "OTAManager.h"
#include "Logger.h"
#include "Version.h"
#include <esp_ota_ops.h>

OTAManager::OTAManager(SendSMS* sms, SendDiscord* discord)
    : smsService(sms), discordService(discord), currentState(OTAState::IDLE),
      lastCheckTime(0), firstBootAfterUpdate(false), rollbackOccurred(false) {
    
    versionInfo.currentVersion = FIRMWARE_VERSION;
}

OTAManager::~OTAManager() {
    preferences.end();
}

void OTAManager::begin() {
    LOG_INFO("[OTA] Initializing OTA Manager v%s", versionInfo.currentVersion.c_str());
    
    // Load configuration from NVS
    loadConfig();
    
    // Check if this is first boot after update
    checkFirstBoot();
    
    LOG_INFO("[OTA] GitHub Repo: %s/%s", config.githubOwner.c_str(), config.githubRepo.c_str());
    LOG_INFO("[OTA] Auto-check: %s (interval: %lu hours)", 
             config.autoCheckEnabled ? "enabled" : "disabled",
             config.checkIntervalMs / 3600000);
    LOG_INFO("[OTA] Auto-install: %s", config.autoInstallEnabled ? "enabled" : "disabled");
}

void OTAManager::loop() {
    // Don't do anything if currently updating
    if (currentState == OTAState::DOWNLOADING || currentState == OTAState::INSTALLING) {
        return;
    }
    
    // Check if auto-check is enabled and it's time to check
    if (config.autoCheckEnabled && 
        (millis() - lastCheckTime) >= config.checkIntervalMs) {
        
        LOG_INFO("[OTA] Automatic update check triggered");
        manualCheckForUpdates();
    }
    
    // Check if auto-install is enabled and update is available
    if (config.autoInstallEnabled && currentState == OTAState::UPDATE_AVAILABLE) {
        LOG_INFO("[OTA] Auto-install enabled and update available - starting automatic installation");
        
        // Automatically start the update (no password required for auto-install)
        // Note: If you want password protection even for auto-install, remove the nullptr
        startUpdate(nullptr);
    }
}

void OTAManager::checkFirstBoot() {
    if (!preferences.begin(OTA_PREFERENCES_NAMESPACE, true)) {
        LOG_CRITICAL("[OTA] Failed to open preferences for first boot check");
        return;
    }
    
    firstBootAfterUpdate = preferences.getBool("first_boot", false);
    rollbackOccurred = preferences.getBool("rollback", false);
    String previousVersion = preferences.getString("prev_version", "");
    
    preferences.end();
    
    // Check for rollback by examining the OTA partition state
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            // Mark this boot as valid
            esp_ota_mark_app_valid_cancel_rollback();
            LOG_INFO("[OTA] New firmware validated successfully");
        }
    }
    
    // Send notifications if needed
    if (firstBootAfterUpdate && !previousVersion.isEmpty()) {
        char msg[150];
        snprintf(msg, sizeof(msg), 
                 "Boat Monitor: Firmware updated successfully! v%s → v%s. System online.",
                 previousVersion.c_str(), versionInfo.currentVersion.c_str());
        sendNotification(msg);
        LOG_INFO("[OTA] %s", msg);
        clearFirstBootFlag();
    } else if (rollbackOccurred && !previousVersion.isEmpty()) {
        char msg[150];
        snprintf(msg, sizeof(msg),
                 "Boat Monitor: New firmware v%s failed to boot. Rolled back to v%s. System stable.",
                 previousVersion.c_str(), versionInfo.currentVersion.c_str());
        sendNotification(msg);
        LOG_CRITICAL("[OTA] %s", msg);
        clearFirstBootFlag();
    }
}

void OTAManager::setFirstBootFlag() {
    if (!preferences.begin(OTA_PREFERENCES_NAMESPACE, false)) {
        LOG_CRITICAL("[OTA] Failed to open preferences to set first boot flag");
        return;
    }
    
    preferences.putBool("first_boot", true);
    preferences.putString("prev_version", versionInfo.currentVersion);
    preferences.end();
}

void OTAManager::clearFirstBootFlag() {
    if (!preferences.begin(OTA_PREFERENCES_NAMESPACE, false)) {
        return;
    }
    
    preferences.putBool("first_boot", false);
    preferences.putBool("rollback", false);
    preferences.remove("prev_version");
    preferences.end();
}

void OTAManager::loadConfig() {
    if (!preferences.begin(OTA_PREFERENCES_NAMESPACE, true)) {
        LOG_CRITICAL("[OTA] Failed to open preferences for reading");
        return;
    }
    
    config.githubOwner = preferences.getString("gh_owner", "");
    config.githubRepo = preferences.getString("gh_repo", "");
    config.githubToken = preferences.getString("gh_token", "");
    config.updatePassword = preferences.getString("password", "");
    config.autoCheckEnabled = preferences.getBool("auto_check", true);
    config.autoInstallEnabled = preferences.getBool("auto_install", true);
    config.checkIntervalMs = preferences.getULong("check_interval", DEFAULT_CHECK_INTERVAL_MS);
    config.notificationsEnabled = preferences.getBool("notify", true);
    lastCheckTime = preferences.getULong("last_check", 0);
    
    preferences.end();
}

void OTAManager::saveConfig() {
    if (!preferences.begin(OTA_PREFERENCES_NAMESPACE, false)) {
        LOG_CRITICAL("[OTA] Failed to open preferences for writing");
        return;
    }
    
    preferences.putString("gh_owner", config.githubOwner);
    preferences.putString("gh_repo", config.githubRepo);
    preferences.putString("gh_token", config.githubToken);
    preferences.putString("password", config.updatePassword);
    preferences.putBool("auto_check", config.autoCheckEnabled);
    preferences.putBool("auto_install", config.autoInstallEnabled);
    preferences.putULong("check_interval", config.checkIntervalMs);
    preferences.putBool("notify", config.notificationsEnabled);
    preferences.putULong("last_check", lastCheckTime);
    
    preferences.end();
    
    LOG_INFO("[OTA] Configuration saved to NVS");
}

void OTAManager::sendNotification(const char* message) {
    if (!config.notificationsEnabled) {
        return;
    }
    
    bool smsSent = false;
    bool discordSent = false;
    
    if (smsService) {
        smsSent = smsService->send(message);
        if (!smsSent) {
            LOG_INFO("[OTA] Failed to send SMS notification");
        }
    }
    
    if (discordService) {
        discordSent = discordService->send(message);
        if (!discordSent) {
            LOG_INFO("[OTA] Failed to send Discord notification");
        }
    }
}

bool OTAManager::manualCheckForUpdates() {
    return checkForUpdates();
}

bool OTAManager::checkForUpdates() {
    if (config.githubOwner.isEmpty() || config.githubRepo.isEmpty()) {
        lastError = "GitHub repository not configured";
        LOG_INFO("[OTA] %s", lastError.c_str());
        return false;
    }
    
    currentState = OTAState::CHECKING;
    lastCheckTime = millis();
    
    // Save last check time
    if (!preferences.begin(OTA_PREFERENCES_NAMESPACE, false)) {
        LOG_CRITICAL("[OTA] Failed to save last check time");
    } else {
        preferences.putULong("last_check", lastCheckTime);
        preferences.end();
    }
    
    LOG_INFO("[OTA] Checking for updates from GitHub...");
    
    // Build GitHub API URL
    String url = "https://api.github.com/repos/" + config.githubOwner + "/" + 
                 config.githubRepo + "/releases/latest";
    
    HTTPClient http;
    http.begin(url);
    http.addHeader("User-Agent", "ESP32-BoatMonitor");
    
    // Add authorization if token is provided
    if (!config.githubToken.isEmpty()) {
        http.addHeader("Authorization", "token " + config.githubToken);
    }
    
    int httpCode = http.GET();
    
    if (httpCode != HTTP_CODE_OK) {
        lastError = "GitHub API request failed: " + String(httpCode);
        LOG_INFO("[OTA] %s", lastError.c_str());
        http.end();
        currentState = OTAState::FAILED;
        return false;
    }
    
    String payload = http.getString();
    http.end();
    
    // Parse JSON response
    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
        lastError = "Failed to parse GitHub API response";
        LOG_INFO("[OTA] %s: %s", lastError.c_str(), error.c_str());
        currentState = OTAState::FAILED;
        return false;
    }
    
    // Extract version and download URL
    const char* tag_name = doc["tag_name"];
    if (!tag_name) {
        lastError = "No tag_name in release";
        LOG_INFO("[OTA] %s", lastError.c_str());
        currentState = OTAState::FAILED;
        return false;
    }
    
    String latestVersion = String(tag_name);
    // Remove 'v' prefix if present
    if (latestVersion.startsWith("v")) {
        latestVersion = latestVersion.substring(1);
    }
    
    // Find firmware.bin asset
    JsonArray assets = doc["assets"];
    String downloadUrl = "";
    size_t firmwareSize = 0;
    
    for (JsonObject asset : assets) {
        const char* name = asset["name"];
        if (name && String(name) == "firmware.bin") {
            downloadUrl = String(asset["browser_download_url"].as<const char*>());
            firmwareSize = asset["size"];
            break;
        }
    }
    
    if (downloadUrl.isEmpty()) {
        lastError = "No firmware.bin found in release";
        LOG_INFO("[OTA] %s", lastError.c_str());
        currentState = OTAState::FAILED;
        return false;
    }
    
    versionInfo.availableVersion = latestVersion;
    versionInfo.downloadUrl = downloadUrl;
    versionInfo.firmwareSize = firmwareSize;
    
    // Compare versions
    if (compareVersions(latestVersion, versionInfo.currentVersion)) {
        currentState = OTAState::UPDATE_AVAILABLE;
        
        char msg[120];
        snprintf(msg, sizeof(msg), 
                 "Boat Monitor: Firmware update available v%s → v%s",
                 versionInfo.currentVersion.c_str(), latestVersion.c_str());
        sendNotification(msg);
        
        LOG_INFO("[OTA] %s", msg);
        LOG_INFO("[OTA] Download URL: %s", downloadUrl.c_str());
        LOG_INFO("[OTA] Size: %u bytes", firmwareSize);
        return true;
    } else {
        currentState = OTAState::IDLE;
        LOG_INFO("[OTA] Already on latest version v%s", versionInfo.currentVersion.c_str());
        return false;
    }
}

bool OTAManager::compareVersions(const String& newVer, const String& currentVer) {
    // Parse semantic versions (major.minor.patch)
    int newMajor = parseVersionComponent(newVer, 0);
    int newMinor = parseVersionComponent(newVer, 1);
    int newPatch = parseVersionComponent(newVer, 2);
    
    int curMajor = parseVersionComponent(currentVer, 0);
    int curMinor = parseVersionComponent(currentVer, 1);
    int curPatch = parseVersionComponent(currentVer, 2);
    
    if (newMajor > curMajor) return true;
    if (newMajor < curMajor) return false;
    
    if (newMinor > curMinor) return true;
    if (newMinor < curMinor) return false;
    
    return newPatch > curPatch;
}

int OTAManager::parseVersionComponent(const String& version, int component) {
    int start = 0;
    int componentIndex = 0;
    
    for (int i = 0; i <= version.length(); i++) {
        if (i == version.length() || version[i] == '.') {
            if (componentIndex == component) {
                return version.substring(start, i).toInt();
            }
            componentIndex++;
            start = i + 1;
        }
    }
    
    return 0;
}

bool OTAManager::startUpdate(const char* password) {
    if (currentState != OTAState::UPDATE_AVAILABLE) {
        lastError = "No update available";
        LOG_INFO("[OTA] %s", lastError.c_str());
        return false;
    }
    
    // Check password if configured
    if (!config.updatePassword.isEmpty()) {
        if (!password || config.updatePassword != String(password)) {
            lastError = "Invalid password";
            LOG_INFO("[OTA] Update blocked: %s", lastError.c_str());
            return false;
        }
    }
    
    char msg[150];
    snprintf(msg, sizeof(msg),
             "Boat Monitor: Starting firmware update from v%s to v%s. Device may be offline for 1-2 minutes.",
             versionInfo.currentVersion.c_str(), versionInfo.availableVersion.c_str());
    sendNotification(msg);
    LOG_INFO("[OTA] %s", msg);
    
    // Download and install
    bool success = downloadAndInstall(versionInfo.downloadUrl, versionInfo.firmwareSize);
    
    if (success) {
        // Set first boot flag before rebooting
        setFirstBootFlag();
        
        currentState = OTAState::SUCCESS;
        LOG_INFO("[OTA] Update successful! Rebooting in 3 seconds...");
        delay(3000);
        ESP.restart();
    } else {
        currentState = OTAState::FAILED;
        
        snprintf(msg, sizeof(msg),
                 "Boat Monitor: Firmware update FAILED - %s. Still running v%s.",
                 lastError.c_str(), versionInfo.currentVersion.c_str());
        sendNotification(msg);
        LOG_CRITICAL("[OTA] %s", msg);
    }
    
    return success;
}

bool OTAManager::downloadAndInstall(const String& url, size_t expectedSize) {
    currentState = OTAState::DOWNLOADING;
    
    HTTPClient http;
    http.begin(url);
    
    // Add GitHub token if available (for private repos)
    if (!config.githubToken.isEmpty()) {
        http.addHeader("Authorization", "token " + config.githubToken);
    }
    
    int httpCode = http.GET();
    
    if (httpCode != HTTP_CODE_OK) {
        lastError = "Download failed: HTTP " + String(httpCode);
        LOG_CRITICAL("[OTA] %s", lastError.c_str());
        http.end();
        return false;
    }
    
    int contentLength = http.getSize();
    
    if (contentLength <= 0 || (expectedSize > 0 && (size_t)contentLength != expectedSize)) {
        lastError = "Invalid content length";
        LOG_CRITICAL("[OTA] %s: expected %u, got %d", lastError.c_str(), expectedSize, contentLength);
        http.end();
        return false;
    }
    
    LOG_INFO("[OTA] Downloading firmware: %d bytes", contentLength);
    
    // Check if there's enough space
    if (!Update.begin(contentLength)) {
        lastError = "Not enough space for update";
        LOG_CRITICAL("[OTA] %s", lastError.c_str());
        http.end();
        return false;
    }
    
    currentState = OTAState::INSTALLING;
    
    // Get stream
    WiFiClient* stream = http.getStreamPtr();
    
    uint8_t buffer[OTA_BUFFER_SIZE];
    size_t written = 0;
    size_t lastProgress = 0;
    
    while (http.connected() && written < contentLength) {
        size_t available = stream->available();
        
        if (available) {
            size_t bytesToRead = (available > sizeof(buffer)) ? sizeof(buffer) : available;
            size_t bytesRead = stream->readBytes(buffer, bytesToRead);
            
            size_t bytesWritten = Update.write(buffer, bytesRead);
            if (bytesWritten != bytesRead) {
                lastError = "Write error";
                LOG_CRITICAL("[OTA] %s", lastError.c_str());
                Update.abort();
                http.end();
                return false;
            }
            
            written += bytesWritten;
            
            // Log progress every 10%
            size_t progress = (written * 100) / contentLength;
            if (progress >= lastProgress + 10) {
                LOG_INFO("[OTA] Progress: %u%%", progress);
                lastProgress = progress;
            }
        }
        
        delay(1);
    }
    
    http.end();
    
    if (written != contentLength) {
        lastError = "Download incomplete";
        LOG_CRITICAL("[OTA] %s: %u/%d bytes", lastError.c_str(), written, contentLength);
        Update.abort();
        return false;
    }
    
    LOG_INFO("[OTA] Download complete: %u bytes", written);
    
    // Finalize update
    if (!Update.end(true)) {
        lastError = "Update.end() failed";
        LOG_CRITICAL("[OTA] %s: %s", lastError.c_str(), Update.errorString());
        return false;
    }
    
    if (!Update.isFinished()) {
        lastError = "Update not finished";
        LOG_CRITICAL("[OTA] %s", lastError.c_str());
        return false;
    }
    
    LOG_INFO("[OTA] Update successfully written to flash");
    return true;
}

// Configuration setters

void OTAManager::setGitHubRepo(const char* owner, const char* repo) {
    config.githubOwner = String(owner);
    config.githubRepo = String(repo);
    saveConfig();
    LOG_INFO("[OTA] GitHub repo set to: %s/%s", owner, repo);
}

void OTAManager::setGitHubToken(const char* token) {
    config.githubToken = String(token);
    saveConfig();
    LOG_INFO("[OTA] GitHub token %s", token && strlen(token) > 0 ? "set" : "cleared");
}

void OTAManager::setUpdatePassword(const char* password) {
    config.updatePassword = String(password);
    saveConfig();
    LOG_INFO("[OTA] Update password %s", password && strlen(password) > 0 ? "set" : "cleared");
}

void OTAManager::setAutoCheck(bool enabled, unsigned long intervalMs) {
    config.autoCheckEnabled = enabled;
    config.checkIntervalMs = intervalMs;
    saveConfig();
    LOG_INFO("[OTA] Auto-check %s, interval: %lu hours", 
             enabled ? "enabled" : "disabled", intervalMs / 3600000);
}

void OTAManager::setAutoInstall(bool enabled) {
    config.autoInstallEnabled = enabled;
    saveConfig();
    LOG_INFO("[OTA] Auto-install %s", enabled ? "enabled" : "disabled");
}

void OTAManager::setNotificationsEnabled(bool enabled) {
    config.notificationsEnabled = enabled;
    saveConfig();
    LOG_INFO("[OTA] Notifications %s", enabled ? "enabled" : "disabled");
}
