#pragma once

// Twilio SMS NotificationChannel. NVS keys: docs/configuration.md.

#include "NotificationChannel.h"

#ifndef UNIT_TESTING

#include "TextEscape.h"
#include "HttpPoster.h"
#include "NvsChannelBase.h"
#include <Arduino.h>
#include <Preferences.h>

class SmsChannel : public NotificationChannel, protected NvsChannelBase {
public:
    SmsChannel();

    // --- NotificationChannel interface ---
    bool        send(const char* message) override;
    bool        isConfigured()            const override;
    const char* name()                    const override { return "SMS"; }
    uint8_t     channelFlag()             const override;
    void        loadCache()                     override;

    // --- Config helpers (called from ConfigServer) ---
    void updatePhoneNumber(const char* phone);
    void updateTwilioCreds(const char* sid, const char* token, const char* svcSid);

    int  getPhoneNumber(char* outBuf, size_t bufSize) const;
    bool hasPhoneNumber()                             const;

private:
    // In-RAM cache — avoids NVS I/O on every send()
    char phoneCache[32];
    char sidCache[48];
    char tokenCache[48];
    char svcSidCache[48];
};

#endif // UNIT_TESTING
