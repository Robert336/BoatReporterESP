#pragma once

/*
    SendDiscord.h

    This module provides an easy way to send emergency notifications via Discord webhooks from an ESP32 device.

    Features:
    - Sends POST requests to a Discord webhook URL for delivering alert messages.
    - Provides an API for getting and setting the webhook URL. The URL is persisted using ESP32's non-volatile storage.
    - Offers a simple method to send a message to the configured Discord channel.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>


class SendDiscord {
    public:
        /// Constructor: Initializes the Discord module and opens preferences storage.
        SendDiscord();
        
        /// Destructor: Closes preferences storage.
        ~SendDiscord();
        
        /// Sends a message to the configured Discord webhook.
        /// @param message The message text to send
        /// @return true if the message was sent successfully, false otherwise
        /// @note Requires WiFi connection and a stored webhook URL
        bool send(const char* message);
        
        /// Updates the Discord webhook URL stored in non-volatile memory.
        /// @param newWebhookUrl The webhook URL to store (will replace any existing URL)
        void updateWebhookUrl(const char* newWebhookUrl);
        
        /// Retrieves the currently stored webhook URL.
        /// @param outBuf Buffer to store the webhook URL string
        /// @param bufferSize Size of the output buffer
        /// @return 0 on success, -1 if buffer is too small or invalid
        int getWebhookUrl(char* outBuf, size_t bufferSize);

        /// Checks if a webhook URL is currently stored in preferences.
        /// @return true if a webhook URL exists, false otherwise
        bool hasWebhookUrl();
        
    private:
        Preferences preferences;
        
        /// Escapes a string for JSON (handles quotes, backslashes, newlines, etc.)
        /// @param input The string to escape
        /// @param output Buffer to store the escaped string
        /// @param outputSize Size of the output buffer
        void escapeJsonString(const char* input, char* output, size_t outputSize);
};

