#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "NotificationWorker.h"

constexpr const char OTA_PREFERENCES_NAMESPACE[] = "ota_config";
constexpr unsigned long DEFAULT_CHECK_INTERVAL_MS = 86400000; // 24 hours
constexpr int OTA_BUFFER_SIZE = 1024; // Download buffer size
constexpr int OTA_MIN_RSSI_DBM = -70; // Minimum signal strength required before starting a download

// Time constants
constexpr unsigned long MS_PER_HOUR = 3600000; // Milliseconds in one hour
constexpr unsigned long FAILED_STATE_RECOVERY_MS = 300000; // 5 minutes
constexpr unsigned long API_TIMEOUT_MS = 30000; // 30 seconds
constexpr unsigned long FIRMWARE_DOWNLOAD_TIMEOUT_MS = 120000; // 2 minutes
constexpr unsigned long DOWNLOAD_TIMEOUT_MS = 300000; // 5 minutes
constexpr unsigned long STALL_TIMEOUT_MS = 30000; // 30 seconds without data
constexpr unsigned long REBOOT_DELAY_MS = 3000; // 3 seconds

// Buffer sizes
constexpr size_t NOTIFICATION_MESSAGE_BUFFER_SIZE = 150;
constexpr size_t SHORT_MESSAGE_BUFFER_SIZE = 120;

// HTTP status codes
constexpr int HTTP_FORBIDDEN = 403;
constexpr int HTTP_TOO_MANY_REQUESTS = 429;

// Progress reporting
constexpr size_t PROGRESS_LOG_INTERVAL_PERCENT = 10;
constexpr unsigned long DOWNLOAD_LOOP_DELAY_MS = 1;

// OTA check task stack and priority
constexpr uint32_t    OTA_TASK_STACK    = 8192;
constexpr UBaseType_t OTA_TASK_PRIORITY = 1;
constexpr BaseType_t  OTA_TASK_CORE     = 0;

enum class OTAState {
    IDLE,
    CHECKING,
    UPDATE_AVAILABLE,
    DOWNLOADING,
    INSTALLING,
    SUCCESS,
    FAILED
};

struct OTAConfig {
    String githubOwner;
    String githubRepo;
    String githubToken;         // Optional, for private repos
    String updatePassword;      // Optional password protection
    bool autoCheckEnabled;
    bool autoInstallEnabled;    // Automatically install updates when found
    unsigned long checkIntervalMs;
    bool notificationsEnabled;
};

struct VersionInfo {
    String currentVersion;
    String availableVersion;
    String downloadUrl;
    size_t firmwareSize;
    String firmwareHash;        // Optional SHA256 hash for verification
};

/**
 * OTAManager - Manages Over-The-Air firmware updates
 *
 * Features:
 * - Check GitHub Releases for new firmware versions (on a dedicated Core 0 task)
 * - Download and install firmware updates via HTTPS (on the loop task — device
 *   is intentionally out-of-service during a download anyway)
 * - Automatic version checking on schedule without blocking loop()
 * - Manual update triggering via web interface
 * - Notification integration via NotificationWorker (no direct send() calls)
 * - Rollback detection and notification
 * - NVS configuration storage
 */
class OTAManager {
private:
    // Services
    NotificationWorker* notifier;
    Preferences preferences;

    // State (guarded by stateMux for cross-task access)
    OTAState      currentState;
    OTAConfig     config;
    VersionInfo   versionInfo;
    unsigned long lastCheckTime;
    String        lastError;
    bool          firstBootAfterUpdate;
    bool          rollbackOccurred;

    // FreeRTOS primitives for the background check task
    SemaphoreHandle_t stateMux;      // Mutex protecting currentState / versionInfo / lastError
    TaskHandle_t      checkTaskHandle;
    volatile bool     checkRequested; // Set by loop task, consumed by check task

    // Helper methods
    void loadConfig();
    void saveConfig();
    void sendNotification(const char* message);
    bool checkForUpdates();
    bool compareVersions(const String& v1, const String& v2);
    bool downloadAndInstall(const String& url, size_t expectedSize);
    void checkFirstBoot();
    void setFirstBootFlag();
    void clearFirstBootFlag();
    int parseVersionComponent(const String& version, int component);

    // Pre-flight validation methods
    bool validateFirmwareSize(size_t size);
    bool checkFlashSpace(size_t requiredSize);
    bool checkHeapAvailable(size_t requiredSize);
    bool checkSignalStrength();

    // Background check task
    static void checkTaskEntry(void* arg);
    void runCheckTask();

public:
    OTAManager(NotificationWorker* notifier = nullptr);
    ~OTAManager();

    // Core operations
    void begin();

    // Called from main loop: handles auto-install only.
    // The version check runs in the background task, not here.
    void loopInstallOnly();

    // Manual control
    bool manualCheckForUpdates();
    bool startUpdate(const char* password = nullptr);

    // Configuration
    void setGitHubRepo(const char* owner, const char* repo);
    void setGitHubToken(const char* token);
    void setUpdatePassword(const char* password);
    void setAutoCheck(bool enabled, unsigned long intervalMs = DEFAULT_CHECK_INTERVAL_MS);
    void setAutoInstall(bool enabled);
    void setNotificationsEnabled(bool enabled);

    // Getters (lock-free for simple booleans; use mutex for String fields)
    OTAState getState() const { return currentState; }
    String getCurrentVersion() const { return versionInfo.currentVersion; }
    String getAvailableVersion() const { return versionInfo.availableVersion; }
    String getLastError() const { return lastError; }
    bool isUpdateAvailable() const { return currentState == OTAState::UPDATE_AVAILABLE; }
    bool isAutoCheckEnabled() const { return config.autoCheckEnabled; }
    bool isAutoInstallEnabled() const { return config.autoInstallEnabled; }
    bool areNotificationsEnabled() const { return config.notificationsEnabled; }
    String getGitHubRepo() const { return config.githubOwner + "/" + config.githubRepo; }
    unsigned long getCheckIntervalMs() const { return config.checkIntervalMs; }
    unsigned long getTimeSinceLastCheck() const { return millis() - lastCheckTime; }
};
