#pragma once
#ifndef UNIT_TESTING

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "SendSMS.h"
#include "SendDiscord.h"

// SMS and Discord are paired in one item so both channels are dropped together
// if the queue is full — prevents partial delivery (SMS only, no Discord).
struct NotifMsg {
    char body[160];
    bool sendSms;
    bool sendDiscord;
};

// Runs outbound HTTP (SMS, Discord) on Core 0 so the main-loop state machine
// is never blocked by a 10-second Twilio/Discord timeout.
class NotificationWorker {
public:
    NotificationWorker() = default;

    void begin(SendSMS* sms, SendDiscord* discord);

    // Enqueue a notification for SMS, Discord, or both atomically.
    // One queue slot = one alert; returns false and logs if the queue is full.
    bool enqueue(const char* message, bool sendSms = true, bool sendDiscord = true);

    uint32_t getPendingCount() const;
    uint32_t getDropCount() const { return dropCount; }

private:
    static void taskEntry(void* arg);
    void run();

    SendSMS*      smsService     = nullptr;
    SendDiscord*  discordService = nullptr;
    QueueHandle_t queue          = nullptr;
    uint32_t      dropCount      = 0;

    static constexpr size_t      QUEUE_DEPTH   = 8;
    static constexpr uint32_t    TASK_STACK    = 6144;
    static constexpr UBaseType_t TASK_PRIORITY = 1;
    static constexpr BaseType_t  TASK_CORE     = 0;
};

#endif // UNIT_TESTING
