#ifdef UNIT_TESTING

#include <unity.h>
#include <string.h>
#include <math.h>

#ifdef ARDUINO
// Include Arduino.h for ESP32 hardware testing (Serial, delay, etc.)
#include <Arduino.h>
#endif

// Include the state machine header
#include "../../include/StateMachine.h"

// Test Constants - avoid magic numbers
namespace TestConstants {
    constexpr float NORMAL_LEVEL_CM = 10.0f;
    constexpr float EMERGENCY_LEVEL_CM = 35.0f;
    constexpr float URGENT_EMERGENCY_LEVEL_CM = 55.0f;
    constexpr float EMERGENCY_THRESHOLD_CM = 30.0f;
    constexpr float URGENT_THRESHOLD_CM = 50.0f;
    // TIMEOUT_MS matches EMERGENCY_TIMEOUT_MS in StateMachine.h (5000 ms — same as live firmware)
    constexpr uint32_t TIMEOUT_MS = EMERGENCY_TIMEOUT_MS;
    constexpr uint32_t NOTIF_INTERVAL_MS = 10000;
    constexpr uint32_t HORN_DURATION_MS = 1000;
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

StateMachineContext createDefaultContext() {
    StateMachineContext ctx;
    ctx.currentState = NORMAL;
    ctx.lastStateChangeTime = 0;
    ctx.emergencyConditionsTrueTime = 0;
    ctx.emergencyConditionsFalseTime = 0;
    ctx.lastEmergencyMessageTime = 0;
    ctx.lastHornToggleTime = 0;
    ctx.emergencyConditions = false;
    ctx.urgentEmergencyConditions = false;
    ctx.hornCurrentlyOn = false;
    ctx.sensorError = false;
    ctx.configCommandReceived = false;
    ctx.notificationsSilenced = false;
    ctx.emergencyWaterLevel_cm = 30.0;
    ctx.urgentEmergencyWaterLevel_cm = 50.0;
    ctx.emergencyNotifFreq_ms = 900000; // 15 minutes
    ctx.hornOnDuration_ms = 1000;
    ctx.hornOffDuration_ms = 1000;
    return ctx;
}

// Parameterized reading creator for flexible testing
StateMachineSensorReading createReading(bool valid = true, float level_cm = TestConstants::NORMAL_LEVEL_CM) {
    StateMachineSensorReading reading;
    reading.valid = valid;
    reading.level_cm = level_cm;
    return reading;
}

// Convenience wrappers for common test scenarios
StateMachineSensorReading createNormalReading() {
    return createReading(true, TestConstants::NORMAL_LEVEL_CM);
}

StateMachineSensorReading createEmergencyReading() {
    return createReading(true, TestConstants::EMERGENCY_LEVEL_CM);
}

StateMachineSensorReading createUrgentEmergencyReading() {
    return createReading(true, TestConstants::URGENT_EMERGENCY_LEVEL_CM);
}

StateMachineSensorReading createInvalidReading() {
    return createReading(false, 0.0f);
}

// ============================================================================
// TEST: Emergency Condition Detection
// ============================================================================

void test_emergency_conditions_not_triggered_below_threshold() {
    StateMachineContext ctx = createDefaultContext();
    StateMachineSensorReading reading = createNormalReading();
    
    updateEmergencyConditions(ctx, reading, 1000);
    
    TEST_ASSERT_FALSE(ctx.emergencyConditions);
    TEST_ASSERT_FALSE(ctx.urgentEmergencyConditions);
}

void test_emergency_conditions_tier1_triggered() {
    StateMachineContext ctx = createDefaultContext();
    StateMachineSensorReading reading = createEmergencyReading();
    
    updateEmergencyConditions(ctx, reading, 1000);
    
    TEST_ASSERT_TRUE(ctx.emergencyConditions);
    TEST_ASSERT_FALSE(ctx.urgentEmergencyConditions);
    TEST_ASSERT_EQUAL(1000, ctx.emergencyConditionsTrueTime);
}

void test_emergency_conditions_tier2_triggered() {
    StateMachineContext ctx = createDefaultContext();
    StateMachineSensorReading reading = createUrgentEmergencyReading();
    
    updateEmergencyConditions(ctx, reading, 2000);
    
    TEST_ASSERT_TRUE(ctx.emergencyConditions);
    TEST_ASSERT_TRUE(ctx.urgentEmergencyConditions);
}

void test_emergency_conditions_cleared() {
    StateMachineContext ctx = createDefaultContext();
    ctx.emergencyConditions = true;
    ctx.urgentEmergencyConditions = true;
    
    StateMachineSensorReading reading = createNormalReading();
    updateEmergencyConditions(ctx, reading, 3000);
    
    TEST_ASSERT_FALSE(ctx.emergencyConditions);
    TEST_ASSERT_FALSE(ctx.urgentEmergencyConditions);
    TEST_ASSERT_EQUAL(3000, ctx.emergencyConditionsFalseTime);
}

// ============================================================================
// TEST: State Transitions - NORMAL State
// ============================================================================

void test_normal_to_emergency_with_timeout() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = NORMAL;
    ctx.emergencyConditions = true;
    ctx.emergencyConditionsTrueTime = 1000;

