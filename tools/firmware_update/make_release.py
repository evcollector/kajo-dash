"""Release builder for KAJO-Dash firmware.

Run it from kajo.bat (option 5), or by double-clicking scripts/make_release.bat.

include/config.h holds the version being worked towards, and the builder never
works a version out for itself: it packages whatever config.h says, in one of
two ways.

- Test package: the firmware built in the kajo_test_package environment, so the
  display shows "0.01-test", signed and packaged into releases/test/ for your
  own boards, replacing the previous test package. Nothing is committed or
  tagged and the version stays as it is, so make as many as you like.
- Release: the firmware as it will be published, signed and packaged into
  releases/ and dist/. It is added to RELEASES.md, committed and tagged (v0.01),
  and then config.h moves on to the next version (0.02) in a second commit, so
  test packages from then on carry the version they lead up to. A release needs
  a clean tree, so its tag matches what was built, and a version above the last
  one in RELEASES.md. Nothing is pushed.

Either way it works in three phases:

1. Check, changing nothing: the signing key, PlatformIO and the Arduino core,
   the Python tests, CHANGELOG.md against the header the display compiles in,
   whether the uploader EXE is current, and for a release the tree, the version
   and the files it would write. Then it prints the plan. A release asks once
   before going ahead; a test package goes ahead unless the plan has warnings.
2. Unlock. It asks for the key password and checks that it opens the key and
   that the key is the pair of the public key compiled into the firmware, so a
   typo is caught before the build instead of after it. The key is held in this
   process's memory for the signing step; the password is never stored, logged
   or handed to another process.
3. Build. It rebuilds the uploader EXE if its inputs changed, then builds, signs
   and packages into a staging folder under .pio/, and moves the result into
   place only when every step has succeeded. If a step fails first, the files it
   edited are put back and the staging folder is deleted, so running it again
   starts clean.

For an unattended run, pass --test or --release together with --yes; the
CYD_RELEASE_KEY_PASSWORD environment variable supplies the password.
"""

from __future__ import annotations

import argparse
import datetime
import getpass
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

import package_usb
import sign_firmware
import uploader_inputs

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools" / "firmware_update"
CONFIG_HEADER = ROOT / "include" / "config.h"
HISTORY = ROOT / "RELEASES.md"
RELEASES = ROOT / "releases"
TEST_RELEASES = RELEASES / "test"
DIST = ROOT / "dist"
PIO_BUILD = ROOT / ".pio" / "build"
# Inside the checkout, so moving a finished package out of it is a rename.
STAGING = ROOT / ".pio" / "release-staging"
UPLOADER_EXE = uploader_inputs.EXE
INDEX_NAME = "kajo-update.json"
CHANGELOG = ROOT / "CHANGELOG.md"
CHANGELOG_TOOL = ROOT / "tools" / "generate_changelog_header.py"
CHANGELOG_HEADER = ROOT / "src" / "lvgl_app" / "firmware_changelog.h"
PASSWORD_ENV = "CYD_RELEASE_KEY_PASSWORD"
# Remembers the key's location, never its password. Gitignored: the path of an
# offline signing key is not something to publish.
SETTINGS = TOOLS / "release.local.json"

# The companion app is closed source, in a private repository checked out as
# android-companion/. Public clones do not have it, and this repository ignores
# it, so its revision reset is left for that repository to commit.
APP_GRADLE = ROOT / "android-companion" / "app" / "build.gradle.kts"

TEST = "test"
RELEASE = "release"
# PlatformIO environments. The test one adds CYD_FIRMWARE_TEST_BUILD, which
# makes the display show "-test" after its version.
ENVIRONMENTS = {TEST: "kajo_test_package", RELEASE: "kajo"}

