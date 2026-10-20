#include "core/cpu_allocator.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

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

} // namespace

int main()
{
    const auto presets = readText("CMakePresets.json");
    assert(contains(presets, "android-arm64-vulkan"));
    assert(contains(presets, "macos-metal"));
    assert(contains(presets, "ios-metal"));
    assert(contains(presets, "linux-vulkan"));

    const auto matrixPath = std::filesystem::path("docs/platform-build-matrix.md");
    assert(std::filesystem::exists(matrixPath));
    const auto matrix = readText(matrixPath);
    assert(contains(matrix, "Android NDK"));
    assert(contains(matrix, "Xcode"));
    assert(contains(matrix, "Vulkan SDK"));
    assert(contains(matrix, "Slang"));

    const auto reportSourcePath = std::filesystem::path("tools/platform_capabilities/main.cpp");
    assert(std::filesystem::exists(reportSourcePath));
    const auto reportSource = readText(reportSourcePath);
    assert(contains(reportSource, "Platform Capability Report"));
    assert(contains(reportSource, "Android"));
    assert(contains(reportSource, "iOS"));
    assert(contains(reportSource, "Vulkan"));
    assert(contains(reportSource, "Metal"));
    const auto removedIncludeToken = std::pmr::string("#include \"") + "rhi/";
    const auto removedNamespaceToken = std::pmr::string("arp::")
        + "rhi"
          "::";
    assert(!contains(reportSource, removedIncludeToken));
    assert(!contains(reportSource, removedNamespaceToken));
    return 0;
}
