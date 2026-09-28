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
