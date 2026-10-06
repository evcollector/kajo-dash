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
| Needle, ring or bar that follows a reading | `Glide`, `glideInit(...)`, `glideAim(...)` | Moves the instrument from where it is to each new reading along a straight `lv_anim` instead of jumping a step at a time; see Gliding instruments. Use it for any continuous instrument; discrete block meters have nothing to glide. |
| Dashboard value updates | `setLabelText(...)`, `setObjHidden(...)`, `setObjTextAlign(...)`, `setObjTextFont(...)`, `setObjTextColor(...)`, `setObjBgColor(...)` | Anything an `update*()` path runs on every tick must skip unchanged writes: LVGL 8 invalidates an object for every style write and every HIDDEN flag write, changed or not (`lv_obj_set_pos` and `lv_obj_set_size` already compare). `cyd_dashboard_redraw` fails a theme that repaints on unchanged values. |
| Text on a dashboard | `tightenLabelRepaint(...)`, `tightenLabelRepaints(...)`, `kLabelRepaintMargin` | `buildDashboardMode` applies it to every label a dashboard builds, so a new label needs nothing. LVGL repaints a label's box grown by a quarter of its font height (28 px round the Pixel theme's numerals); with the margin at 0 a label is drawn inside its box and nowhere else. A caption wider than its box is therefore cut off at the box, in a full repaint as in a partial one: size the box for the longest translation. `cyd_dashboard_redraw` and `cyd_efficiency_redraw` fail a dashboard label that repaints beyond the margin. |

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

The theme selector keeps the dashboard at its normal brightness. Contrast comes
from the individual controls and heading backdrop, without a full-screen shade
or a forced refresh before the controls slide. A slide owns the whole frame budget, on
the dashboard's controls as in the selector: the press that starts it already holds
every live repaint (250 ms) and suspends the instrument glides; the slide extends the
hold, stops the 100 ms tick's repaints (`uiDashboardTick` returns early while
`uiUpdatesHeld()`), freezes the battery blink, and runs the display refresh and
animation timers at `cyd_ui::kSlideFramePeriodMs` (15 ms, normally 30) through
`boostFrameRate`, so the 140 ms slide gets about twice the frames. Holds only ever
extend. Suspended glides resume with their elapsed animation state intact. Any new
slide must go through `slideSelectorPart`, which does all of this; the native
`cyd_selector_animation` test taps a live, moving dashboard and checks the instruments
stay still, the slide gets 7 or more frames, and the frame clock returns to normal.
Selector builds use the shared thumbnail fixture (25 km/h, 1000 W, 52 V,
19.2 battery A, 44 phase A, 60% duty, 75% battery and a 10-minute ride),
including its derived stats, gauge scales and graph history. The opening state
is frozen through the initial hidden-controls delay. Demo readings
resume when the delay ends; opening another theme starts frozen again.
Native thumbnail rendering shares the fixture to prevent drift.
There is no startup sweep
in the selector: 600 ms after the controls finish hiding, the demo ride is released
and every needle, ring and bar glides from its thumbnail reading into the ride's own,
which at that moment is a hard stop from 33 km/h. Returning controls or changing screens
cancels a pending release; once released, the preview stays live. Native states
`13_selector_demo_wait` (the theme at rest) and `13_selector_demo_glide` (150 ms into
the hand-over) cover it, and `cyd_selector_animation` checks that the picture is the
thumbnail until the release and that no instrument jumps when the ride takes over.
Native states `13_dash_ui_selector_full` and
`13_dash_ui_selector_saved` cover the visible selector controls.

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
Configuration is a single page: a row of three tappable pack settings (chemistry,
series count, capacity) above the nine read-only battery figures, all in 43 px
tiles. The chemistry tile opens Cell Voltages, where Li-ion, LiPo and LiFePO4 are
selectable tiles (the chosen one filled, `makeMenuButton(..., active)`) over three
numeric tiles for cell minimum, nominal and maximum volts. Choosing a chemistry
loads its default voltages; choosing the current one again restores them. Minimum
is the empty cell and maximum the full-charge voltage, as the industry quotes it
(3.65 V for LiFePO4). The charge percentage is read from resting voltage, so a
full pack counts as 100% at the lower voltage it settles at (3.40 V for LiFePO4,
4.18 V for Li-ion); that point is a fixed share of the way from minimum to
maximum, so editing either moves it in proportion. Nominal volts only turn Ah
into Wh. The window keeps at least 0.3 V and the nominal voltage stays inside it. Any
change to chemistry or voltages restarts the charge estimate and the measured
capacity, because both were derived from the old curve. Back returns to Battery
Configuration. The series and capacity tiles open the keypad directly.

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
`27_battery_submenu`, `27_battery_lifepo4`, `27_battery_cells` and
`27_battery_cells_edited` cover these sections. Custom gear labels are capped at 12 bytes;
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

