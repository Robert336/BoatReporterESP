#include "SendDiscord.h"
#include "Logger.h"

// Namespace for NVS storage
static constexpr const char* DISCORD_PREFS_NAMESPACE = "discord";

SendDiscord::SendDiscord() {
    // Don't call preferences.begin() here - NVS may not be ready for global objects
    webhookUrlCache[0] = '\0';
}


SendDiscord::~SendDiscord() {
    // Nothing to clean up since we open/close preferences on each operation
}


void SendDiscord::loadCache() {
    webhookUrlCache[0] = '\0';
    cacheLoaded = true; // Mark loaded even on failure so we don't retry on every send

    if (!preferences.begin(DISCORD_PREFS_NAMESPACE, true)) {
        return;
    }
    String url = preferences.getString("webhook-url", "");
    preferences.end();

    if (url.length() > 0 && url.length() < sizeof(webhookUrlCache)) {
        strncpy(webhookUrlCache, url.c_str(), sizeof(webhookUrlCache) - 1);
        webhookUrlCache[sizeof(webhookUrlCache) - 1] = '\0';
    }
}

bool SendDiscord::send(const char* message) {
    // Validate inputs
    if (!message) {
        return false;
    }

    // Check for active wifi connection first
    if (!WiFi.isConnected()) {
        LOG_NETWORK("[Discord] WiFi not connected, cannot send");
        return false;
    }

    // Use in-RAM cache; load from NVS on first call only
    if (!cacheLoaded) {
        loadCache();
    }

    if (webhookUrlCache[0] == '\0') {
        // No webhook URL stored
        return false;
    }

    HTTPClient http;
    
    // Escape the message for JSON
    size_t maxEscapedSize = strlen(message) * 2 + 1; // Worst case: every char needs escaping
    char* escapedMessage = (char*)malloc(maxEscapedSize);
    if (!escapedMessage) {
        return false;
    }
    
    escapeJsonString(message, escapedMessage, maxEscapedSize);
    
    // Build JSON payload for Discord webhook
    // Discord webhook expects: {"content": "message text"}
    size_t jsonSize = strlen(escapedMessage) + 50; // Extra space for JSON structure
    char* jsonPayload = (char*)malloc(jsonSize);
    if (!jsonPayload) {
        free(escapedMessage);
        return false;
    }
    
    snprintf(jsonPayload, jsonSize, "{\"content\":\"%s\"}", escapedMessage);
    
    LOG_NETWORK("[Discord] Initiating HTTP POST to webhook");
    uint32_t startTime = millis();
    http.begin(webhookUrlCache);
    http.setTimeout(10000); // 10 second timeout
    http.addHeader("Content-Type", "application/json");

    int httpResponseCode = http.POST(jsonPayload);
    uint32_t elapsed = millis() - startTime;
    LOG_NETWORK("[Discord] HTTP response code: %d (elapsed: %u ms)", httpResponseCode, elapsed);

    if (httpResponseCode < 0) {
        LOG_NETWORK("[Discord] HTTP error: %s", http.errorToString(httpResponseCode).c_str());
    } else if (httpResponseCode < 200 || httpResponseCode >= 300) {
        // Log response body snippet for error status codes
        String responseBody = http.getString();
        if (responseBody.length() > 0) {
            // Log first 200 chars of response
            String snippet = responseBody.substring(0, 200);
            LOG_NETWORK("[Discord] Error response body: %s", snippet.c_str());
        }
    }

    // Free allocated memory
    free(escapedMessage);
    free(jsonPayload);

    bool success = (httpResponseCode >= 200 && httpResponseCode < 300);
    LOG_NETWORK("[Discord] Send %s (HTTP %d, %u ms)", success ? "SUCCESS" : "FAILED", httpResponseCode, elapsed);

    http.end();
    return success;
}


void SendDiscord::updateWebhookUrl(const char* newWebhookUrl) {
    if (!newWebhookUrl) {
        return; // Invalid input, do nothing
    }
    
    // Open preferences for writing
    if (!preferences.begin(DISCORD_PREFS_NAMESPACE, false)) {
        LOG_CRITICAL("[Discord] Failed to open preferences for writing");
        return;
    }
    
    size_t bytesWritten = preferences.putString("webhook-url", newWebhookUrl);
    preferences.end();

    if (bytesWritten == 0) {
        LOG_CRITICAL("[Discord] Failed to store webhook URL in preferences!");
    } else {
        LOG_INFO("[Discord] Webhook URL saved successfully (%d bytes)", bytesWritten);
        // Refresh in-RAM cache so the next send() picks up the new URL without
        // needing a reboot.
        loadCache();
    }
}


int SendDiscord::getWebhookUrl(char* outBuf, size_t bufferSize) {
    if (!outBuf || bufferSize == 0) {
        return -1;
    }
    
    // Open preferences for reading
    if (!preferences.begin(DISCORD_PREFS_NAMESPACE, true)) {
        LOG_CRITICAL("[Discord] Failed to open preferences for reading");
        return -1;
    }
    
    // Store String in a variable to avoid temporary object destruction
    String webhookUrl = preferences.getString("webhook-url", "");
    preferences.end();
    
    if (webhookUrl.length() == 0) {
        return -1; // No webhook URL stored
    }
    
    size_t requiredSize = webhookUrl.length() + 1;
    if (requiredSize > bufferSize) {
        return -1;
    }

    strcpy(outBuf, webhookUrl.c_str());
    return 0;
}


void SendDiscord::escapeJsonString(const char* input, char* output, size_t outputSize) {
    if (!input || !output || outputSize == 0) {
        if (output && outputSize > 0) {
            output[0] = '\0';
        }
        return;
    }

    size_t outIdx = 0;
    
    for (size_t i = 0; input[i] != '\0' && outIdx < outputSize - 2; i++) {
        char c = input[i];
        
        switch (c) {
            case '"':
                if (outIdx < outputSize - 2) {
                    output[outIdx++] = '\\';
                    output[outIdx++] = '"';
                }
                break;
            case '\\':
                if (outIdx < outputSize - 2) {
                    output[outIdx++] = '\\';
                    output[outIdx++] = '\\';
                }
                break;
            case '\n':
                if (outIdx < outputSize - 2) {
                    output[outIdx++] = '\\';
                    output[outIdx++] = 'n';
                }
                break;
            case '\r':
                if (outIdx < outputSize - 2) {
                    output[outIdx++] = '\\';
                    output[outIdx++] = 'r';
                }
                break;
            case '\t':
                if (outIdx < outputSize - 2) {
                    output[outIdx++] = '\\';
                    output[outIdx++] = 't';
                }
                break;
            default:
                if (outIdx < outputSize - 1) {
                    output[outIdx++] = c;
                }
                break;
        }
    }
    output[outIdx] = '\0';
}


bool SendDiscord::hasWebhookUrl() {
    if (!cacheLoaded) {
        loadCache();
    }
    return webhookUrlCache[0] != '\0';
}
