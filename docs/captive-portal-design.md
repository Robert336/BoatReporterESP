# Captive Portal Handling — Problem Analysis & Proposed Architecture

## Problem statement

Marinas frequently run **captive-portal WiFi** (dock WiFi, "MarinaGuest", etc.).
These networks exhibit two distinct shapes:

| Shape | Example | ESP32 impact today |
|-------|---------|--------------------|
| **Open AP + web portal** | "MarinaGuest" — no WPA password; any client associates, but all HTTP/DNS is intercepted until a splash page is accepted | Our `wifi-config.html` **requires a password** (`required` on the input and `if(!s||!p)` check), so the user literally cannot save the network. Even if they could, `WiFi.begin(ssid, "")` works, but the device would associate and then every outbound POST (Twilio, Discord, MQTT, NTP) would be hijacked by the portal. |
| **WPA-secured AP + web portal** | "MarinaWiFi" — WPA2 password *and* a login/accept page | Credentials save fine and association succeeds, but the device still has no internet until the portal is acknowledged. All outbound traffic is silently swallowed/redirected. Notifications, MQTT, OTA and NTP all fail with confusing low-level errors. |

In both cases the current firmware has **no way to detect** that the link is
portal-limited, **no way to surface that to the user**, and **no mechanism to
let a human complete the portal flow**.

## Current-state summary (what we found)

- `WiFiManager` stores up to `MAX_NETWORKS = 10` SSID/PSK pairs in NVS
  (`wifi` namespace, `ssid_N`/`pass_N` keys), picks the strongest stored SSID
  from a scan, and reconnects with a 30 s throttle and sticky-reason escalation.
  There is no notion of a "portal" state — `isConnected()` is purely
  `WiFi.status() == WL_CONNECTED`, i.e. L2 association only.
- `ConfigServer::startSetupMode()` already implements the *inbound* half of a
  captive portal: AP+STA mode, a wildcard `DNSServer`, and
  `handleCaptivePortalProbe()` returning a 302 on unknown paths so
  iOS/Android/Windows pop the mini-browser.
- The config UI's add-network form
  (`dev-ui/wifi-config.html`) posts `ssid` + `password` to `POST /config`
  → `ConfigServer::handleSubmit()` → `WiFiManager::addNetwork()`.
  Password is mandatory in the HTML and in the JS validation.
- Outbound traffic (`HttpPoster`, MQTT, NTP, OTA) assumes a transparent
  internet path once `WL_CONNECTED` is true. `TimeManagement.cpp` already has
  a comment noting "DNS to pool.ntp.org blocked by a captive portal" as a
  known failure mode, but nothing reacts to it.

## Proposed architecture

The design has four cooperating pieces: **detection**, **state surfacing**,
**user-assisted portal completion**, and **outbound gating**.

### 1. Captive-portal detection (`PortalDetector`)

A small helper (could live in `WiFiManager` or a new `PortalDetector.cpp`)
that runs **after** `WL_CONNECTED` and **periodically while connected**:

1. Fire the standard connectivity probes the OSes use:
   - `http://connectivitycheck.gstatic.com/generate_204` (Android/Chrome)
   - `http://clients3.google.com/generate_204`
   - `http://captive.apple.com/hotspot-detect.html`