The bottom row is zoom out, skip back, play/pause, skip ahead and zoom in, canonical
navigation buttons 34, 70, 88, 70 and 34 px wide at the standard control gap, the
whole row spanning the screen between the edge insets. The play/pause and zoom
glyphs are drawn over the button (a magnifying glass with a minus or a plus). A
zoom button with nowhere to go draws its glyph in the muted colour and stops
being clickable, so there is no pressed flash either. Zoomed in, a 2 px track in
the muted colour sits under the charts with the visible stretch lit in the
chrome accent; the whole ride shows no track. The icons carry no text, so the
row needs no translation.

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
runs, with max and min labels and no horizontal gridlines. Cursor, dots and
bubbles are an overlay the screen repaints on its own: a moved bubble or a new
value invalidates those areas, never the whole chart (see
[ride replay](ride-replay.md)). Speed starts at zero; power, pack current and
phase current include zero and extend below it when the ride contains negative
values. Power adds `(regen)` to the signed callout.

Replay overview columns use each bucket's minimum for both the trace and fill
edge, with the same time-to-bucket mapping as the cursor. Do not average or
smooth the buckets, interpolate between their centres, or overlay min/max
strokes. Cursor bubbles retain the original recorded samples. See
[ride replay](ride-replay.md#repainting) for the rendering and repainting rules.

## Motor Data dashboard

Motor Data replaces Mono in dashboard slot 4. Six fixed instruments, each a
segmented ring (`makeSegRing(...)` / `setSegRingValue(...)` in `ui_common.cpp`)
with the shared outer needle (`makeTickNeedle(...)` / `setTickNeedleValue(...)`
in `dashboards.cpp`). The stock colors follow the Bar Graph dashboard's one hue
per quantity: speed cyan, phase voltage amber-yellow, phase amps lemon yellow (255, 237, 41),
input power red, and an orange run for the battery amps and duty between battery amps and input power. They come from `cyd_ui::kMotorData*565` in
`ui_style.h`, with a separate default palette for light appearance; explicit dashboard
accents override them. Each dial's number is pure white (black in light
appearance) and its caption is the dial's color most of the way to the text
color. Each ring has a full pale disc in light appearance (a dark disc in dark
appearance), tinted by the dial's color at 14/255 over the dashboard ground,
out to the blocks' outer edge, so it shows under them
(`setSegRingFill(...)`), stopping at the footer bar's row (`fillBottom` in `layout.json`, equal to the footer line's y) so the bar is clear of it; where the speed disc reaches the power dial it is drawn
beneath it, because the rings are built in order. The power caption is just the
unit (W or kW). The blocks at each fifth of
the ring are 3 px longer inward, as the Dual Gauge's major ticks are
(`setSegRingMajors(...)`). The input power ring's blocks are 7 px deep (`kMotorDataPowerThickness`, `thickness` in `layout.json`) instead of the rule's 9, which leaves its number room. The four small dials are 42 px in radius. The top bar is the Dual
Gauge and Cyber HUD one (white clock icon, time and battery, and a vehicle
name that is white with the default accent and the chosen accent otherwise) and the stock accent is the Dual Gauge green. The speed number uses the
72 px face below 100 and steps to 48 px from 100 up; the footer is 34 px tall. Geometry and fitted text
positions live in `tools/layout.json` under `motorData`. Speed is largest,
input power next, and the four side gauges are equally smaller. Phase A, speed
and phase V sit over battery A, input power and duty.

