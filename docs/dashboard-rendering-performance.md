# Dashboard redraw optimization — 2026-09-07

Cyber HUD and Dual Gauge now avoid repainting pixels whose content has not
changed. Layout, fonts, telemetry cadence, and displayed values remain
unchanged. Cyber HUD gauge interiors now show the dashboard background directly,
with a black-and-white default light palette. No framebuffer cache or
additional LVGL widgets were introduced.

## Findings and changes

- Cyber HUD invalidated the complete 320×108 paired meter object whenever
  either lit-block count changed. It now invalidates the bounding rectangle of
  the changed active blocks, separately for each meter. Large jumps use one
  rectangle per meter instead of exhausting LVGL's damage-entry buffer.
- HUD polygon drawing now rejects polygons outside the current drawing clip
  before entering LVGL's temporary-buffer allocation and polygon processing.
- Dashboard data-slot updates reapplied unchanged icon descriptors each tick.
  LVGL 8 invalidates an image even when its descriptor is unchanged. The shared
  `setIconSource` helper now checks descriptor and backing-buffer identity first.
- Dual Gauge needles already had tight object bounds and its ticks already
  updated only when their lit state changed. The remaining avoidable needle
  work was reacting to watt changes too small to move either endpoint a pixel.
  The shared tick-needle helper now skips those layout/paint updates. Redline
  also uses this helper.

## Native measurements

The table below records the first optimization pass, before the flat-face
design change and tick-damage batching described below.

The new `cyd_dashboard_redraw` CTest target runs the real firmware drawing code
with the same 320×240 RGB565 framebuffer and 26-line buffers. Each theme is
tested in dark/light appearance and English/Finnish/German. Each configuration
has 100 updates: a rising/falling speed/power sweep, followed by fixed speed
and small power changes. Secondary telemetry is held constant; all update
tiers are requested so unchanged-field work is included.

Mean pixels flushed per update, aggregated across the six configurations:

| Theme | Before | After | Reduction |
| --- | ---: | ---: | ---: |
| Cyber HUD | 29,395 | 14,468 | 50.8% |
| Dual Gauge | 16,827 | 13,963 | 17.0% |
| Redline (shared-helper control) | 14,282 | 14,282 | 0.0% |

All 1,800 incremental frame hashes matched the pre-change baseline. Every
incremental frame also matched a subsequent full redraw, checking for stale
pixels on falling meters, needle movement, and digit-width changes. The test
additionally requires identical-value updates to produce no flushes and a
small HUD change to repaint less than the complete meter housing.

These are native display-transfer workload measurements, **not measured CYD
frame times or FPS**. Different telemetry, custom fields, backgrounds, and
controller/SD activity will produce different results.

## Flat faces and bounded tick damage

The follow-up removes each HUD housing's 16 layered polygon fills entirely.
The interiors show the underlying dashboard background, including custom
solid colors and gradients. This removes all face fills per drawing callback,
including callbacks caused by digit updates. It also removes the
cached inset-shade geometry. Default light mode is monochrome, including the
battery indicator; explicit accent selections remain colored.

Tick dials now register a union of changed ticks before applying their style
changes. The union includes old/new stroke extents so LVGL can discard the
contained per-tick invalidations. This bounds damage entries without enlarging
the invalidation queue or framebuffer. In the original 600-update Dual Gauge
sequence, the six full-screen fallbacks are gone: step 60 falls from 76,800 to
20,945 pixels. The expanded suite also exercises full-scale-to-zero jumps;
all 618 Dual Gauge updates avoid full-screen fallbacks.

The test now covers 1,854 incremental/full-redraw comparisons and asserts a
neutral default light HUD palette, no redraw on unchanged telemetry, and no
full-screen fallback for Dual Gauge.

