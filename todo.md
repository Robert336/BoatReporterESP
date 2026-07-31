# Maintainer follow-ups

Internal checklist for doc/media work that should **not** appear in user-facing guides. Keep product docs free of “add a photo when you have one” style asides.

## Docs media

**Already in repo (do not re-capture unless stale):**

- `docs/screenshots/main-config-screen-mockup.png` — Figma home-screen iPhone mock-up; used at the top of the README landing page
- `docs/screenshots/grafana-full.png` — used in README (dashboard showcase), `server-stack/README.md`, and `docs/mqtt-telemetry.md`
- `docs/screenshots/web-config-pages-v2.png` — used in README and `docs/configuration.md`

**Still open:**

- [x] Add the Figma home-screen mock-up (UI inside an iPhone frame) to `docs/screenshots/` and place it near the top of the README landing page as an eye-catcher for readers
- [ ] Add assembled-unit and wiring photos; place under `docs/screenshots/` and link from `docs/hardware.md` Assembly once they exist
- [ ] Refresh `grafana-full.png` only if dashboard panels change
- [ ] Refresh `web-config-pages-v2.png` only after major web UI changes (capture via `dev-ui/` mock or on-device AP; prefer 2× / phone-width)
- [ ] Refresh `main-config-screen-mockup.png` if the Figma home screen changes

## Capture notes (when regenerating)

- Grafana: flash `pio run -e mock -t upload`, let it run 15–30 minutes so rate-of-change and state-timeline panels have data, then capture the full “Boat Reporter — Bilge Monitor” dashboard; keep one theme consistent
- Config pages: DevTools → “Capture full size screenshot”, or Firefox full-page screenshot
- iPhone mock: export from Figma to `docs/screenshots/main-config-screen-mockup.png`

## Docs / process leftovers to consider

- [ ] Confirm GitHub Pages docs site still matches the restructured `docs/` index after merge
- [ ] When tagging the next firmware release, bump version pins in README badge and doc footers (see CONTRIBUTING)
