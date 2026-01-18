# Testing Without GCC/Native Platform

If you don't want to install GCC on Windows, you can still test your logic using the built-in mock mode that's already in your code.

## Using Built-in Mock Mode

Your `WaterPressureSensor` class already has mock functionality! You can test on the actual ESP32 hardware (or in the dev environment) without physical sensors.

### Enable Mock Mode

In [`src/main.cpp`](../src/main.cpp), line 53:

```cpp
static constexpr bool USE_MOCK = true; // Change to true for testing
```

This will make the sensor generate random water level readings (4-20 cm) without needing the physical sensor connected.

### Test Build for ESP32 (No Native Compiler Required)

The `platformio.ini` now includes an ESP32 test environment:

```ini
; ESP32 test environment - runs tests on actual ESP32 hardware
[env:esp32-test]
platform = ${common.platform}
board = ${common.board}
framework = ${common.framework}
monitor_speed = ${common.monitor_speed}
board_build.partitions = ${common.board_build.partitions}
lib_deps = ${common.lib_deps}
build_flags = 
    -D UNIT_TESTING
    -D ENABLE_MOCK_MODE
test_build_src = yes
```

To run tests on ESP32 hardware:

```bash
pio test -e esp32-test
```

This runs the same Unity unit tests, but on the actual ESP32 hardware instead of your PC.

### Manual Testing via Serial Monitor

1. Upload code with `USE_MOCK = true`
2. Open Serial Monitor (115200 baud)
3. Watch the logs to verify state machine behavior
4. Use the debug/config web interface to test different scenarios

### Web-Based Testing

You already have a great mock server setup in `dev-ui/`:

```bash
cd dev-ui
node mock-server.js
```

Then open `http://localhost:3000` to test the web interface with simulated sensor data.

## Pros and Cons

### Native Testing (Requires GCC)
✅ Fast execution (seconds)
✅ Automated unit tests
✅ CI/CD friendly
❌ Requires GCC installation on Windows

### ESP32 Mock Testing (No GCC Required)
✅ No additional tools needed
✅ Tests on actual target hardware
✅ Easy to enable/disable
❌ Slower (need to upload to ESP32)
❌ More manual testing

### Web Mock Server (No GCC Required)
✅ No hardware needed at all
✅ Visual feedback
✅ Great for UI/API testing
❌ Doesn't test ESP32 code directly

## Recommendation

For your use case, I'd suggest:

1. **Automated unit tests on ESP32**: Use `pio test -e esp32-test` (no GCC required!)
2. **Quick manual testing**: Use `USE_MOCK = true` and upload to ESP32
3. **Full automated testing**: Install MinGW-w64 and use native tests (fastest)
4. **UI testing**: Use the mock server in `dev-ui/`

The ESP32 test environment (`esp32-test`) gives you the best of both worlds - automated Unity tests without needing to install GCC on Windows!
