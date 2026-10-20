#include "3d/animator_component.h"
#include "3d/scene_3d.h"
#include "animation/animation.h"
#include "animation/animator_controller.h"
#include "scene/camera.h"
#include "scene/scene.h"

#include <cassert>
#include <filesystem>
#include <fstream>

namespace {

using namespace arp::scene;
using namespace arp::scene3d;

bool nearlyEqual(float lhs, float rhs)
{
    const auto delta = lhs - rhs;
    return delta > -0.0001f && delta < 0.0001f;
}

void assertVec3(arp::core::Vec3 actual, arp::core::Vec3 expected)
{
    assert(nearlyEqual(actual.x, expected.x));
    assert(nearlyEqual(actual.y, expected.y));
    assert(nearlyEqual(actual.z, expected.z));
}

SceneTraversalSnapshot snapshotFor(Scene& scene)
{
    SceneTraversalOptions options;
    options.maxNodeCount = 16;
    auto snapshot = scene.extractTraversalSnapshot(options);
    assert(snapshot.ok());
    return snapshot;
}

MeshComponentDesc meshDesc()
{
    MeshComponentDesc desc;
    desc.mesh = arp::renderer::MeshHandle{11};
    desc.material = arp::renderer::MaterialHandle{7};
    desc.localBounds = Bounds3D::explicitBounds({0.0f, 1.0f, 0.0f}, {1.0f, 2.0f, 1.0f});
    return desc;
}

TerrainComponentDesc terrainDesc()
{
    TerrainComponentDesc desc;
    desc.terrain = TerrainHandle{5};
    desc.material = arp::renderer::MaterialHandle{8};
    desc.localBounds = Bounds3D::explicitBounds({10.0f, 0.0f, 10.0f}, {16.0f, 2.0f, 16.0f});
    desc.metadata = TerrainMetadata::explicitGrid(32, 32, 1.0f);
    return desc;
}

arp::scene::CameraComponentDesc cameraDesc()
{
    auto desc = arp::scene::CameraComponentDesc::perspective(60.0f, 16.0f / 9.0f, 0.1f, 500.0f);
    desc.visibleQueues = arp::scene::RenderQueueMask::queue3D();
    return desc;
}

arp::core::pmr::String readFile(const std::filesystem::path& path)
{
    std::ifstream input(path);
    assert(input.good());

    arp::core::pmr::String content(&arp::core::defaultCpuMemoryResource());
    char ch = '\0';
    while (input.get(ch)) {
        content.push_back(ch);
    }
    return content;
}

AnimatorComponentDesc animatorDesc()
{
    static animation::AnimationClip clip;
    clip.name = "idle";
    clip.durationSeconds = 1.0f;

    static animation::AnimatorControllerAsset controller;
    controller.name = "humanoid_controller";
    controller.defaultStateName = "idle";

    AnimatorComponentDesc desc;
    desc.clip = &clip;
    desc.controller = &controller;
    return desc;
}

void assertAnimatorComponentIsTheOnlyAnimationComponent()
{
    const std::filesystem::path sourceRoot = ARP_SOURCE_DIR;
    const auto componentsHeader = readFile(sourceRoot / "src/3d/components.h");
    const auto componentsSource = readFile(sourceRoot / "src/3d/components.cpp");
    const auto extractorHeader = readFile(sourceRoot / "src/3d/scene_3d_render_extractor.h");
    const auto extractorSource = readFile(sourceRoot / "src/3d/scene_3d_render_extractor.cpp");

    // The 3D module exposes AnimatorComponent (not the legacy AnimationComponent).
    assert(componentsHeader.find("AnimationComponentDesc") == arp::core::pmr::String::npos);
    assert(componentsHeader.find("class AnimationComponent") == arp::core::pmr::String::npos);
    assert(componentsSource.find("AnimationComponent::") == arp::core::pmr::String::npos);
    assert(extractorHeader.find("AnimationComponent") == arp::core::pmr::String::npos);
    assert(extractorSource.find("AnimationComponent") == arp::core::pmr::String::npos);

    // The extractor must NOT special-case component types via dynamic_cast or
    // an is3DComponent() predicate — it dispatches polymorphically through
    // Component::extract() and routes by typeId contract. AnimatorComponent's
    // data reaches the queue via the "scene3d.animation" typeId, not via a
    // direct type reference in the extractor source.
    assert(extractorSource.find("dynamic_cast") == arp::core::pmr::String::npos);
    assert(extractorSource.find("is3DComponent") == arp::core::pmr::String::npos);
    assert(extractorSource.find("AnimatorComponent") == arp::core::pmr::String::npos);
    assert(extractorSource.find("scene3d.animation") != arp::core::pmr::String::npos);
    assert(extractorSource.find("scene3d.mesh") != arp::core::pmr::String::npos);
    assert(extractorSource.find("scene3d.terrain") != arp::core::pmr::String::npos);

    // Each renderable 3D component must override extract() (self-describing).
    assert(componentsHeader.find("void extract(const arp::scene::RenderExtractContext&")
        != arp::core::pmr::String::npos);
}

void extracts_3d_render_queue_from_component_driven_snapshot()
{
    Scene scene("extract_scene");
    auto& meshObject = scene.createObject("mesh_object");
    auto& terrainObject = scene.createObject("terrain_object");
    auto& cameraObject = scene.createObject("camera_object");
    auto& plainObject = scene.createObject("plain_object");
    assert(scene.reparent(meshObject.id(), scene.rootId()).ok());
    assert(scene.reparent(terrainObject.id(), scene.rootId()).ok());
    assert(scene.reparent(cameraObject.id(), scene.rootId()).ok());
    assert(scene.reparent(plainObject.id(), scene.rootId()).ok());

    meshObject.transform().setLocalTranslation({2.0f, 0.0f, 0.0f});
    terrainObject.transform().setLocalTranslation({-4.0f, 0.0f, 0.0f});
    cameraObject.transform().setLocalTranslation({0.0f, 3.0f, -8.0f});

    auto& mesh = meshObject.addComponent<MeshComponent>(meshDesc());
    auto& animation = meshObject.addComponent<AnimatorComponent>(animatorDesc());
    auto& terrain = terrainObject.addComponent<TerrainComponent>(terrainDesc());
    cameraObject.addComponent<arp::scene::CameraComponent>(cameraDesc());

    const auto snapshot = snapshotFor(scene);
    const auto resolvedCameras = arp::scene::CameraResolver().resolve(scene, snapshot);
    assert(resolvedCameras.ok());
    const auto result =
        Scene3DRenderExtractor().extract(scene, snapshot, resolvedCameras.cameras());
    assert(result.ok());

    const auto& queue = result.queue();
    assert(queue.participationSource() == Scene3DParticipationSource::ComponentDriven);
    assert(!queue.usesSceneModeFlag());
    assert(queue.items().size() == 2);
    assert(queue.meshItemCount() == 1);
    assert(queue.terrainItemCount() == 1);
    assert(queue.cameraCount() == 1);
    assert(queue.animationBindingCount() == 1);
    assert(!queue.copiedStaticVertexDataInHotPath());
    assert(queue.staticVertexDataCopyCount() == 0);
    assert(!queue.exposesNativeBackendHandles());
    assert(queue.nativeBackendHandleCount() == 0);

    const auto& meshItem = queue.items()[0];
    assert(meshItem.kind == Scene3DRenderItemKind::Mesh);
    assert(meshItem.objectId == meshObject.id());
    assert(meshItem.mesh == mesh.desc().mesh);
    assert(meshItem.material == mesh.desc().material);
    assert(meshItem.sortKey < queue.items()[1].sortKey);
    assertVec3(meshItem.worldTransform.translation, {2.0f, 0.0f, 0.0f});
    assertVec3(meshItem.worldBounds.center, {2.0f, 1.0f, 0.0f});

    const auto& terrainItem = queue.items()[1];
    assert(terrainItem.kind == Scene3DRenderItemKind::Terrain);
    assert(terrainItem.objectId == terrainObject.id());
    assert(terrainItem.terrain == terrain.desc().terrain);
    assert(terrainItem.material == terrain.desc().material);
    assertVec3(terrainItem.worldBounds.center, {6.0f, 0.0f, 10.0f});

    assert(queue.cameras()[0].objectId == cameraObject.id());
    assert(queue.cameras()[0].projection == arp::scene::CameraProjection::Perspective);
    assert(queue.cameras()[0].visibleQueues.includes3D());
    assert(queue.animations()[0].objectId == meshObject.id());
    assert(queue.animations()[0].clip == animation.desc().clip);
    assert(!animation.parsesSourceAssetsInUpdatePath());
}

void skips_disabled_objects_and_components_without_scene_modes()
{
    Scene scene("disabled_scene");
    auto& disabledObject = scene.createObject("disabled_mesh_object");
    auto& disabledComponentObject = scene.createObject("disabled_mesh_component");
    assert(scene.reparent(disabledObject.id(), scene.rootId()).ok());
    assert(scene.reparent(disabledComponentObject.id(), scene.rootId()).ok());

    disabledObject.addComponent<MeshComponent>(meshDesc());
    disabledObject.setActive(false);

    auto& disabledMesh = disabledComponentObject.addComponent<MeshComponent>(meshDesc());
    disabledMesh.setEnabled(false);

    const auto result = Scene3DRenderExtractor().extract(scene, snapshotFor(scene));
    assert(result.ok());
    assert(result.queue().items().empty());
    assert(result.queue().disabledComponentCount() == 2);
    assert(result.queue().participationSource() == Scene3DParticipationSource::ComponentDriven);
}

void reports_missing_mesh_bounds_and_handles_deterministically()
{
    Scene missingBoundsScene("missing_bounds");
    auto& missingBoundsObject = missingBoundsScene.createObject("mesh_without_bounds");
    assert(missingBoundsScene.reparent(missingBoundsObject.id(), missingBoundsScene.rootId()).ok());
    MeshComponentDesc missingBounds = meshDesc();
    missingBounds.localBounds = Bounds3D{};
    missingBoundsObject.addComponent<MeshComponent>(missingBounds);

    const auto missingBoundsResult =
        Scene3DRenderExtractor().extract(missingBoundsScene, snapshotFor(missingBoundsScene));
    assert(!missingBoundsResult.ok());
    assert(missingBoundsResult.diagnostic().code == Scene3DDiagnosticCode::MissingBounds);

    Scene missingHandleScene("missing_handle");
    auto& missingHandleObject = missingHandleScene.createObject("mesh_without_handle");
    assert(missingHandleScene.reparent(missingHandleObject.id(), missingHandleScene.rootId()).ok());
    MeshComponentDesc missingHandle = meshDesc();
    missingHandle.mesh = arp::renderer::MeshHandle{};
    missingHandleObject.addComponent<MeshComponent>(missingHandle);

    const auto missingHandleResult =
        Scene3DRenderExtractor().extract(missingHandleScene, snapshotFor(missingHandleScene));
    assert(!missingHandleResult.ok());
    assert(missingHandleResult.diagnostic().code == Scene3DDiagnosticCode::MissingRendererHandle);
}

void reports_missing_terrain_metadata_deterministically()
{
    Scene scene("missing_terrain_metadata");
    auto& terrainObject = scene.createObject("terrain_without_metadata");
    assert(scene.reparent(terrainObject.id(), scene.rootId()).ok());
    TerrainComponentDesc desc = terrainDesc();
    desc.metadata = TerrainMetadata{};
    terrainObject.addComponent<TerrainComponent>(desc);

    const auto result = Scene3DRenderExtractor().extract(scene, snapshotFor(scene));
    assert(!result.ok());
    assert(result.diagnostic().code == Scene3DDiagnosticCode::MissingTerrainMetadata);
}

} // namespace

int main()
{
    assertAnimatorComponentIsTheOnlyAnimationComponent();
    extracts_3d_render_queue_from_component_driven_snapshot();
    skips_disabled_objects_and_components_without_scene_modes();
    reports_missing_mesh_bounds_and_handles_deterministically();
    reports_missing_terrain_metadata_deterministically();
    return 0;
}
