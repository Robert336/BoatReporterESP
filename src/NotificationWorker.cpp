#ifndef UNIT_TESTING

#include "NotificationWorker.h"
#include <freertos/task.h>

void NotificationWorker::begin(SendSMS* sms, SendDiscord* discord) {
    smsService     = sms;
    discordService = discord;
    queue = xQueueCreate(QUEUE_DEPTH, sizeof(NotifMsg));
    xTaskCreatePinnedToCore(taskEntry, "notifier", TASK_STACK, this,
                            TASK_PRIORITY, nullptr, TASK_CORE);
}

bool NotificationWorker::enqueueSms(const char* message) {
    if (!queue || !message) return false;
    NotifMsg msg;
    msg.kind = NotifKind::SMS;
    strncpy(msg.body, message, sizeof(msg.body) - 1);
    msg.body[sizeof(msg.body) - 1] = '\0';
    if (xQueueSend(queue, &msg, 0) != pdTRUE) {
        dropCount++;
        return false;
    }
    return true;
}

bool NotificationWorker::enqueueDiscord(const char* message) {
    if (!queue || !message) return false;
    NotifMsg msg;
    msg.kind = NotifKind::DISCORD;
    strncpy(msg.body, message, sizeof(msg.body) - 1);
    msg.body[sizeof(msg.body) - 1] = '\0';
    if (xQueueSend(queue, &msg, 0) != pdTRUE) {
        dropCount++;
        return false;
    }
    return true;
}

void NotificationWorker::taskEntry(void* arg) {
    static_cast<NotificationWorker*>(arg)->run();
}

void NotificationWorker::run() {
    NotifMsg msg;
    for (;;) {
        if (xQueueReceive(queue, &msg, portMAX_DELAY) == pdTRUE) {
            switch (msg.kind) {
                case NotifKind::SMS:
                    smsService->send(msg.body);
                    break;
                case NotifKind::DISCORD:
                    discordService->send(msg.body);
                    break;
            }
        }
    }
}

#endif // UNIT_TESTING
