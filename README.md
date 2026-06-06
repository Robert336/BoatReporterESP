# BoatReporterESP

An ESP32-based water level monitoring and alert system for boats. Automatically detects rising water levels in your boat's bilge and sends emergency notifications via SMS and webhooks.



<img width="500" alt="hero image" src="https://github.com/user-attachments/assets/67ef94db-ea68-4508-ad74-67d70f768ae9" />

Project by: Robert Mazza and David Miller
## Features

- **Real-time Water Level Monitoring** - Uses a pressure sensor with ADS1115 16-bit ADC (I2C, 100 kHz, address 0x48) for accurate readings; usable range 5–100 cm
- **Two-Tier Emergency System** - Tier 1 (configurable threshold, default 30 cm) sends SMS and Discord alerts; Tier 2 (urgent threshold, default 50 cm) activates a pulsing horn/relay on GPIO 19
- **Emergency Alerts** - Automatic SMS (via Twilio) and Discord notifications including current water level and rate-of-change (cm/30 min)
- **Rate-of-Change Display** - Main dashboard shows water level trend (e.g. `+2.1 cm / 30 min`) once 5+ minutes of readings are available; also included in emergency alert messages
- **Notification Worker** - SMS/Discord HTTP calls run on a dedicated FreeRTOS task (Core 0); latest-wins coalescing prevents stale-message backlogs after WiFi outages
- **Web Configuration Interface** - 5-page UI (dashboard, WiFi, notifications, settings, debug/calibration) served as gzip-compressed HTML via captive portal
- **OTA Firmware Updates** - Remote updates via GitHub Releases; automatic checking and installation enabled by default
- **MQTT Logging** - Streams all log output to a configurable MQTT broker; LWT availability topic for Home Assistant integration
- **Two-Point Calibration** - Accurate sensor calibration for your specific setup
- **Multiple System States** - NORMAL, ERROR, EMERGENCY, and CONFIG modes with LED indicators (including WiFi-disconnected double-blink)
- **Sensor Hardening** - I2C bus auto-recovery, stuck/over-range reading detection, and sustained sensor-failure owner notifications
- **Task Watchdog** - Automatic reboot if the main loop stalls (60-second timeout)
- **NTP Time Synchronization** - Accurate timestamping of events
- **Persistent Storage** - WiFi credentials, calibration, and notification settings saved to NVS (Non-Volatile Storage)
- **Mock Sensor Mode** - Full firmware with simulated sensor data via the `env:mock` build environment (no hardware required)

### Web Configuration Interface Pages
<img width="700" alt="Web-config-pages" src="https://github.com/user-attachments/assets/bb7b6dad-d5b2-42a6-acda-cb48fb81073b" />

## Hardware Requirements

<img width="700" alt="unit diagram with callouts for different components" src="https://github.com/user-attachments/assets/d19359c8-c228-4b33-92cd-703549a533da" />


