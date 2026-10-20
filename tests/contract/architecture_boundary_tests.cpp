#include "core/cpu_allocator.h"

#include <cassert>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Violation {
    std::filesystem::path file;
    std::pmr::string token;
};

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

bool isSourceFile(const std::filesystem::path& path)
{
    const auto ext = path.extension().string();
    return ext == ".h" || ext == ".hpp" || ext == ".cpp" || ext == ".mm";
}

bool hasToken(const std::pmr::string& text, const std::pmr::string& token)
{
    size_t pos = 0;
    while ((pos = text.find(token, pos)) != std::pmr::string::npos) {
        const bool leftOk = pos == 0
            || !(std::isalnum(static_cast<unsigned char>(text[pos - 1])) || text[pos - 1] == '_');
        const auto after = pos + token.size();
        const bool rightOk = after >= text.size()
            || !(std::isalnum(static_cast<unsigned char>(text[after])) || text[after] == '_');
        if (leftOk && rightOk) {
            return true;
        }
        pos = after;
    }
    return false;
}

bool hasBackendTypeToken(const std::pmr::string& text, const std::pmr::string& token)
{
    return hasToken(text, token);
}

bool isSceneRuntimeBackendSemanticReportFile(
    const std::filesystem::path& root, const std::filesystem::path& file)
{
    const auto relative = std::filesystem::relative(file, root).generic_string();
    return relative == "src/renderer/pipeline/scene_runtime_pipeline_rhi_parity.h"
        || relative == "src/renderer/pipeline/scene_runtime_pipeline_rhi_parity.cpp";
}

bool isBackendSemanticLabelToken(const std::pmr::string& token)
{
    return token == "Vulkan" || token == "Metal" || token == "WebGPU" || token == "rhi::Backend";
}

class LegacyRhiRemovalBoundaryScanner {
public:
    explicit LegacyRhiRemovalBoundaryScanner(std::filesystem::path root) : root_(std::move(root)) {}

    void collect(std::pmr::vector<Violation>& violations) const
    {
        collectModuleBoundary(violations);
        collectSourceReferenceBoundary(violations);
        collectCMakeTargetBoundary(violations);
    }

private:
    bool shouldSkipSourceFile(const std::filesystem::path& path) const
    {
        const auto relativePath = std::filesystem::relative(path, root_).generic_string();
        return relativePath == "tests/architecture_boundary_tests.cpp"
            || relativePath == "tests/rhi_no_legacy_source_contract_tests.cpp"
            || relativePath == "tests/render_system_custom_graph_frame_tests.cpp"
            || relativePath == "tests/sample_protected_boundary_tests.cpp";
    }

    void collectModuleBoundary(std::pmr::vector<Violation>& violations) const
    {
        const auto legacyRhiRoot = root_ / "src" / "rhi";
        if (std::filesystem::exists(legacyRhiRoot)) {
            violations.push_back({legacyRhiRoot, "legacy src/rhi module must be removed"});
        }
    }

    void collectSourceReferenceBoundary(std::pmr::vector<Violation>& violations) const
    {
        const std::filesystem::path sourceRoots[] = {
            root_ / "src",
            root_ / "samples",
            root_ / "games",
            root_ / "tests",
        };
        const char* forbiddenSubstrings[] = {
            "#include \"rhi/",
            "#include <"
            "rhi/",
            "arp::rhi"
            "::",
            " rhi"
            "::",
        };

        for (const auto& sourceRoot : sourceRoots) {
            if (!std::filesystem::exists(sourceRoot)) {
                continue;
            }
            for (const auto& entry : std::filesystem::recursive_directory_iterator(sourceRoot)) {
                if (!entry.is_regular_file() || !isSourceFile(entry.path())
                    || shouldSkipSourceFile(entry.path())) {
                    continue;
                }

                const auto text = readText(entry.path());
                for (const auto* token : forbiddenSubstrings) {
                    if (text.find(token) != std::pmr::string::npos) {
                        violations.push_back({entry.path(), token});
                    }
                }
            }
        }
    }

    void collectCMakeTargetBoundary(std::pmr::vector<Violation>& violations) const
    {
        const auto cmakePath = root_ / "CMakeLists.txt";
        const auto cmakeText = readText(cmakePath);
        const char* forbiddenExactTokens[] = {
            "arp_rhi",
        };
        const char* forbiddenSourceSubstrings[] = {
            "src/"
            "rhi/",
        };

        for (const auto* token : forbiddenExactTokens) {
            if (hasToken(cmakeText, token)) {
                violations.push_back({cmakePath, token});
            }
        }
        for (const auto* token : forbiddenSourceSubstrings) {
            if (cmakeText.find(token) != std::pmr::string::npos) {
                violations.push_back({cmakePath, token});
            }
        }
    }

    std::filesystem::path root_;
};

void collectRendererBoundaryViolations(
    const std::filesystem::path& root, std::pmr::vector<Violation>& violations)
{
    const auto rendererRoot = root / "src" / "renderer";
    for (const auto& entry : std::filesystem::recursive_directory_iterator(rendererRoot)) {
        if (!entry.is_regular_file() || !isSourceFile(entry.path())) {
            continue;
        }

        const auto text = readText(entry.path());
        const char* forbiddenIncludes[] = {
            "\"rhi/vulkan_",
            "\"rhi/metal_",
            "\"rhi/metal/",
            "<vulkan/",
            "<Metal/",
        };
        for (const auto* include : forbiddenIncludes) {
            if (text.find(include) != std::pmr::string::npos) {
                violations.push_back({entry.path(), include});
            }
        }

        const char* forbiddenTypes[] = {
            "Vulkan",
            "VkInstance",
            "VkDevice",
            "VkImage",
            "VkBuffer",
            "VkRenderPass",
            "VkFramebuffer",
            "VkCommandBuffer",
            "Metal",
            "MTLDevice",
            "MTLTexture",
            "MTLBuffer",
            "WebGPU",
            "Android",
            "iOS",
            "rhi"
            "::Backend",
            "rhi::Backend",
        };
        for (const auto* token : forbiddenTypes) {
            if (isSceneRuntimeBackendSemanticReportFile(root, entry.path())
                && isBackendSemanticLabelToken(token)) {
                continue;
            }
            if (hasBackendTypeToken(text, token)) {
                violations.push_back({entry.path(), token});
            }
        }
    }
}

void collectRenderGraphBoundaryViolations(
    const std::filesystem::path& root, std::pmr::vector<Violation>& violations)
{
    const auto renderGraphRoot = root / "src" / "render_graph";
    if (!std::filesystem::exists(renderGraphRoot)) {
        violations.push_back({renderGraphRoot, "missing render graph module"});
        return;
    }

    const char* forbiddenIncludes[] = {
        "\"rhi/vulkan/",
        "\"rhi/metal_",
        "\"rhi/metal/",
        "<vulkan/",
        "<Metal/",
        "<android/",
        "<UIKit/",
    };
    const char* forbiddenBackendTokens[] = {
        "rhi::Backend",
    };

    for (const auto& entry : std::filesystem::recursive_directory_iterator(renderGraphRoot)) {
        if (!entry.is_regular_file() || !isSourceFile(entry.path())) {
            continue;
        }

        const auto text = readText(entry.path());
        for (const auto* include : forbiddenIncludes) {
            if (text.find(include) != std::pmr::string::npos) {
                violations.push_back({entry.path(), include});
            }
        }
        for (const auto* token : forbiddenBackendTokens) {
            if (text.find(token) != std::pmr::string::npos) {
                violations.push_back({entry.path(), token});
            }
        }
    }
}

void collectRhiBoundaryViolations(
    const std::filesystem::path& root, std::pmr::vector<Violation>& violations)
{
    const auto rhiRoot = root / "src" / "rhi";
    if (!std::filesystem::exists(rhiRoot)) {
        violations.push_back({rhiRoot, "missing rhi module"});
        return;
    }

    const char* forbiddenIncludes[] = {
        "\"rhi/vulkan/",
        "\"rhi/metal_",
        "\"rhi/metal/",
        "<vulkan/",
        "<Metal/",
        "<webgpu/",
        "<android/",
        "<UIKit/",
    };
    const char* forbiddenTypes[] = {
        "VkInstance",
        "VkDevice",
        "VkImage",
        "VkBuffer",
        "VkCommandBuffer",
        "MTLDevice",
        "MTLTexture",
        "MTLBuffer",
        "WGPUDevice",
        "WGPUTexture",
        "WGPUBuffer",
        "BindGroup",
        "ANativeWindow",
        "UIView",
    };

    for (const auto& entry : std::filesystem::recursive_directory_iterator(rhiRoot)) {
        if (!entry.is_regular_file() || !isSourceFile(entry.path())) {
            continue;
        }
        const auto relativePath = std::filesystem::relative(entry.path(), rhiRoot).generic_string();
        if (relativePath.rfind("backends/", 0) == 0) {
            continue;
        }

        const auto text = readText(entry.path());
        for (const auto* include : forbiddenIncludes) {
            if (text.find(include) != std::pmr::string::npos) {
                violations.push_back({entry.path(), include});
            }
        }
        for (const auto* token : forbiddenTypes) {
            if (hasBackendTypeToken(text, token)) {
                violations.push_back({entry.path(), token});
            }
        }
    }
}

