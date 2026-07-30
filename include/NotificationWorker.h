#pragma once
class NotificationWorker;
#ifndef UNIT_TESTING

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "NotifyChannelFlags.h"
#include "NotificationChannel.h"

// SMS and Discord (and any future channel) are all coalesced into one item so
// every channel is dropped together if the queue is full — prevents partial
// delivery (e.g. SMS only, no Discord).
struct NotifMsg {
    char    body[160];
    uint8_t channels; // bitmask of CHAN_* flags
};

// Core 0 HTTP notification worker. Emergency mailbox (depth-1 overwrite) is
// always drained before the FIFO — see docs/architecture.md.
class NotificationWorker {
public:
    NotificationWorker() = default;

    // dryRun = true: enqueue() logs messages instead of queuing them (mock/test mode).
    // channels: pointer to an array of NotificationChannel* of length count.
    // The caller owns the channel objects; they must outlive this worker.
    void begin(NotificationChannel** channels, size_t count, bool dryRun = false);

    // Enqueue a one-shot event notification. One FIFO slot = one alert;
    // returns false and logs if the FIFO is full.
    // channels: bitmask of CHAN_* flags (default: CHAN_ALL)
    bool enqueue(const char* message, uint8_t channels = CHAN_ALL);

    // Enqueue an emergency snapshot into the latest-wins mailbox. Always
    // succeeds, replacing any older unsent snapshot. Use this for periodic
    // EMERGENCY-state alerts so an outage backlog collapses to the latest.
    bool enqueueEmergency(const char* message, uint8_t channels = CHAN_ALL);

    uint32_t getPendingCount() const;
    uint32_t getDropCount() const { return dropCount; }

    // H5: stack high-water mark for the worker task (bytes of free stack ever
    // remaining). Returns 0 if the task isn't running.
    uint32_t getStackHighWaterMark() const;

private:
    static void taskEntry(void* arg);
    void run();
    void deliver(NotifMsg& msg); // per-channel send with bounded retry/backoff

    // Channel registry — populated by begin(), used by deliver()
    NotificationChannel** channelRegistry = nullptr;
    size_t                channelCount     = 0;

    QueueHandle_t    fifoQueue        = nullptr;
    QueueHandle_t    emergencyMailbox = nullptr;
    TaskHandle_t     taskHandle       = nullptr;
    uint32_t         dropCount        = 0;
    bool             dryRun           = false;

    static constexpr size_t      FIFO_DEPTH    = 8;
    static constexpr uint32_t    TASK_STACK    = 8192;
    static constexpr UBaseType_t TASK_PRIORITY = 1;
    static constexpr BaseType_t  TASK_CORE     = 0;

    // Per-channel retry: a failed critical alert is retried with backoff instead
    // of waiting a full notification cycle. Backoff array length = ATTEMPTS - 1.
    static constexpr uint8_t  SEND_ATTEMPTS        = 4;
    static constexpr uint32_t RETRY_BACKOFF_MS[3]  = {5000, 15000, 30000};
};

#endif // UNIT_TESTING
