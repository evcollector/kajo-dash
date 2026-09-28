# Demo ride

Developer options → Demo Mode provides enable/disable, time compression, and restart.
Compression cycles through **1×, 5×, 15×, 30×, 60×** and is saved with display settings.
The default is **30×**. Changing speed preserves the ride; enabling demo starts a new
ride. Restart resets the trip and battery without changing the selected speed.

The fixed demo vehicle has a 13S3P NMC pack: 39 cells at 3.7 V / 3.5 Ah,
48.1 V nominal, 10.5 Ah, 505.05 Wh, and 54.6 V fully charged. The motor profile
represents 500 W nominal / 1500 W peak, with stops, acceleration, cruise, hills,
coasting and regenerative braking. Its average speed is about 26 km/h.
A complete ride is approximately 57 simulated minutes / 25 km (about 114 real
seconds at 30×). At empty, speed and power become zero and trip time freezes;
use Restart Ride for another run.

`app_logic.cpp` holds a short interpolated road profile and analytically integrates
speed, net power and regeneration across repeated road sections. Trip counters
never reset when a section repeats. Voltage uses a simplified NMC curve and load
sag; temperatures use a simple warm-up curve. These are illustrative demo values,
not a battery or motor engineering model. No real vehicle settings are changed.

Only `serviceDemoMode()` advances time. Telemetry readers share cached values and
battery statistics. Trace and Efficiency history sample simulated seconds so their
time axes match the trip at every compression setting. Theme previews use the same
model with their own paused/resumed clock and cannot enable dashboard demo mode.
Dashboard demo rides are recorded whenever logging is enabled and
are marked as demo rides in the ride list. Switching between controller and
demo telemetry closes the current recording so the two sources never share a
file. Theme previews are not recorded; live logging remains available while
browsing them.

The native `cyd_demo_ride` test covers independent numerical integration, discharge,
restart, rate changes, read-only sampling, preview isolation, timer rollover,
small-step clock drift, actual menu taps, and settings persistence.
