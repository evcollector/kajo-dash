#!/usr/bin/env python3
"""Render the KAJO-Dash demo video chapters from the firmware simulator.

A chapter is a scene script in tools/demo/scenes. The scene recorder
(tools/lvgl_native_preview/demo_recorder.cpp) boots the real firmware UI, plays the script in
simulated time and streams raw frames into ffmpeg, which stores them losslessly. This tool then
adds the chapter card and the caption rail and encodes H.264. It can also write a README GIF, a
contact sheet and the stills a scene asked for.

    python tools/make_demo_video.py                       every chapter in tools/demo/cut.json, then the full cut
    python tools/make_demo_video.py themes --gif          one chapter, with a README GIF
    python tools/make_demo_video.py replay --screen-only --sheet
                                                          authoring: record only, and look at a contact sheet

Everything lands in dist/demo (ignored by git). See docs/demo-video.md.
"""

from __future__ import annotations

import argparse
import functools
import json
import math
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "tools" / "lvgl_native_preview"
BUILD = SOURCE / "build_demo_recorder"
LVGL = ROOT / ".pio" / "libdeps" / "kajo" / "lvgl"
SCENES = ROOT / "tools" / "demo" / "scenes"
STORIES = ROOT / "tools" / "demo"
CUT = ROOT / "tools" / "demo" / "cut.json"
FONTS = ROOT / "tools" / "fonts" / "rajdhani"
BRAND = ROOT / "tools" / "logo" / "brand" / "png"
OUT = ROOT / "dist" / "demo"

FPS = 60
SPEED = 3  # how much faster than real time the firmware chapters play
PACE_MS = 300  # video time a pause the scene wrote for looking may last: one quick rhythm throughout
MIN_CAPTION_MS = 1000  # video time a caption stays, so a fast chapter can still be read
SCALE = 4
DISPLAY = (320, 240)
CANVAS = (1920, 1080)
# Even offsets keep every 4:2:0 chroma block inside one display pixel, so the colours of the
# upscaled UI survive the encode.
SCREEN_AT = (60, 60)
BEZEL = 18
RAIL = (1400, 60, 460, 960)  # x, y, width, height
CARD_SECONDS = 2.6
CARD_FADE = 0.5
FADE_IN_FRAMES = 8
FADE_OUT_FRAMES = 6

INK = (11, 12, 13)
PANEL = (23, 26, 29)
LINE = (44, 49, 55)
ORANGE = (255, 121, 0)
WHITE = (255, 255, 255)
SOFT = (214, 219, 224)
MUTED = (140, 147, 155)

DISCLOSURE = "Rendered from the KAJO-Dash firmware simulator: the real UI code, running on a PC. Demo data."
REPOSITORY = "github.com/evcollector/kajo-dash"


# ------------------------------------------------------------------------------ timeline logic
# Pure functions: they need neither Pillow nor ffmpeg, and test/test_make_demo_video.py pins them.


@dataclass(frozen=True)
class Span:
    """A caption on the rail. Fade-in starts at `start`; the caption is gone from `end`."""

    start: int
    end: int
    heading: str
    body: str


def caption_spans(captions: list[dict], total_frames: int) -> list[Span]:
    """Turn the recorder's caption events into spans.

    A caption lasts until the next caption, a caption-off, or its own until_frame, whichever
    comes first; the last one lasts to the end of the scene.
    """
    spans: list[Span] = []
    for index, event in enumerate(captions):
        if event.get("off"):
            continue
        start = int(event["frame"])
        end = total_frames
        if "until_frame" in event:
            end = min(end, int(event["until_frame"]))
        if index + 1 < len(captions):
            end = min(end, int(captions[index + 1]["frame"]))
        if end > start:
            spans.append(Span(start, end, event.get("heading", ""), event.get("body", "")))
    return spans


def fade_alpha(frame: int, span: Span) -> float:
    """Opacity of a span's caption at a frame: eased in over a few frames, eased out before `end`."""
    rise = (frame - span.start + 1) / FADE_IN_FRAMES
    fall = (span.end - frame) / FADE_OUT_FRAMES
    t = max(0.0, min(1.0, rise, fall))
    return round(t * t * (3.0 - 2.0 * t), 2)


