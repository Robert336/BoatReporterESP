#ifndef UNIT_TESTING

#include "DiscordChannel.h"
#include "NotifyChannelFlags.h"
#include "TextEscape.h"
#include "HttpPoster.h"
#include "Logger.h"

static constexpr const char* NOTIFY_NS = "notify";

DiscordChannel::DiscordChannel() {
    webhookCache[0] = '\0';
}

void DiscordChannel::loadCache() {
    webhookCache[0] = '\0';
    cacheLoaded = true;

    if (!prefs.begin(NOTIFY_NS, true)) return;

    String url = prefs.getString("discord.url", "");
    prefs.end();

    if (url.length() > 0 && url.length() < sizeof(webhookCache)) {
        strncpy(webhookCache, url.c_str(), sizeof(webhookCache) - 1);
        webhookCache[sizeof(webhookCache) - 1] = '\0';
    }
}

bool DiscordChannel::isConfigured() const {
    return webhookCache[0] != '\0';
}

uint8_t DiscordChannel::channelFlag() const {
    return CHAN_DISCORD;
}

bool DiscordChannel::send(const char* message) {
    if (!message) return false;
    if (!cacheLoaded) loadCache();
    if (!isConfigured()) return false;

    size_t maxEscaped = strlen(message) * 2 + 1;
    char* escaped = (char*)malloc(maxEscaped);
    if (!escaped) return false;

    TextEscape::jsonEscape(message, escaped, maxEscaped);

    size_t jsonSize = strlen(escaped) + 16;
    char* payload = (char*)malloc(jsonSize);
    if (!payload) { free(escaped); return false; }

    snprintf(payload, jsonSize, "{\"content\":\"%s\"}", escaped);

    bool ok = HttpPoster::post("[Discord]", webhookCache,
                               "application/json", payload);

    free(escaped);
    free(payload);
    return ok;
}

void DiscordChannel::updateWebhookUrl(const char* url) {
    if (!url) return;

    if (!prefs.begin(NOTIFY_NS, false)) {
        LOG_CRITICAL("[Discord] Failed to open NVS for writing");
        return;
    }
    size_t n = prefs.putString("discord.url", url);
    prefs.end();

    if (n == 0) {
        LOG_CRITICAL("[Discord] Failed to store webhook URL");
    } else {
        LOG_INFO("[Discord] Webhook URL saved (%d bytes)", n);
        loadCache();
    }
}

int DiscordChannel::getWebhookUrl(char* outBuf, size_t bufSize) const {
    if (!outBuf || bufSize == 0) return -1;
    if (webhookCache[0] == '\0') return -1;
    size_t needed = strlen(webhookCache) + 1;
    if (needed > bufSize) return -1;
    memcpy(outBuf, webhookCache, needed);
    return 0;
}

bool DiscordChannel::hasWebhookUrl() const {
    return webhookCache[0] != '\0';
}

#endif // UNIT_TESTING
