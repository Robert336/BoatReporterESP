#pragma once

// Discord webhook NotificationChannel. NVS key discord.url — see docs/configuration.md.

#include "NotificationChannel.h"

#ifndef UNIT_TESTING

#include "TextEscape.h"
#include "HttpPoster.h"
#include "NvsChannelBase.h"
#include <Arduino.h>
#include <Preferences.h>

class DiscordChannel : public NotificationChannel, protected NvsChannelBase {
public:
    DiscordChannel();

    // --- NotificationChannel interface ---
    bool        send(const char* message) override;
    bool        isConfigured()            const override;
    const char* name()                    const override { return "Discord"; }
    uint8_t     channelFlag()             const override;
    void        loadCache()                     override;

    // --- Config helpers (called from ConfigServer) ---
    void updateWebhookUrl(const char* url);
    int  getWebhookUrl(char* outBuf, size_t bufSize) const;
    bool hasWebhookUrl()                             const;

private:
    char webhookCache[256]; // In-RAM cache
};

#endif // UNIT_TESTING
