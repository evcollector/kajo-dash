#!/usr/bin/env python3
"""Local server for the CYD layout editor.

Serves the project folder (so the editor can fetch layout.json and the preview
PNGs) and exposes local-only save endpoints for the layout and shared 16x16
icons. Icon saves update both the canonical source PNG and the normalized RGBA
mask, then regenerate the firmware's packed 1-bit icon data.

Launched by scripts/run_layout_editor_lvgl.bat, or run manually:
    py -3 tools/serve_editor.py [port]
"""
from __future__ import annotations

import json
import io
import subprocess
import sys
import threading
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from PIL import Image

from convert_icons_1bit import ICON_FILES as FIRMWARE_ICON_FILES

ROOT = Path(__file__).resolve().parents[1]
LAYOUT = ROOT / "tools" / "layout.json"
ICON_DIR = ROOT / "preview_output" / "icons16"
ICON_SOURCE_DIR = ROOT / "preview_output" / "new_icons" / "LatestIcons_25.6"
ICON_PACKER = ROOT / "tools" / "convert_icons_1bit.py"
RUN_PREVIEW_LVGL = ROOT / "scripts" / "run_preview_lvgl.bat"
UPLOAD_LVGL = ROOT / "upload_lvgl.bat"
DEFAULT_PORT = 8765
ICON_SIZE = 16
MAX_ICON_REQUEST_BYTES = 16 * 1024
ICON_SAVE_LOCK = threading.Lock()
FIRMWARE_ICON_FILENAMES = frozenset(filename for _, filename in FIRMWARE_ICON_FILES)
ICON_SOURCE_PATHS = {
    f"{path.stem.removeprefix('cyd_icon_').rstrip('_')}.png": path
    for path in ICON_SOURCE_DIR.glob("cyd_icon_*.png")
}


def format_layout(data: dict) -> str:
    """Pretty-print layout.json with one element/shape per line.

    Keeps the file readable and produces minimal git diffs: editing one element
    changes exactly one line. Always emits LF newlines.
    """
    dj = lambda v: json.dumps(v, ensure_ascii=False)  # noqa: E731
    out = ["{"]
    if "_comment" in data:
        out.append(f'  "_comment": {dj(data["_comment"])},')
    out.append('  "screens": {')
    screens = list(data["screens"].items())
    for si, (sid, s) in enumerate(screens):
        out.append(f"    {dj(sid)}: {{")
        for k in (k for k in s if k not in ("elements", "items")):
            out.append(f"      {dj(k)}: {dj(s[k])},")
        if "items" in s:
            out.append('      "items": [')
            for ii, it in enumerate(s["items"]):
                out.append(f"        {dj(it)}{',' if ii < len(s['items']) - 1 else ''}")
            out.append("      ]")
        else:
            els = list(s["elements"].items())
            out.append('      "elements": {')
            for ei, (eid, e) in enumerate(els):
                out.append(f"        {dj(eid)}: {dj(e)}{',' if ei < len(els) - 1 else ''}")
            out.append("      }")
        out.append("    }" + ("," if si < len(screens) - 1 else ""))
    out.append("  }")
    out.append("}")
    return "\n".join(out) + "\n"


# The POST endpoints below write layout.json and launch a build-and-flash, so
# reaching them must require actually having the editor open -- not merely
# having this server running. A page on any site the user visits can issue a
# cross-origin POST to 127.0.0.1 without a preflight, and can point a hostname
# it controls at 127.0.0.1 to defeat the loopback bind (DNS rebinding). The
# Origin check stops the first, the Host check the second.
def allowed_hosts(port: int) -> frozenset[str]:
    names = ("localhost", "127.0.0.1", "[::1]")
    return frozenset(f"{name}:{port}" for name in names) | frozenset(names)


def allowed_origins(port: int) -> frozenset[str]:
    return frozenset(f"http://{name}:{port}" for name in ("localhost", "127.0.0.1", "[::1]"))


