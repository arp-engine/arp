#include "core/memory/cpu_allocator.h"

#include <cassert>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>

namespace {

std::pmr::string readText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    assert(file.good());
    std::ostringstream stream;
    stream << file.rdbuf();
    return arp::core::toPmrString(stream.str());
}

bool contains(const std::pmr::string& text, const std::pmr::string& needle)
{
    return text.find(needle) != std::pmr::string::npos;
}

bool containsEngineSourceCompile(const std::pmr::string& text)
{
    return contains(text, "src/core/diagnostics/log.cpp")
        || contains(text, "src/platform/common/render_system.cpp")
        || contains(text, "src/rhi/frontend/descriptor_descriptors.cpp")
        || contains(text, "src/assets/glb_static_mesh_importer.cpp");
}

std::pmr::string targetLinkBlock(const std::pmr::string& text, const std::pmr::string& target)
{
    const auto marker = arp::core::toPmrString("target_link_libraries(" + std::string(target));
    auto start = text.find(marker);
    while (start != std::pmr::string::npos) {
        const auto next = start + marker.size();
        if (next < text.size() && std::isspace(static_cast<unsigned char>(text[next]))) {
            break;
        }
        start = text.find(marker, start + marker.size());
    }
    assert(start != std::pmr::string::npos);
    const auto end = text.find(")", start);
    assert(end != std::pmr::string::npos);
    return text.substr(start, end - start);
}

void assertExecutableLinksOnlyEngineTarget(
    const std::pmr::string& cmakeText, const std::pmr::string& target)
{
    const auto block = targetLinkBlock(cmakeText, target);
    assert(contains(block, "arp_engine"));
    assert(!contains(block, "arp_core"));
    assert(!contains(block, "arp_assets"));
    assert(!contains(block, "arp_animation"));
    assert(!contains(block, "arp_platform"));
    assert(!contains(block, "arp_gui"));
    assert(!contains(block, "arp_renderer"));
    assert(!contains(block, "arp_render_graph"));
    assert(!contains(block, "arp_rhi"));
}

void assertFileContains(const std::filesystem::path& root,
    const std::filesystem::path& relativePath,
    const std::initializer_list<const char*> needles)
{
    const auto text = readText(root / relativePath);
    for (const auto* needle : needles) {
        assert(contains(text, arp::core::toPmrString(needle)));
    }
}

} // namespace

