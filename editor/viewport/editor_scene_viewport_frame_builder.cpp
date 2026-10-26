#include "editor/viewport/editor_scene_viewport_frame_builder.h"

#include "3d/authoring_components.h"
#include "3d/components.h"
#include "editor/workspace/editor_terrain_preview.h"
#include "resource/terrain_asset_resource.h"

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
    EditorSceneViewportRect physicalViewport,
    const resource::ResourceManager* resources,
    std::size_t maxDrawItems,
    const scene::SceneTraversalSnapshot* visibleSnapshot)
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
    if (!snapshot.ok() || (visibleSnapshot && !visibleSnapshot->ok())) {
        result.validation = {EditorSceneViewportDiagnostic::InvalidSceneSnapshot, 0};
        return result;
    }

    const auto& visible = visibleSnapshot ? *visibleSnapshot : snapshot;
    for (const auto& node : visible.nodes()) {
        if (!node.activeInHierarchy)
            continue;
        const auto* object = scene.findObject(node.objectId);
        if (object == nullptr) {
            result.validation = {EditorSceneViewportDiagnostic::InvalidSceneSnapshot, 0};
            return result;
        }
        for (const auto& component : object->components()) {
            const auto* sky = dynamic_cast<const scene3d::SkyComponent*>(component.get());
            if (sky == nullptr || !component->enabled())
                continue;
            frame.sky.present = true;
            frame.sky.zenith = sky->zenith_color();
            frame.sky.horizon = sky->horizon_color();
            frame.sky.sunDirection = sky->sun_direction();
            frame.sky.sunColor = sky->sun_color();
            frame.sky.domeRadius = sky->dome_radius();
            break;
        }
        if (frame.sky.present)
            break;
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

    for (const auto& node : visible.nodes()) {
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
            const auto* terrain = dynamic_cast<const scene3d::TerrainComponent*>(component.get());
            if (terrain && !terrain->resourceId().empty()) {
                if (!resources || !maxDrawItems) {
                    result.validation = {EditorSceneViewportDiagnostic::GeometryBudgetExceeded,
                        frame.drawItems.size()};
                    return result;
                }
                const auto handle =
                    resources->find<resource::TerrainAssetResource>(terrain->resourceId());
                if (!handle.ready()) {
                    result.validation = {EditorSceneViewportDiagnostic::TerrainResourceInvalid,
                        frame.drawItems.size()};
                    return result;
                }
                const auto previewHandle =
                    resources->find<resource::MaterialResource>(terrain->materialResourceId());
                const auto* preview = previewHandle.ready()
                    ? dynamic_cast<const EditorTerrainPreviewMaterial*>(previewHandle.get())
                    : nullptr;
                if ((handle.get()->hasSurfaceWeights() || handle.get()->hasVegetation())
                    && !preview) {
                    result.validation = {
                        EditorSceneViewportDiagnostic::TerrainSurfacePreviewUnsupported,
                        frame.drawItems.size()};
                    return result;
                }
                if (preview) {
                    frame.bakedTerrainPreview = true;
                    frame.omittedVegetationInstances += preview->vegetationInstanceCount;
                }
                const auto count = static_cast<std::uint64_t>(handle.get()->tileCountX())
                    * handle.get()->tileCountZ();
                if (frame.drawItems.size() > maxDrawItems
                    || count > maxDrawItems - frame.drawItems.size()) {
                    result.validation = {EditorSceneViewportDiagnostic::GeometryBudgetExceeded,
                        frame.drawItems.size()};
                    return result;
                }
                for (std::uint32_t z = 0u; z < handle.get()->tileCountZ(); ++z)
                    for (std::uint32_t x = 0u; x < handle.get()->tileCountX(); ++x) {
                        EditorSceneViewportDrawItem item;
                        item.objectId = node.objectId;
                        item.componentId = terrain->id();
                        item.worldTransform = node.worldTransform;
                        item.meshResourceId = terrain->resourceId();
                        item.materialResourceId = terrain->materialResourceId();
                        item.geometry = EditorSceneViewportGeometry::TerrainTile;
                        item.tile = {static_cast<std::int32_t>(x), static_cast<std::int32_t>(z)};
                        if (preview) {
                            item.terrainPreviewMaterialResourceId = terrain->materialResourceId();
                            item.materialResourceId =
                                resolveEditorTerrainPreviewMaterial(*resources,
                                    item.meshResourceId,
                                    handle.generation(),
                                    item.terrainPreviewMaterialResourceId,
                                    item.tile);
                            if (item.materialResourceId.empty()) {
                                result.validation = {
                                    EditorSceneViewportDiagnostic::TerrainSurfacePreviewUnsupported,
                                    frame.drawItems.size()};
                                return result;
                            }
                        }
                        frame.drawItems.push_back(std::move(item));
                    }
                continue;
            }
            const auto* mesh = dynamic_cast<const scene3d::MeshComponent*>(component.get());
            if (mesh == nullptr)
                continue;
            EditorSceneViewportDrawItem item;
            item.objectId = node.objectId;
            item.componentId = mesh->id();
            item.worldTransform = node.worldTransform;
            item.meshResourceId = mesh->resourceId();
            item.materialResourceId = mesh->materialResourceId();
            if (maxDrawItems && frame.drawItems.size() >= maxDrawItems) {
                result.validation = {
                    EditorSceneViewportDiagnostic::GeometryBudgetExceeded, frame.drawItems.size()};
                return result;
            }
            frame.drawItems.push_back(std::move(item));
        }
    }

    result.validation = validateEditorSceneViewportFrame(frame);
    return result;
}

} // namespace arp::editor
