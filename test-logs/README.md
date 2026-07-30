# Validation campaigns (`test-logs/`)

Long-running soak and hardware validation write-ups. These complement the host-side unit suite in [`test/TESTING_README.md`](../test/TESTING_README.md). The [README Testing section](../README.md#testing) links here for a short showcase.

## Analyses

| Write-up | When | What we exercised | Headline |
|----------|------|-------------------|----------|
| [soak-test-20260321-summary.md](soak-test-20260321-summary.md) | 2026-03-21 | ~24 h mock sensor (`ENABLE_MOCK_MODE`), USB serial log | Heap −416 B over 24 h; no leak; state machine stable |
| [drip-test-20260502-analysis.md](drip-test-20260502-analysis.md) | 2026-05-02 | ~3.5 h real 4–20 mA cylinder fill/drain over MQTT | Peak 64.48 cm; clean voltage tracking; heap stable; plots |
| [random-state-test-20260523-analysis.md](random-state-test-20260523-analysis.md) | 2026-05-23 | ~61.6 h mixed manual state transitions via MQTT log | 53 paired transitions; min heap 113.9 KB |

## Supporting artifacts

| File | Role |
|------|------|
| [parse_mqtt_log.py](parse_mqtt_log.py) | Parser / plotting helper used for MQTT campaign analysis |
| [mqtt_log.txt](mqtt_log.txt) | Sample / campaign MQTT log (large). Prefer the markdown analyses over browsing the raw file |
| `plot_*.png` | Figures referenced from the drip-test analysis (water level, voltage, heap, events, Tier-1 zoom) |

Raw multi-megabyte serial captures from older soaks are not all checked in; the markdown summaries are the durable record.
