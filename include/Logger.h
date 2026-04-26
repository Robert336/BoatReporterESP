#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <WiFi.h>
#include "SendDiscord.h"

// Logging system with configurable levels
// Use PRODUCTION_BUILD flag to disable verbose logging

// Global Discord instance for logging (defined in main.cpp)
extern SendDiscord* g_discord;

inline void sendDiscordLog(const char* message) {
    if (!WiFi.isConnected()) return;
    if (!g_discord) return;
    
    g_discord->send(message);
}

#ifdef PRODUCTION_BUILD
    // Production mode - only critical logs
    #define LOG_DEBUG(...)    ((void)0)
    #define LOG_INFO(...)     ((void)0)
    #define LOG_CRITICAL(...) do { Serial.printf(__VA_ARGS__); Serial.println(); char _buf[256]; snprintf(_buf, sizeof(_buf), __VA_ARGS__); sendDiscordLog(_buf); } while(0)
#else
    // Development mode - all logs enabled
    #define LOG_DEBUG(...)    do { Serial.printf(__VA_ARGS__); Serial.println(); char _buf[256]; snprintf(_buf, sizeof(_buf), __VA_ARGS__); sendDiscordLog(_buf); } while(0)
    #define LOG_INFO(...)     do { Serial.printf(__VA_ARGS__); Serial.println(); char _buf[256]; snprintf(_buf, sizeof(_buf), __VA_ARGS__); sendDiscordLog(_buf); } while(0)
    #define LOG_CRITICAL(...) do { Serial.printf(__VA_ARGS__); Serial.println(); char _buf[256]; snprintf(_buf, sizeof(_buf), __VA_ARGS__); sendDiscordLog(_buf); } while(0)
#endif

// Convenience macros for common log categories
#define LOG_EVENT(...)    LOG_CRITICAL(__VA_ARGS__)
#define LOG_STATE(...)   LOG_INFO(__VA_ARGS__)
#define LOG_SETUP(...)   LOG_INFO(__VA_ARGS__)
#define LOG_STATUS(...)  LOG_DEBUG(__VA_ARGS__)
#define LOG_SENSOR(...) LOG_DEBUG(__VA_ARGS__)

#endif // LOGGER_H
