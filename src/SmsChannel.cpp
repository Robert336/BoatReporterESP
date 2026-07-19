#ifndef UNIT_TESTING

#include "SmsChannel.h"
#include "NotifyChannelFlags.h"
#include "TextEscape.h"
#include "HttpPoster.h"
#include "Logger.h"

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
    if (!beginLoad()) return;
    loadStr("sms.phone",  phoneCache,  sizeof(phoneCache));
    loadStr("sms.sid",    sidCache,    sizeof(sidCache));
    loadStr("sms.token",  tokenCache,  sizeof(tokenCache));
    loadStr("sms.svcsid", svcSidCache, sizeof(svcSidCache));
    finishLoad();
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

    if (!openForWrite("SMS")) return;
    size_t n = putStr("sms.phone", phone);
    endWrite();

    if (n == 0) {
        LOG_CRITICAL("[SMS] Failed to store phone number");
    } else {
        LOG_INFO("[SMS] Phone number saved (%d bytes)", n);
        loadCache();
    }
}

void SmsChannel::updateTwilioCreds(const char* sid, const char* token, const char* svcSid) {
    if (!openForWrite("SMS")) return;
    if (sid    && sid[0])    putStr("sms.sid",    sid);
    if (token  && token[0])  putStr("sms.token",  token);
    if (svcSid && svcSid[0]) putStr("sms.svcsid", svcSid);
    endWrite();

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
