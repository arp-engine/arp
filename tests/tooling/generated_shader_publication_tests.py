"""Publication schema tests; byte fixtures do not authorize shader execution."""
import hashlib
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from compile_shaders import publish_generated_shader_artifact_set
import yaml


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = pathlib.Path(self.folder.name)
        self.metadata = {"sourcePath": "original/source.slang", "targets": {
            "spirv": "original/code.spv", "metal": "original/code.metal", "wgsl": "original/code.wgsl"}}
        for suffix in ("slang", "spv", "metal", "wgsl", "reflection.yaml"):
            (self.root / ("unit." + suffix)).write_bytes((suffix + "\n").encode())

    def publish(self):
        output = publish_generated_shader_artifact_set("unit", str(self.root), self.metadata)
        return yaml.safe_load(pathlib.Path(output).read_text(encoding="utf-8"))

    def test_exact_nested_roles_and_bindings(self):
        result = self.publish()
        self.assertEqual(result["reflectionBindings"], {"source": self.metadata["sourcePath"], **self.metadata["targets"]})
        self.assertEqual(result["contractVersion"], 1)
        for role, value in result["files"].items():
            self.assertIsInstance(value, dict)
            self.assertEqual(value["sha256"], hashlib.sha256((self.root / value["path"]).read_bytes()).hexdigest())
        self.assertEqual(len(result["files"]), 5)
        self.assertEqual(result, self.publish())

    def unavailable(self, role, reason):
        (self.root / ("unit." + role)).unlink()
        self.metadata["targets"][role] = None
        self.metadata["metalBindingSpaceUnavailableReason" if role == "metal" else "webGpuBindingSpaceUnavailableReason"] = reason

    def test_explicit_wgsl_failure(self):
        self.unavailable("wgsl", "WGSL compilation failed")
        value = self.publish()["files"]["wgsl"]
        self.assertEqual(value, {"status": "DiagnosticFailure", "diagnosticId": "SHADER-GENERATED-WGSL-UNAVAILABLE", "reason": "WGSL compilation failed"})

    def test_explicit_metal_failure(self):
        self.unavailable("metal", "SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED")
        self.assertEqual(self.publish()["files"]["metal"]["diagnosticId"], "SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED")

    def test_unknown_failure_rejects(self):
        self.unavailable("wgsl", "unexpected compiler reason")
        with self.assertRaisesRegex(RuntimeError, "UNAVAILABLE-CONTRACT"):
            self.publish()

    def test_failure_cannot_switch_target_domains(self):
        self.unavailable("metal", "WGSL compilation failed")
        with self.assertRaisesRegex(RuntimeError, "UNAVAILABLE-CONTRACT"):
            self.publish()

    def test_unavailable_target_must_not_exist(self):
        self.metadata["targets"]["wgsl"] = None
        self.metadata["webGpuBindingSpaceUnavailableReason"] = "WGSL compilation failed"
        with self.assertRaisesRegex(RuntimeError, "UNAVAILABLE-CONTRACT"):
            self.publish()

    def test_empty_file_rejects(self):
        (self.root / "unit.spv").write_bytes(b"")
        with self.assertRaisesRegex(RuntimeError, "FILE-EMPTY"):
            self.publish()


if __name__ == "__main__":
    unittest.main()