A 2 KiB gradient-cache trial failed pixel comparisons on all four bell
background variants. This LVGL implementation keys maps by descriptor pointer
and dimensions; opposing equal-size halves can reuse the wrong map. The cache
therefore remains disabled. The regression test compares the configured cache
against uncached rendering across ordinary/bell, horizontal/vertical, and
normal/reversed backgrounds, so a future cache change must preserve output.
The 26-line draw buffers also remain unchanged.

## Reproduce

Configure/build the normal native tests, then run:

```powershell
ctest --test-dir build/dashboard-perf -C Release -R cyd_dashboard_redraw --output-on-failure
& build/dashboard-perf/Release/cyd_dashboard_redraw_test.exe build/dashboard-perf/captures > build/dashboard-perf/metrics.csv
```

The optional output directory receives 320×240 PPM captures. The CSV contains
per-frame pixels, flushes, and framebuffer hashes. The test calls
`lv_refr_now(display())` for incremental measurements: the native screenshot
helper intentionally invalidates the entire screen and must not be used to
measure live damage.

For hardware verification, use the existing five-second `LVGL perf` serial
reports (`LVGL_PERFORMANCE_LOG_ENABLED` in `include/config.h`). Compare the
same controller or demo sequence before/after, on the same theme and settings,
with identical BLE/SD activity. Record handler average/max, pixels per second,
and flush wait time; verify touch responsiveness, needle trails, and both
light/dark appearances. No device was flashed as part of this change.


## Efficiency rendering (2026-09-11)

Efficiency now damages the old/new needle sweep and changed arc interval,
rather than its entire 320x114 housing. Cached per-dial/segment bounds reject
unrelated render clips; segment colors and bounds are prepared once per build.
The plot object is restricted to its actual 301x35 drawing bounds so it no
longer overlaps the dial housing. History scale, interpolated heights and row
colors are prepared once per sample; the draw callback only scans clipped rows
and columns. Axis colors are set during construction, not on every sample.
These caches use approximately 1.3 KB of static memory; no canvas is allocated.

The efficiency needle uses elapsed-time exponential smoothing with a 350 ms
time constant, updated on the normal telemetry tick. History still samples
once per second and retains 300 samples. The center number retains its existing
ride-average meaning; the needle continues to represent live consumption.

`cyd_efficiency_redraw_test` compares 6,400 incremental frames with full
redraws in dark/light modes, including a seeded history, an empty history,
more than five minutes of samples, scrolling, flat consumption, coasting,
large speed changes and sub-second needle response. Its small-change frame
must stay below 18,000 pixels and must not intersect the plot. Native pixel
counts measure transfer workload, not physical ESP32 CPU time. Use on-device
handler timing to quantify the real refresh-rate improvement.


## Ride replay rendering (2026-10-04)

The replay screen invalidated its whole 320x159 chart on every 100 ms tick that
moved the cursor, and on every touch event while a finger dragged it: about
55,000 flushed pixels, 22 ms of the 40 MHz panel link by the native model, for
a bubble that changed by a digit. It now keeps the cursor, dots and bubbles as
data and invalidates the old and new place of whatever changed (a strip around
the cursor, a bubble or only its text, a dot). A tick that changed nothing
paints nothing; the rate label, which used to be rewritten every tick, is only
rewritten when it changes. The draw callback skips bands the repaint cannot
reach and draws trace segments only for the columns it covers. A full repaint
draws exactly what it did before: all 22 replay captures (English and Finnish)
are byte-identical to the previous build.

Native flush workload on the 47 minute test ride, mean pixels per step (the
playback rows are per 100 ms tick; before and after are the same scripted
session on the old and new draw code):

| | Before | After |
| --- | ---: | ---: |
| Paused, untouched | not measured | 0 |
| Playing 1x | 55,600 | 1,100 |
| Playing 2x | 55,700 | 2,100 |
| Playing 4x | 56,500 | 2,900 |
| Playing 10x | 59,100 | 6,200 |
| Playing 50x | 59,100 | 9,500 |
| Playing 100x | 59,300 | 12,700 |
| A tap on the chart (press, release, settle) | 59,300 | 16,900 |
| One 40 ms drag step | 58,800 | 14,900 |