void collectAssetsBoundaryViolations(
    const std::filesystem::path& root, std::pmr::vector<Violation>& violations)
{
    const auto assetsRoot = root / "src" / "assets";
    if (!std::filesystem::exists(assetsRoot)) {
        violations.push_back({assetsRoot, "missing bottom-layer assets module"});
        return;
    }

    const char* forbiddenIncludes[] = {
        "\"renderer/",
        "\"rhi/",
        "\"platform/",
        "<vulkan/",
        "<Metal/",
        "<android/",
        "<UIKit/",
    };
    const char* forbiddenTypes[] = {
        "Vulkan",
        "VkInstance",
        "VkDevice",
        "VkImage",
        "VkBuffer",
        "Metal",
        "MTLDevice",
        "MTLTexture",
        "Android",
        "JNIEnv",
        "UIView",
    };

    for (const auto& entry : std::filesystem::recursive_directory_iterator(assetsRoot)) {
        if (!entry.is_regular_file() || !isSourceFile(entry.path())) {
            continue;
        }

        const auto text = readText(entry.path());
        for (const auto* include : forbiddenIncludes) {
            if (text.find(include) != std::pmr::string::npos) {
                violations.push_back({entry.path(), include});
            }
        }
        for (const auto* token : forbiddenTypes) {
            if (hasBackendTypeToken(text, token)) {
                violations.push_back({entry.path(), token});
            }
        }
    }
}

void collectResourceBoundaryViolations(
    const std::filesystem::path& root, std::pmr::vector<Violation>& violations)
{
    const auto resourceRoot = root / "src" / "resource";
    if (!std::filesystem::exists(resourceRoot)) {
        violations.push_back({resourceRoot, "missing runtime resource module"});
        return;
    }

    const char* forbiddenIncludes[] = {
        "\"renderer/",
        "\"rhi/",
        "\"rhi/",
        "\"platform/",
        "\"network/",
        "\"assets/",
        "\"tools/",
        "<vulkan/",
        "<Metal/",
        "<webgpu/",
        "<android/",
        "<UIKit/",
    };
    const char* forbiddenBackendTokens[] = {
        "Vulkan",
        "VkInstance",
        "VkDevice",
        "VkImage",
        "VkBuffer",
        "Metal",
        "MTLDevice",
        "MTLTexture",
        "WebGPU",
        "WGPUDevice",
        "WGPUTexture",
        "Android",
        "JNIEnv",
        "UIView",
        "Assimp",
        "aiScene",
    };
    const char* forbiddenDefaultAllocationTokens[] = {
        "#include <vector>",
        "#include <string>",
        "#include <unordered_map>",
        "#include <memory>",
        "std::vector<",
        "std::string",
        "std::unordered_map<",
        "std::unique_ptr<",
        "std::shared_ptr<",
        "std::make_unique",
        "std::make_shared",
        "new ",
        "delete ",
    };
    const char* forbiddenHotPathIoTokens[] = {
        "std::ifstream",
        "std::ofstream",
        "fopen",
        "std::filesystem::",
        "socket(",
        "WinHttp",
        "curl_",
    };

    for (const auto& entry : std::filesystem::recursive_directory_iterator(resourceRoot)) {
        if (!entry.is_regular_file() || !isSourceFile(entry.path())) {
            continue;
        }
        const auto text = readText(entry.path());
        for (const auto* include : forbiddenIncludes) {
            if (text.find(include) != std::pmr::string::npos) {
                violations.push_back({entry.path(), include});
            }
        }
        for (const auto* token : forbiddenBackendTokens) {
            if (hasBackendTypeToken(text, token)) {
                violations.push_back({entry.path(), token});
            }
        }
        for (const auto* token : forbiddenDefaultAllocationTokens) {
            if (text.find(token) != std::pmr::string::npos) {
                violations.push_back({entry.path(), token});
            }
        }
        for (const auto* token : forbiddenHotPathIoTokens) {
            if (text.find(token) != std::pmr::string::npos) {
                violations.push_back({entry.path(), token});
            }
        }
    }
}

void collectSampleBoundaryViolations(
    const std::filesystem::path& root, std::pmr::vector<Violation>& violations)
{
    const auto samplesRoot = root / "samples";
    const char* forbiddenTokens[] = {
        "rhi/vulkan_gui_renderer.h",
        "rhi/vulkan_stylized_scene_renderer.h",
        "rhi/vulkan_triangle_renderer.h",
        "VulkanGuiRenderer",
        "VulkanStylizedSceneRenderer",
        "VulkanTriangleRenderer",
    };

    for (const auto& entry : std::filesystem::recursive_directory_iterator(samplesRoot)) {
        if (!entry.is_regular_file() || !isSourceFile(entry.path())) {
            continue;
        }

        const auto text = readText(entry.path());
        for (const auto* token : forbiddenTokens) {
            if (text.find(token) != std::pmr::string::npos) {
                violations.push_back({entry.path(), token});
            }
        }
    }
}

void collectUploadStagingProtectedBoundaryViolations(
    const std::filesystem::path& root, std::pmr::vector<Violation>& violations)
{
    const std::filesystem::path protectedRoots[] = {
        root / "src" / "renderer",
        root / "src" / "gui",
        root / "src" / "render_graph",
        root / "samples",
        root / "games",
    };
    const char* forbiddenTokens[] = {
        "VulkanRhiStagingUploadPool",
        "MetalRhiStagingUploadPool",
        "WebGpuRhiStagingUploadPool",
        "VmaAllocation",
        "VkBuffer",
        "MTLBuffer",
        "WGPUBuffer",
    };

    for (const auto& protectedRoot : protectedRoots) {
        if (!std::filesystem::exists(protectedRoot)) {
            continue;
        }
        for (const auto& entry : std::filesystem::recursive_directory_iterator(protectedRoot)) {
            if (!entry.is_regular_file() || !isSourceFile(entry.path())) {
                continue;
            }
            const auto text = readText(entry.path());
            for (const auto* token : forbiddenTokens) {
                if (hasBackendTypeToken(text, token)) {
                    violations.push_back({entry.path(), token});
                }
            }
        }
    }
}

void collectGuiCoreBoundaryViolations(
    const std::filesystem::path& root, std::pmr::vector<Violation>& violations)
{
    const std::filesystem::path guiCoreFiles[] = {
        root / "src" / "gui" / "gui.h",
        root / "src" / "gui" / "gui.cpp",
    };
    const char* forbiddenTokens[] = {
        "imgui",
        "ImGui",
        "Dear",
    };

    for (const auto& file : guiCoreFiles) {
        const auto text = readText(file);
        for (const auto* token : forbiddenTokens) {
            if (text.find(token) != std::pmr::string::npos) {
                violations.push_back({file, token});
            }
        }
    }
}

void forbidText(const std::filesystem::path& file,
    const std::pmr::string& text,
    const std::pmr::string& token,
    std::pmr::vector<Violation>& violations)
{
    if (text.find(token) != std::pmr::string::npos) {
        violations.push_back({file, token});
    }
}

void requireText(const std::filesystem::path& file,
    const std::pmr::string& text,
    const std::pmr::string& token,
    std::pmr::vector<Violation>& violations)
{
    if (text.find(token) == std::pmr::string::npos) {
        violations.push_back({file, std::pmr::string("missing required token: ") + token});
    }
}

void collectLegacyRendererRemovalViolations(
    const std::filesystem::path& root, std::pmr::vector<Violation>& violations)
{
    const char* removedFiles[] = {
        "src/"
        "rhi/vulkan_triangle_renderer.h",
        "src/"
        "rhi/vulkan_triangle_renderer.cpp",
        "src/"
        "rhi/vulkan_gui_renderer.h",
        "src/"
        "rhi/vulkan_gui_renderer.cpp",
        "src/"
        "rhi/vulkan_stylized_scene_renderer.h",
        "src/"
        "rhi/vulkan_stylized_scene_renderer.cpp",
        "tests/vulkan_triangle_tests.cpp",
        "tests/vulkan_gui_overlay_tests.cpp",
        "tests/vulkan_stylized_scene_tests.cpp",
        "tests/vulkan_scene_gui_composition_tests.cpp",
    };
    for (const auto* file : removedFiles) {
        const auto path = root / file;
        if (std::filesystem::exists(path)) {
            violations.push_back({path, "legacy Vulkan renderer file should be removed"});
        }
    }

    const std::filesystem::path scannedFiles[] = {
        root / "CMakeLists.txt",
        root / "scripts" / "build.ps1",
        root / "README.md",
        root / "docs" / "roadmap.md",
    };
    const char* forbiddenTokens[] = {
        "vulkan_triangle_renderer",
        "VulkanTriangleRenderer",
        "vulkan_gui_renderer",
        "VulkanGuiRenderer",
        "vulkan_stylized_scene_renderer",
        "VulkanStylizedSceneRenderer",
        "vulkan_triangle_tests",
        "vulkan_gui_overlay_tests",
        "vulkan_stylized_scene_tests",
        "vulkan_scene_gui_composition_tests",
    };
    for (const auto& file : scannedFiles) {
        const auto text = readText(file);
        for (const auto* token : forbiddenTokens) {
            forbidText(file, text, token, violations);
        }
    }
}

