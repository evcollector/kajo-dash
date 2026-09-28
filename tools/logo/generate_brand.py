"""Generate the KAJO brand kit as clean, resolution-independent SVG.

The app icon generator owns the launcher crop. This script redraws the splash
design language for print, web and documents: the ring and bolt mark, the KAJO
wordmark and lockups use exact geometry. The mountain-road scenes reuse the
hand-traced central landscape from kajo_splash.svg (snapped to the 320x240 TFT
pixel grid) and extend it with generated outer ranges on wide canvases.

Edit the geometry constants below, then run `python tools/logo/generate_brand.py`.
Output goes to tools/logo/brand/. No third-party packages are required.
"""
from __future__ import annotations
import math
import random
import xml.etree.ElementTree as ET
from pathlib import Path

OUT = Path(__file__).resolve().parent / "brand"
SPLASH_SOURCE = Path(__file__).resolve().parent / "kajo_splash.svg"

# ── Palette ──────────────────────────────────────────────────────────────────
ORANGE = "#ff7900"       # primary accent: power ring, A accent, road
WHITE = "#ffffff"
INK = "#0b0c0d"          # letterforms on light backgrounds
BLACK = "#000000"


def f(v: float) -> str:
    """Compact number formatting for path data."""
    s = f"{v:.3f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def poly(points) -> str:
    return "M" + " L".join(f"{f(x)},{f(y)}" for x, y in points) + " Z"


# ── Mark: open power ring with lightning bolt ───────────────────────────────
# Ring centre is (0, 0). The ring opens 90 degrees at the bottom (135..405 deg,
# y-down), like a gauge. Proportions follow the app icon.
THIN_RING = (43.0, 41.5)
POWER_RING = (36.0, 28.0)
RING_START, RING_END = 135.0, 405.0
# Bolt traced from the original splash illustration: the zig-zag break sits
# just below the ring centre and the tip drops into the ring's opening. Edges
# are made exactly parallel in pairs (upper-left // lower-right, notch //
# notch) so the zig-zag reads as one continuous slanted stroke.
BOLT = [(5.3, -22.4), (-15.93, 7.5), (-4.4, 7.5), (-8.8, 30.3), (14.9, -3.1), (1.57, -3.1)]


def ring_path(outer: float, inner: float, cx=0.0, cy=0.0) -> str:
    def pt(r, a):
        return cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a))
    a, b = pt(outer, RING_START), pt(outer, RING_END)
    c, d = pt(inner, RING_END), pt(inner, RING_START)
    return (f"M{f(a[0])},{f(a[1])} A{f(outer)},{f(outer)} 0 1 1 {f(b[0])},{f(b[1])} "
            f"L{f(c[0])},{f(c[1])} A{f(inner)},{f(inner)} 0 1 0 {f(d[0])},{f(d[1])} Z")


def mark_bounds():
    r = THIN_RING[0]
    ys = [y for _, y in BOLT] + [-r, r * math.sin(math.radians(RING_START))]
    return -r, min(ys), r, max(ys)


def mark(ring=ORANGE, bolt=WHITE, thin=None) -> str:
    return (f'<path fill="{thin or ring}" d="{ring_path(*THIN_RING)}"/>'
            f'<path fill="{ring}" d="{ring_path(*POWER_RING)}"/>'
            f'<path fill="{bolt}" d="{poly(BOLT)}"/>')


# ── Wordmark ─────────────────────────────────────────────────────────────────
# Heavy extended caps. Cap height 38, vertical stems 16, horizontal bars 11.
CAP = 38.0
STEM = 16.0
BAR = 11.0
TRACK = 5.0


def letter_k(x0):
    # Arms are parallel strokes 18 wide measured along the cap/base lines.
    slope = 19.0 / 24.0                      # dy/dx of the arm edges
    join = STEM + 0.0
    y_top = (37.0 - join) * slope           # where the upper arm meets the stem
    pts = [(0, 0), (STEM, 0), (STEM, y_top), (37, 0), (55, 0), (31, CAP / 2),
           (55, CAP), (37, CAP), (STEM, CAP - y_top), (STEM, CAP), (0, CAP)]
    return poly([(x0 + x, y) for x, y in pts]), 55.0


