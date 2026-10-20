"""Exercise the standard editor distribution and explicit engine-only build flow."""
import importlib.util
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("arp_build", ROOT / "scripts/build.py")
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


class EditorBuildTests(unittest.TestCase):
    def arguments(self, **overrides):
        values = dict(preset="win-vulkan-debug", config="Debug", no_editor=False,
                      with_shaders=False, with_games=False)
        values.update(overrides)
        return SimpleNamespace(**values)

    @patch.object(build, "_cmake")
    def test_engine_distribution_includes_editor(self, cmake):
        build.cmd_engine(self.arguments())
        cmake.assert_called_once_with(["--build", "--preset", "win-vulkan-debug",
                                       "--target", "arp_engine", "arp_editor", "arp_editor_cli",
                                       "--config", "Debug"])

    @patch.object(build, "_cmake")
    def test_engine_only_does_not_require_editor(self, cmake):
        args = self.arguments()
        args.no_editor = True
        build.cmd_engine(args)
        cmake.assert_called_once_with(["--build", "--preset", "win-vulkan-debug",
                                       "--target", "arp_engine", "--config", "Debug"])

    @patch.object(build, "_cmake")
    def test_editor_has_dedicated_build_entry(self, cmake):
        build.cmd_editor(self.arguments())
        cmake.assert_called_once_with(["--build", "--preset", "win-vulkan-debug",
                                       "--target", "arp_editor", "arp_editor_cli",
                                       "--config", "Debug"])

    @patch.object(build, "_cmake")
    def test_full_distribution_includes_editor_and_shaders(self, cmake):
        build.cmd_all(self.arguments())
        self.assertEqual(cmake.call_args_list[0].args[0],
                         ["--build", "--preset", "win-vulkan-debug", "--target",
                          "arp_engine", "arp_editor", "arp_editor_cli", "--config", "Debug"])
        self.assertEqual(cmake.call_args_list[1].args[0],
                         ["--build", "--preset", "win-vulkan-debug", "--target",
                          "arp_compile_shaders", "--config", "Debug"])

    @patch.object(build, "_cmake")
    def test_web_editor_does_not_request_native_cli(self, cmake):
        args = self.arguments(preset="web-webgpu-debug")
        build.cmd_editor(args)
        cmake.assert_called_once_with(["--build", "--preset", "web-webgpu-debug",
                                       "--target", "arp_editor", "--config", "Debug"])


if __name__ == "__main__":
    unittest.main()
