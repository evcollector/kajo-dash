# Contributing

Bug reports, hardware findings and protocol corrections are welcome, and are
useful even when they come without a patch. Please read the two short sections
below before opening a pull request.

## Licensing

The project is GPL-3.0-or-later, and for almost everything that is all you need
to know — contribute as you would to any GPL project.

The exception is project-owned, hand-written firmware source. Some of that
source is also compiled into the closed-source companion app, and other parts
may be used in authorized KAJO-Dash commercial or white-label products. Changes
to it need the additional, source-preserving licence grant in
[`CLA.md`](CLA.md). The pull request template records agreement for that pull
request, or that the agreement does not apply. Nothing to email or sign.

Issues, discussion and reverse-engineering findings need no agreement at all.

## Before opening a pull request

- Read [`AGENTS.md`](AGENTS.md). It defines the shared UI component system, the
  design tokens, and the popup conventions. New screens are expected to reuse
  the canonical constructors in [`docs/ui-components.md`](docs/ui-components.md)
  rather than hand-build equivalents.
- Regenerate previews for any material UI change, and check a non-English render
  for caption overflow:

  ```bat
  scripts\run_preview_lvgl.bat
  python tools\render_lvgl_native.py --lang=de
  ```

- Run the host tests:

  ```powershell
  ctest --test-dir tools\lvgl_native_preview\build_simulator -C Release
  python -m pytest -q
  ```

- Build each firmware environment when changing shared firmware or build
  configuration:

  ```powershell
  pio run -e kajo -e fardriver_test -e vesc_test
  ```

- For anything touching the display, controller link or update path, run the
  matrix in [`docs/device-smoke-test.md`](docs/device-smoke-test.md) on real
  hardware and say in the pull request which CYD variant you used.

## Third-party components

Anything that adds a dependency has to be recorded in
[`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md), with its licence text in
`licenses/`. If it ends up inside the firmware binary or the Android APK, its
notice has to travel with that binary. The app's source is private, so for
anything that reaches the APK (a new dependency of covered firmware source,
say)
mention it in the pull request and the maintainer will add it to the app's
in-app licence list.

## Safety

This project displays telemetry and does not control the vehicle. Please do not
propose changes to that boundary.
