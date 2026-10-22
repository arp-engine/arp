#include "core/memory/cpu_allocator.h"
#include "renderer/deployment/native_renderer_deployment_bridge.h"
#include "renderer/rhi_draw_plan_bridge.h"

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

} // namespace

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_l1;

    const auto sceneFrame = buildSceneFrame();
    assert(sceneFrame.success);

    auto queriedVulkan = nativeBackendCapabilityContract(Backend::Vulkan);
    queriedVulkan.queriedFromNativeDevice = true;
    queriedVulkan.queriedExtendedFeatures = true;

    NativeRendererDeploymentBridge vulkanBridge(queriedVulkan);
    const auto vulkanReadiness = vulkanBridge.evaluate(sceneFrame);
    assert(vulkanReadiness.success);
    assert(vulkanReadiness.status == NativeRendererDeploymentReadinessStatus::BackendValidated);
    assert(vulkanReadiness.contractComplete);
    assert(vulkanReadiness.backendValidated);
    assert(vulkanReadiness.canStartFrame);
    assert(vulkanReadiness.backend == Backend::Vulkan);
    assert(vulkanReadiness.nativeFeatureCount == 4u);
    assert(vulkanReadiness.diagnosticFailureCount == 0u);
    assert(!vulkanReadiness.telemetryRequired);
    assert(vulkanReadiness.backendFallbacksHiddenFromUpperLayer);
    assert(!vulkanReadiness.nativeHandleExposedToUpperLayer);

    const auto webgpuReadiness = evaluateNativeRendererDeploymentReadiness(
        nativeBackendCapabilityContract(Backend::WebGPU), sceneFrame);
    assert(webgpuReadiness.success);
    assert(webgpuReadiness.status == NativeRendererDeploymentReadinessStatus::ContractComplete);
    assert(webgpuReadiness.contractComplete);
    assert(!webgpuReadiness.backendValidated);
    assert(webgpuReadiness.canStartFrame);
    assert(webgpuReadiness.backend == Backend::WebGPU);
    assert(webgpuReadiness.emulatedFeatureCount == 1u);
    assert(webgpuReadiness.cpuReplayFeatureCount == 1u);
    assert(webgpuReadiness.degradedFeatureCount == 2u);
    assert(webgpuReadiness.telemetryRequired);
    assert(webgpuReadiness.backendFallbacksHiddenFromUpperLayer);

    auto flattenedVulkan = nativeBackendCapabilityContract(Backend::Vulkan);
    flattenedVulkan.preservesNativePotential = false;
    flattenedVulkan.flattenedToPortableSubset = true;
    const auto flattenedReadiness =
        evaluateNativeRendererDeploymentReadiness(flattenedVulkan, sceneFrame);
    assert(!flattenedReadiness.success);
    assert(flattenedReadiness.status == NativeRendererDeploymentReadinessStatus::DiagnosticFailure);
    assert(!flattenedReadiness.canStartFrame);
    assert(flattenedReadiness.diagnosticFailureCount >= 1u);
    assert(flattenedReadiness.diagnostics.find("native-first") != std::pmr::string::npos);

    arp::renderer::RendererRhiFramePlanBuildResult failedRendererFrame;
    failedRendererFrame.success = false;
    failedRendererFrame.diagnostics = "renderer RHI frame plan draw plan capacity exhausted";
    const auto failedReadiness = vulkanBridge.evaluate(failedRendererFrame);
    assert(!failedReadiness.success);
    assert(failedReadiness.status == NativeRendererDeploymentReadinessStatus::DiagnosticFailure);
    assert(!failedReadiness.canStartFrame);
    assert(failedReadiness.telemetryRequired);
    assert(failedReadiness.diagnostics.find("draw plan capacity") != std::pmr::string::npos);

    auto leakedRendererFrame = sceneFrame;
    leakedRendererFrame.nativeHandleExposedToUpperLayer = true;
    const auto leakedReadiness = vulkanBridge.evaluate(leakedRendererFrame);
    assert(!leakedReadiness.success);
    assert(leakedReadiness.status == NativeRendererDeploymentReadinessStatus::DiagnosticFailure);
    assert(leakedReadiness.nativeHandleExposedToUpperLayer);
    assert(!leakedReadiness.backendFallbacksHiddenFromUpperLayer);
    assert(leakedReadiness.diagnostics.find("native handle") != std::pmr::string::npos);

    return 0;
}
