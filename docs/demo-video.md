# Demo video

The demo video is rendered from the [simulator](simulator.md), not filmed or screen-recorded. A
scene script drives the real firmware UI with touch input, the recorder plays it in simulated
time and writes every frame, and `tools/make_demo_video.py` adds a chapter card and captions and
encodes H.264. The same script gives the same pixels on every machine, and a script that no longer
matches the UI fails instead of recording the wrong thing.

## Rendering

Needs what the [simulator](development.md#simulator) needs (an installed
`.pio/libdeps/kajo/lvgl`, CMake, Visual Studio C++ Build Tools, Pillow) and `ffmpeg` in `PATH`
(or its location in `FFMPEG`). The `start` chapter also drives Google Chrome through Playwright
(`pip install playwright`; it uses the installed Chrome and downloads no browser) and needs a
monospace font for the menu (Consolas, or DejaVu Sans Mono or Liberation Mono).

```powershell
python tools\make_demo_video.py                  # every chapter in tools\demo\cut.json, then demo.mp4
python tools\make_demo_video.py themes --gif     # one chapter, plus a README GIF
python tools\make_demo_video.py replay --screen-only --sheet --no-build   # authoring: no dressing
```

All seven chapters render in about four minutes, GIFs and sheets included; `--quick` trades size
for speed while drafting. Everything lands in `dist/demo/`, which git ignores.

| # | Chapter | Shows |
| --- | --- | --- |
| 1 | `themes` | a speed edit of the twelve dashboard themes on their live previews, and saving one |
| 2 | `customize` | Dual Gauge in magenta; dark, light and auto appearance; background and gradient; what each readout shows |
| 3 | `replay` | on-device ride replay: seek, play, zoom, charts, summary |
| 4 | `display` | brightness, auto brightness and its calibration, the ambient LED, the touch test, the display panel page |
| 5 | `developer` | unlocking developer mode, and what is inside it |
| 6 | `updates` | a signed firmware update over Bluetooth, from Bluetooth Link to the verified restart |
| 7 | `start` | a speed edit of getting started: the `kajo.bat` menu, then the layout editor |

`themes-tour` is the unhurried walk through the themes that `themes` condenses (a minute against
half a minute). It is not in the cut; render it by name.

`tools/demo/cut.json` also holds the `intro` and `outro` cards of the full cut (a `title`, an
optional `subtitle` and `lines`, how many `seconds` they hold, and the `mark` they carry in the
chapter list).

| File | What |
| --- | --- |
| `<chapter>.mp4` | 1920x1080, 60 fps, BT.709: chapter card, then the display with captions |
| `demo.mp4` | the intro, the chapters of `cut.json` and the outro joined, each dipping to black at its ends, with MP4 chapter markers |
| `demo.chapters.txt` | the same chapters as `m:ss Title` lines, for a video site's description |
| `<chapter>.screen.mkv` | the display alone at 4x, stored losslessly, touch ring included |
| `<chapter>.events.json` | the caption timeline, in frames |
| `<chapter>.gif`, `<chapter>.sheet.png` | with `--gif` (README loop) and `--sheet` (one frame every few seconds) |
| `stills/<chapter>_<name>.png` | 320x240 captures from the scene's `still` steps |

## What the video is, and is not

It is footage of the firmware's own UI code, running on a PC. Every frame carries that in its
footer, with "Demo data": the ride is the [demo ride](demo-mode.md), and the replay ride is the host
fixture's synthetic 47 minutes, read through the production replay parser.

It makes **no performance claim**. Time is simulated, and LVGL refreshes every 30 ms, so a 60 fps
video holds each distinct frame for one or two video frames: the best a CYD could show, not what
one does. Frame time on the real board is unmeasured; see
[Performance claims](simulator.md#performance-claims).

Bluetooth, the SD card, the light sensor and firmware updates are host fixtures. A chapter that
shows one is showing the UI's reaction to a state, not a radio, a card or a photoresistor; the
`updates` chapter scripts the display's side of a transfer, and a time-lapse of one at that. A
chapter says so on its rail: `note "..."` puts a line in the footer above the standing disclosure.

The host also has no backlight, and the LED and the panel profile do nothing. The recorder draws
the brightness setting itself (see below); the other two are not drawn at all, and the `display`
chapter's note says so.

The `start` chapter is not firmware footage and says so on its rail. The layout editor in it is
the real `tools/layout_editor.html` in Chrome, served read-only so nothing can be saved. The
`kajo.bat` menu is **redrawn**: its entries come from `kajo.bat`, its help column from
`scripts/menu_help.txt` and its layout and colours from `scripts/menu.ps1`, but it is a picture of
the menu and not a console recording. The companion-app entry is left out because it exists only
with the private checkout, which a fresh clone does not have.

## How a frame is made

- LVGL advances in 5 ms steps, the cadence of the firmware's `loop()` (`lv_timer_handler()`,
  `delay(5)`). The pointer changes between steps, at the exact millisecond a scene asks, and LVGL
  reads it at the next step.
