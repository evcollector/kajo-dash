"""Make the digit glyphs of an lv_font_conv-generated C font tabular.

Sets every digit's advance width to the widest digit's and re-centers each
glyph in its new slot (ofs_x), so changing digits repaint strictly in place
instead of jittering. Run after regenerating any font:

  python tools/make_tabular_digits.py <font.c> <first_digit_glyph_id>

The glyph id of '0': for the digits-only speed fonts (symbols "-.0-9") it is
3; for the full-ASCII UI fonts (range 0x20-0x7E) it is 17 (0x30-0x20+1).
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

GLYPH_RE = re.compile(
    r"\{\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), "
    r"\.ofs_x = (-?\d+), \.ofs_y = (-?\d+)\}"
)


def main() -> None:
    path = Path(sys.argv[1])
    first_digit_id = int(sys.argv[2])
    text = path.read_text(encoding="utf-8")

    matches = list(GLYPH_RE.finditer(text))
    digits = matches[first_digit_id : first_digit_id + 10]
    if len(digits) != 10:
        raise SystemExit(f"{path}: expected 10 digit glyphs at id {first_digit_id}")

    max_adv = max(int(m.group(2)) for m in digits)

    out = []
    last = 0
    for m in matches:
        out.append(text[last : m.start()])
        idx = matches.index(m)
        if first_digit_id <= idx < first_digit_id + 10:
            adv = int(m.group(2))
            ofs_x = int(m.group(5)) + round((max_adv - adv) / 2 / 16)
            out.append(
                "{.bitmap_index = %s, .adv_w = %d, .box_w = %s, .box_h = %s, .ofs_x = %d, .ofs_y = %s}"
                % (m.group(1), max_adv, m.group(3), m.group(4), ofs_x, m.group(6))
            )
        else:
            out.append(m.group(0))
        last = m.end()
    out.append(text[last:])

    path.write_text("".join(out), encoding="utf-8")
    print(f"{path.name}: digits -> adv_w {max_adv} (tabular)")


if __name__ == "__main__":
    main()
