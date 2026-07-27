#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <vector>

// Custom STA MAC override. Empty string = use factory MAC. Applied via
// esp_wifi_set_mac(WIFI_IF_STA, ...) before every association attempt, so
// the address the AP sees can be changed from the config page without
// reflashing. Must be a unicast address (LSB of first byte = 0).

constexpr const char* WIFI_PREFERENCES_NAMESPACE = "wifi";
static constexpr int MAX_NETWORKS = 10;
static constexpr int CONNECT_TIMEOUT_MS = 15000; // 15 secs
static constexpr uint32_t RECONNECT_INTERVAL_MS = 30000; // 30 secs between retry attempts

// Captive-portal detection cadence. A probe is a ~1-3s blocking HTTP GET, so
// it runs on its own throttle from maintainConnection() — never in the hot
// path. The fast re-check after a fresh association catches portals within a
// few seconds of connect; the slow cadence catches portal session expiry
// (many marinas de-auth after ~24h) without hammering the network.
static constexpr uint32_t PORTAL_PROBE_AFTER_CONNECT_MS = 3000;
static constexpr uint32_t PORTAL_PROBE_INTERVAL_MS      = 120000; // 2 min
static constexpr int      PORTAL_PROBE_TIMEOUT_MS       = 4000;

// H1/H2: after this many consecutive failed WiFi.reconnect() attempts, fall
// back to a full connectToBestNetwork() scan-and-pick cycle instead of retrying
// the same (possibly permanently-gone) AP forever.
static constexpr uint32_t RECONNECT_FALLBACK_ATTEMPTS = 6; // 6 * 30s = 3 min
// H2: disconnect reasons known to sometimes need a full teardown
// (WiFi.disconnect(true) + rescan) rather than a lightweight WiFi.reconnect()
// escalate after fewer attempts.
static constexpr uint32_t RECONNECT_ESCALATION_ATTEMPTS_STICKY = 2; // 2 * 30s = 1 min

struct WiFiCredential {
    // Fixed-size storage — no heap, no manual delete[], no double-free risk.
    // SSID max 32 chars + NUL (802.11), PSK max 64 chars + NUL (WPA2).
    // Previously raw char* managed with new[]/delete[]; loadCredentials()
    // leaked those blocks on every NVS-reload, and the copyable struct made
    // the vector's reallocation copies a latent double-free. Value storage
    // eliminates the entire class.
    static constexpr size_t SSID_MAX = 33;
    static constexpr size_t PASS_MAX = 65;
    char ssid[SSID_MAX];
    char password[PASS_MAX];
};

// A single visible AP from an on-demand scan, returned by
// scanAvailableNetworks(). SSIDs are de-duplicated (mesh / multi-AP
// deployments advertise several BSSIDs for one name); we keep the strongest
// instance so the config list shows one row per network, like iOS.
struct ScannedNetwork {
    String ssid;
    int32_t rssi;
    uint8_t channel;
    bool open;       // true for WIFI_AUTH_OPEN (no PSK)
};

// Captive-portal reachability classification. UNKNOWN until the first probe
// after association completes; ONLINE means the connectivity probe passed
// un-hijacked; PORTAL means HTTP traffic is being intercepted (marina login).
enum class PortalState : uint8_t {
    UNKNOWN = 0,
    ONLINE  = 1,
    PORTAL  = 2,
};

class WiFiManager {
private:
    Preferences preferences;
    std::vector<WiFiCredential> storedNetworks;
    bool isWiFiConnected = false;

    // Connection health tracking.
    // H6: session-start timestamps are captured in esp_timer_get_time()
    // microseconds (int64, monotonic, never wraps in practice) rather than
    // millis() (uint32, wraps every ~49.7 days). The millis()-based timing
    // *comparisons* in this class are already wrap-safe (unsigned subtraction),
    // but the *logged* session durations ("was up %lus") were computed from
    // millis() and went wrong for any single WiFi session lasting across a
    // rollover. These fields are used solely for logging durations.
    int64_t _connectedSinceUs = 0;
    int64_t _disconnectedSinceUs = 0;
    volatile uint8_t _lastDisconnectReason = 0; // written by WiFi event task
    uint32_t _reconnectAttemptCount = 0;
    uint32_t _lastReconnectAttempt = 0;
    uint32_t _connectedAtMs = 0; // millis() at association (probe scheduling)

