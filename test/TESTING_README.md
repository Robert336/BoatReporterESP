# Unit Testing Guide for Boat Reporter ESP32

This guide explains how to run and maintain unit tests for the Boat Reporter ESP32 project. The tests are designed to run **without any physical hardware**, allowing rapid development and regression testing.

## Table of Contents

- [Overview](#overview)
- [Test Structure](#test-structure)
- [Running Tests](#running-tests)
- [Test Coverage](#test-coverage)
- [Writing New Tests](#writing-new-tests)
- [Troubleshooting](#troubleshooting)

## Overview

The unit testing setup uses **PlatformIO's native test framework** with Unity as the test runner. Tests execute on your development machine (native platform) rather than on the ESP32 hardware.

### Benefits

- **No hardware required**: Test without connecting ESP32, sensors, or network
- **Fast feedback**: Tests complete in seconds
- **Automated regression testing**: Catch bugs before deployment
- **CI/CD ready**: Can integrate with automated build pipelines
- **Refactoring confidence**: Verify behavior during code changes

### What is Tested

1. **Sensor Logic** (`test/test_sensor/`)
   - Voltage-to-centimeters conversion
   - Single-point and two-point calibration
   - Median filtering
   - Invalid reading detection

2. **State Machine** (`test/test_state_machine/`)
   - All state transitions (NORMAL, EMERGENCY, ERROR, CONFIG)
   - Emergency condition detection (Tier 1 and Tier 2)
   - Notification timing and silencing
   - Horn control and pulsing

## Test Structure

```
test/
├── mocks/
│   ├── MockArduino.h           # Arduino framework mocks
│   ├── MockADS1115.h          # ADC sensor mocks
│   └── MockTimeManagement.h   # Time/RTC mocks
├── test_sensor/
│   └── test_sensor_logic.cpp  # Sensor calibration tests
└── test_state_machine/
    └── test_state_transitions.cpp  # State machine tests
```

### Mock Infrastructure

The `mocks/` directory contains header-only mock implementations of hardware dependencies:

- **MockArduino.h**: Provides `millis()`, `digitalWrite()`, `Serial`, etc.
- **MockADS1115.h**: Simulates the ADS1115 ADC without I2C hardware
- **MockTimeManagement.h**: Mocks NTP time synchronization

These mocks allow the core logic to compile and run on your development machine.

## Running Tests

### Prerequisites

**Option 1: Native Testing (Fastest)**
- PlatformIO Core or PlatformIO IDE
- **GCC/G++ compiler for native testing** (Windows users: install MinGW-w64 or MSYS2)
  - Download MinGW-w64: https://winlibs.com/
  - Or use MSYS2: https://www.msys2.org/
  - Add to PATH: `C:\mingw64\bin` or `C:\msys64\mingw64\bin`
- No additional dependencies required (Unity is built-in)

**Option 2: ESP32 Hardware Testing (No GCC Required)**
- PlatformIO Core or PlatformIO IDE
- ESP32 board connected via USB
- No GCC installation needed - uses ESP32 toolchain

**Note for Windows users**: If you don't want to install GCC, see [`TESTING_WITHOUT_GCC.md`](TESTING_WITHOUT_GCC.md) for alternative testing approaches, or use the ESP32 hardware testing method below.

### Run All Tests (Native - Fast, Requires GCC)

```bash
pio test -e native
```

### Run All Tests (ESP32 Hardware - No GCC Required)

```bash
pio test -e esp32-test
```

This will:
1. Compile the tests for ESP32
2. Upload to your connected ESP32
3. Run the tests on the hardware
4. Display results in serial monitor

### Run Specific Test Suite

```bash
# Run only sensor tests (native)
pio test -e native -f test_sensor_logic

# Run only state machine tests (ESP32 hardware)
pio test -e esp32-test -f test_state_transitions
```

### Verbose Output

```bash
pio test -e native -v
```

### Example Output

```
Testing...
test/test_sensor/test_sensor_logic.cpp:42:test_single_point_calibration_zero_point [PASSED]
test/test_sensor/test_sensor_logic.cpp:50:test_single_point_calibration_mid_range [PASSED]
test/test_sensor/test_sensor_logic.cpp:58:test_two_point_calibration_basic [PASSED]
...
-----------------------
23 Tests 0 Failures 0 Ignored
OK
```

## Test Coverage

### Sensor Logic Tests (23 tests)

#### Single-Point Calibration
- ✅ Zero point validation
- ✅ Mid-range conversion accuracy
- ✅ Max range conversion
- ✅ Custom zero point configuration

#### Two-Point Calibration
- ✅ Calibration point storage
- ✅ Zero and second point validation
- ✅ Linear interpolation between points
- ✅ Extrapolation beyond calibration points

#### Edge Cases
- ✅ Negative voltages
- ✅ Zero voltage handling
- ✅ Very high voltages
- ✅ Identical calibration points (division by zero protection)

### State Machine Tests (30+ tests)

#### State Transitions
- ✅ NORMAL → EMERGENCY (with 1-second timeout)
- ✅ NORMAL → ERROR (sensor failure)
- ✅ NORMAL → CONFIG (button press)
- ✅ EMERGENCY → NORMAL (conditions cleared)
- ✅ ERROR → NORMAL (sensor recovery)
- ✅ ERROR → CONFIG (button press)
- ✅ CONFIG → NORMAL (configuration complete)

#### Emergency Conditions
- ✅ Tier 1 threshold detection (message notifications)
- ✅ Tier 2 threshold detection (horn alarm)
- ✅ Condition timing and hysteresis

#### Notifications
- ✅ Emergency notification timing (15-minute intervals)
- ✅ Notification silence toggle
- ✅ Auto-clear silence on exit from emergency

#### Horn Control
- ✅ Horn activation in Tier 2 emergency
- ✅ Horn pulsing pattern (1 sec on, 1 sec off)
- ✅ Horn disabled when silenced
- ✅ Horn disabled outside emergency

## Writing New Tests

### Test File Template

```cpp
#ifdef UNIT_TESTING

#include <unity.h>

// Include mocks first
#include "../mocks/MockArduino.h"

// Include code under test
#include "../../include/YourHeader.h"

// Test functions
void test_example_feature() {
    // Arrange: Set up test conditions
    int expected = 42;
    
    // Act: Execute the code
    int actual = yourFunction();
    
    // Assert: Verify results
    TEST_ASSERT_EQUAL(expected, actual);
}

// Test runner
void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_example_feature);
    return UNITY_END();
}

#endif // UNIT_TESTING
```

### Common Unity Assertions

```cpp
// Equality
TEST_ASSERT_EQUAL(expected, actual);
TEST_ASSERT_EQUAL_STRING("expected", actual);

// Boolean
TEST_ASSERT_TRUE(condition);
TEST_ASSERT_FALSE(condition);

// Floating point (with tolerance)
TEST_ASSERT_FLOAT_WITHIN(0.1, expected, actual);

// Null checks
TEST_ASSERT_NULL(pointer);
TEST_ASSERT_NOT_NULL(pointer);

// Ranges
TEST_ASSERT_GREATER_THAN(threshold, actual);
TEST_ASSERT_LESS_THAN(threshold, actual);
```

### Testing Time-Dependent Code

Use the mock time helpers:

```cpp
#include "../mocks/MockArduino.h"

void test_timeout_behavior() {
    setMockMillis(1000);  // Set current time
    
    // Your code that checks millis()
    
    advanceMockMillis(500);  // Advance by 500ms
    
    // Verify behavior after time advance
}
```

### Testing Sensor Readings

```cpp
#include "../mocks/MockADS1115.h"

void test_sensor_reading() {
    WaterPressureSensor sensor(true);  // Mock mode
    
    // The sensor will generate random values in mock mode
    SensorReading reading = sensor.readLevel();
    
    TEST_ASSERT_TRUE(reading.valid);
    TEST_ASSERT_GREATER_THAN(0, reading.level_cm);
}
```

## Troubleshooting

### "undefined reference" errors

**Problem**: Missing mock implementations

**Solution**: Ensure mocks are included before the real headers:

```cpp
// Include mocks FIRST
#include "../mocks/MockArduino.h"
#include "../mocks/MockADS1115.h"

// Then include real code
#include "../../include/WaterPressureSensor.h"
```

### Tests compile but fail unexpectedly

**Problem**: Test state pollution between tests

**Solution**: Reset state in `setUp()`:

```cpp
void setUp(void) {
    setMockMillis(0);  // Reset time
    // Reset any global state
}
```

### "Test not found" error

**Problem**: Test file not in correct directory structure

**Solution**: Ensure test files are in `test/test_*/` directories and named `test_*.cpp`

### Cannot access private members

**Problem**: Testing private methods

**Solutions**:
1. Test through public interface (preferred)
2. Make methods protected and use friend class
3. Extract logic to separate testable functions

## Integration with Main Code

The state machine has been extracted into [`include/StateMachine.h`](../include/StateMachine.h) to be testable. To use it in `main.cpp`:

```cpp
#include "StateMachine.h"

// In setup()
StateMachineContext stateMachineCtx;
stateMachineCtx.currentState = NORMAL;
// ... initialize other fields

// In loop()
StateMachineSensorReading reading;
reading.valid = currentReading.valid;
reading.level_cm = currentReading.level_cm;

StateMachineOutput output = updateStateMachine(
    stateMachineCtx, 
    reading, 
    millis(), 
    configServer->isSetupModeActive()
);

if (output.stateChanged) {
    LOG_STATE("[STATE] Transitioned to %s", stateToString(output.newState));
}

if (output.setHornState) {
    digitalWrite(ALERT_PIN, output.hornOn ? HIGH : LOW);
}

if (output.sendEmergencyNotification) {
    sms.send(output.message);
    discord.send(output.message);
}
```

## Continuous Integration

To run tests in CI/CD pipeline:

```yaml
# Example GitHub Actions
- name: Run Unit Tests
  run: |
    pip install platformio
    pio test -e native
```

## Further Resources

- [PlatformIO Unit Testing](https://docs.platformio.org/en/latest/advanced/unit-testing/index.html)
- [Unity Test Framework](https://github.com/ThrowTheSwitch/Unity)
- [Test-Driven Development for Embedded C](https://pragprog.com/titles/jgade/test-driven-development-for-embedded-c/)

## Questions or Issues?

If you encounter issues with tests or want to add new test coverage, please:

1. Check this README for troubleshooting steps
2. Review existing test files for examples
3. Ensure mocks are properly configured
4. Verify test environment is set up correctly in `platformio.ini`

Happy testing! 🧪
