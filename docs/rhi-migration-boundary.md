# RHI Migration Boundary

Status: Contract Complete

## Removed production boundary

The removed `src/rhi` module is not a production compatibility route. Production, sample, game, renderer, GUI, and RenderGraph code must use RHI descriptors, draw/frame plans, command encoders, and deterministic diagnostics. Backend-native handles remain private to backend/platform implementation files.

## RHI-only production files

These files are part of the migrated production surface and must remain free of removed-module includes, removed namespace tokens, and backend-native graphics handles.

| Path | Boundary |
| --- | --- |
| `src/renderer/gui/gui_rhi_draw_commands.h` | GUI overlay RHI draw-command path using `rhi::CommandEncoder` and RHI resources. |
| `src/renderer/render_graph/rhi_frame_plan.h` | RenderGraph RHI frame-plan contract and diagnostics path. |
| `src/renderer/render_graph/rhi_frame_plan.cpp` | RenderGraph RHI frame-plan implementation. |
| `src/renderer/render_graph/render_graph.h` | FrameGraph resource and execution facade using RHI resource/state/command concepts. |
| `src/platform/render_system.h` | RHI frame lifecycle, acquire/present, upload, and Custom graph entrypoint. |
| `src/platform/native/render_system_vulkan.cpp` | Vulkan platform RenderSystem owns RHI runtime/device/swapchain and invokes Custom graph execution. |
| `src/platform/web/render_system_webgpu.cpp` | WebGPU platform RenderSystem owns RHI contracts or deterministic unavailable-runtime diagnostics. |
| `src/renderer/renderers/renderers.h` | Thin compatibility facade for focused renderer headers. |
| `src/renderer/renderers/gui_renderer.h` | GUI RHI renderer resource setup and graph/standalone recording entrypoint. |
| `src/renderer/renderers/particle_renderer.h` | Particle RHI renderer resource setup and bounded upload recording entrypoint. |
| `src/renderer/renderers/fxaa_renderer.h` | FXAA RHI renderer resource setup and descriptor-slotted graph/standalone recording entrypoint. |
| `src/renderer/renderers/ssao_renderer.h` | SSAO RHI renderer resource setup and descriptor-slotted graph/standalone recording entrypoint. |
| `samples/stylized_showcase/main.cpp` | Builds `CustomRenderFrameWorkload` and calls `renderGraphFrame` between acquire and present. |
| `samples/production_demo/main.cpp` | Builds the production demo RHI workload and calls `renderGraphFrame` between acquire and present. |
| `games/character_viewer/app/character_viewer_runtime.cpp` | Builds Character Viewer RHI workloads and uses the platform Custom graph frame path. |

## No active compatibility seams

There are no active compatibility seams above backend/platform implementation for the removed module. Future fallbacks must be explicitly marked Prototype/Legacy fallback, emit deterministic diagnostics, and be guarded by contract tests before any production path can reference them.

## Promotion Rule

Architecture and protected-boundary tests must reject removed-module includes, removed namespace tokens, backend-native headers, and native handle tokens in migrated production files. New renderer, GUI, RenderGraph, sample, and game work targets RHI draw/frame plans or command encoders by default.
