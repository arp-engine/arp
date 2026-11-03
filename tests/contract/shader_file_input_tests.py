"""Input semantics with the real Native FileService; pure lowering is unchanged."""
import argparse
import ast
import builtins
import importlib.util
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from shader_file_inputs import ShaderFileInputs
from spirv_layout import SpirvLayoutFacts
import compile_shaders as compiler
import shader_bindings_codegen as bindings

parser = argparse.ArgumentParser()
parser.add_argument("--executable", type=Path, required=True)
args = parser.parse_args()


class ShaderInputTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="arp-shader-file-input-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.files = ShaderFileInputs(args.executable)

    def test_text_matches_original_utf8_bom_and_universal_newline(self):
        for data in (b"", b"a\r\nb\rc\n", b"\xef\xbb\xbfline\r\n",
                     "Unicode中文\r\n".encode("utf-8"), b"\r\r\n\n"):
            path = self.root / "source.slang"
            path.write_bytes(data)
            with builtins.open(path, encoding="utf-8") as source:
                expected = source.read()
            with patch.object(Path, "read_bytes", side_effect=AssertionError("Python input bypass")):
                self.assertEqual(self.files.read_text(path), expected)
                self.assertEqual(self.files.read_bytes(path), data)

    def test_invalid_utf8_missing_file_or_tool_never_retry(self):
        path = self.root / "invalid.slang"
        path.write_bytes(b"\xff")
        with self.assertRaises(UnicodeDecodeError):
            self.files.read_text(path)
        with self.assertRaises(FileNotFoundError):
            self.files.read_text(self.root / "missing.slang")
        with self.assertRaisesRegex(RuntimeError, "TOOL-MISSING"):
            ShaderFileInputs(self.root / "missing.exe")

    def test_acquired_owned_source_survives_path_mutation(self):
        path = self.root / "owned.slang"
        path.write_bytes(b"original\r\n")
        acquired = self.files._files.read_file(path)
        path.write_bytes(b"changed\n")
        with patch.object(self.files._files, "read_file", return_value=acquired):
            self.assertEqual(self.files.read_text(path), "original\n")

    def test_selected_compiler_readers_have_no_legacy_retry(self):
        before = compiler._file_inputs
        self.addCleanup(setattr, compiler, "_file_inputs", before)
        compiler._file_inputs = self.files
        path = self.root / "source.slang"
        path.write_bytes(b"source\r\n")
        with patch.object(compiler, "open", side_effect=AssertionError("legacy compiler read"), create=True):
            self.assertEqual(compiler.read_text(path), "source\n")
            self.assertEqual(compiler.read_binary(path), b"source\r\n")
            with patch.object(self.files, "read_bytes", side_effect=RuntimeError("source-failed")):
                with self.assertRaisesRegex(RuntimeError, "source-failed"):
                    compiler.read_binary(path)
                with self.assertRaisesRegex(RuntimeError, "source-failed"):
                    compiler.read_text(path)

    def test_empty_owned_spirv_is_validated_without_opening_path(self):
        with patch.object(Path, "read_bytes", side_effect=AssertionError("legacy layout read")):
            with self.assertRaisesRegex(RuntimeError, "SPIRV-LAYOUT-INVALID-BINARY"):
                SpirvLayoutFacts(self.root / "missing.spv", data=b"")
            with self.assertRaisesRegex(RuntimeError, "SPIRV-LAYOUT-INVALID-BINARY"):
                bindings._layout_facts(self.root / "missing.spv", lambda _: None)

    def test_real_cli_missing_tool_and_invalid_options_preserve_outputs(self):
        output = self.root / "output"
        output.mkdir()
        sentinel = output / "sentinel.txt"
        sentinel.write_bytes(b"preserve")
        base = [sys.executable, "-X", "utf8", str(ROOT / "scripts/compile_shaders.py"),
                "--module", "scene_2d_sprites", "--out-dir", str(output)]
        for extra, diagnostic in ((["--io-tool", str(self.root / "missing.exe")], b"TOOL-MISSING"),
                                  (["--io-tool", str(args.executable), "--glsl-es-100"],
                                   b"--glsl-es-100 requires")):
            result = subprocess.run([*base, *extra], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(diagnostic, result.stderr)
            self.assertEqual(list(output.iterdir()), [sentinel])
            self.assertEqual(sentinel.read_bytes(), b"preserve")

    def test_pure_lowering_functions_are_ast_identical_to_original(self):
        original = ast.parse((ROOT / "tests/fixtures/shader_tool_input/original_compile_shaders.py")
                             .read_text(encoding="utf-8"))
        current = ast.parse((ROOT / "scripts/compile_shaders.py").read_text(encoding="utf-8"))
        input_owners = {"read_text", "shader_artifact_file_roles", "publish_generated_shader_artifact_set",
                        "publish_graphics_module_artifact", "publish_single_entry_artifact",
                        "strip_spirv_debug", "selected_entry_has_push_constants", "selected_entry_push_stage", "main"}
        current_functions = {node.name: node for node in current.body if isinstance(node, ast.FunctionDef)}
        checked = 0
        for node in original.body:
            if isinstance(node, ast.FunctionDef) and node.name not in input_owners:
                self.assertEqual(ast.dump(node), ast.dump(current_functions[node.name]), node.name)
                checked += 1
        self.assertGreater(checked, 50)
        self.assertEqual(compiler.SHADER_SOURCES, self.original_manifest(original))

    @staticmethod
    def original_manifest(tree):
        node = next(n for n in tree.body if isinstance(n, ast.Assign)
                    and any(isinstance(target, ast.Name) and target.id == "SHADER_SOURCES" for target in n.targets))
        return ast.literal_eval(node.value)


if __name__ == "__main__":
    unittest.main(argv=[__file__])
