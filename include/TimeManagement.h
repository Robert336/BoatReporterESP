#pragma once

// This module will handle supplying the most accurate timestamps available to the system
// We leverage the ESP32's RTC (persistent across resets), high-resolution timer (80MHz),
// and SNTP synchronization for accurate time keeping
#include <cstdint>
#include <sys/time.h>
#include <time.h>
#include <esp_sntp.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_log.h>

struct Timestamp {
    bool isNTPSynced;          // Whether time has been synced via NTP
    time_t unixTime;           // Unix timestamp (seconds since epoch)
    uint32_t timeSinceBoot;    // Milliseconds since boot (from high-res timer)
};

// SNTP sync status enum
enum SNTPSyncStatus {
    SNTP_NOT_STARTED,
    SNTP_SYNCING,
    SNTP_SYNCED,
    SNTP_SYNC_FAILED
};

class TimeManagement {
    public:
        // Get the singleton instance
        static TimeManagement& getInstance();
        
        // Get current timestamp with full details
        Timestamp getCurrentTimestamp();
        

        // Handle orcestraiting syncing RTC with SNTP server
        void sync();

        // Stop SNTP synchronization
        void stopSNTPSync();
        
        // Get current SNTP sync status
        SNTPSyncStatus getSNTPStatus();
        
        // Get time since last SNTP synchronization
        uint32_t getTimeSinceNTPSync();
        
        // Manually set system time (Unix timestamp in seconds)
        void setSystemTime(time_t unixTimestamp);
        
        // Get formatted time string
        // format: strftime format string (e.g., "%Y-%m-%d %H:%M:%S")
        const char* getTimeString(const char* format = "%Y-%m-%d %H:%M:%S");
        
        ~TimeManagement();
        
    private:
        // Prevent copying and moving
        TimeManagement(bool mock = false);
        TimeManagement(const TimeManagement&) = delete;
        TimeManagement& operator=(const TimeManagement&) = delete;
        TimeManagement(TimeManagement&&) = delete;
        TimeManagement& operator=(TimeManagement&&) = delete;
        
        SNTPSyncStatus syncStatus;
        time_t lastSyncTime;       // Unix time of last SNTP sync
        const time_t SYNC_EXPIRY = 86400; // One day in seconds
        bool isMocked;
        char timeStringBuffer[64]; // Buffer for formatted time strings
        
        // Static callback for SNTP time sync events (required for C-style callback)
        static void onSNTPSync(struct timeval *tv);

        // Initialize SNTP synchronization with NTP server
        // server: NTP server name (e.g., "pool.ntp.org")
        // maxWaitMs: Maximum time to wait for initial sync
        // Returns: true if sync initiated successfully
        bool initSNTPSync(const char* server = "pool.ntp.org", uint32_t maxWaitMs = 10000);
        
};

