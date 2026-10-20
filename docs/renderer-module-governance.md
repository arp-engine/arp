# Renderer Module Governance

## Status

- Contract Complete: `renderer_module_governance` enforces the first generic renderer cleanup boundary. Empty renderer aggregate headers, the removed GUI RHI compatibility alias, retired `scene_gui_composer`, and obsolete `gi_compute_plan` cannot re-enter `src/renderer` without failing a fast unit test.
- Contract Complete: `pika_standard_renderer_boundary` keeps PiKa and Character View project-specific names out of generic `src/renderer` paths.
- Contract Complete: `src/renderer/renderers/renderers.h` is now a thin compatibility facade over focused GUI, particle, FXAA, and SSAO renderer headers.
- Contract Complete: `src/renderer/materials/material_texture_resources.h` now contains only renderer upload-source semantics; imported material slot remapping, image decode, and alpha-transparency preprocessing live in sample/game import policy.
- Backend Validated: Not claimed. This document covers source-level module governance and does not validate a backend implementation path.
- Platform Validated: Not claimed. Platform validation still belongs to named hidden-window or app smoke tests.
- Production Ready: Not claimed. Renderer module governance still has explicit legacy fallback inventory and is not broad production validation.

## Current Rules

- Generic renderer modules must be project-neutral. Project-specific contracts belong under `samples/` or `games/`.
- Empty aggregate headers and silent compatibility aliases are not allowed in `src/renderer`.
- Compatibility paths must be explicit `Prototype/Legacy fallback` or test-only code, not production-default renderer APIs.
- Renderer governance changes require at least one fast unit/contract test and one smoke or hidden-frame verification path.

## Completed Cleanup

- Contract Complete: Removed empty renderer aggregate headers for debug, materials, postprocess, world, and lighting.
- Contract Complete: Removed the old GUI RHI compatibility alias in favor of the explicit RHI draw-command path.
- Contract Complete: Removed the engine-level `scene_gui_composer` production path and its obsolete test target.
- Contract Complete: Removed the obsolete `gi_compute_plan` helper and its obsolete test target.
- Contract Complete: Split the former `renderers.h` implementation aggregate into `gui_renderer.h`, `particle_renderer.h`, `fxaa_renderer.h`, `ssao_renderer.h`, and shared `renderer_common.h`.
- Contract Complete: Removed the unused renderer-level `lighting_shadow` contract island; active stylized shadow policy remains sample/game owned.
- Contract Complete: Added typed `CustomRenderFrameWorkload` payload helpers and migrated active sample/game workload construction away from raw `const void*` payload assembly.
- Prototype Complete: `RenderFrameDirector` legacy pipeline composition remains for payloads without provider coverage, but successful fallback now emits an explicit `Prototype/Legacy fallback` diagnostic.
- Contract Complete: Moved imported material texture table construction, mesh material-slot remapping, image decode, and alpha-transparency RGB bleed out of `src/renderer/materials`; renderer material texture resources now consume explicit upload contracts only.

## Remaining Cleanup Queue

- No active renderer module cleanup item remains in this governance queue. Future items should be added as new scoped tasks with their own fast contract test and smoke evidence.