void collectRuntimeBootstrapViolations(
    const std::filesystem::path& root, std::pmr::vector<Violation>& violations)
{
    const auto runtimeSource = root / "src" / "platform" / "render_system.cpp";
    const auto runtimeText = readText(runtimeSource);
    const auto vulkanRuntimeSource = root / "src" / "platform" / "render_system_vulkan.cpp";
    const auto vulkanRuntimeText = std::filesystem::exists(vulkanRuntimeSource)
        ? readText(vulkanRuntimeSource)
        : std::pmr::string{};

    if (!std::filesystem::exists(vulkanRuntimeSource)) {
        violations.push_back(
            {vulkanRuntimeSource, "missing Vulkan render system implementation unit"});
    }
    requireText(vulkanRuntimeSource, vulkanRuntimeText, "createRhiVulkanDevice", violations);
    requireText(
        vulkanRuntimeSource, vulkanRuntimeText, "rhi_vulkan::createVulkanRhiDevice", violations);
    forbidText(vulkanRuntimeSource,
        vulkanRuntimeText,
        "arp::samples::stylized::StylizedSceneRenderer",
        violations);
    forbidText(vulkanRuntimeSource, vulkanRuntimeText, "createStylizedSceneRenderer", violations);
    forbidText(vulkanRuntimeSource, vulkanRuntimeText, "drawScene", violations);
    forbidText(vulkanRuntimeSource,
        vulkanRuntimeText,
        "impl_->renderPipeline->render(context)",
        violations);
    forbidText(
        vulkanRuntimeSource, vulkanRuntimeText, "impl_->guiRenderer->drawFrame(", violations);
    forbidText(
        vulkanRuntimeSource, vulkanRuntimeText, "impl_->particleRenderer->drawFrame(", violations);

    const auto webGpuRuntimeSource = root / "src" / "platform" / "render_system_webgpu.cpp";
    const auto webGpuRuntimeText = std::filesystem::exists(webGpuRuntimeSource)
        ? readText(webGpuRuntimeSource)
        : std::pmr::string{};
    if (std::filesystem::exists(webGpuRuntimeSource)) {
        forbidText(webGpuRuntimeSource,
            webGpuRuntimeText,
            "impl_->renderPipeline->render(context)",
            violations);
        forbidText(
            webGpuRuntimeSource, webGpuRuntimeText, "impl_->guiRenderer->drawFrame(", violations);
        forbidText(webGpuRuntimeSource,
            webGpuRuntimeText,
            "impl_->particleRenderer->drawFrame(",
            violations);
    }

    const auto legacyHotPathSource =
        root / "src" / "platform" / "render_system_legacy_rhi_hot_path.h";
    if (std::filesystem::exists(legacyHotPathSource)) {
        violations.push_back(
            {legacyHotPathSource, "legacy RHI compatibility hot path must be removed"});
    }

    forbidText(runtimeSource, runtimeText, "\"rhi/vulkan_", violations);
    forbidText(runtimeSource,
        runtimeText,
        "rhi"
        "::Vulkan",
        violations);
    forbidText(runtimeSource, runtimeText, "\"rhi/vulkan_stylized_scene_renderer.h\"", violations);
    forbidText(runtimeSource, runtimeText, "\"rhi/vulkan_gui_renderer.h\"", violations);
    forbidText(runtimeSource, runtimeText, "\"rhi/vulkan_triangle_renderer.h\"", violations);
    forbidText(runtimeSource, runtimeText, "\"rhi/vulkan/mesh_buffer.h\"", violations);
    forbidText(runtimeSource, runtimeText, "VulkanStylizedSceneRenderer", violations);
    forbidText(runtimeSource, runtimeText, "VulkanGuiRenderer", violations);
    forbidText(runtimeSource, runtimeText, "VulkanTriangleRenderer", violations);
    forbidText(runtimeSource, runtimeText, "VulkanMeshBuffer", violations);
    forbidText(runtimeSource, runtimeText, "buildStylizedSceneDrawPlan", violations);
    forbidText(runtimeSource, runtimeText, "buildGuiDrawPlan", violations);
}

void collectCustomMainFrameApiViolations(
    const std::filesystem::path& root, std::pmr::vector<Violation>& violations)
{
    const auto renderSystemHeader = root / "src" / "platform" / "render_system.h";
    const auto renderSystemHeaderText = readText(renderSystemHeader);
    requireText(renderSystemHeader, renderSystemHeaderText, "renderGraphFrame", violations);
    requireText(renderSystemHeader,
        renderSystemHeaderText,
        "renderer::CustomRenderFrameWorkload",
        violations);
    forbidText(renderSystemHeader,
        renderSystemHeaderText,
        "renderer::RendererResult renderFrame(",
        violations);
    forbidText(renderSystemHeader,
        renderSystemHeaderText,
        "renderer::RendererResult drawGui(",
        violations);
    forbidText(renderSystemHeader,
        renderSystemHeaderText,
        "renderer::RendererResult drawParticles(",
        violations);

    const std::filesystem::path productionRuntimeUnits[] = {
        root / "src" / "platform" / "render_system_vulkan.cpp",
        root / "src" / "platform" / "render_system_webgpu.cpp",
    };
    for (const auto& file : productionRuntimeUnits) {
        if (!std::filesystem::exists(file)) {
            continue;
        }
        const auto text = readText(file);
        forbidText(file, text, "RenderSystem::renderFrame(", violations);
        forbidText(file, text, "RenderSystem::drawGui(", violations);
        forbidText(file, text, "RenderSystem::drawParticles(", violations);
        forbidText(file, text, "legacyRhiHotPath", violations);
        forbidText(file, text, "LegacyRhiRenderSystemHotPath", violations);
        requireText(file, text, "RenderSystem::renderGraphFrame(", violations);
    }

    const std::filesystem::path migratedGameRuntimeUnits[] = {
        root / "games" / "character_viewer" / "app" / "character_viewer_runtime.cpp",
        root / "games" / "project_lane" / "app" / "project_lane_runtime.cpp",
        root / "games" / "material_editor" / "app" / "material_editor_runtime.cpp",
    };
    for (const auto& file : migratedGameRuntimeUnits) {
        if (!std::filesystem::exists(file)) {
            continue;
        }
        const auto text = readText(file);
        requireText(file, text, "CustomRenderFrameWorkload", violations);
        requireText(file, text, ".renderGraphFrame(", violations);
        forbidText(file, text, ".renderFrame(", violations);
        forbidText(file, text, ".drawGui(", violations);
        forbidText(file, text, ".drawParticles(", violations);
        forbidText(file, text, "legacyRhiHotPath", violations);
    }

    const auto legacyHotPathSource =
        root / "src" / "platform" / "render_system_legacy_rhi_hot_path.h";
    if (std::filesystem::exists(legacyHotPathSource)) {
        violations.push_back(
            {legacyHotPathSource, "legacy RHI compatibility hot path must be removed"});
    }
}

void collectGenericRenderFrameBoundaryViolations(
    const std::filesystem::path& root, std::pmr::vector<Violation>& violations)
{
    const auto renderFrameSource = root / "src" / "renderer" / "render_frame.h";
    const auto renderFrameText = readText(renderFrameSource);
    forbidText(renderFrameSource, renderFrameText, "StylizedSceneRendererDesc", violations);
    forbidText(renderFrameSource, renderFrameText, "StylizedSceneRenderSettings", violations);
    forbidText(renderFrameSource, renderFrameText, "class SceneRenderer", violations);
    forbidText(renderFrameSource, renderFrameText, "class MeshGpuBuffer", violations);
    forbidText(renderFrameSource, renderFrameText, "uploadMeshToRhi", violations);
    forbidText(renderFrameSource, renderFrameText, "calculateSubmeshDrawBounds", violations);

    const auto renderersSource = root / "src" / "renderer" / "renderers" / "renderers.h";
    const auto renderersText = readText(renderersSource);
    forbidText(renderersSource, renderersText, "class StylizedSceneRenderer", violations);
    forbidText(renderersSource,
        renderersText,
        "samples/stylized/stylized_scene_renderer_contract.h",
        violations);
    forbidText(renderersSource,
        renderersText,
        "renderer/materials/material_texture_resources.h",
        violations);
    forbidText(renderersSource, renderersText, "renderer/passes/gi_compute_plan.h", violations);
    forbidText(renderersSource, renderersText, "stylizedSceneVertexLayout", violations);

    const std::filesystem::path removedPassAggregators[] = {
        root / "src" / "renderer" / "passes" / "render_draw_plan.h",
        root / "src" / "renderer" / "passes" / "rhi_draw_commands.h",
    };
    for (const auto& path : removedPassAggregators) {
        if (std::filesystem::exists(path)) {
            violations.push_back(
                {path, "renderer pass compatibility aggregator should be removed"});
        }
    }

    const auto materialTableSource = root / "src" / "renderer" / "materials" / "material_table.h";
    const auto materialTableText = readText(materialTableSource);
    forbidText(materialTableSource,
        materialTableText,
        "StylizedSceneMaterialDescriptorBinding",
        violations);
    forbidText(
        materialTableSource, materialTableText, "samples/stylized/stylized_scene.h", violations);
    forbidText(materialTableSource, materialTableText, "CharacterMaterialSet", violations);
    forbidText(materialTableSource, materialTableText, "CharacterMaterialSlot", violations);
    forbidText(materialTableSource, materialTableText, "CharacterNprMaterial", violations);
    forbidText(materialTableSource, materialTableText, "StylizedMaterialParams", violations);
    forbidText(materialTableSource, materialTableText, "SceneStylizedPbr", violations);

    const auto materialTextureResourcesSource =
        root / "src" / "renderer" / "materials" / "material_texture_resources.h";
    const auto materialTextureResourcesText = readText(materialTextureResourcesSource);
    forbidText(materialTextureResourcesSource,
        materialTextureResourcesText,
        "stylizedSceneMaterialDescriptorBindings",
        violations);
    forbidText(materialTextureResourcesSource,
        materialTextureResourcesText,
        "stylizedSceneIblDescriptorBindings",
        violations);
    forbidText(materialTextureResourcesSource,
        materialTextureResourcesText,
        "stylizedSceneShadowDescriptorBindings",
        violations);
    forbidText(materialTextureResourcesSource,
        materialTextureResourcesText,
        "stylizedSceneGiDescriptorBindings",
        violations);
    forbidText(materialTextureResourcesSource,
        materialTextureResourcesText,
        "class MaterialTextureResourceTable",
        violations);
    forbidText(materialTextureResourcesSource,
        materialTextureResourcesText,
        "StylizedSceneMaterialSet",
        violations);
    forbidText(materialTextureResourcesSource,
        materialTextureResourcesText,
        "MaterialTextureFallbackSet",
        violations);
    forbidText(materialTextureResourcesSource,
        materialTextureResourcesText,
        "materialTextureBindingIndex",
        violations);
    forbidText(materialTextureResourcesSource,
        materialTextureResourcesText,
        "fallbackTextureFor",
        violations);

    const auto particleSystemSource = root / "src" / "renderer" / "particles" / "particle_system.h";
    const auto particleSystemText = readText(particleSystemSource);
    forbidText(
        particleSystemSource, particleSystemText, "samples/stylized/stylized_scene.h", violations);
}

