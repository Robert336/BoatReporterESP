#ifndef UNIT_TESTING

#include "NotificationWorker.h"
#include "Logger.h"
#include <freertos/task.h>

void NotificationWorker::begin(SendSMS* sms, SendDiscord* discord) {
    smsService     = sms;
    discordService = discord;
    queue = xQueueCreate(QUEUE_DEPTH, sizeof(NotifMsg));
    TaskHandle_t handle = nullptr;
    BaseType_t ok = xTaskCreatePinnedToCore(taskEntry, "notifier", TASK_STACK,
                                            this, TASK_PRIORITY, &handle, TASK_CORE);
    if (ok != pdPASS) {
        LOG_CRITICAL("[NOTIFIER] Task creation FAILED — notifications will not be delivered");
    }
}

bool NotificationWorker::enqueue(const char* message, bool sendSms, bool sendDiscord) {
    if (!queue || !message) return false;
    NotifMsg msg;
    strncpy(msg.body, message, sizeof(msg.body) - 1);
    msg.body[sizeof(msg.body) - 1] = '\0';
    msg.sendSms     = sendSms;
    msg.sendDiscord = sendDiscord;
    if (xQueueSend(queue, &msg, 0) != pdTRUE) {
        dropCount++;
        LOG_EVENT("[NOTIFIER] Queue full — message dropped (total dropped: %u): %.60s",
                  dropCount, message);
        return false;
    }
    return true;
}

uint32_t NotificationWorker::getPendingCount() const {
    if (!queue) return 0;
    return (uint32_t)uxQueueMessagesWaiting(queue);
}

void NotificationWorker::taskEntry(void* arg) {
    static_cast<NotificationWorker*>(arg)->run();
}

void NotificationWorker::run() {
    NotifMsg msg;
    for (;;) {
        if (xQueueReceive(queue, &msg, portMAX_DELAY) != pdTRUE) continue;

        if (msg.sendSms) {
            if (!smsService->send(msg.body)) {
                LOG_EVENT("[NOTIFIER] SMS send FAILED: %.60s", msg.body);
            }
        }
        if (msg.sendDiscord) {
            if (!discordService->send(msg.body)) {
                LOG_EVENT("[NOTIFIER] Discord send FAILED: %.60s", msg.body);
            }
        }
    }
}

#endif // UNIT_TESTING
