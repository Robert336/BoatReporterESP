#include "TimeManagement.h"
#include <Arduino.h>

static const char* TAG = "TimeManagement";

const time_t MOCK_EPOCH_TIME = 1762027303; // for testing (UTC: 2025-11-01)

// Singleton instance getter
TimeManagement& TimeManagement::getInstance() {
    static TimeManagement instance;  // Created once on first call, lives for program lifetime
    return instance;
}


TimeManagement::TimeManagement(bool mock) 
    : isMocked(mock), syncStatus(SNTP_NOT_STARTED), lastSyncTime(0) {
    
    if (!isMocked) {
        // Get current SNTP sync status
        sntp_sync_status_t status = sntp_get_sync_status();
        if (status == SNTP_SYNC_STATUS_COMPLETED) {
            syncStatus = SNTP_SYNCED;
        } else if (status == SNTP_SYNC_STATUS_IN_PROGRESS) {
            syncStatus = SNTP_SYNCING;
        }
    } else {
        syncStatus = SNTP_SYNCED; // Mock assumes synced time
    }
    
    ESP_LOGI(TAG, "TimeManagement initialized (mock=%d, syncStatus=%d)", isMocked, syncStatus);
}

TimeManagement::~TimeManagement() {
    // Note: We keep instance pointer set even on destruction because with Meyer's singleton,
    // the static instance lives for the entire program lifetime and is never destroyed
}

Timestamp TimeManagement::getCurrentTimestamp() {
    Timestamp ts;

    ts.timeSinceBoot = (uint32_t)(esp_timer_get_time() / 1000);  // Convert microseconds to milliseconds
    ts.isNTPSynced = (syncStatus == SNTP_SYNCED) && (time(NULL) - lastSyncTime < SYNC_EXPIRY); // Check sync and interval if sync is expired
    
    if (isMocked) {
        // For testing: use mock time + elapsed seconds
        ts.unixTime = MOCK_EPOCH_TIME + (uint32_t)(esp_timer_get_time() / 1000 / 1000);
        return ts;
    }

    // This uses the high-resolution timer (80 MHz APB clock)
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    ts.unixTime = tv.tv_sec;

    return ts;
}

void TimeManagement::sync() {
    if (time(NULL) - lastSyncTime < SYNC_EXPIRY) return; // Guard against already synced RTC
    if (syncStatus == SNTP_SYNCING) return; // Gaurd if currently syncing

    initSNTPSync(); // Sync RTC
}

bool TimeManagement::initSNTPSync(const char* server, uint32_t maxWaitMs) {
    if (isMocked) {
        ESP_LOGI(TAG, "Skipping SNTP init in mock mode");
        return true;
    }
    
    if (syncStatus != SNTP_NOT_STARTED) {
        ESP_LOGW(TAG, "SNTP already initialized (status=%d)", syncStatus);
        return true;
    }
    
    try {
        // Configure SNTP with the older API
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, server);
        
        // Set sync notification callback
        sntp_set_time_sync_notification_cb(onSNTPSync);
        
        // Initialize SNTP service
        sntp_init();
        
        syncStatus = SNTP_SYNCING;
        ESP_LOGI(TAG, "SNTP initialized with server: %s", server);
        
        // Wait for initial synchronization
        if (maxWaitMs > 0) {
            uint32_t startTime = (uint32_t)(esp_timer_get_time() / 1000);
            while (syncStatus == SNTP_SYNCING && ((uint32_t)(esp_timer_get_time() / 1000) - startTime) < maxWaitMs) {
                delay(100);
            }
            
            if (syncStatus == SNTP_SYNCED) {
                ESP_LOGI(TAG, "SNTP sync completed successfully");
                return true;
            } else {
                ESP_LOGW(TAG, "SNTP sync timeout after %lu ms", maxWaitMs);
                return false;
            }
        }
        
        return true;
    } catch (...) {
        ESP_LOGE(TAG, "Error initializing SNTP");
        syncStatus = SNTP_SYNC_FAILED;
        return false;
    }
}

void TimeManagement::stopSNTPSync() {
    if (!isMocked && syncStatus != SNTP_NOT_STARTED) {
        sntp_stop();
        syncStatus = SNTP_NOT_STARTED;
        ESP_LOGI(TAG, "SNTP synchronization stopped");
    }
}

SNTPSyncStatus TimeManagement::getSNTPStatus() {
    if (!isMocked) {
        // Check actual SNTP status from lwIP
        sntp_sync_status_t lwip_status = sntp_get_sync_status();
        if (lwip_status == SNTP_SYNC_STATUS_COMPLETED) {
            syncStatus = SNTP_SYNCED;
        } else if (lwip_status == SNTP_SYNC_STATUS_IN_PROGRESS) {
            syncStatus = SNTP_SYNCING;
        }
    }
    return syncStatus;
}

uint32_t TimeManagement::getTimeSinceNTPSync() {
    if (syncStatus != SNTP_SYNCED || lastSyncTime == 0) {
        return 0;
    }
    
    time_t currentTime = time(NULL);
    uint32_t elapsed = (uint32_t)(currentTime - lastSyncTime);
    
    return elapsed;
}

void TimeManagement::setSystemTime(time_t unixTimestamp) {
    struct timeval tv = {
        .tv_sec = unixTimestamp,
        .tv_usec = 0
    };
    
    settimeofday(&tv, NULL);
    lastSyncTime = unixTimestamp;
    
    if (syncStatus == SNTP_NOT_STARTED) {
        syncStatus = SNTP_SYNCED;
    }
    
    ESP_LOGI(TAG, "System time set to: %ld", unixTimestamp);
}

const char* TimeManagement::getTimeString(const char* format) {
    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    strftime(timeStringBuffer, sizeof(timeStringBuffer), format, &timeinfo);
    
    return timeStringBuffer;
}

// Static callback for SNTP sync events
void TimeManagement::onSNTPSync(struct timeval *tv) {
    TimeManagement& instance = TimeManagement::getInstance();
    instance.syncStatus = SNTP_SYNCED;
    instance.lastSyncTime = tv->tv_sec;
    ESP_LOGI(TAG, "SNTP sync completed via callback");
}