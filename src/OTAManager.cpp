#include "OTAManager.h"
#include "Logger.h"
#include "Version.h"
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>

OTAManager::OTAManager(NotificationWorker* notif)
    : notifier(notif), currentState(OTAState::IDLE),
      lastCheckTime(0), firstBootAfterUpdate(false), rollbackOccurred(false),
      stateMux(nullptr), checkTaskHandle(nullptr), checkRequested(false) {

    versionInfo.currentVersion = FIRMWARE_VERSION;
    stateMux = xSemaphoreCreateMutex();
}

OTAManager::~OTAManager() {
    if (checkTaskHandle) {
        vTaskDelete(checkTaskHandle);
        checkTaskHandle = nullptr;
    }
    if (stateMux) {
        vSemaphoreDelete(stateMux);
        stateMux = nullptr;
    }
    preferences.end();
}

void OTAManager::begin() {
    LOG_INFO("[OTA] Initializing OTA Manager v%s", versionInfo.currentVersion.c_str());

    // Recover from any interrupted update state (device rebooted during update)
    if (currentState == OTAState::DOWNLOADING ||
        currentState == OTAState::INSTALLING ||
        currentState == OTAState::CHECKING) {
        LOG_INFO("[OTA] Recovering from interrupted update state");
        currentState = OTAState::IDLE;
        lastError = "Update interrupted by reboot";
    }

    if (currentState == OTAState::FAILED) {
        LOG_INFO("[OTA] Clearing previous FAILED state on boot");
        currentState = OTAState::IDLE;
    }

    // Load configuration from NVS
    loadConfig();

    // Check if this is first boot after update (may call sendNotification)
    checkFirstBoot();

    LOG_INFO("[OTA] GitHub Repo: %s/%s", config.githubOwner.c_str(), config.githubRepo.c_str());
    LOG_INFO("[OTA] Auto-check: %s (interval: %lu hours)",
             config.autoCheckEnabled ? "enabled" : "disabled",
             config.checkIntervalMs / MS_PER_HOUR);
    LOG_INFO("[OTA] Auto-install: %s", config.autoInstallEnabled ? "enabled" : "disabled");

    // Spawn the background check task on Core 0 (alongside NotificationWorker).
    // The task blocks most of the time, waking only when checkRequested is set
    // or the auto-check interval elapses. The download/install still runs on the
    // loop task (Core 1), intentionally — the device is out of service anyway.
    BaseType_t ok = xTaskCreatePinnedToCore(
        checkTaskEntry, "ota_check", OTA_TASK_STACK,
        this, OTA_TASK_PRIORITY, &checkTaskHandle, OTA_TASK_CORE);
    if (ok != pdPASS) {
        LOG_CRITICAL("[OTA] Failed to create check task — OTA version checks unavailable");
        checkTaskHandle = nullptr;
    } else {
        LOG_INFO("[OTA] Background check task started on Core %d", OTA_TASK_CORE);
    }
}

// Called from main loop — only handles auto-install.
// The expensive HTTP GET runs in checkTaskEntry(), not here.
void OTAManager::loopInstallOnly() {
    // Auto-recover from FAILED state after some time to allow retry
    static unsigned long failedStateTime = 0;
    if (currentState == OTAState::FAILED) {
        if (failedStateTime == 0) {
            failedStateTime = millis();
        } else if (millis() - failedStateTime > FAILED_STATE_RECOVERY_MS) {
            LOG_INFO("[OTA] Auto-recovering from FAILED state");
            if (xSemaphoreTake(stateMux, pdMS_TO_TICKS(50)) == pdTRUE) {
                currentState = OTAState::IDLE;
                xSemaphoreGive(stateMux);
            }
            failedStateTime = 0;
        }
        return;
    } else {
        failedStateTime = 0;
    }

    // Check if auto-install is enabled and update is available
    if (config.autoInstallEnabled && currentState == OTAState::UPDATE_AVAILABLE) {
        LOG_INFO("[OTA] Auto-install enabled and update available - starting automatic installation");
        startUpdate(nullptr);
    }
}

