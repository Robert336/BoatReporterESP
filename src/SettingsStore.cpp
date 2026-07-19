#ifndef UNIT_TESTING
#include "SettingsStore.h"
#include "Logger.h"

SettingsStore::SettingsStore() : vals(SETTINGS_DEFAULTS()) {}

void SettingsStore::load() {
    // Seed defaults first so we always have valid values even if NVS open fails
    vals = SETTINGS_DEFAULTS();

    if (!prefs.begin(SETTINGS_STORE_NAMESPACE, /*readOnly=*/true)) {
        LOG_INFO("[SETTINGS] NVS namespace not found — using defaults");
        return;
    }

    // Key names match the legacy ConfigServer keys so existing NVS data is
    // preserved across firmware updates. Do not rename these.
    float lvl = prefs.getFloat("level_cm", -1.0f);
    if (lvl >= 0.0f) vals.emergencyWaterLevel_cm = lvl;

    int freq = prefs.getInt("notif_freq_ms", -1);
    if (freq >= 0) vals.emergencyNotifFreq_ms = freq;

    float ulvl = prefs.getFloat("urgent_level_cm", -1.0f);
    if (ulvl >= 0.0f) vals.urgentEmergencyWaterLevel_cm = ulvl;

    int hon = prefs.getInt("horn_on_ms", -1);
    if (hon >= 0) vals.hornOnDuration_ms = hon;

    int hoff = prefs.getInt("horn_off_ms", -1);
    if (hoff >= 0) vals.hornOffDuration_ms = hoff;

    prefs.end();

    LOG_INFO("[SETTINGS] Loaded from NVS: emerg=%.1f cm, freq=%d ms, urgent=%.1f cm",
             vals.emergencyWaterLevel_cm, vals.emergencyNotifFreq_ms,
             vals.urgentEmergencyWaterLevel_cm);
}

void SettingsStore::save(const SettingsValues& v) {
    if (!prefs.begin(SETTINGS_STORE_NAMESPACE, /*readOnly=*/false)) {
        LOG_CRITICAL("[SETTINGS] Failed to open NVS for writing");
        return;
    }

    // Key names match legacy ConfigServer keys (NVS backward-compatible)
    prefs.putFloat("level_cm",       v.emergencyWaterLevel_cm);
    prefs.putInt("notif_freq_ms",    v.emergencyNotifFreq_ms);
    prefs.putFloat("urgent_level_cm",v.urgentEmergencyWaterLevel_cm);
    prefs.putInt("horn_on_ms",       v.hornOnDuration_ms);
    prefs.putInt("horn_off_ms",      v.hornOffDuration_ms);

    prefs.end();

    vals = v; // Update in-RAM copy atomically
    LOG_INFO("[SETTINGS] Saved to NVS");
}

#endif // UNIT_TESTING
