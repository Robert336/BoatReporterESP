# BoatReporterESP

An ESP32-based water level monitoring and alert system for boats. Automatically detects rising water levels in your boat's bilge and sends emergency notifications via SMS and webhooks.



<img width="500" alt="hero image" src="https://github.com/user-attachments/assets/67ef94db-ea68-4508-ad74-67d70f768ae9" />

Project by: Robert Mazza and David Miller
## Features

- **Real-time Water Level Monitoring** - Uses a pressure sensor with ADS1115 16-bit ADC for accurate readings
- **Emergency Alerts** - Automatic SMS (via Twilio) and Discord notifications when water levels exceed threshold (May add more webhook integrations later)
- **Web Configuration Interface** - Easy setup via WiFi access point captive protal
- **Two-Point Calibration** - Accurate sensor calibration for your specific setup
- **Multiple System States** - NORMAL, ERROR, EMERGENCY, and CONFIG modes with LED indicators
- **NTP Time Synchronization** - Accurate timestamping of events
- **Persistent Storage** - WiFi credentials, calibration, and notification settings saved to NVS (Non-Volatile Storage)
- **Mock Mode** - Test the system without physical sensors

### Web Configuration Interface Pages
<img width="700" alt="Web-config-pages" src="https://github.com/user-attachments/assets/bb7b6dad-d5b2-42a6-acda-cb48fb81073b" />

## Hardware Requirements

<img width="700" alt="unit diagram with callouts for different components" src="https://github.com/user-attachments/assets/d19359c8-c228-4b33-92cd-703549a533da" />