void collectRendererFrameDirectorGuardrailViolations(
    const std::filesystem::path& root, std::pmr::vector<Violation>& violations)
{
    const std::filesystem::path rendererFrameFiles[] = {
        root / "src" / "renderer" / "frame" / "render_frame_intent.h",
        root / "src" / "renderer" / "frame" / "render_frame_intent.cpp",
        root / "src" / "renderer" / "frame" / "render_frame_payload_adapter.h",
        root / "src" / "renderer" / "frame" / "render_frame_payload_adapter.cpp",
        root / "src" / "renderer" / "frame" / "scene_runtime_payload_adapter.h",
        root / "src" / "renderer" / "frame" / "scene_runtime_payload_adapter.cpp",
        root / "src" / "renderer" / "frame" / "render_pass_provider.h",
        root / "src" / "renderer" / "frame" / "render_pass_provider.cpp",
        root / "src" / "renderer" / "frame" / "scene_runtime_world_ui_pass_provider.h",
        root / "src" / "renderer" / "frame" / "scene_runtime_world_ui_pass_provider.cpp",
        root / "src" / "renderer" / "frame" / "render_frame_director.h",
        root / "src" / "renderer" / "frame" / "render_frame_director.cpp",
    };
    const char* forbiddenNativeTokens[] = {
        "<vulkan/",
        "<Metal/",
        "<webgpu",
        "webgpu.h",
        "<dawn/",
        "<android/",
        "<UIKit/",
        "VkDevice",
        "VkImage",
        "VkBuffer",
        "VkCommandBuffer",
        "MTLDevice",
        "MTLTexture",
        "MTLBuffer",
        "WGPUDevice",
        "WGPUTexture",
        "WGPUBuffer",
    };
    const char* forbiddenHotPathIoTokens[] = {
        "std::ifstream",
        "std::ofstream",
        "fopen",
        "std::filesystem::",
        "socket(",
        "WinHttp",
        "curl_",
    };
    const char* forbiddenPipelineHotPathTokens[] = {
        "createRenderPipeline",
        "createComputePipeline",
        "compileShader",
        "compile shader",
        "per-frame shader",
        "per-draw pipeline",
    };
    const char* forbiddenDefaultAllocationTokens[] = {
        "#include <vector>",
        "#include <string>",
        "#include <unordered_map>",
        "#include <memory>",
        "std::vector<",
        "std::string",
        "std::unordered_map<",
        "std::unique_ptr<",
        "std::shared_ptr<",
        "std::make_unique",
        "std::make_shared",
        "new ",
        "delete ",
    };

    for (const auto& file : rendererFrameFiles) {
        if (!std::filesystem::exists(file)) {
            violations.push_back({file, "missing renderer frame director file"});
            continue;
        }
        const auto text = readText(file);
        for (const auto* token : forbiddenNativeTokens) {
            if (hasBackendTypeToken(text, token)) {
                violations.push_back({file, token});
            }
        }
        for (const auto* token : forbiddenHotPathIoTokens) {
            forbidText(file, text, token, violations);
        }
        for (const auto* token : forbiddenPipelineHotPathTokens) {
            forbidText(file, text, token, violations);
        }
        for (const auto* token : forbiddenDefaultAllocationTokens) {
            forbidText(file, text, token, violations);
        }
    }
}

void collectStylizedImplementationOwnershipViolations(
    const std::filesystem::path& root, std::pmr::vector<Violation>& violations)
{
    const auto rendererStylizedRoot = root / "src" / "renderer" / "stylized";
    if (std::filesystem::exists(rendererStylizedRoot)) {
        violations.push_back(
            {rendererStylizedRoot, "stylized implementation should live outside engine renderer"});
    }

    const auto sampleStylizedRoot = root / "samples" / "stylized";
    if (!std::filesystem::exists(sampleStylizedRoot)) {
        violations.push_back(
            {sampleStylizedRoot, "missing sample-owned stylized implementation module"});
    }

    const char* removedRendererStylizedFiles[] = {
        "src/renderer/scene/stylized_scene.h",
        "src/renderer/materials/stylized_material_params.h",
        "src/renderer/character_npr.h",
    };
    for (const auto* file : removedRendererStylizedFiles) {
        const auto path = root / file;
        if (std::filesystem::exists(path)) {
            violations.push_back(
                {path, "stylized data/model code should live outside engine renderer"});
        }
    }

    const char* expectedSampleStylizedFiles[] = {
        "samples/stylized/stylized_environment.h",
        "samples/stylized/stylized_lighting.h",
        "samples/stylized/stylized_outline.h",
        "samples/stylized/stylized_postprocess.h",
        "samples/stylized/stylized_runtime_debug.h",
        "samples/stylized/stylized_shadows.h",
        "samples/stylized/stylized_world_slice.h",
        "samples/stylized/production_demo_world.h",
        "samples/stylized/stylized_material_params.h",
        "samples/stylized/character_npr.h",
    };
    for (const auto* file : expectedSampleStylizedFiles) {
        const auto path = root / file;
        if (!std::filesystem::exists(path)) {
            violations.push_back({path, "missing sample-owned stylized data/model code"});
        }
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(sampleStylizedRoot)) {
        if (!entry.is_regular_file() || !isSourceFile(entry.path())) {
            continue;
        }
        const auto text = readText(entry.path());
        requireText(entry.path(), text, "namespace arp::samples::stylized", violations);
        forbidText(entry.path(), text, "namespace arp::renderer", violations);
    }

    const auto retiredStylizedSceneSource = root / "samples" / "stylized" / "stylized_scene.h";
    if (std::filesystem::exists(retiredStylizedSceneSource)) {
        violations.push_back(
            {retiredStylizedSceneSource, "broad stylized_scene aggregate should be retired"});
    }

    const std::filesystem::path broadIncludeRoots[] = {
        root / "samples",
        root / "games",
        root / "tests",
    };
    const auto retiredInclude = std::pmr::string("\"samples/stylized/") + "stylized_scene.h\"";
    for (const auto& includeRoot : broadIncludeRoots) {
        if (!std::filesystem::exists(includeRoot)) {
            continue;
        }
        for (const auto& entry : std::filesystem::recursive_directory_iterator(includeRoot)) {
            if (!entry.is_regular_file() || !isSourceFile(entry.path())) {
                continue;
            }
            if (entry.path().filename() == "architecture_boundary_tests.cpp") {
                continue;
            }
            const auto text = readText(entry.path());
            if (text.find(retiredInclude) != std::pmr::string::npos) {
                violations.push_back(
                    {entry.path(), "source code includes retired broad stylized_scene aggregate"});
            }
        }
    }

    const auto characterNprSource = root / "samples" / "stylized" / "character_npr.h";
    const auto characterNprText = readText(characterNprSource);
    requireText(characterNprSource, characterNprText, "struct ToonRampTable", violations);
    requireText(characterNprSource, characterNprText, "struct CharacterMaterialSet", violations);

    const auto rendererRoot = root / "src" / "renderer";
    for (const auto& entry : std::filesystem::recursive_directory_iterator(rendererRoot)) {
        if (!entry.is_regular_file() || !isSourceFile(entry.path())) {
            continue;
        }

        const auto text = readText(entry.path());
        if (text.find("\"samples/stylized/") != std::pmr::string::npos) {
            violations.push_back(
                {entry.path(), "engine renderer includes stylized implementation"});
        }
        if (text.find("\"renderer/scene/stylized_scene.h\"") != std::pmr::string::npos
            || text.find("\"renderer/materials/stylized_material_params.h\"")
                != std::pmr::string::npos
            || text.find("\"renderer/character_npr.h\"") != std::pmr::string::npos) {
            violations.push_back(
                {entry.path(), "engine renderer includes stylized data/model code"});
        }
        const auto relativePath = std::filesystem::relative(entry.path(), root).generic_string();
        const bool l2StageVocabularyFile =
            relativePath == "src/renderer/frame/render_frame_intent.h"
            || relativePath == "src/renderer/frame/render_frame_intent.cpp";
        if (!l2StageVocabularyFile
            && (text.find("Stylized") != std::pmr::string::npos
                || text.find("CharacterNpr") != std::pmr::string::npos)) {
            violations.push_back(
                {entry.path(), "engine renderer contains stylized naming or pass policy"});
        }
    }
}

