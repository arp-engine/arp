"""Check the actual Interpreter CLI parser without a running remote server."""
from pathlib import Path
import os
import subprocess
import sys
import unittest

CLI = Path(sys.argv.pop(1)).resolve()


class TransformPivotCliTests(unittest.TestCase):
    def invoke(self, *arguments):
        environment = os.environ.copy()
        environment["ARP_EDITOR_REMOTE_TOKEN"] = "01234567890123456789012345678901"
        # Port zero cannot serve a request. Invalid arguments must fail locally.
        return subprocess.run(
            [str(CLI), "--url", "http://127.0.0.1:0", *arguments],
            env=environment, capture_output=True, text=True, encoding="utf-8", timeout=15)

    def test_help_exposes_exact_command_and_enum(self):
        result = self.invoke("--help")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("scene.transform.pivot.set <active|center|individual>", result.stdout)

    def test_wrong_arity_never_attempts_network(self):
        for arguments in ((), ("center", "extra")):
            with self.subTest(arguments=arguments):
                result = self.invoke("scene.transform.pivot.set", *arguments)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertIn("requires one pivot value", result.stdout + result.stderr)

    def test_unknown_case_and_payload_are_refused_locally(self):
        for value in ("Center", "unknown", "", 'center","command":"scene.new'):
            with self.subTest(value=value):
                result = self.invoke("scene.transform.pivot.set", value)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertIn("pivot must be active, center or individual", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
