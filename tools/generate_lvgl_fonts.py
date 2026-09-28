"""Rebuild the Rajdhani LVGL fonts from the bundled TTF sources.

Uses a pinned lv_font_conv release through npx. UI fonts from 10 through
24 px include the curated characters needed by the supported European
languages. The 36 px fallback and 48/72/96 px speed fonts remain ASCII/digits
only because they render dashboard numbers rather than localized UI text.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FONT_DIR = Path("tools/fonts/rajdhani")
OUT_DIR = Path("src/lvgl_app")
CONVERTER = "lv_font_conv@1.5.3"

# Union required by Finnish, German, French, Spanish, and Italian. Keep this
# explicit instead of importing all Latin-1 so flash growth stays predictable.
LOCALIZED_SYMBOLS = (
    "°ÄÅÖäåöÜüß"
    "ÀÂÇÉÈÊËÎÏÔÙÛàâçéèêëîïôùûÿŒœ"
    "ÁÍÑÓÚáíñóú¿¡ÌÒìò"
)


def converter_command() -> list[str]:
    npx = shutil.which("npx") or shutil.which("npx.cmd")
    if not npx:
        raise SystemExit("npx was not found; install Node.js before regenerating fonts")
    return [npx, "--yes", CONVERTER]


def generate(font: Path, size: int, output: Path, *, char_range: str | None = None,
             symbols: str | None = None) -> None:
    command = converter_command() + [
        "--font", str(font),
        "--size", str(size),
        "--bpp", "4",
        "--format", "lvgl",
    ]
    if char_range:
        command += ["--range", char_range]
    if symbols:
        command += ["--symbols", symbols]
    command += ["--no-compress", "-o", str(output)]
    subprocess.run(command, cwd=ROOT, check=True)


def make_digits_tabular(output: Path, first_digit_glyph_id: int) -> None:
    subprocess.run(
        [sys.executable, "tools/make_tabular_digits.py", str(output), str(first_digit_glyph_id)],
        cwd=ROOT,
        check=True,
    )


def normalize_lvgl_include(output: Path) -> None:
    path = ROOT / output
    text = path.read_text(encoding="utf-8")
    generated = (
        '#ifdef LV_LVGL_H_INCLUDE_SIMPLE\n'
        '#include "lvgl.h"\n'
        '#else\n'
        '#include "lvgl/lvgl.h"\n'
        '#endif\n'
    )
    if generated not in text:
        raise RuntimeError(f"Unexpected lv_font_conv include block in {output}")
    path.write_text(text.replace(generated, '#include "lvgl.h"\n', 1), encoding="utf-8", newline="\n")


def main() -> None:
    semibold = FONT_DIR / "Rajdhani-SemiBold.ttf"
    bold = FONT_DIR / "Rajdhani-Bold.ttf"

    for size in (10, 12, 14, 16, 20, 24):
        output = OUT_DIR / f"lv_font_rajdhani_{size}.c"
        generate(semibold, size, output, char_range="0x20-0x7E", symbols=LOCALIZED_SYMBOLS)
        normalize_lvgl_include(output)
        make_digits_tabular(output, 17)

    output = OUT_DIR / "lv_font_rajdhani_36.c"
    generate(semibold, 36, output, char_range="0x20-0x7E")
    normalize_lvgl_include(output)
    make_digits_tabular(output, 17)

    for size in (48, 72, 96):
        output = OUT_DIR / f"lv_font_speed{size}.c"
        generate(bold, size, output, symbols="0123456789.-")
        normalize_lvgl_include(output)
        make_digits_tabular(output, 3)

    subprocess.run([sys.executable, "tools/generate_lvgl_font_metrics.py"], cwd=ROOT, check=True)
    print("Rebuilt all Rajdhani LVGL fonts with", CONVERTER)


if __name__ == "__main__":
    main()
