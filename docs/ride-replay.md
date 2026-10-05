# On-device ride replay

Open **Settings → Logging → Ride logs**, then select a completed ride. The ride
still being recorded is listed as `RIDE n | RECORDING` and cannot be opened
until it closes; the list rescans the card when it does. Replay
opens paused. Tap or drag anywhere in the chart bands to seek and pause.
The bottom row is, left to right, zoom out, skip back 10 seconds, play/pause,
skip ahead and zoom in. Three quick
taps on the same skip button (each within 0.7 s of the last) widen both skip
buttons to 30 seconds; they return to 10 seconds once left alone for 2 seconds.
Skipping keeps playback running; only a chart touch pauses. The header rate
button cycles 1×, 2×, 4×, 10×, 50× and 100×. Playback stops at the end; Play
then restarts it. Back retains the ride-list page. Replay does not
automatically return Home.

The header row is Back, the title, the chart button and the rate button.

**Zoom.** The corner buttons zoom the chart in and out, each step halving or
doubling the stretch of the ride it shows, around the cursor: the moment under
the cursor keeps its place on screen unless the end of the ride is in the way.
The change animates over about 280 ms, the traces stretching about the cursor
and contracting the same way on zoom out, and a second tap during it carries on
from the frame in flight. A thin track under the charts shows where the view
sits in the ride, with the stretch on screen lit in the accent colour; the
whole ride has no track. The buttons dim, and stop answering, at the limits:
zoom out on the whole ride, zoom in at the narrowest view, where a recorded
reading is two columns wide (about 29 s at 5 Hz, 14 s at 10 Hz); a ride too
short for that does not zoom. Zoomed in, everything else works on the view:
a tap or drag seeks within it, playback runs through it, and when the cursor
leaves it (running off the end, or a skip) the chart turns the page, to the
next stretch of the same width with the cursor a tenth of the way in, or nine
tenths going back. The ride's scales stay those of the whole ride, so a zoomed
trace is directly comparable with the full one.

- **Title** (`REPLAY RIDE n` over the elapsed/total time) opens the ride
  summary: duration, distance, average and top speed, battery at start and
  end, lowest voltage, the hottest motor and controller temperatures, net
  energy, consumption per distance unit, regenerated energy, peak power, peak
  pack current and peak phase current. Tap anywhere or Done to close it. Longer translations shrink
  the title, then drop the word for "ride". **Delete ride** in its bottom
  right opens a compact confirmation; Cancel returns to the summary, Delete
  closes the reader, removes the file through the logger's writer task (never
  the ride being recorded) and returns to the ride list, whose catalog drops
  the entry at once.
- **Chart button** shows the current layout as coloured bars and opens the
  chart picker. Its heading row has Reset (back to speed, power and voltage)
  beside Done. The next row reads "Number of charts:" followed by 1, 2, 3 and
  4, the current count selected. Below it, one button per chart is stacked in chart order,
  outlined and lettered in that field's colour and splitting the stack's
  height the way the charts split the screen. Tapping a chart opens a grid of
  every field — speed, power, voltage, pack current, phase current, battery,
  motor temperature, controller temperature — with the current one filled; picking one returns
  to the picker, and picking a field another chart already shows swaps the
  two. Done stores the layout in the `replay` preferences namespace for every
  ride. Fields a ride never recorded are dimmed and their chart reads
  "Not recorded".

Opening either popup pauses playback.

The 320×240 UI draws the full-ride traces with flat tinted backgrounds and
solid under-curve fills. Each field has a fixed colour wherever it is shown:
speed cyan, power red, voltage yellow, pack current orange, phase current
lime, battery green, motor temperature magenta and controller temperature
violet. Pack and phase current are deliberately far apart in hue, because they
are the pair most often charted together. The field name sits at the bottom
left of each full-height plot, and the fill follows the trace down to the
bottom row just above the separator. The cursor is a
grey line between two dimmer ones. Cursor bubbles hold the recorded
value and unit, paused or playing; they sit at a fixed height in the middle of
their band. They sit together left of the cursor from a quarter of the way
across the plot onward and right of it before that; just past the switch a
wide bubble may run over the scale labels. The switch point is fixed, so a
bubble that widens as its value changes — power entering regen, say — cannot
make them hop from side to side. Speed respects metric/imperial settings. Speed
starts at zero. Power and both current axes include zero and extend below it
when negative readings occur; the bubble keeps the signed value (power adds
`(regen)`). Phase current is the
motor side: it runs several times the pack figure away from full duty, so it
peaks under load at low speed rather than at peak power. Only VESC reports it;
a FarDriver ride charts it as "Not recorded". Voltage, battery and
temperatures fit the ride's range. Each field has its own fixed scale for the
loaded ride. `--` indicates unavailable data; `gaps` beside the time
indicates skipped CRC-invalid records.

## Data path

