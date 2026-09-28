"""Add the USB install layout to a signed KAJO-Dash release.

Writes the bootloader, partition table and OTA-data reset of the build that
produced the signed image beside it, and describes all four images in
firmware-vN-usb.json (the format is in usb_flash.py). With them, the Windows
updater ZIP can install a blank display over a USB cable without PlatformIO.

Run it straight after signing, before the next build replaces .pio/build/kajo:
it refuses unless the build directory still holds the signed image, because a
bootloader or partition table from a different build would not belong to it.
The release builder (option 5 in kajo.bat) does this for you.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from pathlib import Path

from protocol import TARGET_NAME, load_release_manifest
from usb_flash import APP_OFFSET, CHIP, USB_LAYOUT_FORMAT, load_usb_layout, usb_layout_path

ROOT = Path(__file__).resolve().parents[2]
BUILD_DIR = ROOT / ".pio" / "build" / "kajo"
BOOT_APP0 = (Path.home() / ".platformio" / "packages" / "framework-arduinoespressif32" /
             "tools" / "partitions" / "boot_app0.bin")

# What PlatformIO passes to esptool for the esp32dev board, so a wired install
# from a release writes the same bootloader header as `pio run -t upload`.
FLASH_MODE = "dio"
FLASH_FREQ = "40m"
FLASH_SIZE = "4MB"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Write the USB install layout beside a signed KAJO-Dash release")
    parser.add_argument("manifest", type=Path, help="signed JSON produced by sign_firmware.py")
    parser.add_argument("--build-dir", type=Path, default=BUILD_DIR,
                        help="PlatformIO build directory of the signed image")
    parser.add_argument("--boot-app0", type=Path, default=BOOT_APP0,
                        help="the Arduino core's boot_app0.bin")
    args = parser.parse_args()

    manifest_path = args.manifest.resolve()
    release = load_release_manifest(manifest_path)
    layout_path = usb_layout_path(manifest_path)
    if layout_path.exists():
        parser.error(f"refusing to overwrite USB layout: {layout_path}")

    build_image = args.build_dir / "firmware.bin"
    if not build_image.is_file() or hashlib.sha256(build_image.read_bytes()).digest() != release.image_sha256:
        parser.error(f"{build_image} is not the signed image; the bootloader and partition table "
                     "must come from the build that was signed")
    sources = [
        (0x1000, args.build_dir / "bootloader.bin", "bootloader"),
        (0x8000, args.build_dir / "partitions.bin", "partitions"),
        (0xE000, args.boot_app0, "boot_app0"),
    ]
    for _, source, _ in sources:
        if not source.is_file():
            parser.error(f"not found: {source}")

    stem = manifest_path.stem
    images = []
    for offset, source, suffix in sources:
        target = manifest_path.with_name(f"{stem}-{suffix}.bin")
        shutil.copyfile(source, target)
        images.append((offset, target))
    images.append((APP_OFFSET, release.image_path))

    layout = {
        "format": USB_LAYOUT_FORMAT,
        "target": TARGET_NAME,
        "version_code": release.version_code,
        "chip": CHIP,
        "flash_mode": FLASH_MODE,
        "flash_freq": FLASH_FREQ,
        "flash_size": FLASH_SIZE,
        "images": [{
            "offset": hex(offset),
            "file": path.name,
            "size": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        } for offset, path in images],
    }
    layout_path.write_text(json.dumps(layout, indent=2) + "\n", encoding="utf-8", newline="\n")
    # Read it back through the same checks the installer applies.
    load_usb_layout(manifest_path, release)
    print(f"USB install layout: {layout_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
