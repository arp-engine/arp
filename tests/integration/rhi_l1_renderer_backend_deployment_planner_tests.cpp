#include "core/cpu_allocator.h"
#include "renderer/rhi_draw_plan_bridge.h"
#include "rhi/semantics/renderer_backend_deployment_planner.h"
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

arp::rhi::WebGpuRuntimeProfile makeInvalidBrowserProfile()
{
    arp::rhi::WebGpuRuntimeProbe probe;
    probe.browserRuntime = true;
    probe.canvasSurface = false;
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

arp::rhi::semantics::RendererBackendDeploymentPlannerDesc makePlannerDesc(
    arp::rhi::WebGpuRuntimeProfile profile)
{
    return {
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
    };
}

} // namespace

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_l1;

    const auto frame = buildSceneFrame();
    assert(frame.success);

    RendererBackendDeploymentPlanner webFirstPlanner(makePlannerDesc(makeBrowserProfile()));
    const auto webPlan = webFirstPlanner.plan(frame);
    assert(webPlan.success);
    assert(webPlan.canStartFrame);
    assert(webPlan.selectedBackend == Backend::WebGPU);
    assert(webPlan.selectedCandidateIndex == 1u);
    assert(webPlan.candidateCount == 2u);
    assert(webPlan.selectedReadiness.backend == Backend::WebGPU);
    assert(webPlan.selectedReadiness.status
        == NativeRendererDeploymentReadinessStatus::ContractComplete);
    assert(webPlan.telemetryRequired);
    assert(webPlan.backendFallbacksHiddenFromUpperLayer);
    assert(!webPlan.nativeHandleExposedToUpperLayer);

    RendererBackendDeploymentPlanner fallbackPlanner(makePlannerDesc(makeInvalidBrowserProfile()));
    const auto fallbackPlan = fallbackPlanner.plan(frame);
    assert(fallbackPlan.success);
    assert(fallbackPlan.canStartFrame);
    assert(fallbackPlan.selectedBackend == Backend::Vulkan);
    assert(fallbackPlan.selectedCandidateIndex == 0u);
    assert(fallbackPlan.diagnosticCandidateCount == 1u);
    assert(fallbackPlan.diagnostics.find("canvas") != std::pmr::string::npos);

    auto exhaustedDesc = makePlannerDesc(makeBrowserProfile());
    exhaustedDesc.candidates.maxCandidates = 1;
    const auto exhaustedPlan = planRendererBackendDeployment(exhaustedDesc, frame);
    assert(!exhaustedPlan.success);
    assert(!exhaustedPlan.canStartFrame);
    assert(exhaustedPlan.capacityExceeded);
    assert(exhaustedPlan.diagnostics.find("candidate capacity") != std::pmr::string::npos);

    arp::renderer::RendererRhiFramePlanBuildResult failedFrame;
    failedFrame.success = false;
    failedFrame.diagnostics = "renderer RHI frame plan draw plan capacity exhausted";
    const auto failedFramePlan = webFirstPlanner.plan(failedFrame);
    assert(!failedFramePlan.success);
    assert(!failedFramePlan.canStartFrame);
    assert(failedFramePlan.diagnosticCandidateCount == 2u);
    assert(failedFramePlan.diagnostics.find("draw plan capacity") != std::pmr::string::npos);

    return 0;
}
