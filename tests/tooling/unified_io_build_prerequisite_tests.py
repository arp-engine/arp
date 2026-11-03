"""Target-scoped build selection, not compiler or backend execution authority."""
from contextlib import redirect_stdout
from io import StringIO
import importlib.util
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
spec = importlib.util.spec_from_file_location("unified_io_build", ROOT / "scripts/build.py")
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


class PrerequisiteTests(unittest.TestCase):
    def submit(self, target, exists):
        args = SimpleNamespace(preset="win-vulkan-debug", config="Debug", target=target)
        with patch.object(build, "_ctest_regex_for_target", return_value="^(selected)$"), \
                patch.object(build, "_cmake_target_exists", return_value=exists), \
                patch.object(build, "_cmake") as cmake, patch.object(build, "_ctest") as ctest, \
                redirect_stdout(StringIO()):
            build.cmd_test(args)
        ctest.assert_called_once_with("win-vulkan-debug", "^(selected)$", config="Debug")
        return cmake

    def targets(self, cmake):
        command = cmake.call_args.args[0]
        return command[command.index("--target") + 1:command.index("--config")]

    def test_authoring_only_builds_the_file_input_tool(self):
        targets = self.targets(self.submit("arp_survivors_authoring_input_comparison", False))
        self.assertEqual(targets, ["arp_survivors_package_inventory"])
        self.assertNotIn("arp_survivors", targets)

    def test_native_package_comparison_prepares_actual_role_producers(self):
        self.assertEqual(self.targets(self.submit("arp_survivors_package_inventory_comparison", False)),
                         ["arp_survivors_package_inventory", "arp_cold_tool_files_tests",
                          "arp_compile_shaders", "arp_survivors_script_outputs"])

    def test_real_executable_target_keeps_additional_fixture_dependencies(self):
        targets = self.targets(self.submit("arp_survivors_package_inventory", True))
        self.assertEqual(targets, ["arp_survivors_package_inventory", "arp_cold_tool_files_tests",
                                  "arp_compile_shaders", "arp_survivors_script_outputs"])

    def test_duplicate_declared_target_is_built_once(self):
        name = "arp_survivors_record_tests"
        self.assertEqual(self.targets(self.submit(name, True)), [name])

    def test_memory_and_generation_smokes_use_their_actual_executables(self):
        for test, executable in (("arp_editor_scene_save_admission_memory-smoke", "arp_editor_scene_save_admission_tests"),
                                 ("arp_editor_scene_persistence_io_smoke", "arp_editor_scene_persistence_io_tests"),
                                 ("arp_core_versioned_yaml_store_worker_smoke", "arp_core_versioned_yaml_store_tests"),
                                 ("arp_resource_package_source_output_smoke", "arp_resource_package_output_tests"),
                                 ("arp_resource_package_terrain_output_smoke", "arp_resource_package_output_tests"),
                                 ("arp_survivors_package_inventory_wasm_smoke", "arp_survivors_package_inventory_wasm_tests")):
            with self.subTest(test=test):
                self.assertEqual(self.targets(self.submit(test, False)), [executable])

    def test_shared_input_and_shader_checks_build_their_exact_cpu_tools(self):
        self.assertEqual(self.targets(self.submit("arp_tool_file_inventory", True)),
                         ["arp_tool_file_inventory", "arp_survivors_package_inventory"])
        for name in ("arp_shader_file_input_tests", "arp_shader_file_input_comparison", "arp_shader_file_input_smoke", "arp_shader_dependent_input_tests",
                     "arp_shader_dependent_input_comparison", "arp_shader_dependent_input_smoke"):
            self.assertEqual(self.targets(self.submit(name, False)), ["arp_tool_file_inventory"])
        self.assertEqual(self.targets(self.submit("arp_tool_file_inventory_cli_tests", False)),
                         ["arp_tool_file_inventory", "arp_survivors_package_inventory"])
        self.assertEqual(self.targets(self.submit("arp_tool_file_inventory_smoke", False)),
                         ["arp_tool_file_inventory_tests"])

    def test_plain_metadata_check_requires_no_build(self):
        self.submit("arp_unified_io_inventory_gate", False).assert_not_called()


if __name__ == "__main__":
    unittest.main(argv=[__file__])
