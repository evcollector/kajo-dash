from __future__ import annotations

import argparse
import zipfile
from pathlib import Path

from protocol import load_release_manifest
from release_notices import release_notice_files


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EXE = ROOT / "dist" / "firmware_update" / "KAJO Firmware Uploader.exe"
LAUNCHER = ROOT / "kajo.bat"


def main() -> int:
    parser = argparse.ArgumentParser(description="Package a signed KAJO-Dash release for non-technical Windows users")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--uploader-exe", type=Path, default=DEFAULT_EXE)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    release = load_release_manifest(args.manifest)
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

    instructions = f"""KAJO-Dash firmware v{release.version_code} Bluetooth updater

1. Extract every file from this ZIP.
2. On the display, open Settings > Information > Bluetooth Link.
3. Double-click Update KAJO-Dash Firmware.bat.
4. Keep the display powered until it verifies and restarts.

The updater discovers Bluetooth Link and requests update mode automatically. If
Bluetooth disconnects, reopen Bluetooth Link and run the launcher again to resume.

Licence and third-party notice files are included in this ZIP.
"""
    with zipfile.ZipFile(output, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        archive.write(LAUNCHER, "Update KAJO-Dash Firmware.bat")
        archive.write(executable, "KAJO Firmware Uploader.exe")
        archive.write(args.manifest.resolve(), args.manifest.name)
        archive.write(release.image_path, release.image_path.name)
        archive.write(release.transport_path, release.transport_path.name)
        archive.writestr("README.txt", instructions)
        for path, name in notices:
            archive.write(path, name)

    print(f"Windows release package: {output}")
    print("Package contains public updater, firmware and notice files; no private key is included.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
