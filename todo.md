# Maintainer follow-ups

Internal checklist for doc/media work that should **not** appear in user-facing guides. Keep product docs free of “add a photo when you have one” style asides.

## Docs media

- [ ] Add assembled-unit and wiring photos; place under `docs/screenshots/` and link from `docs/hardware.md` Assembly once they exist
- [ ] Refresh Grafana dashboard capture (`docs/screenshots/grafana-full.png`) if panels change — referenced from README and `server-stack/README.md`
- [ ] Refresh config UI composite (`docs/screenshots/web-config-pages-v2.png`) after major web UI changes — capture via `dev-ui/` mock (`npm start` → `http://localhost:3000`) or on-device AP; Chrome/Edge full-page screenshot works well; prefer 2× / phone-width for the mobile-first UI

## Capture notes (when regenerating)

- Grafana: flash `pio run -e mock -t upload`, let it run 15–30 minutes so rate-of-change and state-timeline panels have data, then capture the full “Boat Reporter — Bilge Monitor” dashboard; keep one theme (dark or light) consistent
- Config pages: DevTools → “Capture full size screenshot”, or Firefox full-page screenshot

## Docs / process leftovers to consider

- [ ] Confirm GitHub Pages docs site still matches the restructured `docs/` index after merge
- [ ] When tagging the next firmware release, bump version pins in README badge and doc footers (see CONTRIBUTING)
