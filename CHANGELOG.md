# Changelog

All notable changes to BoatReporterESP are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Firmware versions are tagged `vX.Y.Z` on the release commit; deployed devices
pick up a new version when a GitHub Release with a `firmware.bin` asset is
published (OTA checks the repo configured in the OTA settings page, defaulting
to `Robert336/BoatReporterESP`).

## [Unreleased]

### Added
- Captive portal (marina WiFi) support: the device probes network reachability after associating and every 2 minutes, detects hijacked connections, and shows a "sign-in needed" banner in the WiFi UI. A time-bounded assist mode relays the marina's splash page through the device (`/portal/assist`, `/portal/relay`) so the owner completes the sign-in from their phone and the marina whitelists the ESP32. Relayed pages get a branded status bar that auto-redirects to a confirmation page once the portal opens.
- Open (passwordless) networks can now be saved from the WiFi config page via an "Open network" checkbox.
- `portalState` and `portalLoginUrl` fields in `GET /status` and `GET /init`.

### Changed
- Notification channels fail fast while a captive portal is confirmed instead of burning their 10 s timeouts against the hijacked connection; sends resume automatically once the portal opens.
- WiFi passwords are no longer logged in plaintext when saving credentials.

## [1.1.8] - 2026-07-19

### Fixed
- **Fail safe on sensor fault in EMERGENCY**: a dead sensor mid-flood no longer latches the device in EMERGENCY off a stale reading. A sustained fault (≥60 s) now transitions EMERGENCY → ERROR; a transient glitch does not bounce an active flood. The horn and ALERT pin fail safe to OFF while the reading is untrustworthy, and ERROR → CONFIG is suppressed until the sensor recovers (prevents state flapping).
- Check the largest contiguous heap block before starting an OTA download (reject early instead of failing mid-install).
- Store WiFi credentials in fixed arrays instead of `String` (heap stability).
- Fix the EMERGENCY-entry log and simplify the settings refresh path.

### Changed
- All ConfigServer JSON responses now go through a shared `JsonResponder` helper.
- GPIO definitions centralized in `include/BoardPins.h` (single source of truth).
- `AlarmSettings` struct shared between the state machine and `SettingsStore`.
- SMS/Discord `send()` paths use stack buffers instead of `malloc`.
- Shared NVS config cache extracted into `NvsChannelBase`.

## [1.1.7] - 2026-07-18

### Added
- Diagnostics fields in MQTT telemetry (chip temp, thresholds, firmware version, last OTA check, heap, uptime).

### Fixed
- Harden OTA check timing.
- Consume config button presses while in CONFIG state so they don't queue up.

## [1.1.6] - 2026-07-17

### Fixed
- Config mode staying active after the web server timeout.

## [1.1.5] - 2026-07-16

### Fixed
- Config AP left up after CONFIG exit.
- Emergency exit from CONFIG and first-boot emergency notification.

## [1.1.4] - 2026-07-15

### Fixed
- Watchdog reboot when the config portal times out.

## [1.1.3] - 2026-07-14

### Changed
- OTA download/install now runs on the Core 0 check task.

## [1.1.2] - 2026-07-13

### Fixed
- Broken `/ota/status` JSON from an invalid `String` overload.
- OTA page fetch failure and settings value mismatch.
- OTA page status population; added an update banner.

## [1.1.1] - 2026-07-12

### Added
- Redesigned firmware (OTA) page, wired into the Settings hub.

## [1.1.0] - 2026-07-11

### Fixed
- WiFi reconnect: reason-based escalation and rescan fallback; feed the task watchdog during `connectToBestNetwork`.
- Use a 64-bit monotonic timer for session-duration logging.
- SNTP re-sync now actually restarts the client.
- Strict emergency notifier priority via task notification.
- Reset the I2C bus-recovery counter on a successful read.
- Abort an OTA download if a flood condition begins mid-download.

### Changed
- Removed horn references from the GUI and user-facing surfaces.
- Automated Let's Encrypt renewal with a hash-gated service restart (server-stack).

## [1.0.1] - 2026-07-10

### Added
- `server-stack/`: self-hosted Grafana telemetry pipeline (Mosquitto → Telegraf → InfluxDB → Grafana) with WAN/TLS broker support.
- Optional TLS for MQTT broker connections (certificate validated against bundled Let's Encrypt roots).
- Structured sensor telemetry published to the MQTT `/telemetry` topic (retained).
- Lean `GET /notifications/status` endpoint for status-pill polling.
- MQTT connection-status pill on the notifications page.

### Fixed
- MQTT JSON `NaN` serialization issue.
- Prevent MQTT credential wipe on unrelated config saves.
- Apply the MQTT default broker when the NVS namespace is absent.

### Changed
- MQTT broker config now persists via NVS instead of being hardcoded.

## [1.0.0] - 2026-07-09

### Added
- Initial release: two-tier emergency alerts (SMS/Discord), OTA firmware updates via GitHub Releases, MQTT logging, `NotificationWorker` task, I2C auto-recovery, rate-of-change tracking, task watchdog.

[Unreleased]: https://github.com/Robert336/BoatReporterESP/compare/v1.1.8...HEAD
[1.1.8]: https://github.com/Robert336/BoatReporterESP/compare/v1.1.7...v1.1.8
[1.1.7]: https://github.com/Robert336/BoatReporterESP/compare/v1.1.6...v1.1.7
[1.1.6]: https://github.com/Robert336/BoatReporterESP/compare/v1.1.5...v1.1.6
[1.1.5]: https://github.com/Robert336/BoatReporterESP/compare/v1.1.4...v1.1.5
[1.1.4]: https://github.com/Robert336/BoatReporterESP/compare/v1.1.3...v1.1.4
[1.1.3]: https://github.com/Robert336/BoatReporterESP/compare/v1.1.2...v1.1.3
[1.1.2]: https://github.com/Robert336/BoatReporterESP/compare/v1.1.1...v1.1.2
[1.1.1]: https://github.com/Robert336/BoatReporterESP/compare/v1.1.0...v1.1.1
[1.1.0]: https://github.com/Robert336/BoatReporterESP/compare/v1.0.1...v1.1.0
[1.0.1]: https://github.com/Robert336/BoatReporterESP/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/Robert336/BoatReporterESP/releases/tag/v1.0.0
