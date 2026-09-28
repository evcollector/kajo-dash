# KAJO-Dash UI component system

This is the canonical visual and interaction system for the 320x240 LVGL UI.
Read it before adding or changing menus, dialogs, selectors, or status messages.
The goal is not only visual similarity: shared components must also preserve the
same touch feedback, translation behavior, animation timing, and memory usage.

## Source of truth

- `src/lvgl_app/ui_style.h` contains stable design tokens: screen dimensions,
  spacing, radii, border widths, chrome colors, and shared motion timing.
- `src/lvgl_app/ui_common.h/.cpp` contains general drawing primitives and
  dashboard widgets.
- `src/lvgl_app/screens.cpp` contains interactive menu components because their
  callbacks and actions are screen-specific.
- `tools/layout_editor.html` contains the human-facing visual component
  reference between the editable workspace and property inspector.
- `AGENTS.md` contains required project-wide UI conventions.

Do not copy raw colors such as `0xFB40` or `0x1082` into new menu code. Use the
named values in `cyd_ui`. If a value will be reused, add a named token rather
than creating another local magic number.

## Canonical components

| Need | Use | Notes |
| --- | --- | --- |
| Screen root | `makeScreen(...)` | Establishes the correct 320x240 root and background behavior. |
| Passive panel/card | `makeInfoPanel(...)` | Borderless information grouping with no touch behavior. Use `makePanel(...)` only for structural/modal housings. |
| Text | `makeLabelAt(...)`, `makeLabel(...)`, `makeLabelFont(...)` | Titles are white; secondary/explainer text uses `cyd_ui::secondaryText()`. |
| Menu tile | `makeMenuButton(...)` | Provides the standard surface, border, title/subtitle layout, touch feedback, and action triangle. |
| Top navigation | `makeMenuTopBar(...)`, `makeSubmenuTopBar(...)` | Provides the standard Back/Next behavior and passive centered page information. |
| Compact navigation | `makeNavButton(...)`, `makeSmallNavButton(...)`, `makeVerticalNavButton(...)` | Reuses standard arrows, surface, border, and press feedback. |
| Selector option | `makePopupButton(...)`, `makePopupSwatch(...)` | Use only within an accent-bordered selector housing: the Customize Theme selector or the ride replay chart pickers (see Ride replay charts). |
| Modal confirmation | `showConfirmationDialog(...)` | Standard dimmed scrim, dialog housing, explanation, Cancel, and explicit action. |
| Transient notice | add a `StatusNotice` and call `showStatusNotice(...)` | Required for success, warning, status, and “already enabled” messages. Never build a separate toast. |
| Dashboard graphics | helpers in `ui_common.h` or the layout editor | Dashboard accent/background settings must not recolor menu chrome. |

If an existing constructor is file-local, extend or move that constructor
instead of reproducing its LVGL styles in a second place.

## Standard menu tile behavior

- Normal interactive tiles use the dark grey control surface and fixed
  light-grey `cyd_ui::idleControlBorder()` border. Dashboard accent selection
  must never recolor an idle menu control.
- Selected/active tiles use the darker `cyd_ui::activeControlSurface()` fill
  and a 3 px inset orange
  highlight border implemented by `makeMenuButton(...)`.
- The selected border must not move its contents. `makeMenuButton(...)`
  compensates for the extra inset so labels and chevrons retain the same
  coordinates in idle, selected, and released states.
- Pressed controls use the same darker active fill with an orange
  outline, keeping a clear distinction between a temporary press and a saved
  selection.
- Non-interactive information must be borderless and must not show an action
  triangle or press animation. Prefer `makeInfoPanel(...)` or a menu tile with
  an action id of zero where appropriate.
- Titles use the primary white text color.
- Explanations use the secondary grey text color and may occupy two rows.
  Explanations that fit on one row stay on the lower subtitle row.
- Subtitle label boxes reserve two pixels beyond the nominal font line height
  for descenders and antialiasing. Keep that allowance in the shared
  `makeMenuButton(...)` implementation instead of compensating per screen.
- Keep the filled orange triangle as the common “opens or performs an action”
  affordance. Do not substitute text glyph arrows.

## Layout rules

