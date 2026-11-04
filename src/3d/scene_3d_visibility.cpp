#include "3d/scene_3d_visibility.h"

#include "3d/camera_parameters.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace arp::scene3d {
namespace {

float boundsRadius(const Bounds3D& bounds)
{
    return std::sqrt(bounds.extents.x * bounds.extents.x + bounds.extents.y * bounds.extents.y
        + bounds.extents.z * bounds.extents.z);
}

} // namespace

Scene3DVisibilityResult::Scene3DVisibilityResult(
    Scene3DDiagnostic diagnostic, Scene3DRenderQueue queue, Scene3DVisibilityTelemetry telemetry)
    : diagnostic_(std::move(diagnostic)), queue_(std::move(queue)), telemetry_(telemetry)
{}

Scene3DVisibilityResult Scene3DVisibilityResult::success(
    Scene3DRenderQueue queue, Scene3DVisibilityTelemetry telemetry)
{
    return Scene3DVisibilityResult(Scene3DDiagnostic{}, std::move(queue), telemetry);
}

Scene3DVisibilityResult Scene3DVisibilityResult::failure(Scene3DDiagnosticCode code,
    arp::core::pmr::String message,
    Scene3DVisibilityTelemetry telemetry)
{
    return Scene3DVisibilityResult(
        Scene3DDiagnostic{code, std::move(message)}, Scene3DRenderQueue{}, telemetry);
}

Scene3DVisibilityResult Scene3DVisibilitySystem::filterVisible(
    const Scene3DRenderQueue& sourceQueue, const Scene3DCameraBinding& camera) const
{
    const auto& c = camera.camera;
    const auto projection = camera.projection == scene::CameraProjection::Orthographic
        ? core::orthographic(camera.orthographicWidth * .5f,
              camera.orthographicHeight * .5f,
              c.nearPlane,
              c.farPlane)
        : core::perspective(c.verticalFovDegrees, c.aspectRatio, c.nearPlane, c.farPlane);
    const auto view = core::Mat4::translation(core::scale(c.position, -1));
    return filterSpatial(sourceQueue, camera, core::multiply(projection, view).m, true);
}

Scene3DVisibilityResult Scene3DVisibilitySystem::filterVisible(const Scene3DRenderQueue& source,
    const Scene3DCameraBinding& camera,
    const std::array<float, 16>& matrix) const
{
    return filterSpatial(source, camera, matrix, false);
}

