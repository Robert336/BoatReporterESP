# Config Server API Reference

## Overview

The configuration web server runs on the ESP32 when in **CONFIG** mode. The device
hosts its own Wi‑Fi access point at `192.168.4.1:80` (SSID: `ESP32-BilgeRise-Setup`,
password derived from the chip's MAC address). All endpoints are plain HTTP.

A captive‑portal DNS server intercepts every DNS query and redirects the client
to the config server, so users can simply open any website to reach the dashboard.

**Firmware version:** 1.1.8

## Common Patterns

- All API responses are `application/json` unless the endpoint serves an HTML page.
- POST endpoints accept URL‑encoded form data (`application/x-www-form-urlencoded`).
- JSON responses are built with the shared `JsonResponder` helper class.
- HTML pages are served gzip‑compressed with `ETag`‑based caching (304 Not Modified).
- Endpoints are only available when the device is in **CONFIG** state.
- The server times out after **240 seconds** (4 minutes) of inactivity and
  automatically returns to normal STA operation.

---

## Endpoints

### Pages (HTML)

Gzip‑compressed static pages served with `Content-Encoding: gzip` and
`Cache-Control: max-age=86400, must-revalidate`. ETags include the firmware
version and build timestamp.

| Method | Path                | Description              |
|--------|---------------------|--------------------------|
| GET    | `/`                 | Main dashboard           |
| GET    | `/wifi-config`      | WiFi configuration page  |
| GET    | `/notifications-page` | Notification settings page |
| GET    | `/settings`         | Settings hub page        |
| GET    | `/debug`            | Debug and calibration page |
| GET    | `/ota-settings`     | OTA firmware settings page |

---

### Init (Merged JSON for fast page load)

These endpoints return a single JSON object with all the data a page needs,
avoiding multiple round‑trips.

#### `GET /init`

Dashboard init: sensor reading, WiFi status, and thresholds.

**Response:**

```json
{
  "wifi": {
    "connected": true,          // bool
    "ssid": "MyBoatWiFi",       // string
    "ip": "192.168.1.100",      // string
    "rssi": -55,                // int (dBm)
    "portalState": "online",    // "online" | "portal" | "unknown"
    "portalLoginUrl": ""        // string; see GET /status
  },
  "sensor": {
    "sensorAvailable": true,    // bool: false if waterSensor is null
    "valid": true,              // bool: whether the current reading is in range
    "level_cm": 12.50,          // float (2 decimals): only present when valid
    "rate_cm_30min": 0.25       // float (2 decimals): rate of change, only when available
  },
  "thresholds": {
    "emergencyWaterLevel_cm": 15.00,       // float (2 decimals): Tier 1
    "urgentEmergencyWaterLevel_cm": 30.00  // float (2 decimals): Tier 2
  }
}
```

```bash
curl http://192.168.4.1/init
```

---

#### `GET /settings/init`

Settings init: notification config booleans, emergency frequency, WiFi status,
and calibration flag.

**Response:**

```json
{
  "notifications": {
    "hasPhoneNumber": true,     // bool
    "hasDiscordWebhook": true,  // bool
    "mqttConfigured": true,    // bool
    "mqttConnected": false      // bool
  },
  "emergencyNotifFreq_ms": 300000,  // int (milliseconds)
  "wifi": {
    "connected": true,         // bool
    "ssid": "MyBoatWiFi"        // string
  },
  "hasTwoPointCalibration": true  // bool
}
```

```bash
curl http://192.168.4.1/settings/init
```

---

#### `GET /debug/init`

Debug init: current sensor reading (raw millivolts) and calibration data.

**Response:**

```json
{
  "reading": {
    "sensorAvailable": true,   // bool
    "valid": true,             // bool
    "millivolts": 1250.50,     // float (2 decimals): raw ADC millivolts
    "level_cm": 12.50          // float (2 decimals): only present when valid
  },
  "calibration": {
    "zeroPoint_mv": 600,       // int
    "hasTwoPointCalibration": true,  // bool
    "secondPoint_mv": 2500,    // int: only when hasTwoPointCalibration is true
    "secondPoint_cm": 50.00    // float (2 decimals): only when hasTwoPointCalibration is true
  }
}
```

When no sensor is available, `reading` contains `{"sensorAvailable":false}` and
`calibration` is `null`.

```bash
curl http://192.168.4.1/debug/init
```

---

### WiFi Configuration

#### `GET /status`

WiFi connection status.

**Response:**

```json
{
  "connected": true,           // bool
  "ssid": "MyBoatWiFi",        // string
  "ip": "192.168.1.100",       // string
  "rssi": -55,                 // int (dBm)
  "portalState": "online",     // "online" | "portal" | "unknown"
  "portalLoginUrl": ""         // string; portal's sign-in URL when portalState is "portal"
}
```

`portalState` reflects the captive-portal probe, not just L2 association:
`unknown` until the first probe completes (~3 s after connect), `online` when
the connectivity check passes un-hijacked, `portal` when the network is
intercepting HTTP (marina sign-in required).

```bash
curl http://192.168.4.1/status
```

---

#### `GET /wifi/networks`

List stored WiFi networks. Returns a bare JSON array (not wrapped in an object).

**Response:**

```json
["MyBoatWiFi", "MarinaGuest", "HomeNetwork"]
```

```bash
curl http://192.168.4.1/wifi/networks
```

---

#### `POST /config`

Save WiFi credentials. Returns an HTML confirmation page.

**Request parameters (form data):**

| Parameter  | Type   | Required | Description        |
|------------|--------|----------|--------------------|
| `ssid`     | string | Yes      | WiFi network SSID  |
| `password` | string | No       | WiFi password; omit or send empty for open networks (e.g. marina guest Wi‑Fi) |

**Response:** `text/html`; success page with a link back to `/`.

**Status codes:**

| Code | Condition                     |
|------|-------------------------------|
| 200  | Credentials saved             |
| 400  | Missing `ssid`                |

```bash
curl -X POST http://192.168.4.1/config \
  -d "ssid=MyBoatWiFi" \
  -d "password=supersecret"
```

---

#### `POST /wifi/remove`

Remove a stored network by SSID.

**Request parameters (form data):**

| Parameter | Type   | Required | Description       |
|-----------|--------|----------|-------------------|
| `ssid`    | string | Yes      | SSID to remove    |

**Response:**

```json
{
  "success": true
}
```

**Status codes:**

| Code | Condition          |
|------|--------------------|
| 200  | Network removed    |
| 400  | Missing `ssid`     |

```bash
curl -X POST http://192.168.4.1/wifi/remove \
  -d "ssid=MarinaGuest"
```

---

### Captive Portal Assist (Marina WiFi)

Marina networks commonly require a browser sign-in after association. The
device detects this automatically (see `portalState` in `GET /status`), and
these endpoints let the owner complete the sign-in from their phone: the
device relays the portal's pages so the marina network sees the requests
coming from the ESP32 itself, whitelisting the device.

Assist mode is time‑bounded (**5 minutes**) and exits automatically once the
connectivity probe reports the network is open.

#### `GET /portal/status`

Portal state and assist-mode status.

**Response:**

```json
{
  "state": "portal",                        // "online" | "portal" | "unknown"
  "loginUrl": "http://10.1.0.1/login?...", // string; may be empty
  "assistActive": true,                     // bool
  "ssid": "MarinaGuest"                     // string
}
```

```bash
curl http://192.168.4.1/portal/status
```

---

#### `POST /portal/assist`

Enter portal-assist mode. Requires the device to be associated to a network.

**Response:**

```json
{
  "success": true,
  "loginUrl": "http://10.1.0.1/login?..."
}
```

**Status codes:**

| Code | Condition                    |
|------|------------------------------|
| 200  | Assist mode started          |
| 409  | Not associated to a network  |

```bash
curl -X POST http://192.168.4.1/portal/assist
```

---

#### `GET|POST /portal/relay`

Relay the marina portal's pages through the device. `GET` fetches the portal
page (defaulting to the captured `loginUrl`); links, images, and form actions
in the returned HTML are rewritten to route back through the relay. `POST`
submits the portal's sign-in form as the device. Responses are `no-store`.

**Request parameters:**

| Parameter | Type   | Required | Description                                  |
|-----------|--------|----------|----------------------------------------------|
| `u`       | string | No       | URL to fetch; defaults to the captured login URL |

Relay targets are restricted to the captured portal host, private-range IPs,
and the subnet gateway, on ports 80/443 only.

**Status codes:**

| Code | Condition                                        |
|------|--------------------------------------------------|
| 200  | Relayed page                                     |
| 302  | Portal redirect; `Location` points back at the relay |
| 400  | No portal URL known                              |
| 403  | Target not allowed                               |
| 409  | Assist mode not active                           |
| 502  | Portal fetch failed                              |

---

#### `GET /portal/done`

Confirmation page ("You're online") shown once the probe reports the portal
has opened. The assist bar injected into relayed pages redirects here
automatically.

---

### Sensor & Calibration

#### `GET /read`

Current sensor reading.

**Response:**

```json
{
  "sensorAvailable": true,     // bool
  "valid": true,               // bool
  "millivolts": 1250.50,       // float (2 decimals): raw ADC millivolts
  "level_cm": 12.50,           // float (2 decimals): only present when valid
  "rate_cm_30min": 0.25        // float (2 decimals): rate of change, only when available
}
```

When no sensor is available, returns `503` with:

```json
{
  "sensorAvailable": false,
  "error": "Water sensor not connected"
}
```

```bash
curl http://192.168.4.1/read
```

---

#### `GET /calibration`

Current calibration settings.

**Response:**

```json
{
  "zeroPoint_mv": 600,         // int
  "hasTwoPointCalibration": true,  // bool
  "secondPoint_mv": 2500,      // int: only when hasTwoPointCalibration is true
  "secondPoint_cm": 50.00      // float (2 decimals): only when hasTwoPointCalibration is true
}
```

**Status codes:**

| Code | Condition               |
|------|-------------------------|
| 200  | Success                 |
| 503  | Sensor not available    |

```bash
curl http://192.168.4.1/calibration
```

---

#### `POST /calibrate/zero`

Set the zero calibration point (sensor submerged at 0 cm water depth).

**Request parameters (form data):**

| Parameter    | Type  | Required | Description                                    |
|--------------|-------|----------|------------------------------------------------|
| `millivolts` | int   | Yes      | Raw ADC millivolt reading at zero water level  |
| `level_cm`   | float | No       | Water level in cm (defaults to 0.0)            |

**Response:**

```json
{
  "success": true,
  "message": "Zero point calibrated",
  "millivolts": 600,
  "level_cm": 0.00
}
```

**Status codes:**

| Code | Condition                    |
|------|------------------------------|
| 200  | Calibrated successfully      |
| 400  | Missing `millivolts`         |
| 503  | Sensor not available         |

```bash
curl -X POST http://192.168.4.1/calibrate/zero \
  -d "millivolts=600"
```

---

#### `POST /calibrate/point2`

Set the second calibration point (sensor at a known water depth).

**Request parameters (form data):**

| Parameter    | Type  | Required | Description                                    |
|--------------|-------|----------|------------------------------------------------|
| `millivolts` | int   | Yes      | Raw ADC millivolt reading at known water level |
| `level_cm`   | float | Yes      | Known water level in cm                        |

**Response:**

```json
{
  "success": true,
  "message": "Second calibration point set",
  "millivolts": 2500,
  "level_cm": 50.00
}
```

**Status codes:**

| Code | Condition                              |
|------|----------------------------------------|
| 200  | Calibrated successfully                |
| 400  | Missing `millivolts` or `level_cm`     |
| 503  | Sensor not available                   |

```bash
curl -X POST http://192.168.4.1/calibrate/point2 \
  -d "millivolts=2500" \
  -d "level_cm=50"
```

---

### Emergency Settings

#### `GET /emergency-settings`

Current emergency thresholds.

**Response:**

```json
{
  "emergencyWaterLevel_cm": 15.00,       // float (2 decimals): Tier 1
  "emergencyNotifFreq_ms": 300000,       // int (milliseconds)
  "urgentEmergencyWaterLevel_cm": 30.00  // float (2 decimals): Tier 2
}
```

```bash
curl http://192.168.4.1/emergency-settings
```

---

#### `POST /calibration/emergency-level`

Set the **Tier 1** emergency water level threshold.

**Request parameters (form data):**

| Parameter  | Type  | Required | Description                         |
|------------|-------|----------|-------------------------------------|
| `level_cm` | float | Yes      | Water level in cm (5.0 – 100.0)     |

**Validation:**

- Must be between `5.0` and `100.0` cm (sensor usable range).
- Must be **less than** the Tier 2 (urgent) threshold.

**Response:**

```json
{
  "success": true,
  "message": "Emergency water level (Tier 1) updated",
  "level_cm": 15.00
}
```

**Status codes:**

| Code | Condition                              |
|------|----------------------------------------|
| 200  | Updated successfully                   |
| 400  | Missing `level_cm` or out of range     |

```bash
curl -X POST http://192.168.4.1/calibration/emergency-level \
  -d "level_cm=15"
```

---

#### `POST /emergency/urgent-level`

Set the **Tier 2** urgent emergency water level threshold.

**Request parameters (form data):**

| Parameter  | Type  | Required | Description                         |
|------------|-------|----------|-------------------------------------|
| `level_cm` | float | Yes      | Water level in cm (5.0 – 100.0)     |

**Validation:**

- Must be between `5.0` and `100.0` cm.
- Must be **greater than** the Tier 1 threshold.

**Response:**

```json
{
  "success": true,
  "message": "Urgent emergency water level (Tier 2) updated",
  "level_cm": 30.00
}
```

**Status codes:**

| Code | Condition                              |
|------|----------------------------------------|
| 200  | Updated successfully                   |
| 400  | Missing `level_cm` or out of range     |

```bash
curl -X POST http://192.168.4.1/emergency/urgent-level \
  -d "level_cm=30"
```

---

#### `POST /emergency/test-pin`

Test the alert output pin. Pulses the `ALERT_PIN` HIGH for 2 seconds, then LOW.

**Request parameters:** None.

**Response:**

```json
{
  "success": true,
  "message": "Emergency pin test completed (2 second pulse)"
}
```

```bash
curl -X POST http://192.168.4.1/emergency/test-pin
```

---

### Notifications

#### `GET /notifications`

Current notification settings (full detail, includes secrets like webhook URLs).

**Response:**

```json
{
  "hasPhoneNumber": true,          // bool
  "phoneNumber": "+15551234567",   // string: only when hasPhoneNumber is true
  "hasTwilioCreds": true,          // bool: Twilio API credentials configured
  "hasDiscordWebhook": true,       // bool
  "discordWebhook": "https://discord.com/api/webhooks/...",  // string: only when configured
  "hasCustomChannel": true,        // bool
  "customEndpoint": "https://example.com/alert",   // string
  "customCtype": "application/json",               // string
  "customAuth": "bearer",                          // string: "none", "basic", or "bearer"
  "customTmpl": "{\"text\":\"{{message}}\"}",      // string: body template
  "mqttConfigured": true,          // bool
  "mqttConnected": false,          // bool
  "mqttHost": "broker.example.com", // string
  "mqttPort": 1883,                // int
  "mqttUser": "boatmonitor",       // string
  "mqttBaseTopic": "boat/sensors", // string
  "mqttTls": false                 // bool
}
```

```bash
curl http://192.168.4.1/notifications
```

---

#### `GET /notifications/status`

Lean status-only JSON; booleans only, no secrets. Designed for periodic polling
(e.g. updating live status indicators on the settings page).

**Response:**

```json
{
  "hasPhoneNumber": true,       // bool
  "hasDiscordWebhook": true,    // bool
  "hasCustomChannel": false,    // bool
  "mqttConfigured": true,       // bool
  "mqttConnected": false        // bool
}
```

```bash
curl http://192.168.4.1/notifications/status
```

---

#### `POST /notifications/phone`

Set the SMS destination phone number.

**Request parameters (form data):**

| Parameter | Type   | Required | Description          |
|-----------|--------|----------|----------------------|
| `phone`   | string | Yes      | Phone number (E.164) |

**Response:**

```json
{
  "success": true,
  "message": "Phone number updated",
  "phoneNumber": "+15551234567"
}
```

**Status codes:**

| Code | Condition                  |
|------|----------------------------|
| 200  | Updated successfully       |
| 400  | Missing `phone`            |
| 503  | SMS service not available  |

```bash
curl -X POST http://192.168.4.1/notifications/phone \
  -d "phone=+15551234567"
```

---

#### `POST /notifications/twilio`

Set Twilio account credentials (SID, auth token, messaging service SID). All
three fields are optional; only provided values are updated.

**Request parameters (form data):**

| Parameter | Type   | Required | Description                  |
|-----------|--------|----------|------------------------------|
| `sid`     | string | No       | Twilio Account SID           |
| `token`   | string | No       | Twilio Auth Token            |
| `svc_sid` | string | No       | Twilio Messaging Service SID |

**Response:**

```json
{
  "success": true,
  "message": "Twilio credentials updated"
}
```

**Status codes:**

| Code | Condition                          |
|------|------------------------------------|
| 200  | Updated successfully               |
| 400  | No credentials provided (all empty)|
| 503  | SMS service not available          |

```bash
curl -X POST http://192.168.4.1/notifications/twilio \
  -d "sid=ACxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" \
  -d "token=your_auth_token" \
  -d "svc_sid=MGxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
```

---

#### `POST /notifications/discord`

Set the Discord webhook URL.

**Request parameters (form data):**

| Parameter | Type   | Required | Description             |
|-----------|--------|----------|-------------------------|
| `webhook` | string | Yes      | Discord webhook URL     |

**Response:**

```json
{
  "success": true,
  "message": "Discord webhook updated"
}
```

**Status codes:**

| Code | Condition                     |
|------|-------------------------------|
| 200  | Updated successfully          |
| 400  | Missing `webhook`             |
| 503  | Discord service not available |

```bash
curl -X POST http://192.168.4.1/notifications/discord \
  -d "webhook=https://discord.com/api/webhooks/123/abc"
```

---

#### `POST /notifications/custom`

Configure the custom HTTP notification channel.

**Request parameters (form data):**

| Parameter  | Type   | Required | Description                                          |
|------------|--------|----------|------------------------------------------------------|
| `endpoint` | string | Yes      | HTTP(S) URL to POST alerts to                        |
| `tmpl`     | string | Yes      | Body template (use `{{message}}` placeholder)         |
| `ctype`    | string | No       | Content-Type header (default: `application/json`)   |
| `auth`     | string | No       | Auth type: `none`, `basic`, or `bearer` (default: `none`) |
| `user`     | string | No       | Username (for `basic` auth)                          |
| `secret`   | string | No       | Password/token (for `basic` or `bearer` auth)        |

**Response:**

```json
{
  "success": true,
  "message": "Custom channel updated"
}
```

**Status codes:**

| Code | Condition                          |
|------|------------------------------------|
| 200  | Updated successfully               |
| 400  | Missing `endpoint` or `tmpl`       |
| 503  | Custom channel service unavailable |

```bash
curl -X POST http://192.168.4.1/notifications/custom \
  -d "endpoint=https://example.com/alert" \
  -d "tmpl={\"text\":\"{{message}}\"}" \
  -d "ctype=application/json" \
  -d "auth=bearer" \
  -d "secret=my-api-token"
```

---

#### `POST /notifications/mqtt`

Configure the MQTT broker. All parameters are optional; only provided fields
are updated. The service reconnects automatically after any change.

**Request parameters (form data):**

| Parameter | Type   | Required | Description                                      |
|-----------|--------|----------|--------------------------------------------------|
| `host`    | string | No       | MQTT broker hostname or IP                       |
| `port`    | int    | No       | MQTT broker port (default: 1883 if omitted/zero)  |
| `user`    | string | No       | MQTT username                                    |
| `pass`    | string | No       | MQTT password                                    |
| `topic`   | string | No       | Base topic prefix (e.g. `boat/sensors`)           |
| `tls`     | string | No       | Enable TLS: `"1"`, `"true"`, or `"on"`           |

**Response:**

```json
{
  "success": true,
  "message": "MQTT configuration updated"
}
```

**Status codes:**

| Code | Condition                       |
|------|---------------------------------|
| 200  | Updated successfully            |
| 400  | No valid parameters provided    |
| 503  | MQTT service not available      |

```bash
curl -X POST http://192.168.4.1/notifications/mqtt \
  -d "host=broker.example.com" \
  -d "port=8883" \
  -d "user=boatmonitor" \
  -d "pass=secret" \
  -d "topic=boat/sensors" \
  -d "tls=true"
```

---

#### `POST /notifications/emergency-freq`

Set the emergency notification repeat frequency.

**Request parameters (form data):**

| Parameter | Type | Required | Description                                    |
|-----------|------|----------|------------------------------------------------|
| `freq_ms` | int  | Yes      | Frequency in milliseconds (5000 – 3600000)      |

**Validation:** Must be between 5,000 ms (5 s) and 3,600,000 ms (1 hour).

**Response:**

```json
{
  "success": true,
  "message": "Emergency notification frequency updated",
  "freq_ms": 300000,
  "freq_seconds": 300
}
```

**Status codes:**

| Code | Condition                          |
|------|------------------------------------|
| 200  | Updated successfully               |
| 400  | Missing `freq_ms` or out of range  |

```bash
curl -X POST http://192.168.4.1/notifications/emergency-freq \
  -d "freq_ms=300000"
```

---

#### `POST /notifications/test/sms`

Send a test SMS message. Requires WiFi connectivity and a configured phone number.

**Request parameters:** None.

**Response (success):**

```json
{
  "success": true,
  "message": "Test SMS sent successfully!"
}
```

**Response (failure):**

```json
{
  "success": false,
  "error": "Failed to send test SMS. Check serial log for details."
}
```

**Status codes:**

| Code | Condition                              |
|------|----------------------------------------|
| 200  | Sent successfully                      |
| 400  | No phone number configured             |
| 500  | Send failed                            |
| 503  | SMS service not available / no WiFi    |

```bash
curl -X POST http://192.168.4.1/notifications/test/sms
```

---

#### `POST /notifications/test/discord`

Send a test Discord message. Requires WiFi connectivity and a configured webhook.

**Request parameters:** None.

**Response (success):**

```json
{
  "success": true,
  "message": "Test Discord message sent successfully!"
}
```

**Response (failure):**

```json
{
  "success": false,
  "error": "Failed to send test Discord message. Check serial log for details."
}
```

**Status codes:**

| Code | Condition                              |
|------|----------------------------------------|
| 200  | Sent successfully                      |
| 400  | No webhook configured                  |
| 500  | Send failed                            |
| 503  | Discord service not available / no WiFi|

```bash
curl -X POST http://192.168.4.1/notifications/test/discord
```

---

#### `POST /notifications/test/custom`

Send a test message via the custom HTTP channel. Requires WiFi connectivity and
a configured custom channel (endpoint + body template).

**Request parameters:** None.

**Response (success):**

```json
{
  "success": true,
  "message": "Test custom notification sent!"
}
```

**Response (failure):**

```json
{
  "success": false,
  "error": "Failed to send test notification. Check serial log for details."
}
```

**Status codes:**

| Code | Condition                                |
|------|------------------------------------------|
| 200  | Sent successfully                        |
| 400  | Custom channel not configured            |
| 500  | Send failed                              |
| 503  | Custom service not available / no WiFi   |

```bash
curl -X POST http://192.168.4.1/notifications/test/custom
```

---

#### `POST /notifications/test/mqtt`

Publish a test message to the MQTT broker at `<baseTopic>/test`. Requires WiFi
connectivity, a configured broker, and an active MQTT connection.

**Request parameters:** None.

**Response (success):**

```json
{
  "success": true,
  "message": "Test MQTT message published successfully!"
}
```

**Response (failure):**

```json
{
  "success": false,
  "error": "Failed to publish test MQTT message."
}
```

**Status codes:**

| Code | Condition                                    |
|------|----------------------------------------------|
| 200  | Published successfully                       |
| 400  | No broker configured                         |
| 500  | Publish failed                               |
| 503  | MQTT service not available / no WiFi / not connected |

```bash
curl -X POST http://192.168.4.1/notifications/test/mqtt
```

---

### OTA Updates

#### `GET /ota/status`

Current OTA state and configuration.

**Response:**

```json
{
  "currentVersion": "1.1.8",          // string
  "availableVersion": "1.2.0",        // string: empty if no update found
  "updateAvailable": true,            // bool
  "state": "idle",                    // string: one of: idle, checking, update_available,
                                      //          downloading, installing, success, failed
  "lastError": "",                    // string
  "autoCheckEnabled": true,           // bool
  "autoInstallEnabled": false,        // bool
  "notificationsEnabled": true,       // bool
  "githubRepo": "owner/repo",         // string
  "hasGithubToken": true,             // bool
  "hasUpdatePassword": false,         // bool
  "checkIntervalHours": 24,           // uint32
  "timeSinceLastCheckHours": 6.5      // float (1 decimal)
}
```

```bash
curl http://192.168.4.1/ota/status
```

---

#### `GET /ota/check`

Manually trigger an update check against the configured GitHub repository.

**Request parameters:** None.

**Response:**

```json
{
  "success": true,
  "updateAvailable": true,
  "version": "1.2.0"          // string: only present when updateAvailable is true
}
```

**Status codes:**

| Code | Condition                  |
|------|----------------------------|
| 200  | Check completed            |
| 503  | OTA manager not available  |

```bash
curl http://192.168.4.1/ota/check
```

---

#### `POST /ota/update`

Start firmware installation. The device will reboot on success, so the HTTP
response may not be received.

**Request parameters (form data):**

| Parameter  | Type   | Required | Description                              |
|------------|--------|----------|------------------------------------------|
| `password` | string | No       | Update password (if one was configured)  |

**Response (success; may not be received due to reboot):**

```json
{
  "success": true,
  "message": "Update started, device will reboot"
}
```

**Response (failure):**

```json
{
  "success": false,
  "error": "Password required"   // example: actual error from OTAManager
}
```

**Status codes:**

| Code | Condition                  |
|------|----------------------------|
| 200  | Update started             |
| 400  | Update failed to start     |
| 503  | OTA manager not available  |

```bash
curl -X POST http://192.168.4.1/ota/update \
  -d "password=my-update-password"
```

---

#### `POST /ota/settings`

Configure OTA update settings. All parameters are optional; only provided
fields are updated.

**Request parameters (form data):**

| Parameter               | Type   | Required | Description                                           |
|-------------------------|--------|----------|-------------------------------------------------------|
| `github_owner`          | string | No       | GitHub repository owner (requires `github_repo`)       |
| `github_repo`           | string | No       | GitHub repository name (requires `github_owner`)       |
| `github_token`          | string | No       | GitHub personal access token (for private repos)       |
| `update_password`       | string | No       | Password required to start an update                   |
| `auto_check`            | string | No       | `"true"` to enable automatic update checks             |
| `check_interval_hours`  | int    | No       | Hours between checks (12–168, only with `auto_check`)  |
| `auto_install`          | string | No       | `"true"` to enable automatic installation              |
| `notifications_enabled` | string | No       | `"true"` to enable OTA notifications                  |

**Response:**

```json
{
  "success": true,
  "message": "OTA settings updated"
}
```

**Status codes:**

| Code | Condition                       |
|------|---------------------------------|
| 200  | Updated successfully            |
| 400  | No valid settings provided      |
| 503  | OTA manager not available       |

```bash
curl -X POST http://192.168.4.1/ota/settings \
  -d "github_owner=myorg" \
  -d "github_repo=BoatReporterESP" \
  -d "auto_check=true" \
  -d "check_interval_hours=24" \
  -d "auto_install=false" \
  -d "notifications_enabled=true"
```

---

### 404 / Captive Portal

| Method | Path | Description                                              |
|--------|------|----------------------------------------------------------|
| Any    | `/*` | Returns `302` redirect to `http://192.168.4.1/`          |

Any request to an unknown path triggers a `302 Found` redirect to the root.
This is how the captive portal works: OS-level portal probes (Apple, Android,
Windows, ChromeOS) request random URLs, receive a 3xx, and open a mini‑browser
pointed at the dashboard. The response is intentionally tiny (no body) to free
the single‑connection slot quickly.

---

## Error Response Format

All error responses follow a consistent format:

```json
{
  "error": "Human-readable error message"
}
```

Success responses from mutating endpoints (POST) use:

```json
{
  "success": true,
  "message": "Human-readable success message"
}
```

plus any endpoint-specific fields.

---

## ETag Caching

HTML pages include an `ETag` header of the form `<FIRMWARE_VERSION>-<BUILD_TIMESTAMP>`
(e.g. `"1.1.8-Jul 23 2026 10:00:00"`). Clients that send `If-None-Match` with a
matching ETag receive a `304 Not Modified` with no body. The build timestamp
ensures that re‑flashing the same firmware version still busts the browser cache.
