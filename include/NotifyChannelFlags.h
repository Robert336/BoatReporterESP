#pragma once

// CHAN_* bitmasks for NotifMsg — no FreeRTOS dependency.

#include <stdint.h>

constexpr uint8_t CHAN_SMS     = 0x01;
constexpr uint8_t CHAN_DISCORD = 0x02;
constexpr uint8_t CHAN_CUSTOM  = 0x04;
constexpr uint8_t CHAN_ALL     = CHAN_SMS | CHAN_DISCORD | CHAN_CUSTOM;
