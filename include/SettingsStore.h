#pragma once

/*
    SettingsStore.h

    Plain NVS-backed settings for alarm thresholds and notification timing.
    Decoupled from ConfigServer so the state machine can read settings
    without going through the web server, and a future config-over-MQTT
    path can write here directly.

    Pattern mirrors MQTTService: fixed char[] / numeric members loaded once
    at begin(), refreshed on saveSettings(). No String heap allocations
    during normal operation.

    ConfigServer is ONE writer of SettingsStore; the state machine context
    is populated from SettingsStore in main.cpp loop() instead of calling
    configServer->get*() directly.
*/

#include <Arduino.h>
#include <Preferences.h>

// NVS namespace (same key names as the old ConfigServer emergency settings)
constexpr const char SETTINGS_STORE_NAMESPACE[] = "emergency";

struct SettingsValues {
    float emergencyWaterLevel_cm;        // Tier 1 threshold (notification)
    int   emergencyNotifFreq_ms;         // Tier 1 notification interval
    float urgentEmergencyWaterLevel_cm;  // Tier 2 threshold
    int   hornOnDuration_ms;
    int   hornOffDuration_ms;
};

// Defaults match the old ConfigServer constants
constexpr SettingsValues SETTINGS_DEFAULTS = {
    /* emergencyWaterLevel_cm       */ 30.0f,
    /* emergencyNotifFreq_ms        */ 900000,
    /* urgentEmergencyWaterLevel_cm */ 50.0f,
    /* hornOnDuration_ms            */ 1000,
    /* hornOffDuration_ms           */ 1000
};

class SettingsStore {
public:
    SettingsStore();

    // Load settings from NVS into in-RAM struct.
    // Call once at startup. ConfigServer also calls this after any save.
    void load();

    // Persist the provided values to NVS and refresh the in-RAM copy.
    void save(const SettingsValues& v);

    // Fast in-RAM getters — safe to call every loop iteration
    float getEmergencyWaterLevel()       const { return vals.emergencyWaterLevel_cm; }
    int   getEmergencyNotifFreq()        const { return vals.emergencyNotifFreq_ms; }
    float getUrgentEmergencyWaterLevel() const { return vals.urgentEmergencyWaterLevel_cm; }
    int   getHornOnDuration()            const { return vals.hornOnDuration_ms; }
    int   getHornOffDuration()           const { return vals.hornOffDuration_ms; }

    // Direct access to the full struct (e.g. for ConfigServer to populate UI fields)
    const SettingsValues& get() const { return vals; }

private:
    Preferences prefs;
    SettingsValues vals;
};