Scene3DVisibilityResult Scene3DVisibilitySystem::filterSpatial(const Scene3DRenderQueue& source,
    const Scene3DCameraBinding& camera,
    const std::array<float, 16>& matrix,
    bool preserveParameterCamera) const
{
    Scene3DVisibilityTelemetry telemetry;
    telemetry.totalRenderables = source.items().size();
    telemetry.disabledRenderables = source.disabledComponentCount();
    // Preflight before retiring any published proxy.
    for (const auto& item : source.items())
        if (!item.worldBounds.valid() || !item.objectId.valid()) {
            ++telemetry.invalidRenderables;
            return Scene3DVisibilityResult::failure(Scene3DDiagnosticCode::InvalidBounds,
                "Scene3D spatial visibility requires an object identity and explicit world bounds.",
                telemetry);
        }
    try {
        while (indexed_.size() > source.items().size()) {
            spatial_.erase(indexed_.back().handle);
            indexed_.pop_back();
        }
        for (std::size_t i = 0; i < source.items().size(); ++i) {
            const auto& item = source.items()[i];
            auto extents = item.worldBounds.extents;
            if (preserveParameterCamera) {
                const auto radius = boundsRadius(item.worldBounds);
                extents = {radius, radius, radius};
            }
            const auto bounds = Aabb::fromCenterExtents(item.worldBounds.center, extents);
            if (i < indexed_.size()
                && (indexed_[i].object != item.objectId || indexed_[i].kind != item.kind)) {
                spatial_.erase(indexed_[i].handle);
                indexed_[i].handle = {};
            }
            if (i == indexed_.size())
                indexed_.push_back({item.objectId, item.kind, {}});
            auto& entry = indexed_[i];
            if (!entry.handle.valid()) {
                entry.object = item.objectId;
                entry.kind = item.kind;
                entry.handle = spatial_.insert({item.objectId, {}},
                    bounds,
                    item.kind == Scene3DRenderItemKind::Terrain ? SpatialMobility::Static
                                                                : SpatialMobility::Dynamic);
            } else
                spatial_.update(entry.handle, bounds);
        }
        spatial_.query(&matrix);
    } catch (const core::DiagnosticError& error) {
        ++telemetry.invalidRenderables;
        return Scene3DVisibilityResult::failure(Scene3DDiagnosticCode::InvalidBounds,
            core::pmr::String(error.what(), &core::defaultCpuMemoryResource()),
            telemetry);
    }
    Scene3DRenderQueue visible;
    if (source.hasCloud())
        visible.setCloud(source.cloud());
    if (source.hasFog())
        visible.setFog(source.fog());
    for (const auto& fog : source.fogVolumes())
        visible.addFogVolume(fog);
    for (const auto& light : source.lights())
        visible.addLight(light);
    for (const auto& binding : source.cameras())
        visible.addCamera(binding);
    for (const auto& animation : source.animations())
        visible.addAnimation(animation);
    visible.recordDisabledComponents(source.disabledComponentCount());
    for (std::size_t i = 0; i < indexed_.size(); ++i)
        if (spatial_.visible(indexed_[i].handle)
            && (!preserveParameterCamera || visibleToCamera(source.items()[i], camera))) {
            visible.addItem(source.items()[i]);
            ++telemetry.visibleRenderables;
        }
    telemetry.culledRenderables = telemetry.totalRenderables - telemetry.visibleRenderables;
    return Scene3DVisibilityResult::success(std::move(visible), telemetry);
}

bool Scene3DVisibilitySystem::visibleToCamera(
    const Scene3DRenderItem& item, const Scene3DCameraBinding& camera) const
{
    if (camera.projection == arp::scene::CameraProjection::Orthographic) {
        return visibleToOrthographicCamera(item, camera);
    }
    return visibleToPerspectiveCamera(item, camera.camera);
}

bool Scene3DVisibilitySystem::visibleToPerspectiveCamera(
    const Scene3DRenderItem& item, const arp::core::Camera& camera) const
{
    const auto center = item.worldBounds.center;
    const auto radius = boundsRadius(item.worldBounds);
    const auto localX = center.x - camera.position.x;
    const auto localY = center.y - camera.position.y;
    const auto localZ = center.z - camera.position.z;

    if (localZ + radius < camera.nearPlane || localZ - radius > camera.farPlane) {
        return false;
    }

    const auto halfFovRadians = camera.verticalFovDegrees * 0.5f * 3.1415926535f / 180.0f;
    const auto halfHeight =
        std::tan(halfFovRadians) * (localZ > camera.nearPlane ? localZ : camera.nearPlane);
    const auto halfWidth = halfHeight * camera.aspectRatio;
    return std::fabs(localX) <= halfWidth + radius && std::fabs(localY) <= halfHeight + radius;
}

bool Scene3DVisibilitySystem::visibleToOrthographicCamera(
    const Scene3DRenderItem& item, const Scene3DCameraBinding& camera) const
{
    const auto center = item.worldBounds.center;
    const auto radius = boundsRadius(item.worldBounds);
    const auto localX = center.x - camera.camera.position.x;
    const auto localY = center.y - camera.camera.position.y;
    const auto localZ = center.z - camera.camera.position.z;

    if (localZ + radius < camera.camera.nearPlane || localZ - radius > camera.camera.farPlane) {
        return false;
    }

    const auto halfWidth = camera.orthographicWidth * 0.5f;
    const auto halfHeight = camera.orthographicHeight * 0.5f;
    return std::fabs(localX) <= halfWidth + radius && std::fabs(localY) <= halfHeight + radius;
}

} // namespace arp::scene3d
