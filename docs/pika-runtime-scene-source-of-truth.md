# Pika Runtime Scene Source Of Truth

Status: Contract Complete.

Pika product runtime state has one fact source: the `RuntimeScene` built and
owned by `games/pika/app/SceneBuilder`.

The product render upstream is:

```text
RuntimeScene
  -> GameObject + Component state
  -> FrameSource::fromScene
  -> PikaStandardFrameProducer
  -> RenderSession
```

Business features must converge into GameObject components or engine framework
systems. Character state, camera state, ground state, UI documents, UI anchors,
input commands, selection, animation state, gameplay bindings, and future
sprite/world UI bridges must be represented through scene objects, components,
value handles, or explicit engine contracts.

Renderer-facing code must not invent a second business source. Pika product
rendering must not build draw payloads directly from gameplay objects, UI
sessions, asset loaders, or ad hoc runtime structs when equivalent state should
live on the RuntimeScene.

Technical details belong below the business layer:

- UI document loading is expressed through `arp::ui::DocumentComponent`.
- World/UI attachment is expressed through `arp::ui::World2DAnchorComponent`.
- Character geometry and material state are expressed through scene components.
- Render extraction is expressed through `FrameSource::fromScene`.
- RHI/backend work remains below renderer and platform contracts.

Current migration status:

- The PikaStandard `runRuntime` path builds the RuntimeScene once during runtime
  initialization and reuses `FrameSource::fromScene` for submitted frames.
- `SceneBuilder` now creates formal `PikaPlayer` and `PikaCameraRig` objects.
  `PikaPlayer` carries `CharacterComponent` and `AnimationStateComponent`;
  `PikaCameraRig` reuses `arp::input::CameraFollowComponent`.
- The visible character runtime mirrors this ownership: selected animation state
  is changed through `AnimationStateComponent`, and camera frame state is derived
  from `PikaCameraRig` after `CameraFollowComponent` late-update.
- `SceneBuilder` attaches Pika UI to the same RuntimeScene through a `pika-ui`
  GameObject with `DocumentComponent` and `World2DAnchorComponent`.
- Visible particle trigger intent, effect binding, debug fixture provenance, and
  world anchor source are represented by `ParticleSystemComponent`.
- Visible world UI bridge state is represented by `ui::DocumentComponent` and
  `ui::World2DAnchorComponent`; screen-space UI remains in `src/ui`.
- Phase 4 adds `PikaSceneRuntimeSubmissionBridge`, which validates the visible
  path's `SceneTraversalSnapshot`, `ResolvedCamera`, 3D/2D/UI queues,
  `RendererSceneRuntimeFrame`, and `SceneRuntimePipelinePayload` without backend
  handle exposure.
- `visible_character_runtime.cpp` still preserves diagnostics for
  Prototype/Legacy migration gaps, but the current formal visible workload no
  longer carries role, particle, or UI legacy sections as the main submission
  path.
- Vulkan/WebGPU platform render systems no longer bypass `RenderFrameDirector`
  for particle-enabled visible frames. They enter the director and expose the
  remaining preview provider through an explicit legacy fallback decision.
- The visible workload's main `scene` section is
  `SceneRuntimePipelinePayload`; `VisiblePreviewFallbackSubmission` no longer
  adds `legacySceneFallback`, `particles`, or `gui` sections to the formal
  scene-runtime workload.

Production Ready remains blocked until broader platform lifecycle, content,
performance, and visual parity evidence are complete. A direct preview payload,
asset loader, UI session, or renderer workload must not reappear as a second
business fact source.

Phase 4 evidence:

- Focused contract: `pika_scene_runtime_submission`
- Hidden-window visible regression: `pika_visible_character_runtime`
- RenderDoc capture:
  `artifacts/renderdoc/pika_phase4_scene_runtime_submission_capture.rdc`
- RenderDoc thumbnail:
  `artifacts/renderdoc/pika_phase4_scene_runtime_submission_thumb.png`
- Director fallback RenderDoc capture:
  `artifacts/renderdoc/pika_phase4_director_fallback_capture.rdc`
- Director fallback RenderDoc thumbnail:
  `artifacts/renderdoc/pika_phase4_director_fallback_thumb.png`
- Scene payload main RenderDoc capture:
  `artifacts/renderdoc/pika_phase4_scene_payload_main_capture.rdc`
- Scene payload main RenderDoc thumbnail:
  `artifacts/renderdoc/pika_phase4_scene_payload_main_thumb.png`
