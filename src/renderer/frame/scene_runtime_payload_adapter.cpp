#include "renderer/frame/scene_runtime_payload_adapter.h"

#include "renderer/pipeline/scene_runtime_pipeline_payload.h"

#include <utility>

namespace arp::renderer {

namespace {

RenderFramePayloadAdapterResult validationFailure(const arp::core::pmr::String& message)
{
    RenderFramePayloadAdapterResult result;
    result.success = false;
    result.diagnostics.push_back({
        RenderFramePayloadAdapterDiagnosticCode::PayloadValidationFailed,
        message,
    });
    return result;
}

RenderFrameQueueKind mapQueueKind(RendererSceneRuntimeQueueKind kind)
{
    switch (kind) {
    case RendererSceneRuntimeQueueKind::Scene3D:
        return RenderFrameQueueKind::Scene3D;
    case RendererSceneRuntimeQueueKind::Scene2D:
        return RenderFrameQueueKind::Scene2D;
    case RendererSceneRuntimeQueueKind::UI:
        return RenderFrameQueueKind::UI;
    }
    return RenderFrameQueueKind::Scene3D;
}

RenderFrameQueueRequirement makeQueueRequirement(const RendererSceneRuntimeQueueRecord& record)
{
    RenderFrameQueueRequirement requirement;
    requirement.kind = mapQueueKind(record.kind);
    requirement.order = record.order;
    requirement.drawItemCount = record.drawItemCount;
    requirement.materialResourceCount = record.materialResourceCount;
    requirement.textureResourceCount = record.textureResourceCount;
    requirement.compatibleWithOnePass = record.compatibleWithOnePass;
    return requirement;
}

} // namespace

RenderFramePayloadAdapterKey SceneRuntimePayloadAdapter::key() const
{
    RenderFramePayloadAdapterKey adapterKey;
    adapterKey.kind = CustomWorkloadPayloadKind::SceneDrawPlan;
    adapterKey.contractName = kSceneRuntimePipelinePayloadContract;
    adapterKey.contractVersion = kSceneRuntimePipelinePayloadVersion;
    return adapterKey;
}

RenderFramePayloadAdapterResult SceneRuntimePayloadAdapter::adapt(
    const CustomWorkloadPayloadRef& payload) const
{
    const auto payloadValidation = validateSceneRuntimePipelinePayload(payload);
    if (!payloadValidation.success) {
        return validationFailure(payloadValidation.diagnostics);
    }

    const auto* scenePayload = customPayloadObjectAs<SceneRuntimePipelinePayload>(payload,
        CustomWorkloadPayloadKind::SceneDrawPlan,
        kSceneRuntimePipelinePayloadContract,
        kSceneRuntimePipelinePayloadVersion);
    if (scenePayload == nullptr) {
        return validationFailure(
            "SceneRuntimePayloadAdapter requires a typed scene runtime payload.");
    }
    const auto& rendererFrame = *scenePayload->rendererFrame;

    RenderFrameIntent intent;
    intent.contractName = kSceneRuntimePipelinePayloadContract;
    intent.contractVersion = kSceneRuntimePipelinePayloadVersion;
    intent.requiresMultiplePhysicalPasses = scenePayload->requiresMultiplePhysicalPasses;
    intent.hasExplicitAttachmentContract = scenePayload->hasExplicitAttachmentContract;
    intent.hasExplicitPostprocessContract = scenePayload->hasExplicitPostprocessContract;
    intent.hasExplicitOffscreenContract = scenePayload->hasExplicitOffscreenContract;

    RenderFrameSceneRequirement sceneRequirement;
    sceneRequirement.id = "SceneRuntime";
    sceneRequirement.resolvedCameraCount = rendererFrame.resolvedCameraCount();
    sceneRequirement.drawItemCount = rendererFrame.drawItemCount();
    sceneRequirement.materialResourceCount = rendererFrame.materialResourceCount();
    sceneRequirement.textureResourceCount = rendererFrame.textureResourceCount();
    sceneRequirement.traversalSnapshot = scenePayload->traversalSnapshot;
    sceneRequirement.rendererFrame = scenePayload->rendererFrame;
    sceneRequirement.compatibleOnePassIntent = rendererFrame.compatibleOnePassIntent();
    intent.sceneRequirements.push_back(sceneRequirement);

    intent.queueRequirements.reserve(rendererFrame.queueRecords().size());
    for (const auto& record : rendererFrame.queueRecords()) {
        intent.queueRequirements.push_back(makeQueueRequirement(record));
    }

    RenderFramePayloadAdapterResult result;
    result.success = true;
    result.intent = std::move(intent);
    return result;
}

} // namespace arp::renderer