    StateMachineSensorReading reading = createEmergencyReading();

    // Before timeout - should stay NORMAL
    State nextState = computeNextState(ctx, reading, 1500, false);
    TEST_ASSERT_EQUAL(NORMAL, nextState);

    // After timeout (5000 ms) - should transition to EMERGENCY
    nextState = computeNextState(ctx, reading, 6001, false);
    TEST_ASSERT_EQUAL(EMERGENCY, nextState);
}

void test_normal_to_error_on_sensor_failure() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = NORMAL;
    ctx.sensorError = true;
    
    StateMachineSensorReading reading = createInvalidReading();
    
    State nextState = computeNextState(ctx, reading, 1000, false);
    TEST_ASSERT_EQUAL(ERROR, nextState);
}

void test_normal_to_config_on_button_press() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = NORMAL;
    ctx.configCommandReceived = true;
    
    StateMachineSensorReading reading = createNormalReading();
    
    State nextState = computeNextState(ctx, reading, 1000, false);
    TEST_ASSERT_EQUAL(CONFIG, nextState);
}

void test_normal_stays_normal_with_no_triggers() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = NORMAL;
    
    StateMachineSensorReading reading = createNormalReading();
    
    State nextState = computeNextState(ctx, reading, 1000, false);
    TEST_ASSERT_EQUAL(NORMAL, nextState);
}

// ============================================================================
// TEST: State Transitions - EMERGENCY State
// ============================================================================

void test_emergency_to_normal_with_timeout() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = EMERGENCY;
    ctx.emergencyConditions = false;
    ctx.emergencyConditionsFalseTime = 1000;

    StateMachineSensorReading reading = createNormalReading();

    // Before timeout - should stay EMERGENCY
    State nextState = computeNextState(ctx, reading, 1500, false);
    TEST_ASSERT_EQUAL(EMERGENCY, nextState);

    // After timeout (5000 ms) - should transition to NORMAL
    nextState = computeNextState(ctx, reading, 6001, false);
    TEST_ASSERT_EQUAL(NORMAL, nextState);
}

void test_emergency_stays_emergency_while_conditions_true() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = EMERGENCY;
    ctx.emergencyConditions = true;
    
    StateMachineSensorReading reading = createEmergencyReading();
    
    State nextState = computeNextState(ctx, reading, 5000, false);
    TEST_ASSERT_EQUAL(EMERGENCY, nextState);
}

// ============================================================================
// TEST: State Transitions - ERROR State
// ============================================================================

void test_error_to_normal_on_sensor_recovery() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = ERROR;
    ctx.sensorError = false;
    
    StateMachineSensorReading reading = createNormalReading();
    
    State nextState = computeNextState(ctx, reading, 1000, false);
    TEST_ASSERT_EQUAL(NORMAL, nextState);
}

