#ifdef UNIT_TESTING

#include <unity.h>
#include <algorithm>

// Only include mocks for native testing, not for ESP32 hardware testing
#ifndef ARDUINO
#include "../mocks/MockArduino.h"
#include "../mocks/MockADS1115.h"
#include "../mocks/MockTimeManagement.h"

// Prevent including the real Adafruit library for native tests
#define Adafruit_ADS1X15_h
#endif

// Include the sensor header
#include "../../include/WaterPressureSensor.h"

// Friend class declaration for testing private methods
#ifdef ARDUINO
// On ESP32, we can only test public methods
#define SENSOR_TEST_FRIEND
#else
// On native, declare test functions as friends
#define SENSOR_TEST_FRIEND friend
#endif

// Test Constants - avoid magic numbers
namespace TestConstants {
    constexpr int DEFAULT_ZERO_MILLIVOLTS = 590;
    constexpr int MID_RANGE_MILLIVOLTS = 1590;
    constexpr int MAX_RANGE_MILLIVOLTS = 4096;
    constexpr int CUSTOM_ZERO_MILLIVOLTS = 500;
    constexpr int TWO_POINT_FIRST_MV = 500;
    constexpr int TWO_POINT_SECOND_MV = 2500;
    constexpr float TWO_POINT_SECOND_CM = 50.0f;
    constexpr float TOLERANCE_CM = 0.1f;
    constexpr float TOLERANCE_CM_WIDE = 1.0f;
}

// We need to create a test-friendly version of the sensor class
// Since we're testing the logic, we'll test the conversion functions directly

// ============================================================================
// TEST: Single Point Calibration
// ============================================================================

void test_single_point_calibration_zero_point() {
    WaterPressureSensor sensor(true); // Mock mode
    sensor.setZeroLevelMilliVolts(TestConstants::DEFAULT_ZERO_MILLIVOLTS);
    
    // At zero point, should read 0 cm
    float level = sensor.voltageToCentimeters(TestConstants::DEFAULT_ZERO_MILLIVOLTS);
    TEST_ASSERT_FLOAT_WITHIN(TestConstants::TOLERANCE_CM, 0.0f, level);
}

void test_single_point_calibration_mid_range() {
    WaterPressureSensor sensor(true);
    sensor.setZeroLevelMilliVolts(TestConstants::DEFAULT_ZERO_MILLIVOLTS);
    
    // Test a mid-range value
    // With default calibration: (4096 - 590) / 100 = 35.06 mV per cm
    // At 1590 mV: (1590 - 590) / 35.06 ≈ 28.5 cm
    float level = sensor.voltageToCentimeters(TestConstants::MID_RANGE_MILLIVOLTS);
    TEST_ASSERT_FLOAT_WITHIN(TestConstants::TOLERANCE_CM_WIDE, 28.5f, level);
}

void test_single_point_calibration_max_range() {
    WaterPressureSensor sensor(true);
    sensor.setZeroLevelMilliVolts(TestConstants::DEFAULT_ZERO_MILLIVOLTS);
    
    // Test max voltage (should be around 100 cm)
    float level = sensor.voltageToCentimeters(TestConstants::MAX_RANGE_MILLIVOLTS);
    TEST_ASSERT_FLOAT_WITHIN(TestConstants::TOLERANCE_CM_WIDE, 100.0f, level);
}

void test_single_point_calibration_custom_zero() {
    WaterPressureSensor sensor(true);
    sensor.setZeroLevelMilliVolts(TestConstants::CUSTOM_ZERO_MILLIVOLTS);
    
    // At custom zero point, should read 0 cm
    float level = sensor.voltageToCentimeters(TestConstants::CUSTOM_ZERO_MILLIVOLTS);
    TEST_ASSERT_FLOAT_WITHIN(TestConstants::TOLERANCE_CM, 0.0f, level);
}

// ============================================================================
// TEST: Two Point Calibration
// ============================================================================

void test_two_point_calibration_basic() {
    WaterPressureSensor sensor(true);
    
    // Set up 2-point calibration
    // Point 0: 500 mV = 0 cm
    // Point 1: 2500 mV = 50 cm
    sensor.setCalibrationPoint(0, TestConstants::TWO_POINT_FIRST_MV, 0.0f);
    sensor.setCalibrationPoint(1, TestConstants::TWO_POINT_SECOND_MV, TestConstants::TWO_POINT_SECOND_CM);
    
    TEST_ASSERT_TRUE(sensor.hasTwoPointCalibration());
    TEST_ASSERT_EQUAL(TestConstants::TWO_POINT_FIRST_MV, sensor.getZeroPointMilliVolts());
    TEST_ASSERT_EQUAL(TestConstants::TWO_POINT_SECOND_MV, sensor.getSecondPointMilliVolts());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, TestConstants::TWO_POINT_SECOND_CM, sensor.getSecondPointLevelCm());
}