// Background task: wakes every second to check if the auto-check interval has
// elapsed, then performs the GitHub API call. Does NOT touch the WDT — it runs
// on Core 0, separate from the loop task's watchdog registration.
void OTAManager::checkTaskEntry(void* arg) {
    static_cast<OTAManager*>(arg)->runCheckTask();
}

void OTAManager::runCheckTask() {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000)); // Wake every second to check timer

        // Don't interfere while downloading/installing on the loop task
        if (currentState == OTAState::DOWNLOADING || currentState == OTAState::INSTALLING) {
            continue;
        }

        bool doCheck = checkRequested;
        checkRequested = false;

        if (!doCheck && config.autoCheckEnabled) {
            doCheck = (millis() - lastCheckTime) >= config.checkIntervalMs;
        }

        if (doCheck && WiFi.isConnected()) {
            LOG_INFO("[OTA] Background check task: starting version check");
            checkForUpdates();
        }
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
        char msg[NOTIFICATION_MESSAGE_BUFFER_SIZE];
        snprintf(msg, sizeof(msg),
                 "BilgeRise: Firmware updated successfully! v%s -> v%s. System online.",
                 previousVersion.c_str(), versionInfo.currentVersion.c_str());
        sendNotification(msg);
        LOG_INFO("[OTA] %s", msg);
        clearFirstBootFlag();
    } else if (rollbackOccurred && !previousVersion.isEmpty()) {
        char msg[NOTIFICATION_MESSAGE_BUFFER_SIZE];
        snprintf(msg, sizeof(msg),
                 "BilgeRise: New firmware v%s failed to boot. Rolled back to v%s. System stable.",
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

    config.githubOwner = preferences.getString("gh_owner", "Robert336");
    config.githubRepo = preferences.getString("gh_repo", "BoatReporterESP");
    config.githubToken = preferences.getString("gh_token", "");
    config.updatePassword = preferences.getString("password", "");
    config.autoCheckEnabled = preferences.getBool("auto_check", true);
    config.autoInstallEnabled = preferences.getBool("auto_install", true);
    config.checkIntervalMs = preferences.getULong("check_interval", DEFAULT_CHECK_INTERVAL_MS);
    config.notificationsEnabled = preferences.getBool("notify", true);
    // Note: We intentionally don't restore lastCheckTime from NVS here because
    // millis() resets to 0 on reboot. Using stored value would cause underflow.
    lastCheckTime = millis();

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

// Route all OTA notifications through NotificationWorker so they run on the
// worker task without blocking the loop or the OTA check task.
void OTAManager::sendNotification(const char* message) {
    if (!config.notificationsEnabled || !notifier) {
        return;
    }
    notifier->enqueue(message);
}

bool OTAManager::manualCheckForUpdates() {
    // Signal the background task rather than calling checkForUpdates() directly
    // from the loop task (which would block loop() for up to API_TIMEOUT_MS).
    checkRequested = true;
    return true;
}

bool OTAManager::checkForUpdates() {
    // Check WiFi connectivity first
    if (!WiFi.isConnected()) {
        if (xSemaphoreTake(stateMux, pdMS_TO_TICKS(100)) == pdTRUE) {
            lastError = "No WiFi connection";
            xSemaphoreGive(stateMux);
        }
        LOG_INFO("[OTA] No WiFi connection");
        return false;
    }

    if (config.githubOwner.isEmpty() || config.githubRepo.isEmpty()) {
        if (xSemaphoreTake(stateMux, pdMS_TO_TICKS(100)) == pdTRUE) {
            lastError = "GitHub repository not configured";
            currentState = OTAState::IDLE;
            xSemaphoreGive(stateMux);
        }
        return false;
    }

    if (xSemaphoreTake(stateMux, pdMS_TO_TICKS(100)) == pdTRUE) {
        currentState = OTAState::CHECKING;
        lastCheckTime = millis();
        xSemaphoreGive(stateMux);
    }

    LOG_INFO("[OTA] Checking for updates from GitHub...");

    // Build GitHub API URL
    String url = "https://api.github.com/repos/" + config.githubOwner + "/" +
                 config.githubRepo + "/releases/latest";

    HTTPClient http;
    http.begin(url);
    http.setTimeout(API_TIMEOUT_MS);
    http.addHeader("User-Agent", "ESP32-BilgeRise");

    if (!config.githubToken.isEmpty()) {
        http.addHeader("Authorization", "Bearer " + config.githubToken);
    }

    int httpCode = http.GET();

    if (httpCode == HTTP_FORBIDDEN || httpCode == HTTP_TOO_MANY_REQUESTS) {
        const char* e = "GitHub API rate limited - try again later";
        LOG_INFO("[OTA] %s", e);
        http.end();
        if (xSemaphoreTake(stateMux, pdMS_TO_TICKS(100)) == pdTRUE) {
            lastError = e;
            currentState = OTAState::FAILED;
            xSemaphoreGive(stateMux);
        }
        return false;
    }

    if (httpCode != HTTP_CODE_OK) {
        LOG_INFO("[OTA] GitHub API request failed: %d", httpCode);
        http.end();
        if (xSemaphoreTake(stateMux, pdMS_TO_TICKS(100)) == pdTRUE) {
            lastError = "GitHub API request failed: " + String(httpCode);
            currentState = OTAState::FAILED;
            xSemaphoreGive(stateMux);
        }
        return false;
    }

    // --- P4: Filtered ArduinoJson parse — only fields we need are buffered ---
    // Filter: keep tag_name and assets[].{name,browser_download_url,size}
    StaticJsonDocument<128> filter;
    filter["tag_name"] = true;
    JsonObject assetFilter = filter["assets"].createNestedObject();
    assetFilter["name"]                 = true;
    assetFilter["browser_download_url"] = true;
    assetFilter["size"]                 = true;

    // Payload document: tag_name (32B) + one asset entry (URL ~120B + name ~20B + size 8B)
    // A generous 512 B is more than enough for the filtered subset.
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, *http.getStreamPtr(),
                                                 DeserializationOption::Filter(filter));
    http.end();

    if (error) {
        LOG_INFO("[OTA] Failed to parse GitHub API response: %s", error.c_str());
        if (xSemaphoreTake(stateMux, pdMS_TO_TICKS(100)) == pdTRUE) {
            lastError = "Failed to parse GitHub API response";
            currentState = OTAState::FAILED;
            xSemaphoreGive(stateMux);
        }
        return false;
    }

    // Extract version and download URL
    const char* tag_name = doc["tag_name"];
    if (!tag_name) {
        LOG_INFO("[OTA] No tag_name in release");
        if (xSemaphoreTake(stateMux, pdMS_TO_TICKS(100)) == pdTRUE) {
            lastError = "No tag_name in release";
            currentState = OTAState::FAILED;
            xSemaphoreGive(stateMux);
        }
        return false;
    }

    String latestVersion = String(tag_name);
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
        LOG_INFO("[OTA] No firmware.bin found in release");
        if (xSemaphoreTake(stateMux, pdMS_TO_TICKS(100)) == pdTRUE) {
            lastError = "No firmware.bin found in release";
            currentState = OTAState::FAILED;
            xSemaphoreGive(stateMux);
        }
        return false;
    }

    // Compare versions and update state
    bool updateAvailable = compareVersions(latestVersion, versionInfo.currentVersion);

    if (xSemaphoreTake(stateMux, pdMS_TO_TICKS(100)) == pdTRUE) {
        versionInfo.availableVersion = latestVersion;
        versionInfo.downloadUrl      = downloadUrl;
        versionInfo.firmwareSize     = firmwareSize;
        currentState = updateAvailable ? OTAState::UPDATE_AVAILABLE : OTAState::IDLE;
        xSemaphoreGive(stateMux);
    }

    if (updateAvailable) {
        char msg[SHORT_MESSAGE_BUFFER_SIZE];
        snprintf(msg, sizeof(msg),
                 "BilgeRise: Firmware update available v%s -> v%s",
                 versionInfo.currentVersion.c_str(), latestVersion.c_str());
        sendNotification(msg);
        LOG_INFO("[OTA] %s", msg);
        LOG_INFO("[OTA] Download URL: %s", downloadUrl.c_str());
        LOG_INFO("[OTA] Size: %u bytes", firmwareSize);
    } else {
        LOG_INFO("[OTA] Already on latest version v%s", versionInfo.currentVersion.c_str());
    }

    return updateAvailable;
}

