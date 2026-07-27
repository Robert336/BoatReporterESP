# BoatReporterESP Documentation

This folder holds the deep documentation for BoatReporterESP. The [main README](../README.md) is a short landing page; everything beyond the quick start lives here.

## Guides

| Doc | What's inside |
|-----|---------------|
| [Configuration](configuration.md) | First-time setup, WiFi (including captive-portal detection and custom MAC override), Twilio/Discord/custom-HTTP credentials, two-point sensor calibration, emergency thresholds, MQTT broker setup |
| [Usage](usage.md) | LED status patterns, system states, button functions, alert behavior |
| [MQTT & Telemetry](mqtt-telemetry.md) | Log and structured telemetry topics, the 13-field JSON payload, Home Assistant / Grafana ingestion |
| [Troubleshooting](troubleshooting.md) | WiFi, sensor, SMS/Discord, LED, web-interface, and sensor-error fixes |
| [Hardware & Assembly](hardware.md) | Full parts list with links, wiring, and step-by-step assembly instructions |
| [Architecture](architecture.md) | Component layout, FreeRTOS task design, state machine, data flow |
| [API Reference](api-reference.md) | All 39 ConfigServer REST endpoints with request/response schemas and curl examples |

## Related project docs (repo root)

- [`server-stack/README.md`](../server-stack/README.md): the self-hosted Grafana monitoring stack (Mosquitto → Telegraf → InfluxDB → Grafana), including WAN/TLS deployment
- [`server-stack/DEPLOYMENT.md`](../server-stack/DEPLOYMENT.md): live runbook for the broker (DDNS, certs, ACLs, debugging connect failures)
- [`dev-ui/README.md`](../dev-ui/README.md): the standalone mock server for developing the web UI without flashing the ESP32
- [`OTA_QUICKSTART.md`](../OTA_QUICKSTART.md): remote firmware update walkthrough
- [`test/TESTING_README.md`](../test/TESTING_README.md): the unit-test suite

## Screenshots

[`screenshots/`](screenshots/README.md): where to place the config-server and Grafana captures referenced from the READMEs, plus a capture checklist.