VERSION_CODE_RE = re.compile(r"(#define\s+CYD_FIRMWARE_VERSION_CODE\s+)(\d+)(UL)?")
VERSION_NAME_RE = re.compile(r'(#define\s+CYD_FIRMWARE_VERSION_NAME\s+")([^"]*)(")')
APP_REVISION_RE = re.compile(r"^(val appRevision = )(\d+)", re.MULTILINE)
CHANGELOG_ENTRY_RE = re.compile(r"^##\s+(\d{4}-\d{2}-\d{2})\s+[—-]\s+(.+?)\s*$", re.MULTILINE)
# A row of the table in RELEASES.md: | 0.01 | 1 | 2026-09-28 | `<sha-256>` |
HISTORY_ROW_RE = re.compile(
    r"^\|\s*(?P<name>[^|\s]+)\s*\|\s*(?P<code>\d+)\s*\|\s*(?P<date>\d{4}-\d{2}-\d{2})\s*\|"
    r"\s*`?(?P<sha256>[0-9a-f]{64})`?\s*\|\s*$", re.MULTILINE)


def version_name_for(code: int) -> str:
    return f"{code // 100}.{code % 100:02d}"


class Abort(Exception):
    """The operator declined, or something was wrong enough to stop."""


def rule(title: str) -> None:
    print(f"\n{title}\n{'-' * len(title)}")


def shown(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return str(path)


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


@dataclass(frozen=True)
class Release:
    name: str
    code: int
    date: str
    sha256: str


def read_history() -> list[Release]:
    """The releases in RELEASES.md, oldest first."""
    if not HISTORY.is_file():
        return []
    return [Release(match["name"], int(match["code"]), match["date"], match["sha256"])
            for match in HISTORY_ROW_RE.finditer(HISTORY.read_text(encoding="utf-8"))]


def append_history(release: Release) -> None:
    """Add a row to the table that ends RELEASES.md."""
    text = HISTORY.read_text(encoding="utf-8")
    if not text.endswith("\n"):
        text += "\n"
    text += f"| {release.name} | {release.code} | {release.date} | `{release.sha256}` |\n"
    HISTORY.write_text(text, encoding="utf-8", newline="\n")


def app_revision_needs_reset() -> bool:
    """The app's versionCode is firmware code * 100 + revision; a new firmware
    code starts the revision again."""
    if not APP_GRADLE.is_file():
        return False
    match = APP_REVISION_RE.search(APP_GRADLE.read_text(encoding="utf-8"))
    return bool(match) and match.group(2) != "0"


def reset_app_revision() -> None:
    text = APP_GRADLE.read_text(encoding="utf-8")
    APP_GRADLE.write_bytes(APP_REVISION_RE.sub(r"\g<1>0", text, count=1).encode("utf-8"))


def run(command: list[str], what: str) -> None:
    print(f"\n$ {subprocess.list2cmdline(command)}\n")
    if subprocess.run(command, cwd=ROOT).returncode != 0:
        raise Abort(f"{what} failed")


def git(*args: str) -> str:
    result = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    return result.stdout.strip() if result.returncode == 0 else ""


def release_outputs(code: int) -> list[Path]:
    """Every file a release of this version writes, apart from the shared index."""
    stem = f"firmware-v{code}"
    names = [f"{stem}{suffix}" for suffix in (
        ".json", ".bin", ".zlib", ".kajofw",
        "-usb.json", "-bootloader.bin", "-partitions.bin", "-boot_app0.bin")]
    return [RELEASES / name for name in names] + [DIST / f"KAJO-Dash-Firmware-v{code}-Windows.zip"]


def find_platformio() -> str | None:
    for name in ("pio", "platformio"):
        found = shutil.which(name)
        if found:
            return found
    # The PlatformIO IDE extension installs the CLI without putting it on PATH.
    for name in ("platformio.exe", "pio.exe", "platformio", "pio"):
        candidate = Path.home() / ".platformio" / "penv" / ("Scripts" if os.name == "nt" else "bin") / name
        if candidate.is_file():
            return str(candidate)
    return None


def key_problem(key: Path) -> str | None:
    if not key.is_file():
        return f"signing key not found: {key}"
    if ROOT in key.resolve().parents:
        return f"the signing key is inside the repository ({key}); it belongs outside it"
    return None


def choose_key(override: Path | None, interactive: bool) -> tuple[Path | None, str | None]:
    """The signing key, or why there is none. Remembers a newly chosen path."""
    remembered = None
    if SETTINGS.is_file():
        try:
            remembered = json.loads(SETTINGS.read_text(encoding="utf-8")).get("private_key")
        except (ValueError, OSError):
            remembered = None

    candidate = override or (Path(remembered) if remembered else None)
    problem = key_problem(candidate.expanduser()) if candidate else "no signing key has been chosen yet"
    while problem:
        if not interactive:
            return None, f"{problem}; pass --key PATH"
        print(f"  {problem[0].upper()}{problem[1:]}.")
        candidate = Path(ask("Signing key (.pem)"))
        problem = key_problem(candidate.expanduser())

    key = candidate.expanduser()
    if str(key) != remembered:
        SETTINGS.write_text(json.dumps({"private_key": str(key)}, indent=2) + "\n", encoding="utf-8")
        print(f"  Remembered the key's location in {SETTINGS.name} (the path only, never the password).")
    return key, None


def run_tests() -> tuple[bool, str]:
    """The Python test suite: whether it passed, and a summary or the failure."""
    result = subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", "test"],
                            cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace",
                            env=dict(os.environ, PYTHONIOENCODING="utf-8"))
    output = (result.stdout + result.stderr).strip()
    ran = re.search(r"^Ran (\d+) tests?", output, re.MULTILINE)
    if result.returncode == 0:
        return True, f"{ran.group(1) if ran else 'all'} passed"
    return False, "\n".join(output.splitlines()[-20:])


