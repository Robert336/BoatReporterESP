#pragma once

/*
    SendSMS.h

    This module provides an easy way to send SMS notifications via Twilio from an ESP32 device.

    Features:
    - Sends POST requests to the Twilio API for delivering SMS messages to users' phones.
    - Provides an API for getting and setting the user's destination phone number. The phone number is persisted using ESP32's non-volatile storage.
    - Offers a simple method to send a message to the currently stored phone number.
*/

#include "secrets.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>


class SendSMS {
    public:
        /// Constructor: Initializes the SMS module and opens preferences storage.
        SendSMS();
        
        /// Destructor: Closes preferences storage.
        ~SendSMS();
        
        /// Sends an SMS message to the currently stored phone number via Twilio API.
        /// @param message The message text to send
        /// @return true if the message was sent successfully, false otherwise
        /// @note Requires WiFi connection and a stored phone number
        bool send(const char* message);
        
        /// Updates the destination phone number stored in non-volatile memory.
        /// @param newPhoneNumber The phone number to store (will replace any existing number)
        void updatePhoneNumber(const char* newPhoneNumber);
        
        /// Retrieves the currently stored phone number.
        /// @param outBuf Buffer to store the phone number string
        /// @param bufferSize Size of the output buffer
        /// @return 0 on success, -1 if buffer is too small or invalid
        int getPhoneNumber(char* outBuf, size_t bufferSize);

        /// Checks if a phone number is currently stored in preferences.
        /// @return true if a phone number exists, false otherwise
        bool hasPhoneNumber();
        
    private:
        Preferences preferences;
        static constexpr auto& twilio_account_sid = TWILIO_ACCOUNT_SID;
        static constexpr auto& twilio_auth_token = TWILIO_AUTH_TOKEN;
        
        /// Builds the Twilio API endpoint URL using the account SID from secrets
        String getEndpointUrl();
        
        /// URL-encodes a string for use in HTTP POST data.
        /// @param input The string to encode
        /// @param output Buffer to store the encoded string
        /// @param outputSize Size of the output buffer
        void urlEncode(const char* input, char* output, size_t outputSize);
};