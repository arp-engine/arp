#pragma once

#include "3d/camera_parameters.h"
#include "3d/scene_3d_render_queue.h"
#include "3d/scene_3d_spatial_index.h"

namespace arp::scene3d {

struct Scene3DVisibilityTelemetry {
    size_t totalRenderables = 0;
    size_t visibleRenderables = 0;
    size_t culledRenderables = 0;
    size_t invalidRenderables = 0;
    size_t disabledRenderables = 0;
};

class Scene3DVisibilityResult {
public:
    static Scene3DVisibilityResult success(
        Scene3DRenderQueue queue, Scene3DVisibilityTelemetry telemetry);
    static Scene3DVisibilityResult failure(Scene3DDiagnosticCode code,
        arp::core::pmr::String message,
        Scene3DVisibilityTelemetry telemetry);

    bool ok() const { return diagnostic_.code == Scene3DDiagnosticCode::None; }

    const Scene3DDiagnostic& diagnostic() const { return diagnostic_; }

    const Scene3DRenderQueue& queue() const { return queue_; }

    const Scene3DVisibilityTelemetry& telemetry() const { return telemetry_; }

private:
    Scene3DVisibilityResult(Scene3DDiagnostic diagnostic,
        Scene3DRenderQueue queue,
        Scene3DVisibilityTelemetry telemetry);

    Scene3DDiagnostic diagnostic_;
    Scene3DRenderQueue queue_;
    Scene3DVisibilityTelemetry telemetry_;
};

class Scene3DVisibilitySystem {
public:
    Scene3DVisibilityResult filterVisible(
        const Scene3DRenderQueue& sourceQueue, const Scene3DCameraBinding& camera) const;
    // Persistent, matrix-based path shared with custom component consumers.
    // Keep one instance per scene/view; callers serialize registry updates.
    Scene3DVisibilityResult filterVisible(const Scene3DRenderQueue& sourceQueue,
        const Scene3DCameraBinding& camera,
        const std::array<float, 16>& viewProjection) const;

    const auto& spatialTelemetry() const { return spatial_.telemetry(); }

private:
    Scene3DVisibilityResult filterSpatial(const Scene3DRenderQueue& source,
        const Scene3DCameraBinding& camera,
        const std::array<float, 16>& matrix,
        bool preserveParameterCamera) const;

    struct IndexedItem {
        scene::GameObjectId object;
        Scene3DRenderItemKind kind;
        SceneSpatialHandle handle;
    };

    mutable Scene3DSpatialIndex spatial_;
    mutable core::pmr::Vector<IndexedItem> indexed_{&core::defaultCpuMemoryResource()};
    bool visibleToCamera(const Scene3DRenderItem& item, const Scene3DCameraBinding& camera) const;
    bool visibleToPerspectiveCamera(
        const Scene3DRenderItem& item, const arp::core::Camera& camera) const;
    bool visibleToOrthographicCamera(
        const Scene3DRenderItem& item, const Scene3DCameraBinding& camera) const;
};

} // namespace arp::scene3d
