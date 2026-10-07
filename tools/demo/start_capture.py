"""The `start` chapter: how a new contributor gets going, as a speed edit.

Not firmware footage, so it is not a scene. Two things are shown, at the 1280x960 the simulator
chapters use for the display, and handed to make_demo_video.py like any other recording:

* the `kajo.bat` menu, REDRAWN here: its entries are read from kajo.bat, its help column from
  scripts/menu_help.txt and its colours and layout from scripts/menu.ps1, but it is a picture of the
  menu, not a screen recording of a console. The chapter's note says so.
* the real tools/layout_editor.html in the installed Chrome, driven by Playwright. Nothing is saved:
  the editor is only ever dragged, undone and switched between screens.

Needs `pip install playwright` and Google Chrome (Playwright drives the installed Chrome; it
downloads nothing). Everything is paced in output frames, so the same script gives the same video.
"""

from __future__ import annotations

import http.server
import io
import json
import re
import subprocess
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FRAME = (1280, 960)

TITLE = "Get started"
SUBTITLE = "From a fresh clone to the simulator, the layout editor and a flashed board."
NOTE = "The menu is redrawn from kajo.bat. The layout editor is the real page, in Chrome."
DISCLOSURE = "Captured from the repository's own tools, not the firmware simulator."

# Every caption of the chapter, so a test can check that each fits the rail.
CAPTIONS = {
    "menu": ("One menu", "Clone the repository and run kajo.bat."),
    "simulator": ("Simulator", "The real firmware UI on your PC. No board needed."),
    "previews": ("Preview renders", "Every screen, as 320x240 images."),
    "layout": ("Layout editor", "Edit tools/layout.json."),
    "editor": ("The layout editor", "Edit any screen at 1:1 and see the result."),
    "pick": ("Pick an element", "Click it, then drag."),
    "undo": ("Undo is one click", ""),
    "screens": ("All twelve themes", "One list. The last two are view only."),
    "flash": ("Flash over USB", "Build and install on a connected display."),
}

CONSOLE_BACKGROUND = (12, 12, 12)
CONSOLE_TEXT = (204, 204, 204)
CONSOLE_DARK_GRAY = (118, 118, 118)
CONSOLE_DARK_YELLOW = (193, 156, 0)
CONSOLE_PRESSED = (249, 241, 165)
TITLE_BAR = (31, 31, 31)

LABEL_WIDTH = 22  # scripts/menu.ps1: $LabelWidth


# ------------------------------------------------------------------------------ the menu


def menu_entries(bat: Path = ROOT / "kajo.bat") -> list[str]:
    """The development menu's entries, as kajo.bat passes them to scripts/menu.ps1.

    The companion-app entry is left out: it only exists with the private checkout, which someone
    starting from a clone does not have.
    """
    text = bat.read_text(encoding="utf-8", errors="replace")
    entries: list[str] = []
    for line in text.splitlines():
        if not re.match(r"\s*set\s+\"?MENU_ENTRIES=", line, re.IGNORECASE):
            continue
        entries += re.findall(r'"([^"]*)"', line.split("MENU_ENTRIES=", 1)[1])
    return entries


def firmware_header() -> tuple[str, str, str]:
    """Version name, version code and last release, read the way kajo.bat reads them."""
    config = (ROOT / "include" / "config.h").read_text(encoding="utf-8", errors="replace")
    name = re.search(r"#define\s+CYD_FIRMWARE_VERSION_NAME\s+\"?([^\s\"]+)", config)
    code = re.search(r"#define\s+CYD_FIRMWARE_VERSION_CODE\s+(\d+)", config)
    last = "none yet"
    releases = ROOT / "RELEASES.md"
    if releases.exists():
        for line in releases.read_text(encoding="utf-8", errors="replace").splitlines():
            row = re.match(r"^\|\s*([0-9][0-9.]*)\s*\|\s*([0-9]+)\s*\|\s*([^|]*?)\s*\|", line)
            if row:
                last = f"{row.group(1)} (version code {row.group(2)}, {row.group(3)})"
    return (name.group(1) if name else "?", code.group(1) if code else "?", last)


