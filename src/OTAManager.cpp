#ifndef UNIT_TESTING

#include "OTAManager.h"
#include "Logger.h"
#include "Version.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include "mbedtls/sha256.h"

// Full Mozilla root CA bundle, embedded in the firmware by the ESP-IDF mbedTLS
// component (CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y in the precompiled
// arduino-esp32 SDK). Pointing WiFiClientSecure at this bundle lets us verify the
// TLS chain for BOTH api.github.com and the objects.githubusercontent.com
// (Fastly) redirect target without shipping or pinning any certificate ourselves,
// and it survives CA rotation on either host.
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");

OTAManager::OTAManager(NotificationWorker* notif)
    : notifier(notif), currentState(OTAState::IDLE),
      lastCheckTime(0),
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

    // Recover from any interrupted update state (device rebooted during update).
    // Runs single-threaded before the check task is spawned, so no lock needed.
    OTAState bootState = currentState.load();
    if (bootState == OTAState::DOWNLOADING ||
        bootState == OTAState::INSTALLING ||
        bootState == OTAState::CHECKING) {
        LOG_INFO("[OTA] Recovering from interrupted update state");
        currentState.store(OTAState::IDLE);
        lastError = "Update interrupted by reboot";
    } else if (bootState == OTAState::FAILED) {
        LOG_INFO("[OTA] Clearing previous FAILED state on boot");
        currentState.store(OTAState::IDLE);
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
    // The task blocks most of the time, waking only when checkRequested or
    // installRequested is set, or the auto-check interval elapses. Downloads
    // also run on this task — its 10KB stack can absorb the mbedTLS handshake,
    // which overflows the loop task's 8KB stack.
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
    if (currentState.load() == OTAState::FAILED) {
        if (failedStateTime == 0) {
            failedStateTime = millis();
        } else if (millis() - failedStateTime > FAILED_STATE_RECOVERY_MS) {
            LOG_INFO("[OTA] Auto-recovering from FAILED state");
            currentState.store(OTAState::IDLE);
            failedStateTime = 0;
        }
        return;
    } else {
        failedStateTime = 0;
    }

    // Check if auto-install is enabled and update is available
    if (config.autoInstallEnabled && currentState.load() == OTAState::UPDATE_AVAILABLE) {
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

        // An install request owns the task until it completes (or fails).
        // Runs here, not on the loop task: the mbedTLS TLS handshake during the
        // firmware download needs ~10KB of stack and overflows the loop task's
        // 8KB (observed as a stack-canary panic in ssl_cli/ECP crypto).
        if (installRequested) {
            installRequested = false;
            executeUpdate(installPassword[0] ? installPassword : nullptr);
            continue;
        }

        // Don't start a version check while a download/install is in flight.
        OTAState s = currentState.load();
        if (s == OTAState::DOWNLOADING || s == OTAState::INSTALLING) {
            continue;
        }

        bool doCheck = checkRequested;
        checkRequested = false;

        if (!doCheck && config.autoCheckEnabled) {
                // Compare wall-clock epochs (seconds) so scheduling is correct across
                // reboots and multi-month uptime. Skip if RTC hasn't synced yet —
                // unixTime will be ~0 and the diff would be enormous.
                Timestamp ts = TimeManagement::getInstance().getCurrentTimestamp();
                if (ts.isNTPSynced && lastCheckTime > 0) {
                    doCheck = (ts.unixTime - (time_t)lastCheckTime) >= (time_t)(config.checkIntervalMs / 1000UL);
                } else if (ts.isNTPSynced && lastCheckTime == 0) {
                    doCheck = true; // Never checked — fire immediately once RTC is up
                }
            }

        if (doCheck && WiFi.isConnected()) {
            LOG_INFO("[OTA] Background check task: starting version check");
            checkForUpdates();
        }
    }
}

