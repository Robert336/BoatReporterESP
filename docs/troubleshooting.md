# Troubleshooting

Symptom → observable signal → fix. For day-to-day owner language, see the [User Guide](../USER_GUIDE.md). For broker WAN/TLS connect failures, use the [live deployment runbook](../server-stack/DEPLOYMENT.md).

## Quick triage

| What you see | Likely cause | First action |
|--------------|--------------|--------------|
| Status LED **off**, alert LED **off**, water OK | NORMAL, WiFi up | None |
| Status LED **double-blink** | WiFi disconnected | Check marina/AP signal; reopen CONFIG WiFi page |
| Status LED **slow blink** | CONFIG mode | Connect to `ESP32-BilgeRise-Setup`; browse `http://192.168.4.1` |
| Status LED **fast blink** | Sensor ERROR | Sensor power / connection first — [Sensor](#sensor-failure-fast-blink) |
| Status LED off + alert LED **solid** | Tier 1 EMERGENCY | Expect SMS/Discord; silence with 5 s hold if on site |
| Status LED off + alert LED **pulsing** | Tier 2 EMERGENCY | Higher severity; same silence hold if needed |
| Alert LED off while you expected a flood | Silenced, below threshold, or sensor fail-safe | Check silence, thresholds, status LED / serial |
| WiFi page **"portal blocked"** | Captive portal | Custom MAC or whitelist — [Configuration](configuration.md#captive-portals-marina--guest-wifi) |
| Serial `[PORTAL] … PORTAL` | Portal detected | Same as above; alerts fail-fast until open |
| Serial `start_ssl_client: -1` | TCP to broker failed (not cert) | [DEPLOYMENT.md](../server-stack/DEPLOYMENT.md) |
| Serial `[EVENT] Sensor error detected!` | Invalid ADC readings | [Sensor](#sensor-failure-fast-blink) |
| No SMS/Discord | Creds, portal, or no internet | [Notifications](#no-sms-or-discord-alerts) |

The device has **two LEDs** (status GPIO 12, alert GPIO 26). Technical patterns: [Usage](usage.md).
---

## Device will not connect to WiFi

**Symptoms:** double-blink LED; STA never gets an IP; serial shows association or auth failures.

**Signals to check:**
- Serial at 115200 baud for disconnect / auth errors
- CONFIG WiFi page: RSSI, connected SSID, portal banner

**Fix:**
1. Press the button to enter CONFIG; reconnect to `ESP32-BilgeRise-Setup` (password printed to serial on boot) and re-enter WiFi credentials.
2. Check signal strength at the installation location; move closer or add an extender if RSSI is poor.
3. Confirm the password; open networks need the "Open network" checkbox when saving.
4. If the marina uses a captive portal, see the next section.

**See also:** [Configuration — WiFi](configuration.md#wifi-configuration).

---

## WiFi shows "portal blocked" / alerts stopped at a marina

**Symptoms:** device associates but outbound alerts stop; amber **"portal blocked"** banner on the WiFi page.

**Signals to check:**
- UI: `portalState` / banner on WiFi page
- Serial: `[PORTAL] State: … -> PORTAL login=<url>`
- Serial: `Captive portal active - deferring send`
- Serial: `[PORTAL] Probe failed … (keeping previous state)` — probe could not reach the internet (weak signal); last known state is kept

**Fix:**
1. Set a **Custom MAC** to one already authenticated on the marina network, **or** ask the admin to whitelist the device MAC shown on the WiFi page.
2. Wait for the 2-minute re-probe; the banner clears and alerts resume when the link is open.
3. Full procedure: [Configuration — Captive portals](configuration.md#captive-portals-marina--guest-wifi).

---

## Sensor failure (fast blink)

**Symptoms:** status LED **fast blink** (ERROR); invalid/odd level on dashboard; serial `[EVENT] Sensor error detected!`.

Fast blink means the firmware cannot get a valid water-level reading. The **most common causes** are under-voltage to the sensor and a disconnected sensor cable — check those before deeper electronics work.

**Signals to check:**
- Serial: `[EVENT] Sensor error detected!` and millivolt traces
- Debug & Calibration page: live mV (often stuck / out of range when failed)
- Physical: sensor connector seated; supply voltage present at the probe

**Fix (most likely first):**
1. **Confirm the sensor is connected to the unit** — reseat the waterproof connector / cable into the enclosure and the green terminal block on the ADC side. A loose or pulled bilge cable is the usual field failure.
2. **Confirm the sensor is getting enough voltage** — the 4–20 mA probe needs a stable supply (typically from the shared 12 V / regulated rail used for the sensor loop). Measure at the sensor terminals; a brownout, blown fuse, or open supply wire looks like a “dead sensor” to the firmware.
3. Only then check in-box wiring: ESP32 ↔ level shifter ↔ ADS1115 (SDA/SCL on GPIO 21/22), ADS1115 VDD on 5 V, sensor → C-V → ADS1115 A0 — [Hardware](hardware.md).
4. If power and connection are good but readings are merely inaccurate (not in ERROR), re-check C-V pots and two-point calibration — [Configuration — Sensor Calibration](configuration.md#sensor-calibration).
5. Power-cycle. If fast-blink returns within a minute after steps 1–3, the probe or ADC module may need replacement.

---

## No SMS or Discord alerts

**Symptoms:** EMERGENCY expected but phone/Discord quiet.

**Signals to check:**
- WiFi page portal banner (alerts fail-fast while portal-blocked)
- Notifications page: phone / webhook configured (Twilio fields are write-only once saved)
- Serial: HTTP status codes from notification sends; `Captive portal active - deferring send`
- Internet reachability from the device (RSSI, portal state)

**Fix:**
1. Clear portal block first if shown.
2. Re-enter Twilio Account SID, Auth Token, Messaging Service SID, and E.164 phone (`+…`) — **Settings → Notifications → SMS · Twilio**; use **Test**.
3. For Discord: paste the full webhook URL; test with curl/Postman if needed; confirm channel still exists.
4. Confirm Twilio trial limits / credits.
5. Serial monitor for definitive error codes.

**See also:** [Configuration — Getting API Credentials](configuration.md#getting-api-credentials).

---

## LED not showing the expected pattern

**Symptoms:** status or alert LED pattern does not match [Usage](usage.md) / [User Guide](../USER_GUIDE.md).

**Signals to check:**
- `LIGHT_PIN` = GPIO 12 (status), `ALERT_PIN` = GPIO 26 (alert) in [`BoardPins.h`](../include/BoardPins.h)
- During EMERGENCY the status LED is **supposed** to be off; the alert LED should be solid (Tier 1) or pulsing (Tier 2)

**Fix:**
1. Confirm both LEDs are wired to the correct pins with current-limiting resistors — [Hardware](hardware.md).
2. Use **Test emergency pin** in the config UI to pulse GPIO 26 and verify the alert LED.
3. Power-cycle. If patterns are still wrong, treat as unresponsive (below).

---

## Web interface not accessible

**Symptoms:** cannot open `http://192.168.4.1` or the captive portal.

**Signals to check:**
- Status LED slow blink (CONFIG)
- Phone connected to `ESP32-BilgeRise-Setup` (not marina WiFi)
- Serial: `Starting configuration server`

**Fix:**
1. Press the button once; wait up to 10 s for slow blink + AP.
2. Use `http://192.168.4.1` (not `https`).
3. Disable client VPN / aggressive WiFi assist that drops the AP.
4. Serial will show the AP password banner if you need it.

---

## MQTT / broker connect failures

**Symptoms:** no telemetry in Grafana; serial TLS/connect errors.

**Signals to check:**
- `start_ssl_client: -1` → **TCP connect failed** (DNS / DMZ / public IP / port), **not** a certificate problem
- Negative mbedTLS codes (e.g. `-0x2700`) → real TLS/cert/clock issues
- Device clock / NTP (cold boot can fail cert validity until time syncs) — [Architecture — NTP and TLS](architecture.md#ntp-and-tls-bootstrap)

**Fix:** Work the layered checklist in **[server-stack/DEPLOYMENT.md](../server-stack/DEPLOYMENT.md)**. Device-side MQTT fields: [Configuration — MQTT](configuration.md#mqtt-broker-configuration).

---

## Device unresponsive

**Symptoms:** LED dark or nonsensical on both indicators; no AP; no serial.

**Fix:**
1. Disconnect 12 V for 10 seconds and reconnect (watchdog should also recover software hangs within ~10 s during normal operation).
2. USB serial at 115200 baud for boot logs.
3. If still dead, reflash `pio run -e prod --target upload` and reconfigure WiFi.