2. Interpretation:
   - `204` with empty body → **online**.
   - `200` on the 204 endpoint, or any 3xx redirect to a different host, or a
     body containing Apple’s `Success` sentinel when it shouldn't → **portal**.
   - TCP/DNS failure → inconclusive (treat as offline, don't flap the state).
3. Expose `PortalState { UNKNOWN, ONLINE, PORTAL_SUSPECTED, PORTAL_CONFIRMED }`
   and the redirect `Location` URL when the portal provides one — this URL is
   gold for step 3 because it lets the UI deep-link the user straight to the
   marina's login page.

Detection cadence: once immediately after connect, then every 60–120 s while
connected, plus once before any "test notification" send. Keep it out of the
hot loop; run it from `WiFiManager::maintainConnection()` or the notification
worker.

### 2. State surfacing (status API + UI)

- Extend `ConfigServer::handleStatus()` and `handleInit()` to include:
  ```json
  "portal": {
    "state": "portal_detected",      // "online" | "offline" | "portal_detected" | "unknown"
    "loginUrl": "http://10.1.0.1/login?...",  // when the portal gave us a Location
    "ssid": "MarinaGuest"
  }
  ```
- In `dev-ui/index.html` and `wifi-config.html`:
  - New pill state (amber) for "Portal — action needed".
  - On the WiFi page, show a banner per stored network whose SSID matches the
    currently-connected portal network: *"This network requires browser
    sign-in. Press **Open portal login** while connected."*
- `wifi-config.html` must **allow an empty password** (drop `required` and the
  `!p` check) and ideally add a scan-to-pick dropdown so users don't typo
  open SSIDs. `WiFiManager::addNetwork()` already handles `""` fine
  (`WiFi.begin(ssid, "")` associates to open APs), so this is a UI-only fix
  plus passing `hasArg("password")`-or-empty in `handleSubmit()`.

### 3. User-assisted portal completion ("portal assist" mode)

This is the crux: an ESP32 can't render the marina's JavaScript splash page,
but the owner's phone **can**. Two viable patterns — recommend **A**:

**A. AP+STA relay assist (recommended).**
The device already supports `WIFI_AP_STA` in setup mode. Reuse that:

1. User presses **"Open portal login"** on the WiFi page. Firmware enters a
   time-bounded (~5 min) assist mode: STA stays associated to the marina AP;
   the soft AP (`ESP32-BilgeRise-Setup`) is (re)started *without tearing down
   STA* — the same `WIFI_AP_STA` mode `startSetupMode()` already uses.
2. User joins the phone to the device's AP and opens
   `http://192.168.4.1/portal`. The config server shows the portal URL it
   captured (`loginUrl`) with a big link, plus instructions.
3. The phone opens the marina page **through the marina's DNS/IP** — the
   trick is the phone must reach the portal *via the ESP32's STA side*.
   Two implementation options:
   - *Simple (v1):* instruct the user to **disconnect from the ESP32 AP and
     join the marina SSID directly on their phone** to complete the portal
     (most portals then whitelist the *device MAC*… no — they whitelist the
     phone's MAC, not the ESP32's). So this alone doesn't fix the ESP32.
   - *Working (v1):* run a **tiny TCP relay / NAT-ish pass-through** on the
     ESP32: a handler on the config server that proxies the portal URL
     (e.g. `/portal/proxy?url=...`) — the ESP32 fetches the splash page with
     its own MAC/IP and serves it back to the phone through the AP. Form
     submissions from the phone go back through the same proxy, so the portal
     sees the ESP32's IP/MAC completing the flow → the ESP32 gets whitelisted.
     For simple GET/POST form portals (the common marina case, often just an
     "Accept terms" button) this works well. Heavy-JS or RADIUS-backed
     portals may still fail, which is acceptable — the UI can then say
     "portal type unsupported; try again or contact marina."
   - *Robust (v2, optional):* full NAT between softAP and STA
     (`esp_netif` NAT is available in ESP-IDF ≥ 4.4 / Arduino core 2.x via
     `esp_nat` or by enabling `CONFIG_LWIP_IP_FORWARD` + NAPT). Phone traffic
     egresses with the ESP32's marina IP, so any portal flow completes
     transparently. Higher complexity/memory cost; keep as a stretch goal.
4. When the periodic probe (step 1) flips to `ONLINE`, assist mode exits
   automatically, the AP shuts down, and normal operation resumes. A
   notification ("Marina WiFi now online") can be sent.

**B. MAC-spoof companion app** (rejected): ask the user to spoof the ESP32's
MAC on their laptop — far too technical for the target audience.

### 4. Outbound gating & backoff

- `HttpPoster::post()`, `MQTTService`, and `OTAManager` should consult the
  portal state: when `PORTAL_CONFIRMED`, **skip sends immediately** (fail
  fast with a clear log line: `[WIFI] Captive portal active — deferring
  outbound traffic`) instead of burning 10 s timeouts against a hijacked
  connection. `TimeManagement` already tolerates NTP failure; make it
  explicitly log "probable captive portal" when probes say so.
- Persist a per-network flag in NVS (`portal_N = 1`) when a portal has been
  detected for that SSID, so after a reconnect the UI can pre-warn "this
  network usually needs sign-in" even before the first probe completes.
- Watchdog care: detection probes and the proxy loop must respect the 10 s
  task-WDT like the existing scan/connect code (use `esp_task_wdt_reset()` in
  blocking loops, short HTTP timeouts ≤ 5 s).

### 5. Config-server changes (small)

- `POST /config`: accept empty `password` (treat missing/empty as open
  network).
- New endpoints (only registered while in setup/assist mode):
  - `GET /portal/status` → `{state, loginUrl}` (used by the banner/polling).
  - `POST /portal/assist` → enter assist mode (start AP+STA, begin probing).
  - `GET|POST /portal/proxy?...` → the relay for v1.
- WiFi page additions: "Open network (no password)" checkbox, amber portal
  banner with **Open portal login** button, per-network "portal" badge fed
  from the persisted NVS flag.

## Rollout plan

1. **Phase 1 (quick win, low risk):** allow empty passwords in UI +
   `handleSubmit`; add portal detection probe; surface state in `/status` and
   UI pill; gate `HttpPoster`/MQTT on portal state; persist `portal_N` flag.
   — Already eliminates the silent-failure confusion and makes open marina
   networks usable at all.
2. **Phase 2:** assist mode with `/portal/proxy` relay for form-based
   portals; auto-exit on `ONLINE`.
3. **Phase 3 (optional):** NAPT forwarding for full transparent assist;
   periodic re-probe with portal-session-expiry detection (many marinas
   de-auth after 24 h — the probe cadence + persisted flag handles this by
   re-flagging and re-prompting).

## Key risks / open questions

- **Memory:** proxy + DNS + webserver + AP+STA concurrently on ESP32 is
  tight but we already run webserver+DNS+AP+STA in setup mode, so the assist
  mode reuses a proven footprint. The proxy adds one HTTPClient at a time.
- **Portal diversity:** JS-heavy or SMS-OTP portals can't be automated; the
  proxy approach handles the common click-to-accept/login-form cases. The UI
  must degrade gracefully with clear messaging.
- **Security:** the `/portal/proxy` endpoint is an open relay while assist
  mode is on — acceptable given it's time-bounded, AP-password-protected
  (unique per-device AP password already exists), and only enabled on
  explicit user request. Restrict proxy targets to http(s) port 80/443 and
  block RFC1918 *except* the portal's captured IP to reduce abuse.
- **Session expiry:** marina portals often expire; the periodic probe is the
  mechanism to catch this and re-prompt. Consider surfacing "portal sign-in
  expired" as its own state so the user isn't confused about why alerts
  stopped.