void OTAManager::checkFirstBoot() {
    // Read update-pending state from NVS (read-only open).
    if (!preferences.begin(OTA_PREFERENCES_NAMESPACE, true)) {
        LOG_CRITICAL("[OTA] Failed to open preferences for first boot check");
        return;
    }

    bool updatePending    = preferences.getBool("upd_pending", false);
    String prevVersion    = preferences.getString("prev_version", "");
    String targetVersion  = preferences.getString("target_version", "");

    preferences.end();

    // The authoritative "did the OTA succeed?" signal is whether the running
    // firmware version matches the target we recorded before rebooting. This
    // works regardless of whether CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is set
    // in the IDF build config — the previous logic relied on
    // ESP_OTA_IMG_PENDING_VERIFY, which is only populated when rollback support
    // is enabled. On builds without it, every first boot after an OTA was
    // wrongly reported as a rollback ("Rolled back to v1.1.3" while actually
    // running v1.1.3).
    //
    // We still call esp_ota_mark_app_valid_cancel_rollback() defensively — it's
    // a no-op when rollback support is disabled, and confirms the image when
    // it is enabled.
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    bool isPendingVerify = (esp_ota_get_state_partition(running, &ota_state) == ESP_OK
                            && ota_state == ESP_OTA_IMG_PENDING_VERIFY);
    if (isPendingVerify) {
        esp_ota_mark_app_valid_cancel_rollback();
        LOG_INFO("[OTA] New firmware validated successfully");
    }

    if (!updatePending) {
        return; // Normal boot with no pending update.
    }

    // Compare the running version against the target we recorded before
    // rebooting. Match = successful update; mismatch = real rollback.
    if (versionInfo.currentVersion == targetVersion) {
        char msg[NOTIFICATION_MESSAGE_BUFFER_SIZE];
        snprintf(msg, sizeof(msg),
                 "BilgeRise: Firmware updated successfully! v%s -> v%s. System online.",
                 prevVersion.c_str(), versionInfo.currentVersion.c_str());
        sendNotification(msg);
        LOG_INFO("[OTA] %s", msg);
    } else {
        char msg[NOTIFICATION_MESSAGE_BUFFER_SIZE];
        snprintf(msg, sizeof(msg),
                 "BilgeRise: New firmware v%s failed to boot. Rolled back to v%s. System stable.",
                 targetVersion.c_str(), versionInfo.currentVersion.c_str());
        sendNotification(msg);
        LOG_CRITICAL("[OTA] %s", msg);
    }
    clearFirstBootFlag();
}

void OTAManager::setFirstBootFlag() {
    if (!preferences.begin(OTA_PREFERENCES_NAMESPACE, false)) {
        LOG_CRITICAL("[OTA] Failed to open preferences to set update pending flag");
        return;
    }

    preferences.putBool("upd_pending", true);
    preferences.putString("prev_version", versionInfo.currentVersion);   // version running now (old)
    preferences.putString("target_version", versionInfo.availableVersion); // version being installed
    preferences.end();
}

