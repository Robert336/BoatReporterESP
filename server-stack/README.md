# BoatReporter Server Stack

A self-contained telemetry stack for [BoatReporterESP](../README.md), designed to
run in Docker on a Raspberry Pi. The Pi hosts everything:

```
ESP32 ──MQTT──▶ Mosquitto ──▶ Telegraf ──▶ InfluxDB ──▶ Grafana
 (publishes      (broker)      (parses      (stores      (dashboards)
  telemetry)                    JSON)        series)
```

| Service | Image | Port | Role |
|---------|-------|------|------|
| Mosquitto | `eclipse-mosquitto:2` | 1883 | MQTT broker the device connects to |
| InfluxDB | `influxdb:2.7` | 8086 | time-series storage |
| Telegraf | `telegraf:1.32` | – | subscribes to MQTT, writes to InfluxDB |
| Grafana | `grafana/grafana-oss:11.4.0` | 3000 | dashboards |

## Requirements

- A **64-bit** Raspberry Pi OS (Pi 3/4/5, 2 GB+ RAM). InfluxDB 2.x has no 32-bit
  build — `uname -m` should report `aarch64`.
- Docker + the Compose plugin: `curl -fsSL https://get.docker.com | sh`
- **Recommended:** put Docker's data on an external SSD/USB rather than the SD
  card. InfluxDB writes continuously and will wear out an SD card over time.

## Setup

```bash
cd server-stack
cp .env.example .env
nano .env                       # set strong passwords + a random INFLUX_TOKEN
                                #   openssl rand -hex 32   → use for INFLUX_TOKEN
docker compose up -d
```

First launch initializes InfluxDB (org/bucket/token from `.env`) and auto-loads
the Grafana datasource + the **Boat Reporter — Bilge Monitor** dashboard.

> For how the live broker is actually deployed (Docker on the host, router DMZ,
> dynamic IP, systemd DDNS) and a runbook for when a device can't connect, see
> [DEPLOYMENT.md](DEPLOYMENT.md).

- **Grafana:** `http://<pi-ip>:3000` — log in with `GRAFANA_ADMIN_USER` / `GRAFANA_ADMIN_PASSWORD`.
- **InfluxDB UI** (optional/debug): `http://<pi-ip>:8086`.

## Point the device at the Pi