void test_error_to_config_on_button_press() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = ERROR;
    ctx.sensorError = true;
    ctx.configCommandReceived = true;
    
    StateMachineSensorReading reading = createInvalidReading();
    
    State nextState = computeNextState(ctx, reading, 1000, false);
    TEST_ASSERT_EQUAL(CONFIG, nextState);
}

void test_error_stays_error_while_sensor_failed() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = ERROR;
    ctx.sensorError = true;
    
    StateMachineSensorReading reading = createInvalidReading();
    
    State nextState = computeNextState(ctx, reading, 1000, false);
    TEST_ASSERT_EQUAL(ERROR, nextState);
}

// ============================================================================
// TEST: State Transitions - CONFIG State
// ============================================================================

void test_config_to_normal_when_config_ends() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = CONFIG;
    ctx.configCommandReceived = false;
    
    StateMachineSensorReading reading = createNormalReading();
    
    // Config server not active - should return to NORMAL
    State nextState = computeNextState(ctx, reading, 1000, false);
    TEST_ASSERT_EQUAL(NORMAL, nextState);
}

void test_config_stays_config_while_active() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = CONFIG;
    
    StateMachineSensorReading reading = createNormalReading();
    
    // Config server still active
    State nextState = computeNextState(ctx, reading, 1000, true);
    TEST_ASSERT_EQUAL(CONFIG, nextState);
}

void test_config_to_emergency_on_flood_while_active() {
    // Safety: a browser tab left open on the config page polls /ota/status
    // every 5s, keeping configServerActive=true. Without this transition the
    // device would be pinned in CONFIG and blind to flooding indefinitely.
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = CONFIG;
    ctx.emergencyConditions = true;
    ctx.emergencyConditionsTrueTime = 1000;

    StateMachineSensorReading reading = createEmergencyReading();

    // Before EMERGENCY_TIMEOUT_MS — still CONFIG (debounced)
    State nextState = computeNextState(ctx, reading, 1500, true);
    TEST_ASSERT_EQUAL(CONFIG, nextState);

    // After timeout — must transition to EMERGENCY even with config active
    nextState = computeNextState(ctx, reading, 6001, true);
    TEST_ASSERT_EQUAL(EMERGENCY, nextState);
}

void test_config_to_error_on_sensor_failure_while_active() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = CONFIG;
    ctx.sensorError = true;

    StateMachineSensorReading reading = createInvalidReading();

    State nextState = computeNextState(ctx, reading, 1000, true);
    TEST_ASSERT_EQUAL(ERROR, nextState);
}

void test_config_exits_after_server_timeout_via_full_update() {
    // Regression test for the infinite-config bug: user presses the button,
    // configCommandReceived triggers NORMAL→CONFIG, but no client ever
    // connects. The web server times out and stops. The state machine must
    // then exit CONFIG → NORMAL instead of restarting the server forever.
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = NORMAL;
    ctx.configCommandReceived = true; // User pressed the button

    StateMachineSensorReading reading = createNormalReading();

    // Step 1: Full update transitions NORMAL → CONFIG
    StateMachineOutput out = updateStateMachine(ctx, reading, 1000, 0.0f, false);
    TEST_ASSERT_TRUE(out.stateChanged);
    TEST_ASSERT_EQUAL(CONFIG, ctx.currentState);

    // Step 2: Next update — the in-CONFIG clear consumes the entry flag,
    // server starts and stays active for a while
    out = updateStateMachine(ctx, reading, 2000, 0.0f, true);
    TEST_ASSERT_EQUAL(CONFIG, ctx.currentState);
    TEST_ASSERT_FALSE(ctx.configCommandReceived);

    // Step 3: Server times out (no client connected) — configServerActive=false
    out = updateStateMachine(ctx, reading, 245000, 0.0f, false);
    TEST_ASSERT_TRUE(out.stateChanged);
    TEST_ASSERT_EQUAL(NORMAL, ctx.currentState);
}