def letter_a(x0):
    # Flat-topped triangle with a triangular counter; the counter holds the
    # orange accent, echoing the road vanishing into the mountains.
    w, top = 68.0, 18.0
    run = (w - top) / 2                      # horizontal run of each leg
    k = CAP / run                            # leg slope dy/dx
    apex_in = CAP - (w / 2 - STEM) * k
    outer = [(0, CAP), (run, 0), (run + top, 0), (w, CAP), (w - STEM, CAP),
             (w / 2, apex_in), (STEM, CAP)]
    acc_half = w / 2 - STEM - 8.0            # 8-unit gap to the counter edge
    accent = [(w / 2 - acc_half, CAP), (w / 2, CAP - acc_half * k), (w / 2 + acc_half, CAP)]
    sh = lambda ps: [(x0 + x, y) for x, y in ps]
    return poly(sh(outer)), poly(sh(accent)), w


def letter_j(x0):
    w = 36.0
    s = w - STEM
    ro, ri = 14.0, 6.0
    d = (f"M{f(x0 + s)},0 H{f(x0 + w)} V{f(CAP - ro)} "
         f"A{f(ro)},{f(ro)} 0 0 1 {f(x0 + w - ro)},{f(CAP)} H{f(x0)} V{f(CAP - BAR)} "
         f"H{f(x0 + s - ri)} A{f(ri)},{f(ri)} 0 0 0 {f(x0 + s)},{f(CAP - BAR - ri)} Z")
    return d, w


def rrect(x, y, w, h, r):
    return (f"M{f(x + r)},{f(y)} H{f(x + w - r)} A{f(r)},{f(r)} 0 0 1 {f(x + w)},{f(y + r)} "
            f"V{f(y + h - r)} A{f(r)},{f(r)} 0 0 1 {f(x + w - r)},{f(y + h)} H{f(x + r)} "
            f"A{f(r)},{f(r)} 0 0 1 {f(x)},{f(y + h - r)} V{f(y + r)} "
            f"A{f(r)},{f(r)} 0 0 1 {f(x + r)},{f(y)} Z")


def letter_o(x0):
    w = 57.0
    # Counter drawn in reverse winding via evenodd so the O stays one path.
    return rrect(x0, 0, w, CAP, 13.0) + " " + rrect(x0 + STEM, BAR, w - 2 * STEM, CAP - 2 * BAR, 4.0), w


def wordmark(ink=WHITE, accent=ORANGE, x0=0.0, y0=0.0, scale=1.0):
    k, wk = letter_k(0)
    a, acc, wa = letter_a(wk + TRACK)
    xj = wk + TRACK + wa + TRACK + 1.0
    j, wj = letter_j(xj)
    xo = xj + wj + TRACK + 2.0
    o, wo = letter_o(xo)
    width = xo + wo
    body = (f'<g transform="translate({f(x0)} {f(y0)}) scale({f(scale)})">'
            f'<path fill="{ink}" fill-rule="evenodd" d="{k} {a} {j} {o}"/>'
            f'<path fill="{accent}" d="{acc}"/></g>')
    return body, width


WORD_W = wordmark()[1]


# ── Mountain-road scene ──────────────────────────────────────────────────────
HORIZON = 165.0


def splash_polygons(prefixes):
    """Reuse the hand-traced 320x240 landscape rather than approximate it."""
    nodes = ET.parse(SPLASH_SOURCE).getroot()
    result = []
    for node in nodes:
        if node.tag.rsplit("}", 1)[-1] != "polygon":
            continue
        if node.get("id", "").startswith(prefixes):
            result.append(f'<polygon fill="{node.get("fill")}" points="{node.get("points")}"/>')
    return "".join(result)


CORE_MOUNTAINS = splash_polygons(("left-", "right-"))
CORE_TREES = splash_polygons(("tree-",))

# (distance from the 320-unit scene edge, ridge height, width of lit rim).
# Each outer range begins at the same contour as the traced central artwork.
LEFT_OUTER_RIDGE = [(0, 146, 7), (29, 131, 1), (59, 150, 7),
                    (88, 139, 1), (115, 152, 8), (148, 126, 1),
                    (182, 148, 7), (207, 137, 1), (237, 153, 8),
                    (264, 129, 1), (297, 149, 7), (330, 135, 1)]
RIGHT_OUTER_RIDGE = [(0, 146, 7), (30, 130, 1), (60, 149, 6),
                     (95, 139, 1), (120, 154, 8), (154, 128, 1),
                     (185, 151, 7), (214, 136, 1), (245, 153, 8),
                     (280, 132, 1), (311, 149, 7), (340, 138, 1)]