void OTAManager::clearFirstBootFlag() {
    if (!preferences.begin(OTA_PREFERENCES_NAMESPACE, false)) {
        return;
    }

    preferences.remove("upd_pending");
    preferences.remove("prev_version");
    preferences.remove("target_version");

    // Migration: remove legacy keys from old installs so they don't linger.
    preferences.remove("first_boot");
    preferences.remove("rollback");

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
    // Restore the wall-clock epoch of the last completed check from NVS. 0 = never
    // checked (or RTC not yet synced when the check ran). Storing epoch (not
    // millis()) makes the timestamp survive reboots and 49-day millis() wraps.
    lastCheckTime = preferences.getULong("last_check_epoch", 0);

    preferences.end();

    // Migration: clamp any pre-existing interval below the NVS-wear floor (12h)
    // and persist the corrected value so this only fires once per device.
    if (config.checkIntervalMs < MIN_CHECK_INTERVAL_MS) {
        LOG_INFO("[OTA] Migrating check interval %lu ms → %lu ms (NVS wear floor)",
                 config.checkIntervalMs, MIN_CHECK_INTERVAL_MS);
        config.checkIntervalMs = MIN_CHECK_INTERVAL_MS;
        saveConfig();
    }
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
    // Only write the epoch when a check has actually completed (non-zero).
    // saveConfig() is called from setters that don't run a check; writing 0
    // here would erase a valid timestamp from a prior session.
    if (lastCheckTime > 0) {
        preferences.putULong("last_check_epoch", lastCheckTime);
    }

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

// lastError is a heap-backed String written by the check task (Core 0) and the
// loop task (Core 1) and read by the web-server task. Every access goes through
// stateMux so a reader never copies the String while a writer reallocates it.
void OTAManager::setError(const String& msg) {
    if (xSemaphoreTake(stateMux, pdMS_TO_TICKS(100)) == pdTRUE) {
        lastError = msg;
        xSemaphoreGive(stateMux);
    }
}

String OTAManager::getLastError() const {
    String copy;
    if (xSemaphoreTake(stateMux, pdMS_TO_TICKS(100)) == pdTRUE) {
        copy = lastError;
        xSemaphoreGive(stateMux);
    }
    return copy;
}

String OTAManager::getAvailableVersion() const {
    String copy;
    if (xSemaphoreTake(stateMux, pdMS_TO_TICKS(100)) == pdTRUE) {
        copy = versionInfo.availableVersion;
        xSemaphoreGive(stateMux);
    }
    return copy;
}

unsigned long OTAManager::getTimeSinceLastCheckS() {
    if (lastCheckTime == 0) return 0;
    Timestamp ts = TimeManagement::getInstance().getCurrentTimestamp();
    if (!ts.isNTPSynced) return 0;
    time_t diff = ts.unixTime - (time_t)lastCheckTime;
    return (diff > 0) ? (unsigned long)diff : 0;
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
        setError("No WiFi connection");
        LOG_INFO("[OTA] No WiFi connection");
        return false;
    }

    if (config.githubOwner.isEmpty() || config.githubRepo.isEmpty()) {
        setError("GitHub repository not configured");
        currentState.store(OTAState::IDLE);
        return false;
    }

    currentState.store(OTAState::CHECKING);
    // Stamp the check with wall-clock epoch so it survives reboots.
    // Falls back to 0 if RTC hasn't synced yet (first boot without NTP).
    {
        Timestamp ts = TimeManagement::getInstance().getCurrentTimestamp();
        unsigned long epoch = ts.isNTPSynced ? (unsigned long)ts.unixTime : 0;
        if (xSemaphoreTake(stateMux, pdMS_TO_TICKS(100)) == pdTRUE) {
            lastCheckTime = epoch;
            xSemaphoreGive(stateMux);
        }
        // Persist immediately so a reboot doesn't lose the timestamp.
        // NVS write cost is bounded by MIN_CHECK_INTERVAL_MS (12h floor).
        if (epoch > 0) {
            saveConfig();
        }
    }

    LOG_INFO("[OTA] Checking for updates from GitHub...");

    // Build GitHub API URL
    String url = "https://api.github.com/repos/" + config.githubOwner + "/" +
                 config.githubRepo + "/releases/latest";

    // Verify the TLS chain against the embedded Mozilla root bundle. No insecure
    // fallback — a failed handshake fails the check rather than trusting the peer.
    WiFiClientSecure client;
    client.setCACertBundle(rootca_crt_bundle_start);

    HTTPClient http;
    if (!http.begin(client, url)) {
        setError("Failed to start HTTPS connection");
        currentState.store(OTAState::FAILED);
        return false;
    }
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
        setError(e);
        currentState.store(OTAState::FAILED);
        return false;
    }

    if (httpCode != HTTP_CODE_OK) {
        LOG_INFO("[OTA] GitHub API request failed: %d", httpCode);
        http.end();
        setError("GitHub API request failed: " + String(httpCode));
        currentState.store(OTAState::FAILED);
        return false;
    }

    // --- P4: Filtered ArduinoJson parse — only fields we need are buffered ---
    // Filter: keep tag_name and assets[].{name,browser_download_url,size,digest}
    StaticJsonDocument<160> filter;
    filter["tag_name"] = true;
    JsonObject assetFilter = filter["assets"].createNestedObject();
    assetFilter["name"]                 = true;
    assetFilter["browser_download_url"] = true;
    assetFilter["size"]                 = true;
    assetFilter["digest"]               = true;

    // Payload document: tag_name + a few asset entries (each ~URL 120B + name 20B +
    // size 8B + digest "sha256:"+64hex ~72B). 1 KB leaves headroom for releases
    // that carry several assets alongside firmware.bin.
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, *http.getStreamPtr(),
                                                 DeserializationOption::Filter(filter));
    http.end();

    if (error) {
        LOG_INFO("[OTA] Failed to parse GitHub API response: %s", error.c_str());
        setError("Failed to parse GitHub API response");
        currentState.store(OTAState::FAILED);
        return false;
    }

    // Extract version and download URL
    const char* tag_name = doc["tag_name"];
    if (!tag_name) {
        LOG_INFO("[OTA] No tag_name in release");
        setError("No tag_name in release");
        currentState.store(OTAState::FAILED);
        return false;
    }

    String latestVersion = String(tag_name);
    if (latestVersion.startsWith("v")) {
        latestVersion = latestVersion.substring(1);
    }

    // Find firmware.bin asset (and capture its SHA256 digest for verification)
    JsonArray assets = doc["assets"];
    String downloadUrl = "";
    String firmwareSha256 = "";
    size_t firmwareSize = 0;

    for (JsonObject asset : assets) {
        const char* name = asset["name"];
        if (name && String(name) == "firmware.bin") {
            downloadUrl = String(asset["browser_download_url"].as<const char*>());
            firmwareSize = asset["size"];
            // GitHub returns "digest":"sha256:<hex>" on release assets. Strip the
            // algorithm prefix so we keep the bare 64-char hex digest.
            const char* digest = asset["digest"];
            if (digest) {
                String d = String(digest);
                int colon = d.indexOf(':');
                firmwareSha256 = (colon >= 0) ? d.substring(colon + 1) : d;
            }
            break;
        }
    }

    if (downloadUrl.isEmpty()) {
        LOG_INFO("[OTA] No firmware.bin found in release");
        setError("No firmware.bin found in release");
        currentState.store(OTAState::FAILED);
        return false;
    }

    // Compare versions and update state
    bool updateAvailable = compareVersions(latestVersion, versionInfo.currentVersion);

    if (xSemaphoreTake(stateMux, pdMS_TO_TICKS(100)) == pdTRUE) {
        versionInfo.availableVersion = latestVersion;
        versionInfo.downloadUrl      = downloadUrl;
        versionInfo.firmwareSize     = firmwareSize;
        versionInfo.firmwareHash     = firmwareSha256;
        xSemaphoreGive(stateMux);
    }
    currentState.store(updateAvailable ? OTAState::UPDATE_AVAILABLE : OTAState::IDLE);

    if (updateAvailable) {
        char msg[SHORT_MESSAGE_BUFFER_SIZE];
        snprintf(msg, sizeof(msg),
                 "BilgeRise: Firmware update available v%s -> v%s",
                 versionInfo.currentVersion.c_str(), latestVersion.c_str());
        sendNotification(msg);
        LOG_INFO("[OTA] %s", msg);
        LOG_INFO("[OTA] Download URL: %s", downloadUrl.c_str());
        LOG_INFO("[OTA] Size: %u bytes", firmwareSize);
        if (firmwareSha256.isEmpty()) {
            LOG_CRITICAL("[OTA] WARNING: release has no SHA256 digest — install will be refused");
        }
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
    if (currentState.load() != OTAState::UPDATE_AVAILABLE) {
        setError("No update available");
        LOG_INFO("[OTA] No update available");
        return false;
    }

    // Check password if configured (validated here so the caller gets an
    // immediate failure response instead of an async one).
    if (!config.updatePassword.isEmpty()) {
        if (!password || config.updatePassword != String(password)) {
            setError("Invalid password");
            LOG_INFO("[OTA] Update blocked: Invalid password");
            return false;
        }
    }

    // Hand the install to the check task — see runCheckTask() for why the
    // download can't run on the loop task. Snapshot the password since the
    // caller's pointer (e.g. a web-server arg) goes out of scope on return.
    if (password) {
        strncpy(installPassword, password, sizeof(installPassword) - 1);
        installPassword[sizeof(installPassword) - 1] = '\0';
    } else {
        installPassword[0] = '\0';
    }
    installRequested = true;
    LOG_INFO("[OTA] Install queued on background task");
    return true;
}

