# Third-party notices

This project is licensed under the GNU General Public License, version 3 or
later (see `LICENSE`). It uses the third-party components listed below, each
under its own licence. Every one of them is GPL-3.0 compatible; where that is
not obvious, the reason is given below the relevant table.

The list is split by which binary a component ends up in, because that is what
decides the obligation. Anything in the first table is redistributed whenever a
`.bin` is published — through a GitHub release, over the Bluetooth updater, or
on a pre-flashed device — and its licence and notices must travel with it.
The second table covers build and desktop tools that are not part of the
firmware. The Windows uploader has its own runtime table below. The final table
is what ships inside the Android companion APK, which is published separately
and carries its own notices in-app.

Licence texts for components redistributed in the firmware, Windows uploader,
and companion APK are in `licenses/`.
The `.kajofw` and Windows updater ZIP include this file, the project `LICENSE`
and `NOTICE`, and the `licenses/` texts alongside the signed firmware.

## Compiled into the firmware binary

| Component | Version | Licence | Text |
| --- | --- | --- | --- |
| Arduino core for ESP32 | 2.0.17 (`framework-arduinoespressif32` 3.20017.241212) | LGPL-2.1-or-later | `licenses/LGPL-2.1.txt`, `licenses/LGPL-3.0.txt` |
| LVGL | 8.4.0 | MIT | `licenses/MIT-LVGL.txt` |
| TFT_eSPI | 2.5.43 | BSD-2-Clause, over MIT | `licenses/BSD-2-Clause-TFT_eSPI.txt` |
| NimBLE-Arduino | 2.5.1 | Apache-2.0 | `licenses/Apache-2.0-NimBLE-Arduino.txt` |
| mbedTLS (`ecdsa`, `sha256`) | via ESP-IDF | Apache-2.0 | `licenses/Apache-2.0.txt` |
| miniz (`esp32/rom/miniz.h`) | via ESP-IDF ROM | MIT | — |
| ESP-IDF second-stage bootloader | via the Arduino core | Apache-2.0 | `licenses/Apache-2.0.txt` |
| Rajdhani (SemiBold, Bold) | Indian Type Foundry | SIL OFL 1.1 | `licenses/OFL-1.1-Rajdhani.txt` |

The bootloader is a separate image rather than part of the `.bin`, but the
Windows updater ZIP ships it, with the partition table and the Arduino core's
`boot_app0.bin` (an all-`0xFF` OTA-data reset), so the release can be installed
over USB on a blank board. It is covered by the same notices.

### Arduino core for ESP32

The Arduino ESP32 core is LGPL-2.1-or-later and is statically linked into the
firmware. The "or later" is what makes the combination work: it permits the core
to be taken under LGPL-3.0, which is expressly compatible with GPL-3.0.

Publishing this project's complete source under GPL-3.0-or-later, together with
the build configuration in `platformio.ini`, is what allows a recipient to
rebuild or relink the firmware, which is the condition LGPL section 6 exists to
guarantee.

Apache-2.0 components are compatible with GPL-3.0 but not with GPL-2.0, which is
why this project is GPL-3.0-or-later and cannot be offered under GPL-2.0.

### Fonts

`src/lvgl_app/lv_font_rajdhani_*.c` and `src/lvgl_app/lv_font_speed*.c` are
generated from the Rajdhani TTFs in `tools/fonts/rajdhani/` by
`tools/generate_lvgl_fonts.py`. They embed glyph outlines, so the OFL applies to
the generated `.c` files and to any firmware binary built from them, not only to
the TTFs. `tools/fonts/rajdhani/OFL.txt` sits beside the fonts as the OFL
requires.

Rajdhani is the only typeface in the product. `include/lv_conf.h` disables every
`LV_FONT_MONTSERRAT_*` face, declares the generated Rajdhani faces, and selects
Rajdhani as the default. Screen code now refers to those Rajdhani faces
directly. LVGL's own Montserrat faces — which embed Montserrat and Font Awesome
5 glyph outlines — are therefore never compiled, and neither typeface appears
in the firmware or in the desktop simulator. Both builds share this `lv_conf.h`.

No `LV_SYMBOL_*` constant is used anywhere in the project either.

The OFL and this project's GPL-3.0 coexist without either giving way. OFL-FAQ
1.2 and 1.3 are explicit that only the font portions stay under the OFL and that
aggregating or bundling them with software under another licence — copyleft or
proprietary — is what the licence is for. Three conditions carry over into every
binary built here: the OFL text travels with the distribution, the reserved font
name is respected so no modified face is called Rajdhani, and the fonts are
never sold on their own.

## Build and desktop tooling

Not part of the firmware. Listed for completeness and reproducibility.

| Component | Version | Licence | Used by |
| --- | --- | --- | --- |
| SDL2 | 2.32.10 | zlib | native simulator (`CYD_BUILD_SIMULATOR`) |
| lv_font_conv | 1.5.3 | MIT | `tools/generate_lvgl_fonts.py`, via npx |
| Pillow | >= 10 | MIT-CMU | icon, background and thumbnail converters |
| .NET 8 | 8.0 | MIT | layout editor wrapper |
| Microsoft.Web.WebView2 | 1.0.4022.49 | Microsoft proprietary (redistributable) | layout editor wrapper |
| cryptography | >= 42, < 47 | Apache-2.0 OR BSD-3-Clause | firmware signing |

