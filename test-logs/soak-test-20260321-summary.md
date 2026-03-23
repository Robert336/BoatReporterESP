# 24-Hour Soak Test Summary

**Date**: 2026-03-21 to 2026-03-22
**Log file**: `esp32-soak-test-20260321-221554.log`
**Duration**: ~24 hours continuous
**Log size**: 61MB / 903,335 lines

## Test Setup

- **Hardware**: ESP32 board only — no physical sensors attached
- **Sensor data**: Mocked via `ENABLE_MOCK_MODE` build flag
  - Sine wave pattern: 1-hour period, 0–60cm range
  - Random noise: ±2cm per reading
  - Automatically crosses Tier 1 (30cm) and Tier 2 (50cm) thresholds each cycle
- **Notifications**: Mocked — SMS/Discord sends were skipped (`[MOCK] Skipping SMS/Discord send`)
- **Connection**: Board connected to server via USB serial (`/dev/ttyUSB0` at 115200 baud)
- **Monitoring**: PlatformIO serial monitor logging to file

## Purpose

Validate board stability, memory behavior, and state machine correctness over an extended run without physical sensor hardware. This test exercises the full firmware logic path — state transitions, threshold detection, horn control, LED patterns, and notification scheduling — using deterministic mock data.

---

## Results

### Memory (Heap) — No Leak Detected

| Metric | Value |
|---|---|
| Start Free Heap | 242,904 bytes |
| End Free Heap | 242,488 bytes |
| **Net change over 24h** | **-416 bytes (0.17%)** |
| MinFree (lifetime low) | 234,572 bytes (stable entire test) |
| MaxAllocBlock | 110,580 bytes (stable entire test) |

Heap was stable at 242,616 bytes for 93% of readings (8,082 of 8,683 samples). Two transient dips to ~240,252 bytes occurred mid-test but recovered immediately — likely from temporary string allocations during notification formatting. The 416-byte net decrease is negligible and non-trending.

**Verdict**: No memory leak.

### Crashes & Reboots — None

- Zero crashes, panics, guru meditations, watchdog resets, or brownouts
- Zero reboots — single continuous run for the entire 24-hour period
- Zero sensor errors (`SensorError=0` on all 8,683 status readings)
- No `[ERROR]` or `[WARN]` log entries

**Verdict**: Board ran stable for 24 hours with no interruptions.

### State Transitions — Correct

| Transition | Count |
|---|---|
| NORMAL → EMERGENCY | 24 |
| EMERGENCY → NORMAL | 24 |
| NORMAL → ERROR | 0 |
| NORMAL → CONFIG | 0 |

24 clean cycles in 24 hours matches the 1-hour mock sine wave exactly. The 5-second debounce timeout prevented any false transitions. Entry into EMERGENCY occurred at ~31–32cm (above the 30cm threshold, after the timeout elapsed). LED patterns toggled correctly with 48 total changes (24x OFF→SOLID, 24x SOLID→OFF).

**Verdict**: State machine behaved correctly with no spurious transitions.

### Notifications — Working as Expected

61 total notification attempts, all correctly mocked:

- ~25 Tier 2 alerts (peak water level near 59–61cm)
- ~36 Tier 1 alerts (initial emergency entry at 30–32cm + periodic 15-minute re-notifications)

This averages ~2.5 notifications per emergency cycle, consistent with the expected pattern: 1 entry alert + 1 Tier 2 peak alert + occasional 15-minute periodic re-notifications during each ~30-minute emergency phase.

**Verdict**: Notification scheduling and message formatting working correctly.

---

## Issue Found: Threshold Chattering

This is the most significant finding from the test.

| Threshold | Detections | Clearances | Events Per Cycle |
|---|---|---|---|
| Tier 1 (30cm) | 178,770 | 178,770 | ~7,449 |
| Tier 2 (50cm) | 223,292 | 227,343 | ~9,304 |
| **Total EVENT logs** | **804,124** | — | — |

EVENT logs account for **89% of the entire log file**. The ±2cm noise causes readings to oscillate rapidly around both thresholds, firing a detect/clear event on nearly every reading when the water level is near a threshold boundary.

The state machine itself is properly protected — the 5-second timeout debounce prevented any false state transitions (only 24 clean cycles occurred). However, the Tier 1/Tier 2 condition flags and their associated log messages fire on every individual threshold crossing without hysteresis.

### Recommendations

1. **Add hysteresis to threshold detection** — e.g., trigger at 30cm but don't clear until below 28cm. This would eliminate chattering entirely and reduce log volume by ~90%.
2. **Rate-limit EVENT logging** — even with hysteresis, consider logging threshold crossings at most once per second to prevent log bloat in noisy sensor environments.

## Horn Logging Imbalance

| Event | Count |
|---|---|
| Horn ON | 50,869 |
| Horn OFF | 26,703 |
| Horn deactivated | 4,051 |

The ~2:1 ON/OFF ratio indicates `Horn ON` is logged on every loop iteration while the GPIO is high, rather than only on the rising edge. This is cosmetic — horn behavior is functionally correct — but logging only on state transitions would reduce noise.

---

## Overall Assessment

**The board passed the 24-hour soak test.** Memory is stable with no leaks, no crashes or reboots occurred, state transitions are correct, and notification scheduling works as designed. The primary actionable finding is threshold chattering, which does not cause incorrect behavior but generates significant log volume and would cause unnecessary horn toggling in a production environment with noisy sensor readings. Adding hysteresis bands to the water level thresholds is recommended before field deployment.