void collectCoreMathOwnershipViolations(
    const std::filesystem::path& root, std::pmr::vector<Violation>& violations)
{
    const auto coreMathSource = root / "src" / "core" / "math.h";
    if (!std::filesystem::exists(coreMathSource)) {
        violations.push_back({coreMathSource, "missing core math module"});
    }
    const auto coreMathText = readText(coreMathSource);
    requireText(coreMathSource, coreMathText, "namespace arp::core", violations);
    requireText(coreMathSource, coreMathText, "struct Vec3", violations);
    requireText(coreMathSource, coreMathText, "struct Mat4", violations);
    requireText(coreMathSource, coreMathText, "struct Camera", violations);

    const auto rendererMathRoot = root / "src" / "renderer" / "math";
    if (std::filesystem::exists(rendererMathRoot)) {
        violations.push_back(
            {rendererMathRoot, "renderer math compatibility module should be removed"});
    }

    const std::filesystem::path stylizedCoreMathUsers[] = {
        root / "samples" / "stylized" / "stylized_shadows.h",
        root / "samples" / "stylized" / "stylized_world_slice.h",
        root / "samples" / "stylized" / "production_demo_world.h",
    };
    for (const auto& file : stylizedCoreMathUsers) {
        const auto text = readText(file);
        requireText(file, text, "\"core/math.h\"", violations);
        forbidText(file, text, "\"renderer/math/math.h\"", violations);
    }

    const auto particleSystemSource = root / "src" / "renderer" / "particles" / "particle_system.h";
    const auto particleSystemText = readText(particleSystemSource);
    requireText(particleSystemSource, particleSystemText, "\"core/math.h\"", violations);
    forbidText(particleSystemSource, particleSystemText, "\"renderer/math/math.h\"", violations);

    const auto rendererRoot = root / "src" / "renderer";
    for (const auto& entry : std::filesystem::recursive_directory_iterator(rendererRoot)) {
        if (!entry.is_regular_file() || !isSourceFile(entry.path())) {
            continue;
        }
        const auto text = readText(entry.path());
        if (text.find("using ::arp::core::Vec3") != std::pmr::string::npos
            || text.find("using ::arp::core::Mat4") != std::pmr::string::npos
            || text.find("using ::arp::core::Camera") != std::pmr::string::npos
            || text.find("using ::arp::core::dot") != std::pmr::string::npos
            || text.find("using ::arp::core::normalize") != std::pmr::string::npos
            || text.find("using ::arp::core::transformPoint") != std::pmr::string::npos) {
            violations.push_back({entry.path(),
                "renderer should use core math directly instead of namespace aliases"});
        }
    }
}

void forbidTokenBetween(const std::filesystem::path& file,
    const std::pmr::string& text,
    const std::pmr::string& beginToken,
    const std::pmr::string& endToken,
    const std::pmr::string& forbiddenToken,
    std::pmr::vector<Violation>& violations)
{
    const auto begin = text.find(beginToken);
    if (begin == std::pmr::string::npos) {
        violations.push_back({file, std::pmr::string("missing required token: ") + beginToken});
        return;
    }
    const auto end = text.find(endToken, begin + beginToken.size());
    const auto block =
        end == std::pmr::string::npos ? text.substr(begin) : text.substr(begin, end - begin);
    if (block.find(forbiddenToken) != std::pmr::string::npos) {
        violations.push_back({file, forbiddenToken});
    }
}

void collectVulkanResourceLifetimeViolations(
    const std::filesystem::path& root, std::pmr::vector<Violation>& violations)
{
    const std::filesystem::path expectedVulkanBackendFiles[] = {
        root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_device.cpp",
        root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_command_objects.cpp",
        root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_swapchain.cpp",
        root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_allocator.cpp",
        root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_runtime.cpp",
    };
    for (const auto& file : expectedVulkanBackendFiles) {
        if (!std::filesystem::exists(file)) {
            violations.push_back({file, "missing RHI Vulkan backend unit"});
        }
    }

    const std::filesystem::path legacyVulkanBackendFiles[] = {
        root / "src" / "rhi" / "vulkan",
        root / "src" / "rhi" / "vulkan_device.cpp",
        root / "src" / "rhi" / "vulkan_device.h",
        root / "src" / "rhi" / "vulkan_rhi_device.cpp",
        root / "src" / "rhi" / "vulkan_rhi_device.h",
        root / "src" / "rhi" / "vulkan_swapchain.cpp",
        root / "src" / "rhi" / "vulkan_swapchain.h",
    };
    for (const auto& file : legacyVulkanBackendFiles) {
        if (std::filesystem::exists(file)) {
            violations.push_back({file, "legacy Vulkan backend unit must stay removed"});
        }
    }

    const auto vulkanDeviceSource =
        root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_device.cpp";
    const auto text = readText(vulkanDeviceSource);
    requireText(vulkanDeviceSource, text, "createVulkanRhiDevice", violations);
    forbidText(vulkanDeviceSource, text, "createNullDevice", violations);
}

void forbidTokensBetween(const std::filesystem::path& file,
    const std::pmr::string& text,
    const std::pmr::string& beginToken,
    const std::pmr::string& endToken,
    const char* const* forbiddenTokens,
    size_t forbiddenTokenCount,
    std::pmr::vector<Violation>& violations)
{
    for (size_t index = 0; index < forbiddenTokenCount; ++index) {
        forbidTokenBetween(file, text, beginToken, endToken, forbiddenTokens[index], violations);
    }
}

template <size_t N>
void forbidTokensBetween(const std::filesystem::path& file,
    const std::pmr::string& text,
    const std::pmr::string& beginToken,
    const std::pmr::string& endToken,
    const char* const (&forbiddenTokens)[N],
    std::pmr::vector<Violation>& violations)
{
    forbidTokensBetween(file, text, beginToken, endToken, forbiddenTokens, N, violations);
}

void collectProductionHotPathViolations(
    const std::filesystem::path& root, std::pmr::vector<Violation>& violations)
{
    const char* blockingNativeTokens[] = {
        "UINT64_MAX",
        "vkDeviceWaitIdle",
        "DeviceWaitIdle",
    };

    const std::filesystem::path rhiVulkanHotPathFiles[] = {
        root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_device.cpp",
        root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_command_objects.cpp",
        root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_swapchain.cpp",
    };
    for (const auto& file : rhiVulkanHotPathFiles) {
        const auto text = readText(file);
        for (const auto* token : blockingNativeTokens) {
            forbidText(file, text, token, violations);
        }
    }

    const char* shaderAndPipelineSetupTokens[] = {
        "std::ifstream",
        "readShaderBytes",
        "createShader(",
        "createGraphicsPipeline(",
        "createComputePipeline(",
    };

    const std::filesystem::path platformHotPathFiles[] = {
        root / "src" / "platform" / "render_system_vulkan.cpp",
        root / "src" / "platform" / "render_system_webgpu.cpp",
    };
    for (const auto& file : platformHotPathFiles) {
        const auto text = readText(file);
        forbidTokensBetween(file,
            text,
            "rhi::AcquiredFrame RenderSystem::acquireFrame()",
            "renderer::RendererResult RenderSystem::renderGraphFrame",
            shaderAndPipelineSetupTokens,
            violations);
        forbidTokensBetween(file,
            text,
            "renderer::RendererResult RenderSystem::renderGraphFrame",
            "rhi::PresentResult RenderSystem::presentFrame",
            shaderAndPipelineSetupTokens,
            violations);
        forbidTokensBetween(file,
            text,
            "rhi::PresentResult RenderSystem::presentFrame",
            "RenderSystemResult RenderSystem::configureRhiFrameLoopDryRun",
            shaderAndPipelineSetupTokens,
            violations);
    }

    const auto guiRendererSource = root / "src" / "renderer" / "renderers" / "gui_renderer.h";
    const auto guiRendererText = readText(guiRendererSource);
    forbidTokensBetween(guiRendererSource,
        guiRendererText,
        "const gui::DrawData& DrawData) override",
        "const rhi::RenderPipeline* graphicsPipeline() const override",
        shaderAndPipelineSetupTokens,
        violations);
    const auto fxaaRendererSource = root / "src" / "renderer" / "renderers" / "fxaa_renderer.h";
    const auto fxaaRendererText = readText(fxaaRendererSource);
    forbidTokensBetween(fxaaRendererSource,
        fxaaRendererText,
        "const FxaaSettings& settings = {})",
        "const rhi::RenderPipeline* graphicsPipeline() const",
        shaderAndPipelineSetupTokens,
        violations);
    const auto taaRendererSource = root / "src" / "renderer" / "renderers" / "taa_renderer.h";
    const auto taaRendererText = readText(taaRendererSource);
    forbidTokensBetween(taaRendererSource,
        taaRendererText,
        "const TaaSettings& settings = {})",
        "const rhi::RenderPipeline* graphicsPipeline() const",
        shaderAndPipelineSetupTokens,
        violations);
    const auto particleRendererSource =
        root / "src" / "renderer" / "renderers" / "particle_renderer.h";
    const auto particleRendererText = readText(particleRendererSource);
    forbidTokensBetween(particleRendererSource,
        particleRendererText,
        "bool sortBackToFront = true)",
        "const rhi::RenderPipeline* graphicsPipeline() const",
        shaderAndPipelineSetupTokens,
        violations);

    const auto stylizedRendererSource = root / "samples" / "stylized" / "stylized_scene_renderer.h";
    const auto stylizedRendererText = readText(stylizedRendererSource);
    forbidTokensBetween(stylizedRendererSource,
        stylizedRendererText,
        "const StylizedSceneRenderSettings& settings = {}) override",
        "const rhi::RenderPipeline* graphicsPipeline() const override",
        shaderAndPipelineSetupTokens,
        violations);

    const auto stylizedPipelineSource =
        root / "samples" / "stylized_showcase" / "stylized_render_pipeline.cpp";
    const auto stylizedPipelineText = readText(stylizedPipelineSource);
    forbidTokensBetween(stylizedPipelineSource,
        stylizedPipelineText,
        "renderer::RendererResult StylizedRenderPipeline::render",
        "} // namespace arp::samples::stylized_showcase",
        shaderAndPipelineSetupTokens,
        violations);

    const auto characterPipelineSource =
        root / "games" / "character_viewer" / "app" / "basic_character_preview_pipeline.cpp";
    const auto characterPipelineText = readText(characterPipelineSource);
    forbidTokensBetween(characterPipelineSource,
        characterPipelineText,
        "arp::renderer::RendererResult BasicCharacterPreviewPipeline::render",
        "} // namespace character_viewer::app",
        shaderAndPipelineSetupTokens,
        violations);
}

