from __future__ import annotations

import base64
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PACKAGER = ROOT / "tools" / "firmware_update" / "package_kajofw.py"
WINDOWS_PACKAGER = ROOT / "tools" / "firmware_update" / "package_windows_release.py"
USB_PACKAGER = ROOT / "tools" / "firmware_update" / "package_usb.py"
RAW_SECTOR_SIZE = 4096
# Far from anything include/config.h will reach, so the name is never read from it.
VERSION = 900005
NOTICE_FILES = ["LICENSE", "NOTICE", "THIRD-PARTY-NOTICES.md"] + [
    f"licenses/{path.name}" for path in sorted((ROOT / "licenses").glob("*.txt"))
]


def write_release(root: Path, version_code: int) -> Path:
    """A release in the shape sign_firmware.py writes, with a dummy signature."""
    image_bytes = bytes([version_code % 256]) * 5000
    transport_bytes = b"".join(
        struct.pack("<H", len(block)) + block
        for block in (zlib.compress(image_bytes[offset : offset + RAW_SECTOR_SIZE], 9)
                      for offset in range(0, len(image_bytes), RAW_SECTOR_SIZE)))
    stem = f"firmware-v{version_code}"
    (root / f"{stem}.bin").write_bytes(image_bytes)
    (root / f"{stem}.zlib").write_bytes(transport_bytes)
    manifest = root / f"{stem}.json"
    manifest.write_text(json.dumps({
        "protocol": 3,
        "target": "cyd-esp32",
        "version_code": version_code,
        "image": f"{stem}.bin",
        "size": len(image_bytes),
        "sha256": hashlib.sha256(image_bytes).hexdigest(),
        "transport": f"{stem}.zlib",
        "transport_size": len(transport_bytes),
        "transport_sha256": hashlib.sha256(transport_bytes).hexdigest(),
        "transport_encoding": "zlib-sectors-4096",
        "signature_algorithm": "ecdsa-p256-sha256-raw",
        "signature": base64.b64encode(bytes(64)).decode("ascii"),
    }), encoding="utf-8")
    return manifest


def package_usb(manifest: Path, build_image: bytes | None = None) -> subprocess.CompletedProcess:
    """Run package_usb.py against a fake PlatformIO build of the signed image."""
    build = manifest.parent / "build"
    build.mkdir(exist_ok=True)
    (build / "firmware.bin").write_bytes(
        manifest.with_suffix(".bin").read_bytes() if build_image is None else build_image)
    (build / "bootloader.bin").write_bytes(b"\xe9" + bytes(100))
    (build / "partitions.bin").write_bytes(b"\xaa\x50" + bytes(200))
    boot_app0 = manifest.parent / "boot_app0.bin"
    boot_app0.write_bytes(b"\xff" * 8192)
    return subprocess.run([sys.executable, str(USB_PACKAGER), str(manifest),
                           "--build-dir", str(build), "--boot-app0", str(boot_app0)],
                          capture_output=True, text=True)


def package(manifest: Path, *extra: str) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(PACKAGER), str(manifest), *extra],
                          capture_output=True, text=True)


def package_windows(manifest: Path, uploader: Path, output: Path) -> subprocess.CompletedProcess:
    return subprocess.run([
        sys.executable, str(WINDOWS_PACKAGER),
        "--manifest", str(manifest),
        "--uploader-exe", str(uploader),
        "--output", str(output),
    ], capture_output=True, text=True)


