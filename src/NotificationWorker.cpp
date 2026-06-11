#ifndef UNIT_TESTING

#include "NotificationWorker.h"
#include "Logger.h"
#include <freertos/task.h>

constexpr uint32_t NotificationWorker::RETRY_BACKOFF_MS[3];

void NotificationWorker::begin(SendSMS* sms, SendDiscord* discord, bool dryRunMode) {
    smsService     = sms;
    discordService = discord;
    dryRun         = dryRunMode;

    fifoQueue        = xQueueCreate(FIFO_DEPTH, sizeof(NotifMsg));
    emergencyMailbox = xQueueCreate(1, sizeof(NotifMsg));
    // Set capacity must equal the sum of all member capacities, or adds fail.
    queueSet         = xQueueCreateSet(FIFO_DEPTH + 1);
    if (!fifoQueue || !emergencyMailbox || !queueSet) {
        LOG_CRITICAL("[NOTIFIER] Queue/set allocation FAILED — notifications unavailable");
        return;
    }
    xQueueAddToSet(fifoQueue, queueSet);
    xQueueAddToSet(emergencyMailbox, queueSet);

    TaskHandle_t handle = nullptr;
    BaseType_t ok = xTaskCreatePinnedToCore(taskEntry, "notifier", TASK_STACK,
                                            this, TASK_PRIORITY, &handle, TASK_CORE);
    if (ok != pdPASS) {
        LOG_CRITICAL("[NOTIFIER] Task creation FAILED — notifications will not be delivered");
    }
}

bool NotificationWorker::enqueue(const char* message, bool sendSms, bool sendDiscord) {
    if (!message) return false;
    if (dryRun) {
        LOG_EVENT("[MOCK] Notification (dry-run): %.120s", message);
        return true;
    }
    if (!fifoQueue) return false;
    NotifMsg msg;
    strncpy(msg.body, message, sizeof(msg.body) - 1);
    msg.body[sizeof(msg.body) - 1] = '\0';
    msg.sendSms     = sendSms;
    msg.sendDiscord = sendDiscord;
    if (xQueueSend(fifoQueue, &msg, 0) != pdTRUE) {
        dropCount++;
        LOG_EVENT("[NOTIFIER] FIFO full — message dropped (total dropped: %u): %.60s",
                  dropCount, message);
        return false;
    }
    return true;
}

bool NotificationWorker::enqueueEmergency(const char* message, bool sendSms, bool sendDiscord) {
    if (!message) return false;
    if (dryRun) {
        LOG_EVENT("[MOCK] Emergency notification (dry-run): %.120s", message);
        return true;
    }
    if (!emergencyMailbox) return false;
    NotifMsg msg;
    strncpy(msg.body, message, sizeof(msg.body) - 1);
    msg.body[sizeof(msg.body) - 1] = '\0';
    msg.sendSms     = sendSms;
    msg.sendDiscord = sendDiscord;
    // Depth-1 overwrite: always succeeds, replacing any older unsent snapshot.
    xQueueOverwrite(emergencyMailbox, &msg);
    return true;
}

uint32_t NotificationWorker::getPendingCount() const {
    if (!fifoQueue) return 0;
    return (uint32_t)uxQueueMessagesWaiting(fifoQueue);
}

void NotificationWorker::taskEntry(void* arg) {
    static_cast<NotificationWorker*>(arg)->run();
}

void NotificationWorker::deliver(NotifMsg& msg) {
    // Track per-channel so a channel that already succeeded is never re-sent.
    bool smsPending     = msg.sendSms;
    bool discordPending = msg.sendDiscord;

    for (uint8_t attempt = 1;
         attempt <= SEND_ATTEMPTS && (smsPending || discordPending);
         attempt++) {

        if (smsPending && smsService->send(msg.body))         smsPending = false;
        if (discordPending && discordService->send(msg.body)) discordPending = false;

        if ((smsPending || discordPending) && attempt < SEND_ATTEMPTS) {
            uint32_t backoff = RETRY_BACKOFF_MS[attempt - 1];
            LOG_EVENT("[NOTIFIER] Send failed (attempt %u/%u), retrying in %ums: %.60s",
                      attempt, SEND_ATTEMPTS, backoff, msg.body);
            vTaskDelay(pdMS_TO_TICKS(backoff));
        }
    }

    if (smsPending) {
        LOG_CRITICAL("[NOTIFIER] SMS send GAVE UP after %u attempts: %.60s", SEND_ATTEMPTS, msg.body);
    }
    if (discordPending) {
        LOG_CRITICAL("[NOTIFIER] Discord send GAVE UP after %u attempts: %.60s", SEND_ATTEMPTS, msg.body);
    }
}

void NotificationWorker::run() {
    NotifMsg msg;
    for (;;) {
        // Block until either channel has data; the set reports which one.
        QueueSetMemberHandle_t member = xQueueSelectFromSet(queueSet, portMAX_DELAY);

        // Emergency takes priority. Receiving only after select keeps the set in
        // sync (per the FreeRTOS queue-set contract). The depth-1 mailbox means
        // we always read the most recent snapshot the producer left behind, so a
        // backlog accumulated during an outage collapses to a single send.
        if (member == emergencyMailbox) {
            if (xQueueReceive(emergencyMailbox, &msg, 0) == pdTRUE) {
                deliver(msg);
            }
        } else if (member == fifoQueue) {
            if (xQueueReceive(fifoQueue, &msg, 0) == pdTRUE) {
                deliver(msg);
            }
        }
    }
}

#endif // UNIT_TESTING
