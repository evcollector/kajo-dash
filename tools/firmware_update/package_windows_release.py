from __future__ import annotations

import argparse
import zipfile
from pathlib import Path

from protocol import load_release_manifest
from release_notices import release_notice_files
from usb_flash import load_usb_layout, usb_layout_path


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EXE = ROOT / "dist" / "firmware_update" / "KAJO Firmware Uploader.exe"
LAUNCHER = ROOT / "kajo.bat"
LAUNCHER_NAME = "Install or Update KAJO-Dash.bat"


def main() -> int:
    parser = argparse.ArgumentParser(description="Package a signed KAJO-Dash release for non-technical Windows users")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--uploader-exe", type=Path, default=DEFAULT_EXE)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    release = load_release_manifest(args.manifest)
    try:
        usb_layout = load_usb_layout(args.manifest, release)
    except (ValueError, OSError) as exc:
        parser.error(f"{exc}; run package_usb.py on the release first")
    executable = args.uploader_exe.resolve()
    if not executable.is_file():
        parser.error(f"standalone uploader not found: {executable}")
    if not LAUNCHER.is_file():
        parser.error(f"launcher not found: {LAUNCHER}")

    notices = release_notice_files()
    output = (args.output or
              (ROOT / "dist" / f"KAJO-Dash-Firmware-v{release.version_code}-Windows.zip")).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        parser.error(f"refusing to overwrite package: {output}")

    instructions = f"""KAJO-Dash firmware v{release.version_code} for Windows

Extract every file from this ZIP, then double-click {LAUNCHER_NAME}.

First install, over a USB cable:
1. Connect the display to this computer with a USB data cable.
2. Choose 1. Install over USB cable.
3. Leave the cable connected until the display restarts.

If the display is not detected, hold its BOOT button, tap RST, and keep holding
BOOT until writing starts. Settings already on the display are kept.

Update, over Bluetooth, once KAJO-Dash is running:
1. On the display, open Settings > Information > Bluetooth Link.
2. Choose 2. Update over Bluetooth.
3. Keep the display powered until it verifies and restarts.

The updater discovers Bluetooth Link and requests update mode automatically. If
Bluetooth disconnects, reopen Bluetooth Link and run the launcher again to resume.

Licence and third-party notice files are included in this ZIP.
"""
    with zipfile.ZipFile(output, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        archive.write(LAUNCHER, LAUNCHER_NAME)
        archive.write(executable, "KAJO Firmware Uploader.exe")
        archive.write(args.manifest.resolve(), args.manifest.name)
        archive.write(release.image_path, release.image_path.name)
        archive.write(release.transport_path, release.transport_path.name)
        layout_path = usb_layout_path(args.manifest.resolve())
        archive.write(layout_path, layout_path.name)
        for image in usb_layout.images:
            if image.path != release.image_path:
                archive.write(image.path, image.path.name)
        archive.writestr("README.txt", instructions)
        for path, name in notices:
            archive.write(path, name)

    print(f"Windows release package: {output}")
    print("Package contains public updater, firmware and notice files; no private key is included.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
