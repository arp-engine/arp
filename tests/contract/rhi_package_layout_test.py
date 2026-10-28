"""RHI packages express architectural boundaries, not tiny domain folders."""
from pathlib import Path
import re
import unittest
from graphlib import TopologicalSorter


def has_files(directory):
    return any(path.is_file() for path in directory.rglob("*"))


COMPATIBILITY_HEADERS = {
    "fallback_plan.h": "rhi/frontend/fallback_planner.h",
    "push_constant_validation.h": "rhi/frontend/command_push_consumption_state.h",
    "webgl_execution_telemetry.h": "rhi/frontend/webgl_execution_telemetry.h",
    "webgpu_runtime_telemetry.h": "rhi/frontend/webgpu_runtime_telemetry.h",
}


def frontend_dispatch(source):
    return re.search(r"case\s+(?:rhi::)?Backend::|\bbackend(?:\(\))?\s*[!=]=\s*(?:rhi::)?Backend::", source)


class RhiPackageLayoutTest(unittest.TestCase):
    ROOT = Path(__file__).resolve().parents[2]

    def test_packages_are_flat_and_backend_roots_are_explicit(self):
        rhi = self.ROOT / "src/rhi"
        self.assertEqual({p.name for p in rhi.iterdir() if p.is_dir() and has_files(p)},
                         {"frontend", "semantics", "backends", "conformance"})
        self.assertEqual({p.name for p in (rhi / "frontend").iterdir() if p.is_dir() and has_files(p)},
                         {"detail", "visibility"})
        for name in ("semantics", "conformance"):
            self.assertFalse(any(p.is_dir() for p in (rhi / name).iterdir()), name)
        self.assertEqual({p.name for p in (rhi / "backends").iterdir() if p.is_dir()},
                         {"null", "vulkan", "metal", "webgpu", "webgl"})
        for backend in (rhi / "backends").iterdir():
            self.assertFalse(any(p.is_dir() for p in backend.iterdir()), backend.name)

    def test_semantics_does_not_own_runtime_objects_or_include_native_headers(self):
        for path in (self.ROOT / "src/rhi/semantics").glob("*"):
            if path.suffix not in (".h", ".cpp", ".inl"):
                continue
            source = path.read_text(encoding="utf-8-sig")
            source = re.sub(r"//[^\n]*|/\*.*?\*/", "", source, flags=re.S)
            self.assertIsNone(re.search(r"core::(?:makeUnique|makeShared)|std::shared_ptr|QuotaBoundSharedObjectIdentity", source), str(path))
            self.assertIsNone(re.search(r'#include\s+[<"](?:rhi/backends/|renderer/|platform/|vulkan/|Metal/|webgpu/)', source), str(path))
            self.assertIsNone(re.search(r'\b(?:class|struct)\s+(?:EngineFallbackPlanner|PushConstantConsumptionState|WebGlExecutionTelemetry|WebGpuRuntimeTelemetryCollector)\s*(?:final\s*)?\{', source), str(path))
            if path.name in COMPATIBILITY_HEADERS:
                self.assertEqual(set(re.findall(r'#include\s+"([^"]+)"', source)),
                                 {COMPATIBILITY_HEADERS[path.name]} if path.name != "push_constant_validation.h" else
                                 {COMPATIBILITY_HEADERS[path.name], "rhi/frontend/command_push_constant_binding.h"})
            else:
                for legacy in COMPATIBILITY_HEADERS:
                    self.assertNotIn('"rhi/semantics/' + legacy + '"', source, str(path))

    def test_internal_imports_resolve_and_rhi_headers_are_acyclic(self):
        graph={}
        for path in (self.ROOT / "src/rhi").rglob("*"):
            if path.suffix not in (".h", ".inl", ".cpp", ".mm"):
                continue
            source=path.read_text(encoding="utf-8-sig")
            dependencies=[]
            for name in re.findall(r'^#include\s+"([^"\n]+)"',source,re.M):
                if name == "bindless_zero_base_identity.h":
                    cmake=(self.ROOT / "CMakeLists.txt").read_text(encoding="utf-8-sig")
                    self.assertIn("generated/rhi/bindless_zero_base_identity.h",cmake)
                    continue
                if name == "metal_dispatch_probe_pin.h":
                    cmake = (self.ROOT / "cmake/ArpMetalDispatchProbe.cmake").read_text(encoding="utf-8-sig")
                    self.assertIn("generated/metal-dispatch/metal_dispatch_probe_pin.h", cmake)
                    self.assertIn("add_dependencies(arp_rhi_metal arp_compute_dispatch_probe_pin)", cmake)
                    continue
                if name.startswith("rhi/"):
                    dependency=self.ROOT / "src" / name
                elif (self.ROOT / "src" / name).is_file():
                    dependency=self.ROOT / "src" / name
                else:
                    dependency=path.parent / name
                if "rhi" not in dependency.parts:
                    continue
                self.assertTrue(dependency.is_file(), f"{path}: {name}")
                if dependency.suffix in (".h", ".inl"):
                    dependencies.append(dependency.resolve())
            if path.suffix in (".h", ".inl"):
                graph[path.resolve()]=dependencies
        # Core/Shader value headers do not import RHI; direct RHI cycles are forbidden.
        list(TopologicalSorter(graph).static_order())

    def test_frontend_does_not_define_backend_dispatch_tables(self):
        for path in (self.ROOT / "src/rhi/frontend").rglob("*"):
            if path.suffix not in (".h", ".cpp", ".inl"):
                continue
            source = path.read_text(encoding="utf-8-sig")
            self.assertIsNone(frontend_dispatch(source), str(path))
            self.assertIsNone(re.search(r'#include\s+"rhi/backends/', source), str(path))

    def test_boundary_guards_reject_counterexamples(self):
        self.assertIsNotNone(frontend_dispatch("switch (backend) { case Backend::Null: break; }"))
        self.assertIsNotNone(frontend_dispatch("if (backend() != Backend::WebGPU) return;"))
        self.assertIsNone(frontend_dispatch("return lowering(backend);"))

    def test_semantics_leaf_dependencies(self):
        semantics = self.ROOT / "src/rhi/semantics"
        graph = {}
        for path in (self.ROOT / "src/rhi").rglob("*.h"):
            source = path.read_text(encoding="utf-8-sig")
            graph[path.resolve()] = [(self.ROOT / "src" / name).resolve()
                                     for name in re.findall(r'#include\s+"(rhi/[^"]+)"', source)]
        for name, forbidden in {
            "webgl_pipeline_creation.h": {"command_draw_validation.h"},
            "frame_graph_retirement_budget.h": {"resource_cache_accounting.h"},
            "command_lowering.h": {"descriptor_buffer_write_validation.h", "descriptor_validation.h",
                                   "gpu_commands.h", "gpu_pipeline.h", "gpu_resources.h"},
            "push_constant_validation_values.h": {"gpu_descriptors.h", "gpu_pipeline.h", "gpu_resources.h",
                                                  "command_push_constant_binding.h"},
        }.items():
            pending = [semantics / name]
            seen = set()
            while pending:
                path = pending.pop().resolve()
                if path in seen:
                    continue
                seen.add(path)
                self.assertNotIn(path.name, forbidden, f"{name} reaches {path}")
                pending.extend(graph.get(path, []))


if __name__ == "__main__":
    unittest.main()
