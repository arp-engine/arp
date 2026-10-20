"""Inventory scope tests do not grant any RHI execution authority."""
from pathlib import Path
from tempfile import TemporaryDirectory
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
import hashlib
import unittest
from unittest.mock import patch

import yaml

from cpp_source_evidence import switch_regions
from rhi_backend_inventory import RhiBackendInventory, main


class BackendInventoryTests(unittest.TestCase):
    def test_repository_backend_switches_explicitly_cover_webgl(self):
        root = Path(__file__).resolve().parents[2]
        report = RhiBackendInventory(root).collect()
        self.assertTrue(report["scanComplete"])
        missing = [f'{record["path"]}:{region["line"]}'
                   for record in report["files"] for region in record["switches"]
                   if not region["explicitWebGl"]]
        self.assertEqual(missing, [], "Missing explicit WebGL decision: " + ", ".join(missing))

    def test_invalid_utf8_is_retained_as_open_diagnostic_with_raw_identity(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            source = b'\xef\xbb\xbfusing B = Backend;\n// \xd6\xd0\n'
            (root / "legacy.cpp").write_bytes(source)
            result = RhiBackendInventory(root).inspect(Path("legacy.cpp"))
            self.assertEqual(result["path"], "legacy.cpp")
            self.assertEqual(result["sha256"], hashlib.sha256(source).hexdigest())
            self.assertFalse(result["parseComplete"])
            self.assertIsNone(result["backendReferenceCount"])
            self.assertIsNone(result["backendTypeTokenCount"])
            self.assertEqual(result["reviewStatus"], "Open")
            self.assertEqual(result["diagnostic"]["id"], "RHI-INVENTORY-SOURCE-ENCODING")
            self.assertEqual(result["diagnostic"]["byteOffset"], source.index(b'\xd6'))

    def test_incomplete_collection_keeps_other_files_and_cli_writes_failure_artifact(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            (root / "src/valid.cpp").write_text("using B = Backend;", encoding="utf-8")
            (root / "src/legacy.cpp").write_bytes(b'// \xff\nusing B = Backend;')
            output = root / "report.yaml"
            stdout, stderr = StringIO(), StringIO()
            with patch("rhi_backend_inventory.subprocess.check_output", return_value="fixture-revision\n"):
                with redirect_stdout(stdout), redirect_stderr(stderr):
                    exit_code = main(["--root", str(root), "--output", str(output)])
            self.assertEqual(exit_code, 2)
            result = yaml.safe_load(output.read_text(encoding="utf-8"))
            self.assertFalse(result["scanComplete"])
            self.assertFalse(result["approvalGranted"])
            self.assertEqual(result["counts"]["sourceFiles"], 2)
            self.assertEqual(result["counts"]["parsedSourceFiles"], 1)
            self.assertEqual(result["counts"]["sourceDiagnostics"], 1)
            self.assertEqual({r["path"] for r in result["files"]},
                             {"src/valid.cpp", "src/legacy.cpp"})
            self.assertIn("RHI-INVENTORY-SOURCE-ENCODING", stderr.getvalue())
            self.assertIn("src/legacy.cpp", stderr.getvalue())

    def test_utf8_bom_is_explicitly_supported_and_unrelated_files_are_counted(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            source = b'\xef\xbb\xbfusing B = Backend;'
            (root / "src/bom.cpp").write_bytes(source)
            (root / "src/unrelated.cpp").write_text("void unrelated();", encoding="utf-8")
            output = root / "report.yaml"
            with patch("rhi_backend_inventory.subprocess.check_output", return_value="fixture-revision\n"):
                with redirect_stdout(StringIO()), redirect_stderr(StringIO()):
                    self.assertEqual(main(["--root", str(root), "--output", str(output)]), 0)
            result = yaml.safe_load(output.read_text(encoding="utf-8"))
            self.assertTrue(result["scanComplete"])
            self.assertEqual(result["counts"]["sourceFiles"], 2)
            self.assertEqual(result["counts"]["parsedSourceFiles"], 2)
            self.assertEqual(result["counts"]["sourceDiagnostics"], 0)
            self.assertEqual(result["counts"]["files"], 1)
            self.assertEqual(result["files"][0]["sha256"], hashlib.sha256(source).hexdigest())

    def test_type_only_conversion_is_not_omitted(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "conversion.hpp").write_text('''
// Backend in comments is not a type occurrence.
const char* fake = "BackendPreference";
arp::rhi::Backend decode(unsigned ordinal) {
    return static_cast<arp::rhi::Backend>(ordinal);
}
''', encoding="utf-8")
            result = RhiBackendInventory(root).inspect(Path("conversion.hpp"))
            self.assertIsNotNone(result)
            self.assertEqual(result["backendReferenceCount"], 0)
            self.assertEqual(result["backendTypeTokenCount"], 2)
            self.assertEqual(result["backendPreferenceTypeTokenCount"], 0)
            self.assertEqual(result["switches"], [])
            self.assertEqual(result["reviewStatus"], "Open")

    def test_authored_source_scope_includes_other_runtime_editor_examples_and_tests(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            names = ("src/asset/conversion.hpp", "editor/pipeline.cpp",
                     "examples/minimal.cc", "tests/contract/native.cxx",
                     "src/rhi/profile.ipp", "3rd/vendor/ignored.h", "build/ignored.cpp")
            for name in names:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("using B = arp::rhi::Backend;", encoding="utf-8")
            with patch("rhi_backend_inventory.subprocess.check_output", return_value="fixture-revision\n"):
                result = RhiBackendInventory(root).collect()
            self.assertEqual(result["contractVersion"], 3)
            self.assertEqual(result["sourceRoots"], ["src", "editor", "examples", "tests"])
            self.assertEqual({row["path"] for row in result["files"]}, set(names[:5]))
            self.assertFalse(result["approvalGranted"])
            self.assertEqual(result["evidenceKind"], "LexicalInventoryOnly")
            self.assertEqual(result["revision"], "fixture-revision")

    def test_preference_only_routing_is_not_omitted(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "routing.cpp"
            path.write_text('''
// Backend::Vulkan and BackendPreference::WebGL are not evidence here.
const char* fake = "case BackendPreference::WebGL:";
switch (request) {
case platform::BackendPreference::Default: return createDefault();
case platform::BackendPreference::Vulkan: return createVulkan();
default: return createDefault();
}
''', encoding="utf-8")
            result = RhiBackendInventory(root).inspect(Path("routing.cpp"))
            self.assertIsNotNone(result)
            self.assertEqual(result["backendPreferenceReferenceCount"], 2)
            self.assertEqual(result["backendReferenceCount"], 0)
            self.assertFalse(result["switches"][0]["explicitWebGl"])
            self.assertEqual(result["switches"][0]["enumTypes"], ["BackendPreference"])
            self.assertEqual(result["reviewStatus"], "Open")

    def test_namespaced_preference_case_is_explicit_but_not_approval(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "routing.h").write_text('''switch (preference) {
case arp::platform::BackendPreference::WebGL: return unavailable();
}''', encoding="utf-8")
            result = RhiBackendInventory(root).inspect(Path("routing.h"))
            self.assertTrue(result["switches"][0]["explicitWebGl"])
            self.assertEqual(result["switches"][0]["reviewStatus"], "Open")

    def test_comments_literals_and_nested_labels(self):
        source = '''
// switch (backend) { case Backend::WebGL: }
const char* text = "switch (backend) { case Backend::WebGL: }";
switch (backend) {
case Backend::Null: {
    switch (other) { case Backend::WebGL: return false; }
    return false;
}
default: return false;
}
'''
        regions = list(switch_regions(source))
        self.assertEqual(len(regions), 2)
        self.assertEqual(regions[0]["labels"], ["case Backend::Null:", "default:"])
        self.assertEqual(regions[1]["labels"], ["case Backend::WebGL:"])

    def test_inventory_does_not_grant_approval_for_explicit_case(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "fixture.h"
            path.write_text("switch (backend) { case Backend::WebGL: return {}; }", encoding="utf-8")
            result = RhiBackendInventory(root).inspect(Path("fixture.h"))
            self.assertTrue(result["switches"][0]["explicitWebGl"])
            self.assertEqual(result["switches"][0]["reviewStatus"], "Open")
            self.assertEqual(result["reviewStatus"], "Open")
            self.assertEqual(len(result["sha256"]), 64)


if __name__ == "__main__":
    unittest.main()