    // Captive-portal detection state. Only meaningful while WL_CONNECTED.
    PortalState _portalState = PortalState::UNKNOWN;
    uint32_t    _lastPortalProbe = 0;
    bool        _portalProbePending = false; // schedule fast probe after (re)connect
    String      _portalLoginUrl;             // redirect Location captured from portal
    String      _portalSsid;                 // SSID the current portal state applies to

    WiFiManager();
    void loadCredentials();
    void loadCustomMac();
    void applyCustomMac();
    String _customMac;
    static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
    static const char* reasonToString(uint8_t reason);
    // H2: true for disconnect reasons (4WAY_HANDSHAKE_TIMEOUT, BEACON_TIMEOUT,
    // HANDSHAKE_TIMEOUT, AUTH_FAIL) that are known to sometimes require a full
    // disconnect+rescan rather than a plain WiFi.reconnect().
    static bool isStickyDisconnectReason(uint8_t reason);
    // Blocking captive-portal probe (~1-4s). Sets _portalState/_portalLoginUrl
    // and persists the per-SSID flag. Caller must have fed the task watchdog.
    void runPortalProbe();
    // NVS per-network portal flag ("portal_N" in the wifi namespace).
    void setPortalFlagForCurrentSsid(bool portal);
    bool getPortalFlagForSsid(const char* ssid);

public:
    static WiFiManager& getInstance();

    WiFiManager(const WiFiManager&) = delete;
    WiFiManager& operator=(const WiFiManager&) = delete;
    ~WiFiManager();

    void begin();
    void addNetwork(const char* ssid, const char* password);
    void removeNetwork(const char* ssid);
    void connectToBestNetwork();
    void maintainConnection(); // Call from main loop — non-blocking reconnect with backoff
    // Force maintainConnection() to attempt a reconnect on its next call,
    // bypassing the RECONNECT_INTERVAL_MS throttle. Used after the config
    // portal closes so the device reconnects promptly without blocking
    // stopSetupMode() (and risking a watchdog reboot).
    void requestImmediateReconnect() { _lastReconnectAttempt = 0; }
    std::vector<String> getStoredSSIDs();
    bool isConnected();

    // On-demand scan of in-range networks (blocking ~2-5s). De-duplicates by
    // SSID keeping the strongest signal, and sorts by RSSI descending so the
    // config page's "Other Networks" list reads like iOS (strongest first).
    // Safe in both pure-STA and AP+STA (config-portal) modes — the STA scans
    // while the AP keeps serving the page.
    std::vector<ScannedNetwork> scanAvailableNetworks();
    // Associate with a specific stored network without changing its saved
    // credentials. Non-blocking (WiFi.begin returns immediately; the result is
    // picked up by maintainConnection()) so the config page can show live
    // "Connecting…/Connected" feedback. Returns false if the SSID isn't saved.
    bool connectToNetwork(const char* ssid);

    // Custom STA MAC. Empty string = factory MAC. Persisted to NVS ("sta_mac"
    // in the wifi namespace) and applied to the radio before the next
    // association. Setting it while connected takes effect on the next
    // reconnect/association.
    String getCustomMac();
    bool setCustomMac(const String& mac);
    static bool parseMac(const String& s, uint8_t out[6]);
    int  getRSSI(); // Returns current RSSI in dBm, 0 if not connected
    void disconnect();

    // === Captive portal state ===
    // L2 association says nothing about real internet reachability behind a
    // marina captive portal. getPortalState() is refreshed by the periodic
    // probe in maintainConnection(); isInternetReachable() is the gate
    // outbound channels should consult before attempting traffic.
    PortalState getPortalState() const { return _portalState; }
    const String& getPortalLoginUrl() const { return _portalLoginUrl; }
    bool isInternetReachable() { return isConnected() && _portalState != PortalState::PORTAL; }
    // True if a stored network was previously seen behind a captive portal
    // (persisted in NVS), used by the UI to pre-warn before the first probe.
    bool storedNetworkHadPortal(const char* ssid) { return getPortalFlagForSsid(ssid); }
    // Force an immediate probe on the next maintainConnection() tick
    // (used when portal-assist mode wants fast feedback after the user
    // completes the portal flow).
    void requestPortalProbe() { _portalProbePending = true; }
};

