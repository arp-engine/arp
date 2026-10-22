#pragma once

#include "2d/scene_2d_render_queue.h"
#include "3d/scene_3d_render_queue.h"
#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "scene/camera.h"
#include "scene/scene_traversal_snapshot.h"
#include "ui/ui.h"

#include <cstddef>

namespace arp::renderer {

enum class RendererSceneRuntimeDiagnosticCode {
    None,
    MissingTraversalSnapshot,
    InvalidTraversalSnapshot,
    MissingResolvedCamera,
    MissingRenderQueue,
    CameraQueueMaskMismatch,
    QueueBudgetExceeded,
    DrawItemBudgetExceeded,
    MaterialResourceBudgetExceeded,
    TextureResourceBudgetExceeded,
    MissingRendererHandle,
    MissingBounds,
    IncompatibleOnePassIntent,
    BackendHandleExposure,
    StaticGeometryPayloadCopy,
};

struct RendererSceneRuntimeDiagnostic {
    RendererSceneRuntimeDiagnosticCode code = RendererSceneRuntimeDiagnosticCode::None;
    arp::core::pmr::String message;
};

enum class RendererSceneRuntimeStatusLabel {
    DiagnosticFailure,
    ContractComplete,
};

enum class RendererSceneRuntimeQueueKind {
    Scene3D,
    Scene2D,
    UI,
};

struct RendererSceneRuntimeQueueRecord {
    RendererSceneRuntimeQueueKind kind = RendererSceneRuntimeQueueKind::Scene3D;
    std::size_t order = 0;
    std::size_t drawItemCount = 0;
    std::size_t materialResourceCount = 0;
    std::size_t textureResourceCount = 0;
    bool compatibleWithOnePass = true;
};

struct RendererSceneRuntimeFrameBuilderDesc {
    std::size_t maxQueueCount = 3;
    std::size_t maxDrawItemCount = 1024;
    std::size_t maxMaterialResourceCount = 1024;
    std::size_t maxTextureResourceCount = 1024;
    bool requireOneCompatibleWorldUiPass = true;
};

struct RendererSceneRuntimeFrameInput {
    const arp::scene::SceneTraversalSnapshot* snapshot = nullptr;
    const arp::core::pmr::Vector<arp::scene::ResolvedCamera>* resolvedCameras = nullptr;
    const arp::scene3d::Scene3DRenderQueue* scene3DQueue = nullptr;
    const arp::scene2d::Scene2DRenderQueue* scene2DQueue = nullptr;
    const arp::ui::RenderQueue* uiQueue = nullptr;
    bool oneCompatibleWorldUiPassIntent = true;
    std::size_t backendHandleExposureCount = 0;
    std::size_t staticGeometryPayloadCopyCount = 0;
};

class ARP_API RendererSceneRuntimeFrame final {
public:
    RendererSceneRuntimeStatusLabel statusLabel() const { return statusLabel_; }

    const arp::core::pmr::Vector<RendererSceneRuntimeQueueRecord>& queueRecords() const
    {
        return queueRecords_;
    }

    std::size_t drawItemCount() const { return drawItemCount_; }

    std::size_t materialResourceCount() const { return materialResourceCount_; }

    std::size_t textureResourceCount() const { return textureResourceCount_; }

    std::size_t resolvedCameraCount() const { return resolvedCameraCount_; }

    bool compatibleOnePassIntent() const { return compatibleOnePassIntent_; }

    bool exposesBackendHandles() const { return backendHandleExposureCount_ != 0; }

    std::size_t backendHandleExposureCount() const { return backendHandleExposureCount_; }

    bool copiesStaticGeometryPayloadInHotPath() const
    {
        return staticGeometryPayloadCopyCount_ != 0;
    }

    std::size_t staticGeometryPayloadCopyCount() const { return staticGeometryPayloadCopyCount_; }

private:
    friend class RendererSceneRuntimeFrameBuilder;
    friend class RendererSceneRuntimeFrameResult;

    RendererSceneRuntimeStatusLabel statusLabel_ =
        RendererSceneRuntimeStatusLabel::DiagnosticFailure;
    arp::core::pmr::Vector<RendererSceneRuntimeQueueRecord> queueRecords_;
    std::size_t drawItemCount_ = 0;
    std::size_t materialResourceCount_ = 0;
    std::size_t textureResourceCount_ = 0;
    std::size_t resolvedCameraCount_ = 0;
    bool compatibleOnePassIntent_ = false;
    std::size_t backendHandleExposureCount_ = 0;
    std::size_t staticGeometryPayloadCopyCount_ = 0;
};

class ARP_API RendererSceneRuntimeFrameResult final {
public:
    static RendererSceneRuntimeFrameResult success(RendererSceneRuntimeFrame frame);
    static RendererSceneRuntimeFrameResult failure(
        RendererSceneRuntimeDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == RendererSceneRuntimeDiagnosticCode::None; }

    const RendererSceneRuntimeDiagnostic& diagnostic() const { return diagnostic_; }

    const RendererSceneRuntimeFrame& frame() const { return frame_; }

private:
    RendererSceneRuntimeFrameResult(
        RendererSceneRuntimeDiagnostic diagnostic, RendererSceneRuntimeFrame frame);

    RendererSceneRuntimeDiagnostic diagnostic_;
    RendererSceneRuntimeFrame frame_;
};

class ARP_API RendererSceneRuntimeFrameBuilder final {
public:
    explicit RendererSceneRuntimeFrameBuilder(RendererSceneRuntimeFrameBuilderDesc desc);

    RendererSceneRuntimeFrameResult build(const RendererSceneRuntimeFrameInput& input) const;

private:
    RendererSceneRuntimeFrameBuilderDesc desc_;
};

} // namespace arp::renderer