bool OTAManager::compareVersions(const String& newVer, const String& currentVer) {
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

    // Pre-flight validation checks
    if (!validateFirmwareSize(versionInfo.firmwareSize)) {
        lastError = "Firmware size validation failed";
        LOG_CRITICAL("[OTA] %s", lastError.c_str());
        currentState = OTAState::FAILED;
        return false;
    }

    if (!checkFlashSpace(versionInfo.firmwareSize)) {
        lastError = "Not enough flash space for firmware";
        LOG_CRITICAL("[OTA] %s - required: %u bytes, available: %u bytes",
                  lastError.c_str(), versionInfo.firmwareSize, ESP.getFreeSketchSpace());
        currentState = OTAState::FAILED;
        return false;
    }

    if (!checkHeapAvailable(OTA_BUFFER_SIZE * 2)) {
        lastError = "Insufficient heap memory for download";
        LOG_CRITICAL("[OTA] %s - free heap: %u bytes", lastError.c_str(), ESP.getFreeHeap());
        currentState = OTAState::FAILED;
        return false;
    }

    if (!checkSignalStrength()) {
        lastError = "WiFi signal too weak for reliable download";
        LOG_CRITICAL("[OTA] %s - RSSI: %d dBm (minimum: %d dBm)",
                     lastError.c_str(), WiFi.RSSI(), OTA_MIN_RSSI_DBM);
        currentState = OTAState::FAILED;
        return false;
    }

    char msg[NOTIFICATION_MESSAGE_BUFFER_SIZE];
    snprintf(msg, sizeof(msg),
             "BilgeRise: Starting firmware update from v%s to v%s. Device may be offline for 1-2 minutes.",
             versionInfo.currentVersion.c_str(), versionInfo.availableVersion.c_str());
    sendNotification(msg);
    LOG_INFO("[OTA] %s", msg);

    bool success = downloadAndInstall(versionInfo.downloadUrl, versionInfo.firmwareSize);

    if (success) {
        setFirstBootFlag();
        currentState = OTAState::SUCCESS;
        LOG_INFO("[OTA] Update successful! Restarting immediately...");
        ESP.restart();
    } else {
        currentState = OTAState::FAILED;

        snprintf(msg, sizeof(msg),
                 "BilgeRise: Firmware update FAILED - %s. Still running v%s.",
                 lastError.c_str(), versionInfo.currentVersion.c_str());
        sendNotification(msg);
        LOG_CRITICAL("[OTA] %s", msg);
    }

    return success;
}

