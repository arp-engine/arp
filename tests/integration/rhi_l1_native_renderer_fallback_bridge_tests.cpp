#include "core/memory/cpu_allocator.h"
#include "renderer/rhi_draw_plan_bridge.h"
#include "rhi/semantics/native_renderer_fallback_bridge.h"

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

arp::renderer::RendererRhiFramePlanBuildResult buildDescriptorFrame()
{
    auto pass = makeScenePass();
    pass.usesTransientTargets = false;
    pass.allowSoftSecondaryCommands = false;
    pass.pushConstantSizeBytes = 0;

    arp::renderer::RendererRhiFramePlanBuilder builder({2, 1, 4, 1, 1});
    return builder.setFrameSlot(1).addDrawPlan(pass).build();
}

} // namespace

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_l1;

    const auto sceneFrame = buildSceneFrame();
    assert(sceneFrame.success);

    NativeRendererFallbackBridge vulkanBridge(nativeBackendCapabilityContract(Backend::Vulkan));
    const auto vulkanPlan = vulkanBridge.buildPlan(sceneFrame);
    assert(vulkanPlan.success);
    assert(vulkanPlan.backend == Backend::Vulkan);
    assert(vulkanPlan.logicalFrameSlots == 2u);
    assert(vulkanPlan.nativeFeatureCount == 4u);
    assert(vulkanPlan.diagnosticFailureCount == 0u);
    assert(!vulkanPlan.upperLayerVisibleFallbacks);
    assert(vulkanPlan.backendFallbacksHiddenFromUpperLayer);

    NativeBackendCapabilityContract limitedVulkan;
    limitedVulkan.backend = Backend::Vulkan;
    limitedVulkan.preservesNativePotential = true;
    limitedVulkan.features = {
        NativeBackendFeature::PushConstants,
        NativeBackendFeature::SecondaryCommandBuffers,
    };
    const auto descriptorFrame = buildDescriptorFrame();
    assert(descriptorFrame.success);
    const auto limitedVulkanPlan = buildNativeRendererFallbackPlan(limitedVulkan, descriptorFrame);
    assert(limitedVulkanPlan.success);
    assert(limitedVulkanPlan.backend == Backend::Vulkan);
    assert(limitedVulkanPlan.degradedFeatureCount == 1u);
    assert(limitedVulkanPlan.telemetryRequired);
    assert(limitedVulkanPlan.diagnostics.find("descriptor table") != std::pmr::string::npos);
    assert(limitedVulkanPlan.backendFallbacksHiddenFromUpperLayer);

    const auto webgpuPlan = buildNativeRendererFallbackPlan(
        nativeBackendCapabilityContract(Backend::WebGPU), sceneFrame);
    assert(webgpuPlan.success);
    assert(webgpuPlan.backend == Backend::WebGPU);
    assert(webgpuPlan.emulatedFeatureCount == 1u);
    assert(webgpuPlan.cpuReplayFeatureCount == 1u);
    assert(webgpuPlan.degradedFeatureCount == 2u);
    assert(webgpuPlan.telemetryRequired);
    assert(!webgpuPlan.upperLayerVisibleFallbacks);
    assert(webgpuPlan.backendFallbacksHiddenFromUpperLayer);

    const auto nullPlan =
        buildNativeRendererFallbackPlan(nativeBackendCapabilityContract(Backend::Null), sceneFrame);
    assert(nullPlan.success);
    assert(nullPlan.backend == Backend::Null);
    assert(nullPlan.nativeFeatureCount == 4u);
    assert(nullPlan.diagnosticFailureCount == 0u);

    arp::renderer::RendererRhiFramePlanBuildResult failedRendererFrame;
    failedRendererFrame.success = false;
    failedRendererFrame.diagnostics = "renderer RHI frame plan draw plan capacity exhausted";
    const auto failedPlan = buildNativeRendererFallbackPlan(
        nativeBackendCapabilityContract(Backend::Vulkan), failedRendererFrame);
    assert(!failedPlan.success);
    assert(failedPlan.diagnosticFailureCount == 1u);
    assert(failedPlan.telemetryRequired);
    assert(failedPlan.diagnostics.find("draw plan capacity") != std::pmr::string::npos);

    auto leakedRendererFrame = sceneFrame;
    leakedRendererFrame.nativeHandleExposedToUpperLayer = true;
    const auto leakedPlan = vulkanBridge.buildPlan(leakedRendererFrame);
    assert(!leakedPlan.success);
    assert(leakedPlan.diagnosticFailureCount == 1u);
    assert(leakedPlan.upperLayerVisibleFallbacks);
    assert(leakedPlan.diagnostics.find("native handle") != std::pmr::string::npos);

    auto branchingRendererFrame = sceneFrame;
    branchingRendererFrame.backendSpecificBranchingRequired = true;
    const auto branchingPlan = vulkanBridge.buildPlan(branchingRendererFrame);
    assert(!branchingPlan.success);
    assert(branchingPlan.diagnosticFailureCount == 1u);
    assert(branchingPlan.upperLayerVisibleFallbacks);
    assert(!branchingPlan.backendFallbacksHiddenFromUpperLayer);
    assert(branchingPlan.diagnostics.find("backend branching") != std::pmr::string::npos);

    return 0;
}
