#include "2d/scene_2d.h"
#include "3d/animator_component.h"
#include "3d/scene_3d.h"
#include "animation/animation.h"
#include "input/input.h"
#include "renderer/scene_runtime_frame.h"
#include "renderer/scene_runtime_frame_rhi_planner.h"
#include "scene/camera.h"
#include "scene/scene.h"
#include "ui/ui.h"

#include <cassert>
#include <cstdio>

namespace {

class SmokeCounterComponent final : public arp::scene::Component {
public:
    void onStart(const arp::scene::SceneUpdateContext&) override { ++starts; }

    void onUpdate(const arp::scene::SceneUpdateContext&) override { ++updates; }

    void onLateUpdate(const arp::scene::SceneUpdateContext&) override { ++lateUpdates; }

    int starts = 0;
    int updates = 0;
    int lateUpdates = 0;
};

void minimal_scene_update_and_snapshot_smoke()
{
    arp::scene::Scene scene("runtime_smoke_scene");
    auto& parent = scene.createObject("smoke_parent");
    auto& child = scene.createObject("smoke_child");
    assert(scene.reparent(parent.id(), scene.rootId()).ok());
    assert(scene.reparent(child.id(), parent.id()).ok());

    parent.transform().setLocalTranslation({3.0f, 0.0f, 0.0f});
    child.transform().setLocalTranslation({0.0f, 4.0f, 0.0f});
    auto& counter = child.addComponent<SmokeCounterComponent>();

    arp::scene::SceneUpdateOptions updateOptions;
    updateOptions.deltaSeconds = 1.0f / 60.0f;
    updateOptions.frameIndex = 7;
    updateOptions.maxObjectCount = 8;
    const auto update = scene.update(updateOptions);
    assert(update.ok());
    assert(update.visitedObjectCount() == 3);
    assert(counter.starts == 1);
    assert(counter.updates == 1);
    assert(counter.lateUpdates == 1);

    arp::scene::SceneTraversalOptions traversalOptions;
    traversalOptions.maxNodeCount = 8;
    const auto snapshot = scene.extractTraversalSnapshot(traversalOptions);
    assert(snapshot.ok());
    assert(snapshot.nodes().size() == 3);
    assert(snapshot.nodes()[0].objectId == scene.rootId());
    assert(snapshot.nodes()[1].objectId == parent.id());
    assert(snapshot.nodes()[2].objectId == child.id());
    assert(snapshot.requireNode(child.id()).worldTransform.translation.x == 3.0f);
    assert(snapshot.requireNode(child.id()).worldTransform.translation.y == 4.0f);
    assert(!snapshot.exposesNativeBackendHandles());
    assert(!snapshot.exposesRawVertexBuffers());
    assert(snapshot.nativeBackendHandleCount() == 0);
    assert(snapshot.rawVertexBufferCount() == 0);
}

void three_d_component_extraction_smoke()
{
    arp::scene::Scene scene("runtime_3d_smoke_scene");
    auto& meshObject = scene.createObject("smoke_mesh");
    auto& culledMeshObject = scene.createObject("smoke_culled_mesh");
    auto& cameraObject = scene.createObject("smoke_camera");
    assert(scene.reparent(meshObject.id(), scene.rootId()).ok());
    assert(scene.reparent(culledMeshObject.id(), scene.rootId()).ok());
    assert(scene.reparent(cameraObject.id(), scene.rootId()).ok());

    meshObject.transform().setLocalTranslation({1.0f, 0.0f, 4.0f});
    culledMeshObject.transform().setLocalTranslation({80.0f, 0.0f, 4.0f});
    cameraObject.transform().setLocalTranslation({0.0f, 2.0f, -8.0f});

    arp::scene3d::MeshComponentDesc meshDesc;
    meshDesc.mesh = arp::renderer::MeshHandle{21};
    meshDesc.material = arp::renderer::MaterialHandle{22};
    meshDesc.localBounds =
        arp::scene3d::Bounds3D::explicitBounds({0.0f, 0.5f, 0.0f}, {0.5f, 1.0f, 0.5f});
    meshObject.addComponent<arp::scene3d::MeshComponent>(meshDesc);

    arp::scene3d::MeshComponentDesc culledMeshDesc = meshDesc;
    culledMeshDesc.mesh = arp::renderer::MeshHandle{23};
    culledMeshObject.addComponent<arp::scene3d::MeshComponent>(culledMeshDesc);

    static animation::AnimationClip clip;
    clip.name = "runtime_smoke_idle";
    clip.durationSeconds = 1.0f;
    arp::scene3d::AnimatorComponentDesc animatorDesc;
    animatorDesc.clip = &clip;
    meshObject.addComponent<arp::scene3d::AnimatorComponent>(animatorDesc);

    auto cameraDesc = arp::scene::CameraComponentDesc::orthographic(24.0f, 0.1f, 100.0f);
    cameraDesc.aspectPolicy = arp::scene::CameraAspectPolicy::fixed(1.0f);
    cameraDesc.visibleQueues = arp::scene::RenderQueueMask::world2DAnd3D();
    cameraObject.addComponent<arp::scene::CameraComponent>(cameraDesc);

    arp::scene::SceneTraversalOptions traversalOptions;
    traversalOptions.maxNodeCount = 8;
    const auto snapshot = scene.extractTraversalSnapshot(traversalOptions);
    assert(snapshot.ok());

    const auto resolvedCameras = arp::scene::CameraResolver().resolve(scene, snapshot);
    assert(resolvedCameras.ok());

    const auto extracted =
        arp::scene3d::Scene3DRenderExtractor().extract(scene, snapshot, resolvedCameras.cameras());
    assert(extracted.ok());
    assert(extracted.queue().items().size() == 2);
    assert(extracted.queue().cameraCount() == 1);
    assert(extracted.queue().animationBindingCount() == 1);
    assert(!extracted.queue().copiedStaticVertexDataInHotPath());
    assert(!extracted.queue().exposesNativeBackendHandles());

    const auto firstVisibility = arp::scene3d::Scene3DVisibilitySystem().filterVisible(
        extracted.queue(), extracted.queue().cameras()[0]);
    assert(firstVisibility.ok());
    assert(firstVisibility.telemetry().visibleRenderables == 1);
    assert(firstVisibility.telemetry().culledRenderables == 1);

    auto movedCamera = extracted.queue().cameras()[0];
    movedCamera.camera.position.x = 80.0f;
    const auto movedVisibility =
        arp::scene3d::Scene3DVisibilitySystem().filterVisible(extracted.queue(), movedCamera);
    assert(movedVisibility.ok());
    assert(movedVisibility.telemetry().visibleRenderables == 1);
    assert(movedVisibility.queue().items()[0].objectId == culledMeshObject.id());
}

void two_d_streaming_smoke()
{
    arp::scene::Scene scene("runtime_2d_smoke_scene");
    auto& tileMapObject = scene.createObject("smoke_tile_map");
    auto& spriteObject = scene.createObject("smoke_sprite");
    auto& cameraObject = scene.createObject("smoke_mixed_camera");
    assert(scene.reparent(tileMapObject.id(), scene.rootId()).ok());
    assert(scene.reparent(spriteObject.id(), scene.rootId()).ok());
    assert(scene.reparent(cameraObject.id(), scene.rootId()).ok());

    arp::scene2d::TileMapComponentDesc tileMapDesc;
    tileMapDesc.tileMap = arp::scene2d::TileMapHandle{3};
    tileMapDesc.material = arp::renderer::MaterialHandle{31};
    tileMapDesc.texture = arp::renderer::TextureHandle{32};
    tileMapDesc.grid = arp::scene2d::Scene2DChunkGrid::explicitGrid({0.0f, 0.0f}, 16.0f, 4, 4);
    tileMapDesc.metadata = arp::scene2d::TileMapMetadata::explicitTiles(8, 8, 1.0f);
    tileMapObject.addComponent<arp::scene2d::TileMapComponent>(tileMapDesc);

    arp::scene2d::SpriteRendererComponentDesc spriteDesc;
    spriteDesc.sprite = arp::scene2d::SpriteHandle{6};
    spriteDesc.material = arp::renderer::MaterialHandle{33};
    spriteDesc.texture = arp::renderer::TextureHandle{34};
    spriteDesc.localBounds =
        arp::scene2d::Bounds2D::explicitBounds({8.0f, 8.0f, 0.0f}, {1.0f, 1.0f, 0.0f});
    spriteObject.addComponent<arp::scene2d::SpriteRendererComponent>(spriteDesc);

    auto cameraDesc = arp::scene::CameraComponentDesc::orthographic(8.0f, 0.1f, 100.0f);
    cameraDesc.aspectPolicy = arp::scene::CameraAspectPolicy::fixed(1.0f);
    cameraDesc.visibleQueues = arp::scene::RenderQueueMask::world2DAnd3D();
    cameraObject.transform().setLocalTranslation({8.0f, 8.0f, -10.0f});
    cameraObject.addComponent<arp::scene::CameraComponent>(cameraDesc);

    arp::scene::SceneTraversalOptions traversalOptions;
    traversalOptions.maxNodeCount = 8;
    const auto snapshot = scene.extractTraversalSnapshot(traversalOptions);
    assert(snapshot.ok());
    const auto resolved = arp::scene::CameraResolver().resolve(scene, snapshot);
    assert(resolved.ok());
    assert(resolved.cameras().size() == 1);
    assert(resolved.cameras()[0].visibleQueues.includes2D());
    assert(resolved.cameras()[0].visibleQueues.includes3D());

    arp::scene2d::Scene2DStreamingRequest smallRequest;
    smallRequest.camera = resolved.cameras()[0];
    smallRequest.policy = arp::scene2d::Scene2DStreamingPolicy::explicitPolicy(1, 8);
    const auto small = arp::scene2d::Scene2DStreamingPlanner().plan(scene, snapshot, smallRequest);
    assert(small.ok());
    assert(small.queue().spriteItemCount() == 1);

    arp::scene2d::Scene2DStreamingRequest largeRequest;
    auto largeCamera = resolved.cameras()[0];
    largeCamera.camera.position = {16.0f, 16.0f, -10.0f};
    largeCamera.orthographicHeight = 32.0f;
    largeCamera.orthographicWidth = 32.0f;
    largeRequest.camera = largeCamera;
    largeRequest.policy = arp::scene2d::Scene2DStreamingPolicy::explicitPolicy(1, 16);
    largeRequest.residentChunks = small.chunkPlan().allWantedChunks;
    const auto large = arp::scene2d::Scene2DStreamingPlanner().plan(scene, snapshot, largeRequest);
    assert(large.ok());
    assert(large.queue().tileMapItemCount() > small.queue().tileMapItemCount());
    assert(!large.chunkPlan().load.empty());
}

void input_controller_camera_follow_smoke()
{
    arp::scene::Scene scene("runtime_input_smoke_scene");
    auto& player = scene.createObject("smoke_player");
    auto& cameraObject = scene.createObject("smoke_input_camera");
    auto& tileMapObject = scene.createObject("smoke_input_tile_map");
    assert(scene.reparent(player.id(), scene.rootId()).ok());
    assert(scene.reparent(cameraObject.id(), scene.rootId()).ok());
    assert(scene.reparent(tileMapObject.id(), scene.rootId()).ok());

    arp::input::InputActionMap map("gameplay");
    assert(
        map.bindAxis1D("move_x",
               arp::input::InputBinding::axis1D(arp::input::InputKey::A, arp::input::InputKey::D))
            .ok());
    assert(
        map.bindAxis1D("move_y",
               arp::input::InputBinding::axis1D(arp::input::InputKey::S, arp::input::InputKey::W))
            .ok());
    assert(map.bindAxis1D("look_x", arp::input::InputBinding::mouseDeltaX()).ok());
    assert(map.bindAxis1D("zoom", arp::input::InputBinding::mouseWheel()).ok());

    arp::input::InputSystem inputSystem(arp::input::InputSystemDesc{1});
    assert(inputSystem.registerActionMap(map).ok());
    assert(inputSystem.bindPlayerActionMap(0, "gameplay").ok());

    arp::input::PlayerInputComponentDesc playerInputDesc;
    playerInputDesc.inputSystem = &inputSystem;
    playerInputDesc.playerSlot = 0;
    playerInputDesc.actionMapId = "gameplay";
    auto& playerInput = player.addComponent<arp::input::PlayerInputComponent>(playerInputDesc);

    arp::input::CharacterControllerComponentDesc controllerDesc;
    controllerDesc.scene = &scene;
    controllerDesc.playerInput = &playerInput;
    controllerDesc.moveXAction = "move_x";
    controllerDesc.moveYAction = "move_y";
    controllerDesc.moveSpeed = 2.0f;
    player.addComponent<arp::input::CharacterControllerComponent>(controllerDesc);

    auto cameraDesc = arp::scene::CameraComponentDesc::orthographic(16.0f, 0.1f, 100.0f);
    cameraDesc.visibleQueues = arp::scene::RenderQueueMask::world2DAnd3D();
    cameraObject.addComponent<arp::scene::CameraComponent>(cameraDesc);

    arp::input::CameraFollowComponentDesc followDesc;
    followDesc.scene = &scene;
    followDesc.playerInput = &playerInput;
    followDesc.target = player.id();
    followDesc.lookXAction = "look_x";
    followDesc.zoomAction = "zoom";
    followDesc.followOffset = {0.0f, 6.0f, -10.0f};
    followDesc.lookSensitivity = 0.25f;
    cameraObject.addComponent<arp::input::CameraFollowComponent>(followDesc);

    arp::scene2d::TileMapComponentDesc tileMapDesc;
    tileMapDesc.tileMap = arp::scene2d::TileMapHandle{9};
    tileMapDesc.material = arp::renderer::MaterialHandle{91};
    tileMapDesc.texture = arp::renderer::TextureHandle{92};
    tileMapDesc.grid = arp::scene2d::Scene2DChunkGrid::explicitGrid({0.0f, 0.0f}, 16.0f, 2, 2);
    tileMapDesc.metadata = arp::scene2d::TileMapMetadata::explicitTiles(8, 8, 1.0f);
    tileMapObject.addComponent<arp::scene2d::TileMapComponent>(tileMapDesc);

    arp::scene3d::MeshComponentDesc meshDesc;
    meshDesc.mesh = arp::renderer::MeshHandle{61};
    meshDesc.material = arp::renderer::MaterialHandle{62};
    meshDesc.localBounds =
        arp::scene3d::Bounds3D::explicitBounds({0.0f, 0.5f, 0.0f}, {0.5f, 1.0f, 0.5f});
    player.addComponent<arp::scene3d::MeshComponent>(meshDesc);

    arp::input::InputFrame inputFrame;
    inputFrame.add(arp::input::InputEvent::keyPressed(arp::input::InputKey::D));
    inputFrame.add(arp::input::InputEvent::mouseMoved(4.0f, 0.0f));
    assert(inputSystem.beginFrame(inputFrame).ok());

    arp::scene::SceneUpdateOptions updateOptions;
    updateOptions.deltaSeconds = 1.0f;
    updateOptions.frameIndex = 12;
    updateOptions.maxObjectCount = 8;
    const auto update = scene.update(updateOptions);
    assert(update.ok());
    assert(player.transform().localTransform().translation.x == 2.0f);
    assert(cameraObject.transform().localTransform().translation.x == 3.0f);

    arp::scene::SceneTraversalOptions traversalOptions;
    traversalOptions.maxNodeCount = 8;
    const auto snapshot = scene.extractTraversalSnapshot(traversalOptions);
    assert(snapshot.ok());
    const auto resolved = arp::scene::CameraResolver().resolve(scene, snapshot);
    assert(resolved.ok());
    assert(resolved.cameras().size() == 1);

    const auto extracted3d =
        arp::scene3d::Scene3DRenderExtractor().extract(scene, snapshot, resolved.cameras(), true);
    if (!extracted3d.ok())
        std::fprintf(stderr, "%s\n", extracted3d.diagnostic().message.c_str());
    assert(extracted3d.ok());
    assert(extracted3d.queue().items().size() == 1);

    arp::scene2d::Scene2DStreamingRequest request;
    request.camera = resolved.cameras()[0];
    request.policy = arp::scene2d::Scene2DStreamingPolicy::explicitPolicy(0, 4);
    // The 2D sink strictly rejects foreign domain payloads. Supply the fixture's
    // explicitly identified tile-map node, retaining its resolved scene identity.
    const auto mixed2d = arp::scene2d::Scene2DStreamingPlanner().plan(scene, snapshot, request);
    assert(!mixed2d.ok() && !mixed2d.diagnostic().message.empty());
    const auto tileSnapshot =
        arp::scene::SceneTraversalSnapshot::success({snapshot.requireNode(tileMapObject.id())});
    const auto planned2d =
        arp::scene2d::Scene2DStreamingPlanner().plan(scene, tileSnapshot, request);
    assert(planned2d.ok());
    assert(planned2d.queue().tileMapItemCount() > 0);
}

void ui_input_routing_and_render_queue_smoke()
{
    arp::scene::Scene scene("runtime_ui_smoke_scene");
    auto& player = scene.createObject("smoke_ui_player");
    auto& cameraObject = scene.createObject("smoke_ui_camera");
    auto& tileMapObject = scene.createObject("smoke_ui_tile_map");
    assert(scene.reparent(player.id(), scene.rootId()).ok());
    assert(scene.reparent(cameraObject.id(), scene.rootId()).ok());
    assert(scene.reparent(tileMapObject.id(), scene.rootId()).ok());

    arp::input::InputActionMap map("gameplay");
    assert(
        map.bindAxis1D("move_x",
               arp::input::InputBinding::axis1D(arp::input::InputKey::A, arp::input::InputKey::D))
            .ok());
    assert(map.bindAxis1D("look_x", arp::input::InputBinding::mouseDeltaX()).ok());

    arp::input::InputSystem inputSystem(arp::input::InputSystemDesc{1});
    assert(inputSystem.registerActionMap(map).ok());
    assert(inputSystem.bindPlayerActionMap(0, "gameplay").ok());

    arp::input::PlayerInputComponentDesc playerInputDesc;
    playerInputDesc.inputSystem = &inputSystem;
    playerInputDesc.playerSlot = 0;
    playerInputDesc.actionMapId = "gameplay";
    auto& playerInput = player.addComponent<arp::input::PlayerInputComponent>(playerInputDesc);

    arp::input::CharacterControllerComponentDesc controllerDesc;
    controllerDesc.scene = &scene;
    controllerDesc.playerInput = &playerInput;
    controllerDesc.moveXAction = "move_x";
    controllerDesc.moveSpeed = 2.0f;
    player.addComponent<arp::input::CharacterControllerComponent>(controllerDesc);

    auto cameraDesc = arp::scene::CameraComponentDesc::orthographic(18.0f, 0.1f, 100.0f);
    cameraDesc.aspectPolicy = arp::scene::CameraAspectPolicy::fixed(1.0f);
    cameraDesc.visibleQueues = arp::scene::RenderQueueMask::world2DAnd3D();
    cameraObject.addComponent<arp::scene::CameraComponent>(cameraDesc);

    arp::input::CameraFollowComponentDesc followDesc;
    followDesc.scene = &scene;
    followDesc.playerInput = &playerInput;
    followDesc.target = player.id();
    followDesc.lookXAction = "look_x";
    followDesc.followOffset = {0.0f, 6.0f, -10.0f};
    followDesc.lookSensitivity = 0.5f;
    cameraObject.addComponent<arp::input::CameraFollowComponent>(followDesc);

    arp::scene2d::TileMapComponentDesc tileMapDesc;
    tileMapDesc.tileMap = arp::scene2d::TileMapHandle{19};
    tileMapDesc.material = arp::renderer::MaterialHandle{191};
    tileMapDesc.texture = arp::renderer::TextureHandle{192};
    tileMapDesc.grid = arp::scene2d::Scene2DChunkGrid::explicitGrid({0.0f, 0.0f}, 16.0f, 2, 2);
    tileMapDesc.metadata = arp::scene2d::TileMapMetadata::explicitTiles(8, 8, 1.0f);
    tileMapObject.addComponent<arp::scene2d::TileMapComponent>(tileMapDesc);

    arp::scene3d::MeshComponentDesc meshDesc;
    meshDesc.mesh = arp::renderer::MeshHandle{71};
    meshDesc.material = arp::renderer::MaterialHandle{72};
    meshDesc.localBounds =
        arp::scene3d::Bounds3D::explicitBounds({0.0f, 0.5f, 0.0f}, {0.5f, 1.0f, 0.5f});
    player.addComponent<arp::scene3d::MeshComponent>(meshDesc);

    arp::ui::CanvasDesc canvasDesc;
    canvasDesc.viewport = arp::ui::UiViewport::explicitPixels(320.0f, 180.0f);
    canvasDesc.safeArea = arp::ui::UiSafeArea::explicitInsets(0.0f, 0.0f, 0.0f, 0.0f);
    canvasDesc.scalePolicy = arp::ui::UiScalePolicy::fixed(1.0f);
    canvasDesc.maxWidgetCount = 8;
    arp::ui::Runtime runtime("smoke_hud", canvasDesc);
    auto& canvas = runtime.canvas();

    arp::ui::WidgetDesc buttonDesc;
    buttonDesc.name = "smoke_button";
    buttonDesc.localRect = arp::ui::LayoutRect::fromOriginSize(24.0f, 18.0f, 80.0f, 24.0f);
    buttonDesc.material = arp::renderer::MaterialHandle{501};
    buttonDesc.texture = arp::renderer::TextureHandle{502};
    buttonDesc.order = 1;
    buttonDesc.acceptsFocus = true;
    arp::ui::ControlDesc buttonControlDesc;
    buttonControlDesc.type = arp::ui::UiControlType::TextInput;
    buttonControlDesc.widget = buttonDesc;
    const auto buttonId = runtime.createControl(buttonControlDesc, runtime.rootId()).widgetId();
    auto& button = *canvas.findWidget(buttonId);
    assert(canvas.reparent(button.id(), canvas.rootId()).ok());

    arp::ui::WidgetDesc nameplateDesc;
    nameplateDesc.name = "smoke_nameplate";
    nameplateDesc.localRect = arp::ui::LayoutRect::fromOriginSize(0.0f, 0.0f, 64.0f, 12.0f);
    nameplateDesc.material = arp::renderer::MaterialHandle{503};
    nameplateDesc.texture = arp::renderer::TextureHandle{504};
    nameplateDesc.order = 2;
    arp::ui::ControlDesc nameplateControlDesc;
    nameplateControlDesc.type = arp::ui::UiControlType::Panel;
    nameplateControlDesc.widget = nameplateDesc;
    const auto nameplateId =
        runtime.createControl(nameplateControlDesc, runtime.rootId()).widgetId();
    auto& nameplate = *canvas.findWidget(nameplateId);
    assert(canvas.reparent(nameplate.id(), canvas.rootId()).ok());

    arp::ui::WorldAnchorComponentDesc anchorDesc;
    anchorDesc.widget = nameplate.id();
    anchorDesc.pixelSize = {64.0f, 12.0f};
    anchorDesc.viewportOffset = {0.0f, -8.0f};
    player.addComponent<arp::ui::WorldAnchorComponent>(anchorDesc);

    arp::input::InputFrame inputFrame;
    inputFrame.add(arp::input::InputEvent::pointerMoved(32.0f, 22.0f));
    inputFrame.add(arp::input::InputEvent::mouseMoved(8.0f, 0.0f));
    inputFrame.add(arp::input::InputEvent::mouseButtonPressed(arp::input::InputMouseButton::Left));
    inputFrame.add(arp::input::InputEvent::textInput("x"));

    const auto layout = canvas.resolveLayout();
    assert(layout.ok());
    arp::ui::InputRouter router;
    const auto routed = router.dispatchFrame(runtime, inputFrame, {0, 16});
    assert(routed.ok());
    assert(routed.eventConsumption[2].pointerConsumed());
    assert(routed.eventConsumption[3].textConsumed());
    assert(runtime.focusedControl() == button.id());
    assert(inputSystem.beginFrame(inputFrame, routed.eventConsumption).ok());

    arp::scene::SceneUpdateOptions updateOptions;
    updateOptions.deltaSeconds = 1.0f;
    updateOptions.frameIndex = 18;
    updateOptions.maxObjectCount = 8;
    const auto update = scene.update(updateOptions);
    assert(update.ok());
    assert(player.transform().localTransform().translation.x == 0.0f);
    // Motion preceding the UI press remains game-owned.
    assert(!routed.eventConsumption[1].pointerConsumed());
    assert(cameraObject.transform().localTransform().translation.x == 4.0f);

    arp::scene::SceneTraversalOptions traversalOptions;
    traversalOptions.maxNodeCount = 8;
    const auto snapshot = scene.extractTraversalSnapshot(traversalOptions);
    assert(snapshot.ok());
    const auto resolved = arp::scene::CameraResolver().resolve(scene, snapshot);
    assert(resolved.ok());
    assert(resolved.cameras().size() == 1);

    const auto anchors =
        arp::ui::WorldAnchorProjector().project(scene, snapshot, resolved.cameras()[0], canvas);
    assert(anchors.ok());
    assert(anchors.anchors().size() == 1);
    assert(anchors.anchors()[0].widgetId == nameplate.id());

    const auto extracted3d =
        arp::scene3d::Scene3DRenderExtractor().extract(scene, snapshot, resolved.cameras(), true);
    if (!extracted3d.ok())
        std::fprintf(stderr, "%s\n", extracted3d.diagnostic().message.c_str());
    assert(extracted3d.ok());
    assert(extracted3d.queue().items().size() == 1);

    arp::scene2d::Scene2DStreamingRequest request;
    request.camera = resolved.cameras()[0];
    request.policy = arp::scene2d::Scene2DStreamingPolicy::explicitPolicy(0, 4);
    // The 2D sink strictly rejects foreign domain payloads. Supply the fixture's
    // explicitly identified tile-map node, retaining its resolved scene identity.
    const auto mixed2d = arp::scene2d::Scene2DStreamingPlanner().plan(scene, snapshot, request);
    assert(!mixed2d.ok() && !mixed2d.diagnostic().message.empty());
    const auto tileSnapshot =
        arp::scene::SceneTraversalSnapshot::success({snapshot.requireNode(tileMapObject.id())});
    const auto planned2d =
        arp::scene2d::Scene2DStreamingPlanner().plan(scene, tileSnapshot, request);
    assert(planned2d.ok());
    assert(planned2d.queue().tileMapItemCount() > 0);

    const auto extractedUi = arp::ui::RenderExtractor().extract(canvas, layout);
    assert(extractedUi.ok());
    assert(extractedUi.queue().items().size() == 2);
    assert(!extractedUi.queue().exposesNativeBackendHandles());
    assert(!extractedUi.queue().generatesRawStaticVertexDataInSceneHotPath());

    arp::renderer::RendererSceneRuntimeFrameInput frameInput;
    frameInput.snapshot = &snapshot;
    frameInput.resolvedCameras = &resolved.cameras();
    frameInput.scene3DQueue = &extracted3d.queue();
    frameInput.scene2DQueue = &planned2d.queue();
    frameInput.uiQueue = &extractedUi.queue();
    frameInput.oneCompatibleWorldUiPassIntent = true;

    arp::renderer::RendererSceneRuntimeFrameBuilder frameBuilder({3, 32, 32, 32});
    const auto rendererFrame = frameBuilder.build(frameInput);
    assert(rendererFrame.ok());
    assert(rendererFrame.frame().queueRecords().size() == 3);
    assert(rendererFrame.frame().queueRecords()[0].kind
        == arp::renderer::RendererSceneRuntimeQueueKind::Scene3D);
    assert(rendererFrame.frame().queueRecords()[1].kind
        == arp::renderer::RendererSceneRuntimeQueueKind::Scene2D);
    assert(rendererFrame.frame().queueRecords()[2].kind
        == arp::renderer::RendererSceneRuntimeQueueKind::UI);
    assert(rendererFrame.frame().compatibleOnePassIntent());
    assert(!rendererFrame.frame().exposesBackendHandles());
    assert(!rendererFrame.frame().copiesStaticGeometryPayloadInHotPath());

    arp::renderer::RendererSceneRuntimeFrameRhiPlanner rhiPlanner({0, 2, 2, 32, 128, 256});
    const auto rhiPlan = rhiPlanner.plan(rendererFrame.frame());
    assert(rhiPlan.ok());
    assert(rhiPlan.framePlan().drawPlans.size() == 1);
    assert(rhiPlan.framePlan().drawPlans[0].workKind == arp::renderer::RendererRhiWorkKind::Scene);
    assert(!rhiPlan.nativeHandleExposedToUpperLayer());
}

} // namespace

int main()
{
    minimal_scene_update_and_snapshot_smoke();
    three_d_component_extraction_smoke();
    two_d_streaming_smoke();
    input_controller_camera_follow_smoke();
    ui_input_routing_and_render_queue_smoke();
    return 0;
}
