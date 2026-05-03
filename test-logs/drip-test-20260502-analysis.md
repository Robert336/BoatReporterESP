# Drip Test Analysis — 2026-05-02

**Test:** Testing cylinder filled to capacity, then allowed to drain slowly while the ESP32 streamed sensor data over MQTT to a Raspberry Pi on the LAN.
**Log:** `mqtt_log.txt` | **Duration:** 3 h 35 m (16:04 – 19:39)

---

## Summary

| Metric | Value |
|---|---|
| Peak water level | **64.48 cm** at 16:23 |
| Tier 1 threshold crossings | 5 (20 cm) |
| Tier 2 threshold crossings | 2 (40 cm) |
| ADC readings captured | 86,252 |
| Avg free heap | 225.6 KB |
| Min free heap (low-water mark) | 156.0 KB |

---

## Water Level

The cylinder was filled rapidly in the first ~7 minutes, triggering Tier 1 at 16:11:34 and Tier 2 at 16:11:38. The level briefly cleared (16:15:57), then immediately re-triggered as the second fill peaked around 64 cm before beginning the long slow drain. The system remained in `EMERGENCY` state from ~16:17 until ~18:07 as the cylinder drained across both thresholds.

![Water Level Over Time](plot_water_level.png)

---

## Pressure Sensor Voltage

Raw ADC was sampled at ~12 Hz and averaged to 30 s bins below. The voltage signal cleanly tracks the fill/drain profile with minimal noise. The small step visible around 16:15 corresponds to the first drain + refill cycle.

![Pressure Sensor Voltage](plot_sensor_voltage.png)

---

## Heap Memory

Free heap fluctuated between ~215 KB and ~238 KB throughout the run with no observable downward trend — no memory leak detected. The minimum free mark (156 KB) is a legacy low-water from earlier in the session rather than from the steady-state run.

![ESP32 Heap Memory](plot_heap.png)

---

## Event Timeline

Red shading shows the two `EMERGENCY` state windows. Tier 2 events (red triangles) and Tier 1 events (orange triangles) are plotted against wall-clock time.

![Event Timeline](plot_events.png)

---

## Tier 1 Oscillation — Zoomed View

The ~2 min window around 18:07 shows rapid Tier 1 oscillation as the level hovered on the 20 cm boundary during drain. The zoomed plot below isolates that window and shows the oscillation cluster, the EMERGENCY_TIMEOUT_MS delay before the actual state transition, and a proposed hysteresis clear threshold.

![Tier 1 Oscillation Zoom](plot_tier1_zoom.png)

- **3× detected / 3× cleared in < 2 s** (18:07:39–41) — all at 19.99 ↔ 20.01 cm with no ADC noise, just the level sitting on the boundary.
- **State did not bounce** — `EMERGENCY_TIMEOUT_MS = 5 s` held the state in EMERGENCY until 18:07:46, exactly 5 s after the final Tier 1 clear. The timeout provided state hysteresis but had no effect on the tier-flag chatter or the log/horn noise it causes.
- **Proposed fix** — a level-based hysteresis band (e.g. −1.5 cm, shown as dotted line at 18.5 cm) on the tier-flag clear condition would have prevented the oscillation entirely without changing state-machine behavior.

---

## Notes

- Total of 1,440 horn ON/OFF cycles fired during the emergency window — consistent with the defined alert cadence.
- No sensor errors or WiFi drops were logged for the entire 3.5 h run.
