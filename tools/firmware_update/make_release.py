"""Interactive release builder for KAJO-Dash firmware.

Run it from kajo.bat, or by double-clicking scripts/make_release.bat.
Every prompt offers the answer it worked out for itself in brackets, so a release where nothing unusual has
happened is a run of Enter presses.

Version numbers look after themselves. The next release is one past the
highest already released -- found from the local releases/ folder and from the
git tags this script leaves, so a fresh clone knows too -- and its name follows
the code: 1 is 0.01, 2 is 0.02, 100 is 1.00. Both are written into config.h,
the companion app's revision is reset to match when its private checkout is
present, and at the end the bump is committed and tagged locally (v0.02 and so
on). Nothing is pushed.

It never asks for, stores, or passes the signing-key password: sign_firmware.py
prompts for that itself, and the answer goes straight into the key.
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools" / "firmware_update"
CONFIG_HEADER = ROOT / "include" / "config.h"
RELEASES = ROOT / "releases"
FIRMWARE_BIN = ROOT / ".pio" / "build" / "kajo" / "firmware.bin"
UPLOADER_EXE = ROOT / "dist" / "firmware_update" / "KAJO Firmware Uploader.exe"
# Remembers the key's location, never its password. Gitignored: the path of an
# offline signing key is not something to publish.
SETTINGS = TOOLS / "release.local.json"

# The companion app is closed source, in a private repository checked out as
# android-companion/. Public clones do not have it.
APP_GRADLE = ROOT / "android-companion" / "app" / "build.gradle.kts"

VERSION_CODE_RE = re.compile(r"(#define\s+CYD_FIRMWARE_VERSION_CODE\s+)(\d+)(UL)?")
VERSION_NAME_RE = re.compile(r'(#define\s+CYD_FIRMWARE_VERSION_NAME\s+")([^"]*)(")')
APP_REVISION_RE = re.compile(r"^(val appRevision = )(\d+)", re.MULTILINE)
# Release tags carry this line in their message, which is how the version code
# is read back without depending on the tag's name.
TAG_CODE_RE = re.compile(r"^firmware version code (\d+)$", re.MULTILINE)


def version_name_for(code: int) -> str:
    return f"{code // 100}.{code % 100:02d}"


class Abort(Exception):
    """The operator declined, or something was wrong enough to stop."""


def rule(title: str) -> None:
    print(f"\n{title}\n{'-' * len(title)}")


def ask(prompt: str, default: str | None = None) -> str:
    """Prompt with a default in brackets; Enter accepts it."""
    suffix = f" [{default}]" if default is not None else ""
    try:
        answer = input(f"{prompt}{suffix}: ").strip()
    except (EOFError, KeyboardInterrupt):
        raise Abort("cancelled")
    if not answer:
        if default is None:
            raise Abort("cancelled")
        return default
    return answer


def confirm(prompt: str, default: bool = True) -> bool:
    answer = ask(f"{prompt} (y/n)", "y" if default else "n").lower()
    return answer.startswith("y")


def read_config() -> tuple[int, str]:
    text = CONFIG_HEADER.read_text(encoding="utf-8")
    code = VERSION_CODE_RE.search(text)
    name = VERSION_NAME_RE.search(text)
    if not code or not name:
        raise Abort(f"could not read the version fields from {CONFIG_HEADER}")
    return int(code.group(2)), name.group(2)


def write_config(code: int, name: str) -> None:
    text = CONFIG_HEADER.read_text(encoding="utf-8")
    text = VERSION_CODE_RE.sub(lambda m: f"{m.group(1)}{code}UL", text, count=1)
    text = VERSION_NAME_RE.sub(lambda m: f"{m.group(1)}{name}{m.group(3)}", text, count=1)
    CONFIG_HEADER.write_bytes(text.encode("utf-8"))


def reset_app_revision() -> bool:
    """The app's versionCode is firmware code * 100 + revision; a new firmware
    code starts the revision again. Returns whether the file changed."""
    if not APP_GRADLE.is_file():
        return False
    text = APP_GRADLE.read_text(encoding="utf-8")
    match = APP_REVISION_RE.search(text)
    if not match or match.group(2) == "0":
        return False
    APP_GRADLE.write_bytes(APP_REVISION_RE.sub(r"\g<1>0", text, count=1).encode("utf-8"))
    return True


def run(command: list[str], what: str) -> None:
    print(f"\n$ {subprocess.list2cmdline(command)}\n")
    if subprocess.run(command, cwd=ROOT).returncode != 0:
        raise Abort(f"{what} failed")


def released_versions() -> set[int]:
    found = set()
    for manifest in sorted(RELEASES.glob("*.json")):
        try:
            found.add(int(json.loads(manifest.read_text(encoding="utf-8"))["version_code"]))
        except (ValueError, KeyError, OSError):
            continue
    return found


def git(*args: str) -> str:
    result = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else ""


def tagged_versions() -> set[int]:
    messages = git("for-each-ref", "refs/tags", "--format=%(contents)")
    return {int(code) for code in TAG_CODE_RE.findall(messages)}


def choose_version(code: int, name: str) -> tuple[int, str, bool]:
    """Return the version to release, and whether config.h has to change."""
    published = released_versions() | tagged_versions()
    if published:
        print(f"Already released: {', '.join('v%d' % v for v in sorted(published))}")
    suggested = code
    if code in published:
        suggested = max(published) + 1
        print(f"v{code} has been released before, so v{suggested} is suggested instead.")

    while True:
        chosen = ask("Version code", str(suggested))
        if not chosen.isdigit():
            print("  The version code is a whole number.")
            continue
        chosen_code = int(chosen)
        if chosen_code in published and not confirm(
                f"  v{chosen_code} was released before. Sign it again anyway?", False):
            continue
        if chosen_code < code:
            # The display treats a lower code as a downgrade and asks the rider
            # to confirm it physically, so this is worth saying out loud.
            print(f"  Note: lower than the current {code}. The display will treat it "
                  "as a downgrade and ask for confirmation on the panel.")
            if not confirm("  Continue?", False):
                continue
        break

    # Keep a hand-picked name while the code stays put; a new code gets the
    # name that goes with it.
    default_name = name if chosen_code == code else version_name_for(chosen_code)
    chosen_name = ask("Version name (shown on the display)", default_name)
    changed = chosen_code != code or chosen_name != name
    return chosen_code, chosen_name, changed


def record_release(code: int, name: str, bumped: list[Path], tree_was_clean: bool) -> None:
    """Commit the version bump and tag the release. Local only: nothing is pushed."""
    rule("Record")
    tag = f"v{name}"
    if bumped:
        paths = [str(path.relative_to(ROOT)) for path in bumped]
        if confirm(f"Commit {', '.join(paths)} as \"Release KAJO-Dash {name}\"?", True):
            # With paths, git commits only those files, whatever else is staged.
            run(["git", "commit", "-m", f"Release KAJO-Dash {name}", "--", *paths], "commit")
        else:
            print("  Left uncommitted, so not tagged either: the tag would point at a "
                  "commit that does not contain this version.")
            return

    if not tree_was_clean:
        # The warning at the start applies: HEAD is not what was built.
        print(f"Not tagged: the release was built with uncommitted changes, so no commit "
              f"matches it. Commit them and tag it yourself with: git tag -a {tag}")
        return
    if git("tag", "-l", tag):
        print(f"Tag {tag} already exists; left as it is.")
        return
    if confirm(f"Tag this commit as {tag}?", True):
        run(["git", "tag", "-a", tag, "-m",
             f"KAJO-Dash {name}\n\nfirmware version code {code}"], "tag")
        print(f"  Tagged locally. Publishing it is up to you: git push origin {tag}")


def choose_key() -> Path:
    remembered = None
    if SETTINGS.is_file():
        try:
            remembered = json.loads(SETTINGS.read_text(encoding="utf-8")).get("private_key")
        except (ValueError, OSError):
            remembered = None

    while True:
        answer = ask("Signing key (.pem)", remembered)
        key = Path(answer).expanduser()
        if not key.is_file():
            print(f"  Not found: {key}")
            continue
        if ROOT in key.resolve().parents:
            print("  That path is inside the repository. The signing key belongs outside it.")
            continue
        if str(key) != remembered:
            SETTINGS.write_text(json.dumps({"private_key": str(key)}, indent=2) + "\n",
                                encoding="utf-8")
            print(f"  Remembered in {SETTINGS.name} (the path only, never the password).")
        return key


def main() -> int:
    print("KAJO-Dash release builder")
    code, name = read_config()

    branch = git("rev-parse", "--abbrev-ref", "HEAD")
    dirty = git("status", "--porcelain")
    print(f"\nBranch:   {branch or 'unknown'}")
    print(f"Version:  v{code}  \"{name}\"")
    if dirty:
        # A release built from a dirty tree cannot be reproduced from a commit.
        print(f"\nWARNING: {len(dirty.splitlines())} uncommitted change(s). A release built now "
              "cannot be traced back to a commit.")
        if not confirm("Continue anyway?", False):
            raise Abort("cancelled")

    rule("Version")
    new_code, new_name, changed = choose_version(code, name)
    bumped: list[Path] = []
    if changed:
        if not confirm(f"Write v{new_code} \"{new_name}\" into include/config.h?"):
            raise Abort("cancelled")
        write_config(new_code, new_name)
        bumped.append(CONFIG_HEADER)
        print("  Updated include/config.h.")
        if new_code != code and reset_app_revision():
            bumped.append(APP_GRADLE)
            print("  Reset the companion app's appRevision to 0 for the new firmware.")

    rule("Key")
    key = choose_key()

    rule("Build")
    build = confirm("Rebuild the firmware first?", True)
    if build:
        run([str(Path.home() / ".platformio" / "penv" / "Scripts" / "platformio.exe"),
             "run", "-e", "kajo"], "firmware build")
    if not FIRMWARE_BIN.is_file():
        raise Abort(f"no firmware binary at {FIRMWARE_BIN}; build it first")

    rule("Sign")
    print("sign_firmware.py will ask for the key password. It is not stored anywhere.")
    manifest = RELEASES / f"firmware-v{new_code}.json"
    if manifest.exists():
        raise Abort(f"{manifest} already exists; move or delete it first")
    run([sys.executable, str(TOOLS / "sign_firmware.py"),
         "--firmware", str(FIRMWARE_BIN), "--private-key", str(key)], "signing")

    rule("Package")
    # Straight after signing, while .pio/build/kajo still holds the signed
    # build: the USB layout takes that build's bootloader and partition table.
    run([sys.executable, str(TOOLS / "package_usb.py"), str(manifest)], "USB layout")
    artifacts = [manifest, manifest.with_name(f"{manifest.stem}-usb.json")]
    if confirm("Build the .kajofw phone bundle and kajo-update.json?", True):
        run([sys.executable, str(TOOLS / "package_kajofw.py"), str(manifest),
             "--version-name", new_name], ".kajofw packaging")
        artifacts += [manifest.with_suffix(".kajofw"), RELEASES / "kajo-update.json"]

    if confirm("Build the Windows updater ZIP?", True):
        if not UPLOADER_EXE.is_file():
            print(f"\nThe standalone uploader is missing: {UPLOADER_EXE}")
            if confirm("Build it now? (slow, needs PyInstaller)", True):
                run([str(TOOLS / "build_windows_uploader.bat")], "uploader build")
        if UPLOADER_EXE.is_file():
            run([sys.executable, str(TOOLS / "package_windows_release.py"),
                 "--manifest", str(manifest)], "Windows packaging")
            artifacts.append(ROOT / "dist" / f"KAJO-Dash-Firmware-v{new_code}-Windows.zip")
        else:
            print("Skipped the Windows ZIP: no uploader executable.")

    record_release(new_code, new_name, bumped, tree_was_clean=not dirty)

    rule("Done")
    for path in artifacts:
        if path.exists():
            print(f"  {path}  ({path.stat().st_size:,} bytes)")
    print("\nNothing here contains the signing key.")
    # Guarded: with no paths, git status would report the whole tree.
    if bumped and git("status", "--porcelain", "--", *(str(p) for p in bumped)):
        print("The version bump is still uncommitted -- commit it, or the next build "
              "disagrees with what you just signed.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Abort as stop:
        print(f"\nStopped: {stop}")
        raise SystemExit(1)
