"""Fresh smoke reports and retained native failure evidence, without a GPU."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

DRIVER = Path(__file__).with_name("bindless_vulkan_smoke.py")
spec = importlib.util.spec_from_file_location("bindless_vulkan_smoke_driver", DRIVER)
driver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(driver)


class SmokeDriverEvidenceTest(unittest.TestCase):
    def test_failed_phase_replaces_previous_success(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            result = driver.run_native_phase([sys.executable, "-c", "print('complete')"],
                                             os.environ.copy(), output, "positive", 5)
            self.assertEqual(result.returncode, 0)
            self.assertTrue(json.loads((output / "report.json").read_text())["success"])
            result = driver.run_native_phase([sys.executable, "-c", "print('failed');raise SystemExit(7)"],
                                             os.environ.copy(), output, "negative", 5)
            self.assertEqual(result.returncode, 7)
            report = json.loads((output / "report.json").read_text())
            self.assertFalse(report["success"])
            self.assertEqual(report["returnCode"], 7)
            self.assertEqual((output / "negative-stdout.log").read_text().strip(), "failed")

    def test_timeout_preserves_partial_output_and_failed_current_report(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            (output / "report.json").write_text('{"success":true}')
            result = driver.run_native_phase([sys.executable, "-c",
                "import time;print('before-timeout',flush=True);time.sleep(5)"],
                os.environ.copy(), output, "negative", 0.5)
            self.assertEqual(result.returncode, 124)
            report = json.loads((output / "report.json").read_text())
            self.assertFalse(report["success"])
            self.assertTrue(report["timedOut"])
            self.assertIn("before-timeout", (output / "negative-stdout.log").read_text())

    def test_compile_failure_invalidates_old_report_before_native_run(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            (output / "report.json").write_text('{"success":true}')
            result = subprocess.run([sys.executable, str(DRIVER), "--slangc", str(output / "missing-slangc"),
                "--runner", str(output / "missing-native-runner"), "--output", str(output)],
                capture_output=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            report = json.loads((output / "report.json").read_text())
            self.assertFalse(report["success"])
            self.assertEqual(report["phase"], "CompileArtifacts")


if __name__ == "__main__":
    unittest.main()