void test_two_point_calibration_zero_point() {
    WaterPressureSensor sensor(true);
    sensor.setCalibrationPoint(0, TestConstants::TWO_POINT_FIRST_MV, 0.0f);
    sensor.setCalibrationPoint(1, TestConstants::TWO_POINT_SECOND_MV, TestConstants::TWO_POINT_SECOND_CM);
    
    // At first calibration point, should read 0 cm
    float level = sensor.voltageToCentimeters(TestConstants::TWO_POINT_FIRST_MV);
    TEST_ASSERT_FLOAT_WITHIN(TestConstants::TOLERANCE_CM, 0.0f, level);
}

void test_two_point_calibration_second_point() {
    WaterPressureSensor sensor(true);
    sensor.setCalibrationPoint(0, TestConstants::TWO_POINT_FIRST_MV, 0.0f);
    sensor.setCalibrationPoint(1, TestConstants::TWO_POINT_SECOND_MV, TestConstants::TWO_POINT_SECOND_CM);
    
    // At second calibration point, should read 50 cm
    float level = sensor.voltageToCentimeters(TestConstants::TWO_POINT_SECOND_MV);
    TEST_ASSERT_FLOAT_WITHIN(TestConstants::TOLERANCE_CM, TestConstants::TWO_POINT_SECOND_CM, level);
}

void test_two_point_calibration_interpolation() {
    WaterPressureSensor sensor(true);
    sensor.setCalibrationPoint(0, TestConstants::TWO_POINT_FIRST_MV, 0.0f);
    sensor.setCalibrationPoint(1, TestConstants::TWO_POINT_SECOND_MV, TestConstants::TWO_POINT_SECOND_CM);
    
    // Test midpoint: 1500 mV should be 25 cm
    // Linear interpolation: (1500 - 500) * (50 / (2500 - 500)) = 1000 * 0.025 = 25
    constexpr int midpoint_mv = (TestConstants::TWO_POINT_FIRST_MV + TestConstants::TWO_POINT_SECOND_MV) / 2;
    float level = sensor.voltageToCentimeters(midpoint_mv);
    TEST_ASSERT_FLOAT_WITHIN(TestConstants::TOLERANCE_CM, 25.0f, level);
}

void test_two_point_calibration_extrapolation_below() {
    WaterPressureSensor sensor(true);
    sensor.setCalibrationPoint(0, TestConstants::TWO_POINT_FIRST_MV, 0.0f);
    sensor.setCalibrationPoint(1, TestConstants::TWO_POINT_SECOND_MV, TestConstants::TWO_POINT_SECOND_CM);
    
    // Test below zero point: 400 mV should give negative value
    constexpr int below_zero_mv = TestConstants::TWO_POINT_FIRST_MV - 100;
    float level = sensor.voltageToCentimeters(below_zero_mv);
    TEST_ASSERT_TRUE(level < 0.0f);
}

void test_two_point_calibration_extrapolation_above() {
    WaterPressureSensor sensor(true);
    sensor.setCalibrationPoint(0, TestConstants::TWO_POINT_FIRST_MV, 0.0f);
    sensor.setCalibrationPoint(1, TestConstants::TWO_POINT_SECOND_MV, TestConstants::TWO_POINT_SECOND_CM);
    
    // Test above second point: 3500 mV should give > 50 cm
    constexpr int above_max_mv = TestConstants::TWO_POINT_SECOND_MV + 1000;
    float level = sensor.voltageToCentimeters(above_max_mv);
    TEST_ASSERT_TRUE(level > TestConstants::TWO_POINT_SECOND_CM);
    TEST_ASSERT_FLOAT_WITHIN(TestConstants::TOLERANCE_CM_WIDE, 75.0f, level); // Should be approximately 75 cm
}

// ============================================================================
// TEST: Median Filtering (requires access to internal buffer)
// ============================================================================

// Note: Testing median filtering requires either:
// 1. Making calculateMedianFromBuffer public or protected
// 2. Testing it indirectly through readLevel()
// 3. Creating a friend test class
// For now, we'll test it indirectly through multiple reads

void test_median_filter_basic_reading() {
    WaterPressureSensor sensor(true);
    sensor.setZeroLevelMilliVolts(TestConstants::CUSTOM_ZERO_MILLIVOLTS);
    
    // The sensor uses mock mode with random values
    // Verify that we can get valid readings from the mock sensor
    SensorReading reading = sensor.readLevel();
    
    // In mock mode, sensor should return valid readings
    TEST_ASSERT_TRUE(reading.valid);
    // Mock sensor generates values in a reasonable range (4-20 cm typically)
    TEST_ASSERT_GREATER_OR_EQUAL(0.0f, reading.level_cm);
    TEST_ASSERT_LESS_OR_EQUAL(100.0f, reading.level_cm);
}

// ============================================================================
// TEST: Edge Cases and Error Handling
// ============================================================================

void test_voltage_conversion_negative_voltage() {
    WaterPressureSensor sensor(true);
    sensor.setZeroLevelMilliVolts(TestConstants::CUSTOM_ZERO_MILLIVOLTS);
    
    // Negative voltage should give negative water level
    float level = sensor.voltageToCentimeters(-100);
    TEST_ASSERT_TRUE(level < 0.0f);
}

