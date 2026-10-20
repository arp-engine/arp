#include "renderer/frame/scene_runtime_world_ui_pass_provider.h"

#include "renderer/pipeline/scene_runtime_pipeline_payload.h"

namespace arp::renderer {

namespace {

RenderPassProviderResult failure(RenderPassProviderDiagnosticCode code, const char* message)
{
    RenderPassProviderResult result;
    result.success = false;
    result.diagnostics.push_back({code, message});
    return result;
}

const RenderFrameQueueRequirement* findQueue(
    const RenderFrameIntent& intent, RenderFrameQueueKind kind)
{
    for (const auto& queue : intent.queueRequirements) {
        if (queue.kind == kind) {
            return &queue;
        }
    }
    return nullptr;
}

bool allQueuesCompatibleWithOnePass(const RenderFrameIntent& intent)
{
    for (const auto& queue : intent.queueRequirements) {
        if (!queue.compatibleWithOnePass) {
            return false;
        }
    }
    return true;
}

bool hasCompatibleSceneRuntimeIntent(const RenderFrameIntent& intent)
{
    return !intent.sceneRequirements.empty() && intent.sceneRequirements[0].compatibleOnePassIntent
        && !intent.queueRequirements.empty() && allQueuesCompatibleWithOnePass(intent);
}

bool hasExplicitMultiPassMetadata(const RenderFrameIntent& intent)
{
    return intent.hasExplicitAttachmentContract || intent.hasExplicitPostprocessContract
        || intent.hasExplicitOffscreenContract;
}

RenderPassDeclaration makeWorldUiPass()
{
    RenderPassDeclaration pass;
    pass.id = kSceneRuntimeWorldUiPassProviderPassId;
    pass.stage = RenderFrameStage::WorldOpaque;
    pass.declaredOrder = 0u;
    pass.providerId = kSceneRuntimeWorldUiPassProviderId;
    pass.capabilityLabel = kSceneRuntimeWorldUiPassProviderCapability;

    RenderFrameAttachmentUse backbufferWrite;
    backbufferWrite.attachmentId = "Backbuffer";
    backbufferWrite.state = rhi::ResourceState::ColorAttachment;
    backbufferWrite.access = RenderFrameAttachmentAccess::Write;
    pass.attachmentUses.push_back(backbufferWrite);
    return pass;
}

void appendQueueSemantic(
    RenderPassProviderSemanticRecord& semantic, const RenderFrameQueueRequirement& queue)
{
    semantic.queues.push_back({
        queue.kind,
        queue.order,
        queue.drawItemCount,
    });
}

RenderPassProviderSemanticRecord makeSemanticRecord(const RenderFrameIntent& intent)
{
    RenderPassProviderSemanticRecord semantic;
    semantic.passName = kSceneRuntimeWorldUiPassProviderPassId;
    semantic.physicalPassCount = 1u;

    const RenderFrameQueueKind orderedKinds[] = {
        RenderFrameQueueKind::Scene3D,
        RenderFrameQueueKind::Scene2D,
        RenderFrameQueueKind::UI,
    };
    for (const auto kind : orderedKinds) {
        const auto* queue = findQueue(intent, kind);
        if (queue != nullptr) {
            appendQueueSemantic(semantic, *queue);
        }
    }

    return semantic;
}

} // namespace

const char* SceneRuntimeWorldUiPassProvider::providerId() const
{
    return kSceneRuntimeWorldUiPassProviderId;
}

RenderFrameStage SceneRuntimeWorldUiPassProvider::stage() const
{
    return RenderFrameStage::WorldOpaque;
}

const char* SceneRuntimeWorldUiPassProvider::capabilityLabel() const
{
    return kSceneRuntimeWorldUiPassProviderCapability;
}

bool SceneRuntimeWorldUiPassProvider::supports(const RenderFrameIntent& intent) const
{
    return intent.contractName == kSceneRuntimePipelinePayloadContract
        && intent.contractVersion == kSceneRuntimePipelinePayloadVersion;
}

RenderPassProviderResult SceneRuntimeWorldUiPassProvider::provide(
    const RenderFrameIntent& intent) const
{
    if (intent.requiresMultiplePhysicalPasses && !hasExplicitMultiPassMetadata(intent)) {
        return failure(RenderPassProviderDiagnosticCode::MultiPassMetadataMissing,
            "SceneRuntimeWorldUiPassProvider rejected multi-pass intent without explicit attachment, postprocess, or offscreen metadata.");
    }
    if (!hasCompatibleSceneRuntimeIntent(intent)) {
        return failure(RenderPassProviderDiagnosticCode::IncompatibleSceneRuntimeIntent,
            "SceneRuntimeWorldUiPassProvider requires one-pass compatible 3D, 2D, and UI queues.");
    }

    RenderPassProviderResult result;
    result.success = true;
    result.passes.push_back(makeWorldUiPass());
    result.semanticRecords.push_back(makeSemanticRecord(intent));
    return result;
}

} // namespace arp::renderer
