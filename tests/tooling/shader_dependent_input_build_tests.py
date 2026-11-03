"""Authoritative Ninja command/dependency checks for Native Shader input selection."""
import argparse
from pathlib import Path
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("--ninja", required=True)
parser.add_argument("--build-dir", required=True)
args = parser.parse_args()


class ShaderBuildTests(unittest.TestCase):
    def ninja(self, operation, target):
        return subprocess.check_output([args.ninja, "-C", args.build_dir, "-t", operation, target],
                                       text=True, timeout=60).replace("\\", "/")

    def test_every_direct_cmake_compiler_uses_the_shared_explicit_profile(self):
        count = 0
        for path in [ROOT / "CMakeLists.txt", *(ROOT / "cmake").rglob("*.cmake")]:
            for line in path.read_text(encoding="utf-8").splitlines():
                if re.search(r'^\s*COMMAND .*?/scripts/compile_shaders\.py"', line):
                    self.assertIn("${ARP_SHADER_TOOL_INPUT_ARGS}", line, str(path))
                    count += 1
        self.assertGreater(count, 70)

    def test_real_native_primary_bindless_and_es100_commands_select_file_service(self):
        for target in ("arp_compile_shaders", "arp_bindless_matrix_memory_fixture",
                       "arp_bindless_pbr_record_fixture", "arp_bindless_gpu_visibility_public_shaders",
                       "arp_webgl_shader_program_artifacts"):
            with self.subTest(target=target):
                commands = [line for line in self.ninja("commands", target).splitlines()
                            if "/scripts/compile_shaders.py " in line]
                self.assertTrue(commands)
                for command in commands:
                    self.assertRegex(command, r'--io-tool [^\r\n]*arp_tool_file_inventory\.exe')
                inputs = self.ninja("inputs", target)
                for source in ("/tools/io/file_inventory_main.cpp", "/scripts/shader_file_inputs.py",
                               "/scripts/tool_file_inventory.py"):
                    self.assertIn(source, inputs)

    def test_host_tool_has_no_shader_editor_or_game_dependency(self):
        inputs = self.ninja("inputs", "arp_tool_file_inventory")
        for forbidden in ("/scripts/compile_shaders.py", "/src/shader/", "/src/rhi/", "/src/renderer/",
                          "/tools/survivors/", "/editor/", "/examples/"):
            self.assertNotIn(forbidden, inputs)
        self.assertIn("/src/core/io/file_service.cpp", inputs)


if __name__ == "__main__":
    unittest.main(argv=[__file__])
