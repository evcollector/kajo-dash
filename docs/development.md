# Development

Building, previewing, testing and releasing the firmware. For what the project
is and how to install it, start with the [README](../README.md).

## Requirements

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html)
  or the PlatformIO IDE extension for VS Code. Check that `pio --version` works
  in the terminal you will use.
- Python 3.11 or newer for the helper and release tools.
- For the preview renderer and simulator: CMake, Visual Studio C++ Build Tools
  and Pillow (`pip install -r tools/requirements.txt`).
- For the demo video, also [ffmpeg](https://ffmpeg.org/) in `PATH`.

The firmware dependencies are downloaded automatically at their exact versions
from `platformio.ini`.

## The `kajo.bat` menu

On Windows, every workflow below has an entry in `kajo.bat` at the project root:
double-click it, move with the arrow keys or W/S and press Enter, or press an
entry's number. Right or D moves to an entry's `[?]`, and Enter there opens a
page explaining what it does and needs; those pages are in
`scripts\menu_help.txt`. The menu itself is `scripts\menu.ps1`; without
PowerShell it falls back to a plain numbered prompt. The individual scripts live in `scripts/`
and can still be run directly; this page names them that way.

| # | Entry | Script |
| --- | --- | --- |
| 1 | Simulator | `scripts\run_simulator_lvgl.bat` |
| 2 | Preview renders | `scripts\run_preview_lvgl.bat` |
| 3 | Layout editor | `scripts\run_layout_editor_lvgl.bat` |
| 4 | Flash over USB | `scripts\upload_firmware_usb.bat` |
| 5 | Package firmware: test package or release | `scripts\make_release.bat [--test \| --release]` |
| 6 | Install package, over a USB cable or Bluetooth | `kajo.bat --usb [release.json] [--port COMx] [--erase-all]`, `kajo.bat --ble [release.json] [uploader options]` |
| 7 | Fake VESC | `scripts\upload_vesc_test.bat` |
| 8 | Fake FarDriver | `scripts\upload_fardriver_test.bat` |
| 9 | Install the companion app (private checkout only) | `android-companion\kajo-app.bat` |

On other platforms, use the `pio` commands directly.

## Building and flashing

```bash
pio run
```

`kajo` is the default environment, so a bare `pio run` builds the dashboard
firmware. `fardriver_test` and `vesc_test` build the two fake controller
senders instead; see [test-senders.md](test-senders.md).

To build and flash over USB:

```bash
pio run -e kajo -t upload
```

or on Windows `scripts\upload_firmware_usb.bat [COMx]`, which also picks the
USB serial port for you. If the upload reports `Wrong boot mode detected`, hold
BOOT, tap RST/EN, and keep holding BOOT until `Connecting...` changes.

Before treating a build as a hardware checkpoint, run the matrix in
[device-smoke-test.md](device-smoke-test.md) on both CYD panel variants.

## Preview renderer

Generate preview images without flashing:

```bat
scripts\run_preview_lvgl.bat
```

This builds a headless renderer from the same LVGL library, fonts,
`dashboards.cpp`, `screens.cpp` and `ui_common.cpp` the firmware uses, and
captures LVGL's 320x240 RGB565 framebuffer — it does not approximate the UI with
desktop drawing calls. It regenerates `include/cyd_layout_generated.h`,
`tools/lvgl_font_metrics.json` and `preview_output/lvgl/`.

The first run needs an installed `.pio/libdeps/kajo/lvgl` (run `pio run` once).
Later runs are incremental.

To check translated captions for overflow, render in another language.
Non-English runs write to `preview_output/lvgl_<lang>/` and leave the
checked-in English set alone:

```bat
python tools\render_lvgl_native.py --lang=de
```

## Simulator

Run the same UI as an interactive virtual CYD with mouse input and a persistent
desktop settings profile:

```powershell
.\scripts\run_simulator_lvgl.bat
```

The first build downloads the pinned SDL2 source release. The window uses a
320x240 logical framebuffer with integer scaling; resizing does not change LVGL
or screenshot coordinates. A native menu bar offers screenshots, state profiles,
1x-8x scaling and performance modes.

Estimated-CYD mode models the target's two 26-line draw buffers, one active DMA
transfer, 40 MHz RGB565 transfer time, per-flush setup, final DMA tail, and the
30 ms LVGL refresh cadence. It is labelled `UNCALIBRATED` until physical
measurements provide CPU and contention coefficients, and it changes only
presentation timing — never deterministic screenshots or firmware behaviour.

For repeatable and automated runs:

```powershell
.\scripts\run_simulator_lvgl.bat --headless --duration-ms=1000 --start=first-boot
.\scripts\run_simulator_lvgl.bat --performance=estimated-cyd --perf-overlay
```

`--scale=1` through `--scale=8`, `--start=auto|dashboard|first-boot`,
`--state-file=<path>`, `--performance=host|estimated-cyd`, `--perf-overlay` and
`--fixed-timestep` are available. Screenshots are raw 320x240 PPM. Known
boundaries are in [simulator.md](simulator.md).

## Demo video

The demo video is rendered from the simulator, one chapter per scene script in `tools/demo/scenes`:

```powershell
python tools\make_demo_video.py themes --gif
```

It needs `ffmpeg` in `PATH` as well as the preview requirements, writes to `dist/demo/`, and is
described, with the scene language, in [demo-video.md](demo-video.md).

## Layout editor

```bat
scripts\run_layout_editor_lvgl.bat
```

A visual editor for `tools/layout.json`, with a built-in 16x16 icon editor that
writes both the source PNG and the packed firmware icon data. The generated
layout header must not be edited by hand — change `tools/layout.json` through
the editor, then regenerate.

Before editing any screen, read [ui-components.md](ui-components.md).

## Fonts

```bat
python tools\generate_lvgl_fonts.py
```

Uses the pinned `lv_font_conv@1.5.3` through `npx`, keeps the large
dashboard-number fonts compact, and includes the localised European glyph set
only in the 12-24 px UI fonts.

## Boot splash

The black KAJO splash is authored in `tools/logo/kajo_splash.svg`. Its logo,
mountains, trees and road use solid vector polygons, without glow or textures.
`tools/logo/generate_splash.py` triangulates the SVG into
`include/kajo_splash_generated.h`, which the boot renderer draws directly with
TFT_eSPI before LVGL starts. PlatformIO regenerates it automatically on build.
The bottom caption stays real text and reads `CYD_FIRMWARE_VERSION_NAME`.

To regenerate the geometry and a 320x240 preview with the existing TFT font:

```powershell
python tools/logo/generate_splash.py --preview preview_output/splash/kajo_splash.png
```

The preview needs Pillow and the installed TFT_eSPI dependency; generating the
header alone uses only the Python standard library. The SVG intentionally uses
explicit integer polygons; unsupported paths, transforms or fills are rejected.
Edit the SVG, never the generated header. Previews draw the same triangles with
Pillow, so edge rasterization can differ slightly from the physical TFT renderer.

### Boot appearance

The panel initially shows white before firmware can draw. Panel bring-up keeps
that white stable, then lowers the backlight over 150 ms, draws the black splash
while dark, and raises it over 250 ms. Remaining initialization runs with the
splash visible. After at least 700 ms of visible artwork, the firmware lowers
the backlight over 140 ms, paints the first dashboard or setup frame, and raises
it over 180 ms to the configured brightness. These are fades through darkness,
not full-frame blended animation. The splash stays black even with a light home
theme. Calibration and recovery bypass the artwork hold if they take the screen.
Physical-panel testing is required to judge the timing and display transitions.

## Tests

Host tests build with the simulator and run under CTest. The Python suite
checks the update protocol, release packaging and test-sender uploader:

```powershell
python -m pip install pytest==8.3.4 -r tools\firmware_update\requirements.txt
ctest --test-dir tools\lvgl_native_preview\build_simulator -C Release
python -m pytest -q
```

## Firmware updates

The 4 MB flash uses two equal `ota_0`/`ota_1` application slots of `0x1f0000`
bytes (1,984 KiB) each, plus a 64 KiB coredump partition. Ride data lives on the
SD card and settings in NVS, so the unused SPIFFS partition was removed.
Changing the partition table requires one wired upload; an application-only OTA
image will not migrate an already deployed device's partition map.

The inactive-slot writer, signed-manifest verifier, BLE transport and delayed
rollback validation are in `src/lvgl_app/firmware_update*`. The checked-in
release public key is a real trust anchor: the display installs only images
signed by the matching private key, which is held offline and outside this
repository. `tools/firmware_update/create_release_key.py` refuses to write a
private key anywhere inside the repository.

Key generation, release signing and upload instructions are in
[`tools/firmware_update/README.md`](../tools/firmware_update/README.md); the
wire protocol is in [ota-update-design.md](ota-update-design.md).

Because the display only installs updates signed by a key it trusts, a build
from your own checkout has to be flashed over USB — or you can install your own
signing key and use the Bluetooth uploader for your own builds. See
"Contributor: build and install your own firmware" in
`tools/firmware_update/README.md`.

### Installing a signed release from a checkout

Choose **6. Install package** in `kajo.bat` and pick **Over Bluetooth**, or run
`kajo.bat --ble`. On the display, first open **Settings > Information >
Bluetooth Link**. The
launcher selects a protocol 3 manifest matching the version in
`include/config.h`, checking the root, then `releases/`, then the test package
in `releases/test/` (newest first in each directory). It uses the standalone
uploader EXE when available, otherwise prepares its Python environment and
starts the upload. After the uploader finishes, press a key to return to the
menu.

**Over a USB cable**, or `kajo.bat --usb`, writes the same signed package with esptool,
without PlatformIO, as the release ZIP's **1. Install over USB cable** does. It
only considers packages with a `-usb.json` layout beside them, which the
release builder writes. The display's settings are kept unless `--erase-all` is
passed.

### Test packages and releases

`include/config.h` holds the version being worked towards: the next release,
not the last one. Until the first release that is `0.01`, and everything built
in the meantime is `0.01`. To choose a different next version, edit its two
defines by hand. [RELEASES.md](../RELEASES.md) lists what has been released;
the `kajo.bat` header shows both.

Menu entry 5 in `kajo.bat`, or `scripts\make_release.bat` directly, packages
that version in one of two ways:

- **T, test package** (`--test`): the firmware built in the `kajo_test_package`
  environment, so the display shows `0.01-test` on the boot splash and in
  Settings, signed and packaged into `releases\test\` for your own boards. It
  replaces the previous test package, commits nothing, tags nothing and leaves
  the version alone, so make as many as you like. Install one with option 6,
  `kajo.bat --usb`, or the launcher inside its ZIP. It carries the same version
  code as the release it leads up to, so the release later installs over it
  without a downgrade prompt.
- **R, release** (`--release`): the firmware as it will be published, signed
  and packaged into `releases\` and `dist\`: the Windows updater ZIP, the
  `.kajofw` bundle for the companion app, and `kajo-update.json`, the
  fixed-name index the app reads from the latest GitHub release to find new
  firmware. It adds a row to `RELEASES.md`, commits it as "Release KAJO-Dash
  0.01" and tags that commit `v0.01`; then it moves `include/config.h` on to
  `0.02` in a commit "Start KAJO-Dash 0.02", so test packages from then on
  carry the version they lead up to. A release needs a clean tree, so the tag
  matches what was built, and a version above the last one in `RELEASES.md`.
  Nothing is pushed: the builder prints what to push and what to attach to the
  GitHub release.

Either way it creates `.venv` on first use, then checks everything before
changing anything: the signing key, PlatformIO, the Python tests, whether
`firmware_changelog.h` matches `CHANGELOG.md`, whether the uploader EXE is
current, and for a release the tree, the version and the files it would write.
It prints that as one plan. A release asks once before going ahead; a test
package only asks when the plan has warnings.

It then asks for the signing-key password and checks that it opens the key, and
that the key matches the public key compiled into the firmware, before the
build starts. It rebuilds the uploader EXE if needed, then builds, signs and
packages in `.pio\release-staging\`; only when every step has succeeded are the
files moved into place. A failed step puts back anything it edited, so the next
run starts clean.

The version name follows the code, 1 is `0.01`, 2 is `0.02` and 100 is `1.00`,
when the release builder moves the version on; a name set by hand is kept until
then. The companion app reads its own version from `include/config.h` too, so a
build of it pairs with the firmware in the same checkout. When the app's private
checkout is present, the move to the next version also resets its revision to
0, and leaves that change for the app's own repository to commit.

For an unattended run, `scripts\make_release.bat --test --yes` (or `--release
--yes`) goes ahead without asking when the plan has no warnings, and takes the
key password from `CYD_RELEASE_KEY_PASSWORD` if it is set. `--key` chooses
another signing key.

## Project layout

| Path | What |
| --- | --- |
| `src/main_lvgl.cpp` | Entry point |
| `src/lvgl_app/` | All UI, controller and update code |
| `src/vesc_test/`, `src/fardriver_test/` | Fake controller senders |
| `include/config.h` | Pin and default configuration |
| `include/lv_conf.h` | LVGL configuration |
| `tools/` | Preview renderer, simulator, layout editor, asset and font pipelines |
| `scripts/` | Windows launchers behind `kajo.bat` |
| `docs/` | Contracts, feature and procedure docs; see [docs/README.md](README.md) |

Why things are the way they are — invariants that are easy to break again, and
decisions where the obvious alternative was rejected — is in
[design-decisions.md](design-decisions.md).
