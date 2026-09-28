import importlib.util
import json
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "sender_uploader", Path(__file__).resolve().parents[1] / "tools/upload_test_sender.py")
uploader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uploader)


class SenderUploaderTests(unittest.TestCase):
    @patch.object(uploader.subprocess, "run")
    def test_build_only_never_enumerates_or_uploads(self, run):
        with patch("sys.argv", ["uploader", "--build-only"]):
            uploader.main("fardriver")
        self.assertEqual(run.call_count, 1)
        self.assertEqual(run.call_args.args[0][-3:], ["run", "-e", "fardriver_test"])

    @patch.object(uploader.subprocess, "run")
    def test_explicit_port_uploads_only_test_environment(self, run):
        with patch("sys.argv", ["uploader", "COM9"]):
            uploader.main("fardriver")
        self.assertEqual(run.call_count, 2)
        self.assertEqual(run.call_args.args[0][-7:],
                         ["run", "-e", "fardriver_test", "-t", "upload", "--upload-port", "COM9"])

    @patch.object(uploader.subprocess, "run")
    def test_vesc_sender_uploads_its_own_environment(self, run):
        with patch("sys.argv", ["uploader", "COM9"]):
            uploader.main("vesc")
        self.assertEqual(run.call_args.args[0][-7:],
                         ["run", "-e", "vesc_test", "-t", "upload", "--upload-port", "COM9"])

    def test_every_sender_has_its_own_environment_and_name(self):
        # The two senders share this module; nothing here should be able to
        # flash one environment while announcing the other.
        environments = [sender.environment for sender in uploader.SENDERS.values()]
        advertised = [sender.advertised for sender in uploader.SENDERS.values()]
        self.assertEqual(len(set(environments)), len(environments))
        self.assertEqual(len(set(advertised)), len(advertised))

    @patch.object(uploader.subprocess, "run")
    def test_multiple_usb_devices_require_selection(self, run):
        run.return_value = subprocess.CompletedProcess([], 0, json.dumps([
            {"port": "COM4", "hwid": "USB VID:PID=1A86:7523"},
            {"port": "COM9", "hwid": "USB VID:PID=1A86:7523"},
        ]))
        with patch("sys.argv", ["uploader"]), patch("builtins.input", return_value=""):
            with self.assertRaisesRegex(RuntimeError, "cancelled"):
                uploader.main("fardriver")
        self.assertEqual(run.call_count, 1)


if __name__ == "__main__":
    unittest.main()