- `ride_replay_core` validates the V3 KAJL header, record size, file length,
  ride ID, CRCs and timestamp order. Unsupported, empty and unreadable files
  fail without touching live telemetry. A partial trailing record — what losing
  power mid-append leaves behind — is ignored rather than failing the ride, the
  same way the ride list counts it. See [ride log format](ride-log-format.md).
- A full scan creates 128 time buckets with each field's minimum and maximum
  over that interval and a record offset index, plus whole-ride per-field
  extremes that set the scales.
  Buckets affected by missing fields, corrupt records or time gaps are
  conservatively left blank. This can widen a short gap to one overview
  bucket on a long ride.
- The same scan produces the summary totals. The logged trip counters
  (distance, net and regenerated energy) run across rides rather than being
  reset for each, so a ride's totals are their growth: consumption (net +
  regen, which only grows) and regen are summed record to record, skipping
  any drop where a counter was reset mid-ride, and net = consumption − regen.
  Rides without counters fall back to integrating recorded speed and power
  between consecutive good samples, never across a time gap.
- The ride list applies the same rule from each file's first good record to
  its last (`ride_replay::counterGrowth`), so list and summary agree; with a
  mid-ride counter reset the list counts only the part after the reset.
- Seeking uses elapsed timestamps and returns the preceding recorded sample,
  without inventing intermediate values. Samples older than max(1 second,
  three recording periods) are unavailable. A forward seek resumes the previous
  scan from where it stopped, so playback at any rate reads each record once;
  backward seeks, and jumps that leave the previous scan behind, start from the
  bucket index.
- On ESP32 a cancellable worker reads 2,024-byte chunks (46 records) through the
  logger's serialized SD task. LVGL never performs the scan or seeks
  synchronously. Memory is independent of ride length: about 11.5 KB for the
  parser (its overview and read cache) and 9.4 KB for the UI's copy of the
  overview, plus a 6 KB worker stack and the logger's 2 KB transfer buffer.
  Initial loading scans the file.
- Every read is a round trip through the writer task, so reads are what loading
  and seeking cost. The writer keeps the ride it is reading open between chunks
  instead of reopening it for each one, which in the Arduino file layer meant
  three path lookups and a 4 KB stdio refill for about 1 KB of data. A 27 minute
  ride loads in 180 reads at 5 Hz and 356 at 10 Hz (372 and 740 with 1,012-byte
  chunks). 100x playback needs about one read per frame at 5 Hz and two at 10 Hz
  (it needed five and nine); a tap or drag costs one to five. `cyd_ride_replay`
  asserts these budgets. Hardware SD-card load times remain to be measured.
