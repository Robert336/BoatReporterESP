# BoatReporterESP Documentation

Deep documentation for BoatReporterESP. The [main README](../README.md) is the landing page with an overview, design decisions, testing summary, and quick start. Everything beyond that lives here or in the related root docs below.

## Who should read what

| If you are… | Start here |
|-------------|------------|
| New to the project | [README](../README.md) → [Architecture — Design Decisions](architecture.md#design-decisions) → [Validation campaigns](../test-logs/README.md) |
| Installing / configuring a device | [Configuration](configuration.md), [Hardware](hardware.md), [User Guide](../USER_GUIDE.md) |
| Debugging in the field | [Troubleshooting](troubleshooting.md) |
| Changing firmware | [Architecture](architecture.md), [API Reference](api-reference.md), [CONTRIBUTING](../CONTRIBUTING.md) |
| Running the broker / Grafana | [server-stack/README.md](../server-stack/README.md), [DEPLOYMENT.md](../server-stack/DEPLOYMENT.md) |

## Guides

| Doc | What's inside |
|-----|---------------|
| [Architecture](architecture.md) | Design Decisions, component layout, FreeRTOS tasks, state machine, data flow |
| [Configuration](configuration.md) | First-time setup, WiFi (captive-portal detection and custom MAC), Twilio/Discord/HTTP credentials, two-point calibration, thresholds, MQTT |
| [Usage](usage.md) | Technical **two-LED** patterns (status + alert), system states, button functions, alert behavior |
| [MQTT & Telemetry](mqtt-telemetry.md) | Log and structured telemetry topics, 13-field JSON payload, Home Assistant / Grafana |
| [Troubleshooting](troubleshooting.md) | Symptom → signal → fix triage (WiFi, portal, sensor, alerts, MQTT) |
| [Hardware & Assembly](hardware.md) | Parts list, wiring diagram, GPIO map (`BoardPins.h`), assembly |
| [API Reference](api-reference.md) | All 39 ConfigServer REST endpoints with schemas and curl examples |

## Related project docs (repo root)

- [`USER_GUIDE.md`](../USER_GUIDE.md): owner / installer day-to-day operation and owner handout page
- [`SECURITY.md`](../SECURITY.md): secrets posture, config AP, broker exposure, vulnerability reporting
- [`server-stack/README.md`](../server-stack/README.md): self-hosted Grafana stack (Mosquitto → Telegraf → InfluxDB → Grafana)
- [`server-stack/DEPLOYMENT.md`](../server-stack/DEPLOYMENT.md): live broker runbook (DDNS, certs, ACLs, connect failures)
- [`dev-ui/README.md`](../dev-ui/README.md): mock server for web UI without flashing
- [`OTA_QUICKSTART.md`](../OTA_QUICKSTART.md): remote firmware update walkthrough
- [`test/TESTING_README.md`](../test/TESTING_README.md): unit-test suite how-to
- [`test-logs/README.md`](../test-logs/README.md): soak / hardware validation analyses

## Screenshots

[`screenshots/`](screenshots/README.md): config-server and Grafana image assets referenced from the READMEs.

## Version pins

Docs that mention a firmware version pin the **last tagged release** (currently **1.1.8**). Behavior already described under `[Unreleased]` in [`CHANGELOG.md`](../CHANGELOG.md) is not back-dated into those footers until it ships.
