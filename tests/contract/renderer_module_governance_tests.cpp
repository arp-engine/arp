#include "core/cpu_allocator.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

std::filesystem::path projectRoot()
{
#ifdef ARP_SOURCE_DIR
    return std::filesystem::path(ARP_SOURCE_DIR);
#else
    auto current = std::filesystem::current_path();
    for (;;) {
        if (std::filesystem::exists(current / "CMakeLists.txt")
            && std::filesystem::exists(current / "src" / "renderer")) {
            return current;
        }
        if (!current.has_parent_path() || current == current.parent_path()) {
            break;
        }
        current = current.parent_path();
    }
    return std::filesystem::current_path();
#endif
}

std::pmr::string readText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream stream;
    stream << file.rdbuf();
    return arp::core::toPmrString(stream.str());
}

bool textContains(const std::pmr::string& text, const char* token)
{
    return text.find(token) != std::pmr::string::npos;
}

void assertRemoved(const std::filesystem::path& root, const char* relative)
{
    assert(!std::filesystem::exists(root / relative));
}

void assertExists(const std::filesystem::path& root, const char* relative)
{
    assert(std::filesystem::exists(root / relative));
}

} // namespace

int main()
{
    const auto root = projectRoot();

    const char* removedRendererAggregates[] = {
        "src/renderer/debug/debug_views.h",
        "src/renderer/materials/materials.h",
        "src/renderer/postprocess/postprocess.h",
        "src/renderer/world/world.h",
        "src/renderer/lighting/lighting.h",
    };
    for (const auto* relative : removedRendererAggregates) {
        assertRemoved(root, relative);
    }

    assertRemoved(root, "src/renderer/gui/gui_rhi_draw_commands.h");
    assertRemoved(root, "src/renderer/passes/scene_gui_composer.h");
    assertRemoved(root, "src/renderer/passes/scene_gui_composer.cpp");
    assertRemoved(root, "tests/scene_gui_composer_tests.cpp");
    assertRemoved(root, "src/renderer/passes/gi_compute_plan.h");
    assertRemoved(root, "tests/gi_compute_plan_tests.cpp");
    assertRemoved(root, "src/renderer/lighting_shadow.h");
    assertRemoved(root, "tests/lighting_shadow_contract_tests.cpp");

    const auto obsoleteDrawPlanTest = root / "tests" / "renderer_draw_plan_tests.cpp";
    if (std::filesystem::exists(obsoleteDrawPlanTest)) {
        const auto text = readText(obsoleteDrawPlanTest);
        assert(!textContains(text, "renderer/gui/gui_rhi_draw_commands.h"));
        assert(!textContains(text, "GuiRhiDrawDesc"));
        assert(!textContains(text, "recordGuiDrawPlan"));
    }

    const auto cmakeLists = readText(root / "CMakeLists.txt");
    assert(!textContains(cmakeLists, "arp_scene_gui_composer_tests"));
    assert(!textContains(cmakeLists, "tests/scene_gui_composer_tests.cpp"));
    assert(!textContains(cmakeLists, "arp_gi_compute_plan_tests"));
    assert(!textContains(cmakeLists, "tests/gi_compute_plan_tests.cpp"));
    assert(!textContains(cmakeLists, "arp_lighting_shadow_contract_tests"));
    assert(!textContains(cmakeLists, "tests/lighting_shadow_contract_tests.cpp"));

    assertExists(root, "src/renderer/ui/gui_renderer.h");
    assertExists(root, "src/renderer/particles/particle_renderer.h");
    assertExists(root, "src/renderer/postprocess/fxaa_renderer.h");
    assertExists(root, "src/renderer/postprocess/ssao_renderer.h");
    assertExists(root, "src/renderer/renderer_common.h");

    const auto renderersFacade = readText(root / "src" / "renderer" / "renderers" / "renderers.h");
    assert(!textContains(renderersFacade, "class GuiRenderer final"));
    assert(!textContains(renderersFacade, "class ParticleRenderer final"));
    assert(!textContains(renderersFacade, "class FxaaRenderer final"));
    assert(!textContains(renderersFacade, "class SsaoRenderer final"));
    assert(!textContains(renderersFacade, "recordGuiRhiDrawPlan"));
    assert(!textContains(renderersFacade, "recordParticleDrawPlan"));
    assert(!textContains(renderersFacade, "recordFxaaPass"));
    assert(!textContains(renderersFacade, "recordSsaoPass"));

    const auto rendererCommon =
        readText(root / "src" / "renderer" / "renderers" / "renderer_common.h");
    assert(!textContains(rendererCommon, "renderer/gui/"));
    assert(!textContains(rendererCommon, "renderer/particles/"));
    assert(!textContains(rendererCommon, "renderer/postprocess/"));
    assert(!textContains(rendererCommon, "GuiPushConstants"));
    assert(!textContains(rendererCommon, "ParticlePushConstants"));

    assertRemoved(root, "src/renderer/materials/material_table.h");

    const auto materialTextureResources =
        readText(root / "src" / "renderer" / "materials" / "material_texture_resources.h");
    assert(!textContains(materialTextureResources, "assets/image_decoder.h"));
    assert(!textContains(materialTextureResources, "renderer/materials/material_table.h"));
    assert(!textContains(materialTextureResources, "decodeImageToRgba8"));
    assert(!textContains(materialTextureResources, "ImportedMaterialTextureBindingTable"));
    assert(!textContains(materialTextureResources, "ImportedTextureAsset"));
    assert(!textContains(materialTextureResources, "ImportedImageAsset"));
    assert(!textContains(materialTextureResources, "MaterialTextureSourceBuildResult"));
    assert(!textContains(materialTextureResources, "appendDecodedMaterialTextureSource"));
    assert(!textContains(materialTextureResources, "buildMaterialTextureSources"));
    assert(!textContains(materialTextureResources, "buildBaseColorTextureSources"));
    assert(!textContains(materialTextureResources, "applyAlphaTransparencyRgbBleed"));

    return 0;
}
