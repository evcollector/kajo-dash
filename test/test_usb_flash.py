from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "firmware_update"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import usb_flash  # noqa: E402
from protocol import load_release_manifest  # noqa: E402
from test_package_kajofw import VERSION, package_usb, write_release  # noqa: E402


def released_with_layout(root: Path):
    manifest = write_release(root, VERSION)
    result = package_usb(manifest)
    if result.returncode != 0:
        raise AssertionError(result.stderr)
    return manifest, load_release_manifest(manifest)


def rewrite_layout(manifest: Path, change) -> None:
    path = usb_flash.usb_layout_path(manifest)
    layout = json.loads(path.read_text(encoding="utf-8"))
    change(layout)
    path.write_text(json.dumps(layout), encoding="utf-8")


def port(device: str, vid: int | None, description: str = "") -> SimpleNamespace:
    return SimpleNamespace(device=device, vid=vid, description=description)


class UsbLayoutTests(unittest.TestCase):
    def test_layout_is_loaded_in_flash_order(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest, release = released_with_layout(Path(directory))
            layout = usb_flash.load_usb_layout(manifest, release)
            self.assertEqual([image.offset for image in layout.images], [0x1000, 0x8000, 0xE000, 0x10000])
            self.assertEqual(layout.images[-1].path, release.image_path)

    def test_damaged_boot_image_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest, release = released_with_layout(root)
            (root / f"firmware-v{VERSION}-partitions.bin").write_bytes(b"truncated")
            with self.assertRaisesRegex(ValueError, "does not match"):
                usb_flash.load_usb_layout(manifest, release)

    def test_layout_for_another_version_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest, release = released_with_layout(Path(directory))
            rewrite_layout(manifest, lambda layout: layout.update(version_code=VERSION + 1))
            with self.assertRaisesRegex(ValueError, "different firmware version"):
                usb_flash.load_usb_layout(manifest, release)

    def test_app_slot_must_hold_the_signed_image(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest, release = released_with_layout(root)
            other = root / "other.bin"
            other.write_bytes(b"unsigned build")

            def swap_app(layout):
                layout["images"][-1].update(file=other.name, size=other.stat().st_size,
                                            sha256=hashlib.sha256(other.read_bytes()).hexdigest())
            rewrite_layout(manifest, swap_app)
            with self.assertRaisesRegex(ValueError, "signed firmware image"):
                usb_flash.load_usb_layout(manifest, release)

    def test_nothing_may_be_written_over_the_settings(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest, release = released_with_layout(Path(directory))
            rewrite_layout(manifest, lambda layout: layout["images"][1].update(offset="0x9000"))
            with self.assertRaisesRegex(ValueError, "settings"):
                usb_flash.load_usb_layout(manifest, release)


class PortSelectionTests(unittest.TestCase):
    def choose(self, ports, requested=None):
        with mock.patch("serial.tools.list_ports.comports", return_value=ports):
            return usb_flash.choose_port(requested)

    def test_the_one_usb_serial_adapter_is_chosen(self):
        chosen = self.choose([port("COM3", None, "Standard Serial over Bluetooth link"),
                              port("COM7", 0x1A86, "USB-SERIAL CH340")])
        self.assertEqual(chosen, "COM7")

    def test_explicit_port_wins(self):
        self.assertEqual(self.choose([port("COM7", 0x1A86)], "COM9"), "COM9")

    def test_no_adapter_explains_cable_and_driver(self):
        with self.assertRaisesRegex(RuntimeError, "CH340"):
            self.choose([port("COM3", None, "Bluetooth")])

    def test_several_adapters_need_a_choice_when_not_interactive(self):
        with mock.patch.object(usb_flash, "interactive", return_value=False):
            with self.assertRaisesRegex(RuntimeError, "--port"):
                self.choose([port("COM5", 0x10C4), port("COM7", 0x1A86)])


class FlashCommandTests(unittest.TestCase):
    def test_esptool_writes_every_image_at_its_offset(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest, release = released_with_layout(Path(directory))
            with mock.patch.object(usb_flash, "run_esptool") as run:
                usb_flash.flash_release(manifest, release, "COM7")
            argv = run.call_args.args[0]
            self.assertEqual(argv[:6], ["--chip", "esp32", "--port", "COM7", "--baud", "460800"])
            self.assertIn("write_flash", argv)
            self.assertNotIn("--erase-all", argv)
            pairs = argv[argv.index("4MB") + 1:]
            self.assertEqual(pairs[0::2], ["0x1000", "0x8000", "0xe000", "0x10000"])
            self.assertEqual(Path(pairs[-1]), release.image_path)

    def test_failed_transfer_is_retried_at_the_safe_speed(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest, release = released_with_layout(Path(directory))
            failures = [RuntimeError("Timed out waiting for packet header"), None]

            def fake_run(argv):
                failure = failures.pop(0)
                if failure:
                    raise failure
            with mock.patch.object(usb_flash, "run_esptool", side_effect=fake_run) as run, \
                    mock.patch.object(usb_flash, "interactive", return_value=True), \
                    mock.patch("builtins.input", return_value=""):
                usb_flash.flash_release(manifest, release, "COM7")
            self.assertEqual(run.call_args_list[1].args[0][5], "115200")

    def test_failure_without_a_console_is_final(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest, release = released_with_layout(Path(directory))
            with mock.patch.object(usb_flash, "run_esptool",
                                   side_effect=RuntimeError("Failed to connect to ESP32")), \
                    mock.patch.object(usb_flash, "interactive", return_value=False):
                with self.assertRaisesRegex(RuntimeError, "Failed to connect"):
                    usb_flash.flash_release(manifest, release, "COM7")


if __name__ == "__main__":
    unittest.main()