void test_config_mid_session_button_press_does_not_block_timeout_exit() {
    // Regression: a button press landing mid-CONFIG sets configCommandReceived
    // again after entry already consumed it. Without the in-CONFIG clear, that
    // flag stays true forever and blocks the idle-timeout exit — the server
    // restarts on every loop iteration, same infinite-config bug.
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = NORMAL;
    ctx.configCommandReceived = true; // Initial press to enter CONFIG

    StateMachineSensorReading reading = createNormalReading();

    // Enter CONFIG
    updateStateMachine(ctx, reading, 1000, 0.0f, false);
    TEST_ASSERT_EQUAL(CONFIG, ctx.currentState);

    // Server starts and runs; the in-CONFIG clear consumes the entry flag
    updateStateMachine(ctx, reading, 2000, 0.0f, true);
    TEST_ASSERT_EQUAL(CONFIG, ctx.currentState);
    TEST_ASSERT_FALSE(ctx.configCommandReceived);

    // User presses the button mid-session (ISR sets the flag again)
    ctx.configCommandReceived = true;

    // Next update: flag must be consumed while in CONFIG, session continues
    StateMachineOutput out = updateStateMachine(ctx, reading, 3000, 0.0f, true);
    TEST_ASSERT_EQUAL(CONFIG, ctx.currentState);
    TEST_ASSERT_FALSE(ctx.configCommandReceived);

    // Server later times out with no client activity — exit must not be blocked
    out = updateStateMachine(ctx, reading, 245000, 0.0f, false);
    TEST_ASSERT_TRUE(out.stateChanged);
    TEST_ASSERT_EQUAL(NORMAL, ctx.currentState);
}

// ============================================================================
// TEST: Emergency Notifications
// ============================================================================

void test_emergency_notification_not_sent_outside_emergency() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = NORMAL;
    
    bool shouldSend = shouldSendEmergencyNotification(ctx, 1000);
    TEST_ASSERT_FALSE(shouldSend);
}

void test_emergency_notification_sent_after_interval() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = EMERGENCY;
    ctx.lastEmergencyMessageTime = 1000;
    ctx.emergencyNotifFreq_ms = 10000; // 10 seconds
    
    // Before interval - should not send
    bool shouldSend = shouldSendEmergencyNotification(ctx, 5000);
    TEST_ASSERT_FALSE(shouldSend);
    
    // After interval - should send
    shouldSend = shouldSendEmergencyNotification(ctx, 11001);
    TEST_ASSERT_TRUE(shouldSend);
}

void test_emergency_notification_sent_immediately_after_boot() {
    // Regression: lastEmergencyMessageTime is initialized to 0 in setup().
    // The elapsed-time check alone would suppress the first alert until
    // emergencyNotifFreq_ms elapses since boot — but the owner needs to know
    // the moment a flood is detected, not 15 minutes later.
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = EMERGENCY;
    ctx.lastEmergencyMessageTime = 0; // Never sent — boot condition
    ctx.emergencyNotifFreq_ms = 900000; // 15 minutes

    // 1 second after boot — must send immediately, not wait 15 minutes
    bool shouldSend = shouldSendEmergencyNotification(ctx, 1000);
    TEST_ASSERT_TRUE(shouldSend);
}

void test_emergency_notification_not_sent_when_silenced() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = EMERGENCY;
    ctx.notificationsSilenced = true;
    ctx.lastEmergencyMessageTime = 0;
    ctx.emergencyNotifFreq_ms = 10000;
    
    bool shouldSend = shouldSendEmergencyNotification(ctx, 20000);
    TEST_ASSERT_FALSE(shouldSend);
}

// ============================================================================
// TEST: Horn Control
// ============================================================================

