# Security

Practical secrets and exposure notes for BoatReporterESP. This is not a formal threat model; it documents how credentials are stored and what operators should avoid.

## Reporting a vulnerability

Prefer a private channel over a public GitHub issue when the report could expose credentials, broker access, or a remote exploit path.

- Open a private [GitHub security advisory](https://github.com/Robert336/BoatReporterESP/security/advisories/new) (preferred), or
- Contact **[@Robert336](https://github.com/Robert336)** directly.

Do not paste live tokens, passwords, phone numbers, or webhook URLs into public issues.

## Firmware credentials (NVS)

- **No compile-time secrets are required.** Twilio, Discord, custom HTTP, MQTT, and WiFi credentials are entered at runtime in the captive-portal web UI and stored in NVS.
- `include/secrets.h.example` is an empty template only; do not commit real secrets.
- Notification credential fields in the UI are write-only once saved (the device will not echo them back).
- NVS survives OTA updates; wiping credentials requires clearing NVS or overwriting values through the UI.

## Config access point (plain HTTP)

- In CONFIG mode the device serves `http://192.168.4.1` over its own AP (`ESP32-BilgeRise-Setup`). There is **no TLS** on that link — the ESP32 has no certificate for the AP.
- This is intentional: configuration is expected to happen with physical proximity to the boat. A hostile actor would need to be nearby and associated with the AP (which has a per-device password printed to serial / on the enclosure sticker).
- Avoid entering production credentials while connected to the setup AP in a crowded RF environment if that risk matters for your deployment; prefer configuring over a trusted STA network after the device has joined WiFi, when your workflow allows.

## Server stack and broker

- Never commit `server-stack/.env` (Cloudflare tokens, MQTT passwords, Grafana secrets).
- Expose **TLS port 8883 only** to the WAN. Do not publish Mosquitto **1883** to the internet.
- Prefer DNS-only (grey-cloud) DNS for MQTT; HTTP proxies break MQTT/TLS.
- Operational hardening and connect debugging: [server-stack/DEPLOYMENT.md](server-stack/DEPLOYMENT.md).

## OTA and GitHub

- Devices pull `firmware.bin` from the configured GitHub Releases repo (default `Robert336/BoatReporterESP`).
- Forks must change the OTA owner/repo **before** first internet connect, or devices will track upstream releases — see [OTA_QUICKSTART.md](OTA_QUICKSTART.md) and [CONTRIBUTING.md](CONTRIBUTING.md).

## Logging

- WiFi passwords must not be logged in plaintext (current firmware avoids this on save).
- Prefer redacting tokens when pasting serial or MQTT logs into issues.