- Zoomed views are built from windows. The overview is 128 buckets, too coarse
  once the chart shows a fraction of the ride, so zooming asks the reader for a
  window: the chart's own 288 columns for that stretch. Each column holds, per
  field, the lowest reading in its slice of time as a position 0 to 254 in the
  ride's scale (one byte; 255 for nothing), exactly the overview's rule at a finer
  grain. Where the recording is sparser than the columns a reading is held across
  the columns up to the next one, as far as the recording's own gap limit allows,
  so the chart steps as the data does, and a real hole stays a hole; a corrupt
  reading or a missing field ends the run. The reader scans only that stretch,
  starting from the overview's index, a couple of reads at a time with seeks
  answered between slices. A 27 minute ride at 10 Hz costs 180 reads for half of
  it, then 91, 47, 25, 14 and 9 for each further step (at 5 Hz 90, 46, 24, 13 and
  7, the narrowest view coming a step sooner); `cyd_ride_replay` builds them
  against an independent definition of each column. Until a window arrives the overview is stretched over the view, and a
  window takes over from it without any repositioning, because it is built for
  exactly the view on screen. Windows are built one at a time and a newer
  request replaces an older one; the columns are allocated on the first zoom
  (2.3 KB in the reader, 2.3 KB for the screen's copy) so a ride that is never
  zoomed pays nothing.
- Playback nearing the end of a window (60 % of the way) asks for the next page
  while there is time, so its columns are usually waiting when the cursor
  arrives; a skip or seek that lands somewhere new asks when it lands, and the
  stretched overview covers the short wait.
- Replay owns separate samples and a playback clock. It does not inject data
  into controller snapshots, battery statistics, live dashboards or logging.
  Active recording files are rejected by the existing logger export path.
- Back removes the LVGL timer and cancels the reader. Card removal stops
  playback and shows an unavailable state; reinsert the card and reopen from
  the ride list. Cancellation waits for any in-flight SD operation to finish
  in the worker; it does not block the UI.

## Verification and real UI captures

The native preview generates CRC-protected V3 fixture bytes and feeds them
through the production parser. It includes a voltage availability gap and a
phase-current trace that peaks away from peak power.
These captures are actual LVGL output with synthetic ride data, not mockups:

```powershell
cmake --build tools/lvgl_native_preview/build_simulator --config Release --target cyd_lvgl_preview cyd_simulator cyd_ride_replay_test cyd_runtime_input_test
ctest --test-dir tools/lvgl_native_preview/build_simulator -C Release -R 'cyd_(ride_replay|runtime_input)$' --output-on-failure
& tools/lvgl_native_preview/build_simulator/Release/cyd_lvgl_renderer.exe preview_output/replay 25_replay
```

Other capture names: `25_replay_edge`, `25_replay_playing`, `25_replay_gap`,
`25_replay_no_card`, `25_replay_regen`, `25_replay_swap`, `25_replay_summary`, `25_replay_delete`,
`25_replay_charts`, `25_replay_charts_two`, `25_replay_fields`,
`25_replay_four`, `25_replay_four_low`, `25_replay_phase`, `25_replay_two`, `25_replay_one`,
`25_replay_zoom`, `25_replay_zoom_max`, `25_replay_zoom_four`, `25_ride_logs`, `25_ride_logs_recording`, `25_ride_logs_clearing`, `25_ride_logs_cleared` and `25_ride_logs_clear_failed`. Add `--lang=fi` (or en/de/fr/es/it)
to check translations. Firmware compile: `platformio run -e kajo`.

`python tools/render_lvgl_native.py` also renders every `25_replay*` state into
`preview_output/lvgl/` with the rest of the preview set, so they appear on
`contact_sheet.png`.

Parser tests cover malformed headers, CRC errors, partial trailing records, time gaps,
missing fields, regeneration, peak preservation, seek bounds, backwards
seeking, sequential playback read cost, the scales of every chartable field,
counter-growth distance and energy across a reset, phase current decoding and
its extremes, and the integrated fallbacks. The native input test covers ride selection, tap/drag seeking,
skipping, rate, pause, restart, the chart picker (chart order, field list,
swap, count choices, Reset, stored layout, charting phase current), the
summary, deleting a ride
(Cancel, then Delete back to the shortened list), Back,
card removal/reopening and separation from live data. Physical CYD touch,
SD latency and simultaneous recording still need a device smoke test.

`cyd_replay_zoom` drives the real screen through the zoom buttons: every step
in and out (each frame of the animation compared with a repaint from scratch),
the limits and the dimmed buttons, two quick taps, zooming at either end of the
ride, coming back out to exactly the picture the ride opened with, playing
through many page turns at 100x, with a slow reader (windows arriving over many
ticks, prefetched pages arriving in time at 50x) and with a very slow one (the
stretched overview standing in until the window arrives), skips and taps that
leave the view in both directions, a layout change while zoomed, and the cost
of an animation frame.

Rendering follows Efficiency: cached per-column heights and clipped horizontal
fill spans, and damage that is as small as the change (below). Horizontal
gridlines are omitted. Replay keeps flat colors. Each
column uses its bucket's minimum for both the trace and the fill edge, so the
fill cannot rise above the visible line. Neither is smoothed, so a column
that touches the top or bottom of a band is a moment the ride reached that
scale value. Each column holds the minimum of the bucket it falls in, picked
by the same mapping the cursor uses. A mean put the trace where the ride never was, and
interpolating between bucket centres narrowed a bucket that reached the end of
the scale to the single column at its centre -- a one-pixel spike, which reads
as a trace that never reaches zero while coasting. Cursor values remain
original recorded samples.
Left labels, right-aligned against the plot, show the scale maximum at the top
and the minimum at the bottom of the band. Speed starts at zero; signed power
and current include zero, while voltage fits the ride range.

### Repainting

Between ticks only the overlay is repainted. The cursor lines and each chart's
dot and bubble are kept as data (`ReplayOverlay` in `ride_replay_screen.inc`),
and a tick invalidates the old and new place of whatever differs from the last
tick: a strip around the cursor, a bubble (just its text when only its value
changed), a dot. A tick that changed nothing paints nothing. The traces
underneath repaint only where one of those areas reaches them; the whole chart
repaints only when the ride finishes loading, the layout changes, or the card
goes. LVGL pads every dirty area by 5 px, which the figures below include. Old
and new places that touch are invalidated as one area, because LVGL holds 32
dirty areas per frame and repaints the whole screen beyond that.

`cyd_replay_redraw` drives the real screen through taps, drags, every playback
rate, skips, every chart layout, both popups and card removal. After each step
it renders what is dirty, repaints the whole screen, and requires the two to be
the same picture: it fails if the old cursor, a dot or a stale value is left
behind. It also budgets the flushed pixels. On the 47 minute test ride, per
100 ms tick, a paused replay flushes nothing, playing at 1x about 1,100 pixels,
10x 6,200 and 100x 12,700, where every tick used to flush 55,000 to 59,000. A
tap or one drag step is 15,000 to 17,000 (59,000 before). At 100x about a third
of that is the header's elapsed-time label, which changes every tick. These are
display-transfer figures on the 40 MHz panel link, not measured ESP32 frame
times; see [dashboard rendering performance](dashboard-rendering-performance.md)
for the table and how to measure on the device.
