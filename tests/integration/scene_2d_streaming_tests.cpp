#include "2d/scene_2d.h"
#include "scene/camera.h"
#include "scene/scene.h"

#include <algorithm>
#include <cassert>

namespace {

using namespace arp::scene;
using namespace arp::scene2d;

template <typename ChunkContainer>
bool containsChunk(const ChunkContainer& chunks, ChunkCoord coord)
{
    return std::find(chunks.begin(), chunks.end(), coord) != chunks.end();
}

SceneTraversalSnapshot snapshotFor(Scene& scene)
{
    SceneTraversalOptions options;
    options.maxNodeCount = 16;
    auto snapshot = scene.extractTraversalSnapshot(options);
    assert(snapshot.ok());
    return snapshot;
}

TileMapComponentDesc tileMapDesc()
{
    TileMapComponentDesc desc;
    desc.tileMap = TileMapHandle{4};
    desc.material = arp::renderer::MaterialHandle{5};
    desc.texture = arp::renderer::TextureHandle{6};
    desc.grid = Scene2DChunkGrid::explicitGrid({0.0f, 0.0f}, 16.0f, 4, 4);
    desc.metadata = TileMapMetadata::explicitTiles(8, 8, 1.0f);
    return desc;
}

SpriteRendererComponentDesc spriteDesc(uint32_t spriteId, arp::core::Vec3 center)
{
    SpriteRendererComponentDesc desc;
    desc.sprite = SpriteHandle{spriteId};
    desc.material = arp::renderer::MaterialHandle{7};
    desc.texture = arp::renderer::TextureHandle{8};
    desc.localBounds = Bounds2D::explicitBounds(center, {1.0f, 1.0f, 0.0f});
    return desc;
}

const ResolvedCamera& resolveSingleCamera(Scene& scene, const SceneTraversalSnapshot& snapshot)
{
    static CameraResolverResult resolved = CameraResolverResult::success({});
    resolved = CameraResolver().resolve(scene, snapshot);
    assert(resolved.ok());
    assert(resolved.cameras().size() == 1);
    return resolved.cameras()[0];
}

void addOrthographicCamera(Scene& scene, arp::core::Vec3 center, float height, float aspectRatio)
{
    auto& cameraObject = scene.createObject("camera");
    assert(scene.reparent(cameraObject.id(), scene.rootId()).ok());
    cameraObject.transform().setLocalTranslation(center);
    auto desc = CameraComponentDesc::orthographic(height, 0.1f, 100.0f);
    desc.aspectPolicy = CameraAspectPolicy::fixed(aspectRatio);
    desc.visibleQueues = RenderQueueMask::world2DAnd3D();
    cameraObject.addComponent<CameraComponent>(desc);
}

Scene2DStreamingRequest requestFor(const ResolvedCamera& camera)
{
    Scene2DStreamingRequest request;
    request.camera = camera;
    request.policy = Scene2DStreamingPolicy::explicitPolicy(1, 8);
    request.residentChunks = {ChunkCoord{0, 0}, ChunkCoord{3, 3}};
    return request;
}

void planner_builds_chunk_plan_and_render_queue_from_components()
{
    Scene scene("streaming_scene");
    auto& tileMapObject = scene.createObject("tile_map");
    auto& visibleSpriteObject = scene.createObject("visible_sprite");
    auto& hiddenSpriteObject = scene.createObject("hidden_sprite");
    assert(scene.reparent(tileMapObject.id(), scene.rootId()).ok());
    assert(scene.reparent(visibleSpriteObject.id(), scene.rootId()).ok());
    assert(scene.reparent(hiddenSpriteObject.id(), scene.rootId()).ok());
    addOrthographicCamera(scene, {8.0f, 8.0f, -10.0f}, 8.0f, 1.0f);

    tileMapObject.addComponent<TileMapComponent>(tileMapDesc());
    visibleSpriteObject.addComponent<SpriteRendererComponent>(spriteDesc(1, {8.0f, 8.0f, 0.0f}));
    hiddenSpriteObject.addComponent<SpriteRendererComponent>(spriteDesc(2, {80.0f, 80.0f, 0.0f}));

    const auto snapshot = snapshotFor(scene);
    const auto& camera = resolveSingleCamera(scene, snapshot);
    assert(camera.visibleQueues.includes2D());
    assert(camera.visibleQueues.includes3D());

    const auto result = Scene2DStreamingPlanner().plan(scene, snapshot, requestFor(camera));
    assert(result.ok());

    const auto& plan = result.chunkPlan();
    assert(containsChunk(plan.keep, ChunkCoord{0, 0}));
    assert(containsChunk(plan.unload, ChunkCoord{3, 3}));
    assert(containsChunk(plan.load, ChunkCoord{1, 0}));
    assert(containsChunk(plan.load, ChunkCoord{0, 1}));
    assert(containsChunk(plan.load, ChunkCoord{1, 1}));

    const auto& queue = result.queue();
    assert(queue.participationSource() == Scene2DParticipationSource::ComponentDriven);
    assert(!queue.usesSceneModeFlag());
    assert(queue.tileMapItemCount() == 4);
    assert(queue.spriteItemCount() == 1);
    assert(queue.items().front().kind == Scene2DRenderItemKind::TileMapChunk);
    assert(queue.items().back().kind == Scene2DRenderItemKind::Sprite);
    assert(queue.visibleSpriteCount() == 1);
    assert(queue.hiddenSpriteCount() == 1);
}

void viewport_pan_and_resize_change_chunk_plan_deterministically()
{
    Scene scene("viewport_change_scene");
    auto& tileMapObject = scene.createObject("tile_map");
    assert(scene.reparent(tileMapObject.id(), scene.rootId()).ok());
    addOrthographicCamera(scene, {8.0f, 8.0f, -10.0f}, 8.0f, 1.0f);
    tileMapObject.addComponent<TileMapComponent>(tileMapDesc());

    const auto snapshot = snapshotFor(scene);
    const auto& camera = resolveSingleCamera(scene, snapshot);
    const auto small = Scene2DStreamingPlanner().plan(scene, snapshot, requestFor(camera));
    assert(small.ok());

    Scene pannedScene("viewport_panned_scene");
    auto& pannedTileMap = pannedScene.createObject("tile_map");
    assert(pannedScene.reparent(pannedTileMap.id(), pannedScene.rootId()).ok());
    addOrthographicCamera(pannedScene, {40.0f, 8.0f, -10.0f}, 8.0f, 1.0f);
    pannedTileMap.addComponent<TileMapComponent>(tileMapDesc());
    const auto pannedSnapshot = snapshotFor(pannedScene);
    auto pannedRequest = requestFor(resolveSingleCamera(pannedScene, pannedSnapshot));
    pannedRequest.residentChunks = small.chunkPlan().allWantedChunks;
    const auto panned = Scene2DStreamingPlanner().plan(pannedScene, pannedSnapshot, pannedRequest);
    assert(panned.ok());
    assert(containsChunk(panned.chunkPlan().load, ChunkCoord{3, 0}));
    assert(containsChunk(panned.chunkPlan().unload, ChunkCoord{0, 0}));

    Scene resizedScene("viewport_resized_scene");
    auto& resizedTileMap = resizedScene.createObject("tile_map");
    assert(resizedScene.reparent(resizedTileMap.id(), resizedScene.rootId()).ok());
    addOrthographicCamera(resizedScene, {16.0f, 16.0f, -10.0f}, 32.0f, 1.0f);
    resizedTileMap.addComponent<TileMapComponent>(tileMapDesc());
    const auto resizedSnapshot = snapshotFor(resizedScene);
    auto resizedRequest = requestFor(resolveSingleCamera(resizedScene, resizedSnapshot));
    resizedRequest.policy = Scene2DStreamingPolicy::explicitPolicy(1, 16);
    const auto resized =
        Scene2DStreamingPlanner().plan(resizedScene, resizedSnapshot, resizedRequest);
    assert(resized.ok());
    assert(resized.chunkPlan().allWantedChunks.size() > small.chunkPlan().allWantedChunks.size());
}

void invalid_budget_and_tile_metadata_fail_with_diagnostics()
{
    Scene scene("invalid_budget_scene");
    auto& tileMapObject = scene.createObject("tile_map");
    assert(scene.reparent(tileMapObject.id(), scene.rootId()).ok());
    addOrthographicCamera(scene, {8.0f, 8.0f, -10.0f}, 8.0f, 1.0f);
    tileMapObject.addComponent<TileMapComponent>(tileMapDesc());

    const auto snapshot = snapshotFor(scene);
    auto invalidBudgetRequest = requestFor(resolveSingleCamera(scene, snapshot));
    invalidBudgetRequest.policy = Scene2DStreamingPolicy::explicitPolicy(1, 0);
    const auto invalidBudget =
        Scene2DStreamingPlanner().plan(scene, snapshot, invalidBudgetRequest);
    assert(!invalidBudget.ok());
    assert(invalidBudget.diagnostic().code == Scene2DDiagnosticCode::InvalidResidentChunkBudget);

    Scene invalidMetadataScene("invalid_metadata_scene");
    auto& invalidTileMapObject = invalidMetadataScene.createObject("tile_map");
    assert(invalidMetadataScene.reparent(invalidTileMapObject.id(), invalidMetadataScene.rootId())
            .ok());
    addOrthographicCamera(invalidMetadataScene, {8.0f, 8.0f, -10.0f}, 8.0f, 1.0f);
    auto invalidDesc = tileMapDesc();
    invalidDesc.metadata = TileMapMetadata{};
    invalidTileMapObject.addComponent<TileMapComponent>(invalidDesc);

    const auto invalidMetadata = Scene2DStreamingPlanner().plan(invalidMetadataScene,
        snapshotFor(invalidMetadataScene),
        requestFor(resolveSingleCamera(invalidMetadataScene, snapshotFor(invalidMetadataScene))));
    assert(!invalidMetadata.ok());
    assert(invalidMetadata.diagnostic().code == Scene2DDiagnosticCode::MissingTileMetadata);
}

void perspective_camera_with_2d_mask_requires_explicit_2d_projection_contract()
{
    Scene scene("perspective_2d_scene");
    auto& tileMapObject = scene.createObject("tile_map");
    auto& cameraObject = scene.createObject("camera");
    assert(scene.reparent(tileMapObject.id(), scene.rootId()).ok());
    assert(scene.reparent(cameraObject.id(), scene.rootId()).ok());
    tileMapObject.addComponent<TileMapComponent>(tileMapDesc());
    auto cameraDesc = CameraComponentDesc::perspective(60.0f, 1.0f, 0.1f, 100.0f);
    cameraDesc.visibleQueues = RenderQueueMask::world2DAnd3D();
    cameraObject.addComponent<CameraComponent>(cameraDesc);

    const auto snapshot = snapshotFor(scene);
    auto request = requestFor(resolveSingleCamera(scene, snapshot));
    const auto result = Scene2DStreamingPlanner().plan(scene, snapshot, request);
    assert(!result.ok());
    assert(result.diagnostic().code == Scene2DDiagnosticCode::Perspective2DProjectionUnsupported);
}

} // namespace

int main()
{
    planner_builds_chunk_plan_and_render_queue_from_components();
    viewport_pan_and_resize_change_chunk_plan_deterministically();
    invalid_budget_and_tile_metadata_fail_with_diagnostics();
    perspective_camera_with_2d_mask_requires_explicit_2d_projection_contract();
    return 0;
}
