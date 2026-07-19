#pragma once

/*
    BoardPins.h

    Single source of truth for the ESP32-WROOM-32 GPIO map used by this
    firmware. Previously these were scattered: BUTTON/ALERT/SENSOR/LIGHT in
    main.cpp and the I2C pins in WaterPressureSensor.h (with a second
    hardcoded ALERT_PIN=26 literal inside ConfigServer.cpp). Centralizing
    them makes pin-conflict review possible in one place — cross-check any
    change here against the UNUSED_GPIOS allowlist in main.cpp.

    Pure constants, no Arduino dependency — safe in native unit-test builds.

    WROOM-32 pins deliberately NOT assigned here:
      - 6-11        SPI flash — driving these crashes the chip instantly
      - 34/35/36/39 input-only, no output driver
      - 1/3         UART0 TX/RX — serial console
      - 0/2/5/15    strapping pins — driving at/after boot is risky
*/

static constexpr int BUTTON_PIN   = 27; // Silence/config button (INPUT_PULLUP, ISR)
static constexpr int ALERT_PIN    = 26; // Emergency horn/alert output
static constexpr int LIGHT_PIN    = 12; // Status LED (LightCode patterns)
static constexpr int I2C_SDA_PIN  = 21; // ADS1115 water-level sensor
static constexpr int I2C_SCL_PIN  = 22; // ADS1115 water-level sensor
