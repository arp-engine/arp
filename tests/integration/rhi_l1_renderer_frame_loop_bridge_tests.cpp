#include "core/cpu_allocator.h"
#include "renderer/rhi_draw_plan_bridge.h"
#include "rhi/semantics/renderer_frame_loop_bridge.h"
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

arp::platform::RendererFrameLoopBridgeDesc makeLoopDesc(
    const arp::rhi::WebGpuRuntimeProfile& profile, std::uint32_t maxTelemetryDiagnostics = 8)
{
    return {
        {
            {
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
            },
            maxTelemetryDiagnostics,
        },
        2,
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
    RendererFrameLoopBridge bridge(makeLoopDesc(browserProfile));
    const auto begin = bridge.evaluate(frame, makeWebGpuCanvasLifecycleState(browserProfile));
    assert(begin.success);
    assert(begin.action == RendererFrameLoopAction::BeginFrame);
    assert(begin.canStartFrame);
    assert(begin.shouldAcquireFrame);
    assert(begin.shouldRecordFrame);
    assert(begin.shouldSubmitFrame);
    assert(begin.shouldPresentFrame);
    assert(begin.selectedBackend == Backend::WebGPU);
    assert(begin.frameSlot == 1u);
    assert(begin.telemetryRequired);
    assert(begin.backendFallbacksHiddenFromUpperLayer);
    assert(!begin.nativeHandleExposedToUpperLayer);

    auto pausedState = makeWebGpuCanvasLifecycleState(browserProfile);
    pausedState.acquireAllowed = false;
    pausedState.presentAllowed = false;
    const auto skipped = bridge.evaluate(frame, pausedState);
    assert(skipped.success);
    assert(skipped.action == RendererFrameLoopAction::SkipFrame);
    assert(!skipped.canStartFrame);
    assert(!skipped.shouldAcquireFrame);
    assert(!skipped.shouldRecordFrame);
    assert(!skipped.shouldSubmitFrame);
    assert(!skipped.shouldPresentFrame);
    assert(skipped.diagnostics.find("acquire") != std::pmr::string::npos);

    auto maintenanceState = makeWebGpuCanvasLifecycleState(browserProfile);
    maintenanceState.acquireAllowed = false;
    maintenanceState.presentAllowed = false;
    maintenanceState.swapchainRebuildRequired = true;
    maintenanceState.releaseSurfaceResources = true;
    maintenanceState.releaseTransientResources = true;
    maintenanceState.memoryBudgetRefreshRequired = true;
    const auto maintenance = bridge.evaluate(frame, maintenanceState);
    assert(maintenance.success);
    assert(maintenance.action == RendererFrameLoopAction::MaintenanceRequired);
    assert(!maintenance.canStartFrame);
    assert(maintenance.swapchainRebuildRequired);
    assert(maintenance.releaseSurfaceResources);
    assert(maintenance.releaseTransientResources);
    assert(maintenance.memoryBudgetRefreshRequired);
    assert(maintenance.diagnostics.find("maintenance") != std::pmr::string::npos);

    const auto invalidProfile = makeBrowserProfile(false);
    RendererFrameLoopBridge fallbackBridge(makeLoopDesc(invalidProfile));
    const auto fallback = fallbackBridge.evaluate(frame, makeDesktopReadyState());
    assert(fallback.success);
    assert(fallback.action == RendererFrameLoopAction::BeginFrame);
    assert(fallback.selectedBackend == Backend::Vulkan);
    assert(fallback.lifecycle.lifecycle.deployment.diagnosticCandidateCount == 1u);
    assert(fallback.diagnostics.find("canvas") != std::pmr::string::npos);

    auto leakedFrame = frame;
    leakedFrame.nativeHandleExposedToUpperLayer = true;
    const auto leaked =
        bridge.evaluate(leakedFrame, makeWebGpuCanvasLifecycleState(browserProfile));
    assert(!leaked.success);
    assert(leaked.action == RendererFrameLoopAction::DiagnosticFailure);
    assert(!leaked.canStartFrame);
    assert(leaked.nativeHandleExposedToUpperLayer);
    assert(leaked.diagnostics.find("native handle") != std::pmr::string::npos);

    RendererFrameLoopBridge tinyTelemetryBridge(makeLoopDesc(browserProfile, 0));
    const auto exhausted =
        tinyTelemetryBridge.evaluate(leakedFrame, makeWebGpuCanvasLifecycleState(browserProfile));
    assert(!exhausted.success);
    assert(exhausted.action == RendererFrameLoopAction::DiagnosticFailure);
    assert(exhausted.lifecycle.telemetry.diagnosticCapacityExhausted);
    assert(exhausted.diagnostics.find("diagnostics capacity") != std::pmr::string::npos);

    return 0;
}
