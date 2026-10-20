# RHI Removed Hot-Path Inventory

Status: Contract Complete

Inventory date: 2026-05-18

## Removed module status

The legacy RHI module `src/rhi` has been removed from the workspace. Production code, RenderGraph, renderer, GUI, samples, games, and active tests now build against RHI contracts instead of the removed module.

This document is kept as a removal ledger rather than an active compatibility inventory. New compatibility seams must not be added above backend or platform implementation code. Unknown resource semantics must fail with diagnostics or be recorded as Prototype/Legacy fallback before any production use.

## RHI-only production surface

| Area | Representative paths | Local evidence |
| --- | --- | --- |
| Platform frame lifecycle | `src/platform/render_system.h`, `src/platform/render_system_vulkan.cpp`, `src/platform/render_system_webgpu.cpp` | RHI acquire, present, frame serial, mesh upload, pipeline setup, GUI, particles, and Custom graph composition are covered by `render_system_custom_graph_frame`. |
| RenderGraph execution | `src/renderer/render_graph/render_graph.h`, `src/renderer/render_graph/rhi_frame_plan.h`, `src/renderer/render_graph/frame_graph_rhi_active_executor.cpp` | FrameGraph resources and execution lower to RHI resource, barrier, queue, load/store, transient alias, and command encoder records. |
| Renderer facade | `src/renderer/renderers/renderers.h`, `src/renderer/renderers/gui_renderer.h`, `src/renderer/renderers/particle_renderer.h`, `src/renderer/renderers/fxaa_renderer.h`, `src/renderer/renderers/ssao_renderer.h`, `src/renderer/gui/gui_rhi_draw_commands.h`, `src/renderer/particles/particle_rhi_draw_commands.h` | Scene, GUI, particle, postprocess, transfer, and GI pass recording use RHI descriptors, command encoders, backend-owned staging, and deterministic diagnostics. |
| Samples | `samples/stylized_showcase/main.cpp`, `samples/production_demo/main.cpp` | Hidden-window MinGW/Vulkan smokes pass through RHI-only acquire, workload build, Custom graph render, and present paths. |
| Games | `games/character_viewer/app/character_viewer_runtime.cpp`, `games/project_lane/app/project_lane_runtime.cpp`, `games/material_editor/app/material_editor_runtime.cpp` | Affected executables build against RHI-only contracts; Character Viewer hidden smoke passes with the configured local asset path. |

## Backend-private implementation

Backend-native resources, staging allocators, swapchains, platform surfaces, and native runtime handles remain private to `src/rhi/backends` and `src/platform`. Upper layers express resource intent through RHI descriptors, frame workloads, command encoders, upload metadata, and capability diagnostics. They do not observe or retain backend allocation handles.

Vulkan local validation now includes resource destruction releasing backend-owned allocation budget and frame-serial retirement for bounded upload staging. Metal and WebGPU upload lowering share the same explicit frame-serial contract; native runtime validation for those platforms remains listed under external validation gaps.

## Status Boundaries

- Contract Complete: removal boundary, RHI L2 descriptors, RenderGraph lowering contracts, renderer pass contracts, and documentation state are locally defined.
- Backend validation evidence is limited to the recorded Null and synthetic Vulkan RHI paths, affected MinGW builds, sample hidden-window smokes, and Character Viewer smoke.
- Platform Validated is not claimed: macOS, iOS, Android, browser WebGPU, and hardware-backed Vulkan runtime validation still require their external environments.
- Production Ready is not claimed: broader driver, hardware, telemetry, visual, lifecycle, and platform matrix evidence remains required.

## Ongoing Gate

Architecture and protected-boundary tests must continue to reject removed-module includes, removed namespace tokens, backend-native handle leakage, and unrecorded compatibility seams in production, RenderGraph, renderer, GUI, samples, games, and tests. Any future fallback must be explicitly labeled Prototype/Legacy fallback with a deterministic diagnostic and a contract test.
