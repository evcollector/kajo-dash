# Native simulator

The simulator compiles the real LVGL UI and the shared application sources for
x86 and runs them on Windows as a 320x240 virtual CYD. It replaces the hardware
boundary with deterministic host input, time, persistence and semantic fixture
state.

There is no second desktop implementation of any screen: the same
`app_logic.cpp`, `controller_manager.cpp`, `dashboards.cpp`, `screens.cpp` and
`ui_common.cpp` that the `kajo` firmware builds are linked here against the
same LVGL version, fonts and assets, and rendered into the same 320x240 RGB565
framebuffer with the same 26-line partial-buffer shape.

It is for UI, behaviour and visual verification. It is not an ESP32 electrical
or timing emulator, and it never gates a performance claim — see
[Performance claims](#performance-claims).

## Ways to run it

Everything lives under `tools/lvgl_native_preview/` and builds with standalone
CMake and MSVC.

**Interactive** — an SDL2 window over the shared framebuffer, driven by mouse:

```powershell
scripts/run-sim.ps1
```

It builds `cyd_simulator` and launches it. Useful arguments pass straight
through: `--headless`, `--scale N`, `--fixed-timestep`, `--reset-state`,
`--state-file=<path>`, `--start=<screen>`, `--screenshot=<path>`,
`--duration-ms=N` and `--perf-overlay`. Logical resolution is always exactly
320x240; scaling enlarges the window without moving LVGL coordinates or
changing screenshot resolution.

**State captures** — `cyd_lvgl_renderer` renders named screens to PPM and is
what the documentation screenshots come from:

```powershell
cmake --build tools/lvgl_native_preview/build_simulator --config Release --target cyd_lvgl_preview
& tools/lvgl_native_preview/build_simulator/Release/cyd_lvgl_renderer.exe preview_output/lvgl 01_cyber_hud
```

It takes an output directory, an optional screen name, and
`--lang=en|fi|de|fr|es|it`, `--accent=…`, `--appearance=dark|light|auto` and
`--top-speed=`. `tools/render_lvgl_native.py` drives it over the whole catalog
and builds the contact sheet. Note that it launches one process per screen, so
faults that need several screens in one process do not appear there — see
[Debugging a native crash](#debugging-a-native-crash).

**Tests** — a ctest suite covering parsers, policy and real UI input:

```powershell
ctest --test-dir tools/lvgl_native_preview/build_simulator -C Release --output-on-failure
```

| Test | Covers |
| --- | --- |
| `cyd_runtime_input` | real tap/drag/long-press navigation through actual LVGL callbacks |
| `cyd_ride_replay` | ride-log parsing, CRCs, gaps, seeking, summary totals |
| `cyd_replay_zoom` | replay zoom: buttons, animation, windows built by the reader, page turns |
| `cyd_ride_log_policy` | ride start/pause/stop decisions |
| `cyd_dashboard_redraw`, `cyd_efficiency_redraw`, `cyd_replay_redraw` | partial-redraw correctness |
| `cyd_seg_ring` | segmented ring: block geometry rule, partial repaint against a full one, block colours |
| `cyd_effort_glide` | Motor Effort needle and ring glide: placement, timing, retargeting, repaint per frame |
| `cyd_gauge_ranges` | automatic gauge-range learning |
| `cyd_demo_ride` | demo-mode ride generation |
| `cyd_vehicle_fields` | vehicle configuration fields |
| `cyd_vesc_protocol`, `cyd_vesc_sender`, `cyd_fardriver_sender` | wire protocols and the fake senders |
| `cyd_preferences` | host persistence, including save/reload across runs |

`cyd_runtime_input` drives real LVGL event callbacks rather than jumping to a
screen through preview setters. Preview setters remain only for external
conditions a user cannot produce by touching the screen — an incoming
controller fault, or an SD card being removed.

**Browser** — `browser_simulator_main.cpp` builds the same runtime to WebAssembly
for the companion app's embedded simulator.

## Host substitutions

| Target facility | Native behavior | Accuracy boundary |
| --- | --- | --- |
| TFT_eSPI / ILI9341 | RGB565 memory framebuffer, two 26-line draw buffers, and optional SDL2 presentation | Pixel output and flush geometry are representative; panel inversion, gamma, electrical color response, and calibrated ESP32 CPU cost are not simulated. |
| XPT2046 touch | Mouse or deterministic pointer state in calibrated screen coordinates | Input callbacks are real; raw ADC values, calibration quality, jitter, pressure, and IRQ behavior are not simulated. |
| `millis()` | Controlled host counter | Timer ordering is deterministic; ESP32 scheduling latency and clock drift are not simulated. |
| Preferences/NVS | Versioned binary file or in-memory store | Application save/reload behavior is covered; flash wear, write latency, corruption, partition exhaustion, and NVS internals are not simulated. |
| VESC/FarDriver links | Semantic status and representative telemetry fixtures | UI reactions are covered; UART framing, BLE discovery/radio timing, packet loss, and controller compatibility are not simulated. |
| SD ride logging | Semantic logger/card/catalog fixtures | UI reactions are covered; filesystem, SPI sharing, card latency, removal during writes, and media corruption require hardware tests. |
| Firmware update | Semantic transfer/status fixtures | Screens and state delivery are covered; BLE transport, signature/OTA partition writes, rollback, and reboot validation require target tests. |
| LDR, backlight, RGB LED | Representative sensor values and no-op output functions | UI and settings are covered; ADC response, PWM, LED polarity, and brightness are not simulated. |
| Restart | Firmware restart requests remain a no-op host service; the native menu can launch a clean replacement simulator process | Menu-driven profile/reset/start-screen testing is covered, but target reboot timing and bootloader behavior are not simulated. |

Fakes are deliberately domain-level — "controller connected", "ride save
completed" — rather than emulated NimBLE packets, SD latency or the ESP32 RTOS.

## Performance claims

Keep three levels distinct, in scripts, logs and reports:

1. **Native correctness** — deterministic Windows execution of real LVGL and
   shared sources. This is what the simulator is for.
2. **Native estimate** — a model from rendering workload plus known display
   transfer cost. Useful for relative regressions and early warnings only.
3. **Measured target performance** — timings from a physical CYD. The only
   authoritative source for frame time, CPU cycles, heap and any
   release-gating claim.

Host wall-clock time is not CYD frame time. The runtime reports flushed pixels,
every flush rectangle, flush count, largest flush, host handler/flush time,
theoretical 40 MHz transfer time, modeled DMA wait/final-tail time and a
provisional estimated total. The `uncalibrated-spi-v1` model assumes the
transfer side limits the double-buffered pipeline and adds an explicitly
provisional 0.04 ms setup allowance per flush; it does not estimate ESP32 CPU
rendering or BLE/SD contention.

`Performance > Estimated CYD` paces only the interactive SDL presentation,
using the greater of the provisional frame estimate and LVGL's 30 ms refresh
period. Headless time advancement and framebuffer captures stay deterministic
and never sleep for the estimate. The title-bar overlay reads `UNCALIBRATED`
until a physical-board calibration replaces the provisional coefficients.

Only an explicit physical-board benchmark may be labelled `measured` or used to
claim a firmware change is faster on the CYD.

## Required physical checks

Use [device-smoke-test.md](device-smoke-test.md) for panel detection, display
inversion and gamma, touch calibration and edge accuracy, UART/BLE operation,
SD logging, OTA and rollback, heap stability, and real rendering performance.

## Debugging a native crash

`render_lvgl_native.py` launches one renderer process per screen, so a fault
that needs several screens in one process does not show up there. Rendering
every state in a single process is the way to provoke that class of bug:

```powershell
& tools/lvgl_native_preview/build_simulator/Release/cyd_lvgl_renderer.exe preview_output/scratch
```

A stale `lv_obj_t *` crashes only when the freed block happens to be reused, so
the fault moves between runs and the exit code alone says nothing useful. The
Release tree carries no symbols; configure a throwaway one that does and run it
under the Windows SDK debugger, which names the faulting function directly:

```powershell
cmake -S tools/lvgl_native_preview -B tools/lvgl_native_preview/build_sym -DLVGL_DIR=.pio/libdeps/kajo/lvgl
cmake --build tools/lvgl_native_preview/build_sym --config RelWithDebInfo --target cyd_lvgl_preview
& "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe" -g -G -c "g; k 25; q" tools/lvgl_native_preview/build_sym/RelWithDebInfo/cyd_lvgl_renderer.exe preview_output/scratch
```

`build_*` trees are ignored, so the symbols build can be deleted afterwards.
MSVC's `/fsanitize=address` was tried first and its runtime failed to
initialise for this target; the debugger was both quicker and enough.

Prefer pinning the fix with a runtime-input test over relying on either tool.
A dangling overlay pointer has an observable symptom — the overlay stops
opening — which fails deterministically in `cyd_runtime_input` on any build.

## Rules for simulator work

- Orchestration code — input scripting, fixture selection, time control,
  screenshots, the performance model — stays under
  `tools/lvgl_native_preview/` or test support. Shared production code may
  expose a narrow observable or injectable dependency, but must never contain a
  simulator-only behaviour path for a real user action.
- The PlatformIO `kajo` target must keep building from the same shared sources.
- UI work follows [ui-components.md](ui-components.md), uses the tokens in
  `src/lvgl_app/ui_style.h`, and satisfies the native-preview and translation
  requirements in `AGENTS.md`.
