"""Real Ninja dependency gates for the independent offline package IO host."""
import argparse
import subprocess
import unittest

parser = argparse.ArgumentParser()
parser.add_argument("--ninja", required=True)
parser.add_argument("--build-dir", required=True)
args = parser.parse_args()


class InventoryBuildBoundaryTests(unittest.TestCase):
    def inputs(self, target):
        return subprocess.check_output([args.ninja, "-C", args.build_dir, "-t", "inputs", target],
                                       text=True, timeout=30).replace("\\", "/")

    def test_core_has_no_package_tool_dependency(self):
        inputs = self.inputs("arp_core")
        self.assertNotIn("/tools/", inputs)
        self.assertNotIn("survivors_package_inventory", inputs)

    def test_package_tool_reuses_file_service_without_shader_or_editor_bootstrap(self):
        inputs = self.inputs("arp_survivors_package_inventory")
        for required in ("/tools/survivors/package_file_inventory.cpp", "/tools/io/file_inventory.cpp", "/tools/io/cold_tool_files.cpp",
                         "/src/core/io/file_service.cpp", "/src/core/hash/sha256.cpp"):
            self.assertIn(required, inputs)
        for forbidden in ("/editor/", "/src/rhi/", "/src/renderer/", "/src/scriptvm/",
                          "generated/shaders/", "arp_compile_shaders", "/examples/survivors/"):
            self.assertNotIn(forbidden, inputs)

    def test_shared_owner_has_no_legacy_or_shader_dependency(self):
        inputs = self.inputs("arp_tool_file_inventory")
        for required in ("/tools/io/file_inventory.cpp", "/tools/io/file_inventory_cli.cpp",
                         "/tools/io/cold_tool_files.cpp", "/src/core/io/file_service.cpp"):
            self.assertIn(required, inputs)
        for forbidden in ("/tools/survivors/", "/editor/", "/src/rhi/", "/src/renderer/",
                          "/src/scriptvm/", "arp_compile_shaders", "/examples/survivors/"):
            self.assertNotIn(forbidden, inputs)

    def test_primary_shader_producer_consumes_the_shared_input_tool(self):
        inputs = self.inputs("arp_compile_shaders")
        for required in ("/tools/io/file_inventory_main.cpp", "/scripts/shader_file_inputs.py",
                         "/scripts/tool_file_inventory.py"):
            self.assertIn(required, inputs)
        self.assertNotIn("/tools/survivors/", inputs)

    def test_game_build_supplies_the_explicit_offline_helper(self):
        self.assertIn("/tools/survivors/package_inventory_main.cpp", self.inputs("arp_survivors"))


if __name__ == "__main__":
    unittest.main(argv=[__file__])
