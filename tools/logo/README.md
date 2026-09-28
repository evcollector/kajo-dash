# KAJO artwork

## Firmware splash

Edit `kajo_splash.svg`, then run `python tools/logo/generate_splash.py`.
PlatformIO also runs this generator before compiling. The generated header uses
solid triangle geometry and the firmware draws its version caption separately.
For a native-size preview, add `--preview preview_output/splash/kajo_splash.png`.

## Application icons

Run `python tools/logo/generate_app_icons.py` with Pillow installed to regenerate
the shared ring-and-bolt icon. The generator owns the smooth vector geometry and
produces the SVG, 512px PNG, seven-size Windows ICO, Android vector drawables,
adaptive launcher resources, and Android 13 themed-icon resource. The Android
files are written only when the private `android-companion/` checkout is
present. Edit its
`PATHS`/ring parameters and `BOLT` coordinates rather than generated assets.

The icon uses flat orange and white on black, without glow. Android foregrounds
are scaled into the adaptive safe zone so circular and rounded-square launcher
masks retain the mark. Windows artwork has transparent rounded corners.

Consumers:

- Companion app (private repository): `@mipmap/ic_launcher` (normal and round).
- Windows simulator: embedded RC icon and explicit large/small window icons.
- Layout editor: `BrowserWrapper/AppIcon.ico` for the desktop application and
  `tools/logo/kajo_app_icon.svg` / `.ico` for its browser favicon.

Rebuild the relevant application after regenerating. Existing shortcuts and
launchers may cache the old icon until restarted or refreshed.
