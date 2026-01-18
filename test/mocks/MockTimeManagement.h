#pragma once

#ifdef UNIT_TESTING

#include <stdint.h>
#include <time.h>

// Timestamp structure (matches the real one)
struct Timestamp {
    bool isNTPSynced;
    time_t unixTime;
    uint32_t timeSinceBoot;
};

// Mock TimeManagement singleton
class TimeManagement {
public:
    static TimeManagement& getInstance() {
        static TimeManagement instance;
        return instance;
    }
    
    void sync() {
        // No-op in testing
    }
    
    Timestamp getCurrentTimestamp() {
        Timestamp ts;
        ts.isNTPSynced = mockNTPSynced;
        ts.unixTime = mockUnixTime;
        ts.timeSinceBoot = mockTimeSinceBoot;
        return ts;
    }
    
    bool isNTPSynced() const {
        return mockNTPSynced;
    }
    
    time_t getUnixTime() const {
        return mockUnixTime;
    }
    
    // Test helper methods
    void setMockNTPSynced(bool synced) {
        mockNTPSynced = synced;
    }
    
    void setMockUnixTime(time_t time) {
        mockUnixTime = time;
    }
    
    void setMockTimeSinceBoot(uint32_t ms) {
        mockTimeSinceBoot = ms;
    }
    
private:
    TimeManagement() : mockNTPSynced(false), mockUnixTime(0), mockTimeSinceBoot(0) {}
    
    bool mockNTPSynced;
    time_t mockUnixTime;
    uint32_t mockTimeSinceBoot;
};

#endif // UNIT_TESTING
