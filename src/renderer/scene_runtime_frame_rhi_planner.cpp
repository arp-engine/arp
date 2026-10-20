#include "renderer/scene_runtime_frame_rhi_planner.h"

#include <utility>

namespace arp::renderer {

namespace {

RendererSceneRuntimeFrameRhiPlanResult failure(
    RendererSceneRuntimeRhiDiagnosticCode code, const char* message)
{
    return RendererSceneRuntimeFrameRhiPlanResult::failure(code, message);
}

bool hasExplicitMultiPassContract(const RendererSceneRuntimeFrameRhiPlanRequest& request)
{
    return request.hasExplicitAttachmentContract || request.hasExplicitPostprocessContract
        || request.hasExplicitOffscreenContract;
}

std::uint32_t queueDrawCount(
    const RendererSceneRuntimeFrame& frame, RendererSceneRuntimeQueueKind kind)
{
    for (const auto& record : frame.queueRecords()) {
        if (record.kind == kind) {
            return static_cast<std::uint32_t>(record.drawItemCount);
        }
    }
    return 0;
}

rhi::RendererInputContract makeInputContract(const RendererSceneRuntimeFrame& frame)
{
    rhi::RendererInputContract input;
    input.kind = rhi::RendererInputKind::ScenePass;
    input.label = "SceneRuntimeWorldUiInput";
    input.drawCommandCount = static_cast<std::uint32_t>(frame.drawItemCount());
    input.meshCount = queueDrawCount(frame, RendererSceneRuntimeQueueKind::Scene3D);
    input.materialBindingCount = static_cast<std::uint32_t>(frame.materialResourceCount());
    input.sampledTextureCount = static_cast<std::uint32_t>(frame.textureResourceCount());
    input.descriptorBacked = true;
    input.pipelineLayoutKnown = true;
    input.explicitResourceStates = true;
    input.backendOwnedStaging = true;
    return input;
}

RendererRhiDrawPlanDesc makeDrawPlan(
    const RendererSceneRuntimeFrame& frame, const RendererSceneRuntimeFrameRhiPlannerDesc& desc)
{
    RendererRhiDrawPlanDesc drawPlan;
    drawPlan.name = "SceneRuntimeWorldUiPass";
    drawPlan.workKind = RendererRhiWorkKind::Scene;
    drawPlan.frameSlot = desc.frameSlot;
    drawPlan.drawCommandCount = static_cast<std::uint32_t>(frame.drawItemCount());
    drawPlan.pushConstantSizeBytes = desc.pushConstantSizeBytes;
    drawPlan.pipelinePushConstantCapacityBytes = desc.pipelinePushConstantCapacityBytes;
    drawPlan.pipelineLayoutName = "SceneRuntimeWorldUiPipelineLayout";
    drawPlan.expectedPipelineLayoutName = "SceneRuntimeWorldUiPipelineLayout";
    drawPlan.descriptorLayoutNameCount = desc.descriptorLayoutNameCount;
    if (drawPlan.descriptorLayoutNameCount > 0) {
        drawPlan.descriptorLayoutNames[0] = "SceneRuntimeFrameSet";
    }
    if (drawPlan.descriptorLayoutNameCount > 1) {
        drawPlan.descriptorLayoutNames[1] = "SceneRuntimeMaterialSet";
    }
    drawPlan.descriptorSetCount = desc.descriptorSetCount;
    drawPlan.descriptorTableCount = frame.materialResourceCount() > 0 ? 1u : 0u;
    drawPlan.requiredMaterialResourceCount =
        static_cast<std::uint32_t>(frame.materialResourceCount());
    drawPlan.materialResourceCount = static_cast<std::uint32_t>(frame.materialResourceCount());
    drawPlan.allowSoftSecondaryCommands = frame.drawItemCount() > 1;
    drawPlan.usesTransientTargets = false;
    drawPlan.hasInputContract = true;
    drawPlan.inputContract = makeInputContract(frame);
    return drawPlan;
}

} // namespace

RendererSceneRuntimeFrameRhiPlanResult RendererSceneRuntimeFrameRhiPlanResult::success(
    RendererRhiFramePlanDesc framePlan)
{
    RendererSceneRuntimeFrameRhiPlanResult result(
        {RendererSceneRuntimeRhiDiagnosticCode::None, {}}, framePlan);
    result.statusLabel_ = RendererSceneRuntimeRhiStatusLabel::ContractComplete;
    result.backendSpecificBranchingRequired_ = false;
    result.nativeHandleExposedToUpperLayer_ = false;
    return result;
}

RendererSceneRuntimeFrameRhiPlanResult RendererSceneRuntimeFrameRhiPlanResult::failure(
    RendererSceneRuntimeRhiDiagnosticCode code, arp::core::pmr::String message)
{
    RendererRhiFramePlanDesc framePlan;
    RendererSceneRuntimeFrameRhiPlanResult result({code, message}, framePlan);
    result.statusLabel_ = RendererSceneRuntimeRhiStatusLabel::DiagnosticFailure;
    result.backendSpecificBranchingRequired_ = false;
    result.nativeHandleExposedToUpperLayer_ = false;
    return result;
}

RendererSceneRuntimeFrameRhiPlanResult::RendererSceneRuntimeFrameRhiPlanResult(
    RendererSceneRuntimeRhiDiagnostic diagnostic, RendererRhiFramePlanDesc framePlan)
    : diagnostic_(diagnostic), framePlan_(framePlan)
{}

RendererSceneRuntimeFrameRhiPlanner::RendererSceneRuntimeFrameRhiPlanner(
    RendererSceneRuntimeFrameRhiPlannerDesc desc)
    : desc_(desc)
{}

RendererSceneRuntimeFrameRhiPlanResult RendererSceneRuntimeFrameRhiPlanner::plan(
    const RendererSceneRuntimeFrame& frame) const
{
    RendererSceneRuntimeFrameRhiPlanRequest request;
    request.frame = &frame;
    return plan(request);
}

RendererSceneRuntimeFrameRhiPlanResult RendererSceneRuntimeFrameRhiPlanner::plan(
    const RendererSceneRuntimeFrameRhiPlanRequest& request) const
{
    if (request.frame == nullptr) {
        return failure(RendererSceneRuntimeRhiDiagnosticCode::MissingFrame,
            "scene runtime RHI planner requires a renderer frame");
    }

    const auto& frame = *request.frame;
    if (frame.statusLabel() != RendererSceneRuntimeStatusLabel::ContractComplete) {
        return failure(RendererSceneRuntimeRhiDiagnosticCode::InvalidFrameStatus,
            "scene runtime RHI planner requires a contract-complete renderer frame");
    }
    if (!frame.compatibleOnePassIntent()) {
        return failure(RendererSceneRuntimeRhiDiagnosticCode::IncompatibleOnePassIntent,
            "scene runtime RHI planner requires compatible one-pass intent");
    }
    if (request.requiresMultiplePhysicalPasses && !hasExplicitMultiPassContract(request)) {
        return failure(RendererSceneRuntimeRhiDiagnosticCode::UnsupportedMultiPassRequirement,
            "scene runtime RHI planner rejected multi-pass requirements without explicit metadata");
    }
    if (frame.exposesBackendHandles()) {
        return failure(RendererSceneRuntimeRhiDiagnosticCode::BackendHandleExposure,
            "scene runtime RHI planner rejected backend handle exposure");
    }
    if (frame.copiesStaticGeometryPayloadInHotPath()) {
        return failure(RendererSceneRuntimeRhiDiagnosticCode::StaticGeometryPayloadCopy,
            "scene runtime RHI planner rejected static geometry payload copies");
    }
    if (desc_.descriptorSetCount > desc_.descriptorLayoutNameCount) {
        return failure(RendererSceneRuntimeRhiDiagnosticCode::DescriptorBudgetExceeded,
            "scene runtime RHI planner descriptor set count exceeds layout names");
    }
    if (frame.drawItemCount() > desc_.maxDrawCommandCount) {
        return failure(RendererSceneRuntimeRhiDiagnosticCode::DrawItemBudgetExceeded,
            "scene runtime RHI planner draw command budget exceeded");
    }
    if (frame.materialResourceCount() > desc_.maxMaterialResourceCount) {
        return failure(RendererSceneRuntimeRhiDiagnosticCode::MaterialResourceBudgetExceeded,
            "scene runtime RHI planner material resource budget exceeded");
    }
    if (desc_.pushConstantSizeBytes > desc_.pipelinePushConstantCapacityBytes) {
        return failure(RendererSceneRuntimeRhiDiagnosticCode::PushConstantCapacityExceeded,
            "scene runtime RHI planner push constants exceed pipeline capacity");
    }

    RendererRhiFramePlanDesc framePlan;
    framePlan.frameSlot = desc_.frameSlot;
    framePlan.drawPlans.push_back(makeDrawPlan(frame, desc_));
    return RendererSceneRuntimeFrameRhiPlanResult::success(framePlan);
}

RendererSceneRuntimeFrameRhiPlanResult RendererSceneRuntimeFrameRhiPlanner::plan(
    const RendererSceneRuntimeFrameRhiPlanRequest& request,
    const SceneRuntimeIndexedDrawMetadata& metadata) const
{
    auto planned = plan(request);
    if (!planned.ok())
        return planned;
    if (metadata.contractVersion != SceneRuntimeIndexedDrawMetadata::currentVersion)
        return failure(RendererSceneRuntimeRhiDiagnosticCode::DrawMetadataVersion,
            "RHI-SCENE-DRAW-METADATA-VERSION");
    if (metadata.commands.empty() && request.frame->drawItemCount() != 0u)
        return failure(RendererSceneRuntimeRhiDiagnosticCode::DrawMetadataMissing,
            "RHI-SCENE-DRAW-METADATA-MISSING");
    if (!metadata.commands.empty() && metadata.commands.data() == nullptr)
        return failure(RendererSceneRuntimeRhiDiagnosticCode::DrawMetadataStorage,
            "RHI-SCENE-DRAW-METADATA-STORAGE");
    if (metadata.commands.size() != request.frame->drawItemCount())
        return failure(RendererSceneRuntimeRhiDiagnosticCode::DrawMetadataCount,
            "RHI-SCENE-DRAW-METADATA-COUNT");

    auto framePlan = planned.framePlan();
    auto& commands = framePlan.drawPlans.front().drawCommands;
    if (!metadata.commands.empty())
        commands.assign(metadata.commands.begin(), metadata.commands.end());
    return RendererSceneRuntimeFrameRhiPlanResult::success(std::move(framePlan));
}

} // namespace arp::renderer