- **[ESP32 Development Board](https://www.amazon.ca/IoTCrazy-ESP32-WROOM-32U-Dual-Core-Development-Type-C/dp/B0FP6DQJQ1/ref=sr_1_8?crid=16WJN03UW3DAD&dib=eyJ2IjoiMSJ9.PHlKJLpL4TOrBTLvcHTBBlmX3FU6JsrCFgCe6Pp3BjffMkVXoccEvFqSYgI6cc9wH9d27de3_Q8YGifJo4egzfuhAVmLgOzuwNZ8jvzjREtCzmcvg7WVBfd71b1CP8I4Y7BXn4JzNONwgtaRffXAT-FtZXy4_nh2ywMpMUWVm0FeeHGbnuloKms7rsFmrVSi90wgWv3tneh6liW4cr5fkD0ChVsYGVx3t_wzFpZAqCSJLuUwRVZ4IND9tr-yPjpT24Ppc3XJ8UYIsaMyJtT8Tl8Agw3b0P4mAnmAHbX3Wm0.VDqu0XPVxtCuBmyRBPAnfFJXXzSDc54qEx4mPAB3JC8&dib_tag=se&keywords=esp32+wifi&qid=1766944208&sprefix=esp32+wif%2Caps%2C161&sr=8-8)** - UPESY WROOM (or compatible ESP32 board). We used one that came with an attachable WiFi attenna to extend the range as the boat's slip may be far from the marina's WiFi access point.
- **[ADS1115 16-bit ADC Module](https://www.amazon.ca/Converter-Programmable-Amplifier-Precision-Development/dp/B0F1D3KGG2/ref=sr_1_5?crid=U8P407WRJNM3&dib=eyJ2IjoiMSJ9.rZWdBIexwv2SkokjtBZYw_iUg9nI20SKVyEqh6RABOrXre7K6FFxEMJOXRaQ1VXlEWf6_bUXKrY52bJ-RhY7C_zeznMVtbGE0gENTlWBiCwdSzXcLPX4qjiWrk_rn5DYe6sN_DtTGGBAU6pDCw5S6fHpEqPYoJvUobUYBw_puDKaTsLReN5Jc1qCGtBLEBAx1paKkGW-s2O_eUk-_seND5gmxZNX-WZBIlFvYGIbBzFI2RIFlzQAPvOVe1rWwVPm0aLsohqg3Rdxv910k6Z2wOHljqDgKqXl1eM6-N5ziYs.6Ic-12Cph37ljZNDkbXOviQ6BSGDjXZQ5i7Ezwa41c8&dib_tag=se&keywords=ads1115+16-bit+adc+module&qid=1766943878&sprefix=ads1115%2Caps%2C128&sr=8-5)** - For precise analog-to-digital conversion with minimal signal noise as the one onboard the ESP32 is too noisy for this application.
- **[4-20ma Water Depth Sensor 0-100cm](https://www.amazon.ca/Submersible-Pressure-Sensors-Transmitter-Detector/dp/B0C44QLSZ1/ref=sr_1_7?crid=1K5513YCPL38O&dib=eyJ2IjoiMSJ9.y-nvCIamXo4xDCL33x6hS-2Ky0SKl_I2GCQ_zCnFToRtf9Nh2xxpMwkE9rONtwJoh-KzvJ8LSEhVc4etItkUrqLWxX1ZGQoZkyUscAtsb8Y_32fTWrtgGhpll6Xbe1suPN7N1ndSGU39bIeK-raDdgWnN9nhORawzQJR8YQnvx4G3xvaLpDFFRXrCUXAncvQ12yrWj7ep3A4VRjYeBkOJgKDfh27Dayd-5qGhbyDcf935LRWDa-e0-91IDIcRBSpuxsmgnPR-dPxe65oAZxO3PEnfrvWaUS69K_SdnPzRCA.uWzE4aHakeb72LPy8j7bgnf31zuu1Ne5Gfd540cDdno&dib_tag=se&keywords=4-20ma%2Bwater%2Bpressure%2Bsensor&qid=1766943748&sprefix=4-20ma%2Bwater%2Bpressure%2Bsenso%2Caps%2C114&sr=8-7&th=1)** - For measuring the depth of water via the difference between atmosphere pressure and the pressure of where the probe is.
- **[Current-to-Voltage Converter](https://www.amazon.ca/Current-Converter-Conversion-Transmitter-Adjustable/dp/B099FJ4GFZ/ref=sr_1_5_mod_primary_new?crid=1CIY3VJ3OZQ94&dib=eyJ2IjoiMSJ9.7-5yGM-pgQma_S9iw3l9KVQXvRyvwjtvBQDSi7NNbxixXSJazNpBV7bFF4ssdoJj4o775Z-LcxnIU09Wj7BsEzWXl6EfZDfC2g12L-GQZtMLZB26Sf3c8PiUli4KHwE-3--912F6mnSpapFKjvJyQO80bmwCpiaWLA3wjVh6X2P09qeZrcLssYEhSMIVBeumutvdYXk8M2KMO2CyeHVvlHXQm2x13Oz8YAYLMsVI1-CI-HTv2L7tRsmSVsKbPobLH_kmbyjCSKZZdyDiZpwgDFQm9gc46NFBmMpDlf9HAMs.Rh5ON89iRgXGL8g7XylVGrEmXMbuMXQOaMkdfYYo37E&dib_tag=se&keywords=current+to+voltage+converter&qid=1766943550&sbo=RZvfv%2F%2FHxDF%2BO5021pAnSA%3D%3D&sprefix=current+to+v%2Caps%2C106&sr=8-5)** - For converting the sensor's 4-20ma reading to an analog voltage signal readable by the analog-to-digital converter.
- **[DC-DC Buck Converter Step Down Module](https://www.amazon.ca/BULVACK-LM2596-Converter-Module-1-25V-30V/dp/B07VVXF7YX/ref=sr_1_5?crid=1ROC6BN6Y6OR7&dib=eyJ2IjoiMSJ9.IzPm51oT3D41gXhy_bhJb0sDM8Yxnjp7MgpE8EV27npIYkwOLhME66vlJgLPbe-g60yLeUnzZ87r34BTTS9eNnQDOxdVLl7pLSQRwTDvprzzTi5yMemmQ-NWE6Zip3TRPuyDiPnwQ69wB94qLcM8QC7rTVP4UoM6Z8uqIQdBo6cleq5hXnz_faBD615LwDvoSoje5kSk7arxh1fNH5jvrGwazgeDZI_6kn24a6pADovGg236aFXJF4NAvwBSSlOkSblnJR_PsarZ0lbooBrrw0YQkubyTD_Rj9kDuAF7eJk.j-vhqnA0Mk_yMLSIWxtIh27Ms1X7nl2grLZew2L8VaA&dib_tag=se&keywords=voltage%2Bbuck&qid=1766944014&sprefix=voltage%2Bbuck%2Caps%2C135&sr=8-5&th=1)** - For steping-down 12VDC to 5VDC for powering the ESP32 off a boat battery.
- **[I2C Logic Level Converter](https://www.amazon.ca/SHILLEHTEK-PRE-SOLDERED-Bi-Directional-Compatible-Microcontrollers/dp/B0CL2R6K26/ref=pd_ci_mcx_di_int_sccai_cn_d_sccl_2_2/147-4847175-2976763?pd_rd_w=2SCHm&content-id=amzn1.sym.d6674fdf-bd00-4d07-8317-6dfd6c498cdf&pf_rd_p=d6674fdf-bd00-4d07-8317-6dfd6c498cdf&pf_rd_r=XX24NN5Y0SPZ21C7Y2CX&pd_rd_wg=dRCOK&pd_rd_r=557bd08b-da65-4592-a467-e8e140a0b864&pd_rd_i=B0CL2R6K26&th=1)** - Bi-directional level shifter to safely bridge the ESP32's 3.3V I2C lines and any 5V I2C devices (e.g. ADS1115 powered at 5V). Prevents logic-high mismatch that can damage the ESP32 or cause unreliable communication.
- **[Waterproof Project Enclosure](https://www.amazon.ca/Joinfworld-Electrical-Weatherproof-Waterproof-Electronics/dp/B0CHHJ49QN/ref=sr_1_8?crid=3C7ILQXJLYSO3&dib=eyJ2IjoiMSJ9.itg-wqa0bqsnR-cUJo6fzYikmA3OMskYzqljCFmk52v1y3qn0hi1PC8ILrdtbgT4HE7UAAoFXGWZFn6tE4gI7W_WRKH2ASw1S0vpI9JhWVqrqLCKfe7WgIq_BQKKqKetrXRs4eGAjfvaSHo_VKQ_PJjTUrTT23Jxn_F6ca-loeDIo-0vWd_L4UQgRkNe_Ka3_7cn_4Yj0nyI6oqbpfx9nv60K-aO0rvSgj96ciUNCT_H8xum_43NzhiX_yr1inJe8F7jKMhLvhqOy4eaa779hUBsOliKjPBOWydym8DBAcQ.FHX2YvkBUMjSXofxsz8-xosB_f_NBN1s8CKg61tdu4c&dib_tag=se&keywords=project+box+enclosure&qid=1780177844&sprefix=project+box+enclos%2Caps%2C248&sr=8-8&xpid=_efXN9DbDxWap)** - Weatherproof enclosure to house the ESP32, ADS1115, buck converter, and supporting components. Essential for marine environments to protect electronics from moisture and bilge splashes.
- **[7-Pin Waterproof Connector](https://www.amazon.ca/Connector-Waterproof-Electrical-Connectors-Industrial/dp/B09PNJYF2T/ref=sr_1_6?crid=1UBYD16V9DN08&dib=eyJ2IjoiMSJ9.i5gKI7-nhw8LQdAvSWXsemdckThUMJjlPy25Tt4w3eC7Hc1UPwUNUeGvt4H77PjB99flRrhWpKpc8xXiRb1TOGBrDPVTe7otSOg79o6ogIHpsYftz5exQB4pbcsWc5hBiqLiKfUGoo5q_rfpVwSOKBTJyBCrKeA--b3F0uQ0cqiXyl39wt7BND_KOQUpnKnqlfVyV_IoGXib2p4Omvqb2cIQIh53Yca4ruITGd2-CZ3fLK2n4pkBI-3G53yJRWBQ6hnyuDNkP5vpEB09-DqFoyw2oTaj0K33CWBfnxoD3MU._YgcrDNKYhUAWoeM6CAtNEiL14hw2lAii4s4Y_q7tI8&dib_tag=se&keywords=7+pin+connector&qid=1780177874&sprefix=7+pin+c%2Caps%2C824&sr=8-6)** - Waterproof connector for running external wiring (power in, sensor, alert output, button) through the enclosure wall. Keeps the enclosure sealed while allowing field-removable connections.
- **Push Button** - For entering configuration mode (normally open, pull-up configured in software)
- **LED Indicator** - Built-in LED works, or connect external LED
- **Alert Output Device** (Optional) - Connect to GPIO 19 (buzzer, relay, larger indicator light, etc.)

## Wiring Diagram

```
ESP32 Pin        Level Shifter          ADS1115
---------        -------------          -------
GPIO 21   <-->  LV1         HV1  <-->  SDA
GPIO 22   <-->  LV2         HV2  <-->  SCL
3.3V      -->   LV (low-voltage rail)
5V        -->   HV (high-voltage rail)  VDD
GND       -->   GND (both sides)        GND

ESP32 Pin       Component
---------       ---------
GPIO 23   <--   Push Button (other side to GND)
GPIO 19   -->   Alert Output (buzzer/relay/LED)
Built-in LED    Status Indicator

ADS1115
-------
A0        <--   Current-to-Voltage Converter output
VDD       -->   5V
GND       -->   GND
```

**[TODO - ADD INFO]** Add a photo or proper wiring diagram showing your actual hardware setup.

## Installation

### Prerequisites

- [PlatformIO](https://platformio.org/) IDE (VS Code extension) or PlatformIO CLI
- USB cable for programming ESP32
- Git (for cloning repository)
- All required hardware components (see [Parts Section](#hardware-requirements))

### Setup

1. Clone this repository

2. Create your secrets file from the template:
```bash
cp include/secrets.h.example include/secrets.h
```

3. Edit `include/secrets.h` with your Twilio API credentials:
```cpp
constexpr const char* TWILIO_ACCOUNT_SID = "your_account_sid_here";
constexpr const char* TWILIO_AUTH_TOKEN = "your_auth_token_here";
constexpr const char* TWILIO_MESSAGING_SERVICE_SID = "your_messaging_service_sid_here";
```

**Note:** Phone number, Discord webhook URL, and MQTT broker settings are configured through the web interface (not in secrets.h) and saved to NVS.

4. Build and upload to your ESP32. Choose the appropriate environment:
```bash
# Production build (only critical logs — use this on the boat)
pio run -e prod --target upload

# Development build (all logs enabled — for debugging)
pio run -e dev --target upload

# Mock sensor build (full firmware with simulated sensor data, no hardware needed)
pio run -e mock --target upload
```

5. Monitor serial output (115200 baud):
```bash
pio device monitor
```

> **Build pipeline note:** When any build runs, `scripts/compress_html.py` automatically gzips all pages from `dev-ui/` and `src/html/` into `src/compressed_pages.h`, which `ConfigServer.cpp` serves directly. You do not need to manually embed HTML.

## Getting API Credentials

### Twilio SMS Setup

1. Create a free account at [Twilio](https://www.twilio.com/)
2. Get your Account SID and Auth Token from the Twilio Console
3. Add these to `include/secrets.h`
4. Get a Twilio phone number
5. Enter your Twilio number and recipient number in the web configuration interface

Using a Twilio trail should be more than enough.

### Discord Webhook Setup

1. In Discord, go to Server Settings → Integrations → Webhooks
2. Click "New Webhook"
3. Choose the channel for notifications
4. Copy the Webhook URL
5. Enter the URL in the web configuration interface under "Notification Settings"

## Configuration

### First Time Setup

On first boot (or when no WiFi credentials are saved), the device automatically enters CONFIG mode:

1. The built-in LED will **slow blink** indicating CONFIG mode
2. Connect to the WiFi access point: `ESP32-BilgeRise-Setup`
   - The AP password is **unique per device**, derived from the chip ID. It is printed to the serial monitor on boot (115200 baud) at startup inside a labelled banner.
3. Open a web browser and navigate to `http://192.168.4.1` or any `http://...` domain (`https://...` will not work since the site does not use SSL) as the captive portal should automatically open `http://192.168.4.1`
4. You'll see the configuration web interface

In case you are concerned about no SSL, remember this device is not connected to the internet. The only way to man-in-the-middle attack would to be physically there with you as you connect to the config server on your boat.

### WiFi Configuration

In the web interface:
1. Enter your WiFi SSID (network name)
2. Enter your WiFi password
3. Click "Save WiFi Settings"
4. The device will restart and connect to your network

### Sensor Calibration

#### Calibrating the Current-to-Voltage (C-V) Converter Module Using a Tube of Water

Before performing any software calibration, you should adjust the potentiometers (pots) on your current-to-voltage module to ensure correct electrical conversion of the sensor output. Here’s how to do this using a tube of water and your sensor:

1. **Set Up the Hardware:**
   - Mount your water level (or pressure) sensor securely at the bottom of a transparent tube. Make sure no water leaks around the sensor.
   - Connect the sensor to your C-V converter module, following the module’s wiring instructions.
   - Connect the output of the C-V module to your multimeter’s voltage input (or to your ESP32's analog input for live readings).

2. **Power the System:**
   - Power up the ESP32, the C-V module, and the sensor. Ensure all grounds are connected.

3. **Set the Zero Point (Adjust Offset Potentiometer):**
   - With the tube completely dry or the water level at 0cm (atmospheric pressure only on the sensor), check the output voltage from the C-V converter.
   - Use a small screwdriver to adjust the 'offset' or 'zero' potentiometer on the C-V module until the output voltage reads as close to 0V as possible (or your desired baseline voltage—some sensors output a small bias at zero).

4. **Set the Span (Adjust Gain Potentiometer):**
   - Fill the tube to your desired maximum calibration level (for example, 50cm of water above the sensor).
   - Wait for the output voltage to stabilize.
   - Adjust the 'gain' or 'span' potentiometer on the module until the voltage matches the expected value for the sensor’s output range.
   - We set our zero to output 600mv and adjusted span to output 2190mv at 50cm. (Our testing water tube didn't go to 1 meter).
   
5. **Check Linearity:**
   - Lower and raise the water level (e.g., to 25cm and then back to 0cm), and confirm the C-V module responds linearly.
   - Make small adjustments to offset and gain as needed, repeating steps 3 and 4, until readings are consistent across the whole range.

6. **Lock Down Calibration:**
   - Once calibration is complete, consider marking the potentiometer positions or adding a drop of nail polish to prevent movement.
   - You are now ready to proceed to software calibration via the ESP32 interface.

**Tip:** Many C-V modules have two potentiometers—one marked “Zero” (offset) and one marked “Span” (gain). If yours has labels, follow the manufacturer’s documentation for each.

**Summary Table for Reference:**

| Step   | Condition         | Pot Adjusted | Target          |
|--------|-------------------|--------------|-----------------|
| 1      | 0cm water (dry)   | Offset/Zero  | ~0V (or baseline)|
| 2      | MAX water depth   | Gain/Span    | Expected Vmax   |

**Now your module will output a voltage directly proportional to the water level before you begin the final software calibration.**

#### Software Calibration
If you calibrated earlier with a multimeter, then you will see that the software will consistantly read off by less/more than 50mv (this is ok).

**Two-point calibration is required for accurate readings:**

1. Press the button (GPIO 23) to enter CONFIG mode
2. Access the web interface at `http://192.168.4.1` (check serial monitor for IP)
3. Go to the "Debug & Calibration" page
4. **Zero Point Calibration:**
   - Place sensor at 0cm water level (dry or at your baseline)
   - Note the millivolt reading displayed
   - Enter this value and click "Set Zero Point"
5. **Second Point Calibration:**
   - Submerge sensor to a known depth (e.g., 30cm or 50cm)
   - Note the millivolt reading
   - Enter the millivolt reading AND the actual depth in cm
   - Click "Set Second Point"
6. Calibration is automatically saved to NVS


### Emergency Threshold Configuration

The system uses two independently configurable thresholds, both set via the web interface:

**Tier 1 — Message Notifications (default: 30 cm)**
- When water exceeds this level for more than 5 seconds continuously, EMERGENCY state triggers
- Sends SMS and Discord alerts at the configured notification frequency (default: 15 minutes)

**Tier 2 — Horn/Relay Alarm (default: 50 cm)**
- When water reaches this higher threshold, GPIO 19 pulses (default: 1 s on / 1 s off)
- Both tiers activate simultaneously if water is above the Tier 2 threshold

> **Note:** The **EMERGENCY** mode is designed as a critical alert—when the threshold is reached, the device will activate the alert output and send emergency notifications.   
> **Only set the emergency threshold high enough to indicate actual danger.**  
> Setting it too low (too close to the normal bilge water level, or below minor expected splashes or condensation) may cause false alarms, unnecessary panic, and alarm fatigue.  
>  
> **Best Practice:** Set the threshold above the typical bilge water level, but below the point where water could damage equipment or overflow.
>  
> Regularly test your setup, and adjust the threshold if needed to balance prompt alerts and avoiding nuisance triggers!

### MQTT Broker Configuration

The device streams all log output to an MQTT broker (useful for Home Assistant integration or remote monitoring). The broker is fully configurable from the web interface and persisted to NVS — no recompile required.

In the web interface, open **Notification Settings → MQTT broker** and set:

| Field | Notes |
|-------|-------|
| **Broker host** | Hostname or IP of your MQTT broker (e.g. `192.168.2.41`) |
| **Port** | Defaults to `1883` |
| **Username** | Optional — leave blank for anonymous brokers |
| **Password** | Optional, write-only. **Leave blank to keep the current password** — saving an unrelated change won't wipe it |
| **Base topic** | Optional — defaults to `boat/<6-hex-MAC>` |

Click **Save** to apply (takes effect live, no reboot) and **Test** to publish a test message. The connection status pill polls every few seconds and shows `connected` / `disconnected` / `off`.

**Default broker:** out of the box (before anything is saved) the device connects to `192.168.2.41:1883` anonymously. This default lives in `DEFAULT_MQTT_HOST` in `src/MQTTService.cpp`; change it there if you want a different fallback baked into the firmware.

**Topics published:**
- `<base topic>/availability` — `online` / `offline` (retained LWT, for Home Assistant availability)
- `<base topic>/log` — all serial log output

> **Note:** Saved broker settings survive reboots and firmware flashes (NVS is preserved). Because of that, bumping `DEFAULT_MQTT_HOST` in firmware only affects devices that have *never* had a broker saved — already-configured devices keep their saved value until you change it in the UI.


## Usage

### LED Status Indicators

| Pattern | State | Meaning |
|---------|-------|---------|
| **OFF** | NORMAL | Normal operation, water level OK, WiFi connected |
| **Double Blink** | NORMAL | Normal operation but WiFi is disconnected |
| **Slow Blink** | CONFIG | Configuration mode active (web interface available) |
| **Fast Blink** | ERROR | Sensor error detected (check wiring/sensor) |
| **Solid ON** | EMERGENCY | Water level exceeded threshold! Sending alerts |

### System States

The device operates in four states:

1. **NORMAL** - Monitoring water level every cycle, all systems operational
2. **CONFIG** - Web configuration interface active, accessible via WiFi
3. **ERROR** - Sensor malfunction detected (invalid readings), system will retry
4. **EMERGENCY** - Water level above threshold, alert output active, notifications sending

### State Transitions

- **NORMAL → CONFIG**: Press button
- **NORMAL → ERROR**: Sensor reading invalid
- **NORMAL → EMERGENCY**: Water level ≥ threshold for ≥ threshold time
- **ERROR → NORMAL**: Sensor recovers
- **ERROR → CONFIG**: Press button
- **CONFIG → NORMAL**: Configuration timeout or manual restart
- **EMERGENCY → NORMAL**: Water level drops below threshold for ≥ threshold time

### Button Functions

- **Single Press** (from NORMAL or ERROR) — Enter configuration mode
- **5-Second Hold** (during EMERGENCY) — Toggle notification silence. When silenced, SMS/Discord alerts and the horn are suppressed; pressing again re-enables them. Silence is automatically cleared when the emergency ends.
- Button is connected to GPIO 23 with internal pull-up (press to GND). Pressing the button while in EMERGENCY (short press) is ignored to prevent accidental CONFIG entry.

### Alert Behavior

When in EMERGENCY state:
- **Tier 1** (water ≥ emergency threshold): SMS and Discord notifications sent with current water level and rate-of-change (e.g. `+3.2 cm/30min`, omitted if fewer than two 5-minute snapshots exist yet). Repeats at a configurable interval (default **15 minutes**, set via web UI).
- **Tier 2** (water ≥ urgent threshold): GPIO 19 pulses the alert output (default 1 second ON / 1 second OFF). Configure horn durations via web UI.
- Notification delivery is handled by a background FreeRTOS task (Core 0). If a prior emergency alert is undelivered when the next one fires (e.g. WiFi outage), the older message is replaced so the owner receives the most current water level — not a backlog of stale readings.
- Silence toggle (5-second button hold) suppresses both the horn and message notifications. The alert output is shut off immediately on silence.
- Serial monitor logs all events at 115200 baud; logs also stream to the MQTT broker if configured.

## Customization

### Adjustable Parameters (in `main.cpp`)

```cpp
static constexpr int BUTTON_PIN = 23;              // Config button GPIO
static constexpr int ALERT_PIN = 19;               // Alert output GPIO (pulses in Tier 2 emergency)
static constexpr int LIGHT_PIN = 12;               // Status LED GPIO

static constexpr int EMERGENCY_TIMEOUT_MS = 5000;  // Sustained time before EMERGENCY state triggers (5 s)

static constexpr uint32_t SENSOR_ERROR_NOTIFY_DELAY_MS  = 60000;   // 1 min sustained failure before owner SMS
static constexpr uint32_t SENSOR_ERROR_NOTIFY_REPEAT_MS = 1800000; // 30 min repeat reminder while still down

static constexpr uint32_t WDT_TIMEOUT_S = 60;     // Task watchdog — reboot if loop stalls
```

Emergency notification frequency, Tier 1 threshold, Tier 2 threshold, and horn durations are configured at runtime through the web interface and saved to NVS — no recompile required.

### Mock Mode for Testing

To test the system without physical sensors, build and upload the `env:mock` environment:

```bash
pio run -e mock --target upload
```

The mock build (`-D ENABLE_MOCK_MODE`) generates simulated water level readings. All other firmware features (WiFi, web interface, MQTT, OTA, notifications) work normally.

**Always use `-e prod` or `-e dev` when deploying with a real sensor.**

## Troubleshooting

### Device won't connect to WiFi
- **Solution 1**: Press button to enter CONFIG mode, reconnect to `ESP32-BilgeRise-Setup` AP (password printed to serial on boot), reconfigure WiFi
- **Solution 2**: Check WiFi signal strength near installation location
- **Solution 3**: Verify WiFi password is correct (check serial monitor for connection errors)

### Sensor readings seem inaccurate or invalid
- Check wiring between ESP32 and ADS1115 (SDA/SCL on GPIO 21/22 via the I2C logic level converter)
- Verify the level shifter is connected correctly: LV side to ESP32 3.3V, HV side to 5V, GND on both sides
- Verify ADS1115 has power (5V and GND)
- Check sensor connection to ADS1115 A0 pin
- Perform two-point calibration
- Check serial monitor for actual millivolt readings

### No SMS alerts received
- Verify Twilio credentials in `secrets.h`
- Check phone number format in web interface (must include country code, e.g., +1234567890)
- Verify ESP32 has internet connectivity (check serial monitor)
- Check Twilio account has credits (for paid accounts) or verified numbers (trial accounts)
- View detailed error messages in serial monitor

### No Discord alerts received
- Verify webhook URL is complete and correct in web interface
- Test webhook URL using curl or Postman
- Check Discord server/channel still exists
- Verify ESP32 has internet connectivity
- Check serial monitor for HTTP error codes

### LED not showing expected pattern
- Some ESP32 boards use different pins for built-in LED
- Check your board's pinout documentation
- Modify `LED_BUILTIN` definition if needed
- Connect external LED to verify functionality

### Web interface not accessible
- Verify device is in CONFIG mode (LED slow blinking)
- Check you're connected to `ESP32-BilgeRise-Setup` WiFi network (password printed to serial on boot)
- Try `http://192.168.4.1` instead of hostname
- Check firewall settings on your phone/computer
- Serial monitor will show "Starting configuration server" message

### Serial Monitor shows "[EVENT] Sensor error detected!"
- Sensor is returning invalid readings
- Check ADS1115 I2C connection (SDA/SCL through the logic level converter)
- Verify sensor has proper power supply
- Check sensor is not damaged
- System will automatically recover when sensor readings become valid

## Development

### Building the Project

```bash
# Clean build
pio run --target clean

# Build only (no upload)
pio run

# Upload and monitor
pio run --target upload && pio device monitor

# Upload to specific port
pio run --target upload --upload-port COM3   # Windows
pio run --target upload --upload-port /dev/ttyUSB0  # Linux
```

### Dependencies

Managed automatically by PlatformIO (defined in `platformio.ini`):
- `adafruit/Adafruit ADS1X15@^2.3.2` — ADS1115 ADC communication
- `bblanchon/ArduinoJson@^6.21.3` — GitHub API JSON parsing for OTA
- `knolleary/PubSubClient@^2.8` — MQTT client

### Adding New Features

The codebase is modular. Key areas:

- **State Machine**: `main.cpp` `loop()` function — inlined switch; `include/StateMachine.h` contains a testable extracted version used by unit tests
- **Sensor Interface**: `WaterPressureSensor.cpp` — sensor reads, I2C recovery, stuck/over-range detection, median buffer, rate-of-change
- **Web UI**: Edit HTML in `dev-ui/*.html` (or `src/html/ota.html`), then build — `scripts/compress_html.py` auto-gzips and embeds into `src/compressed_pages.h`
- **Notifications**: `NotificationWorker.cpp` (FIFO + emergency mailbox) → `SendSMS.cpp`, `SendDiscord.cpp` — add new channels here
- **MQTT Logging**: `MQTTService.cpp` — configure broker host/port/topic via web UI or `MQTTService::updateBroker()`
- **OTA Updates**: `OTAManager.cpp` — GitHub Releases API, auto-check/install, rollback detection
- **Calibration**: `WaterPressureSensor.cpp` — `voltageToCentimeters()` function

### Code Style

- Uses Arduino framework with FreeRTOS (NotificationWorker task runs on Core 0)
- State machine pattern for main logic
- Singleton pattern for managers (`WiFiManager`, `TimeManagement`)
- NVS (`Preferences`) for all persistent configuration
- Median filtering for sensor stability (10-reading circular buffer)
- All HTML gzip-compressed at build time via pre-script (`extra_scripts` in `platformio.ini`)

## MQTT Logging

The device streams all log output to an MQTT broker (in addition to serial). This enables live monitoring without a physical serial connection — useful for a boat at a marina.

**Configuration** — set via the web interface Notifications page (NVS-backed):
- Broker host / port (default fallback: `192.168.2.41:1883` — override this for your network)
- Optional username / password
- Base topic (default: `boat/<mac>`)

**Topics published:**
- `<baseTopic>/log` — plaintext log lines (all `LOG_*` macros)
- `<baseTopic>/availability` — `"online"` on connect, `"offline"` as LWT
- `<baseTopic>/telemetry` — structured JSON sensor reading, published every 60 s (retained)

The log queue is a 16-slot ring buffer (~4 KB RAM). Messages dropped during a slow/blocked connection are counted and reported in the periodic status log.

### Telemetry Topic (for dashboards / Home Assistant)

In addition to the plaintext log, the device publishes a numeric, structured reading to `<baseTopic>/telemetry` once per minute. Unlike the log topic, this is machine-parseable — feed it to a time-series pipeline (e.g. Telegraf → InfluxDB → Grafana) or to Home Assistant. The message is **retained**, so a consumer that connects later immediately sees the last reading.

```json
{
  "level_cm": 42.10,        // current water level in cm
  "rate_cm_30min": 2.30,    // rate-of-change trend (cm per 30 min)
  "state": "NORMAL",        // NORMAL | ERROR | EMERGENCY | CONFIG
  "sensor_error": false,    // true when the latest sample was invalid
  "valid": true,            // validity of the level_cm in this message
  "rssi": -67               // WiFi signal strength (dBm)
}
```

Subscribe with the MAC-derived base topic, or use a wildcard to capture every device on the broker:

```bash
mosquitto_sub -h <broker> -t 'boat/+/telemetry' -v
```

## Remote Firmware Updates (OTA)

The device checks GitHub Releases for new firmware and installs updates automatically. See [`OTA_QUICKSTART.md`](OTA_QUICKSTART.md) for the full walkthrough.

**Defaults (pre-configured):**
- GitHub repository: `Robert336/BoatReporterESP` — **forks must change this** via the OTA settings page before first deployment
- Auto-check: enabled, every 24 hours
- Auto-install: enabled — updates install without user action

Updates only run in NORMAL state (not during emergencies or config mode). Failed updates trigger automatic rollback to the previous firmware partition. The device sends SMS/Discord notifications at each stage (found, installing, success/failure).

## Safety and Deployment

### ⚠️ Important Safety Notice

**This device is a monitoring tool and should NOT be relied upon as the sole means of boat safety.**

- Always maintain proper bilge pumps with independent float switches
- Regular physical inspection of your vessel is essential
- This system is a supplementary alert mechanism
- Test the system regularly
- Ensure reliable power supply (consider battery backup)
- Follow all marine safety regulations and manufacturer guidelines

### Deployment Recommendations

**[TODO - ADD INFO]** Add real-world deployment experience and lessons learnt after we're done.

### Power Considerations

- ESP32 typical consumption: 80-160mA active, ~10mA in light sleep
- Consider adding deep sleep mode for battery operation

### Weatherproofing

- ESP32 and electronics must be in waterproof enclosure
- Use marine-grade connectors for external wiring

## Contributing

Contributions welcome! If you improve this project, please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

### Ideas for Contributions

- Add other notification methods (email, Telegram, etc.)
- Implement deep sleep mode for battery operation
- Create mobile app for configuration
- Add data logging and graphing
- Support for multiple sensors
- Battery voltage monitoring
- Temperature sensor integration

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.


## Acknowledgments

- Built with [PlatformIO](https://platformio.org/) and Arduino framework
- Uses [Adafruit ADS1X15 Library](https://github.com/adafruit/Adafruit_ADS1X15)
- ESP32 WiFi and NVS libraries from Espressif
- Inspired by a friend who wanted us to over-engineer a solution to monitor the water level in his bilge.

## Support

For issues, questions, or suggestions:
- Open an issue on the GitHub repo

## Version History

| Version | Notes |
|---------|-------|
| 1.0.0 | Initial release — two-tier emergency, OTA, MQTT logging, NotificationWorker, I2C recovery, rate-of-change, task watchdog |

---
