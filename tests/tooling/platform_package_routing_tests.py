"""Exercise platform source admission using CMake's conditional evaluator."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("arp_package_build", ROOT / "build.py")
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


class PlatformPackageRoutingTests(unittest.TestCase):
    def test_platform_routes_admit_only_target_window_and_renderer(self):
        module = build._auto_discover_modules()["arp_platform"]
        prefix = "".join(
            f'set({name} "${{TEST_{name}}}")\n'
            for name in ("CMAKE_SYSTEM_NAME", "WIN32", "UNIX", "ANDROID", "APPLE", "IOS"))
        prefix += 'set(ARP_SELECTED_WEB_RHI "${TEST_WEB_RHI}")\n'
        for route, sources in module.platform_srcs.items():
            condition = build.CmakeWriter._cmake_module_platform_condition(module.name, route)
            prefix += (f'if({condition})\n'
                       f'  message(STATUS "SOURCES={";".join(sources)}")\n'
                       'endif()\n')
        cases = (
            ("Windows", True, False, False, False, False, "none", {"windows", "vulkan"}),
            ("Linux", False, True, False, False, False, "none", {"linux", "vulkan"}),
            ("Android", False, True, True, False, False, "none", {"vulkan"}),
            ("Darwin", False, True, False, True, False, "none", {"macos", "shared", "metal"}),
            ("iOS", False, True, False, True, True, "none", {"shared", "metal"}),
            ("Emscripten", False, True, False, False, False, "webgpu", {"web", "shared", "webgpu"}),
            ("Emscripten", False, True, False, False, False, "none", set()),
        )
        source_labels = {
            "window_win32.cpp": "windows", "window_xcb.cpp": "linux",
            "window_cocoa.mm": "macos", "window_web.cpp": "web",
            "render_system_native_shared.cpp": "shared",
            "render_system_vulkan.cpp": "vulkan", "render_system_metal.cpp": "metal",
            "render_system_webgpu.cpp": "webgpu",
        }
        with TemporaryDirectory() as directory:
            driver = Path(directory) / "routes.cmake"
            driver.write_text(prefix, encoding="utf-8")
            keys = ("CMAKE_SYSTEM_NAME", "WIN32", "UNIX", "ANDROID", "APPLE", "IOS", "WEB_RHI")
            for *values, expected in cases:
                with self.subTest(platform=values[0], product=values[-1]):
                    args = [f'-DTEST_{key}={str(value).upper() if isinstance(value, bool) else value}'
                            for key, value in zip(keys, values)]
                    result = subprocess.run([shutil.which("cmake"), *args, "-P", str(driver)],
                                            capture_output=True, text=True, check=True)
                    actual = set()
                    for line in result.stdout.splitlines():
                        if line.startswith("-- SOURCES="):
                            for source in line.removeprefix("-- SOURCES=").split(";"):
                                self.assertTrue((ROOT / source).is_file(), source)
                                actual.add(source_labels[Path(source).name])
                    self.assertEqual(actual, expected)
        self.assertTrue(all(Path(source).parent.name in {"common", "android", "ios"}
                            for source in module.srcs))

    def test_ios_bundle_sources_and_resources_resolve_after_move(self):
        bundle = build._auto_discover_modules()["arp_ios_smoke_app"]
        for source in (*bundle.srcs, bundle.info_plist, *bundle.resources):
            self.assertTrue((ROOT / source).is_file(), source)
            self.assertTrue(source.startswith("src/platform/ios/"), source)


if __name__ == "__main__":
    unittest.main()