def changelog_status(last_tag: str | None) -> tuple[bool, str | None, str | None]:
    """Whether firmware_changelog.h is behind CHANGELOG.md, a problem that stops
    the run, and a note when CHANGELOG.md has nothing since last_tag."""
    check = subprocess.run([sys.executable, str(CHANGELOG_TOOL), "--check"], cwd=ROOT,
                           capture_output=True, text=True, encoding="utf-8", errors="replace")
    if check.returncode not in (0, 1):
        return False, (check.stderr.strip() or "CHANGELOG.md could not be read"), None
    stale = check.returncode == 1

    note = None
    entries = CHANGELOG_ENTRY_RE.findall(CHANGELOG.read_text(encoding="utf-8"))
    if last_tag and entries:
        previous = set(CHANGELOG_ENTRY_RE.findall(git("show", f"{last_tag}:CHANGELOG.md")))
        if entries[0] in previous:
            note = (f"CHANGELOG.md has no entry since {last_tag}, so the display's changelog "
                    "will not mention this release.")
    return stale, None, note


@dataclass
class Plan:
    mode: str
    code: int
    name: str
    branch: str = "unknown"
    dirty: list[str] = field(default_factory=list)
    last_release: Release | None = None
    key: Path | None = None
    platformio: str | None = None
    tests: tuple[bool, str] = (True, "not run")
    uploader_current: bool = True
    changelog_stale: bool = False
    reset_app: bool = False
    problems: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)

    @property
    def is_release(self) -> bool:
        return self.mode == RELEASE

    @property
    def label(self) -> str:
        """The version as the display will show it."""
        return self.name if self.is_release else f"{self.name}-test"

    @property
    def tag(self) -> str:
        return f"v{self.name}"

    @property
    def build_dir(self) -> Path:
        return PIO_BUILD / ENVIRONMENTS[self.mode]

    @property
    def next_code(self) -> int:
        return self.code + 1

    @property
    def next_name(self) -> str:
        return version_name_for(self.next_code)


