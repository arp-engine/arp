"""Actual native package inventory protocol; Python hashlib is an independent oracle."""
import argparse
import base64
from copy import deepcopy
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from survivors_package_inventory import PackageInventoryClient

parser = argparse.ArgumentParser()
parser.add_argument("--executable", type=Path, required=True)
options = parser.parse_args()


class InventoryProtocolTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="arp-package-inventory-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.payloads = {"raw.bin": b"a\x00b\xff\r\n", "empty.bin": b"",
                         "Unicode 路径's.txt": b"\xef\xbb\xbfline\r\n"}
        for name, data in self.payloads.items():
            (self.root / name).write_bytes(data)
        self.document = {"type": "arp.survivors-package-inventory-request", "contractVersion": 1,
                         "maxFiles": 3, "maxFileBytes": 64, "maxTotalBytes": 192,
                         "maxPathBytes": 1024, "files": [
                             {"name": name, "path": str(self.root / name), "bytes": len(data)}
                             for name, data in self.payloads.items()]}

    def invoke(self, source=None, *, request_limit=16384, response_limit=16384, quota=1048576):
        if source is None:
            source = yaml.safe_dump(self.document, sort_keys=False, allow_unicode=True).encode("utf-8")
        return subprocess.run([str(options.executable), "--request-limit-bytes", str(request_limit),
                               "--response-limit-bytes", str(response_limit),
                               "--preparation-bytes", str(quota)], input=source, capture_output=True)

    def reject(self, diagnostic=None, **budgets):
        result = self.invoke(**budgets)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b"", "failure may not publish a partial table")
        if diagnostic:
            self.assertIn(diagnostic.encode(), result.stderr)

    def test_real_client_exact_binary_empty_bom_and_unicode(self):
        with patch.object(Path, "read_bytes", side_effect=AssertionError("Python file read bypass")):
            result = PackageInventoryClient(options.executable).inspect(
                [self.root / name for name in self.payloads], self.root)
        self.assertEqual(result, [{"path": name, "bytes": len(data),
                                   "sha256": hashlib.sha256(data).hexdigest()}
                                  for name, data in self.payloads.items()])

    def test_real_owned_payload_receipts_and_explicit_version(self):
        with patch.object(Path, "read_bytes", side_effect=AssertionError("Python input bypass")):
            for name, expected in self.payloads.items():
                receipt = PackageInventoryClient(options.executable).read_file(self.root / name)
                self.assertEqual(receipt.data, expected)
                self.assertEqual(receipt.sha256, hashlib.sha256(expected).hexdigest())
        ordinary = yaml.safe_load(self.invoke().stdout)
        self.assertEqual(ordinary["contractVersion"], 1)
        self.assertTrue(all("data" not in row for row in ordinary["files"]))

    def test_payload_budget_encoding_and_version_gates(self):
        self.document["contractVersion"] = 2
        self.document["payloadEncoding"] = "base64"
        self.document["maxPayloadBytes"] = 192
        successful = self.invoke()
        self.assertEqual(successful.returncode, 0, successful.stderr)
        response = yaml.safe_load(successful.stdout)
        for row, expected in zip(response["files"], self.payloads.values()):
            self.assertEqual(base64.b64decode(row["data"], validate=True), expected)
        self.document["payloadEncoding"] = "guessed"
        self.reject("PAYLOAD-ENCODING")
        self.document["payloadEncoding"] = "base64"
        self.document["maxPayloadBytes"] = 0
        self.reject("PAYLOAD-BUDGET")
        self.document["maxPayloadBytes"] = sum(row["bytes"] for row in self.document["files"])
        encoded = sum(((row["bytes"] + 2) // 3) * 4 for row in self.document["files"])
        self.reject("RESPONSE-BUDGET", response_limit=encoded - 1)
        self.document["contractVersion"] = 1
        self.reject("YAML-CONTRACT-FIELD-INVALID")

    def test_client_refuses_tampered_payload_protocol_without_claiming_native_execution(self):
        path = self.root / "one.bin"
        path.write_bytes(b"f")
        response = {"type": "arp.survivors-package-inventory", "contractVersion": 2,
                    "payloadEncoding": "base64", "files": [
                        {"path": "one.bin", "bytes": 1, "sha256": hashlib.sha256(b"f").hexdigest(),
                         "data": "Zg=="}]}
        for data in ("eg==", "Zh==", "$$", "非ASCII"):
            response["files"][0]["data"] = data
            completed = SimpleNamespace(returncode=0, stderr=b"",
                                        stdout=yaml.safe_dump(response, allow_unicode=True).encode())
            with patch("tool_file_inventory.subprocess.run", return_value=completed):
                with self.assertRaisesRegex(RuntimeError, "PAYLOAD-(INTEGRITY|ENCODING)"):
                    PackageInventoryClient(options.executable).read_file(path)

    def test_unknown_contract_and_fields(self):
        for key, value in (("contractVersion", 99), ("unexpected", "field")):
            saved = deepcopy(self.document)
            self.document[key] = value
            self.reject()
            self.document = saved

    def test_two_documents_and_empty_second_document(self):
        first = yaml.safe_dump(self.document).encode()
        for suffix in (b"---\n", b"---\n" + first):
            result = self.invoke(first + suffix)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, b"")

    def test_missing_second_file_has_no_partial_stdout(self):
        self.document["files"][1]["path"] = str(self.root / "missing.bin")
        self.reject()

    def test_declared_size_growth_and_shrink(self):
        self.document["files"][0]["bytes"] -= 1
        self.reject("IO-READ-BYTE-BUDGET")
        self.document["files"][0]["bytes"] += 2
        self.reject("SURVIVORS-PACKAGE-INVENTORY-SIZE-CHANGED")

    def test_name_source_and_duplicate_refusal(self):
        for key, value in (("name", "../escape"), ("name", "alias\x00suffix"), ("path", "relative.bin"),
                           ("path", str(self.root / "raw.bin") + "\x00alias")):
            saved = deepcopy(self.document)
            self.document["files"][0][key] = value
            self.reject()
            self.document = saved
        raw = yaml.safe_dump(self.document, sort_keys=False).encode()
        invalid_utf8 = raw.replace(b"name: raw.bin", b"name: \xc0\x80", 1)
        self.assertNotEqual(invalid_utf8, raw)
        result = self.invoke(invalid_utf8)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b"")
        self.document["files"][1]["name"] = "raw.bin"
        self.reject("SURVIVORS-PACKAGE-INVENTORY-DUPLICATE")

    def test_control_response_and_allocation_budgets(self):
        self.reject("CONTROL-BUDGET", request_limit=1)
        self.reject("RESPONSE-BUDGET", response_limit=1)
        self.reject("ALLOCATION", quota=1)

    def test_response_budget_covers_actual_binary_stdout(self):
        ordinary = self.invoke()
        self.assertEqual(ordinary.returncode, 0, ordinary.stderr)
        exact = self.invoke(response_limit=len(ordinary.stdout))
        self.assertEqual(exact.returncode, 0, exact.stderr)
        self.assertEqual(exact.stdout, ordinary.stdout)
        denied = self.invoke(response_limit=len(ordinary.stdout) - 1)
        self.assertNotEqual(denied.returncode, 0)
        self.assertEqual(denied.stdout, b"")

    def test_payload_and_table_budgets(self):
        for key, value in (("maxFiles", 1), ("maxFileBytes", 0),
                           ("maxTotalBytes", 1), ("maxPathBytes", 1)):
            saved = deepcopy(self.document)
            self.document[key] = value
            self.reject()
            self.document = saved

    def test_all_empty_input_has_real_empty_digest(self):
        self.document["files"] = [self.document["files"][1]]
        self.document["maxFiles"] = self.document["maxFileBytes"] = 1
        self.document["maxTotalBytes"] = 0
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(yaml.safe_load(result.stdout)["files"][0]["sha256"],
                         hashlib.sha256(b"").hexdigest())

    def test_client_missing_tool_and_failed_read_never_fall_back(self):
        with self.assertRaisesRegex(RuntimeError, "TOOL-MISSING"):
            PackageInventoryClient(self.root / "missing.exe")
        with patch.object(Path, "read_bytes", side_effect=AssertionError("Python file read bypass")):
            with self.assertRaises((RuntimeError, OSError)):
                PackageInventoryClient(options.executable).inspect([self.root / "absent"], self.root)


if __name__ == "__main__":
    unittest.main(argv=[__file__])