# The YouTube hard-edge variant ends each range at the black foreground,
# rather than cutting through a mountain at the artwork boundary.
LEFT_BOUNDED_RIDGE = [(0, 146, 7), (29, 131, 1), (55, 151, 6),
                      (69, 161, 5), (81, 175, 3), (90, 182, 1)]
RIGHT_BOUNDED_RIDGE = [(0, 146, 7), (30, 130, 1), (56, 150, 6),
                       (72, 164, 5), (84, 177, 3), (91, 182, 1)]
# A wider clean silhouette for the road-fade banner reference.
LEFT_WIDE_BOUNDED_RIDGE = [(0, 146, 7), (29, 131, 1), (55, 151, 6),
                           (95, 140, 1), (120, 151, 8), (150, 160, 5),
                           (175, 174, 3), (190, 182, 1)]
RIGHT_WIDE_BOUNDED_RIDGE = [(0, 146, 7), (30, 130, 1), (60, 149, 6),
                            (95, 139, 1), (120, 153, 8), (150, 160, 5),
                            (175, 174, 3), (190, 182, 1)]


def outer_mountains(edge, side, ridge, *, seam_overlap=0):
    """Continue the reference silhouette with varied peaks and narrow facets."""
    top = [(edge + side * d, y) for d, y, _ in ridge]
    dark_top = [(edge + side * d, y + rim) for d, y, rim in ridge]
    if seam_overlap:
        # Extend under the central polygon so antialiasing cannot expose a join.
        top[0] = (edge - side * seam_overlap, top[0][1])
        dark_top[0] = (edge - side * seam_overlap, dark_top[0][1])
    far = top[-1][0]
    inner = edge - side * seam_overlap
    out = [f'<path fill="#52555a" d="{poly(top + [(far, 185), (inner, 185)])}"/>',
           f'<path fill="#171a1d" d="{poly(dark_top + [(far, 185), (inner, 185)])}"/>']
    inward = -side
    for d, y, _ in ridge[1:]:
        if y < 140:
            x = edge + side * d
            streak = [(x, y + 1), (x + inward * 12, y + 20),
                      (x + inward * 8, y + 15), (x + inward * 30, y + 37),
                      (x + inward * 17, y + 24)]
            out.append(f'<path fill="#44484d" d="{poly(streak)}"/>')
    return "".join(out)


def pine(x, base, h):
    """The stepped, three-bough fir silhouette used by the boot artwork."""
    half, top = h * 0.28, base - h
    left = [(x, top), (x - half * 0.56, top + h * 0.34),
            (x - half * 0.28, top + h * 0.34),
            (x - half * 0.82, top + h * 0.66),
            (x - half * 0.54, top + h * 0.66), (x - half, base)]
    pts = left + [(x + half, base)] + [(2 * x - px, py) for px, py in reversed(left[1:-1])]
    return f'<path fill="#000000" d="{poly(pts)}"/>'


def outer_trees(edge, side, extent, seed, *, taper=False,
                ground_width=None, ridge=None):
    rnd = random.Random(seed)
    out, distance = [], 9.0
    while distance < extent + 15:
        x = edge + side * distance
        base = rnd.uniform(177, 181)
        height = rnd.uniform(20, 29)
        if ground_width is not None and ridge is not None:
            if distance >= ridge[-1][0]:
                break
            cx = ground_width / 2
            slope = max(0, min(1, (abs(x - cx) - 6) / (cx - 6)))
            base = HORIZON + (182 - HORIZON) * slope + 2
            for (d0, y0, _), (d1, y1, _) in zip(ridge, ridge[1:]):
                if distance <= d1:
                    ridge_y = y0 + (y1 - y0) * (distance - d0) / (d1 - d0)
                    height = min(height, base - ridge_y - 2)
                    break
        if taper:
            height *= max(0, min(1, (extent - distance) / 32))
        if height >= 3:
            out.append(pine(x, base, height))
        distance += rnd.uniform(12, 18)
    return "".join(out)


GLOW = ('<defs><filter id="glow" x="-20%" y="-20%" width="140%" height="140%">'
        '<feGaussianBlur stdDeviation="1.6"/></filter></defs>')

RING_GLOW = "#d86200"   # the outer ring reads darker than the power ring