def rail_timeline(spans: list[Span], total_frames: int) -> list[tuple[int, int, int | None, float]]:
    """Run-length encode the rail over every frame: (first_frame, frame_count, span_index, alpha).

    span_index is None where no caption shows. Held frames collapse into one entry; only the few
    fading frames at either end need an entry each.
    """
    runs: list[tuple[int, int, int | None, float]] = []
    cursor = 0
    for index, span in enumerate(spans):
        if span.start > cursor:
            runs.append((cursor, span.start - cursor, None, 0.0))
        for frame in range(span.start, span.end):
            alpha = fade_alpha(frame, span)
            last = runs[-1] if runs else None
            if last and last[2] == index and last[3] == alpha and last[0] + last[1] == frame:
                runs[-1] = (last[0], last[1] + 1, index, alpha)
            else:
                runs.append((frame, 1, index, alpha))
        cursor = span.end
    if cursor < total_frames:
        runs.append((cursor, total_frames - cursor, None, 0.0))
    return runs


def ffconcat_text(entries: list[tuple[str, int]], fps: int) -> str:
    """An ffconcat list holding each image for its frame count.

    The demuxer ignores the last duration, so the final image is listed a second time.
    """
    lines = ["ffconcat version 1.0"]
    for name, frames in entries:
        lines.append(f"file '{name}'")
        lines.append(f"duration {frames / fps:.6f}")
    if entries:
        lines.append(f"file '{entries[-1][0]}'")
    return "\n".join(lines) + "\n"


def load_cut(path: Path = CUT) -> list[str]:
    data = json.loads(path.read_text(encoding="utf-8"))
    chapters = data.get("chapters")
    if not isinstance(chapters, list) or not all(isinstance(name, str) for name in chapters):
        raise ValueError(f"{path}: 'chapters' must be a list of scene names")
    return chapters


def load_cards(path: Path = CUT) -> dict[str, dict]:
    """The optional opening and closing cards of the full cut: {"intro": {...}, "outro": {...}}.

    A card has a `title`, optionally a `subtitle` and `lines`, and how long it holds (`seconds`).
    """
    data = json.loads(path.read_text(encoding="utf-8"))
    cards: dict[str, dict] = {}
    for key in ("intro", "outro"):
        if key not in data:
            continue
        card = data[key]
        if not isinstance(card, dict) or not isinstance(card.get("title"), str):
            raise ValueError(f"{path}: '{key}' needs a title")
        if not isinstance(card.get("lines", []), list) or not all(isinstance(line, str) for line in card.get("lines", [])):
            raise ValueError(f"{path}: '{key}' lines must be a list of strings")
        cards[key] = card
    return cards


def clock(seconds: float) -> str:
    """m:ss for a chapter list; a player or a video site reads these as chapter starts."""
    whole = int(seconds)
    return f"{whole // 60}:{whole % 60:02d}"


def chapter_list(marks: list[tuple[str, float]]) -> str:
    """`m:ss Title` lines, from (title, seconds long) in order. Starts at 0:00, as video sites require."""
    lines, start = [], 0.0
    for title, length in marks:
        lines.append(f"{clock(start)} {title}")
        start += length
    return "\n".join(lines) + "\n"


def chapter_metadata(marks: list[tuple[str, float]]) -> str:
    """ffmpeg's FFMETADATA1 chapter list for the same marks, in milliseconds."""
    out, start = [";FFMETADATA1"], 0
    for title, length in marks:
        end = start + round(length * 1000)
        escaped = re.sub(r"([=;#\\])", r"\\\1", title)
        out += ["[CHAPTER]", "TIMEBASE=1/1000", f"START={start}", f"END={end}", f"title={escaped}"]
        start = end
    return "\n".join(out) + "\n"


def scene_path(name: str) -> Path:
    path = SCENES / f"{name}.scn"
    if not path.exists():
        raise FileNotFoundError(f"no scene named '{name}' in {SCENES} (chapters: {', '.join(known_chapters())})")
    return path


def known_chapters() -> list[str]:
    """Scenes plus the capture modules in tools/demo (`<name>_capture.py`), sorted."""
    names = {p.stem for p in SCENES.glob("*.scn")} | {p.stem.removesuffix("_capture") for p in STORIES.glob("*_capture.py")}
    return sorted(names) or ["none"]


def story_path(name: str) -> Path | None:
    """The capture module for a chapter that is not firmware footage, if it has one."""
    path = STORIES / f"{name}_capture.py"
    return path if path.exists() else None


