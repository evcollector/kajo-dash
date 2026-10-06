# Demo ride

Developer options → Demo Mode provides enable/disable, time compression, and restart.
Compression cycles through **1×, 5×, 15×, 30×, 60×** and is saved with display settings.
The default is **30×**. Changing speed preserves the ride; enabling demo starts a new
ride. Restart resets the trip and battery without changing the selected speed.

The fixed demo vehicle has a 13S3P NMC pack: 39 cells at 3.7 V / 3.5 Ah,
48.1 V nominal, 10.5 Ah, 505.05 Wh, and 54.6 V fully charged. The motor profile
is driven hard on purpose, to exercise the dials and gauges: a 120 s lap holds fourteen
launches and stops of 1.2 to 2.2 m/s², about 30 km/h out of 7 to 14, with short
cruises between. Power peaks near 2.4 kW on a launch and regenerates about 0.9 kW on a
hard stop, and the bike never fully stops after the first launch. Its average speed is
about 23 km/h. A complete ride is approximately 80 simulated minutes / 30 km (about
160 real seconds at 30×). When the pack empties, the running demo automatically
starts a new ride with a full battery and reset trip counters. Restart Ride can
also start a new ride manually without changing the selected speed.

`app_logic.cpp` holds a short interpolated road profile and analytically integrates
speed, net power and regeneration across repeated road sections. Trip counters
never reset when a section repeats. Voltage uses a simplified NMC curve and load
sag; temperatures use a simple warm-up curve. These are illustrative demo values,
not a battery or motor engineering model. No real vehicle settings are changed.

Only `serviceDemoMode()` advances time. Telemetry readers share cached values and
battery statistics. Trace and Efficiency history sample simulated seconds so their
time axes match the trip at every compression setting. Theme previews use the same
model with their own paused/resumed clock, always at 1× whatever the demo speed, and cannot
enable dashboard demo mode. A frozen
theme preview resumes at 21 s, just ahead of the lap's first hard stop, so the dials are
already moving when the demo is released.
Dashboard demo rides are recorded whenever logging is enabled and
are marked as demo rides in the ride list. Switching between controller and
demo telemetry closes the current recording so the two sources never share a
file. Theme previews are not recorded; live logging remains available while
browsing them.

The native `cyd_demo_ride` test covers independent numerical integration, discharge,
restart, rate changes, read-only sampling, preview isolation, timer rollover,
small-step clock drift, actual menu taps, and settings persistence.
