# Screenshots

Drop the project's screenshots here. They are referenced from [`README.md`](../../README.md) and [`server-stack/README.md`](../../server-stack/README.md) using relative paths, so as soon as a PNG lands at the expected filename it appears in the docs; no markdown edits are needed.

## Config server (capture on a phone, or via the mock server)

You can capture these without the boat hardware: run the mock server in [`dev-ui/`](../../dev-ui/README.md) (`npm install && npm start`), then browse to `http://localhost:3000` (or your computer's LAN IP from your phone). The mock serves the exact same HTML as the ESP32.

| File | Content |
|------|---------|
| [`web-config-pages-v2.png`](web-config-pages-v2.png) | Composite view showing all config pages (dashboard, WiFi, notifications, settings, calibration, OTA) side-by-side |

**How to capture:**
- Chrome/Edge: DevTools → `Cmd/Ctrl+Shift+P` → "Capture full size screenshot"
- Firefox: right-click → "Take Screenshot" → "Save full page"

## Grafana dashboard

Flash the `mock` firmware (`pio run -e mock -t upload`) and let it run 15–30 minutes so the rate-of-change and state-timeline panels have data, then capture:

| File | Content |
|------|---------|
| [`grafana-full.png`](grafana-full.png) | Full "Boat Reporter — Bilge Monitor" dashboard; all panels in one shot |

## Tips

- Capture at 2× (retina) and let GitHub downscale; crisper on all displays.
- The config UI is mobile-first (`max-width: 480px`), so phone-sized captures look intentional. Use a narrow browser window or phone.
- Pick one Grafana theme (dark or light) and keep it consistent across all Grafana shots.
