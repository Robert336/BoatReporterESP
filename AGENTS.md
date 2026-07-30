# AGENTS.md — BoatReporterESP

Guidance for coding agents working in this repository. Human-facing product docs live elsewhere; this file is about **how to change the code safely**.

## What this project is (one paragraph)

ESP32 firmware for boat bilge water-level monitoring: ADS1115 + 4–20 mA pressure sensor → state machine → SMS/Discord/webhook alerts + MQTT telemetry + captive-portal web config + OTA from GitHub Releases. Companion pieces: `dev-ui/` (web UI mock), `server-stack/` (Mosquitto/Telegraf/InfluxDB/Grafana).

Do not restate architecture, API schemas, wiring steps, or user setup here. Use the doc map below.

## Doc map — read these, do not duplicate them

| Need | Read |
|------|------|
| Product overview / features | [`README.md`](README.md) |
| Component layout, FreeRTOS tasks, data flow, partitions | [`docs/architecture.md`](docs/architecture.md) |
| GPIO wiring, parts, assembly | [`docs/hardware.md`](docs/hardware.md) |
| Runtime config (WiFi, alerts, MQTT, calibration) | [`docs/configuration.md`](docs/configuration.md) |
| LED / button / alert behavior (operator view) | [`docs/usage.md`](docs/usage.md), [`USER_GUIDE.md`](USER_GUIDE.md) |
| MQTT topics & telemetry JSON | [`docs/mqtt-telemetry.md`](docs/mqtt-telemetry.md) |
| REST endpoints (ConfigServer) | [`docs/api-reference.md`](docs/api-reference.md) |
| OTA release / rollback | [`OTA_QUICKSTART.md`](OTA_QUICKSTART.md) |
| Unit-test philosophy & how to run | [`test/TESTING_README.md`](test/TESTING_README.md) |
| Web UI mock server | [`dev-ui/README.md`](dev-ui/README.md) |
| Monitoring stack deploy | [`server-stack/README.md`](server-stack/README.md), [`server-stack/DEPLOYMENT.md`](server-stack/DEPLOYMENT.md) |
| Contribution / versioning conventions | [`CONTRIBUTING.md`](CONTRIBUTING.md) |

Index of all guides: [`docs/README.md`](docs/README.md).

## Repository map (where to edit)

```
include/          Public headers. Prefer logic here when it must be unit-tested.
  BoardPins.h     ONLY place for GPIO pin numbers
  StateMachine.h  Pure state-machine logic (header-only, native-testable)
  Version.h       FIRMWARE_VERSION string (must match platformio.ini prod flag)
src/              .cpp implementations (Arduino/FreeRTOS). Most TUs wrap in #ifndef UNIT_TESTING
  main.cpp        setup()/loop(), wiring of subsystems, UNUSED_GPIOS allowlist
  compressed_pages.h   AUTO-GENERATED — never edit; produced by scripts/compress_html.py
dev-ui/           Editable HTML for the captive-portal UI + Node mock server
scripts/compress_html.py   Pre-build: gzips dev-ui/*.html → src/compressed_pages.h
test/             Native Unity tests + mocks/ (no ESP32 required)
server-stack/     Docker Compose monitoring stack (separate from firmware)
partitions.csv    4 MB OTA dual-slot layout — changing sizes affects flash/OTA
```

**Find behavior by subsystem**

| Concern | Primary files |
|---------|---------------|
| State transitions / horn / silence / emergency debounce | `include/StateMachine.h`, `src/main.cpp` |
| Sensor read / filter / calibration / I2C recovery | `include/WaterPressureSensor.h`, `src/WaterPressureSensor.cpp` |
| WiFi STA/AP, captive-portal probe, MAC override | `include/WiFiManager.h`, `src/WiFiManager.cpp` |
| Notifications (queue + channels) | `NotificationWorker.*`, `SmsChannel.*`, `DiscordChannel.*`, `CustomChannel.*`, `HttpPoster.*` |
| Web config + REST | `ConfigServer.*`, HTML in `dev-ui/` |
| MQTT telemetry / logs | `MQTTService.*`, `MqttRootCA.h` |
| OTA check/download/install/rollback | `OTAManager.*` |
| Thresholds persisted in NVS | `SettingsStore.*` (namespace `"emergency"`) |
| GPIO map | `include/BoardPins.h` + `UNUSED_GPIOS` in `src/main.cpp` |
| Logging macros | `include/Logger.h` (`LOG_DEBUG`/`INFO`/`CRITICAL`; prod strips non-critical) |

