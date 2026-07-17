#ifndef UNIT_TESTING

#include "NotificationWorker.h"
#include "Logger.h"
#include <freertos/task.h>

constexpr uint32_t NotificationWorker::RETRY_BACKOFF_MS[3];

void NotificationWorker::begin(NotificationChannel** channels, size_t count, bool dryRunMode) {
    channelRegistry = channels;
    channelCount    = count;
    dryRun          = dryRunMode;

    fifoQueue        = xQueueCreate(FIFO_DEPTH, sizeof(NotifMsg));
    emergencyMailbox = xQueueCreate(1, sizeof(NotifMsg));
    if (!fifoQueue || !emergencyMailbox) {
        LOG_CRITICAL("[NOTIFIER] Queue allocation FAILED — notifications unavailable");
        return;
    }

    // H3: a queue set makes no ordering guarantee between members, so an
    // emergency snapshot could be deferred behind FIFO backlog accumulated
    // during a WiFi outage. Block on a direct task notification instead and
    // poll emergency-then-FIFO on wake for strict priority.
    BaseType_t ok = xTaskCreatePinnedToCore(taskEntry, "notifier", TASK_STACK,
                                            this, TASK_PRIORITY, &taskHandle, TASK_CORE);
    if (ok != pdPASS) {
        LOG_CRITICAL("[NOTIFIER] Task creation FAILED — notifications will not be delivered");
        taskHandle = nullptr;
    }
}

uint32_t NotificationWorker::getStackHighWaterMark() const {
    if (!taskHandle) return 0;
    return (uint32_t)uxTaskGetStackHighWaterMark(taskHandle);
}

bool NotificationWorker::enqueue(const char* message, uint8_t channels) {
    if (!message) return false;
    if (dryRun) {
        LOG_EVENT("[MOCK] Notification (dry-run, ch=0x%02x): %.120s", channels, message);
        return true;
    }
    if (!fifoQueue) return false;
    NotifMsg msg;
    strncpy(msg.body, message, sizeof(msg.body) - 1);
    msg.body[sizeof(msg.body) - 1] = '\0';
    msg.channels = channels;
    if (xQueueSend(fifoQueue, &msg, 0) != pdTRUE) {
        dropCount++;
        LOG_EVENT("[NOTIFIER] FIFO full — message dropped (total dropped: %u): %.60s",
                  dropCount, message);
        return false;
    }
    // H3: wake the worker so it can drain the queue (emergency first).
    if (taskHandle) {
        xTaskNotifyGive(taskHandle);
    }
    return true;
}

bool NotificationWorker::enqueueEmergency(const char* message, uint8_t channels) {
    if (!message) return false;
    if (dryRun) {
        LOG_EVENT("[MOCK] Emergency notification (dry-run, ch=0x%02x): %.120s", channels, message);
        return true;
    }
    if (!emergencyMailbox) return false;
    NotifMsg msg;
    strncpy(msg.body, message, sizeof(msg.body) - 1);
    msg.body[sizeof(msg.body) - 1] = '\0';
    msg.channels = channels;
    // Depth-1 overwrite: always succeeds, replacing any older unsent snapshot.
    xQueueOverwrite(emergencyMailbox, &msg);
    // H3: wake the worker so the emergency snapshot is drained ahead of any
    // pending FIFO backlog.
    if (taskHandle) {
        xTaskNotifyGive(taskHandle);
    }
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
    // pending bitmask: only channels that are requested AND configured.
    // Unconfigured channels are skipped silently — not an error, just not set up yet.
    uint8_t pending = 0;
    for (size_t i = 0; i < channelCount; i++) {
        NotificationChannel* ch = channelRegistry[i];
        if (ch && (msg.channels & ch->channelFlag()) && ch->isConfigured()) {
            pending |= ch->channelFlag();
        }
    }

    for (uint8_t attempt = 1; attempt <= SEND_ATTEMPTS && pending; attempt++) {
        for (size_t i = 0; i < channelCount; i++) {
            NotificationChannel* ch = channelRegistry[i];
            if (!ch) continue;
            if (pending & ch->channelFlag()) {
                if (ch->send(msg.body)) {
                    pending &= ~ch->channelFlag();
                }
            }
        }

        if (pending && attempt < SEND_ATTEMPTS) {
            uint32_t backoff = RETRY_BACKOFF_MS[attempt - 1];
            LOG_EVENT("[NOTIFIER] Send failed (attempt %u/%u), retrying in %ums: %.60s",
                      attempt, SEND_ATTEMPTS, backoff, msg.body);
            vTaskDelay(pdMS_TO_TICKS(backoff));
        }
    }

    // Log any channels that exhausted all retries
    for (size_t i = 0; i < channelCount; i++) {
        NotificationChannel* ch = channelRegistry[i];
        if (ch && (pending & ch->channelFlag())) {
            LOG_CRITICAL("[NOTIFIER] %s send GAVE UP after %u attempts: %.60s",
                         ch->name(), SEND_ATTEMPTS, msg.body);
        }
    }
}

void NotificationWorker::run() {
    NotifMsg msg;
    for (;;) {
        // H3: strict priority. Drain the emergency mailbox completely before
        // touching the FIFO, so an emergency snapshot present at wake time is
        // always delivered ahead of one-shot events accumulated during a WiFi
        // outage. Both queues are polled non-blocking; if both are empty the
        // task blocks on a direct notification from a producer.
        if (xQueueReceive(emergencyMailbox, &msg, 0) == pdTRUE) {
            deliver(msg);
            continue;
        }
        if (xQueueReceive(fifoQueue, &msg, 0) == pdTRUE) {
            deliver(msg);
            continue;
        }
        // Nothing pending. Block until a producer (enqueue/enqueueEmergency)
        // signals. pdTRUE clears the notification count on wake so a signal
        // that arrived between the polls above and this take is not lost.
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

#endif // UNIT_TESTING
