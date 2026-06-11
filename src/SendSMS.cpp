#include "SendSMS.h"
#include "Logger.h"
#include <Preferences.h>

// Namespace for NVS storage
static constexpr const char* SMS_PREFS_NAMESPACE = "sms";

SendSMS::SendSMS() {
    // Don't call preferences.begin() here - NVS may not be ready for global objects
    phoneNumberCache[0] = '\0';
}


SendSMS::~SendSMS() {
    // Nothing to clean up since we open/close preferences on each operation
}


void SendSMS::loadCache() {
    phoneNumberCache[0] = '\0';
    cacheLoaded = true; // Mark loaded even on failure so we don't retry on every send

    if (!preferences.begin(SMS_PREFS_NAMESPACE, true)) {
        return;
    }
    String num = preferences.getString("phone-number", "");
    preferences.end();

    if (num.length() > 0 && num.length() < sizeof(phoneNumberCache)) {
        strncpy(phoneNumberCache, num.c_str(), sizeof(phoneNumberCache) - 1);
        phoneNumberCache[sizeof(phoneNumberCache) - 1] = '\0';
    }
}

bool SendSMS::send(const char* message) {
    // Validate inputs
    if (!message) {
        return false;
    }

    // Check for active wifi connection first
    if (!WiFi.isConnected()) {
        LOG_NETWORK("[SMS] WiFi not connected, cannot send");
        return false;
    }

    // Use in-RAM cache; load from NVS on first call only
    if (!cacheLoaded) {
        loadCache();
    }

    if (phoneNumberCache[0] == '\0') {
        // No phone number stored
        return false;
    }

    HTTPClient http;

    // Calculate maximum size needed for encoded strings (worst case: every char becomes %XX = 3x)
    // Plus space for "To=", "&MessagingServiceSid=", "&Body=" and null terminator
    size_t maxEncodedSize = (strlen(phoneNumberCache) + strlen(TWILIO_MESSAGING_SERVICE_SID) + strlen(message)) * 3 + 100;
    char* encodedTo = (char*)malloc(maxEncodedSize);
    char* encodedMessagingServiceSid = (char*)malloc(maxEncodedSize);
    char* encodedBody = (char*)malloc(maxEncodedSize);
    char* postData = (char*)malloc(maxEncodedSize * 2);

    if (!encodedTo || !encodedMessagingServiceSid || !encodedBody || !postData) {
        // Memory allocation failed
        free(encodedTo);
        free(encodedMessagingServiceSid);
        free(encodedBody);
        free(postData);
        return false;
    }

    // URL encode each parameter
    urlEncode(phoneNumberCache, encodedTo, maxEncodedSize);
    urlEncode(TWILIO_MESSAGING_SERVICE_SID, encodedMessagingServiceSid, maxEncodedSize);
    urlEncode(message, encodedBody, maxEncodedSize);

    // Build POST data string
    snprintf(postData, maxEncodedSize * 2, "To=%s&MessagingServiceSid=%s&Body=%s",
             encodedTo, encodedMessagingServiceSid, encodedBody);

    String endpoint = getEndpointUrl();
    LOG_NETWORK("[SMS] Initiating HTTP POST to Twilio - endpoint: %s", endpoint.c_str());
    
    uint32_t startTime = millis();
    http.begin(endpoint);
    http.setAuthorization(twilio_account_sid, twilio_auth_token); // HTTP Basic Auth
    http.setTimeout(10000); // 10 second timeout
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    int httpResponseCode = http.POST(postData);
    uint32_t elapsed = millis() - startTime;
    LOG_NETWORK("[SMS] HTTP response code: %d (elapsed: %u ms)", httpResponseCode, elapsed);

    if (httpResponseCode < 0) {
        LOG_NETWORK("[SMS] HTTP error: %s", http.errorToString(httpResponseCode).c_str());
    } else if (httpResponseCode < 200 || httpResponseCode >= 300) {
        // Log response body snippet for error status codes
        String responseBody = http.getString();
        if (responseBody.length() > 0) {
            // Log first 200 chars of response
            String snippet = responseBody.substring(0, 200);
            LOG_NETWORK("[SMS] Error response body: %s", snippet.c_str());
        }
    }

    // Free allocated memory
    free(encodedTo);
    free(encodedMessagingServiceSid);
    free(encodedBody);
    free(postData);

    bool success = (httpResponseCode >= 200 && httpResponseCode < 300);
    LOG_NETWORK("[SMS] Send %s (HTTP %d, %u ms)", success ? "SUCCESS" : "FAILED", httpResponseCode, elapsed);

    http.end();
    return success;

}


void SendSMS::updatePhoneNumber(const char* newPhoneNumber) {
    if (!newPhoneNumber) {
        return; // Invalid input, do nothing
    }
    
    // Open preferences for writing
    if (!preferences.begin(SMS_PREFS_NAMESPACE, false)) {
        LOG_CRITICAL("[SMS] Failed to open preferences for writing");
        return;
    }
    
    size_t bytesWritten = preferences.putString("phone-number", newPhoneNumber);
    preferences.end();

    if (bytesWritten == 0) {
        LOG_CRITICAL("[SMS] Failed to store phone number in preferences!");
    } else {
        LOG_INFO("[SMS] Phone number saved successfully (%d bytes)", bytesWritten);
        // Refresh in-RAM cache so the next send() picks up the new number without
        // needing a reboot.
        loadCache();
    }
}


int SendSMS::getPhoneNumber(char* outBuf, size_t bufferSize) {
    if (!outBuf || bufferSize == 0){
        return -1;
    }
    
    // Open preferences for reading
    if (!preferences.begin(SMS_PREFS_NAMESPACE, true)) {
        LOG_CRITICAL("[SMS] Failed to open preferences for reading");
        return -1;
    }
    
    // Store String in a variable to avoid temporary object destruction
    String phoneNumber = preferences.getString("phone-number", "");
    preferences.end();
    
    if (phoneNumber.length() == 0) {
        return -1; // No phone number stored
    }
    
    int requiredSize = phoneNumber.length() + 1;
    if (requiredSize > bufferSize) {
        return -1;
    }

    strcpy(outBuf, phoneNumber.c_str());
    return 0;
}


String SendSMS::getEndpointUrl() {
    return String("https://api.twilio.com/2010-04-01/Accounts/") + 
           String(twilio_account_sid) + 
           String("/Messages.json");
}


void SendSMS::urlEncode(const char* input, char* output, size_t outputSize) {
    if (!input || !output || outputSize == 0) {
        if (output && outputSize > 0) {
            output[0] = '\0';
        }
        return;
    }

    size_t outIdx = 0;
    char c;
    char hex[3];

    for (size_t i = 0; input[i] != '\0' && outIdx < outputSize - 1; i++) {
        c = input[i];
        // Keep alphanumeric characters and some safe characters as-is
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            if (outIdx < outputSize - 1) {
                output[outIdx++] = c;
            }
        }
        // Space becomes +
        else if (c == ' ') {
            if (outIdx < outputSize - 1) {
                output[outIdx++] = '+';
            }
        }
        // Everything else becomes %XX hex encoding (uppercase)
        else {
            if (outIdx < outputSize - 4) { // Need space for %XX
                sprintf(hex, "%02X", (unsigned char)c);
                output[outIdx++] = '%';
                output[outIdx++] = hex[0];
                output[outIdx++] = hex[1];
            }
        }
    }
    output[outIdx] = '\0';
}


bool SendSMS::hasPhoneNumber() {
    if (!cacheLoaded) {
        loadCache();
    }
    return phoneNumberCache[0] != '\0';
}
