#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "SendSMS.h"
#include "SendDiscord.h"

constexpr const char OTA_PREFERENCES_NAMESPACE[] = "ota_config";
constexpr unsigned long DEFAULT_CHECK_INTERVAL_MS = 86400000; // 24 hours
constexpr int OTA_BUFFER_SIZE = 1024; // Download buffer size

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
};

/**
 * OTAManager - Manages Over-The-Air firmware updates
 * 
 * Features:
 * - Check GitHub Releases for new firmware versions
 * - Download and install firmware updates via HTTPS
 * - Automatic version checking on schedule
 * - Manual update triggering via web interface
 * - Notification integration (SMS/Discord)
 * - Rollback detection and notification
 * - NVS configuration storage
 */
class OTAManager {
private:
    // Services
    SendSMS* smsService;
    SendDiscord* discordService;
    Preferences preferences;
    
    // State
    OTAState currentState;
    OTAConfig config;
    VersionInfo versionInfo;
    unsigned long lastCheckTime;
    String lastError;
    bool firstBootAfterUpdate;
    bool rollbackOccurred;
    
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
    
public:
    OTAManager(SendSMS* sms = nullptr, SendDiscord* discord = nullptr);
    ~OTAManager();
    
    // Core operations
    void begin();
    void loop();
    
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
    
    // Getters
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
