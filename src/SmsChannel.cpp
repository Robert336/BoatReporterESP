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

    // Build URL-encoded POST body for Twilio Messages API. All inputs are
    // bounded (message <= 160 chars from NotifMsg.body; phone/svcSid are the
    // fixed cache arrays below), so worst-case sizes are compile-time known
    // and stack buffers avoid per-send heap churn + alloc-failure paths.
    static constexpr size_t ENCODED_TO_MAX  = sizeof(phoneCache)  * 3;
    static constexpr size_t ENCODED_SVC_MAX = sizeof(svcSidCache) * 3;
    static constexpr size_t ENCODED_MSG_MAX = 160 * 3 + 1;
    char encodedTo[ENCODED_TO_MAX];
    char encodedSvc[ENCODED_SVC_MAX];
    char encodedBody[ENCODED_MSG_MAX];

    TextEscape::urlEncode(phoneCache,  encodedTo,   sizeof(encodedTo));
    TextEscape::urlEncode(svcSidCache, encodedSvc,  sizeof(encodedSvc));
    TextEscape::urlEncode(message,     encodedBody, sizeof(encodedBody));

    // "To=%s&MessagingServiceSid=%s&Body=%s" with the three encoded parts.
    char postData[ENCODED_TO_MAX + ENCODED_SVC_MAX + ENCODED_MSG_MAX + 32];
    snprintf(postData, sizeof(postData),
             "To=%s&MessagingServiceSid=%s&Body=%s",
             encodedTo, encodedSvc, encodedBody);

    char endpoint[128];
    snprintf(endpoint, sizeof(endpoint),
             "https://api.twilio.com/2010-04-01/Accounts/%s/Messages.json",
             sidCache);

    return HttpPoster::post("[SMS]", endpoint,
                            "application/x-www-form-urlencoded", postData,
                            HttpAuthMode::BASIC, sidCache, tokenCache);
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
