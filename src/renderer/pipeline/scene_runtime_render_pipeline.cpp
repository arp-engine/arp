#include "renderer/pipeline/scene_runtime_render_pipeline.h"

#include "renderer/pipeline/scene_runtime_pipeline_payload.h"

#include <utility>

namespace arp::renderer {

namespace {

bool validExtent(rhi::Extent2D extent)
{
    return extent.width > 0u && extent.height > 0u;
}

bool hasExplicitMultiPassContract(const SceneRuntimePipelinePayload& payload)
{
    return payload.hasExplicitAttachmentContract || payload.hasExplicitPostprocessContract
        || payload.hasExplicitOffscreenContract;
}

CustomWorkloadPayloadRef payloadRefFrom(const RenderFrameRequest& request)
{
    return {
        CustomWorkloadPayloadKind::SceneDrawPlan,
        request.pipelinePayload,
        request.pipelinePayloadContractName,
        request.pipelinePayloadContractVersion,
    };
}

const RendererSceneRuntimeQueueRecord* findQueue(
    const RendererSceneRuntimeFrame& frame, RendererSceneRuntimeQueueKind kind)
{
    for (const auto& queue : frame.queueRecords()) {
        if (queue.kind == kind) {
            return &queue;
        }
    }
    return nullptr;
}

bool appendQueueSemantic(SceneRuntimeRenderPipelineSemanticRecord& semantic,
    const RendererSceneRuntimeQueueRecord& queue)
{
    if (semantic.queueCount >= semantic.queues.size()) {
        return false;
    }
    semantic.queues[semantic.queueCount] = {
        queue.kind,
        queue.order,
        queue.drawItemCount,
    };
    ++semantic.queueCount;
    return true;
}

bool allQueuesCompatibleWithOnePass(const RendererSceneRuntimeFrame& frame)
{
    for (const auto& queue : frame.queueRecords()) {
        if (!queue.compatibleWithOnePass) {
            return false;
        }
    }
    return true;
}

RendererResult fail(const char* diagnostic)
{
    return {false, diagnostic};
}

} // namespace

SceneRuntimeRenderPipeline::SceneRuntimeRenderPipeline()
{
    desc_.name = "SceneRuntimeRenderPipeline";
    desc_.usesDepth = false;
}

const RenderPipelineDesc& SceneRuntimeRenderPipeline::desc() const
{
    return desc_;
}

RendererResult SceneRuntimeRenderPipeline::create(const RenderPipelineCreateContext& context)
{
    if (context.colorFormat == rhi::TextureFormat::Unknown) {
        return fail("SceneRuntimeRenderPipeline requires a known color format.");
    }
    if (!validExtent(context.extent)) {
        return fail("SceneRuntimeRenderPipeline requires a valid extent.");
    }

    device_ = &context.device;
    desc_.colorFormat = context.colorFormat;
    desc_.depthFormat = context.depthFormat;
    desc_.extent = context.extent;
    return {true, {}};
}

RendererResult SceneRuntimeRenderPipeline::resize(rhi::Extent2D extent)
{
    if (!validExtent(extent)) {
        return fail("SceneRuntimeRenderPipeline requires a valid extent.");
    }
    desc_.extent = extent;
    return {true, {}};
}

RendererResult SceneRuntimeRenderPipeline::render(RenderPipelineContext&)
{
    return fail("SceneRuntimeRenderPipeline uses composeGraph; direct render is unsupported.");
}

RendererResult SceneRuntimeRenderPipeline::composeGraph(render_graph::FrameGraph& graph,
    render_graph::TextureHandle backbuffer,
    const RenderPipelineGraphCompositionContext& context,
    RenderPipelineFrameFeatures frameFeatures)
{
    if (device_ == nullptr) {
        return fail("SceneRuntimeRenderPipeline is not created.");
    }
    if (!context.sceneEnabled) {
        return fail("SceneRuntimeRenderPipeline requires scene runtime payloads to be enabled.");
    }
    if (frameFeatures.particlesWithColor || frameFeatures.gui) {
        return fail(
            "SceneRuntimeRenderPipeline consumes scene, particle, and UI queues through the scene runtime payload.");
    }

    const auto validation = validateSceneRuntimePipelinePayload(payloadRefFrom(context.request));
    if (!validation.success) {
        return {false, validation.diagnostics};
    }

    const auto* payload =
        static_cast<const SceneRuntimePipelinePayload*>(context.request.pipelinePayload);
    if (payload->requiresMultiplePhysicalPasses && !hasExplicitMultiPassContract(*payload)) {
        return fail(
            "SceneRuntimeRenderPipeline rejected multi-pass requirements without explicit attachment, postprocess, or offscreen metadata.");
    }
    if (!payload->rendererFrame->compatibleOnePassIntent()
        || !allQueuesCompatibleWithOnePass(*payload->rendererFrame)) {
        return fail(
            "SceneRuntimeRenderPipeline requires one-pass compatible 3D, 2D, and UI queues.");
    }

    SceneRuntimeRenderPipelineSemanticRecord semantic;
    semantic.passName = kSceneRuntimeWorldUiPassName;
    semantic.physicalPassCount = 1u;
    const RendererSceneRuntimeQueueKind orderedKinds[] = {
        RendererSceneRuntimeQueueKind::Scene3D,
        RendererSceneRuntimeQueueKind::Scene2D,
        RendererSceneRuntimeQueueKind::UI,
    };
    for (const auto kind : orderedKinds) {
        const auto* queue = findQueue(*payload->rendererFrame, kind);
        if (queue != nullptr && !appendQueueSemantic(semantic, *queue)) {
            return fail("SceneRuntimeRenderPipeline queue semantic capacity exceeded.");
        }
    }
    lastSemanticRecord_ = semantic;

    graph.addPass(kSceneRuntimeWorldUiPassName)
        .writeAttachment(
            backbuffer, rhi::ResourceState::ColorAttachment, rhi::LoadOp::Load, rhi::StoreOp::Store)
        .execute([](render_graph::RenderContext&) {});

    graph.addPass("DebugOverlay")
        .read(backbuffer, rhi::ResourceState::Present)
        .execute(frameFeatures.debugOverlay ? std::move(frameFeatures.debugOverlay)
                                            : [](render_graph::RenderContext&) {});

    return {true, {}};
}

const SceneRuntimeRenderPipelineSemanticRecord&
SceneRuntimeRenderPipeline::lastSemanticRecord() const
{
    return lastSemanticRecord_;
}

} // namespace arp::renderer