def check_chapter(name: str) -> None:
    if story_path(name) is None:
        scene_path(name)


def x264_arguments(crf: int, preset: str) -> list[str]:
    """H.264 for a flat-colour UI, tagged BT.709 so players do not shift the brand orange."""
    return [
        "-c:v", "libx264", "-preset", preset, "-crf", str(crf), "-tune", "animation",
        "-profile:v", "high", "-level:v", "4.2", "-pix_fmt", "yuv420p",
        "-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "bt709", "-color_range", "tv",
        "-movflags", "+faststart", "-r", str(FPS), "-an",
    ]  # fmt: skip


# BT.709 matrix and accurate rounding for the one RGB -> YUV conversion the video goes through,
# then the frames tagged to match (the encoder writes what the frames carry).
TO_YUV = (
    "scale=out_color_matrix=bt709:out_range=tv:flags=accurate_rnd+full_chroma_int,format=yuv420p,"
    "setparams=colorspace=bt709:color_primaries=bt709:color_trc=bt709:range=tv"
)


def compose_graph(scene_seconds: float, with_card: bool) -> str:
    """The filter graph: card, then bezel + screen + rail, with dips to black at the joins."""
    sx, sy = SCREEN_AT
    rx, ry = RAIL[0], RAIL[1]
    scene = (
        f"[1:v][2:v]overlay={sx}:{sy}:format=gbrp:shortest=1[base];"
        f"[base][3:v]overlay={rx}:{ry}:format=gbrp:eof_action=pass,"
        f"fade=t=in:st=0:d=0.35,fade=t=out:st={max(0.0, scene_seconds - 0.5):.3f}:d=0.5,"
        f"{TO_YUV},fps={FPS},setsar=1[main];"
    )
    if not with_card:
        return scene + "[main]null[v]"
    card = (
        f"[0:v]format=gbrp,fade=t=in:st=0:d={CARD_FADE},fade=t=out:st={CARD_SECONDS - CARD_FADE}:d={CARD_FADE},"
        f"{TO_YUV},fps={FPS},setsar=1[card];"
    )
    return card + scene + "[card][main]concat=n=2:v=1:a=0[v]"


def gif_filter(fps: int, scale: int) -> str:
    """Flat UI colours need a small palette and no dithering; only the changed rectangle is stored."""
    width, height = DISPLAY[0] * scale, DISPLAY[1] * scale
    return (
        f"fps={fps},scale={width}:{height}:flags=area,split[a][b];"
        "[a]palettegen=max_colors=128:stats_mode=diff[p];"
        "[b][p]paletteuse=dither=none:diff_mode=rectangle"
    )


# ------------------------------------------------------------------------------ tools


def run(command: list[str], **kwargs) -> None:
    print("+", subprocess.list2cmdline([str(part) for part in command]), flush=True)
    subprocess.run([str(part) for part in command], check=True, **kwargs)


def find_ffmpeg(name: str = "ffmpeg") -> str:
    override = os.environ.get(name.upper())
    found = override or shutil.which(name)
    if not found:
        raise FileNotFoundError(f"{name} was not found in PATH (set {name.upper()} to its location)")
    return found


def require_encoders(ffmpeg: str, names: tuple[str, ...]) -> None:
    """Fail early, and clearly, when this ffmpeg build lacks an encoder the pipeline needs."""
    listing = subprocess.run([ffmpeg, "-hide_banner", "-encoders"], capture_output=True, text=True, check=False).stdout
    # " V....D libx264   libx264 H.264 ...": flags, then the name, then a description that may
    # itself mention another encoder, so only the name column counts.
    available = {parts[1] for parts in (line.split() for line in listing.splitlines()) if len(parts) > 1 and len(parts[0]) == 6}
    missing = [name for name in names if name not in available]
    if missing:
        raise FileNotFoundError(
            f"this ffmpeg build has no {', '.join(missing)} encoder; install a full build "
            "(the gyan.dev and BtbN Windows builds have them)"
        )


def recorder_executable() -> Path | None:
    for candidate in (
        BUILD / "Release" / "cyd_demo_recorder.exe",
        BUILD / "cyd_demo_recorder.exe",
        BUILD / "cyd_demo_recorder",
    ):
        if candidate.exists():
            return candidate
    return None