void test_voltage_conversion_zero_voltage() {
    WaterPressureSensor sensor(true);
    sensor.setZeroLevelMilliVolts(TestConstants::CUSTOM_ZERO_MILLIVOLTS);
    
    // Zero voltage with 500mV zero point should give negative reading
    float level = sensor.voltageToCentimeters(0);
    TEST_ASSERT_TRUE(level < 0.0f);
}

void test_voltage_conversion_very_high_voltage() {
    WaterPressureSensor sensor(true);
    sensor.setZeroLevelMilliVolts(TestConstants::CUSTOM_ZERO_MILLIVOLTS);
    
    // Very high voltage should still work (extrapolation)
    constexpr int very_high_mv = 10000;
    float level = sensor.voltageToCentimeters(very_high_mv);
    TEST_ASSERT_TRUE(level > 100.0f); // Beyond normal range
}

void test_calibration_point_overwrite() {
    WaterPressureSensor sensor(true);
    
    // Set initial calibration
    sensor.setCalibrationPoint(0, TestConstants::CUSTOM_ZERO_MILLIVOLTS, 0.0f);
    TEST_ASSERT_EQUAL(TestConstants::CUSTOM_ZERO_MILLIVOLTS, sensor.getZeroPointMilliVolts());
    
    // Overwrite zero point
    constexpr int new_zero_mv = 600;
    sensor.setCalibrationPoint(0, new_zero_mv, 0.0f);
    TEST_ASSERT_EQUAL(new_zero_mv, sensor.getZeroPointMilliVolts());
}

void test_two_point_with_identical_voltages() {
    WaterPressureSensor sensor(true);
    
    // Edge case: both points at same voltage (division by zero protection)
    constexpr int same_voltage_mv = 1000;
    sensor.setCalibrationPoint(0, same_voltage_mv, 0.0f);
    sensor.setCalibrationPoint(1, same_voltage_mv, 50.0f);
    
    // Should fallback to single-point calibration behavior
    // At the calibration voltage, should read near 0 cm (first point takes precedence)
    float level = sensor.voltageToCentimeters(same_voltage_mv);
    // Implementation should handle this gracefully - expect first point behavior
    // or fallback to single-point. Either way, result should be finite and reasonable.
    TEST_ASSERT_FALSE(level != level); // Check not NaN
    TEST_ASSERT_FALSE(level == level + 1.0f); // Check not Inf
    TEST_ASSERT_FLOAT_WITHIN(10.0f, 0.0f, level); // Should be near first calibration point level
}

// ============================================================================
// TEST: Reading Validation
// ============================================================================

void test_reading_structure() {
    WaterPressureSensor sensor(true);
    SensorReading reading = sensor.readLevel();
    
    // Verify reading structure is populated
    // Mock sensor should return valid readings
    TEST_ASSERT_TRUE(reading.valid);
    // Mock sensor generates values in reasonable range
    TEST_ASSERT_GREATER_OR_EQUAL(0.0f, reading.level_cm);
    TEST_ASSERT_LESS_OR_EQUAL(100.0f, reading.level_cm);
    // Verify millivolts field is populated
    TEST_ASSERT_GREATER_THAN(0.0f, reading.millivolts);
}

// ============================================================================
// TEST RUNNER
// ============================================================================

void setUp(void) {
    // Set up before each test - reset all mock state
#ifndef ARDUINO
    // Only reset mock state for native tests
    setMockMillis(0);
    TimeManagement::getInstance().setMockNTPSynced(false);
    TimeManagement::getInstance().setMockUnixTime(0);
    TimeManagement::getInstance().setMockTimeSinceBoot(0);
#endif
    // For ESP32 tests, no special setup needed
}

void tearDown(void) {
    // Clean up after each test
}

// Helper function to run all tests
void runAllTests() {
    // Single point calibration tests
    RUN_TEST(test_single_point_calibration_zero_point);
    RUN_TEST(test_single_point_calibration_mid_range);
    RUN_TEST(test_single_point_calibration_max_range);
    RUN_TEST(test_single_point_calibration_custom_zero);
    
    // Two point calibration tests
    RUN_TEST(test_two_point_calibration_basic);
    RUN_TEST(test_two_point_calibration_zero_point);
    RUN_TEST(test_two_point_calibration_second_point);
    RUN_TEST(test_two_point_calibration_interpolation);
    RUN_TEST(test_two_point_calibration_extrapolation_below);
    RUN_TEST(test_two_point_calibration_extrapolation_above);
    
    // Median filtering tests
    RUN_TEST(test_median_filter_basic_reading);
    
    // Edge cases
    RUN_TEST(test_voltage_conversion_negative_voltage);
    RUN_TEST(test_voltage_conversion_zero_voltage);
    RUN_TEST(test_voltage_conversion_very_high_voltage);
    RUN_TEST(test_calibration_point_overwrite);
    RUN_TEST(test_two_point_with_identical_voltages);
    
    // Reading validation
    RUN_TEST(test_reading_structure);
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