def scene_brand(cx: float) -> str:
    """Mark with glowing rings and the wordmark, in 320x240 scene units.
    Draw it before the landscape so the range overlaps the wordmark."""
    rings = (f'<path fill="{RING_GLOW}" d="{ring_path(*THIN_RING)}"/>'
             f'<path fill="{ORANGE}" d="{ring_path(*POWER_RING)}"/>')
    ws = (271 - 49) / WORD_W
    return (f'<g transform="translate({f(cx)} 60) scale(1.1)">'
            f'<g filter="url(#glow)" opacity="0.9">{rings}</g>{mark(thin=RING_GLOW)}</g>'
            + wordmark(x0=cx - WORD_W * ws / 2, y0=108, scale=ws)[0])


def scene(width: float, height: float = 240.0, *, include_brand=True,
          include_background=True, include_defs=True, bounded=False,
          road_to_bottom=False, wide_bounded=False,
          road_end_fade=False) -> str:
    cx = width / 2
    parts = []
    if include_defs:
        parts.append(GLOW)
    if include_background:
        parts.append(f'<rect width="{f(width)}" height="{f(height)}" fill="{BLACK}"/>')
    if include_brand:
        parts.append(scene_brand(cx))
    core_left = round((cx - 160) * 4) / 4
    core_right = core_left + 319
    if core_left > 0:
        parts.append(outer_mountains(core_left, -1,
                                     LEFT_WIDE_BOUNDED_RIDGE if wide_bounded else
                                     LEFT_BOUNDED_RIDGE if bounded else LEFT_OUTER_RIDGE,
                                     seam_overlap=2 if wide_bounded else 0))
    if core_right < width - 1:
        parts.append(outer_mountains(core_right, 1,
                                     RIGHT_WIDE_BOUNDED_RIDGE if wide_bounded else
                                     RIGHT_BOUNDED_RIDGE if bounded else RIGHT_OUTER_RIDGE,
                                     seam_overlap=2 if wide_bounded else 0))
    parts.append(f'<g transform="translate({f(core_left)} 0)">{CORE_MOUNTAINS}</g>')
    if core_left > 0:
        parts.append(outer_trees(core_left, -1, core_left, 17, taper=bounded,
                                 ground_width=width if wide_bounded else None,
                                 ridge=LEFT_WIDE_BOUNDED_RIDGE if wide_bounded else None))
    if core_right < width - 1:
        parts.append(outer_trees(core_right, 1, width - core_right, 31, taper=bounded,
                                 ground_width=width if wide_bounded else None,
                                 ridge=RIGHT_WIDE_BOUNDED_RIDGE if wide_bounded else None))
    parts.append(f'<g transform="translate({f(core_left)} 0)">{CORE_TREES}</g>')
    # Ground: black foreground that meets the horizon at the road.
    parts.append(f'<path fill="{BLACK}" d="{poly([(0, 182), (cx - 6, HORIZON), (cx + 6, HORIZON), (width, 182), (width, height), (0, height)])}"/>')
    # Road edges: tapered strokes that leave the vanishing point almost flat,
    # bend gently down, then continue on their tangent past the 4:3 frame so
    # wide canvases keep the same perspective.
    edges = []
    for sgn in (-1, 1):
        vp = cx + sgn * 5
        if road_to_bottom:
            # Carry the hard-edge banner's borders beyond its lower crop.
            # The ridges and trees still finish cleanly against black.
            c1, e1 = cx + sgn * 80, cx + sgn * (cx - 12)
            far_outer = cx + sgn * 570
            far_inner = cx + sgn * 552
            edges.append(
                f"M{f(vp)},{f(HORIZON)} Q{f(c1)},{f(HORIZON + 15)} {f(e1)},205 "
                f"C{f(e1 + sgn * 95)},221 {f(far_outer - sgn * 30)},400 {f(far_outer)},520 "
                f"L{f(far_inner)},520 "
                f"C{f(far_inner - sgn * 30)},400 {f(e1 + sgn * 90)},228 {f(e1)},212 "
                f"Q{f(c1)},{f(HORIZON + 19)} {f(vp)},{f(HORIZON + 0.4)} Z")
        elif bounded:
            # Square cut where the line ends, level with the ridge ends.
            c1, e1 = cx + sgn * 80, cx + sgn * (cx - 12)
            edges.append(f"M{f(vp)},{f(HORIZON)} Q{f(c1)},{f(HORIZON + 15)} {f(e1)},205 "
                         f"L{f(e1)},212 Q{f(c1)},{f(HORIZON + 19)} "
                         f"{f(vp)},{f(HORIZON + 0.4)} Z")
        else:
            c1, e1, far = cx + sgn * 80, cx + sgn * 162, cx + sgn * 360
            edges.append(f"M{f(vp)},{f(HORIZON)} Q{f(c1)},{f(HORIZON + 15)} {f(e1)},205 L{f(far)},{f(205 + 198 * 0.305)} "
                         f"L{f(far)},{f(212 + 198 * 0.36)} L{f(e1)},212 Q{f(c1)},{f(HORIZON + 19)} {f(vp)},{f(HORIZON + 0.4)} Z")
    road = " ".join(edges)
    road_mask = ""
    if road_end_fade:
        parts.append(
            '<defs><linearGradient id="road-end-gradient" gradientUnits="objectBoundingBox" '
            'x1="0" x2="1" y1="0" y2="0">'
            '<stop offset="0" stop-color="black"/>'
            '<stop offset="0.025" stop-color="black"/>'
            '<stop offset="0.12" stop-color="white"/>'
            '<stop offset="0.88" stop-color="white"/>'
            '<stop offset="0.975" stop-color="black"/>'
            '<stop offset="1" stop-color="black"/>'
            '</linearGradient><mask id="road-end-mask" maskContentUnits="objectBoundingBox">'
            '<rect width="1" height="1" fill="url(#road-end-gradient)"/>'
            '</mask></defs>')
        road_mask = ' mask="url(#road-end-mask)"'
    parts.append(f'<path fill="{ORANGE}" opacity="0.6" filter="url(#glow)"{road_mask} d="{road}"/>')
    parts.append(f'<path fill="{ORANGE}"{road_mask} d="{road}"/>')
    # Centre dashes in true perspective: a point at road distance d projects
    # to y = horizon + K / d; half-width scales with the same factor.
    K, dash, period = 75.0, 0.6, 1.4
    d = 1.2
    while HORIZON + K / d > HORIZON + 1.5:
        y_near, y_far = HORIZON + K / d, HORIZON + K / (d + dash)
        w_near, w_far = (y_near - HORIZON) * 0.13, (y_far - HORIZON) * 0.13
        parts.append(f'<path fill="{ORANGE}" d="{poly([(cx - w_near, y_near), (cx + w_near, y_near), (cx + w_far, y_far), (cx - w_far, y_far)])}"/>')
        d += period
    return "".join(parts)