def check(mode: str, key: Path | None = None, interactive: bool = True,
          tests: tuple[bool, str] | None = None) -> Plan:
    """Work out the whole run and everything that would stop it, changing nothing."""
    code, name = read_config()
    plan = Plan(mode, code, name)
    plan.branch = git("rev-parse", "--abbrev-ref", "HEAD") or "unknown"
    plan.dirty = git("status", "--porcelain").splitlines()
    history = read_history()
    plan.last_release = max(history, key=lambda release: release.code) if history else None
    already_released = plan.last_release is not None and code <= plan.last_release.code

    if plan.is_release:
        if plan.dirty:
            plan.problems.append(f"{len(plan.dirty)} uncommitted change(s). Commit or stash them first, so the "
                                 "tag matches what was built.")
        if already_released:
            plan.problems.append(f"include/config.h is at {name} (code {code}), but RELEASES.md already has "
                                 f"{plan.last_release.name} (code {plan.last_release.code}). Set the next "
                                 "version in include/config.h.")
        if not HISTORY.is_file():
            plan.problems.append(f"{shown(HISTORY)} is missing; restore it from git, since the release "
                                 "is recorded there.")
        if git("tag", "-l", plan.tag):
            plan.problems.append(f"tag {plan.tag} already exists.")
        for path in release_outputs(code):
            if path.exists():
                plan.problems.append(f"{shown(path)} already exists; move or delete it first.")
        plan.reset_app = app_revision_needs_reset()
    elif already_released:
        plan.warnings.append(f"include/config.h is still at {name}, which RELEASES.md lists as released; "
                             "this test package would carry the released version code.")

    plan.key, problem = choose_key(key, interactive)
    if problem:
        plan.problems.append(problem)
    plan.platformio = find_platformio()
    if not plan.platformio:
        plan.problems.append("PlatformIO was not found. Install PlatformIO Core or the PlatformIO IDE "
                             "extension, then run this again.")
    if not package_usb.BOOT_APP0.is_file():
        plan.problems.append(f"the Arduino core's boot_app0.bin is missing ({package_usb.BOOT_APP0}); "
                             "build the firmware once with PlatformIO to install it.")
    plan.uploader_current = uploader_inputs.is_current(UPLOADER_EXE)

    last_tag = f"v{plan.last_release.name}" if plan.last_release else None
    plan.changelog_stale, problem, note = changelog_status(last_tag)
    if problem:
        plan.problems.append(problem)
    if note and plan.is_release:
        plan.notes.append(note)

    if tests is None:
        print("Running the Python tests...")
        tests = run_tests()
    plan.tests = tests
    if not tests[0]:
        plan.problems.append("the Python tests fail:\n" + tests[1])
    return plan


def print_plan(plan: Plan) -> None:
    rule(f"Release {plan.name}" if plan.is_release else f"Test package {plan.label}")
    last = plan.last_release
    stem = f"firmware-v{plan.code}"
    rows = [
        ("Version", f"{plan.name} (code {plan.code}), from include/config.h" +
         ("" if plan.is_release else f"; the display shows {plan.label}")),
        ("Last release", f"{last.name} (code {last.code}), {last.date}" if last else "none yet"),
        ("Branch", f"{plan.branch}, " + (f"{len(plan.dirty)} uncommitted change(s)" if plan.dirty else "clean")),
        ("Signing key", str(plan.key) if plan.key else "none"),
        ("PlatformIO", f"{plan.platformio or 'not found'}, environment {ENVIRONMENTS[plan.mode]}"),
        ("Tests", plan.tests[1] if plan.tests[0] else "failing"),
        ("Uploader EXE", "up to date" if plan.uploader_current
         else "rebuilt first: it is missing, or its inputs changed since it was built"),
    ]
    if plan.changelog_stale:
        rows.append(("Changelog", "firmware_changelog.h regenerated from CHANGELOG.md" +
                     (" and committed" if plan.is_release else ", left uncommitted")))
    else:
        rows.append(("Changelog", "firmware_changelog.h matches CHANGELOG.md"))
    if plan.is_release:
        rows += [
            ("Writes", f"releases\\{stem}.json, .bin and .zlib, signed"),
            ("", f"releases\\{stem}.kajofw and releases\\{INDEX_NAME}"),
            ("", f"releases\\{stem}-usb.json and its bootloader, partition and boot_app0 images"),
            ("", f"dist\\KAJO-Dash-Firmware-v{plan.code}-Windows.zip"),
            ("Records", f'a RELEASES.md row, commit "Release KAJO-Dash {plan.name}" and tag {plan.tag};'),
            ("", f'then include/config.h moves to {plan.next_name} (code {plan.next_code}), '
                 f'commit "Start KAJO-Dash {plan.next_name}"'),
            ("", "Nothing is pushed."),
        ]
        if plan.reset_app:
            rows.append(("Companion app", "appRevision reset to 0 with the move, left for its own repository "
                                          "to commit"))
    else:
        rows += [
            ("Writes", "releases\\test\\, replacing the previous test package: the signed firmware,"),
            ("", "its .kajofw, USB layout and Windows ZIP"),
            ("Records", f"nothing: no commit, no tag, and include/config.h stays at {plan.name}"),
        ]
    for label, value in rows:
        print(f"  {label:<14}{value}")
    for warning in plan.warnings:
        print(f"\n  Warning: {warning}")
    for note in plan.notes:
        print(f"\n  Note: {note}")
    for problem in plan.problems:
        print(f"\n  Cannot continue: {problem}")


