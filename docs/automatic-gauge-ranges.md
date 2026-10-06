# Automatic gauge ranges

Vehicle configuration → Gauge Ranges defaults to **Automatic**. **Manual** restores
the existing speed, power, battery-current and motor-current fields. These are
visual display limits; this feature never changes a controller setting.

Automatic ceilings start at 30 km/h, 500 W, 25 A battery current, 50 A motor
current, 500 W regen and 25 Wh/km efficiency. The current controller snapshot API
provides no verified configured limits, so stored manual values are not treated
as controller-reported limits.

A ceiling follows the peak. A reading above it pushes it up at once, to the exact
peak rounded up to a whole unit (35, not a readable step), so a needle at the end
of a dial stays at the end while the speed keeps rising and nothing moves when the
scale follows. When the reading falls back the scale stays where the peak put it:
a 35 km/h peak leaves a 0-35 dial. A reading only counts once it has held for two
samples (the smaller of two consecutive readings is used), so one corrupt packet
never moves a scale; missing, non-finite, negative or implausible measurements do
not train it, and a gap over 1.5 seconds restarts that confirmation. Efficiency
needs at least 8 km/h and is smoothed over about 3 seconds, so a launch transient
does not set its scale.

Ceilings also shrink. Each range keeps the peak of every minute of the last 15
minutes of riding (only moving time counts, so a stop or a lunch break does not
age it), and the ceiling is the largest of those, never below its default. When
the old peak ages out the ceiling falls back, but only if the new scale is at
least 15% smaller and the reading is below 60% of it, so a needle never moves
because the scale shrank. **Reset Learned Ranges** starts learning again. The
speed ceiling persists independently for VESC UART, VESC BLE and FarDriver BLE
(automatic saves occur after a ten-second stop, no more than once per minute);
it is restored at boot as the last ride's peak and ages out like any other.
Other ranges start fresh at boot. State is per backend, not per controller
address: reset learning when changing vehicles on the same backend. Dashboard
demo and theme previews have separate transient state.

All themes use the shared maxima where they draw these metrics. The dashboard
follows a rising ceiling immediately and eases a falling one over 2 seconds. Ticks,
needles, bars, numeric scale labels, the Efficiency dial, and Trace history use the
same displayed ceiling. A reset, manual-mode switch, source change, or dashboard
rebuild snaps to the new range. Moving speed, power and current indicators also
use a 160 ms response filter, while their numeric readouts, telemetry values and
learned/persisted ceilings remain live. The indicator catches up immediately after
a long update gap and clears old drive power when it crosses into regen.
Battery percentage stays 0–100%; temperature scales and warning thresholds remain
fixed. Regen has its own scale on the bidirectional power beam. Trace retains raw
measurements so historical values remain accurate when the scale changes.

`cyd_gauge_ranges` checks spike rejection, pushing to the exact peak, shrinking after
15 minutes of riding (and not while stopped or while the reading is high), duplicate
timestamps, clock rollover, the displayed ceiling following and easing, persistence,
backend/preview isolation, manual/reset menu actions, and incremental versus full
redraws on all twelve themes. `cyd_motor_data_glide` checks that Motor Data's ring stays
pegged while a rising speed pushes the scale.

Native preview states `02_dual_gauge_range_transition` and
`09_redline_range_transition` push the scales up with a 60 km/h, 2 kW burst, ride
gently for 15 minutes so the burst ages out, and capture a 320x240 frame partway
through the ease back down. Check the Redline state in English,
Finnish and German for scale-label fit.
