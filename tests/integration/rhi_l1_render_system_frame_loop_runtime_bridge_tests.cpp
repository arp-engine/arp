#include "core/cpu_allocator.h"
#include "renderer/rhi_draw_plan_bridge.h"
#include "rhi/semantics/render_system_frame_loop_runtime_bridge.h"
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
    probe.maxSampledTexturesPerShaderStage = 16;
    probe.uniformBufferOffsetAlignment = 256;
    probe.storageBufferOffsetAlignment = 256;
    probe.maxUniformBufferBindingSize = 65536;
    probe.maxStorageBufferBindingSize = 134217728;
    return arp::rhi::buildWebGpuRuntimeProfile(probe);
}

arp::platform::RenderSystemFrameLoopRuntimeBridgeDesc makeRuntimeDesc(
    const arp::rhi::WebGpuRuntimeProfile& profile, std::uint32_t maxTelemetryDiagnostics = 8)
{
    return {
        {
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
        },
        {2},
    };
}

arp::platform::RenderSystemFrameLoopState makeRenderSystemReadyState()
{
    arp::platform::RenderSystemFrameLoopState state;
    state.renderPipelineReady = true;
    state.frameInProgress = false;
    state.nextFrameSerial = 42;
    return state;
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
    RenderSystemFrameLoopRuntimeBridge runtime(makeRuntimeDesc(browserProfile));
    const auto begin = runtime.plan(
        frame, makeWebGpuCanvasLifecycleState(browserProfile), makeRenderSystemReadyState());
    assert(begin.success);
    assert(begin.action == RenderSystemFrameLoopAction::BeginFrame);
    assert(begin.rendererFrameLoop.action == RendererFrameLoopAction::BeginFrame);
    assert(begin.renderSystemPlan.action == RenderSystemFrameLoopAction::BeginFrame);
    assert(begin.selectedBackend == Backend::WebGPU);
    assert(begin.frameSlot == 1u);
    assert(begin.frameSerial == 42u);
    assert(begin.acquireFrame);
    assert(begin.recordFrame);
    assert(begin.submitFrame);
    assert(begin.presentFrame);
    assert(begin.telemetryRequired);
    assert(begin.backendFallbacksHiddenFromUpperLayer);
    assert(!begin.nativeHandleExposedToUpperLayer);

    auto pausedState = makeWebGpuCanvasLifecycleState(browserProfile);
    pausedState.acquireAllowed = false;
    pausedState.presentAllowed = false;
    const auto skipped = runtime.plan(frame, pausedState, makeRenderSystemReadyState());
    assert(skipped.success);
    assert(skipped.action == RenderSystemFrameLoopAction::SkipFrame);
    assert(skipped.skipFrame);
    assert(!skipped.acquireFrame);
    assert(skipped.collectCompletedSubmissions);
    assert(skipped.diagnostics.find("acquire") != std::pmr::string::npos);

    auto maintenanceState = makeWebGpuCanvasLifecycleState(browserProfile);
    maintenanceState.acquireAllowed = false;
    maintenanceState.presentAllowed = false;
    maintenanceState.swapchainRebuildRequired = true;
    maintenanceState.releaseSurfaceResources = true;
    maintenanceState.releaseTransientResources = true;
    maintenanceState.memoryBudgetRefreshRequired = true;
    const auto maintenance = runtime.plan(frame, maintenanceState, makeRenderSystemReadyState());
    assert(maintenance.success);
    assert(maintenance.action == RenderSystemFrameLoopAction::MaintenanceRequired);
    assert(maintenance.runMaintenance);
    assert(maintenance.releaseCurrentFrame);
    assert(maintenance.swapchainRebuildRequired);
    assert(maintenance.releaseSurfaceResources);
    assert(maintenance.releaseTransientResources);
    assert(maintenance.memoryBudgetRefreshRequired);

    const auto invalidProfile = makeBrowserProfile(false);
    RenderSystemFrameLoopRuntimeBridge fallbackRuntime(makeRuntimeDesc(invalidProfile));
    const auto fallback =
        fallbackRuntime.plan(frame, makeDesktopReadyState(), makeRenderSystemReadyState());
    assert(fallback.success);
    assert(fallback.action == RenderSystemFrameLoopAction::BeginFrame);
    assert(fallback.selectedBackend == Backend::Vulkan);
    assert(
        fallback.rendererFrameLoop.lifecycle.lifecycle.deployment.diagnosticCandidateCount == 1u);
    assert(fallback.diagnostics.find("canvas") != std::pmr::string::npos);

    auto noPipelineState = makeRenderSystemReadyState();
    noPipelineState.renderPipelineReady = false;
    const auto noPipeline =
        runtime.plan(frame, makeWebGpuCanvasLifecycleState(browserProfile), noPipelineState);
    assert(!noPipeline.success);
    assert(noPipeline.action == RenderSystemFrameLoopAction::DiagnosticFailure);
    assert(noPipeline.rendererFrameLoop.success);
    assert(!noPipeline.renderSystemPlan.success);
    assert(noPipeline.diagnostics.find("render pipeline") != std::pmr::string::npos);

    auto leakedFrame = frame;
    leakedFrame.nativeHandleExposedToUpperLayer = true;
    const auto leaked = runtime.plan(
        leakedFrame, makeWebGpuCanvasLifecycleState(browserProfile), makeRenderSystemReadyState());
    assert(!leaked.success);
    assert(leaked.action == RenderSystemFrameLoopAction::DiagnosticFailure);
    assert(leaked.nativeHandleExposedToUpperLayer);
    assert(leaked.diagnostics.find("native handle") != std::pmr::string::npos);

    RenderSystemFrameLoopRuntimeBridge tinyRuntime(makeRuntimeDesc(browserProfile, 0));
    const auto exhausted = tinyRuntime.plan(
        leakedFrame, makeWebGpuCanvasLifecycleState(browserProfile), makeRenderSystemReadyState());
    assert(!exhausted.success);
    assert(exhausted.action == RenderSystemFrameLoopAction::DiagnosticFailure);
    assert(exhausted.rendererFrameLoop.lifecycle.telemetry.diagnosticCapacityExhausted);
    assert(exhausted.diagnostics.find("diagnostics capacity") != std::pmr::string::npos);

    return 0;
}
