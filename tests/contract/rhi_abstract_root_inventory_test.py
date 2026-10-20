"""Keep RHI abstraction entries separate from domain implementations."""
from pathlib import Path
import unittest
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tooling"))
from cpp_source_evidence import CppSource


class RhiAbstractRootInventoryTest(unittest.TestCase):
    ROOT = Path(__file__).resolve().parents[2]
    HEADERS = {
        "gpu_common.h", "gpu_resource_types.h", "gpu_resources.h",
        "gpu_descriptors.h", "gpu_pipeline.h", "gpu_commands.h",
        "gpu_device.h", "gpu_swapchain.h", "gpu_subpass.h", "gpu_bindless.h",
        "gpu_capabilities.h", "gpu_extensions.h", "gpu_execution_status.h", "gpu_texture_format.h",
        "gpu_frame_capture.h",
    }

    def test_root_contains_only_public_abstraction_headers(self):
        root = self.ROOT / "src/rhi"
        self.assertEqual({p.name for p in root.iterdir() if p.is_file() and p.name != ".clang-format"}, self.HEADERS)
        for name in self.HEADERS:
            self.assertNotRegex((root / name).read_text(encoding="utf-8-sig"), r'#include\s+"rhi/backends/')

    def test_domain_implementations_are_colocated_with_domain_headers(self):
        public={"bindless_authorization":"gpu_bindless.h",
                "pipeline_cache":"gpu_pipeline.h", "pipeline_package_reflection":"gpu_pipeline.h",
                "descriptor_cache":"gpu_descriptors.h", "frame_validation":"gpu_swapchain.h"}
        for domain, stems in {
            "bindless": ["snapshot", "shader_package", "indirect_packet", "authorization",
                         "binding_preparation", "compute_binding", "graphics_binding", "fault_domain"],
            "pipeline": ["cache", "single_entry_input", "graphics_input", "package_reflection"],
            "descriptor": ["cache"], "frame": ["validation", "resources"],
            "memory": ["lifetime_validation"],
            "semantics": ["fallback_plan", "webgpu_runtime_profile"],
        }.items():
            for stem in stems:
                prefix="" if domain=="semantics" else domain+"_"
                name=prefix+stem
                directory="semantics" if domain=="semantics" else "frontend"
                self.assertTrue((self.ROOT / "src/rhi" / directory / (name+".cpp")).is_file())
                header=public.get(name)
                self.assertTrue((self.ROOT / "src/rhi" / header if header else
                                 self.ROOT / "src/rhi" / directory / (name+".h")).is_file())
        cmake=(self.ROOT / "CMakeLists.txt").read_text(encoding="utf-8-sig")
        self.assertNotRegex(cmake, r'(?m)^\s+src/rhi/[^/\s]+\.cpp\s*$')

    def test_device_abstraction_excludes_factory_and_lifetime_algorithm_bodies(self):
        header = (self.ROOT / "src/rhi/gpu_device.h").read_text(encoding="utf-8-sig")
        implementation = CppSource((self.ROOT / "src/rhi/frontend/device_device.inl").read_text(encoding="utf-8-sig"))
        for token in ["core::makeShared", "CpuMemoryQuotaScope", "catch(const std::bad_alloc&)",
                      "factory(definition ? *definition : frozen)"]:
            self.assertNotIn(token, header)
            self.assertIn(token, implementation)
        self.assertIn('\n#include "rhi/frontend/device_device.inl"', header)

    def test_pipeline_abstraction_declares_validation_but_does_not_define_algorithms(self):
        header = (self.ROOT / "src/rhi/gpu_pipeline.h").read_text(encoding="utf-8-sig")
        implementation = (self.ROOT / "src/rhi/frontend/pipeline_interface_validation.inl").read_text(encoding="utf-8-sig")
        for name in ["stableShaderPackageHash", "validateShaderPackageInterface",
                     "stableRenderPipelineStateKey", "validateRenderPipelineState"]:
            self.assertRegex(header, r'inline\s+[^;{}]+\b' + name + r'\s*\([^;{}]*\)\s*;')
            self.assertRegex(implementation, r'inline\s+[^;{}]+\b' + name + r'\s*\([^;{}]*\)\s*\{')
        for directory in ["src/renderer", "src/platform", "src/shader", "games", "samples"]:
            for path in (self.ROOT / directory).rglob("*"):
                if path.suffix in {".h", ".cpp", ".mm", ".inl"}:
                    self.assertNotRegex(path.read_text(encoding="utf-8-sig"), r'#include\s+"rhi/frontend/(?:device_device|pipeline_interface_validation)\.inl"')


if __name__ == "__main__":
    unittest.main()