void collectMetalBackendSplitViolations(
    const std::filesystem::path& root, std::pmr::vector<Violation>& violations)
{
    const std::filesystem::path expectedMetalBackendFiles[] = {
        root / "src" / "rhi" / "backends" / "metal" / "metal_device.cpp",
        root / "src" / "rhi" / "backends" / "metal" / "metal_backend_commands.cpp",
        root / "src" / "rhi" / "backends" / "metal" / "metal_backend_descriptors.cpp",
        root / "src" / "rhi" / "backends" / "metal" / "metal_runtime_smoke.cpp",
    };
    for (const auto& file : expectedMetalBackendFiles) {
        if (!std::filesystem::exists(file)) {
            violations.push_back({file, "missing RHI Metal backend unit"});
        }
    }

    const std::filesystem::path legacyMetalBackendFiles[] = {
        root / "src" / "rhi" / "metal",
        root / "src" / "rhi" / "metal_device.cpp",
        root / "src" / "rhi" / "metal_device.h",
        root / "src" / "rhi" / "metal_device_mac.mm",
        root / "src" / "rhi" / "metal_resources_mac.mm",
        root / "src" / "rhi" / "metal_pipeline_mac.mm",
        root / "src" / "rhi" / "metal_command_list_mac.mm",
        root / "src" / "rhi" / "metal_swapchain_mac.mm",
    };
    for (const auto& file : legacyMetalBackendFiles) {
        if (std::filesystem::exists(file)) {
            violations.push_back({file, "legacy Metal backend unit must stay removed"});
        }
    }
}

void collectWebGpuWebBuildViolations(
    const std::filesystem::path& root, std::pmr::vector<Violation>& violations)
{
    const auto cmakePath = root / "CMakeLists.txt";
    const auto cmakeText = readText(cmakePath);
    requireText(cmakePath,
        cmakeText,
        "add_dependencies(arp_character_viewer_web arp_compile_shaders)",
        violations);
    requireText(
        cmakePath, cmakeText, "--preload-file=${CMAKE_BINARY_DIR}/shaders@/shaders", violations);
    requireText(cmakePath, cmakeText, "LINK_DEPENDS", violations);

    const auto webShellPath = root / "samples" / "character_viewer_web" / "index.html";
    const auto webShellText = readText(webShellPath);
    requireText(webShellPath, webShellText, "preinitializedWebGPUDevice", violations);
    requireText(webShellPath, webShellText, "requestDevice", violations);

    const auto thirdPartyPath = root / "cmake" / "ThirdParty.cmake";
    const auto thirdPartyText = readText(thirdPartyPath);
    forbidText(thirdPartyPath,
        thirdPartyText,
        "if(EMSCRIPTEN)\n    set(ARP_ASSIMP_SOURCE_DIR \"\"",
        violations);
    requireText(thirdPartyPath, thirdPartyText, "ARP_ENABLE_VENDORED_ASSIMP", violations);
    requireText(thirdPartyPath,
        thirdPartyText,
        "if(ARP_ENABLE_VENDORED_ASSIMP AND NOT ARP_ASSIMP_SOURCE_DIR)",
        violations);
    requireText(thirdPartyPath,
        thirdPartyText,
        "if(ARP_ENABLE_VENDORED_ASSIMP AND ARP_ASSIMP_SOURCE_DIR",
        violations);
}

void collectSceneRuntimeBoundaryViolations(
    const std::filesystem::path& root, std::pmr::vector<Violation>& violations)
{
    const auto sceneRuntimeDocPath = root / "docs" / "engine-scene-runtime-v1.md";
    const auto sceneRuntimeDocText = std::filesystem::exists(sceneRuntimeDocPath)
        ? readText(sceneRuntimeDocPath)
        : std::pmr::string{};
    if (!std::filesystem::exists(sceneRuntimeDocPath)) {
        violations.push_back({sceneRuntimeDocPath, "missing scene runtime V1 contract document"});
    }

    const auto agentsPath = root / "AGENTS.md";
    const auto agentsText =
        std::filesystem::exists(agentsPath) ? readText(agentsPath) : std::pmr::string{};
    if (!std::filesystem::exists(agentsPath)) {
        violations.push_back({agentsPath, "missing AI development constraints document"});
    }

    const char* requiredAgentTokens[] = {
        "core/cpu_allocator",
        "core::pmr",
        "core::makeUnique",
        "core::makeShared",
    };
    for (const auto* token : requiredAgentTokens) {
        requireText(agentsPath, agentsText, token, violations);
    }

    const char* requiredDocTokens[] = {
        "Contract Complete",
        "Prototype Complete",
        "input-control",
        "UI is Prototype Complete",
        "Platform Validated and Production Ready are not claimed",
        "Scene inherits the GameObject contract",
        "deterministic parent-child hierarchy",
        "local/world-global transforms",
        "ordinary GameObject camera with shared CameraComponent",
        "CameraResolver",
        "ResolvedCamera",
        "default mixed 2D/3D one orthographic camera",
        "perspective observing 2D requires explicit 2D projection semantics",
        "input flows from raw events to InputActionState",
        "character controllers consume action intent only",
        "camera follow/input runs in late update",
        "UI input routing consumes before gameplay",
        "stable root traversal snapshot",
        "RenderQueue",
        "RenderQueue",
        "no raw static vertex data",
        "separate logical 2D/3D/UI queues",
        "one physical world render pass",
        "Screen-space UI lives in `src/ui`",
        "World-space UI uses explicit anchor bridge plus `ResolvedCamera`",
        "rather than backend-native draw state",
        "TileMap and Terrain are components",
        "behavior inferred from components",
        "src/scene",
        "src/input",
        "src/ui",
        "src/2d",
        "src/3d",
        "runtime dynamic containers use core/cpu_allocator",
        "core::pmr",
    };
    for (const auto* token : requiredDocTokens) {
        requireText(sceneRuntimeDocPath, sceneRuntimeDocText, token, violations);
    }

    const std::filesystem::path sceneRuntimeRoots[] = {
        root / "src" / "scene",
        root / "src" / "input",
        root / "src" / "ui",
        root / "src" / "2d",
        root / "src" / "3d",
    };
    const char* forbiddenIncludes[] = {
        "renderer/frame/render_frame_director.h",
        "renderer/frame/render_frame_payload_adapter.h",
        "renderer/frame/render_pass_provider.h",
        "renderer/frame/scene_runtime_payload_adapter.h",
        "renderer/frame/scene_runtime_world_ui_pass_provider.h",
        "\"renderer/pipeline/",
        "scene_runtime_pipeline_payload.h",
        "<vulkan/",
        "<Metal/",
        "<webgpu",
        "webgpu.h",
        "<dawn/",
        "<android/",
        "<UIKit/",
    };
    const char* forbiddenBackendTokens[] = {
        "VkDevice",
        "VkImage",
        "VkBuffer",
        "VkCommandBuffer",
        "VkRenderPass",
        "VkFramebuffer",
        "MTLDevice",
        "MTLTexture",
        "MTLBuffer",
        "WGPUDevice",
        "WGPUTexture",
        "WGPUBuffer",
        "rhi::Backend",
        "rhi"
        "::",
        "native handle",
    };
    const char* forbiddenHotPathIoTokens[] = {
        "std::ifstream",
        "std::ofstream",
        "fopen",
        "std::filesystem::copy",
        "socket(",
        "WinHttp",
        "curl_",
    };
    const char* forbiddenRawStaticVertexTokens[] = {
        "raw static vertex",
        "static vertex data",
        "vertexData",
        "MeshVertex",
        "std::vector<MeshVertex>",
    };
    const char* forbiddenSceneModeTokens[] = {
        "enum class SceneMode",
        "enum SceneMode",
        "struct SceneMode",
        "class SceneMode",
        "SceneDimension",
        "sceneMode_",
        "sceneMode =",
        "dimensionFlag",
    };
    const char* forbiddenSpecialCameraTokens[] = {
        "class CameraObject",
        "struct CameraObject",
    };
    const char* forbiddenScreenSpaceUiShortcutTokens[] = {
        "class Canvas : public arp::scene::GameObject",
        "class Widget : public arp::scene::GameObject",
        "struct Canvas : public arp::scene::GameObject",
        "struct Widget : public arp::scene::GameObject",
        "screen-space GameObject",
        "GameObject-only UI",
        "CameraComponent required for screen-space UI",
    };
    const char* forbiddenRawPlatformInputTokens[] = {
        "HWND",
        "WPARAM",
        "LPARAM",
        "SDL_",
        "GLFWwindow",
        "NSEvent",
        "UIEvent",
        "AInputEvent",
        "android_app",
        "WindowHandle",
        "DeviceHandle",
        "nativeKeycode",
        "rawKeycode",
        "platformNativeEvent",
    };
    const char* forbiddenDefaultAllocationTokens[] = {
        "#include <vector>",
        "#include <string>",
        "#include <unordered_map>",
        "#include <memory>",
        "std::vector<",
        "std::string",
        "std::unordered_map<",
        "std::unique_ptr<",
        "std::shared_ptr<",
        "std::make_unique",
        "std::make_shared",
        "new ",
        "delete ",
    };

    for (const auto& sourceRoot : sceneRuntimeRoots) {
        if (!std::filesystem::exists(sourceRoot)) {
            violations.push_back({sourceRoot, "missing scene runtime source root"});
            continue;
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(sourceRoot)) {
            if (!entry.is_regular_file() || !isSourceFile(entry.path())) {
                continue;
            }

            const auto text = readText(entry.path());
            for (const auto* token : forbiddenIncludes) {
                forbidText(entry.path(), text, token, violations);
            }
            for (const auto* token : forbiddenBackendTokens) {
                if (hasBackendTypeToken(text, token)) {
                    violations.push_back({entry.path(), token});
                }
            }
            for (const auto* token : forbiddenHotPathIoTokens) {
                forbidText(entry.path(), text, token, violations);
            }
            for (const auto* token : forbiddenRawStaticVertexTokens) {
                forbidText(entry.path(), text, token, violations);
            }
            for (const auto* token : forbiddenSceneModeTokens) {
                forbidText(entry.path(), text, token, violations);
            }
            for (const auto* token : forbiddenSpecialCameraTokens) {
                forbidText(entry.path(), text, token, violations);
            }
            for (const auto* token : forbiddenScreenSpaceUiShortcutTokens) {
                forbidText(entry.path(), text, token, violations);
            }
            for (const auto* token : forbiddenRawPlatformInputTokens) {
                forbidText(entry.path(), text, token, violations);
            }
            for (const auto* token : forbiddenDefaultAllocationTokens) {
                forbidText(entry.path(), text, token, violations);
            }
        }
    }

    const auto scene3dRoot = root / "src" / "3d";
    if (std::filesystem::exists(scene3dRoot)) {
        const char* forbidden3dCameraOwnershipTokens[] = {
            "class CameraComponent",
            "struct CameraComponentDesc",
        };
        for (const auto& entry : std::filesystem::recursive_directory_iterator(scene3dRoot)) {
            if (!entry.is_regular_file() || !isSourceFile(entry.path())) {
                continue;
            }
            const auto text = readText(entry.path());
            for (const auto* token : forbidden3dCameraOwnershipTokens) {
                forbidText(entry.path(), text, token, violations);
            }
        }
    }
}

