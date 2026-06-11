#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <stdarg.h>

// Logging system with configurable levels
// Use PRODUCTION_BUILD flag to disable verbose logging

// Forward-declare to avoid pulling HTTPClient.h into every translation unit
class MQTTService;
extern MQTTService* g_mqtt;

// Declared in MQTTService.cpp — enqueues the message; never blocks the main loop
void sendMqttLog(const char* message);

// Log level constants (included in MQTT log messages so consumers can filter)
#define LOG_LEVEL_DEBUG    0
#define LOG_LEVEL_INFO     1
#define LOG_LEVEL_CRITICAL 2

// Core logging function: formats once, writes to Serial and MQTT log queue.
// Keeping this inline avoids a separate .cpp compile unit and keeps macro
// call sites to a single function call + branch.
inline void logMessage(uint8_t level, const char* fmt, ...) {
    // Select level prefix for MQTT consumers
    const char* prefix = (level == LOG_LEVEL_CRITICAL) ? "[CRIT] "
                       : (level == LOG_LEVEL_INFO)     ? "[INFO] "
                       :                                  "[DBG]  ";

    // Format into a single stack buffer — vsnprintf once, both sinks read it.
    // Buffer size matches the old per-macro _buf size (256 B).
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 1, fmt, args);
    va_end(args);
    if (n < 0) n = 0;
    buf[n < (int)(sizeof(buf) - 1) ? n : (int)(sizeof(buf) - 1)] = '\0';

    // Serial: write formatted message + newline
    Serial.println(buf);

    // MQTT: prepend level prefix so subscribers can filter by severity
    // Reuse buf in place — shift content right to make room for prefix.
    size_t prefixLen = strlen(prefix);
    size_t msgLen    = strlen(buf);
    // Build prefixed message in a second small buffer to avoid memmove complexity
    char mqttBuf[256];
    size_t copyLen = sizeof(mqttBuf) - prefixLen - 1;
    if (copyLen > msgLen) copyLen = msgLen;
    memcpy(mqttBuf, prefix, prefixLen);
    memcpy(mqttBuf + prefixLen, buf, copyLen);
    mqttBuf[prefixLen + copyLen] = '\0';

    sendMqttLog(mqttBuf);
}

#ifdef PRODUCTION_BUILD
    // Production mode - only critical logs
    #define LOG_DEBUG(...)    ((void)0)
    #define LOG_INFO(...)     ((void)0)
    #define LOG_CRITICAL(...) logMessage(LOG_LEVEL_CRITICAL, __VA_ARGS__)
#else
    // Development mode - all logs enabled
    #define LOG_DEBUG(...)    logMessage(LOG_LEVEL_DEBUG,    __VA_ARGS__)
    #define LOG_INFO(...)     logMessage(LOG_LEVEL_INFO,     __VA_ARGS__)
    #define LOG_CRITICAL(...) logMessage(LOG_LEVEL_CRITICAL, __VA_ARGS__)
#endif

// Convenience macros for common log categories
#define LOG_EVENT(...)    LOG_CRITICAL(__VA_ARGS__)
#define LOG_STATE(...)    LOG_INFO(__VA_ARGS__)
#define LOG_SETUP(...)    LOG_INFO(__VA_ARGS__)
#define LOG_STATUS(...)   LOG_DEBUG(__VA_ARGS__)
#define LOG_SENSOR(...)   LOG_DEBUG(__VA_ARGS__)
#define LOG_NETWORK(...)  LOG_DEBUG(__VA_ARGS__)

#endif // LOGGER_H
