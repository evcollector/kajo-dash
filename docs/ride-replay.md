# On-device ride replay

Open **Settings → Logging → Ride logs**, then select a completed ride. The ride
still being recorded is listed as `RIDE n | RECORDING` and cannot be opened
until it closes; the list rescans the card when it does. Replay
opens paused. Tap or drag anywhere in the chart bands to seek and pause.
The bottom buttons skip back 10 seconds, play/pause, or skip ahead. Three quick
taps on the same skip button (each within 0.7 s of the last) widen both skip
buttons to 30 seconds; they return to 10 seconds once left alone for 2 seconds.
Skipping keeps playback running; only a chart touch pauses. The header rate
button cycles 1×, 2×, 4×, 10×, 50× and 100×. Playback stops at the end; Play
then restarts it. Back retains the ride-list page. Replay does not
automatically return Home.

The header row is Back, the title, the chart button and the rate button.

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
  three recording periods) are unavailable. Sequential playback reuses the
  read cache; large/backward seeks use the bucket index.
- On ESP32 a cancellable worker reads 1,012-byte chunks (23 records) through the logger's
  serialized SD task. LVGL never performs the scan or seeks synchronously.
  Memory is independent of ride length: about 11 KB for the parser, its read
  cache and the UI overview together, plus a 6 KB worker stack. Initial loading scans the file;
  hardware SD-card load times remain to be measured.
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
`25_replay_four`, `25_replay_four_low`, `25_replay_phase`, `25_replay_two`, `25_replay_one`, `25_ride_logs`, `25_ride_logs_recording`, `25_ride_logs_clearing`, `25_ride_logs_cleared` and `25_ride_logs_clear_failed`. Add `--lang=fi` (or en/de/fr/es/it)
to check translations. Firmware compile: `platformio run -e kajo`.

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

Rendering follows Efficiency: cached per-column heights and clipped horizontal
fill spans. Horizontal gridlines are omitted. Replay keeps flat colors. Each
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
