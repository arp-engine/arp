# Pika Particle / World UI Component Prototype

Status: Prototype Complete to Backend Validated for the current Windows
MinGW/Vulkan visible path. Production Ready is not claimed.

## Runtime Objects And Components

- `ParticleSystemComponent` is the formal scene component for Pika visible VFX
  trigger intent, effect resource binding, debug fixture provenance, preview
  anchor mode, and world anchor extraction.
- `pika-world-ui` / visible UI scene entries use `ui::DocumentComponent` plus
  `ui::World2DAnchorComponent` as the formal scene bridge.
- Screen-space UI remains in `src/ui`; world-space linkage is expressed only
  through scene objects and anchors.

## Validation

- Focused/smoke path: `pika_particle_world_ui_component`.
- Visible runtime path: `pika_visible_character_runtime`.
- UI bridge regression: `scene_ui_document_component`.
- RenderDoc evidence:
  - `artifacts/renderdoc/pika_phase3_particle_world_ui_capture_capture.rdc`
  - `artifacts/renderdoc/pika_phase3_particle_world_ui_thumb.png`
  - `artifacts/renderdoc/pika_phase3_explosion_capture_capture.rdc`
  - `artifacts/renderdoc/pika_phase3_explosion_thumb.png`

The visible runs report nonzero particle simulation and draw vertices, particle
frame-graph pass execution, GUI pass execution, and for the explosion fixture a
textured particle draw plan with a renderer-owned 256x256 texture upload.

## Remaining Gap

The broader `visible_character_runtime.cpp` render submission still builds the
legacy custom particle payload as a Phase 4 migration gap. Phase 3 moves the
trigger, effect binding, and anchor source to formal components; Phase 4 must
move the product submission path fully through scene-runtime extraction.
