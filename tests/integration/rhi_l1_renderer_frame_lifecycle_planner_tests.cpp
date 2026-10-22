#include "core/memory/cpu_allocator.h"
#include "renderer/rhi_draw_plan_bridge.h"
#include "rhi/semantics/renderer_frame_lifecycle_planner.h"
#include "rhi/semantics/webgpu_canvas_lifecycle_bridge.h"

#include <cassert>
#include <string>

namespace {

arp::renderer::RendererRhiDrawPlanDesc makeScenePass()
{
    arp::renderer::RendererRhiDrawPlanDesc pass;
    pass.name = "StylizedScene";
    pass.frameSlot = 1;
    pass.drawCommandCount = 24;
    pass.pushConstantSizeBytes = 192;
    pass.pipelinePushConstantCapacityBytes = 192;
    pass.descriptorSetCount = 2;
    pass.descriptorLayoutNameCount = 2;
    pass.descriptorLayoutNames[0] = "SceneFrameSet";
    pass.descriptorLayoutNames[1] = "MaterialSet";
    pass.descriptorTableCount = 1;
    pass.allowSoftSecondaryCommands = true;
    pass.usesTransientTargets = true;
    return pass;
}

arp::renderer::RendererRhiFramePlanBuildResult buildSceneFrame()
{
    arp::renderer::RendererRhiFramePlanBuilder builder({2, 2, 8, 2, 2});
    return builder.setFrameSlot(1).addDrawPlan(makeScenePass()).build();
}

arp::rhi::semantics::NativeBackendCapabilityContract makeQueriedVulkan()
{
    auto contract = arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Vulkan);
    contract.queriedFromNativeDevice = true;
    contract.queriedExtendedFeatures = true;
    return contract;
}

arp::rhi::WebGpuRuntimeProfile makeBrowserProfile(bool canvasSurface)
{
    arp::rhi::WebGpuRuntimeProbe probe;
    probe.browserRuntime = true;
    probe.canvasSurface = canvasSurface;
    probe.maxTexture2DSize = 4096;
    probe.maxColorAttachments = 4;
    probe.maxDescriptorBindings = 64;
    probe.maxSampledTexturesPerShaderStage = 16;
    probe.uniformBufferOffsetAlignment = 256;
    probe.storageBufferOffsetAlignment = 256;
    probe.maxUniformBufferBindingSize = 65536;
    probe.maxStorageBufferBindingSize = 134217728;
    return arp::rhi::buildWebGpuRuntimeProfile(probe);
}

arp::rhi::semantics::RendererFrameLifecyclePlannerDesc makePlannerDesc(
    const arp::rhi::WebGpuRuntimeProfile& profile)
{
    return {
        {
            arp::renderer::deployment::RendererBackendPreference::WebFirst,
            {
                2,
                {makeQueriedVulkan()},
                true,
                profile,
                arp::rhi::semantics::makeWebGpuCanvasLifecycleState(profile),
                arp::platform::PlatformLifecycleEvent::Start,
                8,
            },
        },
        {2, true, true},
    };
}

arp::platform::PlatformLifecycleState makeDesktopReadyState()
{
    arp::platform::PlatformLifecycleState state;
    state.surfaceKind = arp::platform::PlatformSurfaceKind::DesktopWindow;
    state.logicalFrameSlots = 2;
    state.running = true;
    state.surfaceValid = true;
    state.acquireAllowed = true;
    state.presentAllowed = true;
    return state;
}

} // namespace

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_l1;

    const auto frame = buildSceneFrame();
    assert(frame.success);

    const auto browserProfile = makeBrowserProfile(true);
    RendererFrameLifecyclePlanner planner(makePlannerDesc(browserProfile));
    const auto webLifecycle = planner.plan(frame, makeWebGpuCanvasLifecycleState(browserProfile));
    assert(webLifecycle.success);
    assert(webLifecycle.canStartFrame);
    assert(webLifecycle.selectedBackend == Backend::WebGPU);
    assert(webLifecycle.deployment.success);
    assert(webLifecycle.frameStart.success);
    assert(webLifecycle.frameStart.frameSlot == 1u);
    assert(webLifecycle.telemetryRequired);
    assert(!webLifecycle.nativeHandleExposedToUpperLayer);

    auto pausedState = makeWebGpuCanvasLifecycleState(browserProfile);
    pausedState.paused = true;
    pausedState.acquireAllowed = false;
    pausedState.presentAllowed = false;
    const auto pausedLifecycle = planner.plan(frame, pausedState);
    assert(!pausedLifecycle.success);
    assert(!pausedLifecycle.canStartFrame);
    assert(pausedLifecycle.selectedBackend == Backend::WebGPU);
    assert(pausedLifecycle.deployment.success);
    assert(!pausedLifecycle.frameStart.success);
    assert(pausedLifecycle.diagnostics.find("acquire") != std::pmr::string::npos);

    const auto invalidProfile = makeBrowserProfile(false);
    RendererFrameLifecyclePlanner fallbackPlanner(makePlannerDesc(invalidProfile));
    const auto fallbackLifecycle = fallbackPlanner.plan(frame, makeDesktopReadyState());
    assert(fallbackLifecycle.success);
    assert(fallbackLifecycle.canStartFrame);
    assert(fallbackLifecycle.selectedBackend == Backend::Vulkan);
    assert(fallbackLifecycle.deployment.diagnosticCandidateCount == 1u);
    assert(fallbackLifecycle.diagnostics.find("canvas") != std::pmr::string::npos);

    arp::renderer::RendererRhiFramePlanBuildResult failedFrame;
    failedFrame.success = false;
    failedFrame.diagnostics = "renderer RHI frame plan draw plan capacity exhausted";
    const auto failedLifecycle =
        planner.plan(failedFrame, makeWebGpuCanvasLifecycleState(browserProfile));
    assert(!failedLifecycle.success);
    assert(!failedLifecycle.canStartFrame);
    assert(failedLifecycle.diagnostics.find("draw plan capacity") != std::pmr::string::npos);

    return 0;
}
