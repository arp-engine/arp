"""Real shared CLI/protocol identity checks; no simulated backend success."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from tool_file_inventory import ToolFileInventoryClient
from survivors_package_inventory import PackageInventoryClient

parser = argparse.ArgumentParser()
parser.add_argument("--executable", type=Path, required=True)
parser.add_argument("--legacy-executable", type=Path, required=True)
args = parser.parse_args()


class SharedInventoryTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="arp-shared-file-input-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.files = {"empty.bin": b"", "Unicode 路径's.bin": b"a\x00b\xff\r\n",
                      "text.txt": b"\xef\xbb\xbfline\r\nnext\rlast\n"}
        for name, data in self.files.items():
            (self.root / name).write_bytes(data)

    def test_shared_and_legacy_receipts_and_payloads_are_exact(self):
        shared = ToolFileInventoryClient(args.executable)
        legacy = PackageInventoryClient(args.legacy_executable)
        paths = [self.root / name for name in self.files]
        with patch.object(Path, "read_bytes", side_effect=AssertionError("Python read bypass")):
            self.assertEqual(shared.inspect(paths, self.root), legacy.inspect(paths, self.root))
            for name, expected in self.files.items():
                current = shared.read_file(self.root / name)
                self.assertEqual(current, legacy.read_file(self.root / name))
                self.assertEqual(current.data, expected)
                self.assertEqual(current.sha256, hashlib.sha256(expected).hexdigest())

    def test_protocols_never_guess_or_auto_accept_each_other(self):
        for executable, client in ((args.executable, PackageInventoryClient),
                                   (args.legacy_executable, ToolFileInventoryClient)):
            with self.subTest(executable=executable):
                with self.assertRaisesRegex(RuntimeError, "INVENTORY-CONTRACT"):
                    client(executable).read_file(self.root / "empty.bin")

    def test_shared_identity_and_no_partial_response_after_read_failure(self):
        source = self.root / "text.txt"
        document = {"type": "arp.tool-file-inventory-request", "contractVersion": 2,
                    "payloadEncoding": "base64", "maxPayloadBytes": 128,
                    "maxFiles": 2, "maxFileBytes": 128, "maxTotalBytes": 256,
                    "maxPathBytes": 1024, "files": [
                        {"name": "text.txt", "path": str(source), "bytes": len(self.files["text.txt"])},
                        {"name": "missing.bin", "path": str(self.root / "missing.bin"), "bytes": 0}]}
        result = subprocess.run([str(args.executable), "--request-limit-bytes", "16384",
                                 "--response-limit-bytes", "16384", "--preparation-bytes", "1048576"],
                                input=yaml.safe_dump(document).encode("utf-8"), capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b"")
        self.assertTrue(result.stderr)

    def test_missing_tool_and_arguments_fail_closed(self):
        with self.assertRaisesRegex(RuntimeError, "TOOL-FILE-INVENTORY-TOOL-MISSING"):
            ToolFileInventoryClient(self.root / "missing-tool.exe")
        result = subprocess.run([str(args.executable)], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b"")
        self.assertIn(b"TOOL-FILE-INVENTORY-ARGUMENTS", result.stderr)


if __name__ == "__main__":
    unittest.main(argv=[__file__])