The segmented ring is the bar meters' blocks bent round a dial: solid blocks
lit in the ring's color, unlit as a dim ghost of the dial's full-brightness color
(52/255 toward the dashboard's ground, a little brighter than the bar meters'
38/255). Light appearance uses its default dial hue for both lit and inactive
blocks, with inactive blocks at `cyd_ui::kMotorDataLightUnlitMix` (88/255) over
the pale ground. Light-mode defaults are RGB (237, 147, 0) for phase voltage,
(220, 81, 0) for both phase and battery current, (157, 0, 0) for duty and
(222, 0, 0) for input power and (0, 54, 255) for speed, quantized to the
display's RGB565 format.
Every block is the same wedge, about 3.25 px wide (a third thinner than the bar
meters' 5 px), and the gap between blocks is about 2.25 px along the ring's centre line on
every ring, so the spacing is the same on the big speed face and
the small gauges and only the block count follows the radius (27 blocks at r38,
34 at r48, 48 at r62). `lv_draw_arc` takes whole degrees, so `segRingGeometry(...)`
rounds the pitch and gap to degrees once, and every block of a ring is then
identical. The blocks are centred on 12 o'clock within the 240 degree sweep, which
leaves the bottom of each dial open for its caption, and the needle's range is the
ring's own extent, so the two end together. A ring's thickness follows its dial:
`segRingThickness(radius)` gives a sixth of the radius plus a pixel and a third,
7 px at r34, 9 at r48 and 11 at r59, so the speed face has the tallest blocks
while block width and spacing stay the same. (A fifth of the radius would be truer
to the ratio but leaves the power digits 2 px from their ring and, through the
whole-degree rounding, makes the speed blocks visibly wider.) A ring is one custom-drawn object
rather than one object per block, and a new value invalidates only the blocks
that flip; `cyd_seg_ring` checks that against a full repaint. The layout editor's
`segRing` shape is a port of the thickness and geometry rules, so change both
together, and keep `sweepDeg` in `layout.json` equal to `kMotorDataSweepDeg`.

Rings and needles glide between readings (see Gliding instruments below). A reading
that is not available is placed without gliding, with its pointer hidden.

Only the four footer slots are customizable (motor temperature, ESC temperature,
battery voltage and trip distance by default). A Motor Data profile saved under an older
revision (Mono's layout, the footer that defaulted to duty, or the three-slot footer) is reset when
first loading this version. Missing readings show `--` and hide their pointers;
negative power/current retain their sign in the readout and use magnitude in the
gauge. Duty is the bottom-right dial, a magnitude percentage on screen, with its
signed fraction retained in telemetry.

The displayed PHASE V is an estimate of fundamental phase-neutral peak voltage, the
same kind of amplitude as the PHASE A motor current (so P = 1.5 x V x A):
`Vbus * abs(duty) / sqrt(3)`. This assumes sinusoidal VESC FOC and the default
`foc_overmod_factor = 1`; it is not measured, is not valid for arbitrary motor
control modes/modulation configurations, and cannot identify field weakening.
It is marked derived in telemetry. FarDriver has no duty/phase-voltage reading,
so the PHASE V and DUTY dials stay unavailable. A user-facing explanation is
deferred.

The instruments' widgets belong to the active Motor Data screen. A
screen-owned delete callback clears the cached screen pointer so updates stop
once it is gone, and a replacement screen built before the old one is deleted is
not forgotten by it.

Native states: `05_motor_data`, `_rebuild`, `_missing`, `_high`, `_regen`, plus
`13_selector_demo_glide`; check Finnish, German and light appearance as well.

The self-test sweep on entering a dashboard is switched off (`kStartupSweepAvailable` in
`dashboards.cpp`): a theme appears at its live readings and the glides take it from there.
The sweep code and `previewFinishStartupSweep` stay so it can be turned back on, and its
`13_sweep_*` preview states were removed with it.

## Gliding instruments

Motor Data, Dual Gauge, Redline, Ride Console, Minimal and Efficiency's speed dial move
their needle, ring or bar with a shared `Glide` (`ui_common.h`). The dashboard hears
from the controller ten times a second and the speed in whole km/h, so an instrument
moved straight to each reading would jump a step at a time: a km/h on the Dual Gauge's
default 30 km/h scale is almost 9 degrees of needle. Each instrument keeps its position in 4096ths of its
scale (`kGlideScale`), and `glideAim(...)` carries it to a new reading along a straight
`lv_anim` that lasts a quarter longer than the reading had been steady, so the next
reading finds it still on its way: a steady acceleration is one continuous motion. A
change of about one reading (5% of the scale or less) may take up to a second, so a
gentle ramp crawls instead of moving and stopping between readings; a bigger one takes
0.3 s at most, and either takes 0.1 s at least. A reading that arrives mid-glide takes
over from where the instrument is. The price of the continuous motion is an instrument
that trails the true speed by about 1 km/h (a quarter to half a second); the numbers
show the live reading at once. The first reading and every step of the startup sweep
are placed without gliding.

To add one: `glideAdd(place, context)` in the theme's build (the dashboard owns a
registry of them, in the order its theme adds them, and drops them when its screen
goes), a `place` callback that draws the instrument at a position and repaints only
what moved, and `glideAim(glides[i], position)` from the update path with the raw
reading as a fraction of the live scale (`glidePosition(value, maximum)`). Aim at the
raw reading, not at `displaySpeedValue()`: that is the 160 ms damped value, and the
glide would smooth it a second time. A scale that changes under the instrument is just
a new target. Block meters (Cyber HUD, Bar Graph, Simple) and Efficiency's consumption
needle, which has its own 350 ms smoothing, do not glide: a block flips whole.

The `place` callback runs once per animation frame, so it has to be cheap when little
has moved: the tick dials skip a lit count that did not change, the needle skips an
endpoint that did not move a pixel, the Ride Console bars skip less than a pixel, and
the Minimal and Efficiency rings invalidate only the stretch between the old and new
needle and skip the arcs a repaint cannot reach. `cyd_glide` runs the same scenarios on
every theme's speed instrument (the first reading, a one-reading step, a steady stream,
a gentle ramp, retargeting, turning round, a repeated reading, the startup sweep, a
replaced screen, a scale pushed by the speed) and checks the picture of every frame
against a full repaint. A glide is an animation, so a preview or test that moves the
clock has to do it with `advanceTime(...)`, which also runs LVGL's timers.
