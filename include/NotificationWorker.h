#pragma once
class NotificationWorker;
#ifndef UNIT_TESTING

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "SendSMS.h"
#include "SendDiscord.h"

// Channel bitmask for NotifMsg — add new channels here without touching the struct.
// Default (CHAN_ALL) sends to every registered channel.
constexpr uint8_t CHAN_SMS     = 0x01;
constexpr uint8_t CHAN_DISCORD = 0x02;
constexpr uint8_t CHAN_ALL     = CHAN_SMS | CHAN_DISCORD;

// SMS and Discord are paired in one item so both channels are dropped together
// if the queue is full — prevents partial delivery (SMS only, no Discord).
struct NotifMsg {
    char    body[160];
    uint8_t channels; // bitmask of CHAN_* flags
};

// Runs outbound HTTP (SMS, Discord) on Core 0 so the main-loop state machine
// is never blocked by a 10-second Twilio/Discord timeout.
//
// Two channels, joined by a queue set:
//   - fifoQueue:        one-shot event messages (silence confirm, bus error).
//                       Each is distinct, so each is delivered.
//   - emergencyMailbox: depth-1, written with xQueueOverwrite — a newer
//                       emergency snapshot replaces an older unsent one. This
//                       coalesces a backlog so that when WiFi returns after an
//                       outage the owner gets ONE current alert, not N stale ones.
class NotificationWorker {
public:
    NotificationWorker() = default;

    // dryRun = true: enqueue() logs messages instead of queuing them (mock/test mode).
    // Pass USE_MOCK at the call site so all guards live in NotificationWorker.
    void begin(SendSMS* sms, SendDiscord* discord, bool dryRun = false);

    // Enqueue a one-shot event notification. One FIFO slot = one alert;
    // returns false and logs if the FIFO is full.
    // channels: bitmask of CHAN_SMS | CHAN_DISCORD (default: CHAN_ALL)
    bool enqueue(const char* message, uint8_t channels = CHAN_ALL);

    // Enqueue an emergency snapshot into the latest-wins mailbox. Always
    // succeeds, replacing any older unsent snapshot. Use this for periodic
    // EMERGENCY-state alerts so an outage backlog collapses to the latest.
    bool enqueueEmergency(const char* message, uint8_t channels = CHAN_ALL);

    uint32_t getPendingCount() const;
    uint32_t getDropCount() const { return dropCount; }

private:
    static void taskEntry(void* arg);
    void run();
    void deliver(NotifMsg& msg); // per-channel send with bounded retry/backoff

    SendSMS*       smsService       = nullptr;
    SendDiscord*   discordService   = nullptr;
    QueueHandle_t  fifoQueue        = nullptr;
    QueueHandle_t  emergencyMailbox = nullptr;
    QueueSetHandle_t queueSet       = nullptr;
    uint32_t       dropCount        = 0;
    bool           dryRun           = false; // When true, log instead of queuing

    static constexpr size_t      FIFO_DEPTH    = 8;
    static constexpr uint32_t    TASK_STACK    = 6144;
    static constexpr UBaseType_t TASK_PRIORITY = 1;
    static constexpr BaseType_t  TASK_CORE     = 0;

    // Per-channel retry: a failed critical alert is retried with backoff instead
    // of waiting a full notification cycle. Backoff array length = ATTEMPTS - 1.
    static constexpr uint8_t  SEND_ATTEMPTS        = 4;
    static constexpr uint32_t RETRY_BACKOFF_MS[3]  = {5000, 15000, 30000};
};

#endif // UNIT_TESTING
