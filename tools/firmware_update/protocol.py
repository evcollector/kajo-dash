from __future__ import annotations

import base64
import hashlib
import json
import struct
import zlib
from dataclasses import dataclass
from pathlib import Path

PROTOCOL_VERSION = 3
TARGET_CYD_ESP32 = 1
TARGET_NAME = "cyd-esp32"
MANIFEST_MAGIC = b"KAJU"
MANIFEST_SIZE = 84
SIGNATURE_SIZE = 64
MANIFEST_FLAG_ZLIB_SECTORS = 0x0001
RAW_SECTOR_SIZE = 4096
# ATT MTU 247 leaves 244 value bytes; four are reserved for the absolute offset.
MAX_DATA_BYTES = 240

SERVICE_UUID = "7c7d7e00-2aa7-4f62-a497-6a9b02d14d00"
CONTROL_UUID = "7c7d7e01-2aa7-4f62-a497-6a9b02d14d00"
DATA_UUID = "7c7d7e02-2aa7-4f62-a497-6a9b02d14d00"
STATUS_UUID = "7c7d7e03-2aa7-4f62-a497-6a9b02d14d00"

COMMAND_BEGIN = 1
COMMAND_SIGNATURE = 2
COMMAND_COMMIT_MANIFEST = 3
COMMAND_FINISH = 4
COMMAND_ABORT = 5
COMMAND_REBOOT = 6
COMMAND_STATUS = 7
COMMAND_COMMIT_SECTOR = 8

STATE_LOCKED = 0
STATE_READY = 1
STATE_PREPARING = 2
STATE_ADVERTISING = 3
STATE_CONNECTED = 4
STATE_CONFIRM_DOWNGRADE = 5
STATE_RECEIVING = 6
STATE_READY_TO_REBOOT = 7
STATE_ERROR = 8
STATE_CANCELLED = 9


@dataclass(frozen=True)
class ReleaseManifest:
    version_code: int
    image_size: int
    image_sha256: bytes
    transport_size: int
    transport_sha256: bytes
    signature: bytes
    image_path: Path
    transport_path: Path

    def canonical_bytes(self) -> bytes:
        if len(self.image_sha256) != 32 or len(self.transport_sha256) != 32:
            raise ValueError("image and transport SHA-256 values must be exactly 32 bytes")
        return struct.pack(
            "<4sBBHIII32s32s",
            MANIFEST_MAGIC,
            PROTOCOL_VERSION,
            TARGET_CYD_ESP32,
            MANIFEST_FLAG_ZLIB_SECTORS,
            self.version_code,
            self.image_size,
            self.transport_size,
            self.image_sha256,
            self.transport_sha256,
        )


@dataclass(frozen=True)
class DeviceStatus:
    protocol_version: int
    state: int
    flags: int
    expected_offset: int
    total_bytes: int
    error: int
    version_code: int
    image_size: int
    revision: int

    @property
    def key_configured(self) -> bool:
        return bool(self.flags & 0x01)

    @property
    def connected(self) -> bool:
        return bool(self.flags & 0x02)

    @property
    def manifest_accepted(self) -> bool:
        return bool(self.flags & 0x04)


def decode_status(value: bytes) -> DeviceStatus:
    if len(value) != 28:
        raise ValueError(f"status must be 28 bytes, received {len(value)}")
    protocol, state, flags, _reserved, offset, total, error, version, image_size, revision = struct.unpack(
        "<BBBBIIiIII", value)
    return DeviceStatus(protocol, state, flags, offset, total, error, version, image_size, revision)


