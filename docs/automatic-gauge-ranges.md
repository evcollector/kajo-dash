# Automatic gauge ranges

Vehicle configuration → Gauge Ranges defaults to **Automatic**. **Manual** restores
the existing speed, power, battery-current and motor-current fields. These are
visual display limits; this feature never changes a controller setting.

Automatic ceilings start at 30 km/h, 1000 W, 25 A battery current, 100 A motor
current, 500 W regen and 40 Wh/km efficiency. The current controller snapshot API
provides no verified configured limits, so stored manual values are not treated
as controller-reported limits.

A reading at 90% of its ceiling must persist for 1 second for speed, 300 ms for
power/current/regen, or 5 seconds for efficiency. Large overruns can expand after
100 ms, with at least two distinct samples. Efficiency requires at least 8 km/h
and never uses the fast overrun path. Missing, non-finite, negative or implausible
measurements do not train the range. Gaps over 1.5 seconds restart confirmation.
Ceilings round upward to readable values with at least 20% headroom.

Ranges grow and hold, without shrinking while coasting or during short stops.
**Reset Learned Ranges** starts learning again. Speed ceilings persist independently
for VESC UART, VESC BLE and FarDriver BLE; automatic saves occur after a ten-second
stop, no more than once per minute. Other ranges start fresh at boot. State is
per backend, not per controller address: reset learning when changing vehicles on
the same backend. Dashboard demo and theme previews have separate transient state.

All themes use the shared maxima where they draw these metrics. The learned
ceiling still changes in readable steps, but the dashboard moves a separate
display ceiling toward each new step over 900 ms. Ticks, needles, bars, numeric
scale labels, the Efficiency dial, and Trace history use the same intermediate
ceiling. A later expansion continues from the current display ceiling. A reset,
manual-mode switch, source change, or dashboard rebuild snaps to the new range.
Moving speed, power and current indicators also use a 160 ms response filter,
while their numeric readouts, telemetry values and learned/persisted ceilings
remain live. The indicator catches up immediately after a long update gap and
clears old drive power when it crosses into regen.
Battery percentage stays 0–100%; temperature scales and warning thresholds remain
fixed. Regen has its own scale on the bidirectional power beam. Trace retains raw
measurements so historical values remain accurate when the scale changes.

`cyd_gauge_ranges` checks spike rejection, dwell times, duplicate timestamps, clock
rollover, visual transition and interruption, persistence, backend/preview isolation,
manual/reset menu actions, and incremental versus full redraws throughout the
transition on all twelve themes.

Native preview states `02_dual_gauge_range_transition` and
`09_redline_range_transition` hold 28 km/h and 900 W through a learned-range
change and capture a 320x240 midpoint frame. Check the Redline state in English,
Finnish and German for scale-label fit.
