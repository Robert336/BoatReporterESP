#ifndef UNIT_TESTING

#include "HttpPoster.h"
#include "Logger.h"
#include <WiFi.h>
#include <HTTPClient.h>

bool HttpPoster::post(const char* tag,
                      const char* url,
                      const char* contentType,
                      const char* body,
                      HttpAuthMode authMode,
                      const char*  authUser,
                      const char*  authSecret) {
    if (!WiFi.isConnected()) {
        LOG_NETWORK("%s WiFi not connected, cannot send", tag);
        return false;
    }

    LOG_NETWORK("%s HTTP POST — url: %s", tag, url);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(10000);
    http.addHeader("Content-Type", contentType);

    // Apply auth — credentials are intentionally not logged
    switch (authMode) {
        case HttpAuthMode::BASIC:
            if (authUser && authSecret) {
                http.setAuthorization(authUser, authSecret);
            }
            break;
        case HttpAuthMode::BEARER:
            if (authUser) {
                // authUser holds the token for BEARER
                char hdr[320];
                snprintf(hdr, sizeof(hdr), "Bearer %s", authUser);
                http.addHeader("Authorization", hdr);
            }
            break;
        case HttpAuthMode::NONE:
        default:
            break;
    }

    uint32_t startTime = millis();
    int code = http.POST(body);
    uint32_t elapsed = millis() - startTime;

    LOG_NETWORK("%s HTTP response %d (%u ms)", tag, code, elapsed);

    if (code < 0) {
        LOG_NETWORK("%s HTTP error: %s", tag, http.errorToString(code).c_str());
    } else if (code < 200 || code >= 300) {
        String snippet = http.getString();
        if (snippet.length() > 0) {
            LOG_NETWORK("%s Error response (first 200 chars): %.200s", tag, snippet.c_str());
        }
    }

    bool success = (code >= 200 && code < 300);
    LOG_NETWORK("%s Send %s (HTTP %d, %u ms)", tag, success ? "SUCCESS" : "FAILED", code, elapsed);

    http.end();
    return success;
}

#endif // UNIT_TESTING