def build_recorder() -> Path:
    if not LVGL.joinpath("lvgl.h").exists():
        raise FileNotFoundError(
            "The PlatformIO LVGL library is missing. Build the kajo environment once, then retry: "
            "pio run -e kajo"
        )
    if shutil.which("cmake") is None:
        raise FileNotFoundError("CMake was not found in PATH")
    BUILD.mkdir(parents=True, exist_ok=True)
    configure = ["cmake", "-Wno-dev", "-S", SOURCE, "-B", BUILD, f"-DLVGL_DIR={LVGL}"]
    configure += ["-A", "x64"] if os.name == "nt" else ["-DCMAKE_BUILD_TYPE=Release"]
    run(configure)
    run(["cmake", "--build", BUILD, "--config", "Release", "--target", "cyd_demo_recorder"])
    executable = recorder_executable()
    if executable is None:
        raise FileNotFoundError("The demo recorder was not produced")
    return executable


# ------------------------------------------------------------------------------ recording


@dataclass
class Recording:
    name: str
    screen: Path
    events: dict

    @property
    def frames(self) -> int:
        return int(self.events["frames"])

    @property
    def seconds(self) -> float:
        return self.frames / int(self.events["fps"])


def record(recorder: Path, ffmpeg: str, scene: Path, out: Path, speed: int = SPEED, min_caption: int = MIN_CAPTION_MS, pace: int = PACE_MS) -> Recording:
    """Play a scene and store its frames losslessly.

    Lossless H.264 in RGB is bit-exact like FFV1 but predicts from the previous frame, and a UI
    that mostly holds still compresses to a tenth of the size.
    """
    out.mkdir(parents=True, exist_ok=True)
    name = scene.stem
    screen = out / f"{name}.screen.mkv"
    events_file = out / f"{name}.events.json"
    stills = out / "stills"
    width, height = DISPLAY[0] * SCALE, DISPLAY[1] * SCALE

    recorder_command = [
        str(recorder), str(scene), "--raw=-", f"--events={events_file}", f"--stills={stills}",
        f"--fps={FPS}", f"--scale={SCALE}", f"--speed={speed}", f"--pace={pace}", f"--min-caption={min_caption}",
    ]  # fmt: skip
    encoder_command = [
        ffmpeg, "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
        "-s", f"{width}x{height}", "-framerate", str(FPS), "-i", "pipe:0",
        "-c:v", "libx264rgb", "-qp", "0", "-preset", "superfast", "-g", "120", str(screen),
    ]  # fmt: skip
    print("+", subprocess.list2cmdline(recorder_command), "|", "ffmpeg ... lossless", flush=True)

    with subprocess.Popen(recorder_command, stdout=subprocess.PIPE) as recorder_process:
        with subprocess.Popen(encoder_command, stdin=recorder_process.stdout) as encoder_process:
            recorder_process.stdout.close()
            encoder_code = encoder_process.wait()
        recorder_code = recorder_process.wait()
    if recorder_code != 0:
        screen.unlink(missing_ok=True)
        raise RuntimeError(f"scene '{name}' failed (recorder exit code {recorder_code}); see the message above")
    if encoder_code != 0:
        raise RuntimeError(f"ffmpeg could not store the frames of '{name}' (exit code {encoder_code})")

    events = json.loads(events_file.read_text(encoding="utf-8"))
    convert_stills(stills, name)
    return Recording(name, screen, events)


