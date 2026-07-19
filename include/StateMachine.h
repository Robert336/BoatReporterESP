#pragma once

#include <stdint.h>
#include <stdio.h>   // For snprintf (C-style for Arduino compatibility)
#include <math.h>    // For isnan

// Forward declarations for hardware-dependent types
class ConfigServer;

// System States
enum State {
    ERROR,
    NORMAL,
    EMERGENCY,
    CONFIG
};

// Sensor reading for state machine (simplified interface)
struct StateMachineSensorReading {
    bool valid;
    float level_cm;
};

// Alarm/notification settings the state machine operates on. This is a pure
// value type (no Arduino/NVS dependency) so it works in native unit-test
// builds; SettingsStore.h mirrors it 1:1 and provides the NVS-backed copy.
// Keeping the canonical field set here means a new threshold only needs to
// be added in ONE place, not copied field-by-field into the context.
struct AlarmSettings {
    float emergencyWaterLevel_cm;        // Tier 1 threshold (notification)
    int   emergencyNotifFreq_ms;         // Tier 1 notification interval
    float urgentEmergencyWaterLevel_cm;  // Tier 2 threshold
    int   hornOnDuration_ms;
    int   hornOffDuration_ms;
};

// Defaults match the legacy ConfigServer / SettingsStore constants.
constexpr AlarmSettings ALARM_SETTINGS_DEFAULTS = {
    /* emergencyWaterLevel_cm       */ 30.0f,
    /* emergencyNotifFreq_ms        */ 900000,
    /* urgentEmergencyWaterLevel_cm */ 50.0f,
    /* hornOnDuration_ms            */ 1000,
    /* hornOffDuration_ms           */ 1000
};

// State machine context - holds all state information
struct StateMachineContext {
    // Current state
    State currentState;

    // Timers (milliseconds)
    uint32_t lastStateChangeTime;
    uint32_t emergencyConditionsTrueTime;
    uint32_t emergencyConditionsFalseTime;
    uint32_t lastEmergencyMessageTime;
    uint32_t lastHornToggleTime;
    uint32_t sensorErrorTrueTime;          // When sensorError last went false->true
    uint32_t lastSensorErrorNotifyTime;    // Last sustained-failure notification

    // Event flags
    bool emergencyConditions;              // Tier 1 threshold exceeded
    bool urgentEmergencyConditions;        // Tier 2 threshold exceeded
    bool hornCurrentlyOn;                  // Tracks horn state for pulsing
    bool sensorError;
    bool sensorErrorNotified;              // Owner alerted about the current failure episode
    volatile bool configCommandReceived;
    bool notificationsSilenced;            // Tracks if emergency alerts are silenced

    // Last reading where valid=true. Used as the displayed level in emergency
    // notifications when the current sample is invalid, so the owner sees the
    // last trustworthy value instead of a garbage ADC reading.
    float lastValidLevel_cm;
    bool  hasValidLevel;

    // Configuration values (normally from SettingsStore). Stored as a block
    // so main.cpp can refresh all of them with one assignment. The reference
    // members below alias the same fields so existing readers — including the
    // unit tests and the state-machine internals — keep compiling unchanged.
    AlarmSettings settings;
    float& emergencyWaterLevel_cm;
    float& urgentEmergencyWaterLevel_cm;
    int&   emergencyNotifFreq_ms;
    int&   hornOnDuration_ms;
    int&   hornOffDuration_ms;

    // Constructor with defaults
    StateMachineContext() :
        currentState(NORMAL),
        lastStateChangeTime(0),
        emergencyConditionsTrueTime(0),
        emergencyConditionsFalseTime(0),
        lastEmergencyMessageTime(0),
        lastHornToggleTime(0),
        sensorErrorTrueTime(0),
        lastSensorErrorNotifyTime(0),
        emergencyConditions(false),
        urgentEmergencyConditions(false),
        hornCurrentlyOn(false),
        sensorError(false),
        sensorErrorNotified(false),
        configCommandReceived(false),
        notificationsSilenced(false),
        lastValidLevel_cm(0.0f),
        hasValidLevel(false),
        settings(ALARM_SETTINGS_DEFAULTS),
        emergencyWaterLevel_cm(settings.emergencyWaterLevel_cm),
        urgentEmergencyWaterLevel_cm(settings.urgentEmergencyWaterLevel_cm),
        emergencyNotifFreq_ms(settings.emergencyNotifFreq_ms),
        hornOnDuration_ms(settings.hornOnDuration_ms),
        hornOffDuration_ms(settings.hornOffDuration_ms)
    {}

