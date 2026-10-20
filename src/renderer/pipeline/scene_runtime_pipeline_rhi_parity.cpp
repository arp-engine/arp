#include "renderer/pipeline/scene_runtime_pipeline_rhi_parity.h"

#include <cstring>
#include <utility>

namespace arp::renderer {

namespace {

SceneRuntimePipelineRhiParityResultV2 fail(
    SceneRuntimePipelineRhiParityResultV2 result, arp::core::pmr::String diagnostic)
{
    result.success = false;
    result.diagnostics = std::move(diagnostic);
    return result;
}

bool hasExpectedPassName(const SceneRuntimeRenderPipelineSemanticRecord& semantic)
{
    return semantic.passName != nullptr
        && std::strcmp(semantic.passName, kSceneRuntimeWorldUiPassName) == 0;
}

bool hasDescriptorLayout(const RendererRhiDrawPlanDesc& drawPlan, const char* name)
{
    for (std::uint32_t index = 0; index < drawPlan.descriptorLayoutNameCount; ++index) {
        if (drawPlan.descriptorLayoutNames[index] == arp::core::pmr::String(name)) {
            return true;
        }
    }
    return false;
}

bool drawPlanMatchesSceneRuntimeContract(
    const RendererRhiDrawPlanDesc& drawPlan, const RendererSceneRuntimeFrame& frame)
{
    return drawPlan.name == arp::core::pmr::String(kSceneRuntimeWorldUiPassName)
        && drawPlan.workKind == RendererRhiWorkKind::Scene
        && drawPlan.drawCommandCount == frame.drawItemCount() && drawPlan.descriptorSetCount > 0u
        && drawPlan.descriptorLayoutNameCount >= drawPlan.descriptorSetCount
        && hasDescriptorLayout(drawPlan, "SceneRuntimeFrameSet")
        && hasDescriptorLayout(drawPlan, "SceneRuntimeMaterialSet")
        && drawPlan.requiredMaterialResourceCount == frame.materialResourceCount()
        && drawPlan.materialResourceCount == frame.materialResourceCount()
        && drawPlan.pushConstantSizeBytes <= drawPlan.pipelinePushConstantCapacityBytes
        && drawPlan.hasInputContract
        && drawPlan.inputContract.kind == rhi::RendererInputKind::ScenePass
        && drawPlan.inputContract.drawCommandCount == frame.drawItemCount()
        && drawPlan.inputContract.materialBindingCount == frame.materialResourceCount()
        && drawPlan.inputContract.sampledTextureCount == frame.textureResourceCount()
        && drawPlan.inputContract.descriptorBacked && drawPlan.inputContract.pipelineLayoutKnown
        && drawPlan.inputContract.explicitResourceStates
        && drawPlan.inputContract.backendOwnedStaging;
}

rhi::semantics::SceneRuntimeBackendWorkload backendWorkloadFrom(
    const RendererRhiDrawPlanDesc& drawPlan, const RendererSceneRuntimeFrame& frame)
{
    return {
        true,
        drawPlan.drawCommandCount,
        drawPlan.materialResourceCount,
        drawPlan.descriptorSetCount,
        drawPlan.pushConstantSizeBytes,
        drawPlan.pipelinePushConstantCapacityBytes,
        frame.compatibleOnePassIntent(),
        frame.exposesBackendHandles(),
    };
}

SceneRuntimePipelineBackendSemanticReport backendReportFor(rhi::Backend backend,
    const RendererRhiDrawPlanDesc& drawPlan,
    const RendererSceneRuntimeFrame& frame)
{
    SceneRuntimePipelineBackendSemanticReport report;
    report.backend = backend;

    const auto admission = rhi::semantics::SemanticReportBackendAdmission::evaluate(backend);
    if (!admission.inspectStructuralContract) {
        report.supportLabel = rhi::semantics::SceneRuntimeBackendSupportLabel::DiagnosticFailure;
        report.diagnostics = admission.diagnostic;
        return report;
    }

    const auto workload = backendWorkloadFrom(drawPlan, frame);
    const bool commonSupported = workload.requested && workload.drawCommandCount > 0u
        && workload.materialResourceCount > 0u && workload.descriptorSetCount > 0u
        && workload.pushConstantSizeBytes <= workload.pushConstantCapacityBytes
        && workload.oneCompatibleWorldUiPass && !workload.backendHandleExposedToUpperLayer;

    if (!commonSupported) {
        report.supportLabel = rhi::semantics::SceneRuntimeBackendSupportLabel::DiagnosticFailure;
        report.diagnostics =
            "scene-runtime backend semantic report requires draw, descriptor, push-constant, one-pass, and no-native-handle contracts";
        return report;
    }

    // Consume the existing queried/specification lowering contract. Null only
    // checks structure; unsupported native decisions must fail closed too.
    if (drawPlan.pushConstantSizeBytes > 0u && !admission.validatorOnly) {
        const auto contract = rhi::semantics::nativeBackendCapabilityContract(backend);
        const auto pcDecision =
            rhi::semantics::chooseFeatureFallback(contract, rhi::EngineFeature::PushConstants);
        const auto lowering = rhi::semantics::buildPushConstantLoweringPlan(backend, contract);
        if (pcDecision.mode != rhi::FallbackMode::Native || !lowering.hasExecutablePayload) {
            report.supportLabel =
                rhi::semantics::SceneRuntimeBackendSupportLabel::DiagnosticFailure;
            report.diagnostics =
                "RHI-SCENE-PUSH-LOWERING-UNAVAILABLE: scene-runtime structural report "
                "requires an available executable push-constant lowering contract";
            return report;
        }
    }

    report.supportLabel = rhi::semantics::SceneRuntimeBackendSupportLabel::Supported;
    return report;
}

} // namespace

RendererSceneRuntimeFrameRhiPlanRequest sceneRuntimeRhiPlanRequestFromPayload(
    const SceneRuntimePipelinePayload& payload)
{
    RendererSceneRuntimeFrameRhiPlanRequest request;
    request.frame = payload.rendererFrame;
    request.requiresMultiplePhysicalPasses = payload.requiresMultiplePhysicalPasses;
    request.hasExplicitAttachmentContract = payload.hasExplicitAttachmentContract;
    request.hasExplicitPostprocessContract = payload.hasExplicitPostprocessContract;
    request.hasExplicitOffscreenContract = payload.hasExplicitOffscreenContract;
    return request;
}

SceneRuntimePipelineRhiParityResultV2 verifySceneRuntimePipelineRhiParityV2(
    const SceneRuntimePipelinePayload& payload,
    const SceneRuntimeRenderPipelineSemanticRecord& semantic,
    const RendererSceneRuntimeFrameRhiPlannerDesc& plannerDesc,
    const RendererRhiDrawPlanBridgeDesc& bridgeDesc)
{
    return verifySceneRuntimePipelineRhiParityV2(payload, semantic, plannerDesc, bridgeDesc, {});
}

SceneRuntimePipelineRhiParityResultV2 verifySceneRuntimePipelineRhiParityV2(
    const SceneRuntimePipelinePayload& payload,
    const SceneRuntimeRenderPipelineSemanticRecord& semantic,
    const RendererSceneRuntimeFrameRhiPlannerDesc& plannerDesc,
    const RendererRhiDrawPlanBridgeDesc& bridgeDesc,
    const SceneRuntimeIndexedDrawMetadata& metadata)
{
    SceneRuntimePipelineRhiParityResultV2 result;
    const auto payloadValidation = validateSceneRuntimePipelinePayload(payload);
    if (!payloadValidation.success) {
        return fail(std::move(result), payloadValidation.diagnostics);
    }

    result.planRequest = sceneRuntimeRhiPlanRequestFromPayload(payload);
    if (semantic.physicalPassCount != 1u || !hasExpectedPassName(semantic)) {
        return fail(std::move(result),
            "scene-runtime custom pipeline semantic record must contain one SceneRuntimeWorldUiPass");
    }

    RendererSceneRuntimeFrameRhiPlanner planner(plannerDesc);
    const auto planned = planner.plan(result.planRequest, metadata);
    if (!planned.ok()) {
        return fail(std::move(result), planned.diagnostic().message);
    }
    result.framePlan = planned.framePlan();

    if (result.framePlan.drawPlans.size() != 1u) {
        return fail(std::move(result), "scene-runtime RHI parity requires one draw plan");
    }

    const auto& drawPlan = result.framePlan.drawPlans.front();
    if (!drawPlanMatchesSceneRuntimeContract(drawPlan, *payload.rendererFrame)) {
        return fail(std::move(result),
            "scene-runtime custom pipeline pass does not match the RHI planner draw-plan contract");
    }

    result.bridgedFramePlan =
        RendererRhiFramePlanBridge(bridgeDesc).buildFramePlan(result.framePlan);
    if (!result.bridgedFramePlan.success) {
        return fail(std::move(result), result.bridgedFramePlan.diagnostics);
    }

    for (const auto backend : rhi::semantics::SemanticReportBackendAdmission::backends) {
        result.backendReports[result.backendReportCount] =
            backendReportFor(backend, drawPlan, *payload.rendererFrame);
        ++result.backendReportCount;
    }

    result.success = true;
    return result;
}

SceneRuntimePipelineRhiParityResult verifySceneRuntimePipelineRhiParity(
    const SceneRuntimePipelinePayload& payload,
    const SceneRuntimeRenderPipelineSemanticRecord& semantic,
    const RendererSceneRuntimeFrameRhiPlannerDesc& plannerDesc,
    const RendererRhiDrawPlanBridgeDesc& bridgeDesc)
{
    return verifySceneRuntimePipelineRhiParity(payload, semantic, plannerDesc, bridgeDesc, {});
}

SceneRuntimePipelineRhiParityResult verifySceneRuntimePipelineRhiParity(
    const SceneRuntimePipelinePayload& payload,
    const SceneRuntimeRenderPipelineSemanticRecord& semantic,
    const RendererSceneRuntimeFrameRhiPlannerDesc& plannerDesc,
    const RendererRhiDrawPlanBridgeDesc& bridgeDesc,
    const SceneRuntimeIndexedDrawMetadata& metadata)
{
    auto current =
        verifySceneRuntimePipelineRhiParityV2(payload, semantic, plannerDesc, bridgeDesc, metadata);
    SceneRuntimePipelineRhiParityResult legacy;
    legacy.success = current.success;
    legacy.diagnostics = std::move(current.diagnostics);
    legacy.planRequest = current.planRequest;
    legacy.framePlan = std::move(current.framePlan);
    legacy.bridgedFramePlan = std::move(current.bridgedFramePlan);
    // Legacy/1 explicitly exposes the original four entries, in order.
    static_assert(kSceneRuntimePipelineBackendReportCount == 4u);
    static_assert(kSceneRuntimePipelineBackendReportCountV2 == 5u);
    for (std::size_t index = 0;
        index < current.backendReportCount && index < legacy.backendReports.size();
        ++index) {
        legacy.backendReports[index] = std::move(current.backendReports[index]);
        ++legacy.backendReportCount;
    }
    return legacy;
}

} // namespace arp::renderer
