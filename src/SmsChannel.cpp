#ifndef UNIT_TESTING

#include "SmsChannel.h"
#include "NotifyChannelFlags.h"
#include "TextEscape.h"
#include "HttpPoster.h"
#include "Logger.h"

// Consolidated NVS namespace for all notification channel config (Step 3).
// Keys must be ≤15 chars (NVS constraint).
static constexpr const char* NOTIFY_NS = "notify";

SmsChannel::SmsChannel() {
    phoneCache[0]  = '\0';
    sidCache[0]    = '\0';
    tokenCache[0]  = '\0';
    svcSidCache[0] = '\0';
}

void SmsChannel::loadCache() {
    phoneCache[0]  = '\0';
    sidCache[0]    = '\0';
    tokenCache[0]  = '\0';
    svcSidCache[0] = '\0';
    cacheLoaded = true; // Set now so repeated failures don't keep retrying

    if (!prefs.begin(NOTIFY_NS, true)) return;

    auto copyStr = [](Preferences& p, const char* key, char* dst, size_t dstSize) {
        String v = p.getString(key, "");
        if (v.length() > 0 && v.length() < dstSize) {
            strncpy(dst, v.c_str(), dstSize - 1);
            dst[dstSize - 1] = '\0';
        }
    };

    copyStr(prefs, "sms.phone",  phoneCache,  sizeof(phoneCache));
    copyStr(prefs, "sms.sid",    sidCache,    sizeof(sidCache));
    copyStr(prefs, "sms.token",  tokenCache,  sizeof(tokenCache));
    copyStr(prefs, "sms.svcsid", svcSidCache, sizeof(svcSidCache));

    prefs.end();
}

bool SmsChannel::isConfigured() const {
    return phoneCache[0] != '\0' && sidCache[0] != '\0' && tokenCache[0] != '\0';
}

uint8_t SmsChannel::channelFlag() const {
    return CHAN_SMS;
}

bool SmsChannel::send(const char* message) {
    if (!message) return false;
    if (!cacheLoaded) loadCache();
    if (!isConfigured()) return false;

    // Build URL-encoded POST body for Twilio Messages API
    size_t msgLen      = strlen(message);
    size_t svcSidLen   = strlen(svcSidCache);
    size_t phoneLen    = strlen(phoneCache);
    char* encodedTo    = (char*)malloc(phoneLen  * 3 + 1);
    char* encodedSvc   = (char*)malloc(svcSidLen * 3 + 1);
    char* encodedBody  = (char*)malloc(msgLen    * 3 + 1);
    size_t postBufSize = (phoneLen + svcSidLen + msgLen) * 3 + 50;
    char* postData     = (char*)malloc(postBufSize);

    if (!encodedTo || !encodedSvc || !encodedBody || !postData) {
        free(encodedTo); free(encodedSvc); free(encodedBody); free(postData);
        LOG_CRITICAL("[SMS] malloc failed building POST body");
        return false;
    }

    TextEscape::urlEncode(phoneCache,  encodedTo,   phoneLen  * 3 + 1);
    TextEscape::urlEncode(svcSidCache, encodedSvc,  svcSidLen * 3 + 1);
    TextEscape::urlEncode(message,     encodedBody, msgLen    * 3 + 1);

    snprintf(postData, postBufSize,
             "To=%s&MessagingServiceSid=%s&Body=%s",
             encodedTo, encodedSvc, encodedBody);

    char endpoint[128];
    snprintf(endpoint, sizeof(endpoint),
             "https://api.twilio.com/2010-04-01/Accounts/%s/Messages.json",
             sidCache);

    bool ok = HttpPoster::post("[SMS]", endpoint,
                               "application/x-www-form-urlencoded", postData,
                               HttpAuthMode::BASIC, sidCache, tokenCache);

    free(encodedTo); free(encodedSvc); free(encodedBody); free(postData);
    return ok;
}

void SmsChannel::updatePhoneNumber(const char* phone) {
    if (!phone) return;

    if (!prefs.begin(NOTIFY_NS, false)) {
        LOG_CRITICAL("[SMS] Failed to open NVS for writing");
        return;
    }
    size_t n = prefs.putString("sms.phone", phone);
    prefs.end();

    if (n == 0) {
        LOG_CRITICAL("[SMS] Failed to store phone number");
    } else {
        LOG_INFO("[SMS] Phone number saved (%d bytes)", n);
        loadCache();
    }
}

void SmsChannel::updateTwilioCreds(const char* sid, const char* token, const char* svcSid) {
    if (!prefs.begin(NOTIFY_NS, false)) {
        LOG_CRITICAL("[SMS] Failed to open NVS for writing Twilio creds");
        return;
    }
    if (sid    && sid[0])    prefs.putString("sms.sid",    sid);
    if (token  && token[0])  prefs.putString("sms.token",  token);
    if (svcSid && svcSid[0]) prefs.putString("sms.svcsid", svcSid);
    prefs.end();

    LOG_INFO("[SMS] Twilio credentials updated");
    loadCache();
}

int SmsChannel::getPhoneNumber(char* outBuf, size_t bufSize) const {
    if (!outBuf || bufSize == 0) return -1;
    if (phoneCache[0] == '\0') return -1;
    size_t needed = strlen(phoneCache) + 1;
    if (needed > bufSize) return -1;
    memcpy(outBuf, phoneCache, needed);
    return 0;
}

bool SmsChannel::hasPhoneNumber() const {
    return phoneCache[0] != '\0';
}

#endif // UNIT_TESTING