def choose_mode() -> str:
    code, name = read_config()
    print(f"\nWorking version: {name} (code {code}), from include/config.h\n")
    print(f"  T  Test package   {name}-test on the display, for your own boards; nothing is committed or tagged")
    print(f"  R  Release        {name}, recorded in RELEASES.md and tagged v{name}; "
          f"config.h then moves to {version_name_for(code + 1)}")
    answer = ask("\nTest package or release? (t/r)", "t").lower()
    if answer.startswith("t"):
        return TEST
    if answer.startswith("r"):
        return RELEASE
    raise Abort("cancelled")


def confirm(plan: Plan, assume_yes: bool) -> None:
    """A release is confirmed; a test package only when the plan has warnings."""
    if assume_yes:
        if plan.warnings:
            raise Abort("--yes only goes ahead when the plan has no warnings")
        return
    if not plan.is_release and not plan.warnings:
        return
    if plan.warnings:
        prompt, default = "Go ahead despite the warnings? [y/N]: ", "n"
    else:
        prompt, default = f"Release {plan.name}? [Y/n]: ", "y"
    try:
        answer = input(f"\n{prompt}").strip().lower() or default
    except (EOFError, KeyboardInterrupt):
        raise Abort("cancelled")
    if not answer.startswith("y"):
        raise Abort("cancelled")


def unlock_key(key: Path):
    """Open the signing key now, so a mistyped password costs nothing."""
    supplied = os.environ.get(PASSWORD_ENV)
    prompt = supplied is None and sign_firmware.key_needs_password(key)
    for _ in range(3 if prompt else 1):
        try:
            password = getpass.getpass("Release-key password: ") if prompt else supplied
        except EOFError:
            raise Abort("cancelled")
        try:
            private_key = sign_firmware.load_private_key(key, password)
        except sign_firmware.ReleaseKeyError as exc:
            raise Abort(str(exc))
        except ValueError as exc:
            if not prompt:
                raise Abort(f"the key could not be opened: {exc}")
            print("  That password does not open the key.")
            continue
        print("  Key opened; it matches the public key compiled into the firmware.")
        return private_key
    raise Abort("the key password was not accepted")


class FileEdits:
    """Original contents of the files a run edits, to put back if it fails."""

    def __init__(self) -> None:
        self.originals: dict[Path, bytes] = {}

    def save(self, path: Path) -> None:
        self.originals.setdefault(path, path.read_bytes())

    def restore(self) -> None:
        for path, data in self.originals.items():
            path.write_bytes(data)


