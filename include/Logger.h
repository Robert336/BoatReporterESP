#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

// Logging system with configurable levels
// Use PRODUCTION_BUILD flag to disable verbose logging

#ifdef PRODUCTION_BUILD
    // Production mode - only critical logs
    #define LOG_DEBUG(...)    ((void)0)
    #define LOG_INFO(...)     ((void)0)
    #define LOG_CRITICAL(...) Serial.printf(__VA_ARGS__); Serial.println()
#else
    // Development mode - all logs enabled
    #define LOG_DEBUG(...)    Serial.printf(__VA_ARGS__); Serial.println()
    #define LOG_INFO(...)     Serial.printf(__VA_ARGS__); Serial.println()
    #define LOG_CRITICAL(...) Serial.printf(__VA_ARGS__); Serial.println()
#endif

// Convenience macros for common log categories
#define LOG_EVENT(...)    LOG_CRITICAL(__VA_ARGS__)
#define LOG_STATE(...)    LOG_INFO(__VA_ARGS__)
#define LOG_SETUP(...)    LOG_INFO(__VA_ARGS__)
#define LOG_STATUS(...)   LOG_DEBUG(__VA_ARGS__)
#define LOG_SENSOR(...)   LOG_DEBUG(__VA_ARGS__)

#endif // LOGGER_H
