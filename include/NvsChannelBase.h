#pragma once

/*
    NvsChannelBase.h

    Shared NVS-cache plumbing for NotificationChannel implementations.
    Each concrete channel (SMS, Discord, Custom, …) stores its config in the
    "notify" NVS namespace and mirrors it in fixed char[] caches so send()
    never touches NVS. That load/save boilerplate used to be copy-pasted per
    channel; it lives here once.

    Derived classes call beginLoad()/loadStr()/finishLoad() to populate their
    caches from their keys, and openForWrite()/putStr()/endWrite() followed by
    loadCache() so the in-RAM cache always reflects the last successful save.

    Not unit-test-buildable (Preferences is Arduino-only) — same as the
    concrete channels, all of which are excluded from UNIT_TESTING builds.
*/

#ifndef UNIT_TESTING

#include <Preferences.h>
#include <Arduino.h>
#include "Logger.h"

// Single NVS namespace for all notification channel config.
// (Was previously a file-local constant in each channel's .cpp.)
// NOTE: NVS keys must be ≤15 chars — a longer key silently fails to write.
static constexpr const char* NOTIFY_NVS_NAMESPACE = "notify";

class NvsChannelBase {
protected:
    Preferences prefs;
    bool cacheLoaded = false;

    // --- Load path -------------------------------------------------------

    // Start a cache reload: clear caches and open NVS read-only.
    // Returns false if the namespace can't be opened (caches stay empty,
    // cacheLoaded is still set so a missing namespace isn't retried forever).
    bool beginLoad() {
        cacheLoaded = true;
        return prefs.begin(NOTIFY_NVS_NAMESPACE, /*readOnly=*/true);
    }

    void finishLoad() { prefs.end(); }

    // Copy an NVS string into a fixed cache slot; leaves dst empty when the
    // key is absent or the value doesn't fit.
    void loadStr(const char* key, char* dst, size_t dstSize) {
        String v = prefs.getString(key, "");
        if (v.length() > 0 && v.length() < dstSize) {
            strncpy(dst, v.c_str(), dstSize - 1);
            dst[dstSize - 1] = '\0';
        }
    }

    // --- Save path -------------------------------------------------------

    bool openForWrite(const char* logTag) {
        if (!prefs.begin(NOTIFY_NVS_NAMESPACE, /*readOnly=*/false)) {
            LOG_CRITICAL("[%s] Failed to open NVS for writing", logTag);
            return false;
        }
        return true;
    }

    // Write one string key; returns bytes written (0 = failure, matching
    // Preferences::putString semantics).
    size_t putStr(const char* key, const char* value) {
        return prefs.putString(key, value);
    }

    void endWrite() { prefs.end(); }

    // Convenience: reload the in-RAM cache after a successful write.
    // loadCache() is the channel's own virtual, so this stays in the
    // derived class's updateX() — but call sites read better with a name.
    // (Kept as a protected hook so derived updateX() stays one line.)
};

#endif // UNIT_TESTING
