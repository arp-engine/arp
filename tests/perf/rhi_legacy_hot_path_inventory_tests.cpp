#include "core/cpu_allocator.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::filesystem::path findProjectRoot()
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

bool contains(const std::pmr::string& text, const std::pmr::string& token)
{
    return text.find(token) != std::pmr::string::npos;
}

void requireToken(const std::pmr::string& text,
    const std::pmr::string& token,
    std::pmr::vector<std::pmr::string>& missing)
{
    if (!contains(text, token)) {
        missing.push_back(token);
    }
}

void forbidToken(const std::pmr::string& text,
    const std::pmr::string& token,
    std::pmr::vector<std::pmr::string>& unexpected)
{
    if (contains(text, token)) {
        unexpected.push_back(token);
    }
}

} // namespace

int main()
{
    const auto root = findProjectRoot();
    const auto inventoryPath = root / "docs" / "rhi-legacy-hot-path-inventory.md";
    assert(std::filesystem::exists(inventoryPath));
    const auto text = readText(inventoryPath);

    std::pmr::vector<std::pmr::string> missing;
    const char* requiredTokens[] = {
        "Status: Contract Complete",
        "Inventory date: 2026-05-18",
        "Removed module status",
        "`src/"
        "rhi` has been removed from the workspace",
        "RHI-only production surface",
        "Backend Validated locally",
        "Platform Validated is not claimed",
        "Production Ready is not claimed",
        "Backend-private implementation",
        "src/platform/render_system_vulkan.cpp",
        "src/platform/render_system_webgpu.cpp",
        "src/platform/render_system.h",
        "src/renderer/renderers.h",
        "src/renderer/ui/gui_renderer.h",
        "src/renderer/particles/particle_renderer.h",
        "src/renderer/postprocess/fxaa_renderer.h",
        "src/renderer/postprocess/ssao_renderer.h",
        "src/render_graph/render_graph.h",
        "samples/stylized_showcase/main.cpp",
        "samples/production_demo/main.cpp",
        "games/character_viewer/app/character_viewer_runtime.cpp",
    };

    for (const auto* token : requiredTokens) {
        requireToken(text, token, missing);
    }

    std::pmr::vector<std::pmr::string> unexpected;
    const char* forbiddenTokens[] = {
        "Explicit legacy RHI compatibility fallback",
        "LegacyRhiRenderSystemHotPath remains quarantined",
        "still uses `rhi"
        "::",
        "still owns v1",
        "retained as compatibility smoke",
        "Phase 12 retires",
    };
    for (const auto* token : forbiddenTokens) {
        forbidToken(text, token, unexpected);
    }

    assert(missing.empty());
    assert(unexpected.empty());
    return 0;
}
