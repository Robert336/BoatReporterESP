#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <vector>

// Custom STA MAC override (empty = factory). Applied via esp_wifi_set_mac
// before each association. Must be unicast (LSB of first byte = 0).

constexpr const char* WIFI_PREFERENCES_NAMESPACE = "wifi";
static constexpr int MAX_NETWORKS = 10;
static constexpr int CONNECT_TIMEOUT_MS = 15000; // 15 secs
static constexpr uint32_t RECONNECT_INTERVAL_MS = 30000; // 30 secs between retry attempts

// Captive-portal probe cadence (~1–3s blocking GET). Never on the hot path.
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
    // Fixed-size SSID/PSK — no heap (avoids prior new[]/delete[] leak/double-free).
    static constexpr size_t SSID_MAX = 33;
    static constexpr size_t PASS_MAX = 65;
    char ssid[SSID_MAX];
    char password[PASS_MAX];
};

// One row from scanAvailableNetworks(): SSIDs de-duped, strongest BSSID kept.
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

    // Session-start times use esp_timer_get_time() µs (monotonic) — not millis()
    // — so logged "was up %lus" stays correct across the ~49.7-day millis wrap (H6).
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
    // Bypass RECONNECT_INTERVAL_MS on the next maintainConnection() call.
    // Used after stopSetupMode() so reconnect is prompt without blocking (WDT).
    void requestImmediateReconnect() { _lastReconnectAttempt = 0; }
    std::vector<String> getStoredSSIDs();
    bool isConnected();

    // On-demand scan (~2–5s blocking). De-duped by SSID, strongest first.
    std::vector<ScannedNetwork> scanAvailableNetworks();
    // Non-blocking join to a saved SSID (WiFi.begin returns immediately).
    bool connectToNetwork(const char* ssid);

    // Custom STA MAC (empty = factory). Persisted as "sta_mac"; applied next association.
    String getCustomMac();
    bool setCustomMac(const String& mac);
    static bool parseMac(const String& s, uint8_t out[6]);
    int  getRSSI(); // Returns current RSSI in dBm, 0 if not connected
    void disconnect();

    // Captive portal: L2 up ≠ internet. isInternetReachable() gates outbound HTTP.
    PortalState getPortalState() const { return _portalState; }
    const String& getPortalLoginUrl() const { return _portalLoginUrl; }
    bool isInternetReachable() { return isConnected() && _portalState != PortalState::PORTAL; }
    // NVS-persisted "this SSID had a portal" flag for UI pre-warn.
    bool storedNetworkHadPortal(const char* ssid) { return getPortalFlagForSsid(ssid); }
    void requestPortalProbe() { _portalProbePending = true; }
};

