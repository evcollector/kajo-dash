from __future__ import annotations

import base64
import hashlib
import json
import re
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "firmware_update"))

import protocol  # noqa: E402
from protocol import (  # noqa: E402
    COMMAND_BEGIN,
    COMMAND_SIGNATURE,
    MAX_DATA_BYTES,
    PROTOCOL_VERSION,
    ReleaseManifest,
    RAW_SECTOR_SIZE,
    STATE_RECEIVING,
    begin_commands,
    commit_sector_command,
    data_packet,
    decode_status,
    load_release_manifest,
    signature_commands,
    transport_sectors,
)


class FirmwareUpdateProtocolTests(unittest.TestCase):
    def test_manifest_has_fixed_little_endian_layout(self):
        manifest = ReleaseManifest(0x01020304, 0x11223344, bytes(range(32)), 0x55667788,
                                   bytes(reversed(range(32))), bytes(64), Path("firmware.bin"), Path("firmware.zlib"))
        expected = bytes.fromhex(
            "4b414a55" "03" "01" "0100" "04030201" "44332211" "88776655" +  # "KAJU"
            "".join(f"{value:02x}" for value in range(32)) +
            "".join(f"{value:02x}" for value in reversed(range(32)))
        )
        self.assertEqual(manifest.canonical_bytes(), expected)
        commands = begin_commands(manifest)
        self.assertEqual(b"".join(command[2:] for command in commands), expected)
        self.assertEqual([command[1] for command in commands], [0, 59])

    def test_signature_is_fragmented_with_explicit_offsets(self):
        signature = bytes(range(64))
        commands = signature_commands(signature, 32)
        self.assertEqual(commands[0], bytes([COMMAND_SIGNATURE, 0]) + signature[:32])
        self.assertEqual(commands[1], bytes([COMMAND_SIGNATURE, 32]) + signature[32:])

    def test_status_decodes_the_firmware_layout(self):
        # Laid out by hand from encodeStatus() in src/lvgl_app/firmware_update_ble.cpp
        # rather than packed with the decoder's own format string, so a layout
        # change on either side fails here.
        encoded = bytes.fromhex(
            "03"          # [0] FIRMWARE_UPDATE_PROTOCOL_VERSION
            "06"          # [1] FIRMWARE_UPDATE_BLE_RECEIVING
            "07"          # [2] key configured | client connected | manifest accepted
            "00"          # [3] reserved
            "00100000"    # [4] expected offset 4096
            "00200000"    # [8] total bytes 8192
            "50fbffff"    # [12] last error -1200, written as uint32
            "07000000"    # [16] version code 7
            "e02e0000"    # [20] image bytes 12000
            "13000000"    # [24] revision 19
        )
        status = decode_status(encoded)
        self.assertEqual(status.protocol_version, PROTOCOL_VERSION)
        self.assertEqual(status.state, STATE_RECEIVING)
        self.assertEqual(status.expected_offset, 4096)
        self.assertEqual(status.total_bytes, 8192)
        self.assertEqual(status.error, -1200)
        self.assertEqual(status.version_code, 7)
        self.assertTrue(status.key_configured)
        self.assertTrue(status.connected)
        self.assertTrue(status.manifest_accepted)
        self.assertEqual(status.image_size, 12000)
        self.assertEqual(status.revision, 19)

    def test_state_numbers_and_version_match_the_firmware(self):
        header = (ROOT / "src" / "lvgl_app" / "firmware_update_ble.h").read_text(encoding="utf-8")
        body = re.search(r"enum FirmwareUpdateBleState[^{]*\{(.*?)\};", header, re.S).group(1)
        firmware_states = re.findall(r"FIRMWARE_UPDATE_BLE_(\w+)\s*,", body)
        python_states = sorted(((value, name[len("STATE_"):]) for name, value in vars(protocol).items()
                                if name.startswith("STATE_")))
        self.assertEqual([name for _, name in python_states], firmware_states)
        self.assertEqual([value for value, _ in python_states], list(range(len(firmware_states))))

        auth = (ROOT / "src" / "lvgl_app" / "firmware_update_auth.h").read_text(encoding="utf-8")
        version = re.search(r"FIRMWARE_UPDATE_PROTOCOL_VERSION\s*=\s*(\d+)", auth).group(1)
        self.assertEqual(int(version), PROTOCOL_VERSION)

    def test_release_loader_rejects_modified_image(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / "firmware.bin"
            image.write_bytes(b"valid image")
            digest = hashlib.sha256(image.read_bytes()).hexdigest()
            release = root / "release.json"
            release.write_text(json.dumps({
                "protocol": PROTOCOL_VERSION,
                "target": "cyd-esp32",
                "version_code": 2,
                "image": image.name,
                "size": image.stat().st_size,
                "sha256": digest,
                "transport": "firmware.zlib",
                "transport_size": 3,
                "transport_sha256": hashlib.sha256(b"bad").hexdigest(),
                "transport_encoding": "zlib-sectors-4096",
                "signature_algorithm": "ecdsa-p256-sha256-raw",
                "signature": base64.b64encode(bytes(64)).decode("ascii"),
            }), encoding="utf-8")
            image.write_bytes(b"modified image")
            (root / "firmware.zlib").write_bytes(b"bad")
            with self.assertRaisesRegex(ValueError, "size|SHA-256"):
                load_release_manifest(release)

    def test_release_loader_rejects_older_protocol(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / "firmware.bin"
            image.write_bytes(b"firmware")
            release = root / "release.json"
            release.write_text(json.dumps({
                "protocol": PROTOCOL_VERSION - 1,
                "target": "cyd-esp32",
                "version_code": 2,
                "image": image.name,
                "size": image.stat().st_size,
                "sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
                "transport": "firmware.zlib",
                "transport_size": 3,
                "transport_sha256": hashlib.sha256(b"bad").hexdigest(),
                "transport_encoding": "zlib-sectors-4096",
                "signature_algorithm": "ecdsa-p256-sha256-raw",
                "signature": base64.b64encode(bytes(64)).decode("ascii"),
            }), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "unsupported release protocol"):
                load_release_manifest(release)

    def test_release_loader_validates_compressed_transport_round_trip(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image_bytes = bytes(range(256)) * 24
            image = root / "firmware.bin"
            image.write_bytes(image_bytes)
            transport_bytes = b"".join(
                struct.pack("<H", len(block)) + block
                for block in (zlib.compress(image_bytes[offset : offset + RAW_SECTOR_SIZE], 9)
                              for offset in range(0, len(image_bytes), RAW_SECTOR_SIZE))
            )
            transport = root / "firmware.zlib"
            transport.write_bytes(transport_bytes)
            release = root / "release.json"
            release.write_text(json.dumps({
                "protocol": PROTOCOL_VERSION,
                "target": "cyd-esp32",
                "version_code": 5,
                "image": image.name,
                "size": len(image_bytes),
                "sha256": hashlib.sha256(image_bytes).hexdigest(),
                "transport": transport.name,
                "transport_size": len(transport_bytes),
                "transport_sha256": hashlib.sha256(transport_bytes).hexdigest(),
                "transport_encoding": "zlib-sectors-4096",
                "signature_algorithm": "ecdsa-p256-sha256-raw",
                "signature": base64.b64encode(bytes(64)).decode("ascii"),
            }), encoding="utf-8")
            loaded = load_release_manifest(release)
            self.assertEqual(loaded.image_size, len(image_bytes))
            self.assertEqual(loaded.transport_size, len(transport_bytes))

    def test_data_packet_is_offset_prefixed_and_fits_247_byte_mtu(self):
        # 247-byte ATT MTU, minus the 3-byte write header.
        packet = data_packet(0x11223344, bytes(MAX_DATA_BYTES))
        self.assertEqual(packet[:4], bytes.fromhex("44332211"))
        self.assertLessEqual(len(packet), 244)
        with self.assertRaises(ValueError):
            data_packet(0, bytes(MAX_DATA_BYTES + 1))

    def test_zlib_transport_sectors_round_trip_and_commit_crc(self):
        image = bytes(range(256)) * 20
        parts = []
        for offset in range(0, len(image), RAW_SECTOR_SIZE):
            compressed = zlib.compress(image[offset : offset + RAW_SECTOR_SIZE], 9)
            parts.append(struct.pack("<H", len(compressed)) + compressed)
        transport = b"".join(parts)
        sectors = list(transport_sectors(transport))
        rebuilt = b"".join(zlib.decompress(sector[2:]) for _, sector in sectors)
        self.assertEqual(rebuilt, image)
        command = commit_sector_command(sectors[0][0], sectors[0][1])
        self.assertEqual(command[0], 8)
        self.assertEqual(struct.unpack_from("<I", command, 1)[0], 0)


if __name__ == "__main__":
    unittest.main()