# YouTube crops the 2560x1440 banner to a central 1546x423 area on phones, so
# everything that must be seen goes there: from the ring glow's top (scene
# y ~8) to the end of the road edges (scene y 214). The near dashes run on
# below it for the TV view.
YT_W, YT_H = 2560, 1440
YT_SAFE_W, YT_SAFE_H = 1546, 423
YT_SCALE = 2.0                          # uniform, so firs and dashes keep shape
YT_SCENE_TOP, YT_SCENE_BOTTOM = 8.0, 214.0


def youtube_banner(*, fade_edges=True):
    """Mark, name and road inside YouTube's safe crop, with black side space."""
    # The faded version needs room for its fade; the hard-edged one ends where
    # the bounded ridges reach the ground.
    scene_w = 700.0 if fade_edges else 500.0
    left = YT_W / 2 - scene_w * YT_SCALE / 2
    safe_top = (YT_H - YT_SAFE_H) / 2
    spare = YT_SAFE_H - (YT_SCENE_BOTTOM - YT_SCENE_TOP) * YT_SCALE
    top = safe_top + spare / 2 - YT_SCENE_TOP * YT_SCALE
    place = f'transform="translate({f(left)} {f(top)}) scale({f(YT_SCALE)})"'
    landscape = scene(scene_w, include_brand=False, include_background=False,
                      include_defs=False, bounded=not fade_edges,
                      road_to_bottom=not fade_edges)
    landscape_group = f'<g {place}>{landscape}</g>'
    fade_defs = ""
    if fade_edges:
        right = left + scene_w * YT_SCALE
        fade_defs = (
            '<defs><linearGradient id="youtube-edge-fade" gradientUnits="userSpaceOnUse" '
            f'x1="{f(left)}" x2="{f(right)}" y1="0" y2="0">'
            '<stop offset="0" stop-color="#000000"/>'
            '<stop offset="0.18" stop-color="#ffffff"/>'
            '<stop offset="0.82" stop-color="#ffffff"/>'
            '<stop offset="1" stop-color="#000000"/></linearGradient>'
            '<mask id="youtube-landscape-mask" maskUnits="userSpaceOnUse" '
            f'x="0" y="0" width="{YT_W}" height="{YT_H}">'
            f'<rect width="{YT_W}" height="{YT_H}" fill="url(#youtube-edge-fade)"/>'
            '</mask></defs>')
        landscape_group = f'<g mask="url(#youtube-landscape-mask)">{landscape_group}</g>'
    return (GLOW + fade_defs
            + f'<rect width="{YT_W}" height="{YT_H}" fill="{BLACK}"/>'
            + f'<g {place}>{scene_brand(scene_w / 2)}</g>' + landscape_group)


