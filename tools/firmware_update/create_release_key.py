from __future__ import annotations

import argparse
import getpass
import os
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_HEADER = ROOT / "include" / "firmware_update_public_key.h"


def public_header(point: bytes) -> str:
    rows = []
    for offset in range(0, len(point), 11):
        rows.append("    " + ", ".join(f"0x{value:02X}" for value in point[offset : offset + 11]) + ",")
    values = "\n".join(rows)
    return f'''#pragma once

#include <stdint.h>

// Public half of the offline ECDSA P-256 release key. The corresponding
// private key is intentionally stored outside this repository.
#define CYD_FIRMWARE_SIGNING_KEY_CONFIGURED 1

static constexpr uint8_t kFirmwareSigningPublicKey[65] = {{
{values}
}};
'''


def main() -> int:
    parser = argparse.ArgumentParser(description="Create the offline KAJO-Dash firmware release key")
    parser.add_argument("--private-key", type=Path, required=True,
                        help="new PEM path outside the repository")
    parser.add_argument("--public-header", type=Path, default=DEFAULT_HEADER,
                        help="generated firmware public-key header")
    parser.add_argument("--replace-public-header", action="store_true",
                        help="replace an existing configured trust anchor (requires a wired firmware flash)")
    parser.add_argument("--password-env", default="CYD_RELEASE_KEY_PASSWORD",
                        help="environment variable containing the PEM password; prompts when unset")
    args = parser.parse_args()

    private_path = args.private_key.resolve()
    header_path = args.public_header.resolve()
    if private_path == ROOT or ROOT in private_path.parents:
        parser.error("the private release key must be stored outside the repository")
    if private_path.exists():
        parser.error(f"refusing to overwrite existing private key: {private_path}")
    if (not args.replace_public_header and header_path.exists() and header_path.read_text(encoding="utf-8").find(
            "CYD_FIRMWARE_SIGNING_KEY_CONFIGURED 1") >= 0):
        parser.error(f"a configured public key already exists: {header_path}")

    password = os.environ.get(args.password_env)
    if password is None:
        password = getpass.getpass("New release-key password: ")
        confirmation = getpass.getpass("Confirm release-key password: ")
        if password != confirmation:
            parser.error("release-key passwords do not match")
    if len(password) < 12:
        parser.error("release-key password must contain at least 12 characters")

    private_key = ec.generate_private_key(ec.SECP256R1())
    private_pem = private_key.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8,
        serialization.BestAvailableEncryption(password.encode("utf-8")),
    )
    private_path.parent.mkdir(parents=True, exist_ok=True)
    with private_path.open("xb") as handle:
        handle.write(private_pem)
    if os.name != "nt":
        private_path.chmod(0o600)

    point = private_key.public_key().public_bytes(
        serialization.Encoding.X962,
        serialization.PublicFormat.UncompressedPoint,
    )
    header_path.parent.mkdir(parents=True, exist_ok=True)
    header_path.write_text(public_header(point), encoding="utf-8", newline="\n")
    print(f"Private release key: {private_path}")
    print(f"Firmware public key: {header_path}")
    print("Back up the private key securely; it cannot be recovered from the firmware.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
