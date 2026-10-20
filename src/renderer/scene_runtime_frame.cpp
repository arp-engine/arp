#include "renderer/scene_runtime_frame.h"

namespace arp::renderer {

namespace {

RendererSceneRuntimeFrameResult failure(
    RendererSceneRuntimeDiagnosticCode code, const char* message)
{
    return RendererSceneRuntimeFrameResult::failure(code, message);
}

bool has3DQueue(const RendererSceneRuntimeFrameInput& input)
{
    return input.scene3DQueue != nullptr && !input.scene3DQueue->items().empty();
}

bool has2DQueue(const RendererSceneRuntimeFrameInput& input)
{
    return input.scene2DQueue != nullptr && !input.scene2DQueue->items().empty();
}

bool hasUiQueue(const RendererSceneRuntimeFrameInput& input)
{
    return input.uiQueue != nullptr && !input.uiQueue->items().empty();
}

bool anyCameraSees3D(const arp::core::pmr::Vector<arp::scene::ResolvedCamera>& cameras)
{
    for (const auto& camera : cameras) {
        if (camera.visibleQueues.includes3D()) {
            return true;
        }
    }
    return false;
}

bool anyCameraSees2D(const arp::core::pmr::Vector<arp::scene::ResolvedCamera>& cameras)
{
    for (const auto& camera : cameras) {
        if (camera.visibleQueues.includes2D()) {
            return true;
        }
    }
    return false;
}

std::size_t scene3DMaterialCount(const arp::scene3d::Scene3DRenderQueue& queue)
{
    std::size_t count = 0;
    for (const auto& item : queue.items()) {
        if (item.material.valid()) {
            ++count;
        }
    }
    return count;
}

std::size_t scene2DMaterialCount(const arp::scene2d::Scene2DRenderQueue& queue)
{
    std::size_t count = 0;
    for (const auto& item : queue.items()) {
        if (item.material.valid()) {
            ++count;
        }
    }
    return count;
}

std::size_t scene2DTextureCount(const arp::scene2d::Scene2DRenderQueue& queue)
{
    std::size_t count = 0;
    for (const auto& item : queue.items()) {
        if (item.texture.valid()) {
            ++count;
        }
    }
    return count;
}

std::size_t uiMaterialCount(const arp::ui::RenderQueue& queue)
{
    std::size_t count = 0;
    for (const auto& item : queue.items()) {
        if (item.material.valid()) {
            ++count;
        }
    }
    return count;
}

std::size_t uiTextureCount(const arp::ui::RenderQueue& queue)
{
    std::size_t count = 0;
    for (const auto& item : queue.items()) {
        if (item.texture.valid()) {
            ++count;
        }
    }
    return count;
}

RendererSceneRuntimeFrameResult validate3DQueue(const arp::scene3d::Scene3DRenderQueue& queue)
{
    if (queue.exposesNativeBackendHandles() || queue.nativeBackendHandleCount() != 0) {
        return failure(RendererSceneRuntimeDiagnosticCode::BackendHandleExposure,
            "scene runtime 3D queue exposes backend handles");
    }
    if (queue.copiedStaticVertexDataInHotPath() || queue.staticVertexDataCopyCount() != 0) {
        return failure(RendererSceneRuntimeDiagnosticCode::StaticGeometryPayloadCopy,
            "scene runtime 3D queue copies static geometry payload in the frame path");
    }

    for (const auto& item : queue.items()) {
        if (!item.worldBounds.valid()) {
            return failure(RendererSceneRuntimeDiagnosticCode::MissingBounds,
                "scene runtime 3D queue item is missing explicit bounds");
        }
        if (!item.material.valid()) {
            return failure(RendererSceneRuntimeDiagnosticCode::MissingRendererHandle,
                "scene runtime 3D queue item is missing material handle");
        }
        if (item.kind == arp::scene3d::Scene3DRenderItemKind::Mesh && !item.mesh.valid()) {
            return failure(RendererSceneRuntimeDiagnosticCode::MissingRendererHandle,
                "scene runtime 3D mesh item is missing mesh handle");
        }
        if (item.kind == arp::scene3d::Scene3DRenderItemKind::Terrain && !item.terrain.valid()) {
            return failure(RendererSceneRuntimeDiagnosticCode::MissingRendererHandle,
                "scene runtime 3D terrain item is missing terrain handle");
        }
    }

    return RendererSceneRuntimeFrameResult::success({});
}

RendererSceneRuntimeFrameResult validate2DQueue(const arp::scene2d::Scene2DRenderQueue& queue)
{
    if (queue.exposesNativeBackendHandles() || queue.nativeBackendHandleCount() != 0) {
        return failure(RendererSceneRuntimeDiagnosticCode::BackendHandleExposure,
            "scene runtime 2D queue exposes backend handles");
    }

    for (const auto& item : queue.items()) {
        if (!item.worldBounds.valid()) {
            return failure(RendererSceneRuntimeDiagnosticCode::MissingBounds,
                "scene runtime 2D queue item is missing explicit bounds");
        }
        if (!item.material.valid() || !item.texture.valid()) {
            return failure(RendererSceneRuntimeDiagnosticCode::MissingRendererHandle,
                "scene runtime 2D queue item is missing renderer resources");
        }
        if (item.kind == arp::scene2d::Scene2DRenderItemKind::Sprite && !item.sprite.valid()) {
            return failure(RendererSceneRuntimeDiagnosticCode::MissingRendererHandle,
                "scene runtime 2D sprite item is missing sprite handle");
        }
        if (item.kind == arp::scene2d::Scene2DRenderItemKind::TileMapChunk
            && !item.tileMap.valid()) {
            return failure(RendererSceneRuntimeDiagnosticCode::MissingRendererHandle,
                "scene runtime 2D tile map item is missing tile map handle");
        }
    }

    return RendererSceneRuntimeFrameResult::success({});
}

RendererSceneRuntimeFrameResult validateUiQueue(const arp::ui::RenderQueue& queue)
{
    if (queue.exposesNativeBackendHandles() || queue.nativeBackendHandleCount() != 0) {
        return failure(RendererSceneRuntimeDiagnosticCode::BackendHandleExposure,
            "scene runtime UI queue exposes backend handles");
    }
    if (queue.generatesRawStaticVertexDataInSceneHotPath()
        || queue.rawStaticVertexDataCount() != 0) {
        return failure(RendererSceneRuntimeDiagnosticCode::StaticGeometryPayloadCopy,
            "scene runtime UI queue copies static geometry payload in the frame path");
    }

    for (const auto& item : queue.items()) {
        if (!item.material.valid() || !item.texture.valid()) {
            return failure(RendererSceneRuntimeDiagnosticCode::MissingRendererHandle,
                "scene runtime UI queue item is missing renderer resources");
        }
        if (!item.rect.valid() || !item.clipRect.valid()) {
            return failure(RendererSceneRuntimeDiagnosticCode::MissingBounds,
                "scene runtime UI queue item is missing layout bounds");
        }
    }

    return RendererSceneRuntimeFrameResult::success({});
}

RendererSceneRuntimeQueueRecord make3DRecord(const arp::scene3d::Scene3DRenderQueue& queue)
{
    RendererSceneRuntimeQueueRecord record;
    record.kind = RendererSceneRuntimeQueueKind::Scene3D;
    record.order = 0;
    record.drawItemCount = queue.items().size();
    record.materialResourceCount = scene3DMaterialCount(queue);
    record.textureResourceCount = 0;
    record.compatibleWithOnePass = true;
    return record;
}

RendererSceneRuntimeQueueRecord make2DRecord(const arp::scene2d::Scene2DRenderQueue& queue)
{
    RendererSceneRuntimeQueueRecord record;
    record.kind = RendererSceneRuntimeQueueKind::Scene2D;
    record.order = 1;
    record.drawItemCount = queue.items().size();
    record.materialResourceCount = scene2DMaterialCount(queue);
    record.textureResourceCount = scene2DTextureCount(queue);
    record.compatibleWithOnePass = true;
    return record;
}

RendererSceneRuntimeQueueRecord makeUiRecord(const arp::ui::RenderQueue& queue)
{
    RendererSceneRuntimeQueueRecord record;
    record.kind = RendererSceneRuntimeQueueKind::UI;
    record.order = 2;
    record.drawItemCount = queue.items().size();
    record.materialResourceCount = uiMaterialCount(queue);
    record.textureResourceCount = uiTextureCount(queue);
    record.compatibleWithOnePass = true;
    return record;
}

} // namespace

RendererSceneRuntimeFrameResult RendererSceneRuntimeFrameResult::success(
    RendererSceneRuntimeFrame frame)
{
    frame.statusLabel_ = RendererSceneRuntimeStatusLabel::ContractComplete;
    return RendererSceneRuntimeFrameResult({RendererSceneRuntimeDiagnosticCode::None, {}}, frame);
}

RendererSceneRuntimeFrameResult RendererSceneRuntimeFrameResult::failure(
    RendererSceneRuntimeDiagnosticCode code, arp::core::pmr::String message)
{
    RendererSceneRuntimeFrame frame;
    frame.statusLabel_ = RendererSceneRuntimeStatusLabel::DiagnosticFailure;
    return RendererSceneRuntimeFrameResult({code, message}, frame);
}

RendererSceneRuntimeFrameResult::RendererSceneRuntimeFrameResult(
    RendererSceneRuntimeDiagnostic diagnostic, RendererSceneRuntimeFrame frame)
    : diagnostic_(diagnostic), frame_(frame)
{}

RendererSceneRuntimeFrameBuilder::RendererSceneRuntimeFrameBuilder(
    RendererSceneRuntimeFrameBuilderDesc desc)
    : desc_(desc)
{}

RendererSceneRuntimeFrameResult RendererSceneRuntimeFrameBuilder::build(
    const RendererSceneRuntimeFrameInput& input) const
{
    if (input.snapshot == nullptr) {
        return failure(RendererSceneRuntimeDiagnosticCode::MissingTraversalSnapshot,
            "scene runtime renderer frame requires a traversal snapshot");
    }
    if (!input.snapshot->ok()) {
        return failure(RendererSceneRuntimeDiagnosticCode::InvalidTraversalSnapshot,
            "scene runtime renderer frame snapshot is invalid");
    }
    if (input.resolvedCameras == nullptr || input.resolvedCameras->empty()) {
        return failure(RendererSceneRuntimeDiagnosticCode::MissingResolvedCamera,
            "scene runtime renderer frame requires at least one resolved camera");
    }

    const bool include3D = has3DQueue(input);
    const bool include2D = has2DQueue(input);
    const bool includeUi = hasUiQueue(input);
    if (!include3D && !include2D && !includeUi) {
        return failure(RendererSceneRuntimeDiagnosticCode::MissingRenderQueue,
            "scene runtime renderer frame requires at least one logical queue");
    }

    if (desc_.requireOneCompatibleWorldUiPass && !input.oneCompatibleWorldUiPassIntent) {
        return failure(RendererSceneRuntimeDiagnosticCode::IncompatibleOnePassIntent,
            "scene runtime renderer frame requires explicit compatible one-pass intent");
    }
    if (input.backendHandleExposureCount != 0) {
        return failure(RendererSceneRuntimeDiagnosticCode::BackendHandleExposure,
            "scene runtime renderer frame rejected backend handle exposure metadata");
    }
    if (input.staticGeometryPayloadCopyCount != 0) {
        return failure(RendererSceneRuntimeDiagnosticCode::StaticGeometryPayloadCopy,
            "scene runtime renderer frame rejected static geometry payload copies");
    }

    if (include3D && !anyCameraSees3D(*input.resolvedCameras)) {
        return failure(RendererSceneRuntimeDiagnosticCode::CameraQueueMaskMismatch,
            "scene runtime renderer frame has 3D queue without camera visibility");
    }
    if (include2D && !anyCameraSees2D(*input.resolvedCameras)) {
        return failure(RendererSceneRuntimeDiagnosticCode::CameraQueueMaskMismatch,
            "scene runtime renderer frame has 2D queue without camera visibility");
    }

    if (include3D) {
        const auto validation = validate3DQueue(*input.scene3DQueue);
        if (!validation.ok()) {
            return validation;
        }
    }
    if (include2D) {
        const auto validation = validate2DQueue(*input.scene2DQueue);
        if (!validation.ok()) {
            return validation;
        }
    }
    if (includeUi) {
        const auto validation = validateUiQueue(*input.uiQueue);
        if (!validation.ok()) {
            return validation;
        }
    }

    RendererSceneRuntimeFrame frame;
    frame.resolvedCameraCount_ = input.resolvedCameras->size();
    frame.compatibleOnePassIntent_ = input.oneCompatibleWorldUiPassIntent;
    frame.backendHandleExposureCount_ = input.backendHandleExposureCount;
    frame.staticGeometryPayloadCopyCount_ = input.staticGeometryPayloadCopyCount;
    frame.queueRecords_.reserve(desc_.maxQueueCount);

    if (include3D) {
        const auto record = make3DRecord(*input.scene3DQueue);
        frame.queueRecords_.push_back(record);
        frame.drawItemCount_ += record.drawItemCount;
        frame.materialResourceCount_ += record.materialResourceCount;
        frame.textureResourceCount_ += record.textureResourceCount;
    }
    if (include2D) {
        const auto record = make2DRecord(*input.scene2DQueue);
        frame.queueRecords_.push_back(record);
        frame.drawItemCount_ += record.drawItemCount;
        frame.materialResourceCount_ += record.materialResourceCount;
        frame.textureResourceCount_ += record.textureResourceCount;
    }
    if (includeUi) {
        const auto record = makeUiRecord(*input.uiQueue);
        frame.queueRecords_.push_back(record);
        frame.drawItemCount_ += record.drawItemCount;
        frame.materialResourceCount_ += record.materialResourceCount;
        frame.textureResourceCount_ += record.textureResourceCount;
    }

    if (frame.queueRecords_.size() > desc_.maxQueueCount) {
        return failure(RendererSceneRuntimeDiagnosticCode::QueueBudgetExceeded,
            "scene runtime renderer frame queue budget exceeded");
    }
    if (frame.drawItemCount_ > desc_.maxDrawItemCount) {
        return failure(RendererSceneRuntimeDiagnosticCode::DrawItemBudgetExceeded,
            "scene runtime renderer frame draw item budget exceeded");
    }
    if (frame.materialResourceCount_ > desc_.maxMaterialResourceCount) {
        return failure(RendererSceneRuntimeDiagnosticCode::MaterialResourceBudgetExceeded,
            "scene runtime renderer frame material resource budget exceeded");
    }
    if (frame.textureResourceCount_ > desc_.maxTextureResourceCount) {
        return failure(RendererSceneRuntimeDiagnosticCode::TextureResourceBudgetExceeded,
            "scene runtime renderer frame texture resource budget exceeded");
    }

    return RendererSceneRuntimeFrameResult::success(frame);
}

} // namespace arp::renderer