def youtube_banner_road_fade():
    """Wide clean landscape with only the two road border tips fading out."""
    scene_w = 700.0
    left = (YT_W - scene_w * YT_SCALE) / 2
    # Match the supplied reference's lower landscape and larger central mark.
    landscape_top = 540.0
    brand_left, brand_top, brand_scale = 405.0, 481.625, 2.5
    landscape = scene(scene_w, include_brand=False, include_background=False,
                      include_defs=False, bounded=True, wide_bounded=True,
                      road_end_fade=True)
    return (GLOW + f'<rect width="{YT_W}" height="{YT_H}" fill="{BLACK}"/>'
            + f'<g transform="translate({f(brand_left)} {f(brand_top)}) scale({f(brand_scale)})">'
            + scene_brand(scene_w / 2) + '</g>'
            + f'<g transform="translate({f(left)} {f(landscape_top)}) scale({f(YT_SCALE)})">'
            + landscape + '</g>')


def github_banner_road_fade():
    """Fit the same road-fade composition into the 3:1 README banner."""
    scene_w = 700.0
    landscape_scale = 0.65
    landscape_left = (720 - scene_w * landscape_scale) / 2
    landscape_top = 63.0
    brand_scale = landscape_scale * 1.25
    brand_left = 360 - scene_w / 2 * brand_scale
    brand_top = landscape_top + (481.625 - 540.0) * landscape_scale / YT_SCALE
    landscape = scene(scene_w, include_brand=False, include_background=False,
                      include_defs=False, bounded=True, wide_bounded=True,
                      road_end_fade=True)
    return (GLOW + f'<rect width="720" height="240" fill="{BLACK}"/>'
            + f'<g transform="translate({f(brand_left)} {f(brand_top)}) scale({f(brand_scale)})">'
            + scene_brand(scene_w / 2) + '</g>'
            + f'<g transform="translate({f(landscape_left)} {f(landscape_top)}) scale({f(landscape_scale)})">'
            + landscape + '</g>')


# ── Writers ──────────────────────────────────────────────────────────────────
def svg(w, h, body, title, bg=None, pixel_scale=4):
    rect = f'<rect width="{f(w)}" height="{f(h)}" fill="{bg}"/>' if bg else ""
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {f(w)} {f(h)}" '
            f'width="{f(w * pixel_scale)}" height="{f(h * pixel_scale)}">\n<title>{title}</title>\n{rect}{body}\n</svg>\n')


def write(name, text):
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / name).write_bytes(text.encode("utf-8"))


