"""Wired first install of a signed KAJO-Dash release, through esptool.

Bluetooth updates need KAJO-Dash already running on the display. A blank or
foreign board is installed over its USB serial port instead, which needs more
than the application image: the second-stage bootloader, the partition table
and an OTA-data reset that makes the bootloader start the first app slot.
package_usb.py writes those three images beside a signed release, together with
a small layout file, firmware-vN-usb.json, that says where each one goes:

    {
      "format": 1,
      "target": "cyd-esp32",
      "version_code": 5,
      "chip": "esp32",
      "flash_mode": "dio",
      "flash_freq": "40m",
      "flash_size": "4MB",
      "images": [
        {"offset": "0x1000", "file": "firmware-v5-bootloader.bin", "size": ..., "sha256": "..."},
        {"offset": "0x8000", "file": "firmware-v5-partitions.bin", ...},
        {"offset": "0xe000", "file": "firmware-v5-boot_app0.bin", ...},
        {"offset": "0x10000", "file": "firmware-v5.bin", ...}
      ]
    }

The application entry is the signed release's own image, so there is one copy
of it and it is checked against the signed manifest before anything is written.
The images are written separately rather than as one merged file because a
merged image pads the gap at 0x9000 and would erase NVS, which holds the
display's settings; a wired reinstall keeps them unless --erase-all is given.

Nothing here is a security boundary: whoever holds the USB cable can write any
image they like. The checks only stop a damaged or mismatched download.
"""

from __future__ import annotations

import hashlib
import json
import sys
from dataclasses import dataclass
from pathlib import Path

from protocol import TARGET_NAME, ReleaseManifest

USB_LAYOUT_FORMAT = 1
CHIP = "esp32"
FLASH_SIZE_BYTES = 4 * 1024 * 1024
APP_OFFSET = 0x10000
# NVS, from partitions.csv. The layout must never write here.
NVS_RANGE = (0x9000, 0xE000)
# PlatformIO's esp32dev upload speed. A retry after a failed transfer drops to
# the ROM loader's safe default.
DEFAULT_BAUD = 460800
FALLBACK_BAUD = 115200

# USB serial bridges found on CYD boards (WCH CH340/CH9102, Silicon Labs
# CP210x), plus FTDI and Espressif's native USB for completeness. Bluetooth
# serial ports and modems are never offered.
USB_SERIAL_VIDS = {
    0x1A86: "WCH CH340/CH9102",
    0x10C4: "Silicon Labs CP210x",
    0x0403: "FTDI",
    0x303A: "Espressif USB",
}

DRIVER_HINT = """\
Connect the display with a USB data cable; some cables only carry power.
Windows normally installs the USB serial driver by itself within a minute.
If no COM port appears in Device Manager, install the driver for the board's
USB chip:
  CH340 / CH9102: https://www.wch-ic.com/downloads/CH341SER_EXE.html
  CP2102:         https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers"""

PORT_BUSY_HINT = """\
Close any other program that has the port open, such as a serial monitor,
VESC Tool or the Arduino IDE, and check the display is still connected."""

BOOT_MODE_HINT = """\
If the display did not enter its download mode by itself: hold the BOOT
button, tap RST, and keep holding BOOT until writing starts."""


@dataclass(frozen=True)
class FlashImage:
    offset: int
    path: Path


@dataclass(frozen=True)
class UsbLayout:
    version_code: int
    flash_mode: str
    flash_freq: str
    flash_size: str
    images: tuple[FlashImage, ...]


def usb_layout_path(manifest_path: Path) -> Path:
    manifest_path = Path(manifest_path)
    return manifest_path.with_name(f"{manifest_path.stem}-usb.json")


