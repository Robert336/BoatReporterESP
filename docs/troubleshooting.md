# Troubleshooting

### Device will not connect to WiFi
- **Solution 1**: Press the button to enter CONFIG mode, reconnect to the `ESP32-BilgeRise-Setup` AP (password printed to serial on boot), and reconfigure WiFi.
- **Solution 2**: Check WiFi signal strength near the installation location.
- **Solution 3**: Verify the WiFi password is correct (check the serial monitor for connection errors).

### Sensor readings seem inaccurate or invalid
- Check wiring between the ESP32 and ADS1115 (SDA/SCL on GPIO 21/22 via the I2C logic level converter)
- Verify the level shifter is connected correctly: LV side to ESP32 3.3V, HV side to 5V, GND on both sides
- Verify the ADS1115 has power (5V and GND)
- Check the sensor connection to the ADS1115 A0 pin
- Perform two-point calibration
- Check the serial monitor for actual millivolt readings

### No SMS alerts received
- Verify Twilio credentials in the web interface (**Settings → Notifications → SMS · Twilio**): Account SID, Auth Token, and Messaging Service SID are write-only fields saved to NVS
- Check the phone number format in the web interface (must include country code, e.g. +1234567890)
- Verify the ESP32 has internet connectivity (check the serial monitor)
- Check that the Twilio account has credits (for paid accounts) or verified numbers (trial accounts)
- View detailed error messages in the serial monitor

### No Discord alerts received
- Verify the webhook URL is complete and correct in the web interface
- Test the webhook URL using curl or Postman
- Check that the Discord server and channel still exist
- Verify the ESP32 has internet connectivity
- Check the serial monitor for HTTP error codes

### LED not showing the expected pattern
- Some ESP32 boards use different pins for the built-in LED
- Check your board's pinout documentation
- Modify the `LED_BUILTIN` definition if needed
- Connect an external LED to verify functionality

### Web interface not accessible
- Verify the device is in CONFIG mode (LED slow blinking)
- Check that you are connected to the `ESP32-BilgeRise-Setup` WiFi network (password printed to serial on boot)
- Try `http://192.168.4.1` instead of the hostname
- Check firewall settings on your phone or computer
- The serial monitor will show a "Starting configuration server" message

### Serial monitor shows "[EVENT] Sensor error detected!"
- The sensor is returning invalid readings
- Check the ADS1115 I2C connection (SDA/SCL through the logic level converter)
- Verify the sensor has a proper power supply
- Check that the sensor is not damaged
- The system will automatically recover when sensor readings become valid
