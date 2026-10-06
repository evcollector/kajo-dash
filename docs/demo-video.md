# Demo video

The demo video is rendered from the [simulator](simulator.md), not filmed or screen-recorded. A
scene script drives the real firmware UI with touch input, the recorder plays it in simulated
time and writes every frame, and `tools/make_demo_video.py` adds a chapter card and captions and
encodes H.264. The same script gives the same pixels on every machine, and a script that no longer
matches the UI fails instead of recording the wrong thing.

## Rendering

Needs what the [simulator](development.md#simulator) needs (an installed
`.pio/libdeps/kajo/lvgl`, CMake, Visual Studio C++ Build Tools, Pillow) and `ffmpeg` in `PATH`
(or its location in `FFMPEG`).

```powershell
python tools\make_demo_video.py                  # every chapter in tools\demo\cut.json, then demo.mp4
python tools\make_demo_video.py themes --gif     # one chapter, plus a README GIF
python tools\make_demo_video.py replay --screen-only --sheet --no-build   # authoring: no dressing
```

Both chapters render in about a minute and a half, GIFs and sheets included; `--quick` trades size
for speed while drafting. Everything lands in `dist/demo/`, which git ignores.

| File | What |
| --- | --- |
| `<chapter>.mp4` | 1920x1080, 60 fps, BT.709: chapter card, then the display with captions |
| `demo.mp4` | the chapters of `cut.json` joined, each dipping to black at its ends |
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

Bluetooth, the SD card and firmware updates are host fixtures. A chapter that shows one is
showing the UI's reaction to a state, not a radio or a card.

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
- `cyd_demo_recorder --digest` prints a hash of every frame the firmware drew; ctest runs a scene
  twice and fails if the two differ.

## Scene scripts

A scene is a text file in `tools/demo/scenes/`: one command per line, `#` starts a comment, strings
are in double quotes, options are `key=value`. It must `boot` exactly once. The whole file is checked
before anything runs, so a typo costs no render.

| Command | Does |
| --- | --- |
| `title "..."`, `subtitle "..."` | the chapter card and the rail heading |
| `boot [dashboard\|first-boot]` | starts the firmware, on the dashboard by default |
| `logging off\|on\|recording` | ride-logging fixture. Off unless asked, as on a stock firmware |
| `card ready\|missing\|checking` | SD card fixture. Ready unless asked |
| `demo off\|1\|5\|15\|30\|60` | demo ride and its speed. Changing speed mid-scene keeps the ride's place |
| `wait MS` | lets time pass |
| `tap X Y` | touch down, hold, release. Options `hold=90`, `settle=300` (ms) |
| `tap-label "TEXT"` | taps the middle of the visible label with exactly this text. Options `nth=1`, `hold`, `settle` |
| `drag X0 Y0 X1 Y1 MS` | a finger moving between two points. Options `ease=smooth\|linear`, `settle` |
| `hold X Y MS` | a long press. Option `settle` |
| `caption "heading" ["body"]` | puts this caption on the rail until the next one. Option `for=MS` ends it earlier |
| `caption-off` | clears the rail's caption |
| `still NAME` | writes the current 320x240 frame to the stills folder |
| `expect-label "TEXT"`, `expect-no-label "TEXT"` | fails the scene unless the label is, or is not, visible |
| `dump` | prints every visible label with its position, for authoring |

`logging`, `card` and `demo` may come before `boot`, and a scene that wants a running ride on the
dashboard should say `demo` there: the dashboard titles itself DEMO MODE only if the ride is already
running when it is built, and builds its logging pill only if logging is already on.

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

## Adding a chapter

1. Write `tools/demo/scenes/<name>.scn`, with a `title` and captions.
2. Add `<name>` to `tools/demo/cut.json`; its position is the chapter number.
3. Render it, and run the tests below.

## Keeping it working

A UI change that renames or moves a label the scenes tap fails ctest, not the next release video:

| Test | Covers |
| --- | --- |
| `cyd_demo_scene_<name>` | each committed chapter plays to the end |
| `cyd_demo_recorder_smoke`, `_deterministic` | every command once, and the same frame digest on a second run |
| `cyd_demo_recorder_missing_label`, `_expectation_failed`, `_unknown_command`, `_before_boot` | the failures the recorder must name |
| `test/test_make_demo_video.py` | the caption timeline, fades, rail images, cut file, and that every caption fits |

The recorder is `tools/lvgl_native_preview/demo_recorder.cpp`. Like all simulator orchestration it
lives beside the other host tools and adds no behaviour to the firmware sources.
