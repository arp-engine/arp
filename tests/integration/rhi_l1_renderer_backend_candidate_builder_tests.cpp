#include "core/cpu_allocator.h"
#include "renderer/deployment/renderer_backend_deployment.h"
#include "renderer/rhi_draw_plan_bridge.h"
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

} // namespace

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_l1;

    const auto frame = buildSceneFrame();
    assert(frame.success);

    const auto browserProfile = makeBrowserProfile();
    RendererBackendCandidateBuilder builder({
        2,
        {makeQueriedVulkan()},
        true,
        browserProfile,
        makeWebGpuCanvasLifecycleState(browserProfile),
        PlatformLifecycleEvent::Start,
        8,
    });
    const auto candidates = builder.build(frame);
    assert(candidates.success);
    assert(candidates.selection.maxCandidates == 2u);
    assert(candidates.selection.candidates.size() == 2u);
    assert(candidates.selection.candidates[0].backend == Backend::Vulkan);
    assert(candidates.selection.candidates[0].status
        == NativeRendererDeploymentReadinessStatus::BackendValidated);
    assert(candidates.selection.candidates[1].backend == Backend::WebGPU);
    assert(candidates.selection.candidates[1].status
        == NativeRendererDeploymentReadinessStatus::ContractComplete);
    assert(candidates.selection.candidates[1].telemetryRequired);

    const auto webSelection =
        selectRendererBackend(RendererBackendPreference::WebFirst, candidates.selection);
    assert(webSelection.success);
    assert(webSelection.selectedBackend == Backend::WebGPU);
    assert(webSelection.selectedCandidateIndex == 1u);

    WebGpuRuntimeProbe invalidProbe;
    invalidProbe.browserRuntime = true;
    invalidProbe.canvasSurface = false;
    invalidProbe.maxTexture2DSize = 4096;
    invalidProbe.maxColorAttachments = 4;
    invalidProbe.maxDescriptorBindings = 64;
    invalidProbe.maxSampledTexturesPerShaderStage = 16;
    invalidProbe.uniformBufferOffsetAlignment = 256;
    invalidProbe.storageBufferOffsetAlignment = 256;
    invalidProbe.maxUniformBufferBindingSize = 65536;
    invalidProbe.maxStorageBufferBindingSize = 134217728;
    const auto invalidProfile = buildWebGpuRuntimeProfile(invalidProbe);
    RendererBackendCandidateBuilder invalidWebBuilder({
        2,
        {makeQueriedVulkan()},
        true,
        invalidProfile,
        makeWebGpuCanvasLifecycleState(invalidProfile),
        PlatformLifecycleEvent::Start,
        8,
    });
    const auto invalidWebCandidates = invalidWebBuilder.build(frame);
    assert(invalidWebCandidates.success);
    assert(invalidWebCandidates.selection.candidates.size() == 2u);
    assert(invalidWebCandidates.selection.candidates[1].backend == Backend::WebGPU);
    assert(!invalidWebCandidates.selection.candidates[1].success);
    assert(invalidWebCandidates.selection.candidates[1].diagnostics.find("canvas")
        != std::pmr::string::npos);

    const auto fallbackSelection =
        selectRendererBackend(RendererBackendPreference::WebFirst, invalidWebCandidates.selection);
    assert(fallbackSelection.success);
    assert(fallbackSelection.selectedBackend == Backend::Vulkan);
    assert(fallbackSelection.diagnosticCandidateCount == 1u);
    assert(fallbackSelection.diagnostics.find("canvas") != std::pmr::string::npos);

    RendererBackendCandidateBuilder exhaustedBuilder({
        1,
        {makeQueriedVulkan()},
        true,
        browserProfile,
        makeWebGpuCanvasLifecycleState(browserProfile),
        PlatformLifecycleEvent::Start,
        8,
    });
    const auto exhausted = exhaustedBuilder.build(frame);
    assert(!exhausted.success);
    assert(exhausted.capacityExceeded);
    assert(exhausted.diagnostics.find("candidate capacity") != std::pmr::string::npos);

    return 0;
}
