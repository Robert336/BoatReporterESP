# Contributing to BoatReporterESP

Thanks for your interest in contributing. BoatReporterESP is an ESP32-based bilge-water monitor. This guide covers the basics of getting a change landed.

## Project layout

```
src/                 Firmware source (Arduino + FreeRTOS)
include/             Headers: BoardPins.h is the single source of truth for the GPIO map
dev-ui/              Standalone mock server for developing the web UI without flashing
server-stack/        Self-hosted Grafana monitoring stack (Mosquitto/Telegraf/InfluxDB/Grafana)
docs/                Guides (config, architecture, hardware, API, …)
test/                Native unit tests (run on the host, not the ESP32)
test-logs/           Soak / hardware validation analyses
scripts/             Build scripts: compress_html.py gzips the web UI into src/compressed_pages.h
```

## Setting up a build

1. Install [PlatformIO](https://platformio.org/) (VS Code extension or CLI).
2. Clone the repo. **No `secrets.h` is required**: all credentials (Twilio, Discord, custom-HTTP, MQTT) are entered at runtime in the web UI and stored in NVS. `include/secrets.h.example` is an empty template. See [`SECURITY.md`](SECURITY.md).
3. Build the environment appropriate to your task:

   ```bash
   pio run -e prod --target upload    # production build (on the boat)
   pio run -e dev  --target upload    # development build (all logs)
   pio run -e mock --target upload    # full firmware with a simulated sensor (no hardware)
   pio run -e native                  # host-side unit tests
   ```

## Web UI changes

Edit the HTML in `dev-ui/*.html` (or `src/html/ota.html`). You can iterate against the mock server without flashing:

```bash
cd dev-ui && npm install && npm start   # http://localhost:3000
```

When any PlatformIO build runs, `scripts/compress_html.py` automatically gzips the pages into `src/compressed_pages.h`. No manual HTML embedding is required. See [`dev-ui/README.md`](dev-ui/README.md) for the mock server's endpoint coverage (a few newer routes are not yet mocked).

## Tests

The state machine and supporting logic are extracted into `include/StateMachine.h` and covered by native unit tests in `test/`. Run them with:

```bash
pio run -e native -t test
```

See [`test/TESTING_README.md`](test/TESTING_README.md) for the test philosophy and how to run without a local GCC toolchain. Long-running soak / hardware campaign write-ups live under [`test-logs/`](test-logs/README.md).

## Documentation

Edit the **canonical** page for a topic and link from elsewhere — do not copy long procedures into multiple files.

| Topic | Canonical doc |
|-------|----------------|
| Design rationale | [`docs/architecture.md`](docs/architecture.md#design-decisions) |
| Install / WiFi / credentials / calibration | [`docs/configuration.md`](docs/configuration.md) |
| Wiring / GPIO / assembly | [`docs/hardware.md`](docs/hardware.md) |
| Field triage | [`docs/troubleshooting.md`](docs/troubleshooting.md) |
| Owner day-to-day | [`USER_GUIDE.md`](USER_GUIDE.md) |
| Engineer LED/state reference | [`docs/usage.md`](docs/usage.md) |
| Broker ops | [`server-stack/DEPLOYMENT.md`](server-stack/DEPLOYMENT.md) |
| Secrets / exposure | [`SECURITY.md`](SECURITY.md) |

## Submitting changes

1. Fork the repository and create a branch from `main`.
2. Make your change. Keep commits focused: the project uses [Conventional Commits](https://www.conventionalcommits.org/) style messages (e.g. `fix(wifi): …`, `feat(ota): …`, `docs: …`).
3. If you add or change user-visible behavior, update the relevant **canonical** doc under [`docs/`](docs/) (or `USER_GUIDE.md` / `SECURITY.md` as appropriate).
4. Bump `FIRMWARE_VERSION` in [`include/Version.h`](include/Version.h) and [`platformio.ini`](platformio.ini) if your change ships to devices; the two must match. When you tag a release, also update version pins in the README badge and any doc footers that cite the firmware version (they track the **last tagged** release, not `[Unreleased]`).
5. Open a pull request against `main` and fill in the PR template.

## Firmware versioning

Versions follow `MAJOR.MINOR.PATCH` and are tagged `vX.Y.Z` on the release commit. OTA updates are checked against GitHub Releases, so a tagged release with a `firmware.bin` asset is what ships to deployed devices. See [`OTA_QUICKSTART.md`](OTA_QUICKSTART.md).

## Forks and OTA

The OTA system defaults to checking `Robert336/BoatReporterESP`. If you fork, change the GitHub owner/repo on the OTA settings page (or in `src/OTAManager.cpp` NVS defaults) **before** first deployment, or your devices will pull firmware from the upstream repo.

## Questions

Open an [issue](https://github.com/Robert336/BoatReporterESP/issues) for bugs, feature requests, or questions. For security-sensitive reports, see [`SECURITY.md`](SECURITY.md).