    // Refresh settings from SettingsStore in one shot:
    //   smCtx.setSettings(settingsStore.get());
    void setSettings(const AlarmSettings& s) { settings = s; }

    // The reference members below make the implicitly-generated copy
    // assignment operator deleted, so define both copy ops explicitly. The
    // references stay bound to this object's own `settings` block (they are
    // set in the default constructor and never re-seated); copying only
    // transfers the value members. Defined out-of-line below for readability.
    StateMachineContext(const StateMachineContext& other);
    StateMachineContext& operator=(const StateMachineContext& other);
};

// Copy ctor: default-construct first (which binds the reference members to
// this object's own `settings`), then copy the value members across.
inline StateMachineContext::StateMachineContext(const StateMachineContext& other)
    : StateMachineContext() {
    *this = other;
}

inline StateMachineContext& StateMachineContext::operator=(const StateMachineContext& other) {
    if (this == &other) return *this;
    currentState                 = other.currentState;
    lastStateChangeTime          = other.lastStateChangeTime;
    emergencyConditionsTrueTime  = other.emergencyConditionsTrueTime;
    emergencyConditionsFalseTime = other.emergencyConditionsFalseTime;
    lastEmergencyMessageTime     = other.lastEmergencyMessageTime;
    lastHornToggleTime           = other.lastHornToggleTime;
    sensorErrorTrueTime          = other.sensorErrorTrueTime;
    lastSensorErrorNotifyTime    = other.lastSensorErrorNotifyTime;
    emergencyConditions          = other.emergencyConditions;
    urgentEmergencyConditions    = other.urgentEmergencyConditions;
    hornCurrentlyOn              = other.hornCurrentlyOn;
    sensorError                  = other.sensorError;
    sensorErrorNotified          = other.sensorErrorNotified;
    configCommandReceived        = other.configCommandReceived;
    notificationsSilenced        = other.notificationsSilenced;
    lastValidLevel_cm            = other.lastValidLevel_cm;
    hasValidLevel                = other.hasValidLevel;
    settings                     = other.settings;
    return *this;
}

// State machine output - actions to take
struct StateMachineOutput {
    bool stateChanged;
    State newState;

    // Horn control
    bool setHornState;
    bool hornOn;

    // Alert output (GPIO 26) — the dedicated emergency indicator, computed
    // fresh every call. Unlike hornOn/setHornState this isn't edge-triggered;
    // the caller writes it to the pin unconditionally each loop iteration.
    bool alertPinOn;

    // Notification flags
    bool sendEmergencyNotification;
    bool sendSilenceConfirmation;
    bool sendUnsilenceConfirmation;
    bool sendSensorRecoveryNotification;
    bool sendSustainedSensorFailureNotification;

    // Latest reading info for message construction (to avoid re-reading sensor)
    float displayLevel_cm;       // level_cm to use in emergency messages
    bool  sensorFaultActive;     // true when emergency message should note stale data
    float rateOfChange_cm30min;  // NaN when unavailable

    // Sensor-failure context for the sustained-failure notification
    uint32_t sensorDownSeconds;  // how long the sensor has been continuously failed

    // Message to send (if any notification flag is set)
    char message[256];

    // LED pattern (for light code)
    int ledPattern; // Can map to PATTERN_OFF, PATTERN_SOLID, etc.

    StateMachineOutput() :
        stateChanged(false),
        newState(NORMAL),
        setHornState(false),
        hornOn(false),
        alertPinOn(false),
        sendEmergencyNotification(false),
        sendSilenceConfirmation(false),
        sendUnsilenceConfirmation(false),
        sendSensorRecoveryNotification(false),
        sendSustainedSensorFailureNotification(false),
        displayLevel_cm(0.0f),
        sensorFaultActive(false),
        rateOfChange_cm30min(0.0f),
        sensorDownSeconds(0),
        ledPattern(0)
    {
        message[0] = '\0';
    }
};

// State machine timeout constants — match live main.cpp behavior
constexpr uint32_t EMERGENCY_TIMEOUT_MS = 5000;

// Sensor-failure notification timing (mirrors main.cpp constants)
constexpr uint32_t SENSOR_ERROR_NOTIFY_DELAY_MS  = 60000;   // 1 min sustained before first alert
constexpr uint32_t SENSOR_ERROR_NOTIFY_REPEAT_MS = 1800000; // 30 min reminder interval

