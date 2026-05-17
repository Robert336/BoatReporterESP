#pragma once
#ifndef UNIT_TESTING

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "SendSMS.h"
#include "SendDiscord.h"

enum class NotifKind : uint8_t { SMS, DISCORD };

struct NotifMsg {
    NotifKind kind;
    char body[160];
};

// Runs outbound HTTP (SMS, Discord) on Core 0 so the main-loop state machine
// is never blocked by a 10-second Twilio/Discord timeout.
class NotificationWorker {
public:
    NotificationWorker() = default;

    void begin(SendSMS* sms, SendDiscord* discord);

    // Fire-and-forget: returns false only if the queue is full.
    bool enqueueSms(const char* message);
    bool enqueueDiscord(const char* message);

    uint32_t getDropCount() const { return dropCount; }

private:
    static void taskEntry(void* arg);
    void run();

    SendSMS*      smsService     = nullptr;
    SendDiscord*  discordService = nullptr;
    QueueHandle_t queue          = nullptr;
    uint32_t      dropCount      = 0;

    static constexpr size_t    QUEUE_DEPTH    = 8;
    static constexpr uint32_t  TASK_STACK     = 6144;
    static constexpr UBaseType_t TASK_PRIORITY = 1;
    static constexpr BaseType_t TASK_CORE     = 0;
};

#endif // UNIT_TESTING
