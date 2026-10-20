"""Build isolation/failure checks; mock compilers never prove native execution."""
import importlib.util
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

import yaml

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("webgl1_probe_build", ROOT / "scripts/webgl1_probe_build.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
Builder = module.WebGl1ProbeBuilder


class ProbeBuildTests(unittest.TestCase):
    def builder(self, output):
        # A real local file supplies tool identity; it is never executed by
        # the negative tests, which inject the failure before compilation.
        return Builder(ROOT, output, {name: sys.executable for name in
                                     ("slangc", "spirvCross", "spirvReflect", "emxx")})

    def test_webgl1_limits_and_no_webgpu_bridge(self):
        with TemporaryDirectory() as output:
            commands = self.builder(output).commands(Path(output))
            compile_command = commands[1]
            self.assertIn("-sMIN_WEBGL_VERSION=1", compile_command)
            self.assertIn("-sMAX_WEBGL_VERSION=1", compile_command)
            self.assertFalse(any("emdawnwebgpu" in token or "WEBGL_VERSION=2" in token
                                 for command in commands for token in command))

    def test_missing_tool_and_source_output_fail_before_creating_run(self):
        with TemporaryDirectory() as output:
            builder = self.builder(output)
            builder.tools["emxx"] = Path(output) / "absent-emxx.exe"
            with self.assertRaisesRegex(ValueError, "TOOL-MISSING"):
                builder.build()
            self.assertEqual(list(Path(output).iterdir()), [])
        builder = self.builder(ROOT / "src/generated-probe")
        with self.assertRaisesRegex(ValueError, "OUTPUT-SOURCE"):
            builder.validate()

    def test_failed_run_cannot_publish_success_or_reuse_old_evidence(self):
        with TemporaryDirectory() as output:
            builder = self.builder(output)
            failure = subprocess.CalledProcessError(17, ["fixture-compiler"])
            with patch.object(builder, "execute", side_effect=failure):
                with self.assertRaises(subprocess.CalledProcessError):
                    builder.build()
                first = builder.run_directory
                with self.assertRaises(subprocess.CalledProcessError):
                    builder.build()
            self.assertNotEqual(first, builder.run_directory)
            for directory in (first, builder.run_directory):
                manifest = yaml.safe_load((directory / "build-manifest.yaml").read_text())
                self.assertEqual(manifest["status"], "Failed")
                self.assertFalse(manifest["builtSuccessfully"])
                self.assertFalse(manifest["executablePackage"])
                self.assertFalse(manifest["productionBackendValidated"])
                self.assertEqual(manifest["failedExitCode"], 17)
                self.assertNotIn("artifacts", manifest)

    def test_source_change_during_build_refuses_provenance(self):
        with TemporaryDirectory() as output:
            builder = self.builder(output)

            def fake_compile(command, log):
                log.write_text("Fixture only", encoding="utf-8")

            with patch.object(builder, "execute", side_effect=fake_compile), patch.object(
                    builder, "source_identities", side_effect=[{}, {}, {"changed": {}}]):
                with self.assertRaisesRegex(ValueError, "INPUT-CHANGED"):
                    builder.build()
            manifest = yaml.safe_load((builder.run_directory / "build-manifest.yaml").read_text())
            self.assertFalse(manifest["builtSuccessfully"])
            self.assertEqual(manifest["status"], "Failed")
            self.assertNotIn("artifacts", manifest)


if __name__ == "__main__":
    unittest.main()