// Pure function: Update emergency condition flags based on sensor reading
inline void updateEmergencyConditions(StateMachineContext& ctx,
                                      const StateMachineSensorReading& reading,
                                      uint32_t currentTime) {
    // Only evaluate thresholds against valid readings — an invalid sample's
    // level_cm is stale/garbage and would spuriously toggle the debounce
    // timers. Freeze the flags at their last-known-good values; sensorError
    // handling drives the ERROR transition separately.
    if (!reading.valid) {
        return;
    }

    // Remember the last trustworthy level for emergency messaging when the
    // sensor is faulty but EMERGENCY state is still active.
    ctx.lastValidLevel_cm = reading.level_cm;
    ctx.hasValidLevel = true;

    // Check Tier 1 emergency conditions (message notifications)
    bool previousEmergencyConditions = ctx.emergencyConditions;
    if (reading.level_cm >= ctx.emergencyWaterLevel_cm) {
        ctx.emergencyConditions = true;
        if (!previousEmergencyConditions) {
            ctx.emergencyConditionsTrueTime = currentTime;
        }
    } else {
        ctx.emergencyConditions = false;
        if (previousEmergencyConditions) {
            ctx.emergencyConditionsFalseTime = currentTime;
        }
    }

    // Check Tier 2 urgent emergency conditions (horn alarm)
    ctx.urgentEmergencyConditions = (reading.level_cm >= ctx.urgentEmergencyWaterLevel_cm);
}

// Pure function: Compute state transitions
inline State computeNextState(const StateMachineContext& ctx,
                              const StateMachineSensorReading& reading,
                              uint32_t currentTime,
                              bool configServerActive = false) {

    switch (ctx.currentState) {
        case ERROR:
            if (!ctx.sensorError) {
                return NORMAL;
            } else if (ctx.configCommandReceived) {
                // S2: only honor a config request once the sensor is healthy.
                // Allowing ERROR→CONFIG while sensorError is still true makes
                // the CONFIG case immediately bounce back to ERROR (its own
                // sensorError guard), flapping the state and needlessly
                // spinning the web server up and down on every button press
                // during a sensor outage. The !ctx.sensorError branch above
                // already returned, so reaching here means sensorError is
                // true — suppress the transition.
                break;
            }
            break;

        case NORMAL:
            if (ctx.sensorError) {
                return ERROR;
            } else if (ctx.emergencyConditions &&
                       (currentTime - ctx.emergencyConditionsTrueTime) >= EMERGENCY_TIMEOUT_MS) {
                return EMERGENCY;
            } else if (ctx.configCommandReceived) {
                return CONFIG;
            }
            break;

        case CONFIG:
            // Safety first: never let config mode suppress emergency/error
            // transitions. A browser tab left open on the config page polls
            // /ota/status every 5s, which keeps configServerActive=true and
            // would otherwise pin the device in CONFIG indefinitely — blind
            // to flooding. CONFIG is an overlay, not a substitute for safety.
            if (ctx.sensorError) {
                return ERROR;
            }
            if (ctx.emergencyConditions &&
                (currentTime - ctx.emergencyConditionsTrueTime) >= EMERGENCY_TIMEOUT_MS) {
                return EMERGENCY;
            }
            // No safety condition: exit when the config server goes idle.
            if (!configServerActive && !ctx.configCommandReceived) {
                return NORMAL;
            }
            break;

        case EMERGENCY:
            // S1: a dead sensor in EMERGENCY must degrade to ERROR, not latch.
            // updateEmergencyConditions() freezes emergencyConditions on an
            // invalid reading, so without this exit a sensor fault mid-flood
            // would pin the device in EMERGENCY forever — unable to leave (the
            // water-level guard below can never re-evaluate) and masking the
            // sensor failure behind a stale flood alarm. Require the failure to
            // be SUSTAINED (same window as the owner notification) so a single
            // transient glitch doesn't bounce an active flood into ERROR.
            if (ctx.sensorError &&
                (currentTime - ctx.sensorErrorTrueTime) >= SENSOR_ERROR_NOTIFY_DELAY_MS) {
                return ERROR;
            }
            if (!ctx.emergencyConditions &&
                (currentTime - ctx.emergencyConditionsFalseTime) >= EMERGENCY_TIMEOUT_MS) {
                return NORMAL;
            }
            break;
    }

    return ctx.currentState; // No state change
}

// Pure function: Determine if emergency notifications should be sent
inline bool shouldSendEmergencyNotification(const StateMachineContext& ctx,
                                            uint32_t currentTime) {
    if (ctx.currentState != EMERGENCY) {
        return false;
    }

    if (ctx.notificationsSilenced) {
        return false;
    }

    // First emergency after boot: lastEmergencyMessageTime is 0 (set in setup()),
    // so the elapsed-time check below would suppress the very first alert until
    // emergencyNotifFreq_ms elapses since boot. Send immediately instead — the
    // owner needs to know the moment a flood is detected, not 15 minutes later.
    if (ctx.lastEmergencyMessageTime == 0) {
        return true;
    }

    if (currentTime - ctx.lastEmergencyMessageTime >= (uint32_t)ctx.emergencyNotifFreq_ms) {
        return true;
    }

    return false;
}

