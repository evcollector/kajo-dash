"""Package a signed KAJO-Dash release as a .kajofw bundle and describe it in kajo-update.json.

The bundle is an uncompressed ZIP of the three files sign_firmware.py writes --
the signed JSON manifest, the firmware image and the zlib transport -- plus the
project's licence and notice files, so a phone picks one file instead of three.
Nothing is signed or altered here and no key is needed: the display verifies
the signature before it writes anything.

kajo-update.json is the fixed-name index an app fetches from the newest GitHub
release, .../releases/latest/download/kajo-update.json, to learn whether a newer
firmware exists and what its bundle is called. Its format is in README.md beside
this script. It is refreshed for every release, but never rewound to an older one.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import zipfile
from pathlib import Path

from protocol import PROTOCOL_VERSION, TARGET_NAME, load_release_manifest
from release_notices import release_notice_files

ROOT = Path(__file__).resolve().parents[2]
CONFIG_HEADER = ROOT / "include" / "config.h"
INDEX_NAME = "kajo-update.json"
INDEX_FORMAT = 1

VERSION_CODE_RE = re.compile(r"#define\s+CYD_FIRMWARE_VERSION_CODE\s+(\d+)")
VERSION_NAME_RE = re.compile(r'#define\s+CYD_FIRMWARE_VERSION_NAME\s+"([^"]*)"')


def config_version_name(version_code: int) -> str | None:
    """The name in config.h, but only while config.h still describes this release."""
    try:
        text = CONFIG_HEADER.read_text(encoding="utf-8")
    except OSError:
        return None
    code, name = VERSION_CODE_RE.search(text), VERSION_NAME_RE.search(text)
    if code and name and int(code.group(1)) == version_code:
        return name.group(1)
    return None


def write_index(index_path: Path, bundle: Path, version_code: int, version_name: str) -> bool:
    """Write kajo-update.json for this bundle. Returns False if a newer release owns it."""
    if index_path.exists():
        try:
            existing = int(json.loads(index_path.read_text(encoding="utf-8"))["version_code"])
        except (ValueError, KeyError, OSError):
            existing = None
        if existing is not None and existing > version_code:
            print(f"Left {index_path.name} describing v{existing}: it is newer than v{version_code}.")
            return False

    index = {
        "format": INDEX_FORMAT,
        "target": TARGET_NAME,
        "protocol": PROTOCOL_VERSION,
        "version_code": version_code,
        "version_name": version_name,
        # The tag make_release.py gives this release. Downloading the bundle from
        # releases/download/<tag>/ keeps both files from one release even if a
        # newer one is published between the two requests.
        "tag": f"v{version_name}",
        "bundle": bundle.name,
        "bundle_size": bundle.stat().st_size,
        "bundle_sha256": hashlib.sha256(bundle.read_bytes()).hexdigest(),
    }
    index_path.write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"Update index: {index_path}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Package a signed KAJO-Dash release as a .kajofw bundle, and write kajo-update.json")
    parser.add_argument("manifest", type=Path, help="signed JSON produced by sign_firmware.py")
    parser.add_argument("--output", type=Path, help="output .kajofw path")
    parser.add_argument("--version-name",
                        help="display version name, e.g. 0.02 (default: include/config.h, "
                             "if its version code matches the manifest)")
    parser.add_argument("--no-index", action="store_true", help=f"do not write {INDEX_NAME}")
    args = parser.parse_args()

    manifest_path = args.manifest.resolve()
    release = load_release_manifest(manifest_path)
    output = (args.output or manifest_path.with_suffix(".kajofw")).resolve()
    if output.suffix.lower() != ".kajofw":
        parser.error("output must use the .kajofw extension")
    if output.exists():
        parser.error(f"refusing to overwrite release bundle: {output}")
    names = {manifest_path.name, release.image_path.name, release.transport_path.name}
    if len(names) != 3:
        parser.error("manifest, firmware, and transport filenames must be distinct")

    version_name = None
    if not args.no_index:
        version_name = args.version_name or config_version_name(release.version_code)
        if not version_name:
            parser.error(f"include/config.h is not at version code {release.version_code}; "
                         f"pass --version-name, or --no-index to skip {INDEX_NAME}")

    notices = release_notice_files()
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "x", compression=zipfile.ZIP_STORED) as archive:
        archive.write(manifest_path, manifest_path.name)
        archive.write(release.image_path, release.image_path.name)
        archive.write(release.transport_path, release.transport_path.name)
        for path, name in notices:
            archive.write(path, name)

    print(f"Release bundle: {output}")
    print(f"Firmware version: {release.version_code}")
    print(f"Bundle size: {output.stat().st_size} bytes")
    if version_name:
        write_index(output.parent / INDEX_NAME, output, release.version_code, version_name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
