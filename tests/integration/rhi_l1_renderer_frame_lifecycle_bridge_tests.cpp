#include "core/memory/cpu_allocator.h"
#include "renderer/rhi_draw_plan_bridge.h"
#include "rhi/semantics/renderer_frame_lifecycle_bridge.h"
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

arp::rhi::semantics::RendererFrameLifecycleBridgeDesc makeBridgeDesc(
    const arp::rhi::WebGpuRuntimeProfile& profile, std::uint32_t maxTelemetryDiagnostics = 8)
{
    return {
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
    RendererFrameLifecycleBridge bridge(makeBridgeDesc(browserProfile));
    const auto readyDecision =
        bridge.evaluate(frame, makeWebGpuCanvasLifecycleState(browserProfile));
    assert(readyDecision.success);
    assert(readyDecision.canStartFrame);
    assert(readyDecision.status == RendererFrameLifecycleReadinessStatus::FrameStartAllowed);
    assert(readyDecision.selectedBackend == Backend::WebGPU);
    assert(readyDecision.lifecycle.success);
    assert(readyDecision.telemetry.success);
    assert(readyDecision.readiness.success);
    assert(readyDecision.telemetry.frameSlot == 1u);
    assert(readyDecision.readiness.frameSlot == 1u);
    assert(readyDecision.telemetryRequired);
    assert(readyDecision.backendFallbacksHiddenFromUpperLayer);
    assert(!readyDecision.nativeHandleExposedToUpperLayer);

    auto pausedState = makeWebGpuCanvasLifecycleState(browserProfile);
    pausedState.acquireAllowed = false;
    pausedState.presentAllowed = false;
    const auto blockedDecision = bridge.evaluate(frame, pausedState);
    assert(!blockedDecision.success);
    assert(!blockedDecision.canStartFrame);
    assert(blockedDecision.status == RendererFrameLifecycleReadinessStatus::FrameStartBlocked);
    assert(blockedDecision.selectedBackend == Backend::WebGPU);
    assert(blockedDecision.diagnostics.find("acquire") != std::pmr::string::npos);

    const auto invalidProfile = makeBrowserProfile(false);
    RendererFrameLifecycleBridge fallbackBridge(makeBridgeDesc(invalidProfile));
    const auto fallbackDecision = fallbackBridge.evaluate(frame, makeDesktopReadyState());
    assert(fallbackDecision.success);
    assert(fallbackDecision.canStartFrame);
    assert(fallbackDecision.selectedBackend == Backend::Vulkan);
    assert(fallbackDecision.lifecycle.deployment.diagnosticCandidateCount == 1u);
    assert(fallbackDecision.diagnostics.find("canvas") != std::pmr::string::npos);

    auto leakedFrame = frame;
    leakedFrame.nativeHandleExposedToUpperLayer = true;
    const auto leakedDecision =
        bridge.evaluate(leakedFrame, makeWebGpuCanvasLifecycleState(browserProfile));
    assert(!leakedDecision.success);
    assert(!leakedDecision.canStartFrame);
    assert(leakedDecision.status == RendererFrameLifecycleReadinessStatus::DiagnosticFailure);
    assert(leakedDecision.nativeHandleExposedToUpperLayer);
    assert(leakedDecision.diagnostics.find("native handle") != std::pmr::string::npos);

    RendererFrameLifecycleBridge tinyTelemetryBridge(makeBridgeDesc(browserProfile, 0));
    const auto exhaustedDecision =
        tinyTelemetryBridge.evaluate(leakedFrame, makeWebGpuCanvasLifecycleState(browserProfile));
    assert(!exhaustedDecision.success);
    assert(exhaustedDecision.status == RendererFrameLifecycleReadinessStatus::DiagnosticFailure);
    assert(exhaustedDecision.telemetry.diagnosticCapacityExhausted);
    assert(exhaustedDecision.diagnostics.find("diagnostics capacity") != std::pmr::string::npos);

    return 0;
}