// Pure function: Determine horn state
inline bool shouldHornBeOn(const StateMachineContext& ctx,
                           uint32_t currentTime) {
    if (ctx.currentState != EMERGENCY) {
        return false;
    }

    if (!ctx.urgentEmergencyConditions) {
        return false;
    }

    if (ctx.notificationsSilenced) {
        return false;
    }

    // S3: don't sound the Tier-2 flood horn off a STALE reading. On a sensor
    // fault, updateEmergencyConditions() freezes urgentEmergencyConditions at
    // its last value; if that happened to be true, the horn would pulse
    // indefinitely mimicking an active flood while nothing is being measured.
    // A sensor fault is signaled separately (sustained-failure notification +
    // ERROR transition), so the horn fails safe to OFF while the reading is
    // untrustworthy.
    if (ctx.sensorError) {
        return false;
    }

    // Check if it's time to toggle horn based on current state
    uint32_t currentDuration = ctx.hornCurrentlyOn ? ctx.hornOnDuration_ms : ctx.hornOffDuration_ms;
    if (currentTime - ctx.lastHornToggleTime >= currentDuration) {
        return !ctx.hornCurrentlyOn; // Toggle
    }

    return ctx.hornCurrentlyOn; // Maintain current state
}

// Pure function: alert output (GPIO 26) state — solid for Tier 1, pulsing
// (mirrors the horn timer) for Tier 2. Fails safe to OFF on a sensor fault
// (S3): a frozen reading must not drive a flood indication.
inline bool computeAlertPinState(const StateMachineContext& ctx) {
    if (ctx.currentState != EMERGENCY) {
        return false;
    }

    if (ctx.notificationsSilenced) {
        return false;
    }

    // S3: sensor fault — the level driving Tier selection is stale, so don't
    // assert a flood indication. The fault is surfaced via the ERROR state and
    // the sustained-failure notification instead.
    if (ctx.sensorError) {
        return false;
    }

    if (ctx.urgentEmergencyConditions) {
        return ctx.hornCurrentlyOn; // Tier 2: pulsing, driven by the horn timer
    }

    return true; // Tier 1: solid on
}

