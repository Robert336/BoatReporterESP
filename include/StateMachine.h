#pragma once

#include <stdint.h>
#include <stdio.h>   // For snprintf (C-style for Arduino compatibility)

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
    
    // Event flags
    bool emergencyConditions;              // Tier 1 threshold exceeded
    bool urgentEmergencyConditions;        // Tier 2 threshold exceeded
    bool hornCurrentlyOn;                  // Tracks horn state for pulsing
    bool sensorError;
    bool configCommandReceived;
    bool notificationsSilenced;            // Tracks if emergency alerts are silenced
    
    // Configuration values (normally from ConfigServer)
    float emergencyWaterLevel_cm;
    float urgentEmergencyWaterLevel_cm;
    int emergencyNotifFreq_ms;
    int hornOnDuration_ms;
    int hornOffDuration_ms;
    
    // Constructor with defaults
    StateMachineContext() :
        currentState(NORMAL),
        lastStateChangeTime(0),
        emergencyConditionsTrueTime(0),
        emergencyConditionsFalseTime(0),
        lastEmergencyMessageTime(0),
        lastHornToggleTime(0),
        emergencyConditions(false),
        urgentEmergencyConditions(false),
        hornCurrentlyOn(false),
        sensorError(false),
        configCommandReceived(false),
        notificationsSilenced(false),
        emergencyWaterLevel_cm(30.0),
        urgentEmergencyWaterLevel_cm(50.0),
        emergencyNotifFreq_ms(900000),
        hornOnDuration_ms(1000),
        hornOffDuration_ms(1000)
    {}
};

// State machine output - actions to take
struct StateMachineOutput {
    bool stateChanged;
    State newState;
    
    // Horn control
    bool setHornState;
    bool hornOn;
    
    // Notification flags
    bool sendEmergencyNotification;
    bool sendSilenceConfirmation;
    bool sendUnsilenceConfirmation;
    
    // Message to send (if any notification flag is set)
    char message[256];
    
    // LED pattern (for light code)
    int ledPattern; // Can map to PATTERN_OFF, PATTERN_SOLID, etc.
    
    StateMachineOutput() :
        stateChanged(false),
        newState(NORMAL),
        setHornState(false),
        hornOn(false),
        sendEmergencyNotification(false),
        sendSilenceConfirmation(false),
        sendUnsilenceConfirmation(false),
        ledPattern(0)
    {
        message[0] = '\0';
    }
};

// State machine timeout constants
constexpr uint32_t EMERGENCY_TIMEOUT_MS = 1000;

// Pure function: Update emergency condition flags based on sensor reading
inline void updateEmergencyConditions(StateMachineContext& ctx, 
                                      const StateMachineSensorReading& reading,
                                      uint32_t currentTime) {
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
    bool previousUrgentEmergencyConditions = ctx.urgentEmergencyConditions;
    if (reading.level_cm >= ctx.urgentEmergencyWaterLevel_cm) {
        ctx.urgentEmergencyConditions = true;
    } else {
        ctx.urgentEmergencyConditions = false;
    }
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
                return CONFIG;
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
            // Config state exits when config server becomes inactive
            if (!configServerActive && !ctx.configCommandReceived) {
                return NORMAL;
            }
            break;
            
        case EMERGENCY:
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
    
    // Check if it's time to toggle horn based on current state
    uint32_t currentDuration = ctx.hornCurrentlyOn ? ctx.hornOnDuration_ms : ctx.hornOffDuration_ms;
    if (currentTime - ctx.lastHornToggleTime >= currentDuration) {
        return !ctx.hornCurrentlyOn; // Toggle
    }
    
    return ctx.hornCurrentlyOn; // Maintain current state
}

// Main state machine update function
inline StateMachineOutput updateStateMachine(StateMachineContext& ctx,
                                             const StateMachineSensorReading& reading,
                                             uint32_t currentTime,
                                             bool configServerActive = false) {
    StateMachineOutput output;
    
    // Update sensor error flag
    ctx.sensorError = !reading.valid;
    
    // Update emergency conditions
    updateEmergencyConditions(ctx, reading, currentTime);
    
    // Compute next state
    State nextState = computeNextState(ctx, reading, currentTime, configServerActive);
    
    if (nextState != ctx.currentState) {
        output.stateChanged = true;
        output.newState = nextState;
        ctx.currentState = nextState;
        ctx.lastStateChangeTime = currentTime;
        
        // Handle state entry actions
        if (nextState == NORMAL && ctx.notificationsSilenced) {
            // Auto-clear silence flag when returning to normal
            ctx.notificationsSilenced = false;
        }
        
        if (nextState == NORMAL && ctx.configCommandReceived) {
            // Clear config command when entering normal
            ctx.configCommandReceived = false;
        }
    }
    
    // Handle emergency state operations
    if (ctx.currentState == EMERGENCY) {
        // Check if emergency notifications should be sent
        if (shouldSendEmergencyNotification(ctx, currentTime)) {
            output.sendEmergencyNotification = true;
            ctx.lastEmergencyMessageTime = currentTime;
            
            // Format message
            if (ctx.urgentEmergencyConditions) {
                snprintf(output.message, sizeof(output.message),
                        "Boat Monitor URGENT Alert: Tier 2 Emergency Level Reached - Critical Level %.2f cm",
                        reading.level_cm);
            } else {
                snprintf(output.message, sizeof(output.message),
                        "Boat Monitor Alert: Emergency Level %.2f cm",
                        reading.level_cm);
            }
        }
        
        // Handle horn pulsing
        bool newHornState = shouldHornBeOn(ctx, currentTime);
        if (newHornState != ctx.hornCurrentlyOn) {
            output.setHornState = true;
            output.hornOn = newHornState;
            ctx.hornCurrentlyOn = newHornState;
            ctx.lastHornToggleTime = currentTime;
        }
    } else {
        // Not in emergency state - ensure horn is off
        if (ctx.hornCurrentlyOn) {
            output.setHornState = true;
            output.hornOn = false;
            ctx.hornCurrentlyOn = false;
        }
    }
    
    return output;
}

// Helper function to handle button silence toggle
inline StateMachineOutput handleSilenceToggle(StateMachineContext& ctx) {
    StateMachineOutput output;
    
    if (ctx.currentState != EMERGENCY) {
        return output; // Only works in emergency state
    }
    
    ctx.notificationsSilenced = !ctx.notificationsSilenced;
    
    if (ctx.notificationsSilenced) {
        output.sendSilenceConfirmation = true;
        snprintf(output.message, sizeof(output.message),
                "Boat Monitor: Emergency alerts have been temporarily silenced");
        
        // Turn off horn immediately if it was on
        if (ctx.hornCurrentlyOn) {
            output.setHornState = true;
            output.hornOn = false;
            ctx.hornCurrentlyOn = false;
        }
    } else {
        output.sendUnsilenceConfirmation = true;
        snprintf(output.message, sizeof(output.message),
                "Boat Monitor: Emergency alerts have been re-enabled");
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
