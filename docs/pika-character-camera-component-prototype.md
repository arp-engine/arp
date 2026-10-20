# Pika Character / Camera Component Prototype

Status: Prototype Complete for character/camera ownership. Backend visual
evidence exists for the current visible Vulkan path, but full product submission
unification remains a Phase 4 task.

## Runtime Objects

- `PikaPlayer` is the formal scene object for the Pika player character.
- `PikaCameraRig` is the formal scene object for the preview camera rig.
- `SceneBuilder` now creates both objects inside `RuntimeScene`.

## Components

- `CharacterComponent` owns the Pika character identity, explicit mesh/material/
  animation source bindings, facing, and movement observation hooks.
- `AnimationStateComponent` owns the state-machine binding, selected animation
  state, and elapsed animation-state time.
- `PikaCameraRig` reuses `arp::input::CameraFollowComponent`; no second preview
  camera-follow component was introduced.
- `visible_character_runtime.cpp` routes selected animation state through
  `AnimationStateComponent`.
- `visible_character_runtime.cpp` routes camera focus/position state through
  `PikaCameraRig` and `arp::input::CameraFollowComponent`.

## Validation

- Focused contract/unit path: `pika_character_camera_component`.
- Hidden-window style smoke path: `pika_character_camera_hidden_smoke`.
- Regression paths retained:
  - `pika_scene_builder`
  - `pika_scene_frame_source`
- RenderDoc evidence:
  - `artifacts/renderdoc/pika_phase2_character_camera_capture_capture.rdc`
  - `artifacts/renderdoc/pika_phase2_character_camera_thumb.png`
  - `artifacts/renderdoc/pika_phase2_scene_state_capture_capture.rdc`
  - `artifacts/renderdoc/pika_phase2_scene_state_thumb.png`

The thumbnail shows a non-empty frame with the Pika character and UI overlay.
This confirms the visible Vulkan path still renders after the character/camera
scene-object componentization.

## Remaining Gap

`visible_character_runtime.cpp` still contains direct preview render payload
assembly for the broader product submission path. Character and camera state are
now component-owned, while full payload submission unification is explicitly
deferred to Phase 4.

Production Ready is not claimed.
