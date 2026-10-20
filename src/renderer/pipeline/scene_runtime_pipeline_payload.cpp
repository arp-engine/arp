#include "renderer/pipeline/scene_runtime_pipeline_payload.h"

#include "2d/scene_2d_render_queue.h"
#include "3d/scene_3d_render_queue.h"
#include "scene/camera.h"
#include "scene/scene_traversal_snapshot.h"
#include "ui/ui.h"

#include <cstring>

namespace arp::renderer {

namespace {

SceneRuntimePipelinePayloadValidationResult fail(const char* diagnostic)
{
    return {false, diagnostic};
}

bool hasText(const char* text)
{
    return text != nullptr && text[0] != '\0';
}

bool contractMatches(const char* actual, const char* expected)
{
    return actual != nullptr && std::strcmp(actual, expected) == 0;
}

} // namespace

SceneRuntimePipelinePayloadValidationResult validateSceneRuntimePipelinePayload(
    const CustomWorkloadPayloadRef& payload)
{
    if (payload.kind != CustomWorkloadPayloadKind::SceneDrawPlan) {
        return fail("SceneRuntimePipelinePayload requires a scene payload kind.");
    }
    if (payload.object == nullptr) {
        return fail("SceneRuntimePipelinePayload requires a payload object.");
    }
    if (!hasText(payload.contractName)) {
        return fail("SceneRuntimePipelinePayload requires an explicit contract name.");
    }
    if (payload.contractVersion == 0u) {
        return fail("SceneRuntimePipelinePayload requires an explicit contract version.");
    }
    if (!contractMatches(payload.contractName, kSceneRuntimePipelinePayloadContract)) {
        return fail("SceneRuntimePipelinePayload contract name mismatch.");
    }
    if (payload.contractVersion != kSceneRuntimePipelinePayloadVersion) {
        return fail("SceneRuntimePipelinePayload contract version mismatch.");
    }

    const auto* scenePayload = customPayloadObjectAs<SceneRuntimePipelinePayload>(payload,
        CustomWorkloadPayloadKind::SceneDrawPlan,
        kSceneRuntimePipelinePayloadContract,
        kSceneRuntimePipelinePayloadVersion);
    if (scenePayload == nullptr) {
        return fail("SceneRuntimePipelinePayload contract payload type mismatch.");
    }

    return validateSceneRuntimePipelinePayload(*scenePayload);
}

SceneRuntimePipelinePayloadValidationResult validateSceneRuntimePipelinePayload(
    const SceneRuntimePipelinePayload& payload)
{
    if (payload.traversalSnapshot == nullptr) {
        return fail("SceneRuntimePipelinePayload requires a traversal snapshot.");
    }
    if (!payload.traversalSnapshot->ok()) {
        return fail("SceneRuntimePipelinePayload traversal snapshot is invalid.");
    }
    if (payload.traversalSnapshot->exposesNativeBackendHandles()
        || payload.traversalSnapshot->nativeBackendHandleCount() != 0u) {
        return fail(
            "SceneRuntimePipelinePayload rejects traversal snapshot backend handle exposure.");
    }
    if (payload.traversalSnapshot->exposesRawVertexBuffers()
        || payload.traversalSnapshot->rawVertexBufferCount() != 0u) {
        return fail(
            "SceneRuntimePipelinePayload rejects traversal snapshot static geometry payload copies.");
    }

    if (payload.resolvedCameras == nullptr || payload.resolvedCameras->empty()) {
        return fail("SceneRuntimePipelinePayload requires resolved cameras.");
    }
    if (payload.scene3DQueue == nullptr) {
        return fail("SceneRuntimePipelinePayload requires a 3D queue.");
    }
    if (payload.scene2DQueue == nullptr) {
        return fail("SceneRuntimePipelinePayload requires a 2D queue.");
    }
    if (payload.uiQueue == nullptr) {
        return fail("SceneRuntimePipelinePayload requires a UI queue.");
    }
    if (payload.rendererFrame == nullptr) {
        return fail("SceneRuntimePipelinePayload requires a RendererSceneRuntimeFrame.");
    }
    if (payload.rendererFrame->statusLabel() != RendererSceneRuntimeStatusLabel::ContractComplete) {
        return fail(
            "SceneRuntimePipelinePayload requires a Contract Complete RendererSceneRuntimeFrame status.");
    }

    if (payload.rendererFrame->exposesBackendHandles()
        || payload.rendererFrame->backendHandleExposureCount() != 0u
        || payload.scene3DQueue->exposesNativeBackendHandles()
        || payload.scene3DQueue->nativeBackendHandleCount() != 0u
        || payload.scene2DQueue->exposesNativeBackendHandles()
        || payload.scene2DQueue->nativeBackendHandleCount() != 0u
        || payload.uiQueue->exposesNativeBackendHandles()
        || payload.uiQueue->nativeBackendHandleCount() != 0u) {
        return fail("SceneRuntimePipelinePayload rejects backend handle exposure.");
    }

    if (payload.rendererFrame->copiesStaticGeometryPayloadInHotPath()
        || payload.rendererFrame->staticGeometryPayloadCopyCount() != 0u
        || payload.scene3DQueue->copiedStaticVertexDataInHotPath()
        || payload.scene3DQueue->staticVertexDataCopyCount() != 0u
        || payload.uiQueue->generatesRawStaticVertexDataInSceneHotPath()
        || payload.uiQueue->rawStaticVertexDataCount() != 0u) {
        return fail("SceneRuntimePipelinePayload rejects static geometry payload copies.");
    }

    return {true, {}};
}

const SceneRuntimePipelinePayload* sceneRuntimePipelinePayloadFrom(
    const CustomWorkloadSection& section)
{
    if (!section.enabled()) {
        return nullptr;
    }
    const auto validation = validateSceneRuntimePipelinePayload(section.payload());
    if (!validation.success) {
        return nullptr;
    }
    return customPayloadObjectAs<SceneRuntimePipelinePayload>(section.payload(),
        CustomWorkloadPayloadKind::SceneDrawPlan,
        kSceneRuntimePipelinePayloadContract,
        kSceneRuntimePipelinePayloadVersion);
}

} // namespace arp::renderer
