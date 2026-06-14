#pragma once

/*
    NotifyChannelFlags.h

    Channel bitmask constants shared between NotificationWorker, channel
    implementations, and ConfigServer.  Kept in a separate header so it can be
    included by channel headers without dragging in FreeRTOS or the full worker.
*/

#include <stdint.h>

constexpr uint8_t CHAN_SMS     = 0x01;
constexpr uint8_t CHAN_DISCORD = 0x02;
constexpr uint8_t CHAN_CUSTOM  = 0x04;
constexpr uint8_t CHAN_ALL     = CHAN_SMS | CHAN_DISCORD | CHAN_CUSTOM;