## Build environments (`platformio.ini`)

| Env | Use when |
|-----|----------|
| `dev` | Local flash; all log levels |
| `prod` | Boat deployment; `-D PRODUCTION_BUILD` + version flag |
| `mock` | Full firmware, simulated sensor (`ENABLE_MOCK_MODE`) — soak tests without ADC |
| `native` | Host unit tests (`UNIT_TESTING`) |
| `esp32-test` | On-device Unity tests with mock sensor |

Common commands:

```bash
pio run -e dev --target upload
pio run -e prod --target upload
pio run -e mock --target upload
pio test -e native
cd dev-ui && npm install && npm start   # UI at http://localhost:3000
```

No compile-time secrets file is required. Credentials are entered in the web UI and stored in NVS. Do not commit `include/secrets.h`, `server-stack/.env`, or TLS material (see `.gitignore`).

## Non-negotiable behavioral invariants

These are safety / field constraints. Breaking them is a regression even if tests pass.

1. **Flood beats config.** CONFIG is an overlay. Water ≥ Tier 1 for the debounce window must still transition to EMERGENCY while the portal is open.
2. **Do not block Core 1 with HTTP.** Outbound SMS/Discord/webhook and OTA downloads belong on Core 0 tasks (`NotificationWorker`, OTA check task). `loop()` must keep feeding the 10 s task watchdog.
3. **Flood-abort OTA.** Downloads must abort if water reaches Tier 1 mid-transfer; do not auto-install while in EMERGENCY.
4. **Emergency notification coalescing.** Emergency mailbox is depth-1 overwrite (latest-wins). Do not turn it into an unbounded backlog.
5. **Portal-aware outbound gating.** While WiFi link is `PORTAL`, `HttpPoster` must fail fast — do not burn the 10 s HTTP timeout on a captive hijack.
6. **NTP before TLS MQTT.** Clock starts at epoch 0; MQTT TLS can fail until NTP syncs. Preserve backoff / retry behavior.
7. **LED off in EMERGENCY.** Status LED (GPIO 12) must stay forced off during floods.
8. **Pin map is centralized.** Change pins only in `BoardPins.h`, then update `UNUSED_GPIOS` in `main.cpp`. Never hardcode GPIO literals in `.cpp` files.
9. **NVS survives OTA.** Config/calibration live in NVS partitions that must remain intact across updates. Do not casually resize/reorder `partitions.csv` without an explicit migration plan.
10. **Version twin.** Shipping firmware changes: bump `FIRMWARE_VERSION` in **both** `include/Version.h` and `platformio.ini` (`env:prod` `-D FIRMWARE_VERSION=...`) so they match.

## Hardware facts agents need (coding constraints)

Full parts/assembly: [`docs/hardware.md`](docs/hardware.md). For code changes, remember:

| Pin | Role | Notes |
|-----|------|-------|
| GPIO 21 / 22 | I2C SDA / SCL | ADS1115 via 3.3↔5 V level shifter |
| GPIO 27 | Button | `INPUT_PULLUP`, ISR; short = CONFIG, ~5 s hold = silence toggle in EMERGENCY |
| GPIO 26 | Alert / horn | Tier-2 pulsed output |
| GPIO 12 | Status LED | Patterns via `LightCode`; forced off in EMERGENCY |
| — | ADS1115 A0 | From current-to-voltage converter (sensor is 4–20 mA, not a voltage probe) |

**Do not use / assign without review:** GPIO 6–11 (SPI flash — crash), 1/3 (UART0 console), 0/2/5/15 (strapping), 34/35/36/39 (input-only). GPIO 32 is reserved (legacy analog probe wiring still in the field).

