# Usage

Technical reference for LED patterns, states, button behavior, and alerts. For owner-facing language, see the [User Guide](../USER_GUIDE.md). Field triage: [Troubleshooting](troubleshooting.md).

## LEDs (two indicators)

The enclosure has **two LEDs** on separate GPIOs. They never share a meaning: the status LED never indicates a flood, and the alert LED never indicates WiFi/config/sensor health.

### Status LED (`LIGHT_PIN`, GPIO 12)

Reflects NORMAL / ERROR / CONFIG only. Forced **off** for the entire EMERGENCY state so flood indication is left to the alert LED (and outbound notifications).

| Pattern | State | Meaning |
|---------|-------|---------|
| **OFF** | NORMAL | Normal operation, water level OK, WiFi connected |
| **Double Blink** | NORMAL | Normal operation, but WiFi is disconnected |
| **Slow Blink** | CONFIG | Configuration mode active (web interface available) |
| **Fast Blink** | ERROR | Sensor error detected (check wiring and sensor) |
| **OFF** | EMERGENCY | Intentional — look at the **alert LED** instead |

Driven by `LightCode` (`include/LightCode.h`).

### Alert LED (`ALERT_PIN`, GPIO 26)

Dedicated emergency indicator. Off outside EMERGENCY. Driven every `loop()` iteration from `computeAlertPinState()` (not the status LED path).

| Pattern | When | Meaning |
|---------|------|---------|
| **Solid ON** | Tier 1 EMERGENCY | Water level ≥ emergency (warning) threshold |
| **Pulsing** | Tier 2 EMERGENCY | Water level ≥ urgent threshold (on/off timing from horn duration settings) |
| **OFF** | Otherwise | Not in EMERGENCY; notifications silenced (5 s button hold); or sensor fault fail-safe during EMERGENCY |

SMS / Discord / webhook notifications still fire independently when configured; the alert LED is the on-device visual for flood tier.

## System States

The device operates in four states:

1. **NORMAL**: Monitoring water level every cycle; all systems operational
2. **CONFIG**: Web configuration interface active, accessible via WiFi
3. **ERROR**: Sensor malfunction detected (invalid readings); the system retries
4. **EMERGENCY**: Water level above threshold; notifications sending; alert LED active

## State Transitions

- **NORMAL → CONFIG**: Button press
- **NORMAL → ERROR**: Sensor reading invalid
- **NORMAL → EMERGENCY**: Water level ≥ threshold for ≥ threshold time
- **ERROR → NORMAL**: Sensor recovers
- **ERROR → CONFIG**: Button press, honored even while the sensor is still faulted — config mode is entered only via a physical button press, so the owner is on-site and may have intentionally disconnected the sensor (e.g. to perform an OTA update over better WiFi)
- **CONFIG → NORMAL**: Configuration timeout or manual restart
- **EMERGENCY → NORMAL**: Water level drops below threshold for ≥ threshold time
- **EMERGENCY → ERROR**: Sustained sensor fault (≥60 s); a dead sensor mid-flood degrades to ERROR instead of latching in EMERGENCY off a stale reading; a transient glitch does not bounce an active flood

## Button Functions

- **Single Press** (from NORMAL or ERROR): Enter configuration mode
- **5-Second Hold** (during EMERGENCY): Toggle notification silence. When silenced, SMS and Discord alerts are suppressed and the **alert LED** turns off; pressing again re-enables them. Silence is automatically cleared when the emergency ends.
- The button is connected to GPIO 27 with an internal pull-up (press to GND). Pressing the button while in EMERGENCY (short press) is ignored to prevent accidental CONFIG entry.

## Alert Behavior

When in EMERGENCY state:
- **Tier 1** (water ≥ emergency threshold): alert LED solid; SMS and Discord notifications are sent with the current water level and rate-of-change (e.g. `+3.2 cm/30min`, omitted if fewer than two 5-minute snapshots exist yet), repeating at a configurable interval (default **15 minutes**, set via the web UI).
- **Tier 2** (water ≥ urgent threshold): alert LED pulses; both tiers' notifications are sent together when water is above the Tier 2 threshold.
- Notification delivery is handled by a background FreeRTOS task (Core 0). If a prior emergency alert is undelivered when the next one fires (e.g. WiFi outage), the older message is replaced so the owner receives the most current water level rather than a backlog of stale readings.
- The silence toggle (5-second button hold) suppresses message notifications and turns the alert LED off immediately, for either tier.
- **Sensor-fault fail-safe:** if the sensor goes invalid during EMERGENCY, the alert LED fails safe to OFF; it never drives a flood indication off a stale reading. The fault is surfaced via the EMERGENCY → ERROR transition and the sustained-failure owner notification instead.
- The serial monitor logs all events at 115200 baud; logs also stream to the MQTT broker if configured.
