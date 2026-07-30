#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <atomic>
#include "NotificationWorker.h"
#include "TimeManagement.h"

constexpr const char OTA_PREFERENCES_NAMESPACE[] = "ota_config";
constexpr unsigned long DEFAULT_CHECK_INTERVAL_MS = 86400000; // 24 hours
constexpr unsigned long MIN_CHECK_INTERVAL_MS = 43200000; // 12 hours (NVS wear floor)
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
constexpr uint32_t    OTA_TASK_STACK    = 10240;
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
 * OTAManager — GitHub Releases firmware updates (check task on Core 0).
 */
class OTAManager {
private:
    // Services
    NotificationWorker* notifier;
    Preferences preferences;

    // Cross-task state: currentState is atomic; lastError/versionInfo/lastCheckTime
    // require stateMux. versionInfo.currentVersion is immutable after the ctor.
    std::atomic<OTAState> currentState;
    OTAConfig     config;
    VersionInfo   versionInfo;
    unsigned long lastCheckTime;
    String        lastError;

    // FreeRTOS primitives for the background check task
    SemaphoreHandle_t stateMux;      // Mutex protecting lastError / versionInfo / lastCheckTime
    TaskHandle_t      checkTaskHandle;
    volatile bool     checkRequested; // Set by loop task, consumed by check task
    volatile bool     installRequested; // Set by startUpdate(), consumed by check task
    char              installPassword[64]; // Snapshot of the install password for the check task

    // Helper methods
    void loadConfig();
    void saveConfig();
    void sendNotification(const char* message);
    void setError(const String& msg);   // Thread-safe write to lastError (takes stateMux)
    bool checkForUpdates();
    bool compareVersions(const String& v1, const String& v2);
    bool downloadAndInstall(const String& url, size_t expectedSize, const String& expectedSha256);
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

    // Optional flood-watch: return true from cb to abort an in-flight download
    // (C2). Invoked on the Core 0 check task; must be non-blocking.
    typedef bool (*FloodCheckCallback)(void* ctx);
    FloodCheckCallback floodCheckCb = nullptr;
    void*              floodCheckCtx = nullptr;

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
    // Validates the request, then hands the install off to the Core 0 check task
    // (10KB stack) because the mbedTLS handshake overflows the loop task's 8KB.
    // Returns true if the install was queued — the device reboots on success.
    bool startUpdate(const char* password = nullptr);
    // Runs the actual download/flash. Must only be called from the check task.
    bool executeUpdate(const char* password);

    // Configuration
    void setGitHubRepo(const char* owner, const char* repo);
    void setGitHubToken(const char* token);
    void setUpdatePassword(const char* password);
    void setAutoCheck(bool enabled, unsigned long intervalMs = DEFAULT_CHECK_INTERVAL_MS);
    void setAutoInstall(bool enabled);
    void setNotificationsEnabled(bool enabled);

    // C2: abort download if floodCheckCb returns true. Runs on Core 0 check task.
    void setFloodWatch(FloodCheckCallback cb, void* ctx) { floodCheckCb = cb; floodCheckCtx = ctx; }

    // Stack high-water mark for the background check task (for H5 diagnostics).
    // Returns 0 if the task isn't running.
    uint32_t getCheckTaskStackHighWaterMark() const;

    // Getters (currentState is atomic; String fields that the check task may
    // mutate are copied under stateMux — see getAvailableVersion/getLastError).
    OTAState getState() const { return currentState.load(); }
    String getCurrentVersion() const { return versionInfo.currentVersion; } // immutable after ctor
    String getAvailableVersion() const;   // locks stateMux (defined in .cpp)
    String getLastError() const;          // locks stateMux (defined in .cpp)
    bool isUpdateAvailable() const { return currentState.load() == OTAState::UPDATE_AVAILABLE; }
    bool isAutoCheckEnabled() const { return config.autoCheckEnabled; }
    bool isAutoInstallEnabled() const { return config.autoInstallEnabled; }
    bool areNotificationsEnabled() const { return config.notificationsEnabled; }
    String getGitHubRepo() const { return config.githubOwner + "/" + config.githubRepo; }
    bool hasGitHubToken() const { return !config.githubToken.isEmpty(); }
    bool hasUpdatePassword() const { return !config.updatePassword.isEmpty(); }
    unsigned long getCheckIntervalMs() const { return config.checkIntervalMs; }
    // Seconds since last version check (NVS wall-clock epoch). 0 if never / no RTC.
    unsigned long getTimeSinceLastCheckS();
};