def record_story(ffmpeg: str, name: str, out: Path) -> Recording:
    """A chapter that is not firmware footage: tools/demo/<name>_capture.py makes its own screen
    layer and caption events, at the same size and rate as a scene's."""
    import importlib.util

    path = story_path(name)
    spec = importlib.util.spec_from_file_location(f"{name}_capture", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    screen, events = module.capture(out, ffmpeg, FPS)
    return Recording(name, screen, events)


def make_card_clip(ffmpeg: str, image, seconds: float, target: Path, crf: int, preset: str) -> Path:
    """An opening or closing card on its own, with the same fades and encode as a chapter's card."""
    source = target.with_suffix(".png")
    image.save(source)
    graph = (
        f"[0:v]format=gbrp,fade=t=in:st=0:d={CARD_FADE},fade=t=out:st={seconds - CARD_FADE}:d={CARD_FADE},"
        f"{TO_YUV},fps={FPS},setsar=1[v]"
    )
    run([
        ffmpeg, "-y", "-loglevel", "error", "-loop", "1", "-framerate", str(FPS), "-t", str(seconds), "-i", source,
        "-filter_complex", graph, "-map", "[v]", *x264_arguments(crf, preset), target,
    ])  # fmt: skip
    source.unlink()
    return target


def convert_stills(folder: Path, name: str) -> None:
    """The recorder writes stills as PPM at the panel's own 320x240; keep them as PNG."""
    if not folder.exists():
        return
    from PIL import Image

    for ppm in sorted(folder.glob(f"{name}_*.ppm")):
        with Image.open(ppm) as image:
            image.convert("RGB").save(ppm.with_suffix(".png"))
        ppm.unlink()


# ------------------------------------------------------------------------------ artwork (Pillow)


@functools.lru_cache(maxsize=None)
def font(weight: str, size: int):
    from PIL import ImageFont

    return ImageFont.truetype(str(FONTS / f"Rajdhani-{weight}.ttf"), size)


@functools.lru_cache(maxsize=None)
def brand(name: str):
    from PIL import Image

    return Image.open(BRAND / name).convert("RGBA")


def fitted(image, width: int):
    from PIL import Image

    return image.resize((width, round(image.height * width / image.width)), Image.LANCZOS)


def wrap(draw, text: str, face, width: int) -> list[str]:
    lines: list[str] = []
    for paragraph in text.split("\n"):
        line = ""
        for word in paragraph.split():
            trial = f"{line} {word}".strip()
            if line and draw.textlength(trial, font=face) > width:
                lines.append(line)
                line = word
            else:
                line = trial
        lines.append(line)
    return lines


def spaced(draw, origin, text: str, face, fill, spacing: float) -> None:
    x, y = origin
    for character in text:
        draw.text((x, y), character, font=face, fill=fill)
        x += draw.textlength(character, font=face) + spacing


def draw_lines(draw, x: int, y: int, lines: list[str], face, fill, leading: int, anchor: str = "la") -> int:
    for line in lines:
        draw.text((x, y), line, font=face, fill=fill, anchor=anchor)
        y += leading
    return y


def render_background():
    """Canvas, a soft shadow and the bezel the display sits in."""
    from PIL import Image, ImageDraw, ImageFilter

    width, height = CANVAS
    image = Image.new("RGB", CANVAS, INK)
    gradient = Image.linear_gradient("L").resize(CANVAS)
    image.paste(Image.new("RGB", CANVAS, (17, 19, 22)), mask=gradient.point(lambda v: v // 3))

    sx, sy = SCREEN_AT
    box = (sx - BEZEL, sy - BEZEL, sx + DISPLAY[0] * SCALE + BEZEL, sy + DISPLAY[1] * SCALE + BEZEL)
    shadow = Image.new("L", CANVAS, 0)
    ImageDraw.Draw(shadow).rounded_rectangle((box[0], box[1] + 10, box[2], box[3] + 10), radius=30, fill=170)
    image.paste(Image.new("RGB", CANVAS, (0, 0, 0)), mask=shadow.filter(ImageFilter.GaussianBlur(18)))
    ImageDraw.Draw(image).rounded_rectangle(box, radius=26, fill=PANEL, outline=LINE, width=2)
    return image


def render_rail_static(number: int | None, title: str, subtitle: str, note: str = "", disclosure: str = DISCLOSURE):
    """Everything on the rail that does not change during a chapter. Returns the layer, the y at
    which captions may start and the y they must stay above (the footer, which carries the
    chapter's note, if it has one, above the standing disclosure)."""
    from PIL import Image, ImageDraw

    _, _, width, height = RAIL
    layer = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)

    lockup = fitted(brand("kajo-lockup-horizontal.png"), 300)
    layer.alpha_composite(lockup, (0, 0))
    y = lockup.height + 78

    if number is not None:
        spaced(draw, (0, y), f"CHAPTER {number:02d}", font("Bold", 24), ORANGE, 4)
        y += 40
    y = draw_lines(draw, 0, y, wrap(draw, title, font("Bold", 62), width), font("Bold", 62), WHITE, 64)
    if subtitle:
        y += 6
        y = draw_lines(draw, 0, y, wrap(draw, subtitle, font("SemiBold", 28), width), font("SemiBold", 28), MUTED, 34)
    y += 28
    draw.rectangle((0, y, 56, y + 4), fill=ORANGE)
    caption_top = y + 44

    footer = wrap(draw, disclosure, font("SemiBold", 20), width)
    note_lines = wrap(draw, note, font("SemiBold", 20), width) if note else []
    note_height = 24 * len(note_lines) + 14 if note_lines else 0
    footer_top = height - 26 - note_height - 24 * (len(footer) + 1)
    y = footer_top
    if note_lines:
        y = draw_lines(draw, 0, y, note_lines, font("SemiBold", 20), SOFT, 24) + 14
    footer_y = draw_lines(draw, 0, y, footer, font("SemiBold", 20), MUTED, 24)
    draw.text((0, footer_y + 2), REPOSITORY, font=font("SemiBold", 20), fill=ORANGE)
    return layer, caption_top, footer_top - 24


def render_caption(span: Span, top: int, limit: int):
    """A caption layer. A caption that would reach below `limit` is an error: shorten it in the
    scene rather than let it run into the footer."""
    from PIL import Image, ImageDraw

    _, _, width, height = RAIL
    layer = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    y = top
    if span.heading:
        y = draw_lines(draw, 0, y, wrap(draw, span.heading, font("Bold", 48), width), font("Bold", 48), ORANGE, 52)
        y += 10
    if span.body:
        y = draw_lines(draw, 0, y, wrap(draw, span.body, font("SemiBold", 34), width), font("SemiBold", 34), SOFT, 42)
    if y > limit:
        raise ValueError(f"caption '{span.heading}' is too long for the rail: it needs {y - top}px and has {limit - top}px")
    return layer


def with_alpha(layer, alpha: float):
    if alpha >= 1.0:
        return layer
    faded = layer.copy()
    faded.putalpha(layer.getchannel("A").point(lambda v: int(v * alpha)))
    return faded


def render_card(number: int | None, title: str, subtitle: str, lines: list[str] | None = None, disclosure: str = DISCLOSURE):
    """A full-frame card: the lockup, then the chapter number (if any), title, subtitle and, on
    the opening and closing cards, a few lines of plain text."""
    from PIL import Image, ImageDraw

    width, height = CANVAS
    image = Image.new("RGB", CANVAS, INK)
    draw = ImageDraw.Draw(image)
    mark = fitted(brand("kajo-lockup-stacked.png"), 420)
    image.paste(mark, ((width - mark.width) // 2, 150), mark)
    y = 150 + mark.height + 70
    if number is not None:
        label = f"CHAPTER {number:02d}"
        face = font("Bold", 28)
        total = sum(draw.textlength(c, font=face) + 6 for c in label) - 6
        spaced(draw, ((width - total) / 2, y), label, face, ORANGE, 6)
        y += 56
    y = draw_lines(draw, width // 2, y, wrap(draw, title, font("Bold", 104), 1500), font("Bold", 104), WHITE, 108, "ma")
    if subtitle:
        y += 16
        y = draw_lines(draw, width // 2, y, wrap(draw, subtitle, font("SemiBold", 40), 1300), font("SemiBold", 40), MUTED, 48, "ma")
    if lines:
        y += 36
        for line in lines:
            colour = ORANGE if line.startswith(REPOSITORY) else SOFT
            y = draw_lines(draw, width // 2, y, wrap(draw, line, font("SemiBold", 36), 1300), font("SemiBold", 36), colour, 46, "ma")
        if y > height - 110:
            raise ValueError(f"the lines of the '{title}' card are too long: they reach {y}px of {height - 110}px")
    draw.text((width // 2, height - 70), disclosure, font=font("SemiBold", 24), fill=MUTED, anchor="ma")
    return image


# ------------------------------------------------------------------------------ compose


def write_rail(work: Path, recording: Recording, number: int | None) -> Path:
    """Render the rail as a list of PNGs held for exact frame counts."""
    from PIL import Image

    events = recording.events
    spans = caption_spans(events.get("captions", []), recording.frames)
    static, caption_top, caption_limit = render_rail_static(
        number, events.get("title", ""), events.get("subtitle", ""), events.get("note", ""),
        events.get("disclosure", DISCLOSURE),
    )
    layers = [render_caption(span, caption_top, caption_limit) for span in spans]

    work.mkdir(parents=True, exist_ok=True)
    entries: list[tuple[str, int]] = []
    cache: dict[tuple[int | None, float], str] = {}
    for first, count, index, alpha in rail_timeline(spans, recording.frames):
        key = (index, alpha)
        if key not in cache:
            image = static if index is None else Image.alpha_composite(static, with_alpha(layers[index], alpha))
            name = f"rail_{len(cache):04d}.png"
            image.save(work / name, compress_level=1)
            cache[key] = name
        entries.append((cache[key], count))
    listing = work / "rail.ffconcat"
    listing.write_text(ffconcat_text(entries, FPS), encoding="utf-8")
    return listing


def compose_chapter(ffmpeg: str, recording: Recording, number: int | None, out: Path, work: Path, crf: int, preset: str) -> Path:
    events = recording.events
    title, subtitle = events.get("title", ""), events.get("subtitle", "")
    work.mkdir(parents=True, exist_ok=True)

    background = work / "background.png"
    render_background().save(background)
    card = work / "card.png"
    render_card(number, title, subtitle, None, events.get("disclosure", DISCLOSURE)).save(card)
    rail = write_rail(work, recording, number)

    target = out / f"{recording.name}.mp4"
    command = [
        ffmpeg, "-y", "-loglevel", "error", "-stats",
        "-loop", "1", "-framerate", str(FPS), "-t", str(CARD_SECONDS), "-i", card,
        "-loop", "1", "-framerate", str(FPS), "-i", background,
        "-i", recording.screen,
        "-f", "concat", "-safe", "0", "-i", rail,
        "-filter_complex", compose_graph(recording.seconds, with_card=True),
        "-map", "[v]", *x264_arguments(crf, preset), target,
    ]  # fmt: skip
    run(command)
    return target


def make_gif(ffmpeg: str, recording: Recording, out: Path, fps: int, scale: int) -> Path:
    target = out / f"{recording.name}.gif"
    run([ffmpeg, "-y", "-loglevel", "error", "-i", recording.screen, "-filter_complex", gif_filter(fps, scale), "-loop", "0", target])
    print(f"  {target.name}: {target.stat().st_size / 1e6:.1f} MB", flush=True)
    return target


def make_sheet(ffmpeg: str, recording: Recording, out: Path, columns: int = 5, tiles: int = 20) -> Path:
    """One frame every few seconds, for judging a scene without playing it."""
    interval = max(1, math.ceil(recording.seconds / tiles))
    count = math.ceil(recording.seconds / interval)
    rows = math.ceil(count / columns)
    target = out / f"{recording.name}.sheet.png"
    video_filter = (
        f"fps=1/{interval},scale={DISPLAY[0]}:{DISPLAY[1]}:flags=area,"
        f"tile={columns}x{rows}:padding=6:margin=6:color=0x0b0c0d"
    )
    run([ffmpeg, "-y", "-loglevel", "error", "-i", recording.screen, "-vf", video_filter, "-frames:v", "1", target])
    print(f"  {target.name}: one frame every {interval} s", flush=True)
    return target


def join_chapters(ffmpeg: str, clips: list[Path], target: Path, marks: list[tuple[str, float]] | None = None) -> Path:
    """Join the clips without re-encoding. With marks (title, seconds) the file carries MP4 chapters
    and a `<name>.chapters.txt` list is written beside it for a video site's description."""
    listing = target.with_suffix(".txt")
    listing.write_text("".join(f"file '{path.resolve().as_posix()}'\n" for path in clips), encoding="utf-8")
    command = [ffmpeg, "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", listing]
    metadata = target.with_suffix(".ffmetadata")
    if marks:
        metadata.write_text(chapter_metadata(marks), encoding="utf-8")
        command += ["-i", metadata, "-map", "0", "-map_metadata", "1", "-map_chapters", "1"]
        target.with_suffix(".chapters.txt").write_text(chapter_list(marks), encoding="utf-8")
    run(command + ["-c", "copy", "-movflags", "+faststart", target])
    listing.unlink()
    metadata.unlink(missing_ok=True)
    return target


# ------------------------------------------------------------------------------ command line


def parse_arguments(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("scenes", nargs="*", help="scene names from tools/demo/scenes (default: the whole cut)")
    parser.add_argument("--out", type=Path, default=OUT, help="output folder (default: dist/demo)")
    parser.add_argument("--no-build", action="store_true", help="use the recorder as built, without running CMake")
    parser.add_argument("--screen-only", action="store_true", help="record the screen layer only; skip the card, rail and H.264")
    parser.add_argument("--gif", action="store_true", help="also write a README GIF per chapter")
    parser.add_argument("--gif-fps", type=int, default=20, help="GIF frame rate (default 20)")
    parser.add_argument("--gif-scale", type=int, default=2, choices=range(1, 5), help="GIF size as a multiple of 320x240 (default 2)")
    parser.add_argument("--sheet", action="store_true", help="also write a contact sheet per chapter")
    parser.add_argument("--no-cut", action="store_true", help="do not join the chapters into demo.mp4")
    parser.add_argument("--crf", type=int, default=14, help="x264 quality, lower is better (default 14)")
    parser.add_argument("--speed", type=int, default=SPEED, choices=range(1, 11), help=f"play the firmware chapters this many times faster than real time (default {SPEED})")
    parser.add_argument("--pace", type=int, default=PACE_MS, help=f"longest a settle or wait lasts in video milliseconds, 0 to play them as written (default {PACE_MS})")
    parser.add_argument("--min-caption", type=int, default=MIN_CAPTION_MS, help=f"video milliseconds a caption stays on the rail (default {MIN_CAPTION_MS})")
    parser.add_argument("--quick", action="store_true", help="fast, larger encode for checking a draft")
    parser.add_argument("--keep-work", action="store_true", help="keep the rail and card images")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_arguments(sys.argv[1:] if argv is None else argv)
    try:
        ffmpeg = find_ffmpeg()
        require_encoders(ffmpeg, ("libx264", "libx264rgb"))
        cut = load_cut()
        names = args.scenes or cut
        for name in names:
            check_chapter(name)
        cards = load_cards()
        needs_recorder = any(story_path(name) is None for name in names)
        recorder = None
        if needs_recorder:
            recorder = recorder_executable() if args.no_build else build_recorder()
            if recorder is None:
                raise FileNotFoundError("The demo recorder is not built; run without --no-build")

        out: Path = args.out
        out.mkdir(parents=True, exist_ok=True)
        preset, crf = ("veryfast", max(args.crf, 20)) if args.quick else ("slow", args.crf)
        chapters: list[Path] = []
        marks: list[tuple[str, float]] = []
        for name in names:
            print(f"\n== {name}", flush=True)
            if story_path(name) is not None:
                recording = record_story(ffmpeg, name, out)
            else:
                recording = record(recorder, ffmpeg, scene_path(name), out, args.speed, args.min_caption, args.pace)
            print(f"  {recording.frames} frames, {recording.seconds:.1f} s", flush=True)
            if args.sheet:
                make_sheet(ffmpeg, recording, out)
            if args.gif:
                make_gif(ffmpeg, recording, out, args.gif_fps, args.gif_scale)
            if args.screen_only:
                continue
            number = cut.index(name) + 1 if name in cut else None
            work = out / "work" / name
            chapters.append(compose_chapter(ffmpeg, recording, number, out, work, crf, preset))
            marks.append((recording.events.get("title") or name, CARD_SECONDS + recording.seconds))
            if not args.keep_work:
                shutil.rmtree(work, ignore_errors=True)
            print(f"  {chapters[-1].name}: {chapters[-1].stat().st_size / 1e6:.1f} MB", flush=True)

        if chapters and not args.scenes and not args.no_cut and len(chapters) > 1:
            print("\n== cut", flush=True)
            parts, titles = list(chapters), list(marks)
            for key in ("intro", "outro"):
                if key not in cards:
                    continue
                card = cards[key]
                seconds = float(card.get("seconds", 4.0))
                clip = make_card_clip(
                    ffmpeg, render_card(None, card["title"], card.get("subtitle", ""), card.get("lines")),
                    seconds, out / f"{key}.mp4", crf, preset,
                )  # fmt: skip
                position = 0 if key == "intro" else len(parts)
                parts.insert(position, clip)
                titles.insert(position, (card.get("mark", card["title"]), seconds))
            final = join_chapters(ffmpeg, parts, out / "demo.mp4", titles)
            print(f"  {final.name}: {final.stat().st_size / 1e6:.1f} MB", flush=True)
        if not args.keep_work:
            shutil.rmtree(out / "work", ignore_errors=True)
        return 0
    except (FileNotFoundError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