bool OTAManager::downloadAndInstall(const String& url, size_t expectedSize) {
    if (!WiFi.isConnected()) {
        lastError = "No WiFi connection";
        LOG_CRITICAL("[OTA] %s", lastError.c_str());
        return false;
    }

    currentState = OTAState::DOWNLOADING;

    HTTPClient http;
    http.begin(url);
    http.setTimeout((uint16_t)120);

    if (!config.githubToken.isEmpty()) {
        http.addHeader("Authorization", "Bearer " + config.githubToken);
    }

    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        lastError = "Download failed: HTTP " + String(httpCode);
        LOG_CRITICAL("[OTA] %s", lastError.c_str());
        http.end();
        return false;
    }

    int contentLength = http.getSize();

    if (contentLength <= 0) {
        lastError = "Invalid content length (zero or unknown)";
        LOG_CRITICAL("[OTA] %s", lastError.c_str());
        http.end();
        return false;
    }

    // Allow ±2% tolerance for Content-Encoding overhead
    if (expectedSize > 0) {
        size_t tolerance = (expectedSize * 2) / 100;
        size_t minSize = expectedSize > tolerance ? expectedSize - tolerance : expectedSize;
        size_t maxSize = expectedSize + tolerance;

        if ((size_t)contentLength < minSize || (size_t)contentLength > maxSize) {
            LOG_INFO("[OTA] Content length mismatch (tolerance +-2%%): expected %u, got %d bytes",
                     expectedSize, contentLength);
        }
    }

    LOG_INFO("[OTA] Downloading firmware: %d bytes", contentLength);

    if (!Update.begin(contentLength)) {
        lastError = "Not enough space for update";
        LOG_CRITICAL("[OTA] %s", lastError.c_str());
        http.end();
        return false;
    }

    currentState = OTAState::INSTALLING;

    WiFiClient* stream = http.getStreamPtr();

    uint8_t buffer[OTA_BUFFER_SIZE];
    size_t written = 0;
    size_t lastProgress = 0;

    unsigned long downloadStart = millis();
    unsigned long lastDataTime = millis();

    while (http.connected() && written < contentLength) {
        esp_task_wdt_reset(); // Download runs on the loop task; keep feeding the dog
        if (millis() - downloadStart > DOWNLOAD_TIMEOUT_MS) {
            lastError = "Download timeout - exceeded 5 minutes";
            LOG_CRITICAL("[OTA] %s", lastError.c_str());
            Update.abort();
            http.end();
            return false;
        }

        size_t available = stream->available();

        if (available) {
            lastDataTime = millis();
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

            size_t progress = (written * 100) / contentLength;
            if (progress >= lastProgress + PROGRESS_LOG_INTERVAL_PERCENT) {
                LOG_INFO("[OTA] Progress: %u%%", progress);
                lastProgress = progress;
            }
        } else {
            if (millis() - lastDataTime > STALL_TIMEOUT_MS) {
                lastError = "Download stalled - no data for 30 seconds";
                LOG_CRITICAL("[OTA] %s", lastError.c_str());
                Update.abort();
                http.end();
                return false;
            }
        }

        delay(DOWNLOAD_LOOP_DELAY_MS);
    }

    http.end();

    if (written != contentLength) {
        lastError = "Download incomplete";
        LOG_CRITICAL("[OTA] %s: %u/%d bytes", lastError.c_str(), written, contentLength);
        Update.abort();
        return false;
    }

    LOG_INFO("[OTA] Download complete: %u bytes", written);

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

