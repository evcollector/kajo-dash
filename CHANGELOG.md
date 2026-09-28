# Changelog

Notable changes to the KAJO-Dash display firmware.

The four most recent entries are also shown on the display itself, under
Settings → Developer options → Changelog. `tools/generate_changelog_header.py` turns this file into
`src/lvgl_app/firmware_changelog.h`; CI fails if the two drift apart, so edit
this file and rerun the script rather than editing the header.

Format of an entry, which the generator relies on:

```
## YYYY-MM-DD — Short title

One or two lines of summary.
```

Keep titles under 30 characters and summaries under 90 -- one or two lines on
the 320x240 panel -- or the generator will truncate them and say so. Entries are English only: they are not
translated the way the rest of the interface is, because a changelog in six
languages is not worth hand-maintaining.

## 2026-09-12 — Phase current in ride logs

Rides now record motor phase current, and replay can chart it. VESC only for
now.

## 2026-09-12 — Ride replay

Recorded rides replay on the display, with up to four charts, a summary and
delete.

## 2026-09-08 — KAJO rename

The project, firmware and companion app are now KAJO, KAJO-Dash and KAJO
Companion.

## 2026-09-07 — FarDriver discovery freeze

Confirming a FarDriver controller froze the display. Incoming packets rebuilt the
screen.

## 2026-09-06 — On-device changelog

Recent firmware changes are now readable on the display itself.

## 2026-09-05 — FarDriver pairing fix

Selecting a FarDriver controller rebooted the display and lost the pairing.

## 2026-09-04 — TEHO-Dash rename

The project, the companion app and the Bluetooth Link protocol took the
TEHO-Dash name.

## 2026-08-31 — Controller backend layer

Controller support moved behind one backend, so VESC and FarDriver feed the same
telemetry.

## 2026-08-20 — Bluetooth firmware updates

Signed firmware images can now be installed over Bluetooth from the companion
app.

## 2026-08-19 — FarDriver controller support

Added FarDriver Bluetooth telemetry alongside VESC, plus a link diagnostics
screen.

## 2026-08-08 — Controller speed and faults

Speed and distance now come from the controller. Controller faults are shown on
the dashboard.

## 2026-08-07 — Dashboard themes and preview

Added the native LVGL preview, the Trace theme, battery telemetry and the
reworked HUD.

## 2026-07-14 — Localization

The interface is available in English, Finnish, German, French, Spanish and
Italian.