void test_horn_off_outside_emergency() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = NORMAL;
    ctx.hornCurrentlyOn = false;
    
    bool hornOn = shouldHornBeOn(ctx, 1000);
    TEST_ASSERT_FALSE(hornOn);
}

void test_horn_off_without_urgent_conditions() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = EMERGENCY;
    ctx.urgentEmergencyConditions = false;
    
    bool hornOn = shouldHornBeOn(ctx, 1000);
    TEST_ASSERT_FALSE(hornOn);
}

void test_horn_off_when_silenced() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = EMERGENCY;
    ctx.urgentEmergencyConditions = true;
    ctx.notificationsSilenced = true;
    
    bool hornOn = shouldHornBeOn(ctx, 1000);
    TEST_ASSERT_FALSE(hornOn);
}

void test_horn_pulses_in_urgent_emergency() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = EMERGENCY;
    ctx.urgentEmergencyConditions = true;
    ctx.notificationsSilenced = false;
    ctx.hornCurrentlyOn = false;
    ctx.lastHornToggleTime = 1000;
    ctx.hornOnDuration_ms = 1000;
    ctx.hornOffDuration_ms = 1000;
    
    // Before toggle time - should stay off
    bool hornOn = shouldHornBeOn(ctx, 1500);
    TEST_ASSERT_FALSE(hornOn);
    
    // After toggle time - should turn on
    hornOn = shouldHornBeOn(ctx, 2001);
    TEST_ASSERT_TRUE(hornOn);
}

// ============================================================================
// TEST: Alert Pin (GPIO 26) — dedicated emergency indicator
// ============================================================================

void test_alert_pin_off_outside_emergency() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = NORMAL;

    TEST_ASSERT_FALSE(computeAlertPinState(ctx));
}

void test_alert_pin_solid_in_tier1_emergency() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = EMERGENCY;
    ctx.urgentEmergencyConditions = false;

    TEST_ASSERT_TRUE(computeAlertPinState(ctx));
}

void test_alert_pin_follows_horn_in_tier2_emergency() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = EMERGENCY;
    ctx.urgentEmergencyConditions = true;
    ctx.hornCurrentlyOn = false;

    TEST_ASSERT_FALSE(computeAlertPinState(ctx));

    ctx.hornCurrentlyOn = true;
    TEST_ASSERT_TRUE(computeAlertPinState(ctx));
}

void test_alert_pin_off_when_silenced() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = EMERGENCY;
    ctx.urgentEmergencyConditions = false;
    ctx.notificationsSilenced = true;

    TEST_ASSERT_FALSE(computeAlertPinState(ctx));
}

// ============================================================================
// TEST: Full State Machine Update
// ============================================================================

void test_full_update_normal_to_emergency() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = NORMAL;
    
    // First update with emergency reading - conditions become true
    StateMachineSensorReading reading = createEmergencyReading();
    StateMachineOutput output = updateStateMachine(ctx, reading, 1000, NAN, false);

    TEST_ASSERT_FALSE(output.stateChanged); // Not yet, need timeout
    TEST_ASSERT_TRUE(ctx.emergencyConditions);
    
    // Second update after timeout - should transition
    output = updateStateMachine(ctx, reading, 6001, NAN, false);
    
    TEST_ASSERT_TRUE(output.stateChanged);
    TEST_ASSERT_EQUAL(EMERGENCY, output.newState);
    TEST_ASSERT_EQUAL(EMERGENCY, ctx.currentState);
}

void test_full_update_emergency_notification() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = EMERGENCY;
    ctx.emergencyConditions = true;
    ctx.lastEmergencyMessageTime = 0;
    ctx.emergencyNotifFreq_ms = 10000;
    
    StateMachineSensorReading reading = createEmergencyReading();
    StateMachineOutput output = updateStateMachine(ctx, reading, 10001, NAN, false);

    TEST_ASSERT_TRUE(output.sendEmergencyNotification);
    // Caller constructs the message from output.displayLevel_cm / output.sensorFaultActive
    TEST_ASSERT_EQUAL(10001, ctx.lastEmergencyMessageTime);
}

