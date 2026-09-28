#!/usr/bin/env python3
"""Convert the hand-drawn icon set into crisp two-state RGBA glyphs.

Source icons (preview_output/new_icons/LatestIcons_25.6/cyd_icon_*.png) are
16x16 RGB where each pixel's brightness is coverage. The dashboard renderers
recolor icons through their alpha channel, so the normalized assets contain
only transparent and fully opaque white pixels. This deliberately removes the
old grey antialias ramp and keeps browser previews identical to firmware.

    py -3 tools/convert_new_icons.py
"""
from __future__ import annotations

from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "preview_output" / "new_icons" / "LatestIcons_25.6"
OUT = ROOT / "preview_output" / "icons16"
MONO_THRESHOLD = 64


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    count = 0
    for f in sorted(SRC.glob("cyd_icon_*.png")):
        name = f.stem.replace("cyd_icon_", "").rstrip("_")  # "odo_" -> "odo"
        brightness = Image.open(f).convert("L")
        if brightness.size != (16, 16):
            raise ValueError(f"{f} must be 16x16, got {brightness.size}")
        binary = brightness.point(lambda value: 255 if value >= MONO_THRESHOLD else 0)
        glyph = Image.new("RGBA", (16, 16), (255, 255, 255, 0))
        glyph.putalpha(binary)
        glyph.save(OUT / f"{name}.png", optimize=True)
        count += 1
    print(f"Wrote {count} RGBA icons to {OUT}")


if __name__ == "__main__":
    main()
