#include "scene/scene_component_persistence.h"

#include "scene/scene_asset_resource.h"

#include <cmath>

namespace arp::scene {

CameraPersistenceState CameraPersistenceState::from(const CameraComponentDesc& d)
{
    return {d.projection,
        d.viewHandedness,
        d.verticalFovDegrees,
        d.orthographicHeight,
        d.nearPlane,
        d.farPlane,
        d.aspectPolicy,
        d.viewport,
        d.visibleQueues.includes2D(),
        d.visibleQueues.includes3D(),
        d.priority,
        d.clearIntent.colorLoadOp,
        {d.clearIntent.clearColor[0],
            d.clearIntent.clearColor[1],
            d.clearIntent.clearColor[2],
            d.clearIntent.clearColor[3]}};
}

CameraComponentDesc CameraPersistenceState::descriptor() const
{
    CameraComponentDesc d;
    d.projection = projection;
    d.viewHandedness = viewHandedness;
    d.verticalFovDegrees = verticalFovDegrees;
    d.orthographicHeight = orthographicHeight;
    d.nearPlane = nearPlane;
    d.farPlane = farPlane;
    d.aspectPolicy = aspectPolicy;
    d.viewport = viewport;
    d.visibleQueues = visible2D
        ? (visible3D ? RenderQueueMask::world2DAnd3D() : RenderQueueMask::queue2D())
        : (visible3D ? RenderQueueMask::queue3D() : RenderQueueMask::none());
    d.priority = priority;
    d.clearIntent =
        CameraClearIntent::clear(clearColor.x, clearColor.y, clearColor.z, clearColor.w);
    d.clearIntent.colorLoadOp = colorLoadOp;
    return d;
}

bool CameraPersistenceState::valid() const
{
    const auto d = descriptor();
    return CameraResolver::validate(d).ok()
        && (projection == CameraProjection::Perspective
            || projection == CameraProjection::Orthographic)
        && (viewHandedness == CameraViewHandedness::LeftHanded
            || viewHandedness == CameraViewHandedness::RightHanded)
        && std::isfinite(verticalFovDegrees) && verticalFovDegrees > 0 && verticalFovDegrees < 180
        && std::isfinite(orthographicHeight) && orthographicHeight > 0 && std::isfinite(nearPlane)
        && std::isfinite(farPlane) && nearPlane > 0 && farPlane > nearPlane
        && (aspectPolicy.kind == CameraAspectPolicyKind::Fixed
            || aspectPolicy.kind == CameraAspectPolicyKind::MatchViewport)
        && std::isfinite(aspectPolicy.aspectRatio) && aspectPolicy.aspectRatio > 0
        && aspectPolicy.valid() && d.viewport.valid() && std::isfinite(viewport.x)
        && std::isfinite(viewport.y) && std::isfinite(viewport.width)
        && std::isfinite(viewport.height) && !d.visibleQueues.empty()
        && (colorLoadOp == CameraLoadOp::Load || colorLoadOp == CameraLoadOp::Clear)
        && std::isfinite(clearColor.x) && std::isfinite(clearColor.y) && std::isfinite(clearColor.z)
        && std::isfinite(clearColor.w);
}

bool registerScenePersistence(SceneComponentFactory& factory)
{
    if (!initializeSceneAuthoringReflection())
        return false;
    SceneComponentFactory::Persistence e;
    e.type = "arp.camera/1";
    e.nativeType = "CameraComponent";
    e.nativeClass = reflection::class_of<CameraComponent>();
    e.accepts = [](const Component& c) {
        return dynamic_cast<const CameraComponent*>(&c) != nullptr;
    };
    e.construct = [](GameObject&, const reflection::object&) {
        return core::makeUnique<CameraComponent>();
    };
    e.state = [](const Component& c) {
        return reflection::object(
            CameraPersistenceState::from(static_cast<const CameraComponent&>(c).desc()));
    };
    e.inspectionState = [] {
        return reflection::object(CameraPersistenceState{});
    };
    e.validateState = [](const reflection::object& state) {
        return state.is<CameraPersistenceState>()
            && state.cast<const CameraPersistenceState&>().valid();
    };
    e.prepare = [check = e.validateState](Component& c, const reflection::object& state) {
        if (!check(state))
            return false;
        static_cast<CameraComponent&>(c).desc() =
            state.cast<const CameraPersistenceState&>().descriptor();
        return true;
    };
    return factory.addPersistence(std::move(e));
}
} // namespace arp::scene