def promote_release() -> list[Path]:
    """Move the staged release into releases/ and dist/, all of it or none."""
    moves = []
    for staged in sorted(STAGING.iterdir(), key=lambda path: (path.name == INDEX_NAME, path.name)):
        target = (DIST if staged.suffix == ".zip" else RELEASES) / staged.name
        # The index is shared by every release and is meant to be replaced; the
        # packager has already refused to rewind it to an older version.
        if target.exists() and staged.name != INDEX_NAME:
            raise Abort(f"{shown(target)} appeared during the build; nothing was moved")
        moves.append((staged, target))
    done = []
    try:
        # The index moves last, so an earlier failure never leaves it replaced.
        for staged, target in moves:
            target.parent.mkdir(parents=True, exist_ok=True)
            os.replace(staged, target)
            done.append((staged, target))
    except OSError:
        for staged, target in reversed(done):
            os.replace(target, staged)
        raise
    return [target for _, target in moves]


def promote_test() -> list[Path]:
    """Replace the previous test package with the staged one."""
    shutil.rmtree(TEST_RELEASES, ignore_errors=True)
    if TEST_RELEASES.exists():
        raise Abort(f"the previous test package in {shown(TEST_RELEASES)} could not be removed; "
                    "close anything that has its files open")
    TEST_RELEASES.parent.mkdir(parents=True, exist_ok=True)
    os.replace(STAGING, TEST_RELEASES)
    return sorted(TEST_RELEASES.iterdir())


def execute(plan: Plan, private_key) -> list[Path]:
    """Build, sign and package in STAGING, then move the result into place."""
    shutil.rmtree(STAGING, ignore_errors=True)
    STAGING.mkdir(parents=True)
    edits = FileEdits()
    try:
        if not plan.uploader_current:
            rule("Uploader EXE")
            run([str(TOOLS / "build_windows_uploader.bat")], "uploader build")
            if not uploader_inputs.is_current(UPLOADER_EXE):
                raise Abort("the rebuilt uploader EXE did not record its inputs")
        if plan.changelog_stale:
            edits.save(CHANGELOG_HEADER)
            run([sys.executable, str(CHANGELOG_TOOL)], "changelog header")

        rule("Build")
        run([plan.platformio, "run", "-e", ENVIRONMENTS[plan.mode]], "firmware build")

        rule("Sign")
        manifest = STAGING / f"firmware-v{plan.code}.json"
        sign_firmware.sign_release(plan.build_dir / "firmware.bin", private_key, plan.code, manifest)

        rule("Package")
        run([sys.executable, str(TOOLS / "package_usb.py"), str(manifest),
             "--build-dir", str(plan.build_dir), "--boot-app0", str(package_usb.BOOT_APP0)], "USB layout")
        if plan.is_release:
            # The packager refuses to rewind the shared index, which it can
            # only do if it sees the current one.
            if (RELEASES / INDEX_NAME).is_file():
                shutil.copy2(RELEASES / INDEX_NAME, STAGING / INDEX_NAME)
            run([sys.executable, str(TOOLS / "package_kajofw.py"), str(manifest),
                 "--version-name", plan.name], ".kajofw packaging")
            archive = f"KAJO-Dash-Firmware-v{plan.code}-Windows.zip"
        else:
            # kajo-update.json is what phones check for updates: never for a test package.
            run([sys.executable, str(TOOLS / "package_kajofw.py"), str(manifest), "--no-index"],
                ".kajofw packaging")
            archive = f"KAJO-Dash-Firmware-v{plan.code}-test-Windows.zip"
        run([sys.executable, str(TOOLS / "package_windows_release.py"), "--manifest", str(manifest),
             "--uploader-exe", str(UPLOADER_EXE), "--output", str(STAGING / archive)], "Windows packaging")

        artifacts = promote_release() if plan.is_release else promote_test()
    except BaseException:
        edits.restore()
        shutil.rmtree(STAGING, ignore_errors=True)
        print("\nNothing was written." +
              (f" Put back: {', '.join(shown(path) for path in edits.originals)}." if edits.originals else ""))
        raise
    shutil.rmtree(STAGING, ignore_errors=True)
    return artifacts