// Main state machine update function.
// Parameters:
//   ctx           - mutable state context (updated in place)
//   reading       - current sensor reading
//   currentTime   - millis() snapshot for this iteration
//   rateOfChange  - cm/30min from WaterPressureSensor::getRateOfChange_cm30min() (NaN if unavailable)
//   configServerActive - whether ConfigServer is actively serving
inline StateMachineOutput updateStateMachine(StateMachineContext& ctx,
                                             const StateMachineSensorReading& reading,
                                             uint32_t currentTime,
                                             float rateOfChange,
                                             bool configServerActive = false) {
    StateMachineOutput output;

    // While in CONFIG, consume any config-button presses BEFORE the state
    // evaluation below. The flag's only job is to trigger the entry
    // transition; a press that lands mid-session (after entry already
    // consumed the original) would otherwise stay set and block the
    // idle-timeout exit — the same infinite-config bug as before, just
    // with a different trigger. Clearing it here (rather than after the
    // transition logic) also prevents a one-iteration lag that would let
    // loop() restart the server for a full extra timeout cycle before the
    // exit could happen. Mid-CONFIG presses have no wired-up meaning
    // (they don't reset the server's activity timer), so discard them.
    if (ctx.currentState == CONFIG) {
        ctx.configCommandReceived = false;
    }

    // ------------------------------------------------------------------
    // Sensor error tracking
    // ------------------------------------------------------------------
    bool previousSensorError = ctx.sensorError;
    ctx.sensorError = !reading.valid;

    if (ctx.sensorError && !previousSensorError) {
        // Sensor just failed: record onset time, reset notification state
        ctx.sensorErrorTrueTime     = currentTime;
        ctx.sensorErrorNotified     = false;
    } else if (!ctx.sensorError && previousSensorError) {
        // Sensor recovered
        if (ctx.sensorErrorNotified) {
            // Only notify owner of recovery when they were previously told it failed
            ctx.sensorErrorNotified = false;
            output.sendSensorRecoveryNotification = true;
        }
    }

    // Sustained-failure notification (skip if I2C is unrecoverable — caller
    // sends its own distinct alert for that case)
    if (ctx.sensorError) {
        bool dueFirst  = !ctx.sensorErrorNotified &&
                         (currentTime - ctx.sensorErrorTrueTime >= SENSOR_ERROR_NOTIFY_DELAY_MS);
        bool dueRepeat = ctx.sensorErrorNotified &&
                         (currentTime - ctx.lastSensorErrorNotifyTime >= SENSOR_ERROR_NOTIFY_REPEAT_MS);
        if (dueFirst || dueRepeat) {
            ctx.sensorErrorNotified         = true;
            ctx.lastSensorErrorNotifyTime   = currentTime;
            output.sendSustainedSensorFailureNotification = true;
            output.sensorDownSeconds = (currentTime - ctx.sensorErrorTrueTime) / 1000;
        }
    }

    // ------------------------------------------------------------------
    // Update emergency conditions (only when reading is valid)
    // ------------------------------------------------------------------
    updateEmergencyConditions(ctx, reading, currentTime);

    // ------------------------------------------------------------------
    // Compute and apply state transitions
    // ------------------------------------------------------------------
    State nextState = computeNextState(ctx, reading, currentTime, configServerActive);

    if (nextState != ctx.currentState) {
        output.stateChanged = true;
        output.newState = nextState;
        ctx.currentState = nextState;
        ctx.lastStateChangeTime = currentTime;

        // State-entry actions
        if (nextState == NORMAL) {
            // Auto-clear silence when returning to normal (safety feature)
            if (ctx.notificationsSilenced) {
                ctx.notificationsSilenced = false;
            }
            // Clear config command so NORMAL doesn't immediately fall into CONFIG
            ctx.configCommandReceived = false;
            // Ensure horn is driven off
            if (ctx.hornCurrentlyOn) {
                output.setHornState = true;
                output.hornOn = false;
                ctx.hornCurrentlyOn = false;
            }
        }

        if (nextState == EMERGENCY) {
            // Drop any pending config-button press so post-emergency NORMAL
            // doesn't immediately fall into CONFIG.
            ctx.configCommandReceived = false;
        }
    }

    // ------------------------------------------------------------------
    // Handle emergency state operations
    // ------------------------------------------------------------------
    if (ctx.currentState == EMERGENCY) {
        // TIER 1: Periodic emergency message notifications
        if (shouldSendEmergencyNotification(ctx, currentTime)) {
            output.sendEmergencyNotification = true;
            // Update timer even when we're about to populate the output, so
            // the caller doesn't need to touch ctx.
            ctx.lastEmergencyMessageTime = currentTime;

            // Populate display context for caller's message construction
            output.displayLevel_cm      = reading.valid ? reading.level_cm : ctx.lastValidLevel_cm;
            output.sensorFaultActive    = ctx.sensorError;
            output.rateOfChange_cm30min = rateOfChange;
        }

        // TIER 2: Horn pulsing
        bool newHornState = shouldHornBeOn(ctx, currentTime);
        if (newHornState != ctx.hornCurrentlyOn) {
            output.setHornState = true;
            output.hornOn = newHornState;
            ctx.hornCurrentlyOn = newHornState;
            ctx.lastHornToggleTime = currentTime;
        }
    } else {
        // Not in emergency state — ensure horn is off
        if (ctx.hornCurrentlyOn) {
            output.setHornState = true;
            output.hornOn = false;
            ctx.hornCurrentlyOn = false;
        }
    }

    // Alert output (GPIO 26) reflects the current tier every iteration,
    // independent of the horn's edge-triggered setHornState/hornOn pair.
    output.alertPinOn = computeAlertPinState(ctx);

    return output;
}

// Handle button silence toggle in EMERGENCY state
inline StateMachineOutput handleSilenceToggle(StateMachineContext& ctx) {
    StateMachineOutput output;

    if (ctx.currentState != EMERGENCY) {
        return output; // Only works in emergency state
    }

    ctx.notificationsSilenced = !ctx.notificationsSilenced;

    if (ctx.notificationsSilenced) {
        output.sendSilenceConfirmation = true;

        // Turn off horn immediately when silenced
        if (ctx.hornCurrentlyOn) {
            output.setHornState = true;
            output.hornOn = false;
            ctx.hornCurrentlyOn = false;
        }
    } else {
        output.sendUnsilenceConfirmation = true;
    }

    return output;
}

// Helper to convert state to string
inline const char* stateToString(State state) {
    switch (state) {
        case ERROR: return "ERROR";
        case NORMAL: return "NORMAL";
        case EMERGENCY: return "EMERGENCY";
        case CONFIG: return "CONFIG";
        default: return "UNKNOWN";
    }
}
