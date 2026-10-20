#include "editor/viewport/editor_scene_viewport_frame_builder.h"

#include "3d/components.h"

#include <utility>

namespace arp::editor {
namespace {

core::Mat4 projectionForViewport(
    const scene::ResolvedCamera& camera, std::uint32_t width, std::uint32_t height)
{
    if (camera.aspectPolicy.kind != scene::CameraAspectPolicyKind::MatchViewport)
        return camera.projectionMatrix;

    const auto aspect = static_cast<float>(width) / static_cast<float>(height);
    if (camera.projection == scene::CameraProjection::Perspective) {
        return camera.viewHandedness == scene::CameraViewHandedness::RightHanded
            ? core::perspectiveRightHanded(camera.camera.verticalFovDegrees,
                  aspect,
                  camera.camera.nearPlane,
                  camera.camera.farPlane)
            : core::perspective(camera.camera.verticalFovDegrees,
                  aspect,
                  camera.camera.nearPlane,
                  camera.camera.farPlane);
    }

    const auto halfHeight = camera.orthographicHeight * 0.5f;
    const auto halfWidth = halfHeight * aspect;
    return camera.viewHandedness == scene::CameraViewHandedness::RightHanded
        ? core::orthographicRightHanded(
              halfWidth, halfHeight, camera.camera.nearPlane, camera.camera.farPlane)
        : core::orthographic(
              halfWidth, halfHeight, camera.camera.nearPlane, camera.camera.farPlane);
}

} // namespace

EditorSceneViewportFrameBuildResult buildEditorSceneViewportFrame(const scene::Scene& scene,
    const scene::SceneTraversalSnapshot& snapshot,
    std::uint64_t stateVersion,
    std::uint32_t targetWidth,
    std::uint32_t targetHeight,
    EditorSceneViewportRect physicalViewport)
{
    EditorSceneViewportFrameBuildResult result;
    auto& frame = result.frame;
    frame.sceneId = scene.sceneId();
    frame.stateVersion = stateVersion;
    frame.targetWidth = targetWidth;
    frame.targetHeight = targetHeight;
    frame.viewport = physicalViewport;

    if (targetWidth == 0 || targetHeight == 0) {
        result.validation = {EditorSceneViewportDiagnostic::InvalidTargetExtent, 0};
        return result;
    }
    if (physicalViewport.width == 0 || physicalViewport.height == 0
        || physicalViewport.x > targetWidth || physicalViewport.y > targetHeight
        || physicalViewport.width > targetWidth - physicalViewport.x
        || physicalViewport.height > targetHeight - physicalViewport.y) {
        result.validation = {EditorSceneViewportDiagnostic::InvalidViewportRect, 0};
        return result;
    }
    if (!snapshot.ok()) {
        result.validation = {EditorSceneViewportDiagnostic::InvalidSceneSnapshot, 0};
        return result;
    }

    const auto cameras = scene::CameraResolver{}.resolve(scene, snapshot);
    if (!cameras.ok()) {
        result.validation = {EditorSceneViewportDiagnostic::CameraResolutionFailed, 0};
        return result;
    }
    if (cameras.cameras().empty()) {
        result.validation = {EditorSceneViewportDiagnostic::MissingCamera, 0};
        return result;
    }

    const auto& camera = cameras.cameras().front();
    frame.cameraObjectId = camera.objectId;
    frame.cameraWorldTransform = camera.worldTransform;
    frame.cameraViewMatrix = camera.viewMatrix;
    frame.cameraProjectionMatrix =
        projectionForViewport(camera, physicalViewport.width, physicalViewport.height);
    frame.cameraAspectPolicy = camera.aspectPolicy.kind;
    frame.cameraProjectionAspectRatio =
        camera.aspectPolicy.kind == scene::CameraAspectPolicyKind::MatchViewport
        ? static_cast<float>(physicalViewport.width) / static_cast<float>(physicalViewport.height)
        : camera.camera.aspectRatio;

    for (const auto& node : snapshot.nodes()) {
        if (!node.activeInHierarchy)
            continue;
        const auto* object = scene.findObject(node.objectId);
        if (object == nullptr) {
            result.validation = {EditorSceneViewportDiagnostic::InvalidSceneSnapshot, 0};
            return result;
        }
        for (const auto& component : object->components()) {
            if (!component->enabled())
                continue;
            const auto* mesh = dynamic_cast<const scene3d::MeshComponent*>(component.get());
            if (mesh == nullptr)
                continue;
            EditorSceneViewportDrawItem item;
            item.objectId = node.objectId;
            item.componentId = mesh->id();
            item.worldTransform = node.worldTransform;
            item.meshResourceId = mesh->resourceId();
            item.materialResourceId = mesh->materialResourceId();
            frame.drawItems.push_back(std::move(item));
        }
    }

    result.validation = validateEditorSceneViewportFrame(frame);
    return result;
}

} // namespace arp::editor