def help_pages(path: Path = ROOT / "scripts" / "menu_help.txt") -> set[str]:
    return set(re.findall(r"^\[(\w)\]", path.read_text(encoding="utf-8", errors="replace"), re.MULTILINE))


def menu_model() -> tuple[list[dict], list[dict]]:
    """Display lines and the selectable items, built as scripts/menu.ps1 builds them."""
    name, code, last = firmware_header()
    pages = help_pages()
    lines: list[dict] = []
    items: list[dict] = []
    for raw in menu_entries():
        entry = raw.replace("%FW_NAME%", name).replace("%FW_CODE%", code).replace("%LAST_RELEASE%", last)
        if entry.startswith("="):
            lines.append({"kind": "header", "text": entry[1:]})
        elif entry.startswith("#"):
            lines.append({"kind": "blank"})
            lines.append({"kind": "section", "text": entry[1:]})
        else:
            key, label, description = entry.split("|", 2)
            item = {"kind": "item", "key": key, "help": key in pages, "text": f"{key}. {label}".ljust(LABEL_WIDTH + 3) + description}
            lines.append(item)
            items.append(item)
    quit_item = {"kind": "item", "key": "Q", "help": False, "text": "Q. Quit"}
    lines += [{"kind": "blank"}, quit_item]
    items.append(quit_item)
    return lines, items


def console_font(names: tuple[str, ...], size: int):
    """The first installed font of a list: Windows' console faces, then common Linux ones."""
    from PIL import ImageFont

    for name in names:
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    raise FileNotFoundError(f"none of these fonts is installed for the kajo.bat menu: {', '.join(names)}")


MONOSPACE = ("consola.ttf", "DejaVuSansMono.ttf", "LiberationMono-Regular.ttf")
SANS = ("segoeui.ttf", "DejaVuSans.ttf", "LiberationSans-Regular.ttf")


