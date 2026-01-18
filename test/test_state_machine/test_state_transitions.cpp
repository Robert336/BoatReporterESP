#ifdef UNIT_TESTING

#include <unity.h>
#include <string.h>

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
    constexpr uint32_t TIMEOUT_MS = 1000;
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
    
    // After timeout - should transition to EMERGENCY
    nextState = computeNextState(ctx, reading, 2001, false);
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
    
    // After timeout - should transition to NORMAL
    nextState = computeNextState(ctx, reading, 2001, false);
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
// TEST: Full State Machine Update
// ============================================================================

void test_full_update_normal_to_emergency() {
    StateMachineContext ctx = createDefaultContext();
    ctx.currentState = NORMAL;
    
    // First update with emergency reading - conditions become true
    StateMachineSensorReading reading = createEmergencyReading();
    StateMachineOutput output = updateStateMachine(ctx, reading, 1000, false);
    
    TEST_ASSERT_FALSE(output.stateChanged); // Not yet, need timeout
    TEST_ASSERT_TRUE(ctx.emergencyConditions);
    
    // Second update after timeout - should transition
    output = updateStateMachine(ctx, reading, 2001, false);
    
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
    StateMachineOutput output = updateStateMachine(ctx, reading, 10001, false);
    
    TEST_ASSERT_TRUE(output.sendEmergencyNotification);
    TEST_ASSERT_TRUE(strlen(output.message) > 0);
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
    StateMachineOutput output = updateStateMachine(ctx, reading, 2001, false);
    
    TEST_ASSERT_TRUE(output.setHornState);
    TEST_ASSERT_TRUE(output.hornOn);
    TEST_ASSERT_TRUE(ctx.hornCurrentlyOn);
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
    TEST_ASSERT_TRUE(strlen(output.message) > 0);
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
    StateMachineOutput output = updateStateMachine(ctx, reading, 2001, false);
    
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
    
    // Emergency notification tests
    RUN_TEST(test_emergency_notification_not_sent_outside_emergency);
    RUN_TEST(test_emergency_notification_sent_after_interval);
    RUN_TEST(test_emergency_notification_not_sent_when_silenced);
    
    // Horn control tests
    RUN_TEST(test_horn_off_outside_emergency);
    RUN_TEST(test_horn_off_without_urgent_conditions);
    RUN_TEST(test_horn_off_when_silenced);
    RUN_TEST(test_horn_pulses_in_urgent_emergency);
    
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
