#pragma once

// NVS-backed alarm thresholds and notification timing. ConfigServer writes;
// main.cpp copies into StateMachineContext each loop.

#include <Arduino.h>
#include <Preferences.h>
#include "StateMachine.h"   // AlarmSettings + ALARM_SETTINGS_DEFAULTS (shared value types)

// NVS namespace (same key names as the old ConfigServer emergency settings)
constexpr const char SETTINGS_STORE_NAMESPACE[] = "emergency";

using SettingsValues = AlarmSettings;

// Expressed as a constexpr function (not a constexpr variable/reference) so
// no symbol is emitted — a header-level constexpr reference would produce a
// "multiple definition" link error under the pre-C++17 toolchain.
constexpr SettingsValues SETTINGS_DEFAULTS() { return ALARM_SETTINGS_DEFAULTS; }

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