- **[ESP32 Development Board](https://www.amazon.ca/IoTCrazy-ESP32-WROOM-32U-Dual-Core-Development-Type-C/dp/B0FP6DQJQ1/ref=sr_1_8?crid=16WJN03UW3DAD&dib=eyJ2IjoiMSJ9.PHlKJLpL4TOrBTLvcHTBBlmX3FU6JsrCFgCe6Pp3BjffMkVXoccEvFqSYgI6cc9wH9d27de3_Q8YGifJo4egzfuhAVmLgOzuwNZ8jvzjREtCzmcvg7WVBfd71b1CP8I4Y7BXn4JzNONwgtaRffXAT-FtZXy4_nh2ywMpMUWVm0FeeHGbnuloKms7rsFmrVSi90wgWv3tneh6liW4cr5fkD0ChVsYGVx3t_wzFpZAqCSJLuUwRVZ4IND9tr-yPjpT24Ppc3XJ8UYIsaMyJtT8Tl8Agw3b0P4mAnmAHbX3Wm0.VDqu0XPVxtCuBmyRBPAnfFJXXzSDc54qEx4mPAB3JC8&dib_tag=se&keywords=esp32+wifi&qid=1766944208&sprefix=esp32+wif%2Caps%2C161&sr=8-8)** - UPESY WROOM (or compatible ESP32 board). We used one that came with an attachable WiFi attenna to extend the range as the boat's slip may be far from the marina's WiFi access point.
- **[ADS1115 16-bit ADC Module](https://www.amazon.ca/Converter-Programmable-Amplifier-Precision-Development/dp/B0F1D3KGG2/ref=sr_1_5?crid=U8P407WRJNM3&dib=eyJ2IjoiMSJ9.rZWdBIexwv2SkokjtBZYw_iUg9nI20SKVyEqh6RABOrXre7K6FFxEMJOXRaQ1VXlEWf6_bUXKrY52bJ-RhY7C_zeznMVtbGE0gENTlWBiCwdSzXcLPX4qjiWrk_rn5DYe6sN_DtTGGBAU6pDCw5S6fHpEqPYoJvUobUYBw_puDKaTsLReN5Jc1qCGtBLEBAx1paKkGW-s2O_eUk-_seND5gmxZNX-WZBIlFvYGIbBzFI2RIFlzQAPvOVe1rWwVPm0aLsohqg3Rdxv910k6Z2wOHljqDgKqXl1eM6-N5ziYs.6Ic-12Cph37ljZNDkbXOviQ6BSGDjXZQ5i7Ezwa41c8&dib_tag=se&keywords=ads1115+16-bit+adc+module&qid=1766943878&sprefix=ads1115%2Caps%2C128&sr=8-5)** - For precise analog-to-digital conversion with minimal signal noise as the one onboard the ESP32 is too noisy for this application.
- **[4-20ma Water Depth Sensor 0-100cm](https://www.amazon.ca/Submersible-Pressure-Sensors-Transmitter-Detector/dp/B0C44QLSZ1/ref=sr_1_7?crid=1K5513YCPL38O&dib=eyJ2IjoiMSJ9.y-nvCIamXo4xDCL33x6hS-2Ky0SKl_I2GCQ_zCnFToRtf9Nh2xxpMwkE9rONtwJoh-KzvJ8LSEhVc4etItkUrqLWxX1ZGQoZkyUscAtsb8Y_32fTWrtgGhpll6Xbe1suPN7N1ndSGU39bIeK-raDdgWnN9nhORawzQJR8YQnvx4G3xvaLpDFFRXrCUXAncvQ12yrWj7ep3A4VRjYeBkOJgKDfh27Dayd-5qGhbyDcf935LRWDa-e0-91IDIcRBSpuxsmgnPR-dPxe65oAZxO3PEnfrvWaUS69K_SdnPzRCA.uWzE4aHakeb72LPy8j7bgnf31zuu1Ne5Gfd540cDdno&dib_tag=se&keywords=4-20ma%2Bwater%2Bpressure%2Bsensor&qid=1766943748&sprefix=4-20ma%2Bwater%2Bpressure%2Bsenso%2Caps%2C114&sr=8-7&th=1)** - For measuring the depth of water via the difference between atmosphere pressure and the pressure of where the probe is.
- **[Current-to-Voltage Converter](https://www.amazon.ca/Current-Converter-Conversion-Transmitter-Adjustable/dp/B099FJ4GFZ/ref=sr_1_5_mod_primary_new?crid=1CIY3VJ3OZQ94&dib=eyJ2IjoiMSJ9.7-5yGM-pgQma_S9iw3l9KVQXvRyvwjtvBQDSi7NNbxixXSJazNpBV7bFF4ssdoJj4o775Z-LcxnIU09Wj7BsEzWXl6EfZDfC2g12L-GQZtMLZB26Sf3c8PiUli4KHwE-3--912F6mnSpapFKjvJyQO80bmwCpiaWLA3wjVh6X2P09qeZrcLssYEhSMIVBeumutvdYXk8M2KMO2CyeHVvlHXQm2x13Oz8YAYLMsVI1-CI-HTv2L7tRsmSVsKbPobLH_kmbyjCSKZZdyDiZpwgDFQm9gc46NFBmMpDlf9HAMs.Rh5ON89iRgXGL8g7XylVGrEmXMbuMXQOaMkdfYYo37E&dib_tag=se&keywords=current+to+voltage+converter&qid=1766943550&sbo=RZvfv%2F%2FHxDF%2BO5021pAnSA%3D%3D&sprefix=current+to+v%2Caps%2C106&sr=8-5)** - For converting the sensor's 4-20ma reading to an analog voltage signal readable by the analog-to-digital converter.
- **[DC-DC Buck Converter Step Down Module](https://www.amazon.ca/BULVACK-LM2596-Converter-Module-1-25V-30V/dp/B07VVXF7YX/ref=sr_1_5?crid=1ROC6BN6Y6OR7&dib=eyJ2IjoiMSJ9.IzPm51oT3D41gXhy_bhJb0sDM8Yxnjp7MgpE8EV27npIYkwOLhME66vlJgLPbe-g60yLeUnzZ87r34BTTS9eNnQDOxdVLl7pLSQRwTDvprzzTi5yMemmQ-NWE6Zip3TRPuyDiPnwQ69wB94qLcM8QC7rTVP4UoM6Z8uqIQdBo6cleq5hXnz_faBD615LwDvoSoje5kSk7arxh1fNH5jvrGwazgeDZI_6kn24a6pADovGg236aFXJF4NAvwBSSlOkSblnJR_PsarZ0lbooBrrw0YQkubyTD_Rj9kDuAF7eJk.j-vhqnA0Mk_yMLSIWxtIh27Ms1X7nl2grLZew2L8VaA&dib_tag=se&keywords=voltage%2Bbuck&qid=1766944014&sprefix=voltage%2Bbuck%2Caps%2C135&sr=8-5&th=1)** - For steping-down 12VDC to 5VDC for powering the ESP32 off a boat battery.
- **Push Button** - For entering configuration mode (normally open, pull-up configured in software)
- **LED Indicator** - Built-in LED works, or connect external LED
- **Alert Output Device** (Optional) - Connect to GPIO 19 (buzzer, relay, larger indicator light, etc.)

## Wiring Diagram

```
ESP32 Pin       Component
---------       ---------
GPIO 23   <--   Push Button (other side to GND)
GPIO 19   -->   Alert Output (buzzer/relay/LED)
GPIO 21   <-->  ADS1115 SDA
GPIO 22   <-->  ADS1115 SCL
Built-in LED    Status Indicator
3.3V      -->   ADS1115 VDD
GND       -->   ADS1115 GND

ADS1115
-------
A0        <--   Water Pressure Sensor Signal
VDD       -->   3.3V or 5V (check your sensor specs)
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
```

**Note:** Phone numbers and Discord webhook URL are configured through the web interface (not in secrets.h)

4. Build and upload to your ESP32:
```bash
pio run --target upload
```

5. Monitor serial output (115200 baud):
```bash
pio device monitor
```

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
2. Connect to the WiFi access point: `BoatMonitor-Setup`
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

Set your desired water level threshold in the web interface:
- Default: 30cm
- Recommended: Set based on your bilge depth and comfort level
- When water exceeds this level for more than 1 second (configurable in code), EMERGENCY state triggers

> **Note:** The **EMERGENCY** mode is designed as a critical alert—when the threshold is reached, the device will activate the alert output and send emergency notifications.   
> **Only set the emergency threshold high enough to indicate actual danger.**  
> Setting it too low (too close to the normal bilge water level, or below minor expected splashes or condensation) may cause false alarms, unnecessary panic, and alarm fatigue.  
>  
> **Best Practice:** Set the threshold above the typical bilge water level, but below the point where water could damage equipment or overflow.
>  
> Regularly test your setup, and adjust the threshold if needed to balance prompt alerts and avoiding nuisance triggers!


## Usage

### LED Status Indicators

| Pattern | State | Meaning |
|---------|-------|---------|
| **OFF** | NORMAL | Normal operation, water level OK |
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

- **Single Press** (from NORMAL or ERROR) - Enter configuration mode
- Button is connected to GPIO 23 with internal pull-up (press to GND)

### Alert Behavior

When in EMERGENCY state:
- GPIO 19 goes HIGH (can drive relay, buzzer, etc.)
- SMS sent via Twilio
- Discord webhook message sent
- Alerts repeat every 30 seconds (configurable in code: `EMERGENCY_MESSAGE_TIMEOUT_MS`)
- Serial monitor logs all events

## Customization

### Adjustable Parameters (in `main.cpp`)

```cpp
static constexpr int BUTTON_PIN = 23;              // Config button GPIO
static constexpr int ALERT_PIN = 19;               // Alert output GPIO
static constexpr int SENSOR_PIN = 32;              // ADC pin (not used with ADS1115)
static constexpr bool USE_MOCK = true;             // Set to false for real sensor

float EMERGENCY_WATER_LEVEL_CM = 30;               // Emergency threshold (cm)
int EMERGENCY_TIMEOUT_MS = 1000;                   // Delay before state change (ms)
int EMERGENCY_MESSAGE_TIMEOUT_MS = 1000 * 30;      // Time between alert messages (30 sec)
```

### Mock Mode for Testing

To test the system without physical sensors:

1. In `main.cpp`, ensure `USE_MOCK = true`
2. Upload to ESP32
3. The system will generate simulated water level readings
4. Use the web interface to test configuration and alerts

**Set `USE_MOCK = false` when deploying to actual boat!**

## Troubleshooting

### Device won't connect to WiFi
- **Solution 1**: Press button to enter CONFIG mode, reconnect to `BoatMonitor-Setup` AP, reconfigure WiFi
- **Solution 2**: Check WiFi signal strength near installation location
- **Solution 3**: Verify WiFi password is correct (check serial monitor for connection errors)

### Sensor readings seem inaccurate or invalid
- Check wiring between ESP32 and ADS1115 (SDA/SCL on GPIO 21/22)
- Verify ADS1115 has power (3.3V or 5V and GND)
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
- Check you're connected to `BoatMonitor-Setup` WiFi network
- Try `http://192.168.4.1` instead of hostname
- Check firewall settings on your phone/computer
- Serial monitor will show "Starting configuration server" message

### Serial Monitor shows "[EVENT] Sensor error detected!"
- Sensor is returning invalid readings
- Check ADS1115 I2C connection (SDA/SCL)
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

Managed automatically by PlatformIO:
- Adafruit ADS1X15 Library (v2.3.2) - For ADS1115 ADC communication

### Adding New Features

The codebase is modular. Key areas:

- **State Machine**: `main.cpp` loop() function
- **Sensor Interface**: `WaterPressureSensor.cpp` - Add new sensor types here
- **Web UI**: `ConfigServer.cpp` - HTML is embedded as strings
- **Notifications**: `SendSMS.cpp` and `SendDiscord.cpp` - Add new notification methods
- **Calibration**: `WaterPressureSensor.cpp` - `voltageToCentimeters()` function

### Code Style

- Uses Arduino framework conventions
- State machine pattern for main logic
- Singleton pattern for managers (WiFiManager, TimeManagement)
- NVS (Non-Volatile Storage) for persistence
- Median filtering for sensor stability (10-reading buffer)

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

No versions released yet

---
