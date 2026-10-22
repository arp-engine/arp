#include "core/memory/cpu_allocator.h"
#include "platform/render_system.h"
#include "samples/scene_runtime_demo/scene_runtime_demo.h"

#include <cassert>

namespace {

bool hasDeterministicVulkanDiagnostic(const std::pmr::string& diagnostics)
{
    return diagnostics.find("Vulkan") != std::pmr::string::npos
        || diagnostics.find("SDL") != std::pmr::string::npos
        || diagnostics.find("surface") != std::pmr::string::npos
        || diagnostics.find("device") != std::pmr::string::npos
        || diagnostics.find("swapchain") != std::pmr::string::npos;
}

void assertSceneRuntimeDemoUsesCustomGraphVulkanBackendPath()
{
    arp::samples::scene_runtime_demo::SceneRuntimeDemoOptions options;
    options.hidden = true;
    options.frameLimit = 2;
    options.width = 320;
    options.height = 180;

    const auto result = arp::samples::scene_runtime_demo::runSceneRuntimeDemo(options);
    assert(result.exitCode == 0);
    assert(result.framesRendered == 2u);
    assert(result.renderedToSwapchain);
    assert(result.usedSurfaceBackedVulkan);
    assert(result.customGraphRenderGraphFrameUsed);
    assert(result.customGraphSceneRuntimePathSubmitted);
    assert(!result.directRhiSubmitValidationUsed);

    assert(result.customGraphRhiGpuDeviceUsed);
    assert(result.customGraphPreparedBeforeSubmit);
    assert(result.customGraphResourcesPreparedBeforeSubmit);
    assert(result.customGraphPipelinePreparedBeforeSubmit);
    assert(result.customGraphDescriptorsPreparedBeforeSubmit);
    assert(result.customGraphPrepareCount == 1u);
    assert(result.customGraphFramesSubmitted == 2u);

    assert(result.customGraphCommandEncoded);
    assert(result.customGraphQueueSubmitted);
    assert(result.customGraphSwapchainRenderTargetUsed);
    assert(result.customGraphProceduralSceneRuntime3DObjectDrawn);
    assert(result.customGraphProceduralSceneRuntime2DSpriteDrawn);
    assert(result.customGraphProceduralSceneRuntimeUiDrawn);
    assert(result.customGraphRenderPassCount >= 2u);
    assert(result.customGraphDrawCommandCount >= 6u);

    assert(result.customGraphVulkanNativeStatsCaptured);
    assert(result.customGraphVulkanNativeRenderPassBeginCount >= 2u);
    assert(result.customGraphVulkanNativeIndexedDrawCount >= 6u);
    assert(result.customGraphVulkanNativeQueueSubmitCount >= 2u);
    assert(result.customGraphHiddenWindowRenderOutputHashCaptured);
    assert(!result.hiddenWindowRenderOutputHashCaptured);
    assert(result.deterministicRenderOutputHash == 0u);

    assert(!result.customGraphResourceAllocatedDuringSubmit);
    assert(!result.customGraphPipelineCreatedDuringSubmit);
    assert(!result.customGraphHotPathFileIo);
    assert(!result.customGraphHotPathNetworkIo);
    assert(!result.customGraphNativeHandleExposedToUpperLayer);
    assert(result.customGraphBackendFallbacksHiddenFromUpperLayer);
}

} // namespace

int main()
{
    arp::platform::WindowSystem platform;
    const auto init = platform.initialize();
    if (!init.success) {
        assert(hasDeterministicVulkanDiagnostic(init.diagnostics) || !init.diagnostics.empty());
        return 0;
    }

    auto window = platform.createWindow({
        "SceneRuntimeVulkanHiddenWindow",
        320,
        180,
        true,
        true,
    });
    if (!window.valid()) {
        return 0;
    }

    auto renderSystem = arp::platform::createDefaultRenderSystem(
        platform, window, "SceneRuntimeVulkanHiddenWindow");

    if (!renderSystem.valid()) {
        assert(hasDeterministicVulkanDiagnostic(renderSystem.diagnostics()));
        return 0;
    }

    const auto capabilities = renderSystem.capabilities();
    assert(renderSystem.backend() == arp::rhi::Backend::Vulkan);
    assert(capabilities.canRender);
    assert(capabilities.surfaceBacked);
    assert(!capabilities.diagnostics.empty() == !renderSystem.diagnostics().empty());

    const auto frame = renderSystem.acquireFrame();
    assert(frame.success);

    const auto presented = renderSystem.presentFrame(frame);
    assert(presented.success);

    const auto secondFrame = renderSystem.acquireFrame();
    assert(secondFrame.success);
    const auto secondPresented = renderSystem.presentFrame(secondFrame);
    assert(secondPresented.success);

    assertSceneRuntimeDemoUsesCustomGraphVulkanBackendPath();

    return 0;
}
