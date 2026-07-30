# Hardware & Assembly

This guide covers the components, wiring, and assembly of a BoatReporterESP unit. For the firmware side, see [Configuration](configuration.md).

## Parts List

| Component | Purpose |
|-----------|---------|
| [ESP32 Development Board](https://www.amazon.ca/IoTCrazy-ESP32-WROOM-32U-Dual-Core-Development-Type-C/dp/B0FP6DQJQ1/) | UPESY WROOM (or compatible ESP32 board). A model with an attachable WiFi antenna extends range; the boat's slip may be far from the marina's WiFi access point. |
| [ADS1115 16-bit ADC Module](https://www.amazon.ca/Converter-Programmable-Amplifier-Precision-Development/dp/B0F1D3KGG2/) | Precise analog-to-digital conversion with minimal signal noise. The ESP32's onboard ADC is too noisy for this application. |
| [4–20 mA Water Depth Sensor (0–100 cm)](https://www.amazon.ca/Submersible-Pressure-Sensors-Transmitter-Detector/dp/B0C44QLSZ1/) | Measures water depth via the pressure difference between atmosphere and the probe's position. |
| [Current-to-Voltage Converter](https://www.amazon.ca/Current-Converter-Conversion-Transmitter-Adjustable/dp/B099FJ4GFZ/) | Converts the sensor's 4–20 mA output to an analog voltage readable by the ADS1115. |
| [DC-DC Buck Converter (LM2596)](https://www.amazon.ca/BULVACK-LM2596-Converter-Module-1-25V-30V/dp/B07VVXF7YX/) | Steps down 12 VDC (boat battery) to 5 VDC for powering the ESP32. |
| [4-Channel Logic Level Converter](https://www.amazon.ca/CANADUINO%C2%AE-Converter-4-Channel-3-3V-5V-Bi-Directional/dp/B07GD6GN83) | 3.3V ↔ 5V bi-directional level shifter bridging the ESP32's I2C lines and the 5V-powered ADS1115. Only two channels are needed (SDA and SCL). |
| [Waterproof Project Enclosure](https://www.amazon.ca/Joinfworld-Electrical-Weatherproof-Waterproof-Electronics/dp/B0CHHJ49QN/) | Houses the ESP32, ADS1115, buck converter, and supporting components. Essential for marine environments. |
| [7-Pin Waterproof Connector](https://www.amazon.ca/Connector-Waterproof-Electrical-Connectors-Industrial/dp/B09PNJYF2T/) | Runs external wiring (power in, sensor, button) through the enclosure wall while keeping it sealed. |
| Push Button | Normally open, pull-up configured in software. Enters configuration mode. |
| LED Indicator | The built-in LED works, or connect an external one. Shows NORMAL/ERROR/CONFIG; never lights for EMERGENCY. |

## GPIO map

Firmware pin assignments live in `include/BoardPins.h` (the single source of truth). Cross-check any change against the `UNUSED_GPIOS` allowlist in `src/main.cpp`.

| GPIO | Role |
|------|------|
| 27 | Config / silence button (`INPUT_PULLUP`, ISR) |
| 26 | Emergency horn / alert output |
| 12 | Status LED (`LightCode` patterns) |
| 21 | I2C SDA (ADS1115) |
| 22 | I2C SCL (ADS1115) |

At boot, a curated set of otherwise-unused GPIOs (`4, 13, 14, 16, 17, 18, 19, 23, 25, 33`) is driven LOW so floating inputs do not pick up noise in a wet enclosure. **Do not** expand that allowlist without checking the WROOM-32 pin map:

| Pins | Why excluded |
|------|----------------|
| 6–11 | SPI flash — driving these crashes the chip |
| 34 / 35 / 36 / 39 | Input-only (no output driver) |
| 1 / 3 | UART0 TX/RX (serial console) |
| 0 / 2 / 5 / 15 | Strapping pins — risky at/after boot |
| 12 / 21 / 22 / 26 / 27 | Already assigned (see table above) |
| 32 | Reserved — previously used by an analog water sensor on field boards |

## Wiring

See the [main README](../README.md#hardware--wiring) for the wiring diagram and signal summary table.

### Level Shifter Rails

- ESP32 3.3V → LV side
- 5V → HV side
- GND is common to all modules
- ADS1115 VDD is 5V (HV side)
- The C-V converter output feeds ADS1115 A0

## Assembly

> 📸 **Photo placeholder:** add a photo of the assembled unit and wiring here once available.

1. Mount the ESP32, ADS1115, buck converter, and level shifter inside the waterproof enclosure.
2. Drill a hole for the 7-pin waterproof connector and mount it through the enclosure wall.
3. Wire the 12V input through the buck converter to 5V, then to the ESP32's 5V pin.
4. Wire the sensor through the C-V converter to ADS1115 A0.
5. Wire the ADS1115 SDA/SCL through the level shifter to ESP32 GPIO 21/22.
6. Wire the push button between GPIO 27 and GND.
7. Wire the status LED to GPIO 12 (with appropriate current-limiting resistor).
8. Seal the enclosure and test before deploying to the bilge.
