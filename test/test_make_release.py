from __future__ import annotations

import contextlib
import io
import json
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "firmware_update"))

import make_release  # noqa: E402
import package_usb  # noqa: E402
import sign_firmware  # noqa: E402
import uploader_inputs  # noqa: E402

CONFIG = '#define CYD_FIRMWARE_VERSION_CODE {code}UL\n#define CYD_FIRMWARE_VERSION_NAME "{name}"\n'
HISTORY_HEAD = "# Releases\n\n| Version | Code | Date | Firmware SHA-256 |\n| --- | --- | --- | --- |\n"


def quietly(function, *args, **kwargs):
    with contextlib.redirect_stdout(io.StringIO()):
        return function(*args, **kwargs)


class UploaderInputsTests(unittest.TestCase):
    def test_the_uploaders_own_modules_are_followed_through_imports(self):
        names = {path.name for path in uploader_inputs.local_modules()}
        self.assertEqual(names, {"upload_firmware.py", "protocol.py", "usb_flash.py"})

    def test_stamp_holds_until_an_input_or_the_exe_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "entry.py").write_text("import helper\n", encoding="utf-8")
            (root / "helper.py").write_text("VALUE = 1\n", encoding="utf-8")
            (root / "requirements.txt").write_text("bleak\n", encoding="utf-8")
            exe = root / "uploader.exe"
            exe.write_bytes(b"MZ one")
            inputs = {"entry": root / "entry.py", "build_files": (root / "requirements.txt",),
                      "requirements": root / "requirements.txt"}

            self.assertFalse(uploader_inputs.is_current(exe, **inputs))
            uploader_inputs.write_stamp(exe, **inputs)
            self.assertTrue(uploader_inputs.is_current(exe, **inputs))

            (root / "helper.py").write_text("VALUE = 2\n", encoding="utf-8")
            self.assertFalse(uploader_inputs.is_current(exe, **inputs))
            uploader_inputs.write_stamp(exe, **inputs)
            exe.write_bytes(b"MZ built some other way")
            self.assertFalse(uploader_inputs.is_current(exe, **inputs))

    def test_packages_installed_beside_the_uploaders_do_not_count(self):
        packages = {line.split("==")[0] for line in uploader_inputs.bundled_packages()}
        self.assertIn("bleak", packages)
        self.assertIn("esptool", packages)
        self.assertNotIn("pytest", packages)