In the device's web UI → **Notifications → MQTT broker**, set the broker host to
the **Pi's LAN IP** and port `1883`. (Settings persist in NVS; the firmware
default `192.168.2.41` only applies to a device that has never been configured —
see the note in the [main README](../README.md#mqtt-broker-configuration).)

## Verify data is flowing

```bash
# Watch raw telemetry hit the broker (run on the Pi, or any LAN host):
docker exec -it boat-mosquitto mosquitto_sub -t 'boat/+/telemetry' -v

# Confirm Telegraf is writing (no errors = good):
docker compose logs -f telegraf
```

Then open Grafana — the dashboard's **Device** dropdown will list each unit by
its MAC-derived id, and panels populate within a minute or two. For a quick
end-to-end test without hardware, flash the `mock` firmware build (`pio run -e
mock -t upload`) — it publishes simulated readings on the same topics.

## WAN / TLS deployment

The default config is an open, anonymous broker — fine on a trusted LAN, but
**never expose port 1883 to the internet**. To let a boat on marina WiFi reach
the broker, use a domain name pointed at your home IP, port-forward the **TLS**
port only, and lock the broker down with certificates + auth + ACLs.

```
ESP32 (marina WiFi) ──TLS:8883──▶ home router (port-forward) ──▶ Pi : mosquitto
        resolves mqtt.example.com  ▲
                                   └─ Dynamic DNS keeps the A record current
```

### 1. Domain + Dynamic DNS

Point a DNS-only (**grey-cloud**, never proxied — Cloudflare's proxy only does
HTTP and would break MQTT) A record at your home IP, and keep it current as your
ISP rotates the IP:

```bash
# Edit ddns/cloudflare-ddns.sh header for the env vars, then cron it every 5 min:
*/5 * * * * CF_API_TOKEN=… CF_ZONE=example.com CF_RECORD=mqtt.example.com \
            /path/to/server-stack/ddns/cloudflare-ddns.sh >> /var/log/ddns.log 2>&1
```

On a systemd host, use the timer units in [`ddns/systemd/`](ddns/systemd/)
instead of cron — see [DEPLOYMENT.md](DEPLOYMENT.md#dynamic-dns-via-systemd-timer).
(DuckDNS works equally well if you'd rather not manage a domain.)

### 2. TLS certificate (Let's Encrypt, DNS-01)

We use the **DNS-01** challenge because only 8883 is open — there's no inbound
80/443 for an HTTP challenge. With Cloudflare DNS:

```bash
cp secrets/cloudflare.ini.example secrets/cloudflare.ini   # add your API token
chmod 600 secrets/cloudflare.ini
echo "MQTT_DOMAIN=mqtt.example.com" >> .env                # your hostname
./scripts/issue-cert.sh                                    # writes certs/{fullchain,privkey}.pem
```

Let's Encrypt certs expire every 90 days — re-run `issue-cert.sh` from cron
(e.g. weekly) and `docker compose restart mosquitto` afterwards to reload.

### 3. Broker auth + ACLs

```bash
# Create the ingest user (must match MQTT_USERNAME in .env) and one user per device:
./scripts/mosquitto-passwd.sh telegraf
./scripts/mosquitto-passwd.sh boat-aabbcc      # use the device's MAC (boat/<mac>)
```

Set `MQTT_USERNAME` / `MQTT_PASSWORD` in `.env` to the **telegraf** credentials,
and edit `mosquitto/config/acl` so each `boat-<mac>` user maps to its own
`boat/<mac>/#` subtree (a template is already in the file).

### 4. Switch on the hardened config

```bash
cd mosquitto/config
cp mosquitto.conf mosquitto.conf.lan.bak
cp mosquitto.conf.wan mosquitto.conf
cd ../..
docker compose up -d        # recreate so 8883 + new env take effect
docker compose restart mosquitto
```

Forward **8883 → Pi:8883** on your router (and only 8883).

### 5. Configure the device

In the device UI → **Notifications → MQTT broker**:

| Field | Value |
|-------|-------|
| Broker host | `mqtt.example.com` (the **domain**, not an IP — it's verified against the cert) |
| Port | `8883` |
| Use TLS encryption | ✅ |
| Username / Password | the `boat-<mac>` credentials you created |

The firmware validates the broker cert against its bundled Let's Encrypt roots
(`include/MqttRootCA.h`). If your broker ever uses a non-Let's-Encrypt CA,
update that bundle and reflash.

> **Security checklist:** only 8883 forwarded · `allow_anonymous false` · every
> device has its own credentials + ACL · strong `INFLUX_TOKEN` and Grafana
> password · consider `fail2ban`/rate-limiting on the Pi for an internet-facing
> service.

## Dashboard panels

| Panel | Source field | Notes |
|-------|--------------|-------|
| Water Level | `level_cm` | threshold lines at 30 cm (Tier 1) and 50 cm (Tier 2) |
| Current Level | `level_cm` | latest value, color-coded against the thresholds |
| Rate of Change | `rate_cm_30min` | the trend used in the device's alerts |
| WiFi Signal | `rssi` | link health (dBm) |
| System State | `state` | NORMAL / CONFIG / ERROR / EMERGENCY timeline |
| Connectivity | `boat_availability` | online / offline from the retained LWT topic |

## Notes & customization

- **Retention:** `INFLUX_RETENTION` in `.env` (default `90d`) controls how long
  raw data is kept. Set `0s` to keep forever.
- **Bucket name:** the dashboard's Flux queries hardcode the bucket `boat`. If
  you change `INFLUX_BUCKET`, update the queries in
  `grafana/dashboards/boat-reporter.json` (or edit them in the Grafana UI).
- **Broker authentication / internet exposure:** anonymous by default (fine on
  a trusted LAN). To require credentials and TLS for WAN access, see
  [WAN / TLS deployment](#wan--tls-deployment) above.
- **Multiple boats:** the `boat/+/telemetry` wildcard ingests every device on
  the broker automatically; each is tagged `device=<mac>` and selectable in the
  dashboard dropdown.

## Common commands

```bash
docker compose up -d            # start / apply changes
docker compose logs -f          # tail all services
docker compose restart telegraf # reload telegraf after editing its config
docker compose down             # stop (volumes/data preserved)
docker compose down -v          # stop AND delete all data (fresh start)
```