**Why external ADC exists:** ESP32 onboard ADC conflicts with WiFi and is too noisy/non-linear for bilge depth. Do not “simplify” back to internal ADC.

**Usable depth band:** Hardware C-V converter non-linearity + span clipping ≈ reliable ~5–70 cm after bias; software over-range (~110 cm) is treated as electrical fault. Thresholds are typically 5–20+ cm — keep defaults/ranges sensible for bilge use, not the full 1 m probe span.

**Power domain:** Boat 12 V → buck → 5 V ESP32 / ADS1115 HV; ESP32 logic is 3.3 V. I2C must stay level-shifted.

## Testing expectations

- Pure logic that can live without Arduino belongs in headers (pattern: `StateMachine.h`) so `pio test -e native` covers it.
- Nearly every `src/*.cpp` is wrapped in `#ifndef UNIT_TESTING` because `test_build_src = yes` compiles all of `src/` for native suites. Removing a guard breaks the whole native build with a misleading missing-Arduino error — keep the guards.
- `WaterPressureSensor.cpp` documents this as a load-bearing guard; treat similar exclusions as intentional.
- Add/extend tests under `test/test_state_machine/`, `test/test_sensor/`, `test/test_notifications/` when changing those behaviors. Prefer native tests over requiring hardware.
- Web UI: edit `dev-ui/*.html`, verify with the mock server; firmware builds regenerate `compressed_pages.h` automatically. Do not hand-edit the generated header. Mock coverage may lag newer REST routes — check `dev-ui/README.md` before assuming an endpoint is mocked.

## Change playbooks (common agent tasks)

**Adjust thresholds / state behavior**
1. Edit pure logic + constants in `include/StateMachine.h`.
2. Update native tests in `test/test_state_machine/`.
3. If persisted settings change, update `SettingsStore` / `AlarmSettings` and any ConfigServer validators + `docs/` if user-visible.

**Add or change a REST endpoint**
1. Implement in `ConfigServer.*`.
2. Update the matching page in `dev-ui/` (and mock-server.js if useful for UI work).
3. Update [`docs/api-reference.md`](docs/api-reference.md).

**GPIO / peripheral change**
1. `BoardPins.h` only for numbers.
2. Sync `UNUSED_GPIOS` allowlist in `main.cpp`.
3. Confirm level-shifter / power assumptions still hold; update hardware docs if wiring changes.

**OTA / release-affecting change**
1. Bump version in `Version.h` + `platformio.ini`.
2. Respect flood-abort, RSSI preflight (−70 dBm), and rollback paths in `OTAManager`.
3. Follow [`OTA_QUICKSTART.md`](OTA_QUICKSTART.md) for release assets (`firmware.bin` on GitHub Releases). Forks must point OTA owner/repo away from upstream before deploy.

**Notification channel change**
1. Keep work off the main loop; enqueue through `NotificationWorker`.
2. Honor portal fail-fast via `HttpPoster` / `WiFiManager` link state.
3. Preserve emergency latest-wins semantics.

## Style & process notes

- Commits: [Conventional Commits](https://www.conventionalcommits.org/) (`fix(wifi): …`, `feat(ota): …`, `docs: …`).
- Prefer small, focused diffs. Do not drive-by refactor unrelated modules.
- User-visible behavior changes need a docs update under `docs/` (or root guides) in the same change.
- ArduinoJson **v6** and PubSubClient are pinned via `platformio.ini` — do not silently major-bump without migration.
- Prefer existing patterns: NVS namespaces per subsystem, in-RAM settings cache (`SettingsStore::get()`), side-effect flags from `updateStateMachine()` executed in `loop()`.

## When unsure

1. Search `include/` + `src/` for the subsystem table above before inventing a new module.
2. Read [`docs/architecture.md`](docs/architecture.md) for task boundaries and data flow.
3. Run `pio test -e native` after logic changes.
4. For field behavior (LED, button, alerts), cross-check [`docs/usage.md`](docs/usage.md) so code and operator docs stay aligned.
