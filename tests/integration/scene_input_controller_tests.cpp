#include "input/input.h"
#include "scene/camera.h"
#include "scene/scene.h"

#include <cassert>
#include <cmath>

namespace {

using namespace arp::input;
using namespace arp::scene;

bool nearly(float lhs, float rhs)
{
    return std::fabs(lhs - rhs) < 0.0001f;
}

InputActionMap gameplayMap()
{
    InputActionMap map("gameplay");
    assert(map.bindButton("jump", InputBinding::key(InputKey::Space)).ok());
    assert(map.bindButton("move_right", InputBinding::key(InputKey::D)).ok());
    assert(map.bindAxis1D("move_x", InputBinding::axis1D(InputKey::A, InputKey::D)).ok());
    assert(map.bindAxis1D("move_y", InputBinding::axis1D(InputKey::S, InputKey::W)).ok());
    assert(map.bindAxis1D("look_x", InputBinding::mouseDeltaX()).ok());
    assert(map.bindAxis1D("zoom", InputBinding::mouseWheel()).ok());
    return map;
}

void action_state_tracks_edges_holds_and_mouse_accumulation()
{
    InputSystem system(InputSystemDesc{2});
    assert(system.registerActionMap(gameplayMap()).ok());
    assert(system.bindPlayerActionMap(0, "gameplay").ok());

    InputFrame pressedFrame;
    pressedFrame.add(InputEvent::keyPressed(InputKey::D));
    pressedFrame.add(InputEvent::keyPressed(InputKey::Space));
    pressedFrame.add(InputEvent::mouseMoved(3.0f, -2.0f));
    pressedFrame.add(InputEvent::mouseMoved(2.0f, 1.0f));
    pressedFrame.add(InputEvent::mouseWheel(1.0f));
    pressedFrame.add(InputEvent::mouseWheel(-0.25f));
    assert(system.beginFrame(pressedFrame).ok());

    auto state = system.actionState(0);
    assert(state.ok());
    assert(state.held("move_right"));
    assert(state.pressed("jump"));
    assert(state.held("jump"));
    assert(nearly(state.axis("move_x"), 1.0f));
    assert(nearly(state.axis("look_x"), 5.0f));
    assert(nearly(state.axis("zoom"), 0.75f));
    assert(!state.exposesRawKeycodes());

    InputFrame heldFrame;
    assert(system.beginFrame(heldFrame).ok());
    state = system.actionState(0);
    assert(state.ok());
    assert(state.held("move_right"));
    assert(!state.pressed("jump"));
    assert(state.held("jump"));
    assert(nearly(state.axis("move_x"), 1.0f));
    assert(nearly(state.axis("look_x"), 0.0f));
    assert(nearly(state.axis("zoom"), 0.0f));

    InputFrame releasedFrame;
    releasedFrame.add(InputEvent::keyReleased(InputKey::D));
    releasedFrame.add(InputEvent::keyReleased(InputKey::Space));
    assert(system.beginFrame(releasedFrame).ok());
    state = system.actionState(0);
    assert(state.ok());
    assert(!state.held("move_right"));
    assert(state.released("jump"));
    assert(!state.held("jump"));
    assert(nearly(state.axis("move_x"), 0.0f));
}

void diagnostics_are_deterministic_for_invalid_input_contracts()
{
    InputSystem system(InputSystemDesc{1});
    assert(system.registerActionMap(gameplayMap()).ok());

    const auto invalidSlot = system.bindPlayerActionMap(4, "gameplay");
    assert(!invalidSlot.ok());
    assert(invalidSlot.diagnostic().code == InputDiagnosticCode::InvalidPlayerSlot);

    const auto missingMap = system.bindPlayerActionMap(0, "missing");
    assert(!missingMap.ok());
    assert(missingMap.diagnostic().code == InputDiagnosticCode::MissingActionMap);

    InputActionMap duplicate("duplicate");
    assert(duplicate.bindButton("jump", InputBinding::key(InputKey::Space)).ok());
    const auto duplicateBinding = duplicate.bindButton("jump", InputBinding::key(InputKey::W));
    assert(!duplicateBinding.ok());
    assert(duplicateBinding.diagnostic().code == InputDiagnosticCode::DuplicateBinding);
}

void scene_components_consume_actions_without_raw_input_or_camera_projection_coupling()
{
    Scene scene("input_controller_scene");
    auto& player = scene.createObject("player");
    auto& cameraObject = scene.createObject("follow_camera");
    assert(scene.reparent(player.id(), scene.rootId()).ok());
    assert(scene.reparent(cameraObject.id(), scene.rootId()).ok());

    InputSystem system(InputSystemDesc{2});
    assert(system.registerActionMap(gameplayMap()).ok());
    assert(system.bindPlayerActionMap(0, "gameplay").ok());

    PlayerInputComponentDesc playerInputDesc;
    playerInputDesc.inputSystem = &system;
    playerInputDesc.playerSlot = 0;
    playerInputDesc.actionMapId = "gameplay";
    auto& playerInput = player.addComponent<PlayerInputComponent>(playerInputDesc);

    CharacterControllerComponentDesc controllerDesc;
    controllerDesc.scene = &scene;
    controllerDesc.playerInput = &playerInput;
    controllerDesc.moveXAction = "move_x";
    controllerDesc.moveYAction = "move_y";
    controllerDesc.moveSpeed = 3.0f;
    auto& controller = player.addComponent<CharacterControllerComponent>(controllerDesc);

    auto cameraDesc = CameraComponentDesc::orthographic(12.0f, 0.1f, 100.0f);
    cameraDesc.visibleQueues = RenderQueueMask::world2DAnd3D();
    cameraObject.addComponent<CameraComponent>(cameraDesc);

    CameraFollowComponentDesc followDesc;
    followDesc.scene = &scene;
    followDesc.playerInput = &playerInput;
    followDesc.target = player.id();
    followDesc.lookXAction = "look_x";
    followDesc.zoomAction = "zoom";
    followDesc.followOffset = {0.0f, 4.0f, -10.0f};
    followDesc.lookSensitivity = 0.5f;
    auto& follow = cameraObject.addComponent<CameraFollowComponent>(followDesc);

    InputFrame frame;
    frame.add(InputEvent::keyPressed(InputKey::D));
    frame.add(InputEvent::mouseMoved(2.0f, 0.0f));
    frame.add(InputEvent::mouseWheel(1.0f));
    assert(system.beginFrame(frame).ok());

    SceneUpdateOptions options;
    options.deltaSeconds = 1.0f;
    options.frameIndex = 9;
    options.maxObjectCount = 8;
    const auto update = scene.update(options);
    assert(update.ok());

    assert(nearly(player.transform().localTransform().translation.x, 3.0f));
    assert(nearly(controller.lastMovement().x, 3.0f));
    assert(!controller.consumesRawInput());
    assert(!controller.readsCameraProjectionState());

    assert(follow.lateUpdateFrame() == 9);
    assert(nearly(follow.lastTargetTranslation().x, 3.0f));
    assert(nearly(follow.lastLookAxis(), 2.0f));
    assert(nearly(follow.lastZoomAxis(), 1.0f));
    assert(!follow.consumesRawInput());

    const auto cameraTranslation = cameraObject.transform().localTransform().translation;
    assert(nearly(cameraTranslation.x, 4.0f));
    assert(nearly(cameraTranslation.y, 4.0f));
    assert(nearly(cameraTranslation.z, -10.0f));

    const auto snapshot = scene.extractTraversalSnapshot(SceneTraversalOptions{8});
    assert(snapshot.ok());
    const auto resolved = CameraResolver().resolve(scene, snapshot);
    assert(resolved.ok());
    assert(resolved.cameras().size() == 1);
    assert(nearly(resolved.cameras()[0].worldTransform.translation.x, 4.0f));
}

} // namespace

int main()
{
    action_state_tracks_edges_holds_and_mouse_accumulation();
    diagnostics_are_deterministic_for_invalid_input_contracts();
    scene_components_consume_actions_without_raw_input_or_camera_projection_coupling();
    return 0;
}