def record_release(plan: Plan) -> None:
    """Add the release to RELEASES.md, commit and tag it, then move config.h on.
    Local only: nothing is pushed."""
    rule("Record")
    manifest = json.loads((RELEASES / f"firmware-v{plan.code}.json").read_text(encoding="utf-8"))
    append_history(Release(plan.name, plan.code, datetime.date.today().isoformat(), manifest["sha256"]))
    paths = [shown(HISTORY)] + ([shown(CHANGELOG_HEADER)] if plan.changelog_stale else [])
    steps = [
        # With paths, git commits only those files, whatever else is staged.
        (["git", "commit", "-m", f"Release KAJO-Dash {plan.name}", "--", *paths], "commit"),
        (["git", "tag", "-a", plan.tag, "-m",
          f"KAJO-Dash {plan.name}\n\nfirmware version code {plan.code}"], "tag"),
    ]
    for command, what in steps:
        try:
            run(command, what)
        except Abort:
            print(f"The release files are in place, but the {what} failed. Finish by hand: commit "
                  f"{', '.join(paths)}, tag it {plan.tag}, then set the next version in include/config.h.")
            return
    print(f"  Tagged {plan.tag} locally.")

    write_config(plan.next_code, plan.next_name)
    if plan.reset_app:
        reset_app_revision()
        print("  Reset the companion app's appRevision to 0 for the new firmware version.")
    try:
        run(["git", "commit", "-m", f"Start KAJO-Dash {plan.next_name}", "--", shown(CONFIG_HEADER)], "commit")
    except Abort:
        print(f"include/config.h now says {plan.next_name}, but committing it failed; commit it by hand.")
        return
    print(f"  include/config.h now says {plan.next_name} (code {plan.next_code}).")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Package the version in include/config.h as a test package or a release")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--test", dest="mode", action="store_const", const=TEST,
                      help="a test package for your own boards: no commit, no tag")
    mode.add_argument("--release", dest="mode", action="store_const", const=RELEASE,
                      help="a release: recorded in RELEASES.md, committed and tagged")
    parser.add_argument("--key", type=Path, help="signing key (.pem); remembered for next time")
    parser.add_argument("--yes", action="store_true",
                        help=f"go ahead without asking when the plan has no warnings; the key "
                             f"password then comes from {PASSWORD_ENV} or a prompt")
    args = parser.parse_args(argv)

    print("KAJO-Dash release builder")
    if args.mode is None and args.yes:
        raise Abort("--yes needs --test or --release")
    chosen = args.mode or choose_mode()
    plan = check(chosen, args.key, interactive=not args.yes)
    print_plan(plan)
    if plan.problems:
        raise Abort("fix the problems above and run it again")
    confirm(plan, args.yes)

    rule("Key")
    private_key = unlock_key(plan.key)
    artifacts = execute(plan, private_key)
    if plan.is_release:
        record_release(plan)

    rule("Done")
    for path in artifacts:
        print(f"  {shown(path)}  ({path.stat().st_size:,} bytes)")
    print("\nNothing here contains the signing key.")
    if plan.is_release:
        if plan.reset_app:
            print("Commit the companion app's appRevision reset in its own repository.")
        print(f"To publish: git push origin {plan.branch} {plan.tag}, then attach the Windows ZIP, "
              f"firmware-v{plan.code}.kajofw and {INDEX_NAME} to a GitHub release for {plan.tag}.")
    else:
        print(f"Install it with option 6 in kajo.bat (Bluetooth), kajo.bat --usb (cable), or the "
              f"ZIP's own launcher. The display shows {plan.label}.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Abort as stop:
        print(f"\nStopped: {stop}")
        raise SystemExit(1)
    except KeyboardInterrupt:
        print("\nStopped: cancelled")
        raise SystemExit(1)
