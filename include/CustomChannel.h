#pragma once

/*
    CustomChannel.h

    User-configurable generic HTTP POST notification channel.
    All config is stored in NVS namespace "notify" (keys below) and cached in RAM.

    NVS keys (namespace "notify"):
      custom.endpoint  — full URL to POST to
      custom.ctype     — Content-Type header (e.g. "application/json")
      custom.auth      — auth type: "none", "basic", or "bearer"
      custom.user      — Basic: username; Bearer: token; none: unused
      custom.secret    — Basic: password; bearer/none: unused
      custom.tmpl      — body template containing {{message}} placeholder

    Body substitution rules:
      - If content-type contains "json"           → jsonEscape the message before substitution
      - If content-type contains "form"           → urlEncode the message before substitution
      - Otherwise                                 → substitute raw (no escaping)
      - If {{message}} is absent from the template → template is used verbatim (no message injected)
      - Message body is bounded to 160 chars (NotifMsg.body size)

    isConfigured() = endpoint + template are both non-empty.
*/

#include "NotificationChannel.h"

// Max field sizes — chosen to fit on-stack buffers without blowing the notifier task stack.
// The notifier task has 6 KB stack; keep buffers proportionate.
static constexpr size_t CUSTOM_ENDPOINT_MAX = 256;
static constexpr size_t CUSTOM_CTYPE_MAX    = 64;
static constexpr size_t CUSTOM_AUTH_MAX     = 8;    // "none" / "basic" / "bearer"
static constexpr size_t CUSTOM_USER_MAX     = 128;  // also holds bearer token
static constexpr size_t CUSTOM_SECRET_MAX   = 128;
static constexpr size_t CUSTOM_TMPL_MAX     = 512;

#ifndef UNIT_TESTING

#include "TextEscape.h"
#include "HttpPoster.h"
#include "NvsChannelBase.h"
#include <Arduino.h>
#include <Preferences.h>

class CustomChannel : public NotificationChannel, protected NvsChannelBase {
public:
    CustomChannel();

    // --- NotificationChannel interface ---
    bool        send(const char* message) override;
    bool        isConfigured()            const override;
    const char* name()                    const override { return "Custom"; }
    uint8_t     channelFlag()             const override;
    void        loadCache()                     override;

    // --- Config helpers (called from ConfigServer) ---
    void updateConfig(const char* endpoint,
                      const char* contentType,
                      const char* authType,
                      const char* authUser,
                      const char* authSecret,
                      const char* bodyTemplate);

    // Read back the stored config (auth credentials are NOT returned — write-only)
    void getConfig(char* endpointOut, size_t endpointSize,
                   char* ctypeOut,    size_t ctypeSize,
                   char* authOut,     size_t authSize,
                   char* tmplOut,     size_t tmplSize) const;

    bool hasEndpoint()  const { return endpointCache[0] != '\0'; }
    bool hasTemplate()  const { return tmplCache[0]     != '\0'; }

private:
    // In-RAM cache
    char endpointCache[CUSTOM_ENDPOINT_MAX];
    char ctypeCache[CUSTOM_CTYPE_MAX];
    char authCache[CUSTOM_AUTH_MAX];
    char userCache[CUSTOM_USER_MAX];
    char secretCache[CUSTOM_SECRET_MAX];
    char tmplCache[CUSTOM_TMPL_MAX];

    // Substitute {{message}} in tmplCache, writing to outBuf (size outSize).
    // escapedMsg must already be escaped appropriately for the content type.
    bool substituteTemplate(const char* escapedMsg,
                            char* outBuf, size_t outSize) const;
};

#endif // UNIT_TESTING
