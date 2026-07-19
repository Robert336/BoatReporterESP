#ifndef UNIT_TESTING

#include "CustomChannel.h"
#include "NotifyChannelFlags.h"
#include "TextEscape.h"
#include "HttpPoster.h"
#include "Logger.h"
#include <string.h>

static constexpr const char* PLACEHOLDER    = "{{message}}";
static constexpr size_t      PLACEHOLDER_LEN = 11; // strlen("{{message}}")

CustomChannel::CustomChannel() {
    endpointCache[0] = '\0';
    ctypeCache[0]    = '\0';
    authCache[0]     = '\0';
    userCache[0]     = '\0';
    secretCache[0]   = '\0';
    tmplCache[0]     = '\0';
}

void CustomChannel::loadCache() {
    endpointCache[0] = '\0';
    ctypeCache[0]    = '\0';
    authCache[0]     = '\0';
    userCache[0]     = '\0';
    secretCache[0]   = '\0';
    tmplCache[0]     = '\0';
    if (!beginLoad()) return;
    loadStr("custom.endpoint", endpointCache, sizeof(endpointCache));
    loadStr("custom.ctype",    ctypeCache,    sizeof(ctypeCache));
    loadStr("custom.auth",     authCache,     sizeof(authCache));
    loadStr("custom.user",     userCache,     sizeof(userCache));
    loadStr("custom.secret",   secretCache,   sizeof(secretCache));
    loadStr("custom.tmpl",     tmplCache,     sizeof(tmplCache));
    finishLoad();
}

bool CustomChannel::isConfigured() const {
    return endpointCache[0] != '\0' && tmplCache[0] != '\0';
}

uint8_t CustomChannel::channelFlag() const {
    return CHAN_CUSTOM;
}

bool CustomChannel::substituteTemplate(const char* escapedMsg,
                                       char* outBuf, size_t outSize) const {
    // Find the placeholder in the template
    const char* ph = strstr(tmplCache, PLACEHOLDER);
    if (!ph) {
        // No placeholder — use template verbatim.
        // This is not an error; the user may have a static body.
        LOG_NETWORK("[Custom] Body template has no {{message}} placeholder — using verbatim");
        size_t tmplLen = strlen(tmplCache);
        if (tmplLen >= outSize) {
            LOG_CRITICAL("[Custom] Template too large for output buffer (%u >= %u)", tmplLen, outSize);
            return false;
        }
        memcpy(outBuf, tmplCache, tmplLen + 1);
        return true;
    }

    size_t preLen  = (size_t)(ph - tmplCache);
    size_t msgLen  = strlen(escapedMsg);
    size_t postLen = strlen(ph + PLACEHOLDER_LEN);
    size_t total   = preLen + msgLen + postLen + 1;

    if (total > outSize) {
        LOG_CRITICAL("[Custom] Substituted body too large (%u > %u) — truncating not supported",
                     total, outSize);
        return false;
    }

    memcpy(outBuf,                  tmplCache,              preLen);
    memcpy(outBuf + preLen,         escapedMsg,             msgLen);
    memcpy(outBuf + preLen + msgLen, ph + PLACEHOLDER_LEN, postLen + 1);
    return true;
}

bool CustomChannel::send(const char* message) {
    if (!message) return false;
    if (!cacheLoaded) loadCache();
    if (!isConfigured()) return false;

    // Pick escaping strategy based on content-type
    // message body is bounded to 160 chars (NotifMsg.body), so worst-case
    // escaped is 3x that = 480 chars. Use a stack buffer to avoid heap churn.
    char escaped[512];
    const char* ct = ctypeCache;

    if (strstr(ct, "json") || strstr(ct, "JSON")) {
        TextEscape::jsonEscape(message, escaped, sizeof(escaped));
    } else if (strstr(ct, "form") || strstr(ct, "urlencoded")) {
        TextEscape::urlEncode(message, escaped, sizeof(escaped));
    } else {
        // Raw — copy with NUL-termination, no escaping
        strncpy(escaped, message, sizeof(escaped) - 1);
        escaped[sizeof(escaped) - 1] = '\0';
    }

    // Build body by substituting escaped message into the template.
    // Template ≤ 512 chars, escaped ≤ 480 chars → body ≤ ~1000 chars; heap is safer.
    size_t bodyBufSize = sizeof(tmplCache) + sizeof(escaped);
    char* body = (char*)malloc(bodyBufSize);
    if (!body) {
        LOG_CRITICAL("[Custom] malloc failed building POST body");
        return false;
    }

    if (!substituteTemplate(escaped, body, bodyBufSize)) {
        free(body);
        return false;
    }

    // Determine auth mode
    HttpAuthMode mode = HttpAuthMode::NONE;
    const char*  user   = nullptr;
    const char*  secret = nullptr;

    if (strcmp(authCache, "basic") == 0) {
        mode   = HttpAuthMode::BASIC;
        user   = userCache[0]   ? userCache   : nullptr;
        secret = secretCache[0] ? secretCache : nullptr;
    } else if (strcmp(authCache, "bearer") == 0) {
        mode = HttpAuthMode::BEARER;
        user = userCache[0] ? userCache : nullptr; // token stored in userCache
    }

    bool ok = HttpPoster::post("[Custom]", endpointCache, ctypeCache, body,
                               mode, user, secret);
    free(body);
    return ok;
}

void CustomChannel::updateConfig(const char* endpoint,
                                 const char* contentType,
                                 const char* authType,
                                 const char* authUser,
                                 const char* authSecret,
                                 const char* bodyTemplate) {
    if (!openForWrite("Custom")) return;

    auto putIfPresent = [this](const char* key, const char* val, size_t maxLen) {
        if (!val) return;
        // Truncate silently if the value exceeds our in-RAM max
        // (NVS itself supports up to ~4000 bytes per key, but we keep RAM bounded)
        char tmp[512];
        strncpy(tmp, val, maxLen - 1);
        tmp[maxLen - 1] = '\0';
        putStr(key, tmp);
    };

    putIfPresent("custom.endpoint", endpoint,    CUSTOM_ENDPOINT_MAX);
    putIfPresent("custom.ctype",    contentType, CUSTOM_CTYPE_MAX);
    putIfPresent("custom.auth",     authType,    CUSTOM_AUTH_MAX);
    putIfPresent("custom.user",     authUser,    CUSTOM_USER_MAX);
    putIfPresent("custom.secret",   authSecret,  CUSTOM_SECRET_MAX);
    putIfPresent("custom.tmpl",     bodyTemplate, CUSTOM_TMPL_MAX);

    endWrite();
    LOG_INFO("[Custom] Channel config updated");
    loadCache();
}

void CustomChannel::getConfig(char* endpointOut, size_t endpointSize,
                              char* ctypeOut,    size_t ctypeSize,
                              char* authOut,     size_t authSize,
                              char* tmplOut,     size_t tmplSize) const {
    auto copyOut = [](const char* src, char* dst, size_t dstSize) {
        if (!dst || dstSize == 0) return;
        strncpy(dst, src, dstSize - 1);
        dst[dstSize - 1] = '\0';
    };
    copyOut(endpointCache, endpointOut, endpointSize);
    copyOut(ctypeCache,    ctypeOut,    ctypeSize);
    copyOut(authCache,     authOut,     authSize);
    copyOut(tmplCache,     tmplOut,     tmplSize);
    // authUser / authSecret intentionally not exposed (write-only from the UI)
}

#endif // UNIT_TESTING
