# KAJO artwork

## Firmware splash

Edit `kajo_splash.svg`, then run `python tools/logo/generate_splash.py`.
PlatformIO also runs this generator before compiling. The generated header uses
solid triangle geometry and the firmware draws its version caption separately.
For a native-size preview, add `--preview preview_output/splash/kajo_splash.png`.

## Brand kit

`python tools/logo/generate_brand.py` redraws the splash design language into
`tools/logo/brand/`: the ring-and-bolt mark, the KAJO
wordmark, horizontal and stacked lockups (each for dark and light backgrounds
plus single-colour variants), a rounded-square icon, and the mountain-road hero
scene at 4:3 and 16:9, plus a 3:1 GitHub README banner and a 2:1 repository
social preview. The YouTube channel banner is 2560x1440. YouTube shows only
the central 1546x423 area on phones, so the scene is drawn at one uniform 2x
scale and placed so that everything from the ring glow down to the end of the
road edges fits that area; the near dashes continue below it for the TV view.
Two versions are provided:
`brand/png/kajo-banner-youtube.png` fades the mountain-road scene into black
on both sides; `brand/png/kajo-banner-youtube-hard-edge.png` lets the ridges,
trees end as crisp shapes against black, while the two road borders continue
to the bottom edge of the canvas. Their corresponding SVGs
have the same names in `brand/`. Edit the generator and rerun it to change
either layout.
`brand/png/kajo-banner-youtube-road-fade.png` follows the wider reference
composition with clean mountain and tree ends; only the outer tips of the
orange road borders fade into black. Its SVG is in `brand/`.
`brand/png/kajo-banner-github.png` uses the same road-fade composition at 3:1;
its editable source is `brand/kajo-banner-github.svg`.
Logos are flat; only the scenes carry the soft ring and road
glow of the original splash illustration, and in the scenes the mountain range
overlaps the foot of the wordmark as it does there. The central mountains and
fir trees come directly from the hand-traced polygons in `kajo_splash.svg`;
wide scenes extend those contours with additional peaks and spaced firs. Edit
the central landscape in `kajo_splash.svg` and the outer range in
`generate_brand.py`, then regenerate both the firmware splash and brand kit.
The `brand/png/` scene previews have solid black backgrounds and are rendered
from the SVGs with Edge headless.

Palette: orange `#ff7900`, white, ink `#0b0c0d` on light backgrounds, and in
the scenes a darker outer-ring orange `#d86200` (the firmware splash uses
`#b95500`) and rock greys `#171a1d`-`#606268` taken from `kajo_splash.svg`.

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
