"""Invalid Legacy opt-ins must fail before creating an Editor window/process host."""
import pathlib
import subprocess
import sys
import unittest

EDITOR = pathlib.Path(sys.argv.pop(1)).resolve()


class PlayLaunchOptionsTests(unittest.TestCase):
    def test_invalid_opt_in_is_rejected_before_window_creation(self):
        absolute_root = str(EDITOR.parent)
        absolute_executable = str(EDITOR)
        cases = [
            ["--legacy-wocc-executable", absolute_executable],
            ["--project-root", absolute_root, "--legacy-wocc-executable", "relative.exe"],
            ["--project-root", absolute_root, "--legacy-wocc-executable", ""],
            ["--project-root", "relative-root", "--legacy-wocc-executable", absolute_executable],
            ["--project-root", absolute_root, "--legacy-wocc-executable", absolute_executable,
             "--legacy-wocc-executable", absolute_executable],
        ]
        for arguments in cases:
            with self.subTest(arguments=arguments):
                result = subprocess.run([str(EDITOR)] + arguments, capture_output=True,
                                        text=True, encoding="utf-8", timeout=10)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertIn("EDITOR-LEGACY-PREVIEW-OPTIONS", result.stderr)
                self.assertNotIn("vulkan-swapchain", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
