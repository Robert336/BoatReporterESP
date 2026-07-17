# Live Deployment & Runbook

Operational notes for the **actual running** BoatReporter broker, complementing
the generic setup guide in [README.md](README.md). The README is a reusable
template (`mqtt.example.com`, a Pi); this file records how the real instance is
wired and how to debug it when a device can't connect.

## Topology

The stack is **self-hosted in Docker on the dev workstation** (not a cloud
broker). A remote ESP on marina/home WiFi reaches it over the public internet:

```
ESP32 (remote WiFi) ──TLS:8883──▶ Cloudflare DNS ──▶ home router (DMZ) ──▶ host:8883 ──▶ docker-proxy ──▶ mosquitto container
   default broker:                resolves to the          basic DMZ →        published        DNAT to
   mqtt.bilgerise.garageforge.ca  current public IP        the LAN host       0.0.0.0:8883     172.x:8883
```

Key facts:

- **Host:** the broker container's ports `1883` (LAN) and `8883` (WAN TLS) are
  published with `docker-proxy`, which inserts an iptables DNAT. **This bypasses
  `ufw`** — broker reachability is *not* gated by ufw rules. (ufw still protects
  everything else on the host.)
- **Edge:** exposed through the router's **basic DMZ** pointed at the host's LAN
  IP. Basic DMZ keeps the host on its private address; prefer it over "Advanced
  DMZ", which assigns the host the public IP directly and over-exposes the box.
  Forward/expose **only 8883** — never 1883.
- **Public IP is dynamic** (residential ISP). It changes on ISP/lease/location
  events — this is why DDNS is mandatory (see below). A stale A record pointing
  at a previous IP is the most common cause of fleet-wide connect failures.
- **DNS:** Cloudflare, zone `garageforge.ca`. The broker record **must be
  DNS-only (grey-cloud)** — Cloudflare's proxy only handles HTTP(S) and breaks
  MQTT/TLS on 8883.
- **TLS:** Let's Encrypt cert whose CN matches the broker hostname. The firmware
  validates it against the bundled ISRG roots in `include/MqttRootCA.h`. No
  per-device cert is needed; the CA is compiled into the firmware. Auth is
  username/password, one user per device (no mTLS).

## Dynamic DNS (compose service)

The host's public IP is dynamic, so the Cloudflare A record must be kept current.
This runs as the **`cloudflare-ddns` service in
[docker-compose.yml](docker-compose.yml)** — a tiny container
([`ddns/Dockerfile`](ddns/Dockerfile)) that runs the bundled
[`ddns/cloudflare-ddns.sh`](ddns/cloudflare-ddns.sh) every `DDNS_INTERVAL`
seconds (default 300). Keeping it in compose makes the whole stack one source of
truth — no host cron or systemd timer, nothing in `/etc`, and it reproduces on
any host with `docker compose up -d`.

Required in `.env`:

```ini
CF_API_TOKEN=…                          # Cloudflare token, Zone:DNS:Edit on the zone
CF_ZONE=garageforge.ca                  # the zone (root domain)
CF_RECORD=mqtt.bilgerise.garageforge.ca # the full A record to keep updated
DDNS_INTERVAL=300                       # optional, seconds between checks
```

Bring it up / watch it:

```bash
docker compose up -d --build cloudflare-ddns   # build image + start
docker compose logs -f cloudflare-ddns         # "updated ... -> <ip>" / "already <ip>"
```

The record MUST stay **DNS-only (grey-cloud)** — the script enforces
`proxied:false`, but don't flip it on in the dashboard either.

## Troubleshooting: device can't reach the broker

> **`[E][WiFiClientSecure.cpp] connect(): start_ssl_client: -1` is a TCP-connect
> failure, NOT a TLS/cert error.** A `-1` means the socket never connected, so
> the handshake never started — the cert, CA bundle, and clock are irrelevant.
> (Real TLS/cert failures return negative mbedTLS codes like `-0x2700`, not -1.)
> So a `-1` points at DNS / DMZ / port-forward / public-IP, not certificates.

Work down this list — each step isolates one layer:

```bash
# 1. DNS resolves to the host's CURRENT public IP?
getent hosts mqtt.bilgerise.garageforge.ca      # what the fleet will reach
curl -s https://api.ipify.org                    # the host's actual public IP
#    Mismatch → DDNS isn't running. Check the timer; run the script by hand.

# 2. Is the broker healthy on the host itself? (proves cert + listener)
openssl s_client -connect 127.0.0.1:8883 -servername mqtt.bilgerise.garageforge.ca </dev/null \
  | openssl x509 -noout -subject -dates
#    Should show the Let's Encrypt cert, correct CN, in-date.
#    EXPIRED? → The renewal automation failed. Run ./scripts/renew-cert.sh manually.
#    WRONG CN? → You changed MQTT_DOMAIN in .env but didn't re-issue the cert.

# 3. Is the port published/forwarded inside the host?
ss -tlnp | grep 8883                              # docker-proxy on 0.0.0.0:8883
```

```bash
# 4. External reachability — MUST be run from OFF the host's network
#    (phone on cellular, etc.). Testing from the host or a device using this
#    network as a Tailscale exit node HAIRPINS and gives a false "unreachable".
mosquitto_pub -h mqtt.bilgerise.garageforge.ca -p 8883 --capath /etc/ssl/certs \
  -u <boat-user> -P '<pass>' -t boatreporter/test -m ping -d
#    CONNACK (0) → fully working.  Hangs → DMZ/forward not pointing at the host.
```

⚠️ **NTP-dependent TLS bootstrap.** After a cold boot the ESP32 clock is ~1970.
TLS cert validation will reject the broker cert as "not yet valid" until NTP
syncs. The firmware's exponential backoff eventually succeeds once the clock is
correct, but **telemetry is silently dropped for the first 1–3 minutes** after
boot. On a device that's power-cycled frequently (e.g. solar/battery setups),
consider whether you need an RTC module or a battery-backed NVRAM clock.

---

## Certificate renewal automation (CRITICAL)

Let's Encrypt certificates expire every **90 days**. The fleet will lose trust
in the broker the moment the leaf cert expires. You must automate renewal.

### One-time setup

```bash
# Run once after you've issued the first certificate:
cd server-stack
sudo ./scripts/setup-cert-renewal.sh
```

This installs a **systemd timer** (`boat-cert-renewal.timer`) that runs
`scripts/renew-cert.sh` every Sunday at 03:00. The script only restarts
Mosquitto when the certificate has actually changed, so devices are not kicked
off the broker on every check.

### Verify it is scheduled

```bash
systemctl status boat-cert-renewal.timer
# Next run column should show a future date.

# Force a dry-run check:
sudo systemctl start boat-cert-renewal.service
journalctl -u boat-cert-renewal.service --no-pager
```

### Why not just cron?

The timer is self-documenting (`systemctl cat boat-cert-renewal.timer`), runs
with the correct `$PWD`, and persists across reboots without needing a cron
service to be enabled. If you prefer cron, run `renew-cert.sh` from a weekly
crontab entry instead — the script is daemon-agnostic.

### Manual emergency renewal

If the certificate has already expired and devices are offline:

```bash
cd server-stack
./scripts/renew-cert.sh          # forces issuance + restart
docker compose restart mosquitto  # belt-and-suspenders
```