- Design and verify at exactly 320x240.
- Controls against the display edge use `cyd_ui::kEdgeInset` (2 px), matching
  Back/Next. Gaps between neighboring controls use `cyd_ui::kControlGap`
  (5 px). Larger margins are reserved for intentionally centered/narrow content.
- Standard top controls are 84x36 and use the positions in `ui_style.h`.
- The centered top heading/page readout is informational: it has no border,
  button fill, or touch behavior.
- Do not add a horizontal divider below the top controls.
- Size paired Back and Next buttons equally. Remove/disable Next on the final
  page rather than wrapping to the first page.
- Information-only readouts should visually differ from touch targets: use a
  subtle panel fill with no border, triangle, or press effect.

## Shared icon assets

- Dashboard and status glyphs use crisp 16x16 two-state masks from
  `preview_output/icons16`; keep these source dimensions unchanged.
- Pixel coverage is binary: transparent or fully opaque. Color remains a
  runtime property supplied to `makeIcon(...)`, so one mask works across dark,
  light, and customized dashboard appearances.
- Rebuild normalized PNGs with `tools/convert_new_icons.py`, then regenerate
  the packed firmware data with `tools/convert_icons_1bit.py`.
- For pixel-level edits, use the Icon pixel editor at the bottom of the LVGL
  layout editor's right inspector. Its Save action updates the canonical source
  and normalized PNG together, then runs the 1-bit firmware packer.
- Extend the shared icon list and generator instead of embedding a one-off
  bitmap in an individual screen.

## Color ownership

Menu chrome uses the fixed orange, grey, and white system palette exposed by
`ui_style.h`. Per-theme accent, background, and light/dark appearance settings
belong only to dashboard themes. Do not apply dashboard colors to menus,
dialogs, navigation, or status notices.

Cyber HUD gauge interiors show the dashboard background directly, including
custom solid colors and gradients; they have no separate fill. With the Default accent,
light Cyber HUD uses black outlines, meters, icons, text, and battery blocks.
Explicit accent choices retain their color. Do not restore layered polygon
shading beneath its frequently updated readouts.

The segmented top-bar battery (`makeSegBattery` / `setSegBatteryLevel`) is the
same in every theme that uses it and ignores the accent. In dark appearance it
has a white frame and green blocks, amber (`cyd_ui::kBatteryLow565`) at two
blocks left and red at one. In light appearance the frame and blocks are always
black. Below one block's worth the last block blinks (`kBatteryBlinkMs` on,
then off) in either appearance. The `13_battery_*` preview states cover each
step. Do not pass theme colors to it or restyle it per theme.

## Overlays that outlive their screen

An overlay parented to `lv_scr_act()` — the recovery-hold ring, the developer
prompt, a status toast — dies with that screen. `loadScreen(...)` deletes the
outgoing screen, and LVGL frees its children with it, but a `static lv_obj_t *`
still pointing at one is not cleared by that. Two things then go wrong: the
next `lv_obj_del` on the cached pointer writes through freed memory, and any
`if (thePointer) return;` guard refuses to open the overlay for the rest of the
session.

So whenever a pointer to an overlay is cached in a static, register an
`LV_EVENT_DELETE` callback on the object that clears it:

```cpp
static void thingDeleteCb(lv_event_t *) { thing = NULL; }
...
thing = lv_obj_create(lv_scr_act());
lv_obj_add_event_cb(thing, thingDeleteCb, LV_EVENT_DELETE, NULL);
```

LVGL sends the event to children as well as to the deleted object, so this
fires however the overlay dies. `statusToastDeleteCb`,
`recoveryHoldOverlayDeleteCb`, `developerPromptDeleteCb` and the developer hold
button all use it. Clearing the pointer in `uiShow(...)` instead works but has
to be remembered for every new screen; the callback cannot be forgotten.

The related rule is that replay popups close synchronously — see the ride
replay section.

## Motion and messages

- Shared selector/popup motion duration is `cyd_ui::kMotionMs` (140 ms).
- Transient messages must follow the complete popup convention in `AGENTS.md`.
- Modal confirmations require an explicit Cancel and action button and use
  `showConfirmationDialog(...)`.
- Do not animate expensive dashboard updates at the same time as selector or
  overlay motion.

## Translation and verification checklist

Before considering a UI change complete:

