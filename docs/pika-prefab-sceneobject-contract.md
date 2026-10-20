## Pika Prefab / PrefabInstance / SceneObject Contract

Status: Contract Complete for the Phase 1 boundary freeze. Prototype Complete, Backend Validated, Platform Validated, and Production Ready are not claimed by this document.

Date: 2026-07-03

### Purpose

This document freezes the vocabulary and ownership boundary that Pika must use while it returns from app-local preview orchestration to the formal engine runtime chain:

`Scene -> GameObject -> Component -> Scene Runtime -> Render/UI/Particle`

The goal of this phase is not to finish full prefab authoring. The goal is to make sure every following phase uses the same meaning for `Prefab`, `PrefabInstance`, and `SceneObject`, and that the runtime consumes instantiated objects rather than authoring templates.

### Definitions

- `SceneObject`: the runtime object that lives inside a `scene::Scene` hierarchy and participates in lifecycle, transform traversal, extraction, and renderer/UI/particle bridging. In current engine code this is `scene::GameObject`.
- `Prefab`: a reusable authoring or packaged template asset that can describe at least a `GameObject + Component + child hierarchy` shape. A prefab is an asset, not a runtime scene node by itself.
- `PrefabInstance`: a scene instance created from a prefab, with a retained source-prefab relationship and room for explicit override metadata. A prefab instance enters the scene as scene objects; it is not a parallel runtime object model.

### Required Runtime Boundary

The runtime scene consumes only instantiated scene objects.

- `scene::Scene` owns `scene::GameObject` instances and traversal/update order.
- `SceneTraversalSnapshot` describes instantiated object state only.
- `SceneRuntimePipelinePayload` and renderer extraction consume instantiated object graphs only.
- `resource::PrefabResource` stays on the asset side and must not be treated as a renderable or updatable scene node.

Said plainly:

- `Prefab != SceneObject`
- `PrefabInstance -> SceneObject graph`
- plain scene-authored objects and prefab-instantiated objects must be able to coexist in the same scene hierarchy

### Minimum Prefab Contract For The Next Phases

Phase 1 freezes only the minimum contract needed by Pika migration:

- A prefab must be able to describe object identity, parent-child hierarchy, and component payloads.
- A prefab instance must retain at least:
  - `source prefab id`
  - stable local object identifiers for instantiated objects
  - an explicit override entry point
- Scene runtime must not directly consume prefab assets, implicit prefab defaults, or app-local guesses.

Nested prefab editing, prefab variants, apply/revert UX, and production authoring workflows remain out of scope for this phase.

### Pika Migration Rule

Pika must first return its runtime objects to `SceneObject + Component`.

That means:

- character, camera rig, particle attachment, and world-UI entry must become formal scene objects plus components
- prefab work follows as the authoring/source representation for those objects
- `visible_character_runtime.cpp` must stop acting as the long-term owner of gameplay/runtime object state

Prefab is therefore a source asset concern for upcoming phases, while `SceneObject` remains the only runtime fact shape consumed by scene traversal and extraction.

### First Batch Components Frozen In Phase 1

The first batch of Pika runtime-facing components is:

- `CharacterComponent`
- `AnimationStateComponent`
- `ParticleSystemComponent`
- `CameraRigComponent` or reuse of `input::CameraFollowComponent`
- `ui::World2DAnchorComponent`
- `ui::DocumentComponent`

Existing reusable runtime building blocks already present in the repository and expected to stay on the formal chain include:

- `input::PlayerInputComponent`
- `input::CharacterControllerComponent`
- `input::CameraFollowComponent`
- `ui::DocumentComponent`
- `ui::World2DAnchorComponent`

### App Glue That Still Needs To Move Later

Phase 1 also freezes what still lives in app glue today so later phases can remove it deliberately:

- app-owned character preview state
- app-owned animation state selection
- app-owned camera preview rig policy
- app-owned particle preview trigger and anchor selection
- app-owned visible UI session orchestration
- direct custom payload assembly in `visible_character_runtime.cpp`

Those paths are tolerated only as explicit migration gaps, not as the target architecture.

### Phase 1 Evidence

- Focused contract tests:
  - `arp_pika_prefab_sceneobject_contract_tests`
  - `arp_pika_runtime_scene_source_contract_tests`
- Smoke path retained for this phase:
  - `arp_scene_runtime_smoke_tests`

### Non-Claims

This document does not claim:

- prefab authoring completeness
- prefab variant completeness
- backend visual validation
- RenderDoc visual acceptance
- production-ready scene/prefab pipeline

Those belong to later phases and must carry their own evidence.
