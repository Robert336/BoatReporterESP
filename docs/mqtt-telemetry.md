# MQTT Telemetry & Logging

The device streams all log output to an MQTT broker in addition to serial. This enables live monitoring without a physical serial connection, useful for a boat at a marina.

**Configuration**: set via the web interface Notifications page (NVS-backed):
- Broker host / port (default fallback: `192.168.2.41:1883`; override this for your network)
- Optional username / password
- Base topic (default: `boat/<mac>`)

**Topics published:**
- `<baseTopic>/log`: plaintext log lines (all `LOG_*` macros)
- `<baseTopic>/availability`: `"online"` on connect, `"offline"` as LWT
- `<baseTopic>/telemetry`: structured JSON sensor reading, published every 60 s (retained)

The log queue is a 16-slot ring buffer (~4 KB RAM). Messages dropped during a slow/blocked connection are counted and reported in the periodic status log.

## Telemetry Topic (for dashboards / Home Assistant)

In addition to the plaintext log, the device publishes a numeric, structured reading to `<baseTopic>/telemetry` once per minute. Unlike the log topic, this is machine-parseable: feed it to a time-series pipeline (e.g. Telegraf → InfluxDB → Grafana) or to Home Assistant. The message is **retained**, so a consumer that connects later immediately sees the last reading.

```json
{
  "level_cm": 42.10,                  // current water level in cm (null if NaN)
  "rate_cm_30min": 2.30,              // rate-of-change trend cm/30 min (null if NaN)
  "state": "NORMAL",                  // NORMAL | ERROR | EMERGENCY | CONFIG
  "sensor_error": false,              // true when the latest sample was invalid
  "valid": true,                      // validity of level_cm in this message
  "rssi": -67,                        // WiFi signal strength (dBm)
  "chip_temp_c": 47.13,               // ESP32 die temp (uncalibrated, relative trend only)
  "emergency_level_cm": 30.0,         // configured Tier 1 threshold
  "urgent_emergency_level_cm": 50.0,  // configured Tier 2 threshold
  "fw_version": "1.1.8",              // firmware version
  "last_fw_check_s": 3600,            // seconds since last OTA update check
  "heap_free": 210000,                // free heap (bytes)
  "uptime_s": 86400                   // uptime (seconds)
}
```

Telegraf ingests every field automatically (json_v2 object parse), so all of these flow into InfluxDB and are available to the Grafana dashboard. See [`server-stack/README.md`](../server-stack/README.md) for the full monitoring-stack setup and the 12-panel dashboard reference.

Subscribe with the MAC-derived base topic, or use a wildcard to capture every device on the broker:

```bash
mosquitto_sub -h <broker> -t 'boat/+/telemetry' -v
```
