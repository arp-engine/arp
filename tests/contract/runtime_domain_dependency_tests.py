"""Negative and current-source architecture contracts for runtime domains."""

import importlib.util
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from runtime_domain_dependencies import RuntimeDomainDependencies
from cmake_gen import CmakeWriter


class RuntimeDomainDependencyTests(unittest.TestCase):
    def test_current_source_has_no_reverse_dependency(self):
        self.assertEqual(RuntimeDomainDependencies(ROOT).source_diagnostics(), [])

    def test_rejects_header_namespace_and_allocator_violations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "src/3d/terrain/test.h"
            target.parent.mkdir(parents=True)
            target.write_text('#include "pcg/terrain_heightfield.h"\n'
                              '#include "renderer/terrain_runtime.h"\n'
                              'std::vector<pcg::TerrainStreamer> forbidden;\n', encoding="utf-8")
            self.assertEqual(len(RuntimeDomainDependencies(root).source_diagnostics()), 3)

    def test_rejects_transitive_source_and_interface_link_dependencies(self):
        targets = {name: ("", "") for name in RuntimeDomainDependencies.runtime_targets}
        targets["arp_terrain"] = ("src/3d/terrain/terrain_runtime.cpp", "$<LINK_ONLY:leak>")
        targets["leak"] = ("", "arp_pcg")
        targets["arp_pcg"] = ("src/pcg/world_terrain.cpp", "arp_core")
        diagnostics = RuntimeDomainDependencies.target_diagnostics(targets)
        self.assertTrue(any("arp_terrain -> leak -> arp_pcg" in value for value in diagnostics))
        targets["leak"] = ("src/pcg/world_terrain.cpp", "")
        self.assertTrue(RuntimeDomainDependencies.target_diagnostics(targets))

    def test_rejects_relative_include_and_object_library_leak(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "src/3d/terrain/test.h"
            target.parent.mkdir(parents=True)
            target.write_text('#include "../../pcg/terrain_streamer.h"\n', encoding="utf-8")
            self.assertTrue(RuntimeDomainDependencies(root).source_diagnostics())
        targets = {name: ("", "") for name in RuntimeDomainDependencies.runtime_targets}
        targets["arp_terrain"] = ("$<TARGET_OBJECTS:producer>", "")
        targets["producer"] = ("src/pcg/world_terrain.cpp", "")
        self.assertTrue(RuntimeDomainDependencies.target_diagnostics(targets))

    def test_rejects_direct_archive_link(self):
        targets = {name: ("", "") for name in RuntimeDomainDependencies.runtime_targets}
        targets["arp_terrain"] = ("", "D:/build/libarp_pcgd.a")
        self.assertTrue(RuntimeDomainDependencies.target_diagnostics(targets))

    def test_all_3d_domains_reject_scene_and_allocator_dependencies(self):
        for domain in ("lod", "terrain", "vegetation"):
            with self.subTest(domain=domain), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                target = root / "src/3d" / domain / "test.h"
                target.parent.mkdir(parents=True)
                target.write_text('#include "3d/components.h"\n'
                                  'std::vector<int> forbidden;\n', encoding="utf-8")
                diagnostics = RuntimeDomainDependencies(root).source_diagnostics()
                self.assertEqual(len(diagnostics), 2)
                self.assertTrue(any("UPPER-LAYER-DEPENDENCY" in value for value in diagnostics))
                self.assertTrue(any("ALLOCATOR-BYPASS" in value for value in diagnostics))
        targets = {name: ("", "") for name in RuntimeDomainDependencies.runtime_targets}
        targets["arp_terrain"] = ("", "bridge")
        targets["bridge"] = ("", "arp_3d")
        targets["arp_3d"] = ("src/3d/components.cpp", "arp_scene")
        targets["arp_scene"] = ("", "")
        self.assertTrue(any("arp_terrain -> bridge -> arp_3d" in value
                            for value in RuntimeDomainDependencies.target_diagnostics(targets)))

    def test_old_directories_are_removed_and_cannot_return(self):
        for domain in ("lod", "terrain", "vegetation"):
            self.assertFalse((ROOT / "src" / domain).exists())
            self.assertTrue(list((ROOT / "src/3d" / domain).glob("*.h")))
            with self.subTest(domain=domain), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                (root / "src" / domain).mkdir(parents=True)
                self.assertEqual(RuntimeDomainDependencies(root).source_diagnostics(),
                                 [f"RUNTIME-DOMAIN-LEGACY-DIRECTORY: src/{domain}"])

    def test_producers_cannot_use_removed_include_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "src/pcg/producer.h"
            target.parent.mkdir(parents=True)
            target.write_text('#include "terrain/terrain_surface.h"\n', encoding="utf-8")
            self.assertTrue(any("LEGACY-INCLUDE" in value
                                for value in RuntimeDomainDependencies(root).source_diagnostics()))

    def test_terrain_integration_has_explicit_upper_layer_and_allocator_boundary(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "src/3d/terrain/integration/session.h"
            target.parent.mkdir(parents=True)
            target.write_text('#include "renderer/scene_terrain_preparation.h"\n'
                              '#include "resource/resource_manager.h"\n'
                              '#include "3d/components.h"\n', encoding="utf-8")
            self.assertEqual(RuntimeDomainDependencies(root).source_diagnostics(), [])
            target.write_text(target.read_text() + 'std::vector<int> forbidden;\n'
                              '#include "pcg/terrain_heightfield.h"\n', encoding="utf-8")
            diagnostics = RuntimeDomainDependencies(root).source_diagnostics()
            self.assertEqual(len(diagnostics), 2)
            self.assertTrue(any("PCG-DEPENDENCY" in value for value in diagnostics))
            self.assertTrue(any("ALLOCATOR-BYPASS" in value for value in diagnostics))

    def test_foundational_domains_cannot_include_terrain_integration(self):
        for domain in ("lod", "terrain", "vegetation"):
            for include in ('3d/terrain/integration/terrain_scene_session.h',
                            '../integration/terrain_scene_session.h'):
                with self.subTest(domain=domain, include=include), tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    target = root / "src/3d" / domain / "detail/test.h"
                    target.parent.mkdir(parents=True)
                    target.write_text(f'#include "{include}"\n', encoding="utf-8")
                    diagnostics = RuntimeDomainDependencies(root).source_diagnostics()
                    self.assertTrue(any("INTEGRATION-DEPENDENCY" in value for value in diagnostics))

    def test_lower_targets_cannot_link_or_compile_terrain_integration(self):
        for lower in RuntimeDomainDependencies.integration_dependencies:
            with self.subTest(target=lower):
                targets = {name: ("", "") for name in RuntimeDomainDependencies.runtime_targets}
                targets[lower] = ("", "bridge")
                targets["bridge"] = ("", "arp_terrain_scene_runtime")
                diagnostics = RuntimeDomainDependencies.target_diagnostics(targets)
                self.assertTrue(any(f"{lower} -> bridge -> arp_terrain_scene_runtime" in value
                                    and "INTEGRATION-TARGET" in value for value in diagnostics))
                targets["bridge"] = ("src/3d/terrain/integration/terrain_scene_session.cpp", "")
                self.assertTrue(any(f"{lower} -> bridge" in value and "INTEGRATION-TARGET" in value
                                    for value in RuntimeDomainDependencies.target_diagnostics(targets)))

    def test_session_implementation_has_only_the_terrain_integration_owner(self):
        self.assertFalse((ROOT / "src/engine/terrain_scene_session.h").exists())
        self.assertFalse((ROOT / "src/engine/terrain_scene_session.cpp").exists())
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        source = "src/3d/terrain/integration/terrain_scene_session.cpp"
        self.assertEqual(cmake.count(source), 1)
        self.assertIn(f"add_library(arp_terrain_scene_runtime {source})", cmake)
        self.assertTrue((ROOT / source).is_file())

    def test_generated_engine_assembly_keeps_terrain_integration(self):
        modules = {name: SimpleNamespace(name=name, type="library")
                   for name in ("arp_engine", "arp_terrain_scene_runtime")}
        writer = CmakeWriter(modules, "unused-build", "release", "win", "vulkan",
                             "unused-gcc", "unused-gxx", "unused-ninja", "", "")
        writer._emit_engine_assembly()
        generated = "\n".join(writer.lines)
        self.assertIn("set(ARP_ENGINE_MODULE_TARGETS\n    arp_terrain_scene_runtime\n", generated)
        self.assertNotIn("arp_engine_scene_runtime", generated)


if __name__ == "__main__":
    unittest.main()
