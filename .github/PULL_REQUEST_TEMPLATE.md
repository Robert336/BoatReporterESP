## Summary

<!-- What does this PR change, and why? -->

## Type of change

- [ ] Bug fix (non-breaking)
- [ ] New feature (non-breaking)
- [ ] Breaking change
- [ ] Documentation

## Checklist

- [ ] Builds clean: `pio run -e prod` and `pio run -e dev`
- [ ] Native tests pass: `pio run -e native -t test`
- [ ] If user-visible behavior changed, updated the relevant doc under `docs/` or `README.md`
- [ ] If shipping to devices, bumped `FIRMWARE_VERSION` in **both** `include/Version.h` and `platformio.ini`
- [ ] Web UI changes tested against the `dev-ui/` mock server (no manual HTML embedding; `compress_html.py` runs on build)
- [ ] Commit messages follow Conventional Commits (`fix(scope): …`, `feat(scope): …`, `docs: …`)

## Notes for review

<!-- Anything reviewers should look at closely, or test manually on hardware. -->
