#pragma once

// RTC + SNTP timekeeping. Under UNIT_TESTING this header is inert — native
// builds use test/mocks/MockTimeManagement.h (esp_sntp.h is ESP-IDF only).
#ifndef UNIT_TESTING

#include <cstdint>
#include <sys/time.h>
#include <time.h>
#include <esp_sntp.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_log.h>


static constexpr uint32_t SNTP_MAX_WAIT = 10000;
static constexpr time_t SYNC_EXPIRY = 86400; // One day in seconds
// H4: throttle application-level SNTP re-init attempts so sync() (called every
// loop) doesn't restart the client every iteration while waiting for a callback.
static constexpr time_t SNTP_REINIT_INTERVAL_S = 60;
static constexpr int TIME_STR_BUFFER = 64;

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
        
        // Handle orchestrating syncing RTC with SNTP server
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
        time_t lastSyncInitTime;   // H4: unix time of last SNTP (re)init attempt

        bool isMocked;
        char timeStringBuffer[TIME_STR_BUFFER]; // Buffer for formatted time strings
        
        // Static callback for SNTP time sync events (required for C-style callback)
        static void onSNTPSync(struct timeval *tv);

        // Kick/restart SNTP. Returns true if sync was initiated.
        bool initSNTPSync(const char* server = "pool.ntp.org", uint32_t maxWaitMs = SNTP_MAX_WAIT);

};

#endif // UNIT_TESTING