SDL2 is fetched by CMake at configure time and static-links into
`cyd_simulator`; it is not vendored. WebView2 is a NuGet `PackageReference`
restored at build time under Microsoft's own terms and is likewise not vendored,
which is the only reason a non-open-source licence appears in this list. Do not
commit the WebView2 runtime into this repository.

## Redistributed in the Windows firmware uploader

The PyInstaller executable in the Windows release ZIP bundles the uploader's
Python runtime, its BLE dependencies, and esptool for the USB install. The
package includes these licence texts alongside the executable. Recheck the
inventory when rebuilding the executable with different dependency versions:
PyInstaller's `Analysis-00.toc` in `.pio/pyinstaller-firmware-uploader/` lists
what was bundled.

| Component | Version used for this inventory | Licence | Text |
| --- | --- | --- | --- |
| Python runtime | 3.11 | PSF-2.0 and bundled notices | `licenses/PSF-Python-3.11.txt` |
| bleak | 1.1.1 | MIT | `licenses/MIT-Bleak.txt` |
| PyWinRT runtime and projections | 3.2.1 | MIT | `licenses/MIT-PyWinRT.txt` |
| typing_extensions | 4.16.0 | PSF-2.0 | `licenses/PSF-Typing-Extensions.txt` |
| esptool | 4.12.0 | GPL-2.0-or-later | `licenses/GPL-2.0-esptool.txt` |
| esptool legacy flasher stub (`stub_flasher/1`) | 1.11.1 | GPL-2.0-or-later | `licenses/GPL-2.0-esptool.txt` |
| esp-flasher-stub (`stub_flasher/2`) | 0.7.0 | Apache-2.0 OR MIT | `licenses/MIT-esp-flasher-stub.txt` |
| pySerial | 3.5 | BSD-3-Clause | `licenses/BSD-3-Clause-pySerial.txt` |
| IntelHex | 2.3.0 | BSD-3-Clause | `licenses/BSD-3-Clause-IntelHex.txt` |
| PyInstaller bootloader and loader | 6.22.2 | GPL-2.0-or-later with bootloader exception | `licenses/PyInstaller-COPYING.txt` |
| PyInstaller runtime hooks | 6.22.2 | Apache-2.0 | `licenses/Apache-2.0.txt` |

esptool and its legacy flasher stub are GPL-2.0-or-later, which this project
takes under GPL-3.0, the same licence as the rest of the uploader. Their
corresponding source is Espressif's published release, at
<https://github.com/espressif/esptool/tree/v4.12.0> and
<https://github.com/espressif/esptool-legacy-flasher-stub/releases/tag/v1.11.1>;
the uploader's own source, including how it calls esptool, is in
`tools/firmware_update/`. esptool pulls in further packages for commands the
uploader never runs (espsecure, espefuse); PyInstaller leaves them out because
nothing imports them, so they are not listed.

## Redistributed in the Android companion APK

The companion app is published as a closed-source binary, and its source is
kept in a separate private repository, so unlike the firmware it cannot rely on
its own source being available to satisfy anything. MIT, BSD-3-Clause and the
OFL each require their notice to travel with the binary, which is why the app
carries an in-app licence screen with the full texts. This table is the public
record of what that screen has to list.

| Component | Version | Licence | Text |
| --- | --- | --- | --- |
| Jetpack Compose and AndroidX | BOM 2025.02.00 | Apache-2.0 | `licenses/Apache-2.0.txt` |
| Kotlin stdlib and coroutines | via Kotlin plugin | Apache-2.0 | `licenses/Apache-2.0.txt` |
| Android BLE Library | 2.11.0 | BSD-3-Clause | `licenses/BSD-3-Clause-Android-BLE-Library.txt` |
| LVGL | 8.4.0 | MIT | `licenses/MIT-LVGL.txt` |
| Rajdhani (SemiBold, Bold) | Indian Type Foundry | SIL OFL 1.1 | `licenses/OFL-1.1-Rajdhani.txt` |
| Emscripten runtime | via emsdk | MIT | `licenses/MIT-Emscripten.txt` |

LVGL, Rajdhani and the Emscripten runtime are in the APK because
`app/src/main/assets/simulator/index.html` is a WebAssembly build of the
firmware's own LVGL screens, produced by the `cyd_web_simulator` target in
`tools/lvgl_native_preview/CMakeLists.txt`. That target links LVGL and the
firmware sources; it does not link SDL2, which is native-simulator only.

The firmware sources compiled into that bundle are the project's own, so their
licence is the project's to set. `CLA.md` covers project-owned, hand-written
firmware source by directory rather than maintaining a second copy of this
CMake source list. Generated files and third-party material are explicitly
excluded from that additional grant and remain governed by their own licences.

## Project-owned assets

These are original work under this project's GPL-3.0-or-later licence, recorded
here so their provenance is not ambiguous later:

- the 21 icons in `preview_output/new_icons/LatestIcons_25.6/`, drawn with the
  icon editor built into `tools/layout_editor.html` and packed by
  `tools/convert_icons_1bit.py`;
- `src/lvgl_app/pixel_font.c`, a hand-authored 5x7 bitmap face;
- `src/lvgl_app/vesc_protocol.*`, an independent implementation of the VESC
  UART wire format written from the field layout;
- all dashboard, layout and UI code under `src/`.

## Removed dependencies

The SolidGeek VescUart library (GPLv3) was used until it was replaced by
`src/lvgl_app/vesc_protocol.*`. No code from it remains, and it is no longer a
build dependency. It is noted here because it appears in this repository's git
history.
