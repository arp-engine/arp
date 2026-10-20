#include "3d/scene_3d_visibility.h"

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
    Scene3DRenderQueue visibleQueue;
    for (const auto& cameraBinding : sourceQueue.cameras()) {
        visibleQueue.addCamera(cameraBinding);
    }
    for (const auto& animation : sourceQueue.animations()) {
        visibleQueue.addAnimation(animation);
    }
    visibleQueue.recordDisabledComponents(sourceQueue.disabledComponentCount());

    Scene3DVisibilityTelemetry telemetry;
    telemetry.totalRenderables = sourceQueue.items().size();
    telemetry.disabledRenderables = sourceQueue.disabledComponentCount();

    for (const auto& item : sourceQueue.items()) {
        if (!item.worldBounds.valid()) {
            ++telemetry.invalidRenderables;
            return Scene3DVisibilityResult::failure(Scene3DDiagnosticCode::InvalidBounds,
                "Scene3DVisibilitySystem requires explicit valid world bounds.",
                telemetry);
        }

        if (visibleToCamera(item, camera)) {
            visibleQueue.addItem(item);
            ++telemetry.visibleRenderables;
        } else {
            ++telemetry.culledRenderables;
        }
    }

    return Scene3DVisibilityResult::success(std::move(visibleQueue), telemetry);
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