bool OTAManager::executeUpdate(const char* password) {
    (void)password; // Already validated in startUpdate()

    if (currentState.load() != OTAState::UPDATE_AVAILABLE) {
        setError("No update available");
        LOG_INFO("[OTA] No update available");
        return false;
    }

    // Snapshot the version info under the mutex so a subsequent check can't
    // mutate these Strings out from under us mid-update.
    String url, expectedHash, availVer, curVer;
    size_t fwSize = 0;
    if (xSemaphoreTake(stateMux, pdMS_TO_TICKS(100)) == pdTRUE) {
        url          = versionInfo.downloadUrl;
        expectedHash = versionInfo.firmwareHash;
        availVer     = versionInfo.availableVersion;
        curVer       = versionInfo.currentVersion;
        fwSize       = versionInfo.firmwareSize;
        xSemaphoreGive(stateMux);
    }

    // Fail closed: never flash firmware we can't verify against a known digest.
    if (expectedHash.isEmpty()) {
        setError("No SHA256 digest in release - refusing to flash unverified firmware");
        LOG_CRITICAL("[OTA] No SHA256 digest in release - refusing to flash unverified firmware");
        currentState.store(OTAState::FAILED);
        return false;
    }

    // Pre-flight validation checks
    if (!validateFirmwareSize(fwSize)) {
        setError("Firmware size validation failed");
        LOG_CRITICAL("[OTA] Firmware size validation failed");
        currentState.store(OTAState::FAILED);
        return false;
    }

    if (!checkFlashSpace(fwSize)) {
        setError("Not enough flash space for firmware");
        LOG_CRITICAL("[OTA] Not enough flash space - required: %u bytes, available: %u bytes",
                  fwSize, ESP.getFreeSketchSpace());
        currentState.store(OTAState::FAILED);
        return false;
    }

    if (!checkHeapAvailable(OTA_BUFFER_SIZE * 2)) {
        setError("Insufficient heap memory for download");
        LOG_CRITICAL("[OTA] Insufficient heap memory - free heap: %u bytes", ESP.getFreeHeap());
        currentState.store(OTAState::FAILED);
        return false;
    }

    if (!checkSignalStrength()) {
        setError("WiFi signal too weak for reliable download");
        LOG_CRITICAL("[OTA] WiFi signal too weak - RSSI: %d dBm (minimum: %d dBm)",
                     WiFi.RSSI(), OTA_MIN_RSSI_DBM);
        currentState.store(OTAState::FAILED);
        return false;
    }

    char msg[NOTIFICATION_MESSAGE_BUFFER_SIZE];
    snprintf(msg, sizeof(msg),
             "BilgeRise: Starting firmware update from v%s to v%s. Device may be offline for 1-2 minutes.",
             curVer.c_str(), availVer.c_str());
    sendNotification(msg);
    LOG_INFO("[OTA] %s", msg);

    bool success = downloadAndInstall(url, fwSize, expectedHash);

    if (success) {
        setFirstBootFlag();
        currentState.store(OTAState::SUCCESS);
        LOG_INFO("[OTA] Update successful! Restarting immediately...");
        ESP.restart();
    } else {
        currentState.store(OTAState::FAILED);

        snprintf(msg, sizeof(msg),
                 "BilgeRise: Firmware update FAILED - %s. Still running v%s.",
                 getLastError().c_str(), curVer.c_str());
        sendNotification(msg);
        LOG_CRITICAL("[OTA] %s", msg);
    }

    return success;
}