def render_menu(selected: str, pressed: bool = False):
    from PIL import Image, ImageDraw

    lines, items = menu_model()
    bar_width = max(len(item["text"]) for item in items) + 2
    longest = 4 + bar_width + 4
    size = 24
    while size > 12:
        face = console_font(MONOSPACE, size)
        if face.getlength("M") * longest <= FRAME[0] - 80:
            break
        size -= 1
    face = console_font(MONOSPACE, size)
    cell = face.getlength("M")
    leading = round(size * 1.32)

    image = Image.new("RGB", FRAME, CONSOLE_BACKGROUND)
    draw = ImageDraw.Draw(image)
    bar = 52
    draw.rectangle((0, 0, FRAME[0], bar), fill=TITLE_BAR)
    draw.text((24, bar // 2), "KAJO-Dash", font=console_font(SANS, 22), fill=CONSOLE_TEXT, anchor="lm")

    def put(column: float, row: int, text: str, fill, background=None) -> None:
        x, y = 24 + column * cell, bar + 24 + row * leading
        if background is not None:
            draw.rectangle((x, y, x + len(text) * cell, y + leading), fill=background)
        draw.text((x, y), text, font=face, fill=fill)

    row = 1
    header_done = False
    for line in lines:
        if line["kind"] != "header" and not header_done:
            put(0, row, "  ==========================================", CONSOLE_TEXT)
            row += 1
            header_done = True
        kind = line["kind"]
        if kind == "header":
            put(0, row, "  " + line["text"], CONSOLE_TEXT)
        elif kind == "section":
            put(0, row, "  " + line["text"], CONSOLE_DARK_YELLOW)
        elif kind == "item":
            current = line["key"] == selected
            if current:
                put(0, row, "  > ", CONSOLE_DARK_YELLOW)
                put(4, row, line["text"].ljust(bar_width), (0, 0, 0), CONSOLE_PRESSED if pressed else CONSOLE_DARK_YELLOW)
            else:
                put(0, row, "    " + line["text"].ljust(bar_width), CONSOLE_TEXT)
            if line["help"]:
                put(4 + bar_width + 1, row, "[?]", CONSOLE_DARK_GRAY)
        row += 1
    row += 1
    put(0, row, "  Up/Down or W/S to move, Right or D for help, Enter or a number to choose,", CONSOLE_DARK_GRAY)
    put(0, row + 1, "  Q to quit", CONSOLE_DARK_GRAY)
    return image


# ------------------------------------------------------------------------------ the timeline


class Writer:
    """Raw frames in, lossless H.264 out; the same encoder settings as the simulator chapters."""

    def __init__(self, ffmpeg: str, target: Path, fps: int) -> None:
        self.frames = 0
        self.process = subprocess.Popen(
            [
                ffmpeg, "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
                "-s", f"{FRAME[0]}x{FRAME[1]}", "-framerate", str(fps), "-i", "pipe:0",
                "-c:v", "libx264rgb", "-qp", "0", "-preset", "superfast", "-g", "120", str(target),
            ],
            stdin=subprocess.PIPE,
        )  # fmt: skip

    def add(self, image, count: int = 1) -> None:
        raw = image.convert("RGB").tobytes()
        for _ in range(count):
            self.process.stdin.write(raw)
        self.frames += count

    def close(self) -> None:
        self.process.stdin.close()
        if self.process.wait() != 0:
            raise RuntimeError("ffmpeg could not store the frames of the start chapter")


def frames_for(seconds: float, fps: int) -> int:
    return max(1, round(seconds * fps))


class Captions:
    def __init__(self) -> None:
        self.events: list[dict] = []

    def at(self, frame: int, heading: str, body: str = "") -> None:
        self.events.append({"frame": frame, "heading": heading, "body": body})


def menu_part(writer: Writer, captions: Captions, fps: int, steps: list[tuple[str, float, tuple[str, str] | None]], press: bool) -> None:
    """Hold the menu on each key for its time; the last key is pressed on the way out."""
    for index, (key, seconds, caption) in enumerate(steps):
        if caption:
            captions.at(writer.frames, *caption)
        last = press and index == len(steps) - 1
        hold = frames_for(seconds, fps)
        if last:
            pressed = max(1, round(0.18 * fps))
            writer.add(render_menu(key), hold - pressed)
            writer.add(render_menu(key, pressed=True), pressed)
        else:
            writer.add(render_menu(key), hold)


# ------------------------------------------------------------------------------ the layout editor

CURSOR_SCRIPT = """
(() => {
  const ring = document.createElement('div');
  ring.id = 'demo-cursor';
  ring.style.cssText = 'position:fixed;left:0;top:0;width:34px;height:34px;margin:-17px 0 0 -17px;border-radius:50%;'
    + 'border:3px solid #ff7900;background:rgba(255,121,0,.18);pointer-events:none;z-index:2147483647;'
    + 'transition:none;transform:scale(1)';
  document.body.appendChild(ring);
  window.__demoCursor = (x, y, down) => {
    ring.style.left = x + 'px'; ring.style.top = y + 'px';
    ring.style.transform = down ? 'scale(.7)' : 'scale(1)';
  };
})();
"""


class Serve:
    """The editor's own page, read-only: no save endpoint, so nothing can be written by accident."""

    def __init__(self) -> None:
        root = str(ROOT)

        class Handler(http.server.SimpleHTTPRequestHandler):
            def __init__(self, *args, **kwargs):
                super().__init__(*args, directory=root, **kwargs)

            def do_POST(self):  # the editor's save and upload endpoints are not served
                self.send_error(405, "read-only demo server")

            def log_message(self, *args):
                pass

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

    def __enter__(self) -> str:
        self.thread.start()
        return f"http://127.0.0.1:{self.server.server_address[1]}"

    def __exit__(self, *exc) -> None:
        self.server.shutdown()
        self.server.server_close()


def editor_part(writer: Writer, captions: Captions, fps: int) -> None:
    from PIL import Image

    try:
        from playwright.sync_api import sync_playwright
    except ImportError as error:
        raise FileNotFoundError("the start chapter drives Chrome through Playwright: pip install playwright") from error

    position = [640.0, 520.0]
    down = [False]

    with Serve() as base, sync_playwright() as playwright:
        try:
            browser = playwright.chromium.launch(channel="chrome", headless=True)
        except Exception as error:  # Playwright's own message names what is missing
            raise FileNotFoundError(f"could not start Google Chrome for the start chapter: {error}") from error
        page = browser.new_page(viewport={"width": FRAME[0], "height": FRAME[1]})
        page.goto(f"{base}/tools/layout_editor.html")
        page.wait_for_function("document.querySelectorAll('#screenSelect option').length > 1")
        page.wait_for_timeout(1500)
        page.evaluate("window.scrollTo(0, 0); document.getElementById('screen').scrollIntoView({block:'center'}); window.scrollBy(0, -8)")
        page.add_script_tag(content=CURSOR_SCRIPT)
        page.wait_for_timeout(300)

        def shot() -> Image.Image:
            page.evaluate("([x, y, d]) => window.__demoCursor(x, y, d)", [position[0], position[1], down[0]])
            return Image.open(io.BytesIO(page.screenshot(type="png"))).convert("RGB")

        def hold(seconds: float) -> None:
            writer.add(shot(), frames_for(seconds, fps))

        def move(x: float, y: float, seconds: float) -> None:
            """Glide the pointer, a screenshot every other frame."""
            steps = max(1, frames_for(seconds, fps) // 2)
            x0, y0 = position
            for step in range(1, steps + 1):
                t = step / steps
                t = t * t * (3.0 - 2.0 * t)
                position[0], position[1] = x0 + (x - x0) * t, y0 + (y - y0) * t
                page.mouse.move(position[0], position[1])
                writer.add(shot(), 2)

        captions.at(writer.frames, *CAPTIONS["editor"])
        hold(1.2)
        move(362, 352, 0.6)
        captions.at(writer.frames, *CAPTIONS["pick"])
        page.mouse.down()
        down[0] = True
        hold(0.5)
        move(470, 420, 0.7)
        page.mouse.up()
        down[0] = False
        hold(0.7)
        captions.at(writer.frames, *CAPTIONS["undo"])
        move(188, 944, 0.6)
        page.mouse.click(188, 944)
        down[0] = True
        hold(0.15)
        down[0] = False
        hold(0.7)
        captions.at(writer.frames, *CAPTIONS["screens"])
        move(640, 560, 0.4)
        for index in (1, 4, 10, 11):
            page.select_option("#screenSelect", index=index)
            page.evaluate("document.getElementById('screen').scrollIntoView({block:'center'}); window.scrollBy(0, -8)")
            page.wait_for_timeout(250)
            hold(0.9)
        browser.close()


# ------------------------------------------------------------------------------ entry point


def capture(out: Path, ffmpeg: str, fps: int) -> tuple[Path, dict]:
    """Write start.screen.mkv and return it with the caption events the composer expects."""
    out.mkdir(parents=True, exist_ok=True)
    screen = out / "start.screen.mkv"
    captions = Captions()
    writer = Writer(ffmpeg, screen, fps)
    try:
        captions.at(0, *CAPTIONS["menu"])
        menu_part(
            writer, captions, fps,
            [
                ("1", 2.4, CAPTIONS["simulator"]),
                ("2", 1.5, CAPTIONS["previews"]),
                ("3", 1.8, CAPTIONS["layout"]),
            ],
            press=True,
        )  # fmt: skip
        editor_part(writer, captions, fps)
        menu_part(
            writer, captions, fps,
            [("4", 3.0, CAPTIONS["flash"])],
            press=True,
        )  # fmt: skip
    except BaseException:
        writer.process.kill()
        screen.unlink(missing_ok=True)
        raise
    writer.close()
    events = {
        "frames": writer.frames,
        "fps": fps,
        "title": TITLE,
        "subtitle": SUBTITLE,
        "note": NOTE,
        "disclosure": DISCLOSURE,
        "captions": captions.events,
    }
    (out / "start.events.json").write_text(json.dumps(events, indent=2), encoding="utf-8")
    return screen, events