- A frame is sampled at tick `round(f x 1000 / fps)` ms, between steps, straight from the
  framebuffer. There is no forced full repaint, so a partial repaint shows as the panel would draw
  it.
- The 320x240 frame is upscaled 4x with nearest neighbour. The touch ring is drawn into the video at
  that size, never into the UI.
- The screen layer is stored as lossless H.264 in RGB. One conversion to `yuv420p` (BT.709, tagged)
  happens at the final encode. The display sits at even offsets so that 4:2:0 chroma blocks stay
  inside one display pixel.
- The backlight is drawn as a gain on the picture, in linear light, before the touch ring: manual
  brightness is the app's `displayBrightnessPercent`, and Auto brightness walks toward the sensor's
  target two percent a tick as `serviceAutoBrightness()` does in `src/main_lvgl.cpp` (which the host
  does not compile, so those few lines are repeated in the recorder). The sensor's target is a
  fixture the scene sets with `light`; the Auto *appearance* logic that reads it is the firmware's.
  Stills keep the framebuffer as drawn, and `--no-backlight` turns the effect off.
- `cyd_demo_recorder --digest` prints a hash of every frame the firmware drew and the backlight
  level; ctest runs a scene twice and fails if the two differ.

## Scene scripts

A scene is a text file in `tools/demo/scenes/`: one command per line, `#` starts a comment, strings
are in double quotes, options are `key=value`. It must `boot` exactly once. The whole file is checked
before anything runs, so a typo costs no render.

| Command | Does |
| --- | --- |
| `title "..."`, `subtitle "..."` | the chapter card and the rail heading |
| `note "..."` | a caveat for the rail's footer, above the standing disclosure |
| `boot [dashboard\|first-boot]` | starts the firmware, on the dashboard by default |
| `logging off\|on\|recording` | ride-logging fixture. Off unless asked, as on a stock firmware |
| `card ready\|missing\|checking` | SD card fixture. Ready unless asked |
| `demo off\|1\|5\|15\|30\|60` | demo ride and its speed. Changing speed mid-scene keeps the ride's place |
| `light RAW TARGET` | the front light sensor: its ADC reading (0 to 4095, high in the dark) and the brightness percentage it maps to. Option `over=MS` ramps both |
| `companion off\|preparing\|advertising\|connected\|error ["message"]` | the Bluetooth Link status. Options `seconds=N`, `paused=yes\|no` |
| `update locked\|ready\|preparing\|advertising\|connected\|confirm-downgrade\|receiving\|ready-to-reboot\|error\|cancelled ["message"]` | the update status the display reports. Options `key=yes\|no`, `received=BYTES`, `total=BYTES`. Starts locked, as a build without a release key does |
| `update-request [on\|off]` | the phone asking for update mode, which hands Bluetooth Link over to the update screen |
| `update-progress PERCENT MS` | moves an update in progress to a percentage over the given time |
| `wait MS` | lets time pass |
| `tap X Y` | touch down, hold, release. Options `hold=90`, `settle=300` (ms) |
| `tap-label "TEXT"` | taps the middle of the visible label with exactly this text. Options `nth=1`, `hold`, `settle` |
| `drag X0 Y0 X1 Y1 MS` | a finger moving between two points. Options `ease=smooth\|linear`, `settle` |
| `path MS X0 Y0 X1 Y1 [X Y ...]` | a finger drawing a smooth curve through the points. Options `ease`, `settle` |
| `hold X Y MS` | a long press. Option `settle` |
| `caption "heading" ["body"]` | puts this caption on the rail until the next one. Option `for=MS` ends it earlier |
| `caption-off` | clears the rail's caption |
| `still NAME` | writes the current 320x240 frame to the stills folder |
| `expect-label "TEXT"`, `expect-no-label "TEXT"` | fails the scene unless the label is, or is not, visible |
| `dump` | prints every visible label with its position, for authoring |

