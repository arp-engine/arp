#include "3d/scene_3d.h"
#include "scene/camera.h"
#include "scene/scene.h"

#include <cassert>

namespace {

using namespace arp::scene;
using namespace arp::scene3d;

MeshComponentDesc meshDesc(uint32_t meshId, arp::core::Vec3 center, arp::core::Vec3 extents)
{
    MeshComponentDesc desc;
    desc.mesh = arp::renderer::MeshHandle{meshId};
    desc.material = arp::renderer::MaterialHandle{1};
    desc.localBounds = Bounds3D::explicitBounds(center, extents);
    return desc;
}

TerrainComponentDesc terrainDesc(
    uint32_t terrainId, arp::core::Vec3 center, arp::core::Vec3 extents)
{
    TerrainComponentDesc desc;
    desc.terrain = TerrainHandle{terrainId};
    desc.material = arp::renderer::MaterialHandle{2};
    desc.localBounds = Bounds3D::explicitBounds(center, extents);
    desc.metadata = TerrainMetadata::explicitGrid(8, 8, 1.0f);
    return desc;
}

arp::scene::CameraComponentDesc cameraDesc()
{
    auto desc = arp::scene::CameraComponentDesc::perspective(60.0f, 1.0f, 0.1f, 100.0f);
    desc.visibleQueues = arp::scene::RenderQueueMask::queue3D();
    return desc;
}

SceneTraversalSnapshot snapshotFor(Scene& scene)
{
    SceneTraversalOptions options;
    options.maxNodeCount = 32;
    auto snapshot = scene.extractTraversalSnapshot(options);
    assert(snapshot.ok());
    return snapshot;
}

const Scene3DCameraBinding& firstCamera(const Scene3DRenderQueue& queue)
{
    assert(!queue.cameras().empty());
    return queue.cameras()[0];
}

void visibility_culls_with_explicit_camera_and_bounds()
{
    Scene scene("visibility_scene");
    auto& cameraObject = scene.createObject("camera");
    auto& visibleMesh = scene.createObject("visible_mesh");
    auto& outsideMesh = scene.createObject("outside_mesh");
    auto& visibleTerrain = scene.createObject("visible_terrain");
    auto& behindMesh = scene.createObject("behind_mesh");
    auto& nearMesh = scene.createObject("near_mesh");
    auto& farMesh = scene.createObject("far_mesh");
    auto& disabledObject = scene.createObject("disabled_mesh_object");
    auto& disabledComponentObject = scene.createObject("disabled_mesh_component");

    assert(scene.reparent(cameraObject.id(), scene.rootId()).ok());
    assert(scene.reparent(visibleMesh.id(), scene.rootId()).ok());
    assert(scene.reparent(outsideMesh.id(), scene.rootId()).ok());
    assert(scene.reparent(visibleTerrain.id(), scene.rootId()).ok());
    assert(scene.reparent(behindMesh.id(), scene.rootId()).ok());
    assert(scene.reparent(nearMesh.id(), scene.rootId()).ok());
    assert(scene.reparent(farMesh.id(), scene.rootId()).ok());
    assert(scene.reparent(disabledObject.id(), scene.rootId()).ok());
    assert(scene.reparent(disabledComponentObject.id(), scene.rootId()).ok());

    cameraObject.addComponent<arp::scene::CameraComponent>(cameraDesc());
    visibleMesh.addComponent<MeshComponent>(meshDesc(1, {0.0f, 0.0f, 10.0f}, {1.0f, 1.0f, 1.0f}));
    outsideMesh.addComponent<MeshComponent>(meshDesc(2, {100.0f, 0.0f, 10.0f}, {1.0f, 1.0f, 1.0f}));
    visibleTerrain.addComponent<TerrainComponent>(
        terrainDesc(1, {2.0f, 0.0f, 20.0f}, {2.0f, 1.0f, 2.0f}));
    behindMesh.addComponent<MeshComponent>(meshDesc(3, {0.0f, 0.0f, -5.0f}, {0.5f, 0.5f, 0.5f}));
    nearMesh.addComponent<MeshComponent>(meshDesc(4, {0.0f, 0.0f, 0.01f}, {0.01f, 0.01f, 0.01f}));
    farMesh.addComponent<MeshComponent>(meshDesc(5, {0.0f, 0.0f, 150.0f}, {1.0f, 1.0f, 1.0f}));
    disabledObject.addComponent<MeshComponent>(
        meshDesc(6, {0.0f, 0.0f, 10.0f}, {1.0f, 1.0f, 1.0f}));
    disabledObject.setActive(false);
    auto& disabledComponent = disabledComponentObject.addComponent<MeshComponent>(
        meshDesc(7, {0.0f, 0.0f, 10.0f}, {1.0f, 1.0f, 1.0f}));
    disabledComponent.setEnabled(false);

    const auto snapshot = snapshotFor(scene);
    const auto resolvedCameras = arp::scene::CameraResolver().resolve(scene, snapshot);
    assert(resolvedCameras.ok());
    const auto extracted =
        Scene3DRenderExtractor().extract(scene, snapshot, resolvedCameras.cameras());
    assert(extracted.ok());

    const auto result =
        Scene3DVisibilitySystem().filterVisible(extracted.queue(), firstCamera(extracted.queue()));
    assert(result.ok());
    assert(result.telemetry().totalRenderables == 6);
    assert(result.telemetry().visibleRenderables == 2);
    assert(result.telemetry().culledRenderables == 4);
    assert(result.telemetry().invalidRenderables == 0);
    assert(result.telemetry().disabledRenderables == 2);

    const auto& visibleQueue = result.queue();
    assert(visibleQueue.participationSource() == Scene3DParticipationSource::ComponentDriven);
    assert(!visibleQueue.usesSceneModeFlag());
    assert(visibleQueue.items().size() == 2);
    assert(visibleQueue.items()[0].objectId == visibleMesh.id());
    assert(visibleQueue.items()[1].objectId == visibleTerrain.id());
}

void invalid_bounds_are_reported_deterministically()
{
    Scene3DRenderQueue queue;
    Scene3DRenderItem invalidItem;
    invalidItem.kind = Scene3DRenderItemKind::Mesh;
    invalidItem.mesh = arp::renderer::MeshHandle{9};
    invalidItem.material = arp::renderer::MaterialHandle{9};
    invalidItem.worldBounds = Bounds3D{};
    queue.addItem(invalidItem);

    Scene3DCameraBinding camera;
    camera.camera = cameraDesc().camera;
    camera.projection = arp::scene::CameraProjection::Perspective;

    const auto result = Scene3DVisibilitySystem().filterVisible(queue, camera);
    assert(!result.ok());
    assert(result.diagnostic().code == Scene3DDiagnosticCode::InvalidBounds);
    assert(result.telemetry().invalidRenderables == 1);
}

} // namespace

int main()
{
    visibility_culls_with_explicit_camera_and_bounds();
    invalid_bounds_are_reported_deterministically();
    return 0;
}
