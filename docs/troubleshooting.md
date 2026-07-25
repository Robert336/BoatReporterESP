# Troubleshooting

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
- Verify Twilio credentials in the web interface (**Settings → Notifications → SMS · Twilio**) — Account SID, Auth Token, and Messaging Service SID are write-only fields saved to NVS
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