`logging`, `card`, `demo`, `light`, `companion` and `update` may come before `boot`, and a scene that
wants a running ride on the dashboard should say `demo` there: the dashboard titles itself DEMO MODE
only if the ride is already running when it is built, and builds its logging pill only if logging
is already on. A `light` ramp and the rest of the update commands need the firmware running.

The fixtures only report: the update fixtures do not move on their own, and the sensor's target is
not computed from its reading. Calibrating the sensor on screen sets the firmware's real dark and
bright levels and checks their range for real, but the target the display shows is the one the
scene last set.

`tap-label` picks the topmost match, so an overlay's label wins over the screen below it, and says
what was on screen when it finds nothing. It also warns when the tap would land on a different
control than the label's own, which means something is covering it. Labels are matched by their
English text, so a scene plays in English only.

Times are scene time. A caption appears from the next frame written and fades in over 8 frames and
out over 6; one that cannot fit above the rail's footer is an error rather than an overlap.

## Writing a scene

```powershell
cyd_demo_recorder tools\demo\scenes\themes.scn --trace      # every step with its time; no encode, under a second
```

Without `--raw` the recorder only plays the scene, which makes a draft quick to check, and the
`dump` step shows what a label is called and where. Then look at what it made:
`python tools\make_demo_video.py <name> --screen-only --sheet --no-build` writes a contact sheet.

The recorder exits 0 when the scene plays, 1 when it fails (a label that is not there, a failed
expectation, an encoder that stopped) and 2 for a script or usage error.

Things the UI does that a scene has to work with:

- A tap on the dashboard shows its controls and another tap hides them, so a scene must know which
  state it is in.
- A theme preview sits on its frozen thumbnail while its controls show. Hiding them resumes the ride
  600 ms later, and previews always run in real time whatever the demo speed.
- Back steps one level at a time: preview, selector (page by page), Settings, dashboard.
- Menus return to the dashboard after 30 s without a touch. Replay does not.
- Some labels exist twice, a pixel apart (the theme names on the selector, for one). Both match
  `tap-label`, and the topmost is tapped.
- The Customize Theme panel belongs to the preview's controls. A tap outside it closes it and brings
  the preview's own controls back; the panel keeps working while Auto appearance flips the theme
  behind it.
- Selecting Auto appearance asks to enable Auto brightness, since both read the same sensor.
- Holding the Display tile in Settings for two seconds opens the developer prompt; the tile fills
  with the accent colour from one second to two.
- The Touch Test hides its buttons while a finger draws and brings them back after about a second.
- The host build draws a sample stroke on the Touch Test that a device does not (a `CYD_LVGL_PREVIEW`
  block in `screens.cpp`; a device starts blank), so a scene taps Clear before anyone can see it.
- An update screen has no way back: a finished or cancelled update restarts the display, so a scene
  ends there.
- The first page of Display Panel prints the host's build time and the Information page its build
  date, so a scene that shows either differs with every build. The `display` chapter goes straight
  to the panel's second page.

## Adding a chapter

1. Write `tools/demo/scenes/<name>.scn`, with a `title` and captions.
2. Add `<name>` to `tools/demo/cut.json`; its position is the chapter number.
3. Render it, and run the tests below.

A chapter that is not the firmware (like `start`) is a Python module instead,
`tools/demo/<name>_capture.py`, whose `capture(out, ffmpeg, fps)` writes `<name>.screen.mkv` at
1280x960 and returns it with the caption events (`frames`, `fps`, `title`, `subtitle`, `note`,
`disclosure`, `captions`). Give it a `disclosure` of its own: the standing footer says the frames
come from the simulator.

## Keeping it working

A UI change that renames or moves a label the scenes tap fails ctest, not the next release video:

| Test | Covers |
| --- | --- |
| `cyd_demo_scene_<name>` | each committed chapter plays to the end |
| `cyd_demo_recorder_smoke`, `_deterministic` | the scene commands once, and the same frame digest on a second run |
| `cyd_demo_recorder_fixtures`, `_deterministic_fixtures` | the sensor, link and update fixtures and the finger path, each update state checked by the label it shows, and the same digest twice |
| `cyd_demo_recorder_missing_label`, `_expectation_failed`, `_unknown_command`, `_before_boot`, `_light_range`, `_path_pairs` | the failures the recorder must name |
| `test/test_make_demo_video.py` | the caption timeline, fades, rail images, cut file, chapter markers, the `start` chapter's menu model, and that every caption fits |

The recorder is `tools/lvgl_native_preview/demo_recorder.cpp`. Like all simulator orchestration it
lives beside the other host tools and adds no behaviour to the firmware sources.