1. Use `txt(...)` for every new user-facing string.
2. Check long Finnish and German labels, not only English.
3. Add or update the relevant native-preview state.
4. Render the native preview at 320x240 and inspect touch-target distinction,
   clipping, alignment, and text wrapping.
5. Build the `kajo` PlatformIO environment (the former `lvgl_test` environment).

When a genuinely new reusable component is needed, add one canonical helper,
add any reusable values to `ui_style.h`, and update both this document and the
layout editor's visual reference in the same change.

## Vehicle configuration

Connection owns controller selection, pairing, UART baud, CAN target and link
diagnostics. Configuration groups Power & Current, Ride Modes, Calibration and
Gauge Ranges. Vehicle Info contains identity and continuous rated power. Battery
has statistics on page one and local pack setup on page two.

Ride Modes uses the backend's `reportsRideMode` capability: FarDriver has a
reported-gear page and a second page of display-only aliases. VESC explains that
standard telemetry does not report a profile. Back traverses pages before
returning to the parent; Next is absent on the final page. Field editors preserve
the originating section and page. Use `makeMenuTopBar`, without extra arrows.

Power & Current currently directs riders to the controller's configuration app;
neither backend implements writes. Gauge ranges are explicitly local display
scales, never controller limits. Future write support must use separate confirmed
and pending settings, explicit application, and read-back verification. Do not
connect the existing gauge-scale globals to controller configuration writes.

Native states `19_controller_config_*`, `20_connection_*`, `20_power_*`,
`21_calibration`, `21_gauge_ranges`, `22_modes_*`, `23_mode_labels` and
`27_pack_setup` cover these sections. Custom gear labels are capped at 12 bytes;
empty labels use translated defaults and unknown gear codes display a dash.

## Ride replay charts

The replay screen (`ride_replay_screen.inc`) uses canonical navigation buttons
with a drawn play/pause glyph. One to four chart bands split a fixed 159 px
area and share one time cursor. Each `ride_replay::Field` has fixed colours in
the `kReplayTrace565`, `kReplayBackground565` and `kReplayFill565` palettes
from `ui_style.h`, so a field keeps its colour in any band. Pack current and
phase current are the pair most often charted together, so their colours are
orange and lime rather than two shades of one hue. Fills are opaque
and flat; they do not follow the dashboard accent or gradients.

The header's title is a button that opens the ride summary; the 44 px chart
button beside the rate button draws the current layout as coloured bars and
opens the chart picker. The popups use the confirmation dialog's dimmed scrim
and accent-bordered housing, `makePopupButton(...)` controls, and a Done (or,
in the per-chart field list, Back) button in the heading row. Chart and field
choices are outlined and lettered in the field's colour rather than the
chrome accent; the current field is filled with a 3 px border. The summary's
Delete ride confirmation is a deliberately compact 236x112 variant of the
same housing, so it reads as a step within the summary rather than the
full-size `showConfirmationDialog(...)`. Close replay popups synchronously,
as `closeDeveloperPrompt()` does: a deferred delete can run after a screen
rebuild has already freed the popup.
Each band names its series, in the trace colour, at the bottom-left of the
full-height plot. Its visible glyphs end on the plot's bottom row. The fill
follows the trace without a separate solid strip. Keep the top and bottom
scale labels inside their own band so adjacent labels never overlap.

Values appear in black callouts with a trace-colored outline and white text,
holding the value and unit only, with no leader line to the sample dot. They
show both paused and playing, stay vertically centred in their band, and sit
left of the cursor from a fixed point a quarter of the way across the plot,
right of it before, all together; they may overlap the scale labels but stay
on screen. Never derive the switch from the current values. Missing data shows `--` and breaks the trace. See
[ride replay](ride-replay.md) for behavior, data handling and native
screenshot commands.

Replay uses Efficiency-style cached column heights and clipped horizontal fill
runs, with max and min labels and no horizontal gridlines. Speed starts at zero;
power, pack current and phase current include zero and extend below it when
the ride contains negative values. Power adds `(regen)` to the signed callout.

Replay overview curves smooth per-bucket means and round only after column
interpolation. Do not overlay raw min/max strokes; cursor bubbles retain the
original recorded values independently of this visual smoothing.