int main()
{
    const auto root = std::filesystem::current_path();
    const auto cmakeLists = root / "CMakeLists.txt";
    const auto apiHeader = root / "src" / "core" / "api.h";
    const auto engineScript = root / "scripts" / "build_engine.ps1";
    const auto projectScript = root / "scripts" / "build_project_lane.ps1";

    assert(std::filesystem::exists(cmakeLists));
    assert(std::filesystem::exists(apiHeader));
    assert(std::filesystem::exists(engineScript));
    assert(std::filesystem::exists(projectScript));

    const auto cmakeText = readText(cmakeLists);
    const auto apiHeaderText = readText(apiHeader);
    const auto engineText = readText(engineScript);
    const auto projectText = readText(projectScript);

    assert(contains(cmakeText, "add_library(arp_engine SHARED"));
    assert(
        contains(cmakeText, "target_compile_definitions(arp_engine PRIVATE ARP_BUILDING_DLL=1)"));
    assert(contains(cmakeText, "target_compile_definitions(${moduleTarget}"));
    assert(contains(cmakeText, "PRIVATE ARP_BUILDING_DLL=1"));
    assert(contains(cmakeText, "INTERFACE ARP_STATIC_LINK=1"));
    assert(contains(cmakeText, "OUTPUT_NAME arp_engine"));
    assert(contains(cmakeText, "PREFIX \"\""));
    assert(contains(cmakeText, "DEBUG_POSTFIX \"\""));
    assert(contains(cmakeText, "target_link_libraries(arp_engine"));
    assert(contains(cmakeText, "arp_core"));
    assert(contains(cmakeText, "arp_assets"));
    assert(contains(cmakeText, "arp_renderer"));
    assert(contains(cmakeText, "arp_rhi"));

    assertExecutableLinksOnlyEngineTarget(cmakeText, "arp_clear_sample");
    assertExecutableLinksOnlyEngineTarget(cmakeText, "arp_splash_boot");
    assertExecutableLinksOnlyEngineTarget(cmakeText, "arp_stylized_showcase");
    assertExecutableLinksOnlyEngineTarget(cmakeText, "arp_production_demo");
    assertExecutableLinksOnlyEngineTarget(cmakeText, "arp_project_lane");
    assertExecutableLinksOnlyEngineTarget(cmakeText, "arp_material_editor");
    assertExecutableLinksOnlyEngineTarget(cmakeText, "arp_character_viewer");

    assert(contains(apiHeaderText, "#define ARP_API __declspec(dllexport)"));
    assert(contains(apiHeaderText, "ARP_STATIC_LINK"));
    assert(contains(apiHeaderText, "#define ARP_API __declspec(dllimport)"));
    assert(contains(apiHeaderText, "#define ARP_API __attribute__((visibility(\"default\")))"));
    assert(contains(apiHeaderText, "#define ARP_API"));
    assert(contains(apiHeaderText, "ARP_BUILDING_DLL"));

    assertFileContains(root,
        "src/core/memory/cpu_allocator.h",
        {
            "#include \"core/api.h\"",
            "struct ARP_API CpuMemoryStats",
            "class ARP_API CpuAllocator",
            "class ARP_API DefaultCpuAllocator",
            "class ARP_API CallbackCpuAllocator",
            "class ARP_API CpuMemoryResource",
            "ARP_API DefaultCpuAllocator& defaultCpuAllocator()",
        });
    assertFileContains(root,
        "src/animation/animation.h",
        {
            "#include \"core/api.h\"",
            "struct ARP_API Quat",
            "ARP_API bool sampleClip",
            "class ARP_API AnimationStateMachine",
        });
    assertFileContains(root,
        "src/animation/animator_controller.h",
        {
            "#include \"core/api.h\"",
            "class ARP_API AnimatorControllerRuntime",
        });
    assertFileContains(root,
        "src/animation/skinned_mesh_player.h",
        {
            "#include \"core/api.h\"",
            "class ARP_API SkinnedMeshPlayer",
        });
    assertFileContains(root,
        "src/assets/fbx_character_importer.h",
        {
            "#include \"core/api.h\"",
            "class ARP_API FbxCharacterImporter",
        });
    assertFileContains(root,
        "src/assets/glb_animated_mesh_importer.h",
        {
            "#include \"core/api.h\"",
            "ARP_API AnimatedMeshImportResult importAnimatedMeshFromGlbFile",
        });
    assertFileContains(root,
        "src/assets/glb_static_mesh_importer.h",
        {
            "#include \"core/api.h\"",
            "ARP_API StaticMeshImportResult importStaticMeshFromGlbFile",
        });
    assertFileContains(root,
        "src/assets/image_decoder.h",
        {
            "#include \"core/api.h\"",
            "ARP_API ImageDecodeResult decodeImageToRgba8",
        });
    assertFileContains(root,
        "tools/unity_import/unity_import.arp",
        {
            "class UnityCharacterPackageImporter",
            "class UnityParticlePackageImporter",
            "class UnityMetaImporter",
            "raise unity_import_common.DIAGNOSTIC_CHARACTER_PACKAGE_MODEL",
        });
    assert(!contains(cmakeText, "add_library(arp_unity_import"));
    assertFileContains(root,
        "src/assets/virtualized_geometry.h",
        {
            "#include \"core/api.h\"",
            "class ARP_API TriangleAdjacencyBuilder",
            "ARP_API VirtualizedGeometryBuildResult buildVirtualizedGeometry",
        });
    assertFileContains(root,
        "src/gui/gui.h",
        {
            "#include \"core/api.h\"",
            "class ARP_API Builder",
            "struct ARP_API DrawData",
            "class ARP_API GuiContext",
        });
    assertFileContains(root,
        "src/gui/imgui_extension.h",
        {
            "#include \"core/api.h\"",
            "class ARP_API ImGuiExtension",
        });
    assertFileContains(root,
        "src/platform/render_system.h",
        {
            "#include \"core/api.h\"",
            "class ARP_API RenderSystem",
        });
    assertFileContains(root,
        "src/platform/window.h",
        {
            "#include \"core/api.h\"",
            "class ARP_API PlatformWindow",
            "class ARP_API WindowSystem",
        });
    assertFileContains(root,
        "src/render_graph/render_graph.h",
        {
            "#include \"core/api.h\"",
            "class ARP_API FrameGraph",
            "class ARP_API PassBuilder",
        });
    assertFileContains(root,
        "src/renderer/rhi_draw_plan_bridge.h",
        {
            "#include \"core/api.h\"",
            "class ARP_API RendererRhiDrawPlanBridge",
            "class ARP_API RendererRhiFramePlanBuilder",
        });
    assertFileContains(root,
        "src/renderer/rhi_production_frame.h",
        {
            "#include \"core/api.h\"",
            "class ARP_API RendererRhiProductionFramePath",
        });
    assertFileContains(root,
        "src/renderer/pipeline/render_pipeline.h",
        {
            "#include \"core/api.h\"",
            "class ARP_API RenderPipeline",
        });
    assertFileContains(root,
        "src/rhi/gpu_device.h",
        {
            "#include \"core/api.h\"",
            "class ARP_API GpuQueue",
            "class ARP_API GpuDevice",
        });
    assertFileContains(root,
        "src/rhi/gpu_resources.h",
        {
            "#include \"core/api.h\"",
            "class ARP_API GpuBuffer",
            "class ARP_API GpuTexture",
        });
    assertFileContains(root,
        "src/rhi/gpu_pipeline.h",
        {
            "#include \"core/api.h\"",
            "class ARP_API PipelineLayout",
            "class ARP_API RenderPipeline",
        });
    assertFileContains(root,
        "src/rhi/null_backend.h",
        {
            "#include \"core/api.h\"",
            "ARP_API arp::core::UniquePtr<GpuDevice> createNullDevice",
        });
    assertFileContains(root,
        "src/shader/shader_compiler.h",
        {
            "#include \"core/api.h\"",
            "class ARP_API ShaderCompiler",
        });

    assert(contains(engineText, "arp_engine.dll"));
    assert(contains(engineText, "libarp_engine.dll.a"));
    assert(contains(engineText, "-shared"));
    assert(containsEngineSourceCompile(engineText));
    assert(!contains(engineText,
        "src/"
        "rhi/"));

    assert(contains(projectText, "Build-ProjectExe \"arp_project_lane\""));
    assert(contains(projectText, "games/project_lane/main.cpp"));
    assert(contains(projectText, "libarp_engine.dll.a"));
    assert(contains(projectText, "arp_engine.dll"));
    assert(!containsEngineSourceCompile(projectText));

    return 0;
}