void test_full_update_horn_activation() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = EMERGENCY;
    ctx.urgentEmergencyConditions = true;
    ctx.hornCurrentlyOn = false;
    ctx.lastHornToggleTime = 1000;
    ctx.hornOnDuration_ms = 1000;
    
    StateMachineSensorReading reading = createUrgentEmergencyReading();
    StateMachineOutput output = updateStateMachine(ctx, reading, 2001, NAN, false);

    TEST_ASSERT_TRUE(output.setHornState);
    TEST_ASSERT_TRUE(output.hornOn);
    TEST_ASSERT_TRUE(ctx.hornCurrentlyOn);
    TEST_ASSERT_TRUE(output.alertPinOn); // GPIO 26 mirrors the horn while pulsing
}

// ============================================================================
// TEST: Silence Toggle
// ============================================================================

void test_silence_toggle_enables_silence() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = EMERGENCY;
    ctx.notificationsSilenced = false;
    
    StateMachineOutput output = handleSilenceToggle(ctx);
    
    TEST_ASSERT_TRUE(ctx.notificationsSilenced);
    TEST_ASSERT_TRUE(output.sendSilenceConfirmation);
    // Note: handleSilenceToggle's contract is the boolean flags only; the
    // confirmation text is built by the caller (main.cpp), not in output.message.
}

void test_silence_toggle_disables_silence() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = EMERGENCY;
    ctx.notificationsSilenced = true;
    
    StateMachineOutput output = handleSilenceToggle(ctx);
    
    TEST_ASSERT_FALSE(ctx.notificationsSilenced);
    TEST_ASSERT_TRUE(output.sendUnsilenceConfirmation);
}

void test_silence_toggle_turns_off_horn() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = EMERGENCY;
    ctx.notificationsSilenced = false;
    ctx.hornCurrentlyOn = true;
    
    StateMachineOutput output = handleSilenceToggle(ctx);
    
    TEST_ASSERT_TRUE(output.setHornState);
    TEST_ASSERT_FALSE(output.hornOn);
    TEST_ASSERT_FALSE(ctx.hornCurrentlyOn);
}

void test_silence_toggle_only_works_in_emergency() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = NORMAL;
    ctx.notificationsSilenced = false;
    
    StateMachineOutput output = handleSilenceToggle(ctx);
    
    TEST_ASSERT_FALSE(ctx.notificationsSilenced);
    TEST_ASSERT_FALSE(output.sendSilenceConfirmation);
}

// ============================================================================
// TEST: Auto-clear Silence on Exit
// ============================================================================

void test_silence_cleared_on_return_to_normal() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = EMERGENCY;
    ctx.notificationsSilenced = true;
    ctx.emergencyConditions = false;
    ctx.emergencyConditionsFalseTime = 1000;
    
    StateMachineSensorReading reading = createNormalReading();
    StateMachineOutput output = updateStateMachine(ctx, reading, 6001, NAN, false);

    TEST_ASSERT_TRUE(output.stateChanged);
    TEST_ASSERT_EQUAL(NORMAL, ctx.currentState);
    TEST_ASSERT_FALSE(ctx.notificationsSilenced);
}

// ============================================================================
// TEST: State to String Helper
// ============================================================================

void test_state_to_string() {
    TEST_ASSERT_EQUAL_STRING("NORMAL", stateToString(NORMAL));
    TEST_ASSERT_EQUAL_STRING("EMERGENCY", stateToString(EMERGENCY));
    TEST_ASSERT_EQUAL_STRING("ERROR", stateToString(ERROR));
    TEST_ASSERT_EQUAL_STRING("CONFIG", stateToString(CONFIG));
}

// ============================================================================
// TEST RUNNER
// ============================================================================

void setUp(void) {
    // Set up before each test - ensure clean state
    // No mock state to reset for state machine tests currently,
    // but this ensures consistency if mocks are added later
}

