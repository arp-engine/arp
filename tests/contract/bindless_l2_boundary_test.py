"""Bindless L2 responsibility boundaries; execution tests cover actual behavior."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]

class BindlessL2BoundaryTest(unittest.TestCase):
    def test_frontend_has_no_bindless_backend_matrices(self):
        for path in (ROOT / "src/rhi/frontend").glob("bindless*"):
            if path.suffix not in (".h", ".cpp"):
                continue
            source = path.read_text(encoding="utf-8-sig")
            self.assertNotRegex(source, r"case\s+(?:rhi::)?Backend::", str(path))

    def test_rhi_has_no_scene_record_or_numeric_transport_dependencies(self):
        forbidden = r"Bindless(?:Pbr|Terrain)|numericLanesPerPixel|meshColorVertex|skinDepthVertex|grassShadowVertex|terrainColorMain|selectEntries\("
        for path in (ROOT / "src/rhi").rglob("*"):
            if path.suffix not in (".h", ".cpp", ".mm", ".inl"):
                continue
            source = path.read_text(encoding="utf-8-sig")
            self.assertNotRegex(source, forbidden, str(path))
            self.assertNotIn('"renderer/bindless/', source, str(path))

    def test_selector_borrows_request(self):
        source = (ROOT / "src/rhi/semantics/bindless_lowering.cpp").read_text()
        selector = source[source.index("BindlessSelection selectBindlessVariant("):]
        self.assertNotRegex(selector, r"auto\s+\w+\s*=\s*request\s*;")
        self.assertIn("planBindlessResolved(caps, request,", selector)

    def test_recording_uses_prepared_pipeline_view(self):
        for file in ("vulkan/vulkan_graphics_pass.cpp", "webgpu/webgpu_backend_commands.cpp"):
            source = (ROOT / "src/rhi/backends" / file).read_text()
            self.assertIn("validateBindlessRenderPass(activePipelineReport_, passDesc_)", source)
            self.assertNotRegex(source, r"colorTargets\.reserve\(activePipelineReport_")

if __name__ == "__main__":
    unittest.main()
