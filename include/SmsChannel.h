#pragma once

/*
    SmsChannel.h

    Twilio SMS implementation of NotificationChannel.
    All credentials (account SID, auth token, messaging service SID) and the
    destination phone number are read from NVS namespace "notify" at runtime —
    nothing provider-specific lives in secrets.h anymore.

    NVS keys (namespace "notify"):
      sms.phone   — destination phone number
      sms.sid     — Twilio account SID
      sms.token   — Twilio auth token
      sms.svcsid  — Twilio messaging service SID

    isConfigured() = phone + sid + token are all non-empty.
*/

#include "NotificationChannel.h"

#ifndef UNIT_TESTING

#include "TextEscape.h"
#include "HttpPoster.h"
#include <Arduino.h>
#include <Preferences.h>

class SmsChannel : public NotificationChannel {
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
    Preferences prefs;

    // In-RAM cache — avoids NVS I/O on every send()
    char phoneCache[32];
    char sidCache[48];
    char tokenCache[48];
    char svcSidCache[48];
    bool cacheLoaded = false;
};

#endif // UNIT_TESTING
