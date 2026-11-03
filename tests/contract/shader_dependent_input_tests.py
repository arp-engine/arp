"""Real cold input and Cross lifecycle contracts; native compilation is not GPU execution."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import compile_shaders as compiler
import bindless_codegen
from shader_file_inputs import ShaderFileInputs
from spirv_cross_api import SpirvCrossCompiler
from webgl_shader_artifact import WebGlShaderTargetCompiler, publish_program

parser = argparse.ArgumentParser()
parser.add_argument("--executable", type=Path, required=True)
parser.add_argument("--slangc", type=Path, required=True)
parser.add_argument("--spirv-reflect", type=Path, required=True)
parser.add_argument("--library", type=Path, required=True)
args = parser.parse_args()


class DependentInputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workspace = tempfile.TemporaryDirectory(prefix="arp-dependent-unit-")
        cls.root = Path(cls.workspace.name)
        cls.fixture = cls.root / "original"
        command = [sys.executable, "-X", "utf8", str(ROOT / "scripts/compile_shaders.py"),
                   "--slangc", str(args.slangc), "--spirv-reflect", str(args.spirv_reflect),
                   "--io-tool", str(args.executable), "--fixture", "webgl1_graphics", "--graphics-module",
                   "--preserve-implicit-sampling", "--glsl-es-100", "--spirv-cross-library", str(args.library),
                   "--webgl-optional-uniform", "Vertex:arpUbo_0_0.m3",
                   "--webgl-optional-uniform", "Fragment:arpUbo_0_0.m3", "--out-dir", str(cls.fixture)]
        result = subprocess.run(command, capture_output=True, timeout=120)
        if result.returncode:
            cls.workspace.cleanup()
            raise RuntimeError(result.stderr.decode("utf-8", errors="replace"))
        cls.log = result.stdout.decode("utf-8")
        cls.spirv = (cls.fixture / "Vertex/webgl1_graphics.spv").read_bytes()

    @classmethod
    def tearDownClass(cls):
        cls.workspace.cleanup()

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir=self.root, prefix="case-")
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.fixture_copy = self.folder / "fixture"
        shutil.copytree(self.fixture, self.fixture_copy)
        self.inputs = ShaderFileInputs(args.executable)

    def test_direct_cli_parent_and_real_children_select_same_source(self):
        self.assertEqual(self.log.count("FileService/1"), 3)
        self.assertNotIn("LegacyHostFile/1", self.log)
        self.assertNotIn("UNMIGRATED", self.log)

    def test_nested_scope_and_bindless_failure_restore_prior_source(self):
        previous = compiler._file_inputs
        token = object()
        with compiler.ShaderFileInputScope(token):
            self.assertIs(compiler._file_inputs, token)
            with self.assertRaisesRegex(bindless_codegen.BindlessContractError, "EXECUTION-MODE"):
                bindless_codegen.compile_bindless_package({"name": "ScopeProbe"}, None, [], "Invalid",
                    self.folder, args.slangc, args.spirv_reflect, input_files=self.inputs)
            self.assertIs(compiler._file_inputs, token)
        self.assertIs(compiler._file_inputs, previous)

    def test_visibility_closed_schema_uses_explicit_input_even_in_pure_generator(self):
        import gpu_visibility_codegen as visibility
        for consumer in (False, True):
            document = visibility.schema(consumer, input_files=self.inputs)
            with patch.object(Path, "read_text", side_effect=AssertionError("closed schema bypass")):
                generated = bindless_codegen.generate_bindless(document, "Emulated", input_files=self.inputs)
            self.assertEqual(generated.reflection["gpuVisibilityKernelContractVersion"], 1)

    def test_native_cross_owned_bytes_need_no_physical_spirv_path(self):
        with patch.object(Path, "read_bytes", side_effect=AssertionError("Cross path read bypass")):
            with SpirvCrossCompiler(args.library, self.folder / "missing.spv", data=self.spirv) as cross:
                self.assertEqual(cross.version(), [0, 68, 0])
                cross.configure()
                self.assertIn("#version 100", cross.compile())
            self.assertFalse(cross.context.value)

    def test_invalid_owned_cross_inputs_close_real_context_without_path_retry(self):
        closed = []
        original = SpirvCrossCompiler.close
        def close(cross):
            original(cross)
            closed.append(cross.context.value)
        with patch.object(SpirvCrossCompiler, "close", close), \
                patch.object(Path, "read_bytes", side_effect=AssertionError("Cross legacy retry")):
            for invalid in (None, b"", b"invalid", b"\x00" * 20):
                with self.assertRaisesRegex(RuntimeError, "SHADER-WEBGL1-SPIRV-BINARY"):
                    SpirvCrossCompiler(args.library, self.folder / "missing.spv", data=invalid)
        self.assertEqual(closed, [None] * 4)

    def test_same_validated_spirv_receipt_feeds_layout_and_native_converter(self):
        class MutatingInputs(ShaderFileInputs):
            reads = 0
            def read_bytes(self, path):
                data = super().read_bytes(path)
                if Path(path).suffix == ".spv":
                    self.reads += 1
                    Path(path).write_bytes(b"mutated after acquisition")
                return data
        inputs = MutatingInputs(args.executable)
        entry = self.fixture_copy / "Vertex/webgl1_graphics.vertexMain.entry.yaml"
        target = WebGlShaderTargetCompiler(args.library, args.slangc, input_files=inputs)
        with patch.object(Path, "read_bytes", side_effect=AssertionError("unified source bypass")), \
                patch.object(Path, "read_text", side_effect=AssertionError("unified text bypass")):
            result = target.generate(entry, ["arpUbo_0_0.m3"])
        self.assertEqual(result["status"], "Available")
        self.assertEqual(inputs.reads, 1)
        self.assertEqual((entry.parent / "webgl1_graphics.spv").read_bytes(), b"mutated after acquisition")

    def test_original_publication_identity_uses_the_same_acquired_document(self):
        entry = (self.fixture_copy / "Vertex/webgl1_graphics.vertexMain.entry.yaml").resolve()
        expected = hashlib.sha256(entry.read_bytes()).hexdigest()
        class MutatingPublication(ShaderFileInputs):
            reads = 0
            def read_bytes(self, path):
                data = super().read_bytes(path)
                if Path(path).resolve() == entry:
                    self.reads += 1
                    entry.write_bytes(b"changed after entry acquisition")
                return data
        inputs = MutatingPublication(args.executable)
        target = WebGlShaderTargetCompiler(args.library, args.slangc, input_files=inputs)
        with patch.object(Path, "read_bytes", side_effect=AssertionError("entry path retry")), \
                patch.object(Path, "read_text", side_effect=AssertionError("entry text bypass")):
            result = target.generate(entry, ["arpUbo_0_0.m3"])
        self.assertEqual(inputs.reads, 1)
        self.assertEqual(result["original"]["sha256"], expected)
        self.assertNotEqual(result["original"]["sha256"], hashlib.sha256(entry.read_bytes()).hexdigest())

    def test_source_failure_and_bad_role_hash_do_not_fabricate_success(self):
        entry = self.fixture_copy / "Vertex/webgl1_graphics.vertexMain.entry.yaml"
        compiler_instance = WebGlShaderTargetCompiler(args.library, args.slangc, input_files=self.inputs)
        with patch.object(self.inputs, "read_bytes", side_effect=RuntimeError("IO-TEST-REFUSED")), \
                patch.object(Path, "read_text", side_effect=AssertionError("legacy fallback")):
            with self.assertRaisesRegex(RuntimeError, "IO-TEST-REFUSED"):
                compiler_instance.publish(entry)
        value = yaml.safe_load(entry.read_text(encoding="utf-8"))
        value["files"]["spirv"]["sha256"] = "0" * 64
        entry.write_text(yaml.safe_dump(value, sort_keys=False), encoding="utf-8")
        path = compiler_instance.publish(entry)
        refused = yaml.safe_load(path.read_text(encoding="utf-8"))
        self.assertEqual(refused["status"], "DiagnosticFailure")
        self.assertEqual(refused["diagnosticId"], "SHADER-WEBGL1-ORIGINAL-HASH")
        self.assertFalse((entry.parent / (entry.stem + ".glsl-es-100")).exists())

    def test_program_io_and_hash_only_receipts_use_real_shared_host(self):
        vertex = self.fixture_copy / "Vertex/webgl1_graphics.vertexMain.entry.glsl-es-100.yaml"
        fragment = self.fixture_copy / "Fragment/webgl1_graphics.fragmentMain.entry.glsl-es-100.yaml"
        expected = hashlib.sha256(args.library.read_bytes()).hexdigest()
        with patch.object(Path, "read_bytes", side_effect=AssertionError("host byte bypass")), \
                patch.object(Path, "read_text", side_effect=AssertionError("host text bypass")):
            self.assertEqual(self.inputs.digest(args.library), expected)
            publication = publish_program(vertex, fragment, self.fixture_copy / "recomposed.yaml", input_files=self.inputs)
        value = yaml.safe_load(publication.read_text(encoding="utf-8"))
        for record in value["targets"]:
            self.assertEqual(record["sha256"], hashlib.sha256((publication.parent / record["path"]).read_bytes()).hexdigest())

    def test_program_targets_identify_the_same_consumed_stage_documents(self):
        vertex = (self.fixture_copy / "Vertex/webgl1_graphics.vertexMain.entry.glsl-es-100.yaml").resolve()
        fragment = (self.fixture_copy / "Fragment/webgl1_graphics.fragmentMain.entry.glsl-es-100.yaml").resolve()
        expected = [hashlib.sha256(path.read_bytes()).hexdigest() for path in (vertex, fragment)]
        class MutatingStages(ShaderFileInputs):
            reads = 0
            def read_bytes(self, path):
                data = super().read_bytes(path)
                if Path(path).resolve() in (vertex, fragment):
                    self.reads += 1
                    Path(path).write_bytes(b"changed stage after acquisition")
                return data
        inputs = MutatingStages(args.executable)
        with patch.object(Path, "read_text", side_effect=AssertionError("program document retry")), \
                patch.object(Path, "read_bytes", side_effect=AssertionError("program byte bypass")):
            output = publish_program(vertex, fragment, self.fixture_copy / "owned-program.yaml", input_files=inputs)
        result = yaml.safe_load(output.read_text(encoding="utf-8"))
        self.assertEqual(inputs.reads, 2)
        self.assertEqual([record["sha256"] for record in result["targets"]], expected)
        self.assertNotEqual(result["targets"][0]["sha256"], hashlib.sha256(vertex.read_bytes()).hexdigest())

    def test_publication_reader_envelope_integrity_uses_owned_inputs(self):
        # Transport-only payload: this checks the envelope reader, and grants
        # no shader package validation or GPU execution status.
        payload = self.folder / "transport.payload.yaml"
        data = b"type: transport-only-fixture\ncontractVersion: 1\n"
        payload.write_bytes(data)
        publication = self.folder / "transport.bindless.yaml"
        publication.write_text(yaml.safe_dump(dict(type="BindlessPackagePublication", contractVersion=1,
            payload=payload.name, shaderPackageIdentity=hashlib.sha256(data).hexdigest()), sort_keys=False),
            encoding="utf-8")
        with patch.object(Path, "read_text", side_effect=AssertionError("publication text bypass")), \
                patch.object(Path, "read_bytes", side_effect=AssertionError("publication byte bypass")):
            value = bindless_codegen.read_bindless_publication(publication, input_files=self.inputs)
            self.assertEqual(value["type"], "transport-only-fixture")
            payload.write_bytes(data + b"changed")
            with self.assertRaisesRegex(bindless_codegen.BindlessContractError, "PACKAGE-HASH-MISMATCH"):
                bindless_codegen.read_bindless_publication(publication, input_files=self.inputs)

    def test_ordinary_native_context_lifecycle_closes_twenty_times(self):
        with patch.object(Path, "read_bytes", side_effect=AssertionError("native context path read")):
            for _ in range(20):
                with SpirvCrossCompiler(args.library, self.folder / "missing.spv", data=self.spirv) as cross:
                    self.assertTrue(cross.context.value)
                self.assertFalse(cross.context.value)


if __name__ == "__main__":
    unittest.main(argv=[__file__])
