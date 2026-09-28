from __future__ import annotations

import argparse
import base64
import getpass
import hashlib
import json
import os
import re
import struct
import zlib
from pathlib import Path

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature

from protocol import PROTOCOL_VERSION, RAW_SECTOR_SIZE, ReleaseManifest, TARGET_NAME

ROOT = Path(__file__).resolve().parents[2]
PUBLIC_HEADER = ROOT / "include" / "firmware_update_public_key.h"
CONFIG_HEADER = ROOT / "include" / "config.h"


def configured_version_code() -> int | None:
    """The version the firmware reports about itself.

    Typing it again on the command line is how a manifest ends up claiming one
    version while wrapping a binary that reports another: the display installs
    it, reboots reporting the lower number, and the next release then looks like
    a downgrade.
    """
    match = re.search(r"#define\s+CYD_FIRMWARE_VERSION_CODE\s+(\d+)",
                      CONFIG_HEADER.read_text(encoding="utf-8"))
    return int(match.group(1)) if match else None


def main() -> int:
    parser = argparse.ArgumentParser(description="Sign a KAJO-Dash firmware binary for Bluetooth upload")
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--private-key", type=Path, required=True)
    parser.add_argument("--version-code", type=int,
                        help="defaults to CYD_FIRMWARE_VERSION_CODE in include/config.h")
    parser.add_argument("--output", type=Path,
                        help="defaults to releases/firmware-v<version>.json")
    parser.add_argument("--password-env", default="CYD_RELEASE_KEY_PASSWORD",
                        help="environment variable containing the PEM password; prompts when unset")
    args = parser.parse_args()

    if args.version_code is None:
        args.version_code = configured_version_code()
        if args.version_code is None:
            parser.error(f"could not read CYD_FIRMWARE_VERSION_CODE from {CONFIG_HEADER}")
    if args.output is None:
        args.output = ROOT / "releases" / f"firmware-v{args.version_code}.json"

    if args.output.exists():
        parser.error(f"refusing to overwrite release manifest: {args.output}")
    output = args.output.resolve()
    release_image_path = output.with_suffix(".bin")
    transport_path = output.with_suffix(".zlib")
    source_image_path = args.firmware.resolve()
    if release_image_path.exists() and release_image_path != source_image_path:
        parser.error(f"refusing to overwrite release firmware: {release_image_path}")
    if transport_path.exists():
        parser.error(f"refusing to overwrite compressed transport: {transport_path}")
    if not 0 <= args.version_code <= 0xFFFFFFFF:
        parser.error("version-code must fit an unsigned 32-bit integer")
    configured = configured_version_code()
    if configured is not None and configured != args.version_code:
        parser.error(
            f"--version-code {args.version_code} does not match "
            f"CYD_FIRMWARE_VERSION_CODE {configured} in include/config.h; the signed "
            "manifest would claim a version the firmware does not report")
    image = source_image_path.read_bytes()
    digest = hashlib.sha256(image).digest()
    transport_parts = []
    for offset in range(0, len(image), RAW_SECTOR_SIZE):
        compressed = zlib.compress(image[offset : offset + RAW_SECTOR_SIZE], level=9)
        transport_parts.append(struct.pack("<H", len(compressed)) + compressed)
    transport = b"".join(transport_parts)
    transport_digest = hashlib.sha256(transport).digest()
    unsigned = ReleaseManifest(args.version_code, len(image), digest, len(transport), transport_digest,
                               bytes(64), release_image_path, transport_path)

    private_pem = args.private_key.read_bytes()
    password = os.environ.get(args.password_env)
    if password is None and b"ENCRYPTED" in private_pem:
        password = getpass.getpass("Release-key password: ")
    private_key = serialization.load_pem_private_key(
        private_pem, password=password.encode("utf-8") if password is not None else None)
    if not isinstance(private_key, ec.EllipticCurvePrivateKey) or not isinstance(private_key.curve, ec.SECP256R1):
        parser.error("private key must be ECDSA P-256 (secp256r1)")
    compiled_point = bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", PUBLIC_HEADER.read_text()))
    private_point = private_key.public_key().public_bytes(
        serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
    if len(compiled_point) != 65 or private_point != compiled_point:
        parser.error("private key does not match the public release key compiled into this firmware")
    der_signature = private_key.sign(unsigned.canonical_bytes(), ec.ECDSA(hashes.SHA256()))
    r, s = decode_dss_signature(der_signature)
    raw_signature = r.to_bytes(32, "big") + s.to_bytes(32, "big")

    image_reference = release_image_path.name
    transport_reference = transport_path.name
    payload = {
        "protocol": PROTOCOL_VERSION,
        "target": TARGET_NAME,
        "version_code": args.version_code,
        "image": image_reference,
        "size": len(image),
        "sha256": digest.hex(),
        "transport": transport_reference,
        "transport_size": len(transport),
        "transport_sha256": transport_digest.hex(),
        "transport_encoding": "zlib-sectors-4096",
        "signature_algorithm": "ecdsa-p256-sha256-raw",
        "signature": base64.b64encode(raw_signature).decode("ascii"),
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    if release_image_path != source_image_path:
        release_image_path.write_bytes(image)
    transport_path.write_bytes(transport)
    output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"Signed release manifest: {output}")
    print(f"Firmware: {release_image_path} ({len(image)} bytes)")
    print(f"SHA-256: {digest.hex()}")
    print(f"Compressed transport: {transport_path} ({len(transport)} bytes, {len(transport) * 100 / len(image):.1f}%)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