void collectSceneRuntimeRendererIntegrationGuardrailViolations(
    const std::filesystem::path& root, std::pmr::vector<Violation>& violations)
{
    const auto sceneRuntimeDocPath = root / "docs" / "engine-scene-runtime-v1.md";
    const auto sceneRuntimeDocText = std::filesystem::exists(sceneRuntimeDocPath)
        ? readText(sceneRuntimeDocPath)
        : std::pmr::string{};
    const char* requiredRendererStatusTokens[] = {
        "## Renderer Integration Status",
        "Scene-runtime renderer frame: Contract Complete",
        "Scene-runtime RHI lowering: Contract Complete",
        "Scene-runtime custom pipeline payload: Contract Complete",
        "RenderFrameDirector frame orchestration: Prototype Complete",
        "Scene-runtime pass provider: Prototype Complete",
        "Generic prepared renderer-frame submit: Contract Complete",
        "Scene-runtime custom RenderGraph adapter: Prototype Complete",
        "Scene-runtime custom pipeline/RHI parity: Contract Complete",
        "Scene-runtime demo director path: Backend Validated",
        "Null scene-runtime submit is Prototype Complete only after a fast hidden-frame smoke",
        "Vulkan hidden-window direct scene-runtime submit: Backend Validated",
        "Vulkan hidden-window custom RenderGraph scene-runtime submit: Backend Validated",
        "Vulkan hidden-window submit is Backend Validated only after surface-backed Vulkan hidden-window smoke",
        "Metal and WebGPU are not promoted by Vulkan evidence",
        "Backend Validated is not Platform Validated",
        "Platform Validated and Production Ready are not claimed",
    };
    for (const auto* token : requiredRendererStatusTokens) {
        requireText(sceneRuntimeDocPath, sceneRuntimeDocText, token, violations);
    }

    const std::filesystem::path rendererBridgeFiles[] = {
        root / "src" / "renderer" / "scene_runtime_frame.h",
        root / "src" / "renderer" / "scene_runtime_frame.cpp",
        root / "src" / "renderer" / "scene_runtime_frame_rhi_planner.h",
        root / "src" / "renderer" / "scene_runtime_frame_rhi_planner.cpp",
        root / "src" / "renderer" / "pipeline" / "scene_runtime_pipeline_payload.h",
        root / "src" / "renderer" / "pipeline" / "scene_runtime_pipeline_payload.cpp",
        root / "src" / "renderer" / "pipeline" / "scene_runtime_render_pipeline.h",
        root / "src" / "renderer" / "pipeline" / "scene_runtime_render_pipeline.cpp",
        root / "src" / "renderer" / "pipeline" / "scene_runtime_pipeline_rhi_parity.h",
        root / "src" / "renderer" / "pipeline" / "scene_runtime_pipeline_rhi_parity.cpp",
    };
    const char* forbiddenIncludes[] = {
        "<vulkan/",
        "<Metal/",
        "<webgpu",
        "webgpu.h",
        "<dawn/",
        "<android/",
        "<UIKit/",
    };
    const char* forbiddenBackendTokens[] = {
        "VkInstance",
        "VkDevice",
        "VkImage",
        "VkBuffer",
        "VkCommandBuffer",
        "VkRenderPass",
        "VkFramebuffer",
        "MTLDevice",
        "MTLTexture",
        "MTLBuffer",
        "WGPUDevice",
        "WGPUTexture",
        "WGPUBuffer",
        "rhi_vulkan",
        "rhi_metal",
        "rhi_webgpu",
        "rhi::Backend",
    };
    const char* forbiddenHotPathIoTokens[] = {
        "std::ifstream",
        "std::ofstream",
        "fopen",
        "std::filesystem::",
        "socket(",
        "WinHttp",
        "curl_",
    };
    const char* forbiddenPipelineHotPathTokens[] = {
        "createRenderPipeline",
        "createComputePipeline",
        "compileShader",
        "compile shader",
        "per-frame shader",
        "per-draw pipeline",
    };
    const char* forbiddenDefaultAllocationTokens[] = {
        "#include <vector>",
        "#include <string>",
        "#include <unordered_map>",
        "#include <memory>",
        "std::vector<",
        "std::string",
        "std::unordered_map<",
        "std::unique_ptr<",
        "std::shared_ptr<",
        "std::make_unique",
        "std::make_shared",
        "new ",
        "delete ",
    };

    for (const auto& file : rendererBridgeFiles) {
        if (!std::filesystem::exists(file)) {
            violations.push_back({file, "missing scene-runtime renderer bridge file"});
            continue;
        }
        const auto text = readText(file);
        for (const auto* token : forbiddenIncludes) {
            forbidText(file, text, token, violations);
        }
        for (const auto* token : forbiddenBackendTokens) {
            if (isSceneRuntimeBackendSemanticReportFile(root, file)
                && isBackendSemanticLabelToken(token)) {
                continue;
            }
            if (hasBackendTypeToken(text, token)) {
                violations.push_back({file, token});
            }
        }
        for (const auto* token : forbiddenHotPathIoTokens) {
            forbidText(file, text, token, violations);
        }
        for (const auto* token : forbiddenPipelineHotPathTokens) {
            forbidText(file, text, token, violations);
        }
        for (const auto* token : forbiddenDefaultAllocationTokens) {
            forbidText(file, text, token, violations);
        }
    }
}

void collectDocumentationStatusViolations(
    const std::filesystem::path& root, std::pmr::vector<Violation>& violations)
{
    const auto rhiDesignPath = root / "docs" / "rhi-v2-layered-design.md";
    const auto rhiDesignText = readText(rhiDesignPath);
    const char* requiredStatusTokens[] = {
        "## Status Taxonomy",
        "- Contract Complete:",
        "- Prototype Complete:",
        "- Backend Validated:",
        "- Platform Validated:",
        "- Production Ready:",
    };
    for (const auto* token : requiredStatusTokens) {
        requireText(rhiDesignPath, rhiDesignText, token, violations);
    }
    const char* requiredRenderFrameDirectorTokens[] = {
        "## RenderFrameDirector Ownership Split",
        "RenderSystem owns platform frame execution",
        "renderer owns payload adaptation and pass composition semantics",
        "director decisions carry generic prepared renderer-frame requirements",
        "backend/platform/production status is not promoted by contract evidence",
    };
    for (const auto* token : requiredRenderFrameDirectorTokens) {
        requireText(rhiDesignPath, rhiDesignText, token, violations);
    }

    const std::filesystem::path productionReadyDocs[] = {
        rhiDesignPath,
        root / "docs" / "superpowers" / "plans" / "2026-05-16-rhi-forward-long-plan.md",
        root / "docs" / "roadmap.md",
    };
    const char* requiredProductionReadyEvidenceTokens[] = {
        "Production Ready evidence requirements",
        "conformance evidence",
        "backend parity evidence",
        "platform smoke evidence",
        "memory lifetime evidence",
        "debug telemetry evidence",
        "migration boundary evidence",
        "visual regression evidence",
    };
    for (const auto& docPath : productionReadyDocs) {
        const auto text = readText(docPath);
        for (const auto* token : requiredProductionReadyEvidenceTokens) {
            requireText(docPath, text, token, violations);
        }
    }

    const std::filesystem::path statusLadderDocs[] = {
        rhiDesignPath,
        root / "docs" / "roadmap.md",
        root / "TODO.md",
    };
    const char* requiredStatusDistinctionTokens[] = {
        "Contract Complete is not Backend Validated.",
        "Prototype Complete is not Backend Validated.",
        "Backend Validated is not Platform Validated.",
        "Platform Validated is not Production Ready.",
        "Production Ready requires backend, platform, migration, telemetry, lifetime, and final smoke evidence.",
    };
    for (const auto& docPath : statusLadderDocs) {
        const auto text = readText(docPath);
        for (const auto* token : requiredStatusDistinctionTokens) {
            requireText(docPath, text, token, violations);
        }
    }
}