def load_release_manifest(path: Path, image_override: Path | None = None) -> ReleaseManifest:
    manifest_path = path.resolve()
    payload = json.loads(manifest_path.read_text(encoding="utf-8"))
    if payload.get("protocol") != PROTOCOL_VERSION:
        raise ValueError("unsupported release protocol")
    if payload.get("target") != TARGET_NAME:
        raise ValueError("release targets a different device family")
    if payload.get("signature_algorithm") != "ecdsa-p256-sha256-raw":
        raise ValueError("unsupported signature algorithm")
    if payload.get("transport_encoding") != "zlib-sectors-4096":
        raise ValueError("unsupported firmware transport encoding")

    image_path = image_override.resolve() if image_override else (manifest_path.parent / payload["image"]).resolve()
    transport_path = (manifest_path.parent / payload["transport"]).resolve()
    image = image_path.read_bytes()
    transport = transport_path.read_bytes()
    digest = hashlib.sha256(image).digest()
    transport_digest = hashlib.sha256(transport).digest()
    declared_digest = bytes.fromhex(payload["sha256"])
    declared_transport_digest = bytes.fromhex(payload["transport_sha256"])
    signature = base64.b64decode(payload["signature"], validate=True)
    if len(signature) != SIGNATURE_SIZE:
        raise ValueError("release signature must be a raw 64-byte P-256 r||s value")
    if len(image) != payload["size"]:
        raise ValueError("firmware size does not match the signed release manifest")
    if digest != declared_digest:
        raise ValueError("firmware SHA-256 does not match the signed release manifest")
    if len(transport) != payload["transport_size"]:
        raise ValueError("compressed transport size does not match the signed release manifest")
    if transport_digest != declared_transport_digest:
        raise ValueError("compressed transport SHA-256 does not match the signed release manifest")
    rebuilt = b"".join(zlib.decompress(block[2:]) for _, block in transport_sectors(transport))
    if rebuilt != image:
        raise ValueError("compressed transport does not reconstruct the signed firmware image")
    version_code = int(payload["version_code"])
    if not 0 <= version_code <= 0xFFFFFFFF:
        raise ValueError("version_code must fit an unsigned 32-bit integer")
    return ReleaseManifest(version_code, len(image), digest, len(transport), transport_digest,
                           signature, image_path, transport_path)


def begin_commands(manifest: ReleaseManifest, fragment_size: int = 59) -> list[bytes]:
    encoded = manifest.canonical_bytes()
    if len(encoded) != MANIFEST_SIZE:
        raise AssertionError("canonical manifest size changed")
    if not 1 <= fragment_size <= 59:
        raise ValueError("manifest fragment must fit the 64-byte control characteristic")
    return [bytes([COMMAND_BEGIN, offset]) + encoded[offset : offset + fragment_size]
            for offset in range(0, len(encoded), fragment_size)]


def signature_commands(signature: bytes, fragment_size: int = 32) -> list[bytes]:
    if len(signature) != SIGNATURE_SIZE:
        raise ValueError("signature must contain 64 raw bytes")
    if not 1 <= fragment_size <= 59:
        raise ValueError("signature fragment must fit the 64-byte control characteristic")
    return [bytes([COMMAND_SIGNATURE, offset]) + signature[offset : offset + fragment_size]
            for offset in range(0, len(signature), fragment_size)]


def data_packet(offset: int, payload: bytes) -> bytes:
    if not payload or len(payload) > MAX_DATA_BYTES:
        raise ValueError(f"firmware data payload must contain 1..{MAX_DATA_BYTES} bytes")
    return struct.pack("<I", offset) + payload


def commit_sector_command(offset: int, sector: bytes) -> bytes:
    if not sector or len(sector) > RAW_SECTOR_SIZE + 16:
        raise ValueError("compressed sector has an invalid size")
    return struct.pack("<BIHI", COMMAND_COMMIT_SECTOR, offset, len(sector), zlib.crc32(sector))


def transport_sectors(transport: bytes):
    offset = 0
    while offset < len(transport):
        if len(transport) - offset < 2:
            raise ValueError("truncated compressed sector header")
        compressed_size = struct.unpack_from("<H", transport, offset)[0]
        sector_size = 2 + compressed_size
        if compressed_size == 0 or sector_size > RAW_SECTOR_SIZE + 16 or offset + sector_size > len(transport):
            raise ValueError("invalid compressed sector framing")
        yield offset, transport[offset : offset + sector_size]
        offset += sector_size