void tearDown(void) {
    // Clean up after each test
}

// Helper function to run all tests
void runAllTests() {
    // Emergency condition detection tests
    RUN_TEST(test_emergency_conditions_not_triggered_below_threshold);
    RUN_TEST(test_emergency_conditions_tier1_triggered);
    RUN_TEST(test_emergency_conditions_tier2_triggered);
    RUN_TEST(test_emergency_conditions_cleared);
    
    // NORMAL state transition tests
    RUN_TEST(test_normal_to_emergency_with_timeout);
    RUN_TEST(test_normal_to_error_on_sensor_failure);
    RUN_TEST(test_normal_to_config_on_button_press);
    RUN_TEST(test_normal_stays_normal_with_no_triggers);
    
    // EMERGENCY state transition tests
    RUN_TEST(test_emergency_to_normal_with_timeout);
    RUN_TEST(test_emergency_stays_emergency_while_conditions_true);
    
    // ERROR state transition tests
    RUN_TEST(test_error_to_normal_on_sensor_recovery);
    RUN_TEST(test_error_to_config_on_button_press);
    RUN_TEST(test_error_stays_error_while_sensor_failed);
    
    // CONFIG state transition tests
    RUN_TEST(test_config_to_normal_when_config_ends);
    RUN_TEST(test_config_stays_config_while_active);
    RUN_TEST(test_config_to_emergency_on_flood_while_active);
    RUN_TEST(test_config_to_error_on_sensor_failure_while_active);
    RUN_TEST(test_config_exits_after_server_timeout_via_full_update);
    RUN_TEST(test_config_mid_session_button_press_does_not_block_timeout_exit);
    
    // Emergency notification tests
    RUN_TEST(test_emergency_notification_not_sent_outside_emergency);
    RUN_TEST(test_emergency_notification_sent_after_interval);
    RUN_TEST(test_emergency_notification_sent_immediately_after_boot);
    RUN_TEST(test_emergency_notification_not_sent_when_silenced);
    
    // Horn control tests
    RUN_TEST(test_horn_off_outside_emergency);
    RUN_TEST(test_horn_off_without_urgent_conditions);
    RUN_TEST(test_horn_off_when_silenced);
    RUN_TEST(test_horn_pulses_in_urgent_emergency);

    // Alert pin (GPIO 26) tests
    RUN_TEST(test_alert_pin_off_outside_emergency);
    RUN_TEST(test_alert_pin_solid_in_tier1_emergency);
    RUN_TEST(test_alert_pin_follows_horn_in_tier2_emergency);
    RUN_TEST(test_alert_pin_off_when_silenced);

    // Full state machine update tests
    RUN_TEST(test_full_update_normal_to_emergency);
    RUN_TEST(test_full_update_emergency_notification);
    RUN_TEST(test_full_update_horn_activation);
    
    // Silence toggle tests
    RUN_TEST(test_silence_toggle_enables_silence);
    RUN_TEST(test_silence_toggle_disables_silence);
    RUN_TEST(test_silence_toggle_turns_off_horn);
    RUN_TEST(test_silence_toggle_only_works_in_emergency);
    
    // Auto-clear silence tests
    RUN_TEST(test_silence_cleared_on_return_to_normal);
    
    // Helper function tests
    RUN_TEST(test_state_to_string);
}

#ifdef ARDUINO
// ESP32 hardware testing - uses setup() and loop()
void setup() {
    // Initialize serial for test output
    Serial.begin(115200);
    delay(2000);  // Wait for serial connection to stabilize
    
    // Start Unity testing framework
    UNITY_BEGIN();
    
    // Run all tests
    runAllTests();
    
    // End Unity testing
    UNITY_END();
}

void loop() {
    // Tests run once in setup(), nothing to do in loop
}
#else
// Native testing - uses main()
int main(int argc, char **argv) {
    UNITY_BEGIN();
    runAllTests();
    return UNITY_END();
}
#endif

#endif // UNIT_TESTING
