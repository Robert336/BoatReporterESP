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
- [ ] If user-visible behavior changed, updated the **canonical** doc (see `CONTRIBUTING.md` Documentation table) rather than duplicating procedures
- [ ] If shipping to devices, bumped `FIRMWARE_VERSION` in **both** `include/Version.h` and `platformio.ini`
- [ ] If tagging a release, updated version pins in the README badge / doc footers to the new tagged version (not `[Unreleased]`)
- [ ] Web UI changes tested against the `dev-ui/` mock server (no manual HTML embedding; `compress_html.py` runs on build)
- [ ] Commit messages follow Conventional Commits (`fix(scope): …`, `feat(scope): …`, `docs: …`)

## Notes for review

<!-- Anything reviewers should look at closely, or test manually on hardware. -->