bool OTAManager::downloadAndInstall(const String& url, size_t expectedSize, const String& expectedSha256) {
    if (!WiFi.isConnected()) {
        setError("No WiFi connection");
        LOG_CRITICAL("[OTA] No WiFi connection");
        return false;
    }

    currentState.store(OTAState::DOWNLOADING);

    // Verify the TLS chain against the embedded Mozilla root bundle. The same
    // client is reused across the GitHub 302 redirect to objects.githubusercontent.com,
    // so a CA bundle (not a pinned cert) is what makes both hops verifiable.
    WiFiClientSecure client;
    client.setCACertBundle(rootca_crt_bundle_start);

    HTTPClient http;
    if (!http.begin(client, url)) {
        setError("Failed to start HTTPS download");
        LOG_CRITICAL("[OTA] Failed to start HTTPS download");
        return false;
    }

    // GitHub release asset URLs (browser_download_url) return an HTTP 302 to
    // objects.githubusercontent.com. Without this, GET() fails with HTTP 302.
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    // Note: for PRIVATE repos the redirect target is a pre-signed S3 URL that
    // rejects an Authorization header. This project's repo is public and the
    // token is only added when set, so the existing logic below is left as-is.

    // HTTPClient::setTimeout() takes uint16_t milliseconds (max ~65535 ms). This
    // is the socket/header timeout only — the full download is bounded by the
    // DOWNLOAD_TIMEOUT_MS watchdog in the read loop.
    http.setTimeout((uint16_t)60000);

    if (!config.githubToken.isEmpty()) {
        http.addHeader("Authorization", "Bearer " + config.githubToken);
    }

    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        setError("Download failed: HTTP " + String(httpCode));
        LOG_CRITICAL("[OTA] Download failed: HTTP %d", httpCode);
        http.end();
        return false;
    }

    int contentLength = http.getSize();

    if (contentLength <= 0) {
        setError("Invalid content length (zero or unknown)");
        LOG_CRITICAL("[OTA] Invalid content length (zero or unknown)");
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
        setError("Not enough space for update");
        LOG_CRITICAL("[OTA] Not enough space for update");
        http.end();
        return false;
    }

    currentState.store(OTAState::INSTALLING);

    // Hash the payload as it streams so we can verify the firmware against the
    // release's published SHA256 BEFORE committing the image to the boot partition.
    mbedtls_sha256_context shaCtx;
    mbedtls_sha256_init(&shaCtx);
    mbedtls_sha256_starts_ret(&shaCtx, 0); // 0 = SHA-256 (not SHA-224)

    WiFiClient* stream = http.getStreamPtr();

    uint8_t buffer[OTA_BUFFER_SIZE];
    size_t written = 0;
    size_t lastProgress = 0;

    unsigned long downloadStart = millis();
    unsigned long lastDataTime = millis();

    // NOTE: no esp_task_wdt_reset() here. The download runs on the Core 0
    // check task, which is intentionally NOT registered with the task watchdog
    // (a 5-minute download would exceed the 10s WDT). The loop task on Core 1
    // keeps running and feeding the WDT throughout, so a genuine hang is still
    // caught by the download's own STALL_TIMEOUT_MS / DOWNLOAD_TIMEOUT_MS.
    while (http.connected() && written < contentLength) {
        // C2: don't let a firmware download blind the flood sensor. The
        // download blocks this task for up to 5 minutes; waterSensor.readLevel()
        // and the state machine keep running on the loop task, so a real flood
        // is still detected — abort (leaving current firmware intact) if a
        // Tier-1+ flood condition appears mid-download.
        if (floodCheckCb && floodCheckCb(floodCheckCtx)) {
            setError("OTA aborted - flood condition detected mid-download");
            LOG_CRITICAL("[OTA] Flood condition detected mid-download - aborting to preserve flood monitoring");
            Update.abort();
            mbedtls_sha256_free(&shaCtx);
            http.end();
            return false;
        }
        if (millis() - downloadStart > DOWNLOAD_TIMEOUT_MS) {
            setError("Download timeout - exceeded 5 minutes");
            LOG_CRITICAL("[OTA] Download timeout - exceeded 5 minutes");
            Update.abort();
            mbedtls_sha256_free(&shaCtx);
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
                setError("Write error");
                LOG_CRITICAL("[OTA] Write error");
                Update.abort();
                mbedtls_sha256_free(&shaCtx);
                http.end();
                return false;
            }

            mbedtls_sha256_update_ret(&shaCtx, buffer, bytesRead);
            written += bytesWritten;

            size_t progress = (written * 100) / contentLength;
            if (progress >= lastProgress + PROGRESS_LOG_INTERVAL_PERCENT) {
                LOG_INFO("[OTA] Progress: %u%%", progress);
                lastProgress = progress;
            }
        } else {
            if (millis() - lastDataTime > STALL_TIMEOUT_MS) {
                setError("Download stalled - no data for 30 seconds");
                LOG_CRITICAL("[OTA] Download stalled - no data for 30 seconds");
                Update.abort();
                mbedtls_sha256_free(&shaCtx);
                http.end();
                return false;
            }
        }

        delay(DOWNLOAD_LOOP_DELAY_MS);
    }

    http.end();

    if (written != contentLength) {
        setError("Download incomplete");
        LOG_CRITICAL("[OTA] Download incomplete: %u/%d bytes", written, contentLength);
        Update.abort();
        mbedtls_sha256_free(&shaCtx);
        return false;
    }

    // Finalize the hash and verify it BEFORE committing the image. A mismatch
    // means corruption or tampering — abort without activating the partition.
    uint8_t hash[32];
    mbedtls_sha256_finish_ret(&shaCtx, hash);
    mbedtls_sha256_free(&shaCtx);

    char hashHex[65];
    for (int i = 0; i < 32; i++) {
        snprintf(hashHex + (i * 2), 3, "%02x", hash[i]);
    }

    if (!expectedSha256.equalsIgnoreCase(hashHex)) {
        setError("Firmware SHA256 mismatch - aborting install");
        LOG_CRITICAL("[OTA] Firmware SHA256 mismatch - expected %s, got %s",
                     expectedSha256.c_str(), hashHex);
        Update.abort();
        return false;
    }

    LOG_INFO("[OTA] Firmware SHA256 verified: %s", hashHex);
    LOG_INFO("[OTA] Download complete: %u bytes", written);

    if (!Update.end(true)) {
        setError(String("Update.end() failed: ") + Update.errorString());
        LOG_CRITICAL("[OTA] Update.end() failed: %s", Update.errorString());
        return false;
    }

    if (!Update.isFinished()) {
        setError("Update not finished");
        LOG_CRITICAL("[OTA] Update not finished");
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

// Minimum LARGEST-CONTIGUOUS-block heap required to attempt a firmware
// download. The mbedTLS handshake against GitHub (full Mozilla root bundle)
// needs ~40-50 KB of contiguous heap; the old guard of OTA_BUFFER_SIZE*2
// (2 KB) against TOTAL free heap passed even on a fragmented heap where the
// handshake was guaranteed to fail mid-flash. 56 KB gives margin over the
// observed handshake peak plus the HTTPClient/Update overhead.
static constexpr size_t OTA_MIN_CONTIGUOUS_HEAP = 56 * 1024;

bool OTAManager::checkHeapAvailable(size_t /*unused*/) {
    // TLS needs one large CONTIGUOUS allocation, so total free heap is the
    // wrong metric — a heap fragmented into many small blocks passes a total-
    // size check yet cannot satisfy the handshake. Check the largest free
    // block instead.
    uint32_t maxBlock = ESP.getMaxAllocHeap();
    uint32_t freeHeap = ESP.getFreeHeap();

    if (maxBlock < OTA_MIN_CONTIGUOUS_HEAP) {
        LOG_CRITICAL("[OTA] Insufficient contiguous heap: largest block %u bytes (need %u), total free %u",
                  maxBlock, (unsigned)OTA_MIN_CONTIGUOUS_HEAP, freeHeap);
        return false;
    }

    LOG_INFO("[OTA] Heap check passed: largest block %u bytes, total free %u (need %u contiguous)",
              maxBlock, freeHeap, (unsigned)OTA_MIN_CONTIGUOUS_HEAP);
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
    // Enforce the NVS-wear floor: 12h minimum → max 730 writes/year.
    config.checkIntervalMs = max(intervalMs, MIN_CHECK_INTERVAL_MS);
    saveConfig();
    LOG_INFO("[OTA] Auto-check %s, interval: %lu hours",
             enabled ? "enabled" : "disabled", config.checkIntervalMs / MS_PER_HOUR);
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

uint32_t OTAManager::getCheckTaskStackHighWaterMark() const {
    if (!checkTaskHandle) return 0;
    return (uint32_t)uxTaskGetStackHighWaterMark(checkTaskHandle);
}

#endif // UNIT_TESTING
