"""Build and flash one of the standalone fake-controller CYD firmwares.

Both senders are flashed the same way and differ only in the PlatformIO
environment and the name they advertise, so they share this module rather than
drifting apart as two copies.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


class Sender:
    def __init__(self, environment, banner, advertised):
        self.environment = environment
        self.banner = banner
        self.advertised = advertised


SENDERS = {
    "fardriver": Sender("fardriver_test", "FAKE FARDRIVER TEST FIRMWARE", "FarDriver TEST ..."),
    "vesc": Sender("vesc_test", "FAKE VESC TEST FIRMWARE", "VESC TEST ..."),
}


def main(kind):
    sender = SENDERS[kind]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", nargs="?", help="Explicit target port, e.g. COM4")
    parser.add_argument("--build-only", action="store_true")
    args = parser.parse_args()
    pio = [sys.executable, "-m", "platformio"]
    print(sender.banner, flush=True)
    print("Use a spare CYD as the sender. This replaces its dashboard firmware.", flush=True)
    port = args.port
    if not args.build_only and not port:
        result = subprocess.run(pio + ["device", "list", "--json-output"], cwd=ROOT,
                                check=True, capture_output=True, text=True)
        devices = [device for device in json.loads(result.stdout)
                   if "USB" in device.get("hwid", "").upper()]
        if not devices:
            raise RuntimeError("No USB serial device found. Connect the spare CYD with a data cable. "
                               f"You can also specify its port: upload_{kind}_test.bat COM4")
        if len(devices) == 1:
            port = devices[0]["port"]
            print(f"USB target: {port} ({devices[0].get('description', '')})", flush=True)
        else:
            for index, device in enumerate(devices, 1):
                print(f"{index}: {device['port']} - {device.get('description', '')}")
            selection = input("Choose the SPARE CYD port number (blank cancels): ").strip()
            if not selection.isdigit() or not 1 <= int(selection) <= len(devices):
                raise RuntimeError("Upload cancelled; no valid target selected.")
            port = devices[int(selection) - 1]["port"]
    subprocess.run(pio + ["run", "-e", sender.environment], cwd=ROOT, check=True)
    if args.build_only:
        print("Build complete; no device was flashed.")
        return
    subprocess.run(pio + ["run", "-e", sender.environment, "-t", "upload", "--upload-port", port],
                   cwd=ROOT, check=True)
    print(f"Uploaded test sender to {port}. Select '{sender.advertised}' on the receiver.")


def run(kind):
    """Entry point for the per-sender wrappers, including their error reporting."""
    try:
        main(kind)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError, KeyboardInterrupt) as error:
        print(f"\nNot completed: {error}", file=sys.stderr)
        print("For boot-mode errors: hold BOOT, tap RST/EN, release BOOT after connecting starts.",
              file=sys.stderr)
        sys.exit(1)
