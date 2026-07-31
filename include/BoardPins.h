#pragma once

// ESP32-WROOM-32 GPIO map — single source of truth for the firmware.
// Cross-check changes against UNUSED_GPIOS in main.cpp and docs/hardware.md.
// Pure constants (no Arduino) so native unit-test builds can include this.
//
// Never assign: 6-11 (SPI flash — driving crashes the chip), 34/35/36/39
// (input-only), 1/3 (UART0), 0/2/5/15 (strapping pins).

static constexpr int BUTTON_PIN   = 27; // Silence/config button (INPUT_PULLUP, ISR)
static constexpr int ALERT_PIN    = 26; // Alert LED — Tier 1 solid / Tier 2 pulse in EMERGENCY
static constexpr int LIGHT_PIN    = 12; // Status LED (LightCode patterns; off in EMERGENCY)
static constexpr int I2C_SDA_PIN  = 21; // ADS1115 water-level sensor
static constexpr int I2C_SCL_PIN  = 22; // ADS1115 water-level sensor