bool OTAManager::validateFirmwareSize(size_t size) {
    constexpr size_t MIN_FIRMWARE_SIZE = 65536;   // 64KB minimum
    constexpr size_t MAX_FIRMWARE_SIZE = 4194304; // 4MB maximum

    if (size < MIN_FIRMWARE_SIZE || size > MAX_FIRMWARE_SIZE) {
        LOG_CRITICAL("[OTA] Firmware size out of bounds: %u bytes (valid: %u-%u)",
                  size, MIN_FIRMWARE_SIZE, MAX_FIRMWARE_SIZE);
        return false;
    }

    LOG_INFO("[OTA] Firmware size validation passed: %u bytes", size);
    return true;
}

bool OTAManager::checkFlashSpace(size_t requiredSize) {
    size_t freeSpace = ESP.getFreeSketchSpace();
    size_t requiredWithMargin = requiredSize + (requiredSize / 20);

    if (freeSpace < requiredWithMargin) {
        LOG_CRITICAL("[OTA] Insufficient flash space: need %u bytes, have %u bytes",
                  requiredWithMargin, freeSpace);
        return false;
    }

    LOG_INFO("[OTA] Flash space check passed: %u bytes available (need %u)",
              freeSpace, requiredWithMargin);
    return true;
}

bool OTAManager::checkHeapAvailable(size_t requiredSize) {
    uint32_t freeHeap = ESP.getFreeHeap();

    if (freeHeap < requiredSize) {
        LOG_CRITICAL("[OTA] Insufficient heap: need %u bytes, have %u bytes",
                  requiredSize, freeHeap);
        return false;
    }

    LOG_INFO("[OTA] Heap check passed: %u bytes available (need %u)",
              freeHeap, requiredSize);
    return true;
}

bool OTAManager::checkSignalStrength() {
    int rssi = WiFi.RSSI();

    if (rssi < OTA_MIN_RSSI_DBM) {
        LOG_CRITICAL("[OTA] Signal strength check FAILED: %d dBm (minimum: %d dBm)",
                  rssi, OTA_MIN_RSSI_DBM);
        return false;
    }

    LOG_INFO("[OTA] Signal strength check passed: %d dBm (minimum: %d dBm)",
              rssi, OTA_MIN_RSSI_DBM);
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
             enabled ? "enabled" : "disabled", intervalMs / MS_PER_HOUR);
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
