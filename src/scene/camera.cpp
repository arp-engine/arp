#include "scene/camera.h"

#include "scene/game_object.h"
#include "scene/scene.h"

#include <algorithm>
#include <utility>

namespace arp::scene {

CameraAspectPolicy CameraAspectPolicy::fixed(float aspectRatio)
{
    CameraAspectPolicy policy;
    policy.kind = CameraAspectPolicyKind::Fixed;
    policy.aspectRatio = aspectRatio;
    return policy;
}

CameraAspectPolicy CameraAspectPolicy::matchViewport()
{
    CameraAspectPolicy policy;
    policy.kind = CameraAspectPolicyKind::MatchViewport;
    policy.aspectRatio = 1.0f;
    return policy;
}

bool CameraAspectPolicy::valid() const
{
    return kind == CameraAspectPolicyKind::MatchViewport || aspectRatio > 0.0f;
}

ViewportRect ViewportRect::normalized(float x, float y, float width, float height)
{
    ViewportRect viewport;
    viewport.x = x;
    viewport.y = y;
    viewport.width = width;
    viewport.height = height;
    return viewport;
}

bool ViewportRect::valid() const
{
    return width > 0.0f && height > 0.0f && x >= 0.0f && y >= 0.0f && x + width <= 1.0f
        && y + height <= 1.0f;
}

RenderQueueMask RenderQueueMask::none()
{
    return RenderQueueMask(0);
}

RenderQueueMask RenderQueueMask::queue2D()
{
    return RenderQueueMask(0x1u);
}

RenderQueueMask RenderQueueMask::queue3D()
{
    return RenderQueueMask(0x2u);
}

RenderQueueMask RenderQueueMask::world2DAnd3D()
{
    return RenderQueueMask(0x1u | 0x2u);
}

CameraClearIntent CameraClearIntent::load()
{
    return CameraClearIntent{};
}

CameraClearIntent CameraClearIntent::clear(float red, float green, float blue, float alpha)
{
    CameraClearIntent intent;
    intent.colorLoadOp = CameraLoadOp::Clear;
    intent.clearColor[0] = red;
    intent.clearColor[1] = green;
    intent.clearColor[2] = blue;
    intent.clearColor[3] = alpha;
    return intent;
}

CameraComponentDesc CameraComponentDesc::perspective(
    float verticalFovDegrees, float aspectRatio, float nearPlane, float farPlane)
{
    CameraComponentDesc desc;
    desc.projection = CameraProjection::Perspective;
    desc.verticalFovDegrees = verticalFovDegrees;
    desc.nearPlane = nearPlane;
    desc.farPlane = farPlane;
    desc.aspectPolicy = CameraAspectPolicy::fixed(aspectRatio);
    desc.visibleQueues = RenderQueueMask::queue3D();
    desc.camera.setPerspective(verticalFovDegrees, aspectRatio, nearPlane, farPlane);
    return desc;
}

CameraComponentDesc CameraComponentDesc::orthographic(
    float orthographicHeight, float nearPlane, float farPlane)
{
    CameraComponentDesc desc;
    desc.projection = CameraProjection::Orthographic;
    desc.orthographicHeight = orthographicHeight;
    desc.nearPlane = nearPlane;
    desc.farPlane = farPlane;
    desc.aspectPolicy = CameraAspectPolicy::fixed(1.0f);
    desc.visibleQueues = RenderQueueMask::world2DAnd3D();
    desc.camera.setPerspective(60.0f, 1.0f, nearPlane, farPlane);
    return desc;
}

CameraComponent::CameraComponent(CameraComponentDesc desc) : desc_(desc)
{}

CameraResolverResult::CameraResolverResult(
    CameraDiagnostic diagnostic, arp::core::pmr::Vector<ResolvedCamera> cameras)
    : diagnostic_(std::move(diagnostic)), cameras_(std::move(cameras))
{}

CameraResolverResult CameraResolverResult::success(arp::core::pmr::Vector<ResolvedCamera> cameras)
{
    return CameraResolverResult(CameraDiagnostic{}, std::move(cameras));
}

CameraResolverResult CameraResolverResult::failure(
    CameraDiagnosticCode code, arp::core::pmr::String message)
{
    return CameraResolverResult(CameraDiagnostic{code, std::move(message)}, {});
}

CameraResolverResult CameraResolver::resolve(
    const Scene& scene, const SceneTraversalSnapshot& snapshot) const
{
    arp::core::pmr::Vector<ResolvedCamera> cameras;
    for (const auto& node : snapshot.nodes()) {
        if (!node.activeInHierarchy) {
            continue;
        }
        const auto* object = scene.findObject(node.objectId);
        if (object == nullptr) {
            continue;
        }
        for (const auto& component : object->components()) {
            if (!component->enabled()) {
                continue;
            }
            const auto* camera = dynamic_cast<const CameraComponent*>(component.get());
            if (camera == nullptr) {
                continue;
            }
            const auto validated = validate(camera->desc());
            if (!validated.ok()) {
                return validated;
            }
            cameras.push_back(resolveCamera(*camera, node));
        }
    }

    std::stable_sort(
        cameras.begin(), cameras.end(), [](const ResolvedCamera& lhs, const ResolvedCamera& rhs) {
            if (lhs.priority != rhs.priority) {
                return lhs.priority > rhs.priority;
            }
            return lhs.objectId.value() < rhs.objectId.value();
        });
    return CameraResolverResult::success(std::move(cameras));
}

CameraResolverResult CameraResolver::validate(const CameraComponentDesc& desc) const
{
    if (desc.viewHandedness != CameraViewHandedness::LeftHanded
        && desc.viewHandedness != CameraViewHandedness::RightHanded) {
        return CameraResolverResult::failure(CameraDiagnosticCode::InvalidViewHandedness,
            "CameraComponent requires an explicit supported view handedness.");
    }
    if (desc.nearPlane <= 0.0f || desc.farPlane <= desc.nearPlane) {
        return CameraResolverResult::failure(CameraDiagnosticCode::InvalidNearFar,
            "CameraComponent requires near > 0 and far > near.");
    }
    if (!desc.viewport.valid()) {
        return CameraResolverResult::failure(CameraDiagnosticCode::InvalidViewport,
            "CameraComponent requires an explicit valid normalized viewport.");
    }
    if (!desc.aspectPolicy.valid()) {
        return CameraResolverResult::failure(CameraDiagnosticCode::InvalidAspectPolicy,
            "CameraComponent requires a valid aspect policy.");
    }
    if (desc.visibleQueues.empty()) {
        return CameraResolverResult::failure(CameraDiagnosticCode::EmptyRenderQueueMask,
            "CameraComponent requires a non-empty visible RenderQueue mask.");
    }
    return CameraResolverResult::success({});
}

ResolvedCamera CameraResolver::resolveCamera(
    const CameraComponent& component, const SceneTraversalNode& node) const
{
    const auto& desc = component.desc();
    const auto aspect = desc.aspectPolicy.kind == CameraAspectPolicyKind::Fixed
        ? desc.aspectPolicy.aspectRatio
        : desc.viewport.width / desc.viewport.height;

    ResolvedCamera resolved;
    resolved.objectId = node.objectId;
    resolved.componentId = component.id();
    resolved.projection = desc.projection;
    resolved.viewHandedness = desc.viewHandedness;
    resolved.worldTransform = node.worldTransform;
    resolved.viewport = desc.viewport;
    resolved.visibleQueues = desc.visibleQueues;
    resolved.aspectPolicy = desc.aspectPolicy;
    resolved.clearIntent = desc.clearIntent;
    resolved.priority = desc.priority;
    resolved.orthographicHeight = desc.orthographicHeight;
    resolved.orthographicWidth = desc.orthographicHeight * aspect;
    resolved.camera = desc.camera;
    resolved.camera.position = node.worldTransform.translation;
    resolved.camera.aspectRatio = aspect;
    resolved.camera.nearPlane = desc.nearPlane;
    resolved.camera.farPlane = desc.farPlane;

    // Derive per-frame orientation + matrices from the world transform's
    // rotation, with local -Z forward (identity rotation looks down -Z). This
    // is the single place the view/projection matrices are computed; render
    // consumers read resolved.viewMatrix/projectionMatrix instead of each
    // recomputing lookAt/perspective.
    const auto position = node.worldTransform.translation;
    resolved.forward =
        arp::core::rotate(node.worldTransform.rotation, arp::core::Vec3{0.0f, 0.0f, -1.0f});
    const auto focus = arp::core::Vec3{
        position.x + resolved.forward.x,
        position.y + resolved.forward.y,
        position.z + resolved.forward.z,
    };
    const bool rightHanded = desc.viewHandedness == CameraViewHandedness::RightHanded;
    resolved.viewMatrix = rightHanded ? arp::core::lookAtRightHanded(position, focus)
                                      : arp::core::lookAt(position, focus);
    resolved.depthPositiveViewMatrix = rightHanded
        ? arp::core::multiply(arp::core::Mat4::scale({1, 1, -1}), resolved.viewMatrix)
        : resolved.viewMatrix;
    if (desc.projection == CameraProjection::Perspective) {
        resolved.projectionMatrix =
            (rightHanded ? arp::core::perspectiveRightHanded : arp::core::perspective)(
                desc.verticalFovDegrees, aspect, desc.nearPlane, desc.farPlane);
    } else {
        const auto halfWidth = resolved.orthographicWidth * 0.5f;
        const auto halfHeight = resolved.orthographicHeight * 0.5f;
        resolved.projectionMatrix =
            (rightHanded ? arp::core::orthographicRightHanded : arp::core::orthographic)(
                halfWidth, halfHeight, desc.nearPlane, desc.farPlane);
    }
    resolved.depthPositiveProjectionScale =
        rightHanded ? -resolved.projectionMatrix.m[10] : resolved.projectionMatrix.m[10];
    return resolved;
}

} // namespace arp::scene
