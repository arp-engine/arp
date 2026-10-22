#include "core/memory/cpu_allocator.h"
#include "renderer/rhi_draw_plan_bridge.h"
#include "rhi/semantics/renderer_backend_deployment_planner.h"
#include "rhi/semantics/renderer_frame_start_gate.h"
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

arp::rhi::WebGpuRuntimeProfile makeBrowserProfile()
{
    arp::rhi::WebGpuRuntimeProbe probe;
    probe.browserRuntime = true;
    probe.canvasSurface = true;
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

arp::rhi::semantics::RendererBackendDeploymentPlan makeDeploymentPlan(
    const arp::renderer::RendererRhiFramePlanBuildResult& frame,
    const arp::rhi::WebGpuRuntimeProfile& profile)
{
    arp::rhi::semantics::RendererBackendDeploymentPlanner planner({
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
    });
    return planner.plan(frame);
}

} // namespace

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_l1;

    const auto frame = buildSceneFrame();
    assert(frame.success);

    const auto profile = makeBrowserProfile();
    const auto deployment = makeDeploymentPlan(frame, profile);
    assert(deployment.success);

    RendererFrameStartGate gate({2, true, true});
    const auto state = makeWebGpuCanvasLifecycleState(profile);
    const auto start = gate.evaluate(deployment, frame.plan, state);
    assert(start.success);
    assert(start.canStartFrame);
    assert(start.backend == Backend::WebGPU);
    assert(start.frameSlot == 1u);
    assert(start.telemetryRequired);
    assert(!start.nativeHandleExposedToUpperLayer);
    assert(start.backendFallbacksHiddenFromUpperLayer);

    auto pausedState = state;
    pausedState.paused = true;
    pausedState.acquireAllowed = false;
    pausedState.presentAllowed = false;
    const auto pausedStart = gate.evaluate(deployment, frame.plan, pausedState);
    assert(!pausedStart.success);
    assert(!pausedStart.canStartFrame);
    assert(pausedStart.diagnostics.find("acquire") != std::pmr::string::npos);

    auto invalidFrame = frame.plan;
    invalidFrame.frameSlot = 2;
    const auto invalidSlotStart = gate.evaluate(deployment, invalidFrame, state);
    assert(!invalidSlotStart.success);
    assert(!invalidSlotStart.canStartFrame);
    assert(invalidSlotStart.diagnostics.find("frame slot") != std::pmr::string::npos);

    auto leakingDeployment = deployment;
    leakingDeployment.nativeHandleExposedToUpperLayer = true;
    const auto leakingStart = gate.evaluate(leakingDeployment, frame.plan, state);
    assert(!leakingStart.success);
    assert(!leakingStart.canStartFrame);
    assert(leakingStart.nativeHandleExposedToUpperLayer);
    assert(leakingStart.diagnostics.find("native handle") != std::pmr::string::npos);

    RendererBackendDeploymentPlan failedDeployment;
    failedDeployment.success = false;
    failedDeployment.canStartFrame = false;
    failedDeployment.diagnostics = "renderer backend deployment failed";
    const auto failedStart = gate.evaluate(failedDeployment, frame.plan, state);
    assert(!failedStart.success);
    assert(!failedStart.canStartFrame);
    assert(failedStart.diagnostics.find("deployment") != std::pmr::string::npos);

    return 0;
}
