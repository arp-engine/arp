#include "core/memory/cpu_allocator.h"
#include "renderer/deployment/native_renderer_deployment_bridge.h"
#include "renderer/deployment/renderer_backend_deployment.h"
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

arp::rhi::semantics::NativeRendererDeploymentReadinessReport makeVulkanReadiness(
    const arp::renderer::RendererRhiFramePlanBuildResult& frame)
{
    auto contract = arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Vulkan);
    contract.queriedFromNativeDevice = true;
    contract.queriedExtendedFeatures = true;
    return arp::rhi::semantics::evaluateNativeRendererDeploymentReadiness(contract, frame);
}

arp::rhi::semantics::NativeRendererDeploymentReadinessReport makeWebGpuReadiness(
    const arp::renderer::RendererRhiFramePlanBuildResult& frame)
{
    return arp::rhi::semantics::evaluateNativeRendererDeploymentReadiness(
        arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::WebGPU), frame);
}

} // namespace

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_l1;

    const auto frame = buildSceneFrame();
    assert(frame.success);

    const auto vulkanReadiness = makeVulkanReadiness(frame);
    const auto webgpuReadiness = makeWebGpuReadiness(frame);
    assert(vulkanReadiness.status == NativeRendererDeploymentReadinessStatus::BackendValidated);
    assert(webgpuReadiness.status == NativeRendererDeploymentReadinessStatus::ContractComplete);

    RendererBackendSelectionDesc candidates;
    candidates.maxCandidates = 2;
    candidates.candidates = {vulkanReadiness, webgpuReadiness};

    RendererBackendSelectionPolicy webFirst(RendererBackendPreference::WebFirst);
    const auto webSelection = webFirst.select(candidates);
    assert(webSelection.success);
    assert(webSelection.selectedBackend == Backend::WebGPU);
    assert(
        webSelection.selectedStatus == NativeRendererDeploymentReadinessStatus::ContractComplete);
    assert(webSelection.selectedCandidateIndex == 1u);
    assert(webSelection.candidateCount == 2u);
    assert(webSelection.diagnosticCandidateCount == 0u);
    assert(webSelection.telemetryRequired);

    RendererBackendSelectionPolicy nativeFirst(RendererBackendPreference::NativeValidatedFirst);
    const auto nativeSelection = nativeFirst.select(candidates);
    assert(nativeSelection.success);
    assert(nativeSelection.selectedBackend == Backend::Vulkan);
    assert(nativeSelection.selectedStatus
        == NativeRendererDeploymentReadinessStatus::BackendValidated);
    assert(nativeSelection.selectedCandidateIndex == 0u);
    assert(!nativeSelection.telemetryRequired);

    auto failedWebGpu = webgpuReadiness;
    failedWebGpu.success = false;
    failedWebGpu.canStartFrame = false;
    failedWebGpu.status = NativeRendererDeploymentReadinessStatus::DiagnosticFailure;
    failedWebGpu.diagnosticFailureCount = 1;
    failedWebGpu.diagnostics = "webgpu canvas unavailable";

    RendererBackendSelectionDesc fallbackCandidates;
    fallbackCandidates.maxCandidates = 2;
    fallbackCandidates.candidates = {failedWebGpu, vulkanReadiness};
    const auto fallbackSelection = webFirst.select(fallbackCandidates);
    assert(fallbackSelection.success);
    assert(fallbackSelection.selectedBackend == Backend::Vulkan);
    assert(fallbackSelection.diagnosticCandidateCount == 1u);
    assert(fallbackSelection.diagnostics.find("webgpu canvas") != std::pmr::string::npos);
    assert(!fallbackSelection.diagnosticFailureUserVisible);
    assert(!fallbackSelection.diagnosticFailureSilent);
    assert(fallbackSelection.diagnosticFailureReason.empty());

    RendererBackendSelectionDesc failingCandidates;
    failingCandidates.maxCandidates = 1;
    failingCandidates.candidates = {failedWebGpu};
    const auto failingSelection = webFirst.select(failingCandidates);
    assert(!failingSelection.success);
    assert(failingSelection.selectedBackend == Backend::Null);
    assert(failingSelection.diagnosticCandidateCount == 1u);
    assert(failingSelection.diagnostics.find("no renderer backend candidate")
        != std::pmr::string::npos);
    assert(failingSelection.diagnosticFailureUserVisible);
    assert(!failingSelection.diagnosticFailureSilent);
    assert(failingSelection.diagnosticFailureReason.find("RHI diagnostic failure")
        != std::pmr::string::npos);
    assert(failingSelection.diagnosticFailureReason.find("no renderer backend candidate")
        != std::pmr::string::npos);

    RendererBackendSelectionDesc exhaustedCandidates;
    exhaustedCandidates.maxCandidates = 1;
    exhaustedCandidates.candidates = {vulkanReadiness, webgpuReadiness};
    const auto exhaustedSelection = webFirst.select(exhaustedCandidates);
    assert(!exhaustedSelection.success);
    assert(exhaustedSelection.diagnostics.find("candidate capacity") != std::pmr::string::npos);
    assert(exhaustedSelection.diagnosticFailureUserVisible);
    assert(!exhaustedSelection.diagnosticFailureSilent);
    assert(exhaustedSelection.diagnosticFailureReason.find("candidate capacity")
        != std::pmr::string::npos);

    return 0;
}
