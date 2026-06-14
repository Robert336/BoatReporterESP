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

## Dynamic DNS via systemd timer

`ddns/cloudflare-ddns.sh` updates the Cloudflare A record to the host's current
public IP (reading `CF_API_TOKEN` / `CF_ZONE` / `CF_RECORD` from `.env`). The
README shows a cron example; the live host instead uses a **systemd timer**
(unit files in [`ddns/systemd/`](ddns/systemd/)):

```bash
# Adjust ExecStart path / User in the .service first, then:
sudo cp ddns/systemd/cloudflare-ddns.{service,timer} /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now cloudflare-ddns.timer

# Verify:
systemctl list-timers cloudflare-ddns.timer      # next/last run
journalctl -u cloudflare-ddns.service -n 5       # "updated ... -> <ip>" / "already <ip>"
```

> These unit files live **outside the repo** once installed (`/etc/systemd/`),
> so they are not captured by a repo backup — reinstall them after a reimage.

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

Note: ESP TLS validation needs a synced clock, so the **first connect(s) after a
cold boot fail** (clock ~1970 → cert "not yet valid") until NTP lands, then
succeed on retry/backoff. Brief post-boot `-1`s that clear on their own are
expected, not a misconfiguration.