class Handler(SimpleHTTPRequestHandler):
    port = DEFAULT_PORT

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT), **kwargs)

    def host_is_local(self) -> bool:
        return self.headers.get("Host", "").lower() in allowed_hosts(self.port)

    def origin_is_local(self) -> bool:
        # fetch() sets Origin on every non-GET request, so a missing one here is
        # not the editor and gets no benefit of the doubt.
        return self.headers.get("Origin", "") in allowed_origins(self.port)

    def send_head(self):
        if not self.host_is_local():
            self.send_error(421, "Misdirected Request")
            return None
        # The served root is the whole project, so keep .git and other dotfiles
        # out of it rather than relying on nobody asking.
        if any(part.startswith(".") for part in self.path.split("?")[0].split("/") if part):
            self.send_error(404, "Not Found")
            return None
        return super().send_head()

    def end_headers(self) -> None:
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")
        super().end_headers()

    def _json(self, code: int, payload: dict) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self) -> None:
        if not self.host_is_local():
            self._json(421, {"ok": False, "error": "unexpected Host header"})
            return
        if not self.origin_is_local():
            self._json(403, {"ok": False, "error": "cross-origin request refused"})
            return
        path = self.path.rstrip("/")
        if path == "/api/run-preview-lvgl":
            self.run_preview(RUN_PREVIEW_LVGL, ROOT / "preview_output" / "lvgl" / "contact_sheet.png")
            return
        if path == "/api/upload-lvgl":
            self.upload_lvgl()
            return
        if path == "/api/save-icon":
            self.save_icon()
            return
        if path != "/save-layout":
            self._json(404, {"ok": False, "error": "unknown endpoint"})
            return
        try:
            length = int(self.headers.get("Content-Length", 0))
            raw = self.rfile.read(length).decode("utf-8")
            data = json.loads(raw)  # validate it parses
            if "screens" not in data:
                raise ValueError("not a layout file (missing 'screens')")
        except Exception as exc:  # noqa: BLE001 - report any parse/validation error
            self._json(400, {"ok": False, "error": str(exc)})
            return
        # Canonicalise formatting and force LF (write_bytes avoids the CRLF
        # translation that text mode would apply on Windows).
        text = format_layout(data)
        LAYOUT.write_bytes(text.encode("utf-8"))
        print(f"Saved layout.json ({len(text)} bytes)")
        self._json(200, {"ok": True, "path": str(LAYOUT)})

    def save_icon(self) -> None:
        try:
            length = int(self.headers.get("Content-Length", 0))
            if length <= 0 or length > MAX_ICON_REQUEST_BYTES:
                raise ValueError("invalid icon request size")
            data = json.loads(self.rfile.read(length).decode("utf-8"))
            filename = data.get("filename")
            pixels = data.get("pixels")
            if filename not in FIRMWARE_ICON_FILENAMES:
                raise ValueError("unknown firmware icon")
            if not isinstance(pixels, list) or len(pixels) != ICON_SIZE * ICON_SIZE:
                raise ValueError("pixels must contain exactly 256 binary values")
            if any(not isinstance(value, (bool, int)) or int(value) not in (0, 1) for value in pixels):
                raise ValueError("icon pixels must be 0 or 1")
            source_path = ICON_SOURCE_PATHS.get(filename)
            if source_path is None:
                raise ValueError("canonical icon source is missing")
        except Exception as exc:  # noqa: BLE001 - return validation details to the local editor
            self._json(400, {"ok": False, "error": str(exc)})
            return

        coverage = Image.new("L", (ICON_SIZE, ICON_SIZE), 0)
        coverage.putdata([255 if int(value) else 0 for value in pixels])
        glyph = Image.new("RGBA", (ICON_SIZE, ICON_SIZE), (255, 255, 255, 0))
        glyph.putalpha(coverage)
        output_path = ICON_DIR / filename

        def png_bytes(image: Image.Image) -> bytes:
            buffer = io.BytesIO()
            image.save(buffer, format="PNG", optimize=True)
            return buffer.getvalue()

        def replace_bytes(path: Path, content: bytes) -> None:
            temporary = path.with_name(path.name + ".tmp")
            temporary.write_bytes(content)
            temporary.replace(path)

        with ICON_SAVE_LOCK:
            previous_source = source_path.read_bytes()
            previous_output = output_path.read_bytes()
            try:
                replace_bytes(source_path, png_bytes(coverage))
                replace_bytes(output_path, png_bytes(glyph))
                result = subprocess.run(
                    [sys.executable, str(ICON_PACKER)],
                    cwd=str(ROOT),
                    capture_output=True,
                    text=True,
                    timeout=30,
                    check=False,
                )
                if result.returncode != 0:
                    raise RuntimeError(result.stderr.strip() or result.stdout.strip() or "icon packer failed")
            except Exception as exc:  # noqa: BLE001 - restore both assets if generation fails
                replace_bytes(source_path, previous_source)
                replace_bytes(output_path, previous_output)
                subprocess.run(
                    [sys.executable, str(ICON_PACKER)], cwd=str(ROOT), capture_output=True, timeout=30, check=False
                )
                self._json(500, {"ok": False, "error": str(exc)})
                return

        shared_ids = [name for name, mapped_filename in FIRMWARE_ICON_FILES if mapped_filename == filename]
        print(f"Saved {filename}; regenerated packed 1-bit firmware icons")
        self._json(200, {
            "ok": True,
            "filename": filename,
            "source": str(source_path),
            "output": str(output_path),
            "sharedIds": shared_ids,
        })

    def run_preview(self, script: Path, contact_sheet: Path) -> None:
        if not script.exists():
            self._json(404, {"ok": False, "error": f"missing {script}"})
            return
        try:
            result = subprocess.run(
                ["cmd", "/c", str(script)],
                cwd=str(ROOT),
                capture_output=True,
                text=True,
                timeout=120,
                check=False,
            )
        except Exception as exc:  # noqa: BLE001 - surface process launch/timeout errors to the editor
            self._json(500, {"ok": False, "error": str(exc)})
            return
        if result.returncode != 0:
            self._json(500, {
                "ok": False,
                "error": f"{script.name} exited with {result.returncode}",
                "stdout": result.stdout[-4000:],
                "stderr": result.stderr[-4000:],
            })
            return
        print(f"Regenerated preview images with {script.name}")
        self._json(200, {
            "ok": True,
            "stdout": result.stdout[-4000:],
            "contactSheet": str(contact_sheet),
        })

    def upload_lvgl(self) -> None:
        if not UPLOAD_LVGL.exists():
            self._json(404, {"ok": False, "error": f"missing {UPLOAD_LVGL}"})
            return
        try:
            subprocess.Popen(
                ["cmd", "/c", "start", "CYD LVGL Upload", str(UPLOAD_LVGL)],
                cwd=str(ROOT),
                shell=False,
            )
        except Exception as exc:  # noqa: BLE001 - report process launch errors to the editor
            self._json(500, {"ok": False, "error": str(exc)})
            return
        print("Started upload_lvgl.bat")
        self._json(200, {"ok": True, "script": str(UPLOAD_LVGL)})

    def log_message(self, fmt, *args):  # keep the console quiet except for POSTs
        if getattr(self, "command", "") == "POST":
            super().log_message(fmt, *args)


def main() -> None:
    port = int(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_PORT
    Handler.port = port
    httpd = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    print(f"KAJO layout editor: http://localhost:{port}/tools/layout_editor.html?lvgl=1")
    print(f"Save endpoint writes -> {LAYOUT}")
    print(f"Icon editor writes -> {ICON_DIR}")
    print("Close this window (or press Ctrl+C) to stop.")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