A tap or a drag step is the most that moves: three bubbles, their dots and the
cursor strip each change place, every one padded by LVGL's 5 px. At 100x about
4,000 of the per-tick figure is the header's elapsed-time label, which changes
every tick; it is the largest remaining cost and unchanged by this work.

`cyd_replay_redraw` is the regression test: after every step of a scripted
session (taps, drags, all playback rates, skips, layouts, popups, card
removal) the partial repaint must equal a full repaint, and the pixel budgets
above must hold. Deleting any one of the three invalidations (old cursor
strip, a bubble's text, a dot) makes it fail.

These are display-transfer figures, **not measured CYD frame times**. To
measure on the device, play the same ride at the same rate before and after and
compare the `LVGL perf` serial reports (`pixels_s` and the handler average).


## Ride replay zoom (2026-10-04)

Zooming the replay chart animates the view and, once it is narrower than the
overview can serve, reads that stretch of the ride from the card. Both are
measured here the way the rest of this document is: on the host, counting what
the display and the card would be asked to do. **Neither figure is a measured
CYD time.**

**The animation.** The view changes by a constant factor per frame with the
cursor's moment held in place, for 280 ms (twice `kMotionMs`) under the same
`lv_anim_path_ease_out` as the selector. Every frame repaints the whole chart:
about 56,000 pixels, 22 ms of the 40 MHz panel link by the native model, over
roughly 1,300 rectangles and 6,000 line segments. The host renders 8 frames in
the 280 ms; the target has to render each one on a 240 MHz core as well as send
it, so expect it to manage fewer, probably 4 to 8 at 10 to 20 frames a second
(an estimate from the transfer time and the draw-call count; the `LVGL perf`
`handler_us` and `pixels_s` during a zoom give the real figure). Because the
animation is timed rather than stepped, a slow frame skips ahead and the zoom
still takes 280 ms; the ease-out spends most of the motion in the first frames.
Between taps nothing animates, and playback stays on the cursor-only repaint
above (about 1,900 pixels a tick at 1x zoomed in, against 56,000 for a frame of
the animation).

**The windows.** A zoomed view's columns come from a window the reader builds
for exactly that stretch, a couple of reads at a time between answering seeks.
It reads only that stretch of the file, from the overview's index:

| Zoom step | Stretch of a 27 minute ride | Reads at 5 Hz | Reads at 10 Hz |
| ---: | ---: | ---: | ---: |
| 1 | 13.5 min | 90 | 180 |
| 2 | 6.8 min | 46 | 91 |
| 3 | 3.4 min | 24 | 47 |
| 4 | 1.7 min | 13 | 25 |
| 5 | 51 s | 7 | 14 |
| 6 | 25 s | not reachable | 9 |

The narrowest view comes a step sooner at 5 Hz, where a reading is twice as
wide. At the 3 to 7 ms a 2 KB read costs the writer task with the file held open
(an estimate, not a measurement) that is 0.5 to 1.3 s for the first step at
10 Hz, 0.15 to 0.3 s by the third, and nothing noticeable beyond; the host
spends under 2.2 ms of CPU on the largest, a few tens of milliseconds on the
chip. The window arrives after the animation for the first steps and with it
for the rest, and the overview, stretched, fills in meanwhile. Playback turns
the page by asking for the next window at 60 % of the way through the current
one, so at 10x there are tens of seconds to build it even at the first step.
The columns are allocated on the first zoom: 2.3 KB in the reader and 2.3 KB for
the screen's copy.

`cyd_replay_zoom` compares every animation frame and every page turn with a
repaint from scratch and checks the page turns with an instant reader, a slow
one and a very slow one. The window builder is tested in `cyd_ride_replay`
against an independent definition of each column, including held readings,
holes, corrupt records and missing fields, and the read counts above.
