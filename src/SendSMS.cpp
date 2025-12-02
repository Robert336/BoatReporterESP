#include "SendSMS.h"
#include <Preferences.h>

SendSMS::SendSMS() {
    preferences.begin("sms", false);
}


SendSMS::~SendSMS() {
    preferences.end();
}


bool SendSMS::send(const char* message) {
    // Validate inputs
    if (!message) {
        return false;
    }
    
    // Check for active wifi connection first
    // Send message and wait for response
    if (!WiFi.isConnected()) {
        return false;
    }

    HTTPClient http;

    // Retrieve phone number from preferences
    String toPhoneNumber = preferences.getString("phone-number", "");
    if (toPhoneNumber.length() == 0) {
        // No phone number stored
        return false;
    }

    // Calculate maximum size needed for encoded strings (worst case: every char becomes %XX = 3x)
    // Plus space for "To=", "&MessagingServiceSid=", "&Body=" and null terminator
    size_t maxEncodedSize = (strlen(toPhoneNumber.c_str()) + strlen(TWILIO_ACCOUNT_SID) + strlen(message)) * 3 + 100;
    char* encodedTo = (char*)malloc(maxEncodedSize);
    char* encodedSid = (char*)malloc(maxEncodedSize);
    char* encodedBody = (char*)malloc(maxEncodedSize);
    char* postData = (char*)malloc(maxEncodedSize * 2);

    if (!encodedTo || !encodedSid || !encodedBody || !postData) {
        // Memory allocation failed
        free(encodedTo);
        free(encodedSid);
        free(encodedBody);
        free(postData);
        return false;
    }

    // URL encode each parameter
    urlEncode(toPhoneNumber.c_str(), encodedTo, maxEncodedSize);
    urlEncode(TWILIO_ACCOUNT_SID, encodedSid, maxEncodedSize);
    urlEncode(message, encodedBody, maxEncodedSize);

    // Build POST data string
    snprintf(postData, maxEncodedSize * 2, "To=%s&MessagingServiceSid=%s&Body=%s",
             encodedTo, encodedSid, encodedBody);

    String endpoint = getEndpointUrl();
    http.begin(endpoint);
    http.setAuthorization(twilio_account_sid, twilio_auth_token); // HTTP Basic Auth
    http.setTimeout(10000); // 10 second timeout
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    int httpResponseCode = http.POST(postData);

    // Free allocated memory
    free(encodedTo);
    free(encodedSid);
    free(encodedBody);
    free(postData);

    bool success = (httpResponseCode >= 200 && httpResponseCode < 300);

    http.end();
    return success;

}


void SendSMS::updatePhoneNumber(const char* newPhoneNumber) {
    if (!newPhoneNumber) {
        return; // Invalid input, do nothing
    }
    preferences.clear();
    preferences.putString("phone-number", newPhoneNumber);
}


int SendSMS::getPhoneNumber(char* outBuf, size_t bufferSize) {
    if (!outBuf || bufferSize == 0){
        return -1;
    }
    
    // Store String in a variable to avoid temporary object destruction
    String phoneNumber = preferences.getString("phone-number", "");
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