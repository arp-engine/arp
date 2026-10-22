#include "core/memory/cpu_allocator.h"
#include "platform/render_system.h"
#include "rhi/backends/metal/metal_runtime_smoke.h"
#include "rhi/backends/vulkan/vulkan_device.h"

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

void requireText(const std::pmr::string& text, const std::pmr::string& token)
{
    assert(text.find(token) != std::pmr::string::npos);
}

} // namespace

int main()
{
    arp::platform::RenderSystemSceneRuntimeCustomGraphFrameReport report;
    assert(!report.hiddenWindowRenderOutputHashCaptured);
    assert(report.deterministicRenderOutputHash == 0u);
    assert(std::pmr::string(report.renderOutputEvidenceDiagnostic)
        == "RHI-OUTPUT-ORIGINAL-OBSERVATION-UNAVAILABLE");
    assert(!report.nativeHandleExposedToUpperLayer);
    const arp::rhi_vulkan::VulkanRhiNativeResourceStats vulkan;
    assert(!vulkan.hiddenWindowRenderOutputHashCaptured);
    assert(vulkan.deterministicRenderOutputHash == 0u);
    assert(vulkan.renderOutputHashCaptureCount == 0u);
    const arp::rhi_metal::MetalRhiAppleRuntimeSmokeReport metal;
    assert(!metal.appleRuntimeRenderOutputHashCaptured);
    assert(metal.deterministicRenderOutputHash == 0u);

#if defined(ARP_SOURCE_DIR)
    const auto root = std::filesystem::path(ARP_SOURCE_DIR);
    const auto source = readText(root / "src" / "platform" / "render_system_webgpu.cpp");
    assert(
        source.find("deterministicWebGpuSceneRuntimeRenderOutputHash") == std::pmr::string::npos);
    const auto shared = readText(root / "src" / "platform" / "render_system_native_shared.cpp");
    requireText(source, "applyNativeSceneRuntimeFrameStats");
    requireText(source, "inspectWebGpuRhiNativeCommandStats");
    requireText(source, "inspectWebGpuRhiNativeSwapchainStats");
    requireText(shared, "sceneRuntimeCustomGraphPendingPresent");
    requireText(shared, "lastSceneRuntimeCustomGraphFrameReport() const");
    requireText(source, "target.hiddenWindowRenderOutputHashCaptured = false;");
    requireText(source, "target.deterministicRenderOutputHash = 0;");
    requireText(source, "report.hiddenWindowRenderOutputHashCaptured = false;");
    requireText(source, "report.deterministicRenderOutputHash = 0;");
    requireText(source, "surfaceBackedPresentation = true;");
    requireText(shared, "RendererRhiProductionFramePath");
    requireText(shared, "prepareRendererFrame(");
    requireText(shared, "submitRhiProductionFrame(");
    requireText(shared, "RenderOutputObservationContract::unavailableDiagnostic");
    const auto vulkanSource = readText(root / "src/rhi/backends/vulkan/vulkan_device.cpp");
    assert(vulkanSource.find("withRenderOutputHash") == std::pmr::string::npos);
    requireText(vulkanSource, "return vulkanDevice->nativeResourceStats();");
    const auto metalSource = readText(root / "src/rhi/backends/metal/metal_runtime_smoke.cpp");
    assert(metalSource.find("deterministicMetalRenderOutputHash") == std::pmr::string::npos);
    requireText(metalSource, "hashBytes(");
    assert(source.find("requires the Vulkan backend path") == std::pmr::string::npos);
#endif

    return 0;
}