class ReleaseKey:
    """A throwaway release key and the public-key header that trusts it."""

    def __init__(self, root: Path, password: bytes = b"correct horse"):
        self.private_key = ec.generate_private_key(ec.SECP256R1())
        self.path = root / "release-key.pem"
        self.path.write_bytes(self.private_key.private_bytes(
            serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
            serialization.BestAvailableEncryption(password)))
        point = self.private_key.public_key().public_bytes(
            serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
        self.header = root / "firmware_update_public_key.h"
        self.header.write_text(", ".join(f"0x{byte:02x}" for byte in point) + "\n", encoding="utf-8")


class SigningKeyTests(unittest.TestCase):
    def test_wrong_password_and_foreign_key_are_told_apart(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            key = ReleaseKey(root)
            with mock.patch.object(sign_firmware, "PUBLIC_HEADER", key.header):
                self.assertTrue(sign_firmware.key_needs_password(key.path))
                sign_firmware.load_private_key(key.path, "correct horse")
                with self.assertRaises(ValueError) as wrong:
                    sign_firmware.load_private_key(key.path, "battery staple")
                self.assertNotIsInstance(wrong.exception, sign_firmware.ReleaseKeyError)
            foreign = root / "foreign"
            foreign.mkdir()
            with mock.patch.object(sign_firmware, "PUBLIC_HEADER", ReleaseKey(foreign).header):
                with self.assertRaises(sign_firmware.ReleaseKeyError):
                    sign_firmware.load_private_key(key.path, "correct horse")


class Sandbox:
    """make_release's paths pointed into a temporary directory."""

    def __init__(self, root: Path):
        self.root = root
        self.config = root / "include" / "config.h"
        self.config.parent.mkdir()
        self.config.write_text(CONFIG.format(code=1, name="0.01"), encoding="utf-8")
        self.history = root / "RELEASES.md"
        self.history.write_text(HISTORY_HEAD, encoding="utf-8")
        self.releases = root / "releases"
        self.releases.mkdir()
        self.dist = root / "dist"
        self.pio_build = root / "build"
        self.staging = root / "staging"
        self.boot_app0 = root / "boot_app0.bin"
        self.boot_app0.write_bytes(b"\xff" * 8192)
        self.uploader = root / "uploader.exe"
        self.uploader.write_bytes(b"MZ test uploader")
        self.settings = root / "release.local.json"
        self.key = ReleaseKey(root)
        self.settings.write_text(json.dumps({"private_key": str(self.key.path)}), encoding="utf-8")
        self.commands: list[list[str]] = []
        self.stack = contextlib.ExitStack()

    def __enter__(self):
        for name, value in {
            "CONFIG_HEADER": self.config, "HISTORY": self.history, "RELEASES": self.releases,
            "TEST_RELEASES": self.releases / "test", "DIST": self.dist, "PIO_BUILD": self.pio_build,
            "STAGING": self.staging, "UPLOADER_EXE": self.uploader, "SETTINGS": self.settings,
            "APP_GRADLE": self.root / "missing.gradle.kts",
        }.items():
            self.stack.enter_context(mock.patch.object(make_release, name, value))
        self.stack.enter_context(mock.patch.object(package_usb, "BOOT_APP0", self.boot_app0))
        self.stack.enter_context(mock.patch.object(sign_firmware, "PUBLIC_HEADER", self.key.header))
        return self

    def __exit__(self, *exc):
        self.stack.close()

    def plan(self, mode: str, **overrides) -> make_release.Plan:
        code, name = make_release.read_config()
        plan = make_release.Plan(mode, code, name, platformio="pio")
        for field_name, value in overrides.items():
            setattr(plan, field_name, value)
        return plan

    def fake_run(self, fail_on: str | None = None):
        """PlatformIO, git and the EXE build are faked; the packagers run for real."""
        def run(command, what):
            self.commands.append(command)
            if fail_on and fail_on in " ".join(command):
                raise make_release.Abort(f"{what} failed")
            if command[0] == "pio":
                build = self.pio_build / command[command.index("-e") + 1]
                build.mkdir(parents=True, exist_ok=True)
                (build / "firmware.bin").write_bytes(b"\xe9" + bytes(5000))
                (build / "bootloader.bin").write_bytes(b"\xe9" + bytes(100))
                (build / "partitions.bin").write_bytes(b"\xaa\x50" + bytes(200))
                return
            if command[0] == "git":
                return
            result = subprocess.run(command, capture_output=True, text=True)
            if result.returncode != 0:
                raise AssertionError(f"{what} failed:\n{result.stderr}")
        return run


class HistoryTests(unittest.TestCase):
    def test_rows_are_appended_and_read_back_in_order(self):
        with tempfile.TemporaryDirectory() as directory, Sandbox(Path(directory)):
            self.assertEqual(make_release.read_history(), [])
            first = make_release.Release("0.01", 1, "2026-10-01", "a" * 64)
            second = make_release.Release("0.02", 2, "2026-10-15", "b" * 64)
            make_release.append_history(first)
            make_release.append_history(second)
            self.assertEqual(make_release.read_history(), [first, second])


class PlanTests(unittest.TestCase):
    def check(self, sandbox: Sandbox, mode: str, git_status: str = "", tags: str = "") -> make_release.Plan:
        def git(*args):
            return {"rev-parse": "main", "status": git_status, "tag": tags}.get(args[0], "")
        with mock.patch.object(make_release, "git", git), \
                mock.patch.object(make_release, "find_platformio", return_value="pio"), \
                mock.patch.object(uploader_inputs, "is_current", return_value=True), \
                mock.patch.object(make_release, "changelog_status", return_value=(False, None, None)):
            return quietly(make_release.check, mode, interactive=False, tests=(True, "3 passed"))

    def test_release_takes_the_version_from_config_h(self):
        with tempfile.TemporaryDirectory() as directory, Sandbox(Path(directory)) as sandbox:
            plan = self.check(sandbox, make_release.RELEASE)
            self.assertEqual((plan.name, plan.code, plan.tag), ("0.01", 1, "v0.01"))
            self.assertEqual((plan.next_name, plan.next_code), ("0.02", 2))
            self.assertEqual(plan.problems, [])
            self.assertEqual(plan.warnings, [])
            self.assertEqual(plan.build_dir, sandbox.pio_build / "kajo")

    def test_release_needs_a_clean_tree_and_a_new_version(self):
        with tempfile.TemporaryDirectory() as directory, Sandbox(Path(directory)) as sandbox:
            make_release.append_history(make_release.Release("0.01", 1, "2026-10-01", "a" * 64))
            plan = self.check(sandbox, make_release.RELEASE, git_status=" M src/main_lvgl.cpp", tags="v0.01")
            self.assertEqual(len(plan.problems), 3)
            self.assertIn("uncommitted", plan.problems[0])
            self.assertIn("RELEASES.md already has 0.01", plan.problems[1])
            self.assertIn("v0.01 already exists", plan.problems[2])

    def test_an_existing_output_stops_the_release_before_anything_changes(self):
        with tempfile.TemporaryDirectory() as directory, Sandbox(Path(directory)) as sandbox:
            sandbox.dist.mkdir()
            (sandbox.dist / "KAJO-Dash-Firmware-v1-Windows.zip").write_bytes(b"old")
            plan = self.check(sandbox, make_release.RELEASE)
            self.assertEqual(len(plan.problems), 1)
            self.assertIn("KAJO-Dash-Firmware-v1-Windows.zip", plan.problems[0])

    def test_test_package_is_fine_with_uncommitted_changes(self):
        with tempfile.TemporaryDirectory() as directory, Sandbox(Path(directory)) as sandbox:
            plan = self.check(sandbox, make_release.TEST, git_status=" M src/main_lvgl.cpp")
            self.assertEqual((plan.problems, plan.warnings), ([], []))
            self.assertEqual(plan.label, "0.01-test")
            self.assertEqual(plan.build_dir, sandbox.pio_build / "kajo_test_package")

    def test_test_package_of_a_released_version_is_a_warning(self):
        with tempfile.TemporaryDirectory() as directory, Sandbox(Path(directory)) as sandbox:
            make_release.append_history(make_release.Release("0.01", 1, "2026-10-01", "a" * 64))
            plan = self.check(sandbox, make_release.TEST)
            self.assertEqual(plan.problems, [])
            self.assertEqual(len(plan.warnings), 1)

    def test_missing_key_is_a_problem_without_a_console(self):
        with tempfile.TemporaryDirectory() as directory, Sandbox(Path(directory)) as sandbox:
            sandbox.settings.unlink()
            plan = self.check(sandbox, make_release.TEST)
            self.assertIsNone(plan.key)
            self.assertIn("--key", plan.problems[0])


class ExecutionTests(unittest.TestCase):
    def test_test_package_replaces_the_previous_one_and_leaves_the_version(self):
        with tempfile.TemporaryDirectory() as directory, Sandbox(Path(directory)) as sandbox:
            previous = sandbox.releases / "test"
            previous.mkdir()
            (previous / "firmware-v0.json").write_text("{}", encoding="utf-8")
            with mock.patch.object(make_release, "run", sandbox.fake_run()):
                artifacts = quietly(make_release.execute, sandbox.plan(make_release.TEST),
                                    sandbox.key.private_key)

            self.assertTrue(all(path.parent == previous for path in artifacts))
            self.assertEqual(sorted(path.name for path in previous.iterdir()), sorted([
                "firmware-v1.json", "firmware-v1.bin", "firmware-v1.zlib", "firmware-v1.kajofw",
                "firmware-v1-usb.json", "firmware-v1-bootloader.bin", "firmware-v1-partitions.bin",
                "firmware-v1-boot_app0.bin", "KAJO-Dash-Firmware-v1-test-Windows.zip"]))
            self.assertIn(["pio", "run", "-e", "kajo_test_package"], sandbox.commands)
            self.assertEqual(sandbox.config.read_text(encoding="utf-8"), CONFIG.format(code=1, name="0.01"))
            self.assertFalse((sandbox.releases / "kajo-update.json").exists())
            self.assertFalse(sandbox.dist.exists())
            self.assertFalse(sandbox.staging.exists())

    def test_release_lands_in_releases_and_dist(self):
        with tempfile.TemporaryDirectory() as directory, Sandbox(Path(directory)) as sandbox:
            with mock.patch.object(make_release, "run", sandbox.fake_run()):
                artifacts = quietly(make_release.execute, sandbox.plan(make_release.RELEASE),
                                    sandbox.key.private_key)

            self.assertEqual({path.name for path in artifacts},
                             {path.name for path in make_release.release_outputs(1)} | {"kajo-update.json"})
            self.assertTrue(all(path.is_file() for path in artifacts))
            self.assertIn(["pio", "run", "-e", "kajo"], sandbox.commands)
            self.assertFalse(sandbox.staging.exists())
            index = json.loads((sandbox.releases / "kajo-update.json").read_text(encoding="utf-8"))
            self.assertEqual((index["version_code"], index["version_name"], index["tag"]), (1, "0.01", "v0.01"))
            with zipfile.ZipFile(sandbox.dist / "KAJO-Dash-Firmware-v1-Windows.zip") as archive:
                self.assertIn("firmware-v1-usb.json", archive.namelist())

    def test_recording_a_release_tags_it_and_moves_the_version_on(self):
        with tempfile.TemporaryDirectory() as directory, Sandbox(Path(directory)) as sandbox:
            plan = sandbox.plan(make_release.RELEASE)
            with mock.patch.object(make_release, "run", sandbox.fake_run()):
                quietly(make_release.execute, plan, sandbox.key.private_key)
                quietly(make_release.record_release, plan)

            manifest = json.loads((sandbox.releases / "firmware-v1.json").read_text(encoding="utf-8"))
            [release] = make_release.read_history()
            self.assertEqual((release.name, release.code, release.sha256), ("0.01", 1, manifest["sha256"]))
            self.assertEqual(sandbox.config.read_text(encoding="utf-8"), CONFIG.format(code=2, name="0.02"))
            git = [command[1:4] for command in sandbox.commands if command[0] == "git"]
            self.assertEqual(git, [["commit", "-m", "Release KAJO-Dash 0.01"],
                                   ["tag", "-a", "v0.01"],
                                   ["commit", "-m", "Start KAJO-Dash 0.02"]])

    def test_failed_step_puts_back_what_it_edited_and_writes_nothing(self):
        with tempfile.TemporaryDirectory() as directory, Sandbox(Path(directory)) as sandbox:
            index = sandbox.releases / "kajo-update.json"
            index.write_text('{"version_code": 0}\n', encoding="utf-8")
            header = sandbox.root / "firmware_changelog.h"
            header.write_text("old header\n", encoding="utf-8")

            def fake_run(command, what):
                if str(make_release.CHANGELOG_TOOL) in command:
                    header.write_text("new header\n", encoding="utf-8")
                    return None
                return sandbox.fake_run(fail_on="package_windows_release")(command, what)

            with mock.patch.object(make_release, "CHANGELOG_HEADER", header), \
                    mock.patch.object(make_release, "run", fake_run):
                with self.assertRaises(make_release.Abort):
                    quietly(make_release.execute, sandbox.plan(make_release.RELEASE, changelog_stale=True),
                            sandbox.key.private_key)

            self.assertEqual(header.read_text(encoding="utf-8"), "old header\n")
            self.assertEqual(sorted(path.name for path in sandbox.releases.iterdir()), ["kajo-update.json"])
            self.assertEqual(index.read_text(encoding="utf-8"), '{"version_code": 0}\n')
            self.assertFalse(sandbox.dist.exists())
            self.assertFalse(sandbox.staging.exists())

    def test_failed_move_leaves_nothing_behind(self):
        with tempfile.TemporaryDirectory() as directory, Sandbox(Path(directory)) as sandbox:
            real_replace = make_release.os.replace
            calls = []

            def replace(source, target):
                # The fourth move fails; the rollback's moves after it succeed.
                calls.append(target)
                if len(calls) == 4:
                    raise OSError("disk full")
                real_replace(source, target)

            with mock.patch.object(make_release, "run", sandbox.fake_run()), \
                    mock.patch.object(make_release.os, "replace", replace):
                with self.assertRaises(OSError):
                    quietly(make_release.execute, sandbox.plan(make_release.RELEASE), sandbox.key.private_key)

            self.assertEqual(list(sandbox.releases.iterdir()), [])
            self.assertFalse(sandbox.staging.exists())


if __name__ == "__main__":
    unittest.main()