def load_usb_layout(manifest_path: Path, release: ReleaseManifest) -> UsbLayout:
    """Read and verify the USB layout that belongs to a verified signed release."""
    path = usb_layout_path(manifest_path).resolve()
    if not path.is_file():
        raise ValueError(f"this release has no USB install layout ({path.name} is missing)")
    payload = json.loads(path.read_text(encoding="utf-8"))
    if payload.get("format") != USB_LAYOUT_FORMAT:
        raise ValueError(f"{path.name} uses an unsupported layout format")
    if payload.get("target") != TARGET_NAME or payload.get("chip") != CHIP:
        raise ValueError(f"{path.name} targets a different device family")
    if int(payload.get("version_code", -1)) != release.version_code:
        raise ValueError(f"{path.name} belongs to a different firmware version")

    images = []
    for entry in payload.get("images", []):
        offset = int(entry["offset"], 16)
        image_path = (path.parent / entry["file"]).resolve()
        data = image_path.read_bytes()
        if len(data) != entry["size"] or hashlib.sha256(data).hexdigest() != entry["sha256"]:
            raise ValueError(f"{image_path.name} does not match {path.name}")
        images.append((offset, image_path, len(data), data))
    images.sort(key=lambda image: image[0])

    app = [image for image in images if image[0] == APP_OFFSET]
    if len(app) != 1 or hashlib.sha256(app[0][3]).digest() != release.image_sha256:
        raise ValueError(f"{path.name} does not install the signed firmware image")
    end = 0
    for offset, image_path, size, _ in images:
        if offset < end:
            raise ValueError(f"{image_path.name} overlaps the image before it")
        end = offset + size
        if end > FLASH_SIZE_BYTES:
            raise ValueError(f"{image_path.name} does not fit a 4 MB flash")
        if offset < NVS_RANGE[1] and end > NVS_RANGE[0]:
            raise ValueError(f"{image_path.name} would overwrite the display's settings")
    return UsbLayout(
        release.version_code,
        payload["flash_mode"],
        payload["flash_freq"],
        payload["flash_size"],
        tuple(FlashImage(offset, image_path) for offset, image_path, _, _ in images),
    )


def interactive() -> bool:
    return sys.stdin is not None and sys.stdin.isatty()


def choose_port(port: str | None) -> str:
    """The requested port, or the one USB serial adapter that is connected."""
    if port:
        return port
    from serial.tools import list_ports

    ports = sorted(list_ports.comports(), key=lambda info: info.device)
    candidates = [info for info in ports if info.vid in USB_SERIAL_VIDS]
    if len(candidates) == 1:
        info = candidates[0]
        print(f"Found {USB_SERIAL_VIDS[info.vid]} on {info.device}.")
        return info.device
    if not candidates:
        message = "no USB serial adapter was found.\n\n" + DRIVER_HINT
        if ports:
            others = ", ".join(info.device for info in ports)
            message += f"\n\nOther serial ports ({others}) were ignored; pass --port COMx to use one."
        raise RuntimeError(message)

    print("More than one USB serial adapter is connected:")
    for number, info in enumerate(candidates, 1):
        print(f"  {number}. {info.device}  {info.description}")
    if not interactive():
        raise RuntimeError("choose one with --port COMx")
    while True:
        answer = input(f"Which one is the display? [1-{len(candidates)}]: ").strip()
        if answer.isdigit() and 1 <= int(answer) <= len(candidates):
            return candidates[int(answer) - 1].device


def run_esptool(argv: list[str]) -> None:
    """esptool's own entry point, with its failures turned into exceptions."""
    import esptool
    import serial

    try:
        esptool.main(argv)
    except esptool.FatalError as exc:
        raise RuntimeError(str(exc)) from exc
    except serial.SerialException as exc:
        raise RuntimeError(f"serial port error: {exc}") from exc
    except StopIteration as exc:
        raise RuntimeError("the chip stopped responding") from exc
    except SystemExit as exc:
        if exc.code:
            raise RuntimeError(f"esptool stopped with exit code {exc.code}") from exc


def flash_release(manifest_path: Path, release: ReleaseManifest, port: str | None,
                  baud: int = DEFAULT_BAUD, erase_all: bool = False) -> None:
    layout = load_usb_layout(manifest_path, release)
    port = choose_port(port)
    print(f"Installing firmware v{layout.version_code} on {port}.")
    if erase_all:
        print("The whole flash is erased first, so the display's settings are reset.")
    else:
        print("The display's settings are kept.")

    while True:
        argv = ["--chip", CHIP, "--port", port, "--baud", str(baud),
                "--before", "default_reset", "--after", "hard_reset",
                "write_flash", "-z",
                "--flash_mode", layout.flash_mode,
                "--flash_freq", layout.flash_freq,
                "--flash_size", layout.flash_size]
        if erase_all:
            argv.append("--erase-all")
        for image in layout.images:
            argv += [hex(image.offset), str(image.path)]
        print()
        try:
            run_esptool(argv)
            return
        except RuntimeError as exc:
            failure = exc
        print(f"\nInstall attempt failed: {failure}\n")
        if "Could not open" in str(failure):
            print(PORT_BUSY_HINT)
        elif "Failed to connect" in str(failure):
            print(BOOT_MODE_HINT)
        elif baud != FALLBACK_BAUD:
            # The link came up, so the board is fine; a marginal cable or
            # adapter often copes at the lower speed.
            print(f"The next attempt uses the slower {FALLBACK_BAUD} baud.")
            baud = FALLBACK_BAUD
        if not interactive():
            raise failure
        try:
            answer = input("Press Enter to try again, or Q and Enter to stop: ").strip().lower()
        except EOFError:
            answer = "q"
        if answer.startswith("q"):
            raise failure
