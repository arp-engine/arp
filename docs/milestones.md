# Anime Render Pipeline Milestones

## Final Acceptance Target

Run a Windows MinGW build that opens an SDL3 Vulkan window, creates a Vulkan swapchain through the project RHI, renders a clear/triangle frame through the FrameGraph, compiles Slang shaders, and displays the first self-authored retained GUI overlay path. The final validation command must run CMake build, shader compilation, and tests from a clean build directory.

## Milestone 0: Foundation - Complete

- C++17 project structure with direct MinGW and CMake/Ninja build paths.
- Core logging, Null RHI, lightweight FrameGraph, shader compiler contract, retained GUI tree/layout/input.
- SDL3 devel package integrated.
- Slang sample compiles to SPIR-V and Metal.
- Current tests: render graph, GUI, shader, Vulkan runtime loader, SDL window.

## Milestone 1: Vulkan Instance and SDL Surface - Complete

- Create a Vulkan instance with SDL-required instance extensions.
- Dynamically resolve Vulkan global and instance functions without linking `vulkan-1.lib`.
- Create and destroy an SDL-backed `VkSurfaceKHR` through RAII.
- Tests must prove hidden SDL Vulkan windows can produce a live Vulkan surface.

## Milestone 2: Vulkan Physical Device and Logical Device - Complete

- Enumerate physical devices and select a graphics+present queue family.
- Create a logical device and graphics queue.
- Query surface formats, present modes, and surface capabilities.
- Tests must prove a device can be selected for the SDL surface.

## Milestone 3: Swapchain and Frame Lifecycle - Complete

- Create swapchain images for the SDL window.
- Acquire/present frames with semaphores and fences.
- Add resize-safe swapchain recreation.
- Tests or sample must run a short headless/hidden-window frame loop without validation errors.

## Milestone 4: RHI Vulkan Backend Clear Pass - Complete

- Implement Vulkan-backed `Device`, `Texture`, `Buffer`, and `CommandList` enough for clear.
- Route FrameGraph transitions into Vulkan image/buffer barriers.
- The sample must clear a real Vulkan swapchain image through RHI + FrameGraph.

## Milestone 5: Slang Pipeline Triangle - Complete

- Compile the sample Slang shader to SPIR-V during build.
- Create shader modules, pipeline layout, render pass/dynamic rendering path, and graphics pipeline.
- Render a triangle into the SDL Vulkan window.

## Milestone 6: Retained GUI Render Path - Complete

- Convert GUI layout output into renderable quads.
- Add font atlas planning and placeholder colored panel rendering.
- Render the first GUI overlay pass after the scene pass.

## Milestone 7: Stylized Rendering Slice - Complete

- Add scene constants, camera, mesh vertex format, and material schema.
- Add first stylized scene shader and character NPR shader skeleton.
- Render a simple scene object with ramp lighting controls ready for expansion.

## Milestone 8: Mesh and Material Data Path - Complete

- Add CPU mesh asset structs for vertices, indices, submeshes, and material slots.
- Upload a small mesh into Vulkan vertex/index buffers.
- Render indexed stylized geometry using the existing scene/material contracts.

## Milestone 9: FrameGraph Scene/GUI Composition - Complete

- Route scene and GUI passes through explicit FrameGraph pass declarations.
- Track swapchain image transitions for scene clear, stylized draw, GUI overlay, and present.
- Keep the self-authored GUI overlay composited after the stylized scene pass.

## Milestone 10: Toon Lighting Expansion - Complete

- Add ramp texture planning and a CPU fallback ramp table contract.
- Add rim/specular/outline parameters to character NPR data.
- Compile separate scene and character variants while preserving Vulkan/Metal shader outputs.

## Execution Rules

- Each new behavior starts with a failing test or sample target.
- Direct build script and CMake/Ninja path must both stay healthy.
- Avoid adding broad engine abstractions until a milestone needs them.
- Keep Vulkan/Metal portability at the RHI boundary; Windows implementation can advance first.
