# Pika Scene Runtime Submission Bridge

Status: Backend Validated for the Windows/Vulkan visible contract bridge.

This document does not claim Platform Validated or Production Ready status.

## Contract Complete Surface

`PikaSceneRuntimeSubmissionBridge` accepts only explicit scene/runtime inputs:

- `SceneTraversalSnapshot`
- `ResolvedCamera`
- `Scene3DRenderQueue`
- `Scene2DRenderQueue`
- `ui::RenderQueue`

The bridge builds a `RendererSceneRuntimeFrame`, wraps it in
`SceneRuntimePipelinePayload`, and validates that payload before the visible
runtime records the frame as scene-runtime submitted.

The bridge rejects missing queues, invalid traversal snapshots, backend handle
exposure, and hot-path static geometry payload copies with deterministic
diagnostics.

## Backend Validated Evidence

- Focused contract test: `pika_scene_runtime_submission`
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

The RenderDoc run confirmed the current visible Vulkan frame still has main
scene, particle, and GUI graph activity.

## Formal Scene Runtime Workload

The visible workload's main `scene` section carries
`SceneRuntimePipelinePayload`. `VisiblePreviewFallbackSubmission` no longer adds
`legacySceneFallback`, `particles`, or `gui` workload sections to the formal
scene-runtime submission path. The director therefore routes the current visible
workload through `PreparedSubmit` instead of `LegacyPipelineFallback`, and
`pika_visible_character_runtime` records `sceneRuntimeFormalPathSubmitted=true`
for the hidden-window smoke path.

`visible_character_runtime.cpp` no longer writes `CustomParticleGraphPayload`
fields directly and no longer directly references
`CharacterPreviewRenderPayload`, `CustomParticleGraphPayload`, or
`CustomUiGraphPayload`; those legacy payload types are contained below the
preview submission helper and are not part of the formal workload contract.

Vulkan and WebGPU keep explicit `RenderFrameDirector` reporting for
`legacyPipelineFallbackUsed`, `sceneRuntimePathSubmitted`, and backend fallback
visibility. The contract test keeps those backend diagnostics alive so a future
fallback can only reappear as an explicit diagnostic/capability result, not as a
silent sample-specific bypass.