def main():
    pad = 6.0
    x0, y0, x1, y1 = mark_bounds()
    mw, mh = x1 - x0 + 2 * pad, y1 - y0 + 2 * pad
    at = f'<g transform="translate({f(-x0 + pad)} {f(-y0 + pad)})">'
    marks = {
        "kajo-mark.svg": (mark(), None, "KAJO mark - for dark backgrounds"),
        "kajo-mark-on-light.svg": (mark(bolt=INK), None, "KAJO mark - for light backgrounds"),
        "kajo-mark-mono-white.svg": (mark(WHITE, WHITE), None, "KAJO mark - single colour white"),
        "kajo-mark-mono-black.svg": (mark(INK, INK), None, "KAJO mark - single colour black"),
    }
    for name, (body, bg, title) in marks.items():
        write(name, svg(mw, mh, at + body + "</g>", title, bg))

    # Badge icon: mark centred on its visual bounds in a rounded square.
    size, inset = 108.0, 0.72
    s = size * inset / max(x1 - x0, y1 - y0)
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    badge = (f'<rect width="{f(size)}" height="{f(size)}" rx="24" fill="{BLACK}"/>'
             f'<g transform="translate({f(size / 2 - cx * s)} {f(size / 2 - cy * s)}) scale({f(s)})">{mark()}</g>')
    write("kajo-icon.svg", svg(size, size, badge, "KAJO app icon"))

    ww, wh = WORD_W + 2 * pad, CAP + 2 * pad
    for name, ink, acc, title in [
        ("kajo-wordmark.svg", WHITE, ORANGE, "KAJO wordmark - for dark backgrounds"),
        ("kajo-wordmark-on-light.svg", INK, ORANGE, "KAJO wordmark - for light backgrounds"),
        ("kajo-wordmark-mono-white.svg", WHITE, WHITE, "KAJO wordmark - single colour white"),
        ("kajo-wordmark-mono-black.svg", INK, INK, "KAJO wordmark - single colour black"),
    ]:
        write(name, svg(ww, wh, wordmark(ink, acc, pad, pad)[0], title))

    # Horizontal lockup: mark height = 2.2x cap height, wordmark centred on
    # the power ring's centre line.
    mark_h = y1 - y0
    ms = CAP * 2.2 / mark_h
    gap = 16.0
    hw = pad * 2 + (x1 - x0) * ms + gap + WORD_W
    hh = pad * 2 + mark_h * ms
    ring_cy = pad + (-y0) * ms
    for suffix, bolt, ink, title in [("", WHITE, WHITE, "for dark backgrounds"),
                                     ("-on-light", INK, INK, "for light backgrounds")]:
        body = (f'<g transform="translate({f(pad - x0 * ms)} {f(pad - y0 * ms)}) scale({f(ms)})">{mark(bolt=bolt)}</g>'
                + wordmark(ink, ORANGE, pad + (x1 - x0) * ms + gap, ring_cy - CAP / 2)[0])
        write(f"kajo-lockup-horizontal{suffix}.svg", svg(hw, hh, body, f"KAJO horizontal lockup - {title}"))

    # Stacked lockup: mark over wordmark, as on the boot splash.
    ms2 = WORD_W * 0.42 / (x1 - x0)
    sw = WORD_W + 2 * pad
    mark_block = mark_h * ms2
    sh = pad * 2 + mark_block + 14 + CAP
    for suffix, bolt, ink, title in [("", WHITE, WHITE, "for dark backgrounds"),
                                     ("-on-light", INK, INK, "for light backgrounds")]:
        body = (f'<g transform="translate({f(sw / 2 - (x0 + x1) / 2 * ms2)} {f(pad - y0 * ms2)}) scale({f(ms2)})">{mark(bolt=bolt)}</g>'
                + wordmark(ink, ORANGE, pad, pad + mark_block + 14)[0])
        write(f"kajo-lockup-stacked{suffix}.svg", svg(sw, sh, body, f"KAJO stacked lockup - {title}"))

    write("kajo-hero-4x3.svg", svg(320, 240, scene(320), "KAJO hero - boot splash scene, 4:3"))
    write("kajo-hero-16x9.svg", svg(426.667, 240, scene(426.667), "KAJO hero - boot splash scene, 16:9"))
    # GitHub: a 3:1 README header and the 2:1 repository social preview.
    write("kajo-banner-github.svg",
          svg(720, 240, github_banner_road_fade(),
              "KAJO banner - GitHub README header, road-fade style, 3:1"))
    write("kajo-social-preview.svg", svg(480, 240, scene(480), "KAJO social preview - 2:1"))
    write("kajo-banner-youtube.svg", svg(YT_W, YT_H, youtube_banner(),
                                         "KAJO YouTube channel banner, 2560x1440", pixel_scale=1))
    write("kajo-banner-youtube-hard-edge.svg",
          svg(YT_W, YT_H, youtube_banner(fade_edges=False),
              "KAJO YouTube channel banner - clean-edged landscape, 2560x1440",
              pixel_scale=1))
    write("kajo-banner-youtube-road-fade.svg",
          svg(YT_W, YT_H, youtube_banner_road_fade(),
              "KAJO YouTube channel banner - road ends fade, 2560x1440",
              pixel_scale=1))
    print(f"Wrote {len(list(OUT.glob('*.svg')))} SVG files to {OUT}")


if __name__ == "__main__":
    main()