bool rhiBackendDelegatesToNull(const std::filesystem::path& root)
{
    const std::filesystem::path backendFiles[] = {
        root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_device.cpp",
        root / "src" / "rhi" / "backends" / "metal" / "metal_device.cpp",
        root / "src" / "rhi" / "backends" / "webgpu" / "webgpu_device.cpp",
    };
    for (const auto& file : backendFiles) {
        if (!std::filesystem::exists(file)) {
            continue;
        }
        const auto text = readText(file);
        if (text.find("delegate_(rhi::createNullDevice())") != std::pmr::string::npos) {
            return true;
        }
    }
    return false;
}

void collectRhiBackendStatusGuardrailViolations(
    const std::filesystem::path& root, std::pmr::vector<Violation>& violations)
{
    if (!rhiBackendDelegatesToNull(root)) {
        return;
    }

    const std::filesystem::path guardedDocs[] = {
        root / "TODO.md",
        root / "docs" / "rhi-v2-layered-design.md",
        root / "docs" / "roadmap.md",
        root / "docs" / "superpowers" / "plans" / "2026-05-16-rhi-forward-long-plan.md",
    };
    const char* requiredTruthfulStatusTokens[] = {
        "Contract/Facade Complete",
        "Production Ready is blocked",
        "src/rhi/backends/vulkan",
        "src/rhi/backends/metal",
        "src/rhi/backends/webgpu",
        "rhi::createNullDevice()",
    };
    for (const auto& docPath : guardedDocs) {
        const auto text = readText(docPath);
        for (const auto* token : requiredTruthfulStatusTokens) {
            requireText(docPath, text, token, violations);
        }
    }

    const auto todoPath = root / "TODO.md";
    const auto todoText = readText(todoPath);
    requireText(todoPath, todoText, "## Backend Gap Inventory", violations);
    requireText(todoPath,
        todoText,
        "native resources, descriptors, pipelines, command encoders, queue submit, swapchain/present",
        violations);
    forbidText(todoPath, todoText, "Status: Complete.", violations);

    const auto rhiDesignPath = root / "docs" / "rhi-v2-layered-design.md";
    const auto rhiDesignText = readText(rhiDesignPath);
    requireText(rhiDesignPath, rhiDesignText, "## RHI Backend Completion Status", violations);
    requireText(rhiDesignPath,
        rhiDesignText,
        "native resources, descriptors, pipelines, command encoders, queue submit, swapchain/present",
        violations);
    forbidText(rhiDesignPath,
        rhiDesignText,
        "Production Ready still requires the final current verification matrix.",
        violations);

    const auto roadmapPath = root / "docs" / "roadmap.md";
    const auto roadmapText = readText(roadmapPath);
    requireText(roadmapPath, roadmapText, "## RHI Current Backend Status", violations);
    requireText(roadmapPath,
        roadmapText,
        "native resources, descriptors, pipelines, command encoders, queue submit, swapchain/present",
        violations);

    const auto forwardPlanPath =
        root / "docs" / "superpowers" / "plans" / "2026-05-16-rhi-forward-long-plan.md";
    const auto forwardPlanText = readText(forwardPlanPath);
    requireText(forwardPlanPath,
        forwardPlanText,
        "Production Ready is blocked by real native RHI backend implementations",
        violations);
    forbidText(forwardPlanPath,
        forwardPlanText,
        "Production Ready: blocked until final current status documentation and the final Production Ready verification matrix are complete.",
        violations);
}

void collectRhiMigrationBoundaryViolations(
    const std::filesystem::path& root, std::pmr::vector<Violation>& violations)
{
    const auto boundaryDocPath = root / "docs" / "rhi-migration-boundary.md";
    const auto boundaryDocText =
        std::filesystem::exists(boundaryDocPath) ? readText(boundaryDocPath) : std::pmr::string{};
    if (!std::filesystem::exists(boundaryDocPath)) {
        violations.push_back({boundaryDocPath, "missing RHI migration boundary document"});
    }

    const char* requiredDocTokens[] = {
        "Status: Contract Complete",
        "Removed production boundary",
        "RHI-only production files",
        "No active compatibility seams",
        "src/renderer/gui/gui_rhi_draw_commands.h",
        "src/render_graph/rhi_frame_plan.h",
        "src/render_graph/rhi_frame_plan.cpp",
        "src/render_graph/render_graph.h",
        "src/platform/render_system_vulkan.cpp",
        "src/renderer/renderers.h",
        "src/renderer/ui/gui_renderer.h",
        "src/renderer/particles/particle_renderer.h",
        "src/renderer/postprocess/fxaa_renderer.h",
        "src/renderer/postprocess/ssao_renderer.h",
    };
    for (const auto* token : requiredDocTokens) {
        requireText(boundaryDocPath, boundaryDocText, token, violations);
    }

    const std::filesystem::path migratedProductionFiles[] = {
        root / "src" / "renderer" / "gui" / "gui_rhi_draw_commands.h",
        root / "src" / "render_graph" / "rhi_frame_plan.h",
        root / "src" / "render_graph" / "rhi_frame_plan.cpp",
    };
    const char* forbiddenMigratedTokens[] = {
        "\"rhi/rhi.h\"",
        "<vulkan/",
        "<Metal/",
        "<webgpu/",
        "\"rhi/vulkan/",
        "\"rhi/metal/",
        "rhi"
        "::Device",
        "rhi"
        "::CommandList",
        "VkDevice",
        "VkImage",
        "MTLDevice",
        "MTLTexture",
        "WGPUDevice",
    };
    for (const auto& file : migratedProductionFiles) {
        if (!std::filesystem::exists(file)) {
            violations.push_back({file, "missing migrated production file"});
            continue;
        }
        const auto text = readText(file);
        for (const auto* token : forbiddenMigratedTokens) {
            forbidText(file, text, token, violations);
        }
    }
}

// SDL3 has been fully removed from the engine: no translation unit under
// src/ may reference SDL types or headers again (WindowSystem/1 replaced the
// SDL facade). This scan covers the entire production tree, not just the
// scene-runtime roots.
void collectSdlFreeBoundaryViolations(
    const std::filesystem::path& root, std::pmr::vector<Violation>& violations)
{
    const char* forbiddenSdlTokens[] = {
        "SDL_",
        "SDL3",
        "SdlPlatform",
        "SdlWindow",
        "sdl_window.h",
    };
    const auto sourceRoot = root / "src";
    if (!std::filesystem::exists(sourceRoot)) {
        violations.push_back({sourceRoot, "missing src root"});
        return;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(sourceRoot)) {
        if (!entry.is_regular_file() || !isSourceFile(entry.path())) {
            continue;
        }
        const auto text = readText(entry.path());
        for (const auto* token : forbiddenSdlTokens) {
            forbidText(entry.path(), text, token, violations);
        }
    }
}

void printViolations(
    const std::filesystem::path& root, const std::pmr::vector<Violation>& violations)
{
    for (const auto& violation : violations) {
        std::cerr << std::filesystem::relative(violation.file, root).generic_string()
                  << ": forbidden backend token '" << violation.token << "'\n";
    }
}

} // namespace

int main()
{
    const auto root = findProjectRoot();
    std::pmr::vector<Violation> violations;
    LegacyRhiRemovalBoundaryScanner(root).collect(violations);
    collectAssetsBoundaryViolations(root, violations);
    collectResourceBoundaryViolations(root, violations);
    collectRhiBoundaryViolations(root, violations);
    collectRenderGraphBoundaryViolations(root, violations);
    collectRendererBoundaryViolations(root, violations);
    collectUploadStagingProtectedBoundaryViolations(root, violations);
    collectGuiCoreBoundaryViolations(root, violations);
    collectSampleBoundaryViolations(root, violations);
    collectLegacyRendererRemovalViolations(root, violations);
    collectRuntimeBootstrapViolations(root, violations);
    collectCustomMainFrameApiViolations(root, violations);
    collectGenericRenderFrameBoundaryViolations(root, violations);
    collectRendererFrameDirectorGuardrailViolations(root, violations);
    collectStylizedImplementationOwnershipViolations(root, violations);
    collectCoreMathOwnershipViolations(root, violations);
    collectVulkanResourceLifetimeViolations(root, violations);
    collectProductionHotPathViolations(root, violations);
    collectMetalBackendSplitViolations(root, violations);
    collectWebGpuWebBuildViolations(root, violations);
    collectRhiMigrationBoundaryViolations(root, violations);
    collectSceneRuntimeBoundaryViolations(root, violations);
    collectSceneRuntimeRendererIntegrationGuardrailViolations(root, violations);
    collectSdlFreeBoundaryViolations(root, violations);
    collectDocumentationStatusViolations(root, violations);
    collectRhiBackendStatusGuardrailViolations(root, violations);
    printViolations(root, violations);
    assert(violations.empty());
    return 0;
}