class PackageKajofwTests(unittest.TestCase):
    def test_bundle_holds_firmware_and_notices_and_the_index_describes_it(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = write_release(root, VERSION)
            result = package(manifest, "--version-name", "9000.05")
            self.assertEqual(result.returncode, 0, result.stderr)

            bundle = root / f"firmware-v{VERSION}.kajofw"
            with zipfile.ZipFile(bundle) as archive:
                self.assertEqual(sorted(archive.namelist()), sorted(
                    [f"firmware-v{VERSION}{suffix}" for suffix in (".bin", ".json", ".zlib")]
                    + NOTICE_FILES))
                self.assertTrue(all(info.compress_type == zipfile.ZIP_STORED
                                    for info in archive.infolist()))
                for name in NOTICE_FILES:
                    self.assertEqual(archive.read(name), (ROOT / name).read_bytes())

            index = json.loads((root / "kajo-update.json").read_text(encoding="utf-8"))
            self.assertEqual(index, {
                "format": 1,
                "target": "cyd-esp32",
                "protocol": 3,
                "version_code": VERSION,
                "version_name": "9000.05",
                "tag": "v9000.05",
                "bundle": bundle.name,
                "bundle_size": bundle.stat().st_size,
                "bundle_sha256": hashlib.sha256(bundle.read_bytes()).hexdigest(),
            })

    def test_index_is_never_rewound_to_an_older_release(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.assertEqual(package(write_release(root, VERSION), "--version-name", "new").returncode, 0)
            older = package(write_release(root, VERSION - 1), "--version-name", "old")
            self.assertEqual(older.returncode, 0, older.stderr)
            self.assertTrue((root / f"firmware-v{VERSION - 1}.kajofw").is_file())
            index = json.loads((root / "kajo-update.json").read_text(encoding="utf-8"))
            self.assertEqual(index["version_code"], VERSION)

    def test_version_name_is_required_when_config_h_has_moved_on(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = package(write_release(root, VERSION))
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("--version-name", result.stderr)
            self.assertFalse((root / f"firmware-v{VERSION}.kajofw").exists())

    def test_no_index_packages_the_bundle_alone(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = package(write_release(root, VERSION), "--no-index")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue((root / f"firmware-v{VERSION}.kajofw").is_file())
            self.assertFalse((root / "kajo-update.json").exists())

    def test_windows_package_has_exact_public_files_and_verified_payloads(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = write_release(root, VERSION)
            uploader = root / "KAJO Firmware Uploader.exe"
            uploader.write_bytes(b"MZ\x00public test uploader")
            # Nearby private-looking files must never be swept into the ZIP.
            (root / "release.local.json").write_text("private key path", encoding="utf-8")
            (root / "official-private-key.pem").write_text(
                "-----BEGIN PRIVATE KEY-----\nnot-a-real-key\n", encoding="utf-8")
            output = root / f"KAJO-Dash-Firmware-v{VERSION}-Windows.zip"
            usb = package_usb(manifest)
            self.assertEqual(usb.returncode, 0, usb.stderr)

            result = package_windows(manifest, uploader, output)
            self.assertEqual(result.returncode, 0, result.stderr)
            usb_files = [f"firmware-v{VERSION}-usb.json"] + [
                f"firmware-v{VERSION}-{part}.bin" for part in ("bootloader", "partitions", "boot_app0")]
            with zipfile.ZipFile(output) as archive:
                self.assertEqual(sorted(archive.namelist()), sorted([
                    "Install or Update KAJO-Dash.bat",
                    "KAJO Firmware Uploader.exe",
                    f"firmware-v{VERSION}.json",
                    f"firmware-v{VERSION}.bin",
                    f"firmware-v{VERSION}.zlib",
                    "README.txt",
                ] + usb_files + NOTICE_FILES))
                for name in usb_files:
                    self.assertEqual(archive.read(name), (root / name).read_bytes())
                self.assertEqual(archive.read(f"firmware-v{VERSION}.json"), manifest.read_bytes())
                self.assertEqual(archive.read(f"firmware-v{VERSION}.bin"),
                                 manifest.with_suffix(".bin").read_bytes())
                self.assertEqual(archive.read(f"firmware-v{VERSION}.zlib"),
                                 manifest.with_suffix(".zlib").read_bytes())
                self.assertEqual(archive.read("KAJO Firmware Uploader.exe"), uploader.read_bytes())
                for name in NOTICE_FILES:
                    self.assertEqual(archive.read(name), (ROOT / name).read_bytes())

    def test_windows_package_rejects_firmware_with_wrong_checksum(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = write_release(root, VERSION)
            self.assertEqual(package_usb(manifest).returncode, 0)
            manifest.with_suffix(".bin").write_bytes(b"tampered")
            uploader = root / "uploader.exe"
            uploader.write_bytes(b"MZ")
            output = root / "release.zip"

            result = package_windows(manifest, uploader, output)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(output.exists())

    def test_windows_package_requires_the_usb_layout(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = write_release(root, VERSION)
            uploader = root / "uploader.exe"
            uploader.write_bytes(b"MZ")
            output = root / "release.zip"

            result = package_windows(manifest, uploader, output)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("package_usb.py", result.stderr)
            self.assertFalse(output.exists())


class PackageUsbTests(unittest.TestCase):
    def test_layout_places_boot_images_and_the_signed_app_outside_nvs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = write_release(root, VERSION)
            result = package_usb(manifest)
            self.assertEqual(result.returncode, 0, result.stderr)

            layout = json.loads((root / f"firmware-v{VERSION}-usb.json").read_text(encoding="utf-8"))
            self.assertEqual(layout["version_code"], VERSION)
            self.assertEqual((layout["chip"], layout["flash_mode"], layout["flash_freq"], layout["flash_size"]),
                             ("esp32", "dio", "40m", "4MB"))
            self.assertEqual([(image["offset"], image["file"]) for image in layout["images"]], [
                ("0x1000", f"firmware-v{VERSION}-bootloader.bin"),
                ("0x8000", f"firmware-v{VERSION}-partitions.bin"),
                ("0xe000", f"firmware-v{VERSION}-boot_app0.bin"),
                ("0x10000", f"firmware-v{VERSION}.bin"),
            ])
            for image in layout["images"]:
                data = (root / image["file"]).read_bytes()
                self.assertEqual(image["size"], len(data))
                self.assertEqual(image["sha256"], hashlib.sha256(data).hexdigest())

    def test_boot_images_must_come_from_the_signed_build(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = write_release(root, VERSION)
            result = package_usb(manifest, build_image=b"a later build")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("not the signed image", result.stderr)
            self.assertFalse((root / f"firmware-v{VERSION}-usb.json").exists())


if __name__ == "__main__":
    unittest.main()
