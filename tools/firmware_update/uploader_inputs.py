"""What the standalone uploader EXE is built from, so a stale one is never shipped.

The release builder packages dist/firmware_update/KAJO Firmware Uploader.exe
into the Windows ZIP. An EXE left over from before an uploader change would
ship the old uploader silently, so build_windows_uploader.bat stamps each EXE it
builds with a fingerprint of its inputs, and the release builder rebuilds
whenever the stamp is missing or no longer matches.

The inputs are upload_firmware.py and every module beside it that it imports,
directly or through another, requirements.txt and the build script, and the
installed versions of the packages PyInstaller bundles from this environment:
the requirements, PyInstaller itself, and their dependencies. The stamp also holds the EXE's own hash, so an EXE
built some other way never passes for a stamped one.

    python uploader_inputs.py            exit 0 if the EXE is current, 1 if not
    python uploader_inputs.py --stamp    record the EXE as built from the current inputs
"""

from __future__ import annotations

import argparse
import ast
import hashlib
import re
from importlib import metadata
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parents[1]
ENTRY = TOOLS / "upload_firmware.py"
REQUIREMENTS = TOOLS / "requirements.txt"
BUILD_FILES = (REQUIREMENTS, TOOLS / "build_windows_uploader.bat")
EXE = ROOT / "dist" / "firmware_update" / "KAJO Firmware Uploader.exe"


def local_modules(entry: Path = ENTRY) -> list[Path]:
    """entry, and every module in its directory that it imports, directly or not."""
    found: list[Path] = []
    pending = [entry.resolve()]
    while pending:
        path = pending.pop()
        if path in found:
            continue
        found.append(path)
        for node in ast.walk(ast.parse(path.read_text(encoding="utf-8"), filename=str(path))):
            if isinstance(node, ast.Import):
                names = [alias.name for alias in node.names]
            elif isinstance(node, ast.ImportFrom) and node.level == 0 and node.module:
                names = [node.module]
            else:
                continue
            for name in names:
                candidate = path.parent / f"{name.split('.')[0]}.py"
                if candidate.is_file():
                    pending.append(candidate.resolve())
    return sorted(found)


def requirement_name(requirement: str) -> str:
    return re.split(r"[\s<>=!~;\[(]", requirement.strip(), maxsplit=1)[0]


def bundled_packages(requirements: Path = REQUIREMENTS) -> list[str]:
    """name==version of every installed package the EXE can be built from:
    the uploader's requirements and PyInstaller, and everything they depend on.
    Tools installed beside them, such as pytest, do not count."""
    pending = [requirement_name(line) for line in requirements.read_text(encoding="utf-8").splitlines()
               if line.strip() and not line.lstrip().startswith("#")] + ["pyinstaller"]
    versions: dict[str, str] = {}
    while pending:
        name = pending.pop()
        key = re.sub(r"[-_.]+", "-", name).lower()
        if key in versions:
            continue
        try:
            dist = metadata.distribution(name)
        except metadata.PackageNotFoundError:
            versions[key] = "missing"
            continue
        versions[key] = dist.version
        pending += [requirement_name(requirement) for requirement in dist.requires or []
                    if "extra ==" not in requirement]
    return sorted(f"{key}=={version}" for key, version in versions.items())


def fingerprint(entry: Path = ENTRY, build_files: tuple[Path, ...] = BUILD_FILES,
                requirements: Path = REQUIREMENTS) -> str:
    digest = hashlib.sha256()
    for path in local_modules(entry) + sorted(build_files):
        digest.update(path.name.encode("utf-8") + b"\0")
        # Line endings are the checkout's business, not the EXE's.
        digest.update(path.read_bytes().replace(b"\r\n", b"\n") + b"\0")
    digest.update("\n".join(bundled_packages(requirements)).encode("utf-8"))
    return digest.hexdigest()


def stamp_path(exe: Path) -> Path:
    return exe.with_suffix(".inputs")


def stamp_for(exe: Path, **inputs) -> str:
    return f"{fingerprint(**inputs)}\n{hashlib.sha256(exe.read_bytes()).hexdigest()}\n"


def is_current(exe: Path = EXE, **inputs) -> bool:
    stamp = stamp_path(exe)
    if not exe.is_file() or not stamp.is_file():
        return False
    return stamp.read_text(encoding="utf-8") == stamp_for(exe, **inputs)


def write_stamp(exe: Path = EXE, **inputs) -> None:
    stamp_path(exe).write_text(stamp_for(exe, **inputs), encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description="Check or record what the uploader EXE was built from")
    parser.add_argument("--stamp", action="store_true", help="record the EXE as built from the current inputs")
    parser.add_argument("--exe", type=Path, default=EXE)
    args = parser.parse_args()
    if args.stamp:
        write_stamp(args.exe)
        print(f"Recorded the uploader's inputs: {stamp_path(args.exe)}")
        return 0
    current = is_current(args.exe)
    print("The uploader EXE is current." if current else "The uploader EXE is missing or out of date.")
    return 0 if current else 1


if __name__ == "__main__":
    raise SystemExit(main())
