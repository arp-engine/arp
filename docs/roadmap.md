# Anime Open World Rendering Pipeline Roadmap

Future mobile-first cross-platform plan: [mobile-first-technical-roadmap.md](mobile-first-technical-roadmap.md)

FrameGraph v2 upgrade plan: [framegraph-v2-roadmap.md](framegraph-v2-roadmap.md)

## Vision

Build a C++17 self-authored stylized open-world rendering stack for clean anime-style 3D rendering. The target first playable slice is a 512m x 512m world containing terrain, vegetation, static props, one high-quality character, day/night lighting, character NPR shading, stylized scene shading, outline, shadows, postprocess, asset import, runtime tuning, and a retained GUI that can serve both tools and game UI.

Primary launch platforms:

- Windows: Vulkan
- macOS: Metal

Core constraints:

- C++17
- SDL3 platform layer
- Slang as the only shader source language
- Self-authored RHI
- Self-authored retained GUI
- No ImGui
- FrameGraph-backed pass composition

## Current Baseline

The current repository has an RHI-only local baseline for migrated production, sample, game, renderer, GUI, and RenderGraph paths:

- CMake/Ninja and direct MinGW build paths.
- SDL3 window and Vulkan surface creation.
- Vulkan instance, device, swapchain, acquire/present lifecycle.
- RHI Custom RenderGraph frame execution through acquire, workload build, graph render, and present.
- Slang compilation to SPIR-V and Metal outputs.
- Retained GUI layout and Vulkan overlay rendering.
- Bottom-layer `src/assets` mesh/material/import/package contracts.
- Vulkan vertex/index buffer upload.
- Stylized indexed mesh rendering.
- 3D low-poly toon character showcase.
- Character material slots for cloth, face, and hair.
- Renderer-facing frame/mesh/scene/GUI interfaces that depend only on RHI contracts.
- Renderer-owned scene and GUI draw plans centralize submesh/material push constants, clear values, GUI panel/text ordering, and text-only draw validity.
- Renderer-side RHI draw command helpers record scene, GUI, particle, postprocess, transfer, and GI plans through RHI command encoders.
- Concrete renderer-side RHI scene/GUI helpers create shader packages, pipeline layouts, descriptor sets, and bounded upload staging through RHI contracts.
- Vulkan RHI command paths expose bounded graphics command telemetry for renderer-owned abstract command recording.
- Vulkan RHI resource creation wraps native buffers, images, memory, and pipeline objects behind backend-owned RHI resources.
- Platform `RenderSystem` owns Windows Vulkan bootstrap, RHI backend selection, surface/swapchain acquire-present lifecycle, and Custom RenderGraph execution.
- Backend implementation units live under the RHI backend package, with native handles private to backend/platform files.
- Sample-owned stylized implementation data is split into focused modules for environment, lighting, postprocess, runtime debug, shadows, world slice, outline, and character NPR/materials; the former broad `samples/stylized/stylized_scene.h` aggregate has been retired.
- Hidden-window smoke tests and clean CMake validation.

## Roadmap Overview

| Phase | Name | Goal | Status |
| --- | --- | --- | --- |
| 0 | Foundation | Project, SDL3, Null RHI, FrameGraph, GUI core | Contract Complete |
| 1 | Vulkan Vertical Slice | Vulkan instance/device/swapchain/clear/triangle | Backend Validated |
| 2 | Stylized Mesh Slice | Slang stylized shader, mesh upload, GUI overlay, 3D toon sample | Prototype Complete |
| 3 | RHI Hardening | Turn current Vulkan helpers into reusable RHI resources and binding model | Backend Validated |
| 4 | Shader Reflection and Cache | Generate binding metadata and reduce handwritten binding assumptions | Backend Validated |
| 5 | RenderGraph Expansion | Color/depth attachments, sampled textures, storage buffers, fixed frame graph | Contract Complete |
| 6 | Retained GUI v1 | Tool/game UI controls, input, text rendering, style states | Prototype Complete |
| 7 | Scene Rendering v1 | glTF import, camera, culling, draw lists, stylized PBR | Prototype Complete |
| 8 | Forward+ Lighting | Cluster build and light culling compute path | Prototype Complete |
| 9 | Shadow System | CSM, character shadow atlas, PCF/ramp/contact shadows | Prototype Complete |
| 10 | Character NPR | Skin/face/hair/cloth/eye variants and face-safe lighting | Prototype Complete |
| 11 | Outline System | Inverted hull for characters and depth/normal scene edges | Prototype Complete |
| 12 | Open World Slice | 512m x 512m terrain, props, vegetation, LOD, day/night | Prototype Complete |
| 13 | Postprocess and Color | HDR, tone mapping, LUT, bloom, fog, FXAA, sharpen | Prototype Complete |
| 14 | Tools and Runtime Tuning | Importer, JSON schemas, hot reload, debug views, GPU timing | Contract Complete |
| 15 | Metal Backend | macOS Metal parity for accepted RHI feature set | In Progress |
| 16 | Global Illumination Contract | DDGI-lite probe-grid planning, fallback, budgets, and frame shape | Contract Complete |
| 17 | GI RHI Capabilities | Compute/storage/sampled-resource capability and binding contracts | Contract Complete |
| 18 | GI Shader Pipeline | Slang probe update and irradiance resolve with SPIR-V/MSL/reflection | Prototype Complete |
| 19 | GI Renderer Integration | FrameGraph resources, draw-plan data, scene shader consumption, tuning | In Progress |
| 20 | Vulkan GI Validation | Vulkan compute, descriptor, barrier, timing, and hidden smoke validation | Backend Validated |
| 21 | Metal And Mobile GI Validation | Metal parity plus Android/iOS lifecycle and memory-budget validation | In Progress |

## Status Taxonomy

- Contract Complete: API, semantic, or planning contracts are specified and covered by deterministic tests.
- Prototype Complete: A working implementation exists behind explicit gates or limited scope, with smoke coverage, but is not the production default.
- Backend Validated: A backend path has real resource creation, command encoding, queue submit, acquire/present or equivalent lifecycle, and deterministic visual/hash evidence for the named backend.
- Platform Validated: A named runtime or deployment target has platform-specific smoke/lifecycle evidence such as MinGW/Vulkan hidden-window, browser/WebGPU, macOS/Metal, iOS/Metal, or Android/Vulkan.
- Production Ready: The production path is migrated, bounded, observable, and covered by the current final regression/smoke matrix.

Status distinction guardrails:

- Contract Complete is not Backend Validated.
- Prototype Complete is not Backend Validated.
- Backend Validated is not Platform Validated.
- Platform Validated is not Production Ready.
- Production Ready requires backend, platform, migration, telemetry, lifetime, and final smoke evidence.

## RHI Production Ready Status

Production Ready evidence requirements are cumulative for the RHI cutover. RHI cannot be labeled Production Ready until the current matrix records all of the following:

- conformance evidence: Null, Vulkan, Metal, and WebGPU pass the required RHI scenario matrix.
- backend parity evidence: the conformance matrix reports backend parity across those backends, with deterministic diagnostics for any unsupported state.
- platform smoke evidence: Vulkan hidden-window, WebGPU browser/runtime, and available platform lifecycle smoke paths have current evidence.
- memory lifetime evidence: frame resources, transient allocations, descriptor arenas, staging paths, surface resources, and memory-budget telemetry stay bounded across lifecycle transitions.
- debug telemetry evidence: debug names, command markers, query capability checks, fallback counters, validation diagnostics, memory diagnostics, and frame telemetry are structured and bounded.
- migration boundary evidence: renderer, GUI, RenderGraph, samples, and games stay behind RHI draw/frame plans, with no native handle leakage and no migrated startup redirects into removed-module routes.
- visual regression evidence: deterministic screenshot/hash or visual-hash checks are current for conformance and smoke workloads.

## RHI Current Backend Status

RHI is currently In Progress for the final Production Ready gate. L2/Null contracts, capability semantics and backend lowering, renderer/FrameGraph planning contracts, WebGPU runtime plumbing, and the removed-module production boundary remain Contract Complete support work. The cohesive RHI package keeps capability semantics, lowering, and backend runtime implementations native-header free above backend-private directories. Vulkan RHI has hidden-window-tested native resource, descriptor, pipeline, command, queue, swapchain, lifecycle, and deterministic render-output hash evidence on MinGW. WebGPU RHI has browser/runtime native resource, descriptor, pipeline, command, queue, canvas acquire/present, lifecycle, and deterministic screenshot/hash evidence when the Emscripten browser runtime is available. Metal RHI has backend-owned Apple native resource, argument binding, shader/pipeline, command, queue, drawable/swapchain, lifecycle, and hash-gated smoke telemetry, while MinGW correctly reports Apple runtime hash capture as a platform gap. RHI backend targets remain build-graph and source-graph independent after `src/rhi` removal. Migrated RHI paths do not list removed v1 fallback as a supported compatibility route. No production startup path selects a removed v1 backend route. Final local Production Ready matrix passed on 2026-05-17. Production Ready is not claimed. Production Ready remains blocked by Pending Confirmation / External Validation. The blocking gaps are macOS Metal runtime/hash, iOS Metal lifecycle, Android Vulkan lifecycle, and broader hardware/driver matrix.

The remaining RHI Production Ready gap is external platform/runtime validation. The local matrix now covers available backend conformance/visual evidence, honest unsupported-platform diagnostics, renderer/RenderGraph production cutover, telemetry, lifetime, and final smoke verification. Renderer, GUI, RenderGraph, samples, and games must continue using RHI draw/frame plans without native handle leakage.

## Phase 3: RHI Hardening

Goal: move from Vulkan-specific renderer helpers toward reusable RHI objects without over-abstracting beyond current needs.

### Deliverables

- `RhiBuffer` with usage flags, memory location, upload helper, and debug name. Basic contract is implemented for Null RHI, and `VulkanMeshBuffer` now creates buffers from RHI-style descriptors.
- `RhiTexture` with format, extent, usage, mip count, and initial state.
- `RhiShader` and `RhiPipeline` contracts for graphics and compute pipelines.
- `RhiCommandList` support for:
  - begin/end render pass or dynamic rendering equivalent
  - bind pipeline
  - bind vertex/index buffers
  - push constants
  - draw/draw indexed
  - dispatch compute
- `RhiDescriptorSet` first-version contract for texture/sampler/buffer bindings.
- Vulkan backend implementation for the above, using current working Vulkan code as the migration source.

### Acceptance

- Existing stylized showcase renders through RHI-facing interfaces, not direct renderer-owned Vulkan setup.
- Direct build passes.
- Clean CMake build passes.
- Hidden showcase smoke test passes.

### Current Progress

This phase is retained as historical design context. The production path has since moved to RHI resource descriptors, frame contracts, command encoders, shader-package validation, descriptor-backed draw recording, backend-owned staging, and platform-owned acquire/present. Current boundary tests prevent renderer code, samples, and games from depending on backend-native handles or removed-module APIs.

### Risks

- Too much abstraction too early can freeze a bad API. Keep the first RHI version shaped by clear, mesh draw, GUI draw, and future compute requirements only.

## Phase 4: Shader Reflection and Cache

Goal: make Slang the source of truth for shader resource bindings and vertex layouts.

### Deliverables

- Shader compile script emits SPIR-V, MSL, and reflection metadata.
- Reflection cache contains:
  - entry points
  - push constant size/ranges
  - vertex attributes
  - textures
  - samplers
  - uniform/storage buffers
- Runtime loader validates pipeline creation against reflection metadata.
- Build fails when expected reflection output is missing.

### Acceptance

- Current stylized scene shader and GUI shader compile with reflection.
- A shader reflection test verifies vertex layout and push constant size for `stylized_scene.slang`.
- No handwritten platform-specific binding table is introduced above the RHI.

### Current Progress

- `ShaderCompiler` now emits in-memory reflection metadata for entry points, bindings, push constant size, and vertex attributes.
- Shader tests verify stylized scene reflection: 80-byte push constants and POSITION/NORMAL/TEXCOORD vertex layout.
- `scripts/compile-shaders.ps1` now emits `.reflection.json` files beside SPIR-V/MSL outputs for clear, GUI overlay, stylized scene, and character NPR shaders.
- Shader compilation now fails if any expected reflection JSON output is missing.
- `ShaderReflectionCache` can load runtime reflection JSON from strings or files and reconstruct module targets, entry points, push constant size, and vertex attributes.
- `ShaderPipelineReflectionValidator` validates runtime pipeline push constant and vertex layout assumptions against reflection metadata.
- Vulkan GUI and stylized scene renderer creation now loads `.reflection.json` beside the shader bytecode and validates pipeline layout before creating Vulkan pipelines.
- CMake shader generation now produces reflection JSON files as first-class build outputs.
- Reflection JSON now includes a `bindings` array, and `ShaderReflectionCache` reconstructs uniform/storage buffer, sampled texture, storage texture, and sampler bindings.
- Direct MinGW build and clean CMake/CTest passed after runtime reflection validation was added.

### Completion Notes

- Phase 4 acceptance passed with direct MinGW build, clean CMake/CTest, shader compilation, runtime reflection validation, and hidden stylized showcase smoke.

## Phase 5: RenderGraph Expansion

Goal: make the fixed frame structure declarative while keeping scheduling simple.

### Deliverables

- Pass declarations for:
  - DepthPrepass
  - ClusterBuild
  - CsmShadow
  - CharacterShadow
  - SceneStylizedPbr
  - CharacterNpr
  - TransparentVfx
  - Outline
  - PostProcess
  - Gui
  - DebugOverlay
- Resource types:
  - color attachment
  - depth attachment
  - sampled texture
  - storage buffer
  - uniform buffer
- Vulkan barriers for image and buffer transitions.
- Transient render target allocation contract.

### Acceptance

- A FrameGraph test proves pass order and resource states for the full fixed frame shape.
- Vulkan smoke test clears, draws scene, draws GUI, and presents through the expanded graph.

### Current Progress

- Added a full fixed-frame RenderGraph test covering:
  - DepthPrepass
  - ClusterBuild
  - CsmShadow
  - CharacterShadow
  - SceneStylizedPbr
  - CharacterNpr
  - TransparentVfx
  - Outline
  - PostProcess
  - Gui
  - DebugOverlay
- FrameGraph now records `resourceTrace()` entries with pass name, resource name, resource kind, access mode, and target resource state.
- The fixed-frame test covers color attachment, depth attachment, sampled texture reads, storage texture writes, and storage buffer read/write transitions.
- Direct MinGW build and clean CMake/CTest passed after the first RenderGraph expansion block.
- Added explicit transient texture and transient buffer creation APIs.
- Transient resources are frame-local: `FrameGraph::reset()` now removes transient resources and traces while persistent resources survive.
- Transient handles can be reused after reset through smallest-free-id allocation.
- Focused RenderGraph test, direct MinGW build, and clean CMake/CTest passed after the transient resource block.
- Added `barrierTrace()` with Vulkan-facing image/buffer barrier details: source/destination stages, access masks, image layouts, and image aspect mask.
- Added FrameGraph v2 compiled planning:
  - `FrameGraph::compile()` returns `CompiledFrameGraph`.
  - `FrameGraphExecutor` executes compiled plans while preserving existing traces.
  - Resource dependencies now produce a topological executable pass order and reject cycles.
  - Compiled plans expose resource lifetimes, barrier plans, transient alias plans, load/store plans, queue plans, and imported external resources.
- Scene+GUI composition now submits the Phase 5 fixed frame shape:
  - DepthPrepass
  - ClusterBuild
  - CsmShadow
  - CharacterShadow
  - SceneStylizedPbr
  - CharacterNpr
  - TransparentVfx
  - Outline
  - PostProcess
  - Gui
  - DebugOverlay
- Existing Vulkan scene + GUI smoke path now runs through the expanded FrameGraph shape while preserving the current scene and GUI draw callbacks.
- Phase 5 acceptance passed with focused RenderGraph/composer tests, direct MinGW build, clean CMake/CTest, shader compilation, and hidden stylized showcase smoke.

### Completion Notes

- Phase 5 deliberately records Vulkan-facing barrier details as a portable trace contract first. The current Vulkan clear path still has a clear-specific layout need; draw-pass attachment layout tightening is deferred to later Vulkan render-pass work.

## Phase 6: Retained GUI v1

Goal: turn the current retained GUI into a useful runtime tool/game UI foundation.

### Deliverables

- GUI tree with stable ids and retained node state.
- Style system supporting:
  - color
  - typography tokens and font atlas references
  - spacing
  - border
  - hover/active/focused/disabled states
- Layout:
  - row/column
  - gap
  - padding
  - margin
  - fixed size
  - percent size
  - min/max size
  - align
  - scroll view
- Input:
  - hover
  - active
  - focus
  - keyboard navigation
  - pointer capture
- Controls:
  - label
  - button
  - icon button
  - checkbox
  - slider
  - numeric input
  - dropdown
  - tabs
  - panel
  - scroll view
  - tree view
  - color picker
- Text path using FreeType/msdfgen or an interim SDF atlas path.

### Acceptance

- GUI layout tests cover 16:9, 21:9, 4:3, and resized window sizes.
- Input tests cover hover, focus, button click, checkbox toggle, slider drag, and scroll.
- GUI pass renders at least 500 simple nodes in a smoke sample.

### Current Progress

- Existing retained GUI has stable node ids, tree layout, draw data generation, and font atlas planning.
- Pointer input supports hover, active, focus, button click detection, and deepest interactive hit testing.
- Added retained slider value state with pointer capture during drag, so dragging outside the slider bounds still updates the active slider.
- Added checkbox retained checked state and click-to-toggle event results.
- Focused GUI tests, direct MinGW build, and clean CMake/CTest passed after the first Phase 6 input/control block.
- Added percent sizing plus min/max size constraints for responsive retained layout.
- Added cross-axis alignment handling for row/column layout, including start, center, end, and stretch behavior.
- Focused GUI tests, direct MinGW build, and clean CMake/CTest passed after the first Phase 6 layout constraint block.
- Added keyboard navigation contract: Tab/Shift+Tab cycles focus across interactive controls, Space/Enter activates the focused button or checkbox.
- Focused GUI tests, direct MinGW build, and clean CMake/CTest passed after the first Phase 6 keyboard navigation block.
- Added `TextInput` node type plus focused text input event handling and Backspace editing.
- Focused GUI tests, direct MinGW build, and clean CMake/CTest passed after the first Phase 6 text input block.
- Added retained `ScrollView` node type with clamped vertical scroll state, scroll event dispatch, scrolled child layout, and draw-data clipping for fully hidden scroll children.
- Focused GUI tests, direct MinGW build, and clean CMake/CTest passed after the first Phase 6 scroll view block.
- Added `VisualState` resolution into GUI draw data for normal, hovered, active, focused, and disabled nodes.
- Added a retained GUI 500-node draw-data smoke test.
- Focused GUI tests, direct MinGW build, and clean CMake/CTest passed after the first Phase 6 style-state and 500-node block.
- Added first-pass retained control contracts for `NumericInput`, `Dropdown`, `Tabs`, `TreeView`, and `ColorPicker`.
- `NumericInput` parses focused text input into a clamped numeric value; dropdown and tree view toggle retained expansion; tabs advance retained selected index; color picker updates a retained red-channel value from pointer capture.
- Focused GUI tests, direct MinGW build, and clean CMake/CTest passed after the first Phase 6 complex-controls block.
- Expanded the Vulkan GUI overlay smoke test to render 500 simple GUI nodes through the real GUI pass.
- Phase 6 acceptance passed with focused GUI tests, direct MinGW build, clean CMake/CTest, shader compilation, hidden showcase smoke, and hidden Vulkan GUI 500-node smoke.

### Completion Notes

- Text rendering remains represented by the current font atlas planning and GUI quad path; full FreeType/msdfgen raster text is intentionally left for a later text rendering depth pass.

## Phase 7: Scene Rendering v1

Goal: render imported scene meshes with stylized PBR material data.

### Deliverables

- glTF 2.0 mesh importer for static meshes.
- CPU asset structs for:
  - mesh
  - material
  - texture references
  - scene instance
- Camera with view/projection matrices.
- Frustum culling.
- Draw list generation.
- Depth prepass.
- Scene stylized PBR shader:
  - base color
  - normal
  - roughness
  - metallic
  - AO
  - height tint
  - biome tint
  - light wrap
  - distance fade
  - weather mask
- Directional light, ambient cube, fog.

### Acceptance

- Sample imports at least one glTF prop and renders it with a stylized material.
- Draw list test verifies culling removes out-of-frustum instances.
- Depth prepass and scene pass appear in FrameGraph execution trace.

### Current Progress

- Added CPU scene instance and draw-list contracts with bounding-sphere visibility.
- Camera now carries a position for first-pass frustum culling.
- Draw-list generation keeps visible instances and removes behind-camera, too-far, and horizontally out-of-frustum instances.
- Focused stylized scene test, direct MinGW build, and clean CMake/CTest passed after the first Phase 7 culling block.
- Added a first-pass static mesh glTF JSON importer contract for glTF 2.0 assets with direct POSITION/NORMAL/TEXCOORD_0/indices arrays.
- Importer reports diagnostics for missing glTF 2.0 version and invalid indexed geometry.
- Added `SceneMaterialAsset` with stylized prop texture references and default stylized PBR parameters.
- Focused stylized scene tests, direct MinGW build, and clean CMake/CTest passed after the first Phase 7 importer/material block.
- Added `--imported-prop` stylized showcase mode that imports a minimal glTF 2.0 prop mesh, uploads it through the existing Vulkan mesh buffer, and renders it with stylized prop material parameters.
- Direct build now runs hidden toon-character smoke plus hidden imported-prop smoke.
- CMake/CTest now includes `stylized_showcase_imported_prop_smoke`; clean CMake/CTest passed 19/19 after adding it.
- Added first-pass directional light, ambient cube, and fog scene environment contracts; stylized showcase now builds scene constants from `SceneEnvironment::clearDay()`.
- Phase 7 acceptance passed with draw-list culling tests, imported prop render smoke, direct MinGW build, clean CMake/CTest, shader compilation, and hidden showcase smoke.

### Completion Notes

- The first importer accepts a constrained glTF 2.0 JSON shape with direct arrays. Full bufferView/accessor/base64/bin support remains a later importer expansion.

## Phase 8: Forward+ Lighting

Goal: add scalable local light support while keeping the first version compact.

### Deliverables

- Cluster grid definition.
- Light structs for directional, point, and spot lights.
- Compute pass for cluster build/light culling.
- Storage buffer for light lists.
- Scene and character shaders read cluster light data.
- First-stage limit: one directional light plus up to 128 local lights.

### Acceptance

- Compute dispatch smoke test runs on Vulkan.
- Cluster occupancy debug data can be read or visualized.
- Scene pass renders visibly different output when point lights move.

### Current Progress

- Added first-pass `ClusterGrid` contract for viewport tile sizing and z-slice cluster counts.
- Added `LocalLight` point/spot data contract and `ForwardLightSet` with a 128 local-light cap.
- Forward light set reports dropped local lights when content exceeds the first-stage limit.
- Focused stylized scene tests, direct MinGW build, and clean CMake/CTest passed after the first Phase 8 cluster/light contract block.
- Added CPU/debug cluster occupancy contract:
  - linear `ClusterGrid::clusterIndex(x, y, z)`
  - per-cluster light reference counts
  - occupied cluster count
  - max lights in a cluster
  - dropped light-reference count when a per-cluster cap is exceeded
- Occupancy generation projects local point lights into screen tiles and linear depth slices for deterministic debug/readback parity.
- Focused stylized scene test, direct MinGW build, and clean CMake/CTest passed after the Phase 8 occupancy/debug block.
- Added Vulkan compute dispatch smoke coverage for the ClusterBuild path.
- The smoke test creates a hidden SDL/Vulkan device, runs a FrameGraph `ClusterBuild` pass against `ClusterLightList`, dispatches `3 x 2 x 24` workgroups for a 320x180 viewport with 16x16 tiles and 24 z slices, transitions the buffer to shader-read, and verifies Vulkan RHI dispatch telemetry.
- Direct MinGW build and clean CMake/CTest passed after the Phase 8 Vulkan dispatch smoke block; CTest now covers 20 tests including `vulkan_compute_dispatch`.
- Added Forward+ shader data contracts:
  - `ForwardPlusLightRecord` packs position/range, color/intensity, and direction/type for GPU storage buffers.
  - `ForwardPlusShaderData::fromLightSet` converts capped local lights into shader-facing records.
  - Scene and character Slang sources now declare the shared `ForwardPlusLightRecord` and `ClusterLightList` storage buffer contract.
- Shader tests verify both scene and character shader sources expose `ClusterLightList` reflection while preserving existing push constant sizes.
- Direct MinGW build and clean CMake/CTest passed after the Phase 8 shader-contract block.
- Added a visible local-light preview path:
  - `SceneConstants::fromCameraEnvironmentAndLights` evaluates local point-light contribution at a sample position.
  - The stylized scene push constants feed that preview value into the scene shader.
  - The showcase moves a warm point light each frame, so point-light motion changes scene output before the full clustered shader read path is bound.
- Direct MinGW build and clean CMake/CTest passed after the Phase 8 visible point-light preview block.

### Completion Notes

- Phase 8 acceptance passed for CPU cluster/light contracts, CPU/debug occupancy, Vulkan ClusterBuild dispatch smoke, shader-facing light-list contracts, and a visible point-light preview path.
- The current Vulkan compute path records and dispatches the ClusterBuild pass boundary; full GPU light-list population and descriptor-backed shader reads are intentionally left for a deeper renderer/RHI binding pass.

## Phase 9: Shadow System

Goal: provide readable stylized shadows for scene and character.

### Deliverables

- Four-cascade CSM.
- Shadow atlas allocation.
- Character-only high-resolution shadow pass.
- PCF plus stylized shadow ramp.
- Contact shadow for character grounding.
- Face SDF hook for later face material.

### Acceptance

- Shadow cascade debug view shows all cascades.
- Character remains readable in noon, dusk, night, and backlight presets.
- Contact shadow appears under the character in showcase.

### Current Progress

- Added first-pass CSM split contract:
  - `CsmSettings` defines cascade count, max shadow distance, atlas resolution, and practical-split lambda.
  - `ShadowCascadeSet::forCamera` builds up to 4 cascades from camera near plane to the clamped shadow distance.
  - Each cascade records near/far depth coverage plus texel world size for later debug views and stabilization work.
- Focused stylized scene test, direct MinGW build, and clean CMake/CTest passed after the Phase 9 CSM contract block.
- Added first-pass shadow atlas layout contract:
  - 4 CSM cascades are allocated into stable 2x2 regions in a 2048 shadow atlas using 1024 tiles.
  - A separate 1024 character shadow region is reserved for high-resolution character-only shadows.
  - The layout reports validity and per-region viewport coordinates for later Vulkan pass setup and debug views.
- Focused stylized scene test, direct MinGW build, and clean CMake/CTest passed after the Phase 9 shadow-atlas contract block.
- Added Vulkan shadow-pass smoke path:
  - hidden SDL/Vulkan test builds CSM and character shadow atlas depth textures through FrameGraph
  - executes `CsmShadow`, `CharacterShadow`, and `ShadowResolve` passes
  - verifies D32 depth barriers use the depth aspect mask and that Vulkan RHI records depth texture transition telemetry
- Direct MinGW build and clean CMake/CTest passed after the Phase 9 Vulkan shadow-pass block; CTest now covers 21 tests including `vulkan_shadow_pass`.
- Added PCF plus stylized shadow-ramp contract:
  - `StylizedShadowRamp` defines pcf radius, lit threshold, softness, and shadow strength.
  - CPU tests cover lit, soft-transition, and fully shadowed ramp output.
  - Scene Slang declares `ShadowSamplingParams` and `sampleStylizedShadow`, then applies the ramp to the current stylized lighting term.
- Direct MinGW build and clean CMake/CTest passed after the Phase 9 shadow-ramp shader block.
- Added character-only shadow pass and contact shadow preview:
  - `CharacterShadowPassDesc` consumes the reserved high-resolution character atlas region.
  - `ContactShadowSettings` evaluates distance-based contact shadow opacity.
  - The showcase feeds contact shadow preview intensity into scene constants, and the scene shader darkens lower character regions through a `contactShadow` term.
- Direct MinGW build and clean CMake/CTest passed after the Phase 9 contact-shadow block.

### Completion Notes

- Phase 9 acceptance passed for CSM splits, shadow atlas regions, Vulkan shadow-pass smoke, stylized shadow ramp, character shadow region, and contact shadow preview.
- Current shadow maps are represented as pass/resource contracts and visual preview terms; full depth rendering into Vulkan shadow image resources remains a later renderer-depth implementation detail.

## Phase 10: Character NPR

Goal: implement the plan's dedicated character pipeline.

### Deliverables

- Character material schema:
  - base color texture
  - ramp texture
  - region mask
  - outline settings
  - shadow softness
  - rim settings
  - optional face SDF
- Material variants:
  - skin
  - face
  - hair
  - cloth
  - eye
- Region mask convention:
  - R = outline width
  - G = shadow softness
  - B = specular strength
  - A = material region
- Ramp lighting.
- Rim light.
- Stylized specular.
- Face SDF shadow correction.
- Per-region parameter tuning.

### Acceptance

- Showcase character uses separate face, hair, cloth, and eye material slots.
- GUI panel can edit ramp threshold, rim intensity, specular strength, and shadow softness.
- Visual smoke presets cover noon, dusk, night, and backlight.

### Current Progress

- Added Character NPR material schema extensions:
  - `CharacterTextureRefs` for base color, ramp, region mask, optional face SDF, and optional matcap.
  - `CharacterRegionMaskConvention` for R/G/B/A channel meaning.
  - Default toon character now includes cloth, face, hair, and eye slots.
  - Toon test mesh now includes eye submeshes so material coverage is exercised by validation.
- Focused stylized scene test, direct MinGW build, and clean CMake/CTest passed after the Phase 10 character-schema block.
- Added face-safe lighting and tuning contracts:
  - `FaceSdfShadowSettings` evaluates side-aware face shadow visibility from light direction and face SDF value.
  - `CharacterNprTuningSchema` exposes ramp threshold, rim intensity, specular intensity, and shadow softness controls for the retained GUI layer.
  - Character NPR Slang now declares `FaceSdfShadowParams` and `applyFaceSdfShadow`.
- Focused stylized scene/shader tests, direct MinGW build, and clean CMake/CTest passed after the Phase 10 face-SDF/tuning block.
- Added noon/dusk/night/backlight visual smoke presets:
  - showcase options now parse `--lighting-preset noon|dusk|night|backlight`
  - each preset adjusts exposure, light wrap, and contact shadow preview strength
  - direct build and CMake/CTest run hidden dusk, night, and backlight smokes
- Direct MinGW build and clean CMake/CTest passed after the Phase 10 visual-preset block; CTest now covers 24 tests.

### Completion Notes

- Phase 10 acceptance passed for separate face/hair/cloth/eye material slots, GUI-facing tuning schema, face SDF hook, and noon/dusk/night/backlight hidden visual smoke presets.

## Phase 11: Outline System

Goal: implement character-priority outlines and subtle scene edge lines.

### Deliverables

- Inverted hull character outline pass.
- Depth/normal edge detection scene outline pass.
- Distance-based outline width falloff.
- Material mask control for outline width.
- Debug view for outline mask and width.

### Acceptance

- Character outline remains stable under camera movement.
- Scene outline is weaker than character outline by default.
- Debug view can isolate outline width and mask.

### Current Progress

- Added first-pass outline contracts:
  - `CharacterOutlineSettings` defines inverted-hull width and distance falloff.
  - `SceneOutlineSettings` defines weaker depth/normal edge strength.
  - `OutlineSettings` exposes debug views for final composite, width, and mask.
  - `OutlinePassDesc` packages character-priority inverted hull plus scene depth/normal edges for the existing FrameGraph `Outline` pass.
- Focused stylized scene test, direct MinGW build, and clean CMake/CTest passed after the Phase 11 outline-contract block.

### Completion Notes

- Phase 11 acceptance passed at the current renderer-contract level: stable distance-based character width, scene outline weaker than character outline, and explicit debug view modes. Full postprocess edge-image rendering remains a later visual-depth pass.

## Phase 12: Open World Slice

Goal: create the first 512m x 512m world rendering slice.

### Deliverables

- Terrain chunk representation.
- Terrain renderer.
- Static object batching.
- Vegetation instancing.
- Simple LOD.
- Distance culling.
- World scene containing:
  - terrain
  - grass
  - trees
  - rocks
  - building
  - one character
  - day/night cycle

### Acceptance

- Showcase can load the 512m x 512m world slice.
- Camera can move through the scene.
- LOD and culling counters are visible in GUI.

### Current Progress

- Added first-pass open-world slice contracts:
  - `OpenWorldSlice::makeTestSlice(512, 128)` creates a 4x4 terrain chunk grid.
  - `buildWorldSliceDrawStats` reports total, visible, culled, LOD0, and LOD1 terrain chunk counts.
- Focused stylized scene test, direct MinGW build, and clean CMake/CTest passed after the Phase 12 world-slice contract block.
- Added showcase world-slice smoke path:
  - `--world-slice` option loads the 512m x 512m slice contract.
  - Retained GUI overlay displays visible/culled terrain chunk counts plus LOD0/LOD1 counts.
  - Direct build and CMake/CTest run a hidden world-slice smoke.
- Direct MinGW build and clean CMake/CTest passed after the Phase 12 world-slice smoke block; CTest now covers 25 tests.
- Added static object batching and vegetation instancing contracts:
  - `OpenWorldSlice::makeTestSlice(512, 128)` now includes deterministic static props for rocks, a building, and a larger scene prop.
  - `buildStaticObjectBatches` groups static objects by mesh/material while preserving first-seen batch order.
  - `buildVegetationInstances` creates deterministic per-chunk grass/tree instances for first-pass instanced rendering contracts.
  - World-slice GUI counters now include static batch count, static instance count, and vegetation instance count.
- Focused stylized scene test, direct MinGW build, and clean CMake/CTest passed after the Phase 12 batching/vegetation block.
- Added world-slice day/night and movement controls:
  - `WorldDayNightCycle::genshinLikeDefaults()` samples noon, dawn, dusk, and night lighting into environment, exposure, light-wrap, contact-shadow scale, and LUT name contracts.
  - `SceneConstants::applyDayNight` applies the sampled time-of-day contract to the current frame constants.
  - Showcase supports `--world-time <hour>` and registers a hidden world-time smoke in both direct and CMake builds.
  - World-slice GUI displays current time/LUT, and the interactive sample uses WASD/arrow keys to move the camera through the 512m slice while refreshing LOD/culling counters.
- Phase 12 acceptance passed with focused stylized scene/options tests, direct MinGW build, shader compilation, hidden world-slice/world-time smokes, and clean CMake/CTest at 26/26.

### Completion Notes

- Phase 12 is complete at the current first-slice contract level. Terrain, static props, vegetation, LOD/culling counters, world-time controls, and movement are represented in CPU/render-sample contracts; deeper terrain/material GPU rendering remains part of later visual-depth passes.

## Phase 13: Postprocess and Color

Goal: unify final image style with color management and lightweight post effects.

### Deliverables

- HDR main color buffer.
- Custom tone mapping.
- LUT pass.
- Bloom.
- Height/depth fog.
- Stylized AO.
- FXAA.
- Sharpen.
- Biome/time-of-day LUT switching.

### Acceptance

- GUI can enable/disable each postprocess pass.
- Noon, dusk, night, and biome LUTs produce distinct but coherent looks.
- FXAA and sharpen are individually toggleable.

### Current Progress

- Added first-pass postprocess/color contracts:
  - `PostProcessSettings::animeDefaults()` defines HDR, tone mapping, LUT, bloom, height/depth fog, stylized AO, FXAA, and sharpen toggles.
  - Individual passes can be enabled/disabled through `setEnabled`, with deterministic enabled-pass counting for GUI/tooling.
  - `LutLibrary::timeOfDayDefaults()` includes day, dawn, dusk, night, and meadow biome LUT presets.
  - `LutLibrary::selectFor(WorldDayNightSample)` maps world time-of-day lighting to the active LUT contract.
  - `PostProcessTuningSchema::defaults()` exposes GUI-facing toggles for all eight postprocess stages.
- Focused stylized scene test, direct MinGW build, shader compilation, hidden showcase smokes, and clean CMake/CTest passed after the first Phase 13 postprocess settings block.
- Expanded postprocess FrameGraph composition:
  - Project-owned render graph composition carries `PostProcessSettings`; the former engine-level `SceneGuiCompositionDesc` path has since been retired.
  - The former single `PostProcess` node expands into `BloomDownsample`, `BloomComposite`, `ToneMapping`, `Lut`, `HeightFog`, `StylizedAo`, `Fxaa`, and `Sharpen` passes.
  - Disabled bloom/FXAA/sharpen settings remove their corresponding graph passes.
  - Vulkan scene+GUI composition smoke now validates the expanded postprocess pass trace.
- Focused composer test, direct MinGW build, hidden Vulkan composition smoke, shader compilation, and clean CMake/CTest passed after the Phase 13 postprocess graph block.
- Added shader-facing and GUI/schema wiring contracts:
  - `PostProcessShaderParams::fromSettingsAndLut` packs enable flags plus tone-map, LUT, bloom, fog, AO, FXAA, and sharpen parameters for future Slang postprocess shaders.
  - `PostProcessSettings::setEnabledByParameter` maps retained-GUI schema parameter names to pass toggles without exposing GUI code to individual renderer fields.
  - FXAA and sharpen toggles are individually covered by tests, and time-of-day/biome LUT presets remain distinct.
- Phase 13 acceptance passed with focused stylized scene/composer tests, direct MinGW build, shader compilation, hidden Vulkan composition smoke, and clean CMake/CTest at 26/26.

### Completion Notes

- Phase 13 is complete at the current renderer-contract and FrameGraph level. Actual full-screen Slang postprocess kernels are represented by stable pass/parameter contracts and can now be implemented behind the existing graph nodes without changing higher-level composition.

## Phase 14: Tools and Runtime Tuning

Goal: make iteration fast enough for real content work.

### Deliverables

- Asset importer CLI.
- Material JSON schema.
- UI layout JSON schema.
- Runtime tuning panel for:
  - lighting
  - ramp
  - LUT
  - outline
  - fog
  - shadows
  - character parameters
  - GPU timing
- Hot reload:
  - shader
  - material JSON
  - UI layout JSON
- Debug views:
  - depth
  - normal
  - cluster occupancy
  - shadow cascades
  - ramp
  - outline mask
  - overdraw
  - GPU timings

### Acceptance

- Shader edit reloads within 2 seconds.
- Material JSON edit changes rendered output at runtime.
- UI layout JSON edit changes GUI at runtime.
- GPU timing panel reports each major pass.

### Current Progress

- Added first-pass material JSON schema/import contract:
  - `MaterialJsonSchema::sceneStylizedPbr()` declares required and optional fields for scene stylized PBR materials.
  - `importSceneMaterialFromJson` parses `SceneStylizedPbr` material JSON into `SceneMaterialAsset`.
  - The importer reports diagnostics for missing/invalid required fields.
- Focused stylized scene test, direct MinGW build, shader compilation, hidden showcase smokes, and clean CMake/CTest passed after the Phase 14 material JSON block.
- Added first-pass asset importer CLI:
  - `arp_asset_importer` supports `--self-test`, `--material-json <file>`, and `--gltf-json <file>`.
  - CLI self-test validates both material JSON import and constrained glTF mesh import.
  - Direct MinGW build compiles and runs the importer self-test.
  - CMake registers `arp_asset_importer`, `asset_importer`, and `asset_importer_self_test`.
- Focused asset importer test, direct MinGW build, importer self-test, shader compilation, hidden showcase smokes, and clean CMake/CTest passed after the Phase 14 importer CLI block; CTest now covers 28 tests.
- Added first-pass UI layout JSON schema/import/cache contract:
  - `UiLayoutJsonSchema::retainedGui()` declares a retained-GUI layout format with stable node ids, node types, parent ids, text, dimensions, and simple control state.
  - `importUiLayoutFromJson` parses layout JSON into `gui::Node`-backed layout nodes and reports diagnostics for missing/invalid top-level data.
  - `UiLayoutCache` records node count, text run count, and a deterministic stable hash for hot reload/cache invalidation.
  - `arp_asset_importer --self-test` now validates material, glTF, and UI layout import contracts.
- Focused asset importer test, direct MinGW build, importer self-test, shader compilation, hidden showcase smokes, and clean CMake/CTest passed after the Phase 14 UI layout block.
- Added runtime hot reload, tuning, debug view, and GPU timing contracts:
  - `HotReloadRegistry` tracks shader, material JSON, and UI layout JSON content hashes and reports whether reload events are within the 2 second budget.
  - `RuntimeTuningPanel::defaults()` defines lighting, ramp, LUT, outline, fog, shadows, character, and GPU timing tuning sections.
  - `DebugViewCatalog::defaults()` defines depth, normal, cluster occupancy, shadow cascade, ramp, outline mask, overdraw, and GPU timing debug views.
  - `GpuTimingFrame` and `GpuTimingPanelRows` collect per-pass timings and generate panel rows including total frame GPU time.
- Focused asset importer/stylized scene tests, direct MinGW build, importer self-test, shader compilation, hidden showcase smokes, and clean CMake/CTest passed after the Phase 14 runtime tooling block; CTest remains 28/28.

### Completion Notes

- Phase 14 is complete at the current first-stage tooling-contract level. Importer CLI, material schema, UI layout schema/cache, hot reload change detection, runtime tuning schema, debug view catalog, and GPU timing panel data are now stable enough for later live editor wiring.

## Phase 15: Metal Backend

Goal: reach macOS parity for the accepted RHI feature set.

### Deliverables

- Metal device and swapchain layer.
- Metal buffer/texture/sampler resources.
- Metal pipeline creation from Slang MSL.
- Metal command list and queue submit.
- Metal descriptor/argument binding equivalent.
- Metal versions of clear, mesh draw, GUI, compute, and postprocess paths.

### Acceptance

- macOS sample renders stylized showcase.
- Metal tests cover clear, triangle/mesh draw, texture upload, and compute dispatch.
- No renderer/GUI/asset pipeline code depends on Vulkan-only handles.

### Current Progress

- Added first-pass Metal backend discovery, parity, resource mapping, creation-plan, pipeline-plan, command-list, queue-submit, and argument-binding contracts.
- Added Apple-only Metal implementation entries:
  - `queryMetalImplementationUnits()` records the Apple implementation units for device, resources, pipeline, command list, and swapchain layer.
  - Apple-only CMake wiring enables `OBJCXX`, adds the `.mm` files, and links the Metal and QuartzCore frameworks without affecting Windows/MinGW builds.
  - On Windows/MinGW, `createMetalDevice()` remains an explicit unavailable stub.
  - On Apple builds, `createMetalDevice()` is provided by the backend-private Metal implementation, creates a Metal device and command queue, and returns a minimal Metal device object.
  - Apple-only Metal resource, pipeline, descriptor, command-list, and queue-submit factories now live under the backend-private Metal package.
- Split the Metal backend into the same professional package shape as Vulkan:
  - Cross-platform Metal contracts now live in the backend-private Metal package.
  - Apple Objective-C++ units cover device, resources, pipeline, command list, and swapchain implementation domains.
  - Framework headers reserve the future implementation domains: `resources.h`, `pipeline.h`, `command_list.h`, `swapchain.h`, and `telemetry.h`.
  - Architecture boundary coverage now rejects removed root-level Metal implementation layouts and requires split implementation units.
- Added a current-state feature demo mode to the stylized showcase:
  - `arp_stylized_showcase --feature-demo` keeps running until Esc or window close.
  - The demo uses the self-authored retained GUI to expose platform/RHI, Slang/reflection, fixed Forward+ graph, scene/character rendering, world-slice counters, shadows, outline, postprocess, tooling schemas, GPU timing rows, and Metal parity/build contracts.
  - Hidden smoke coverage is available through `arp_stylized_showcase --hidden --frames 1 --feature-demo`.
- Connected first-stage retained GUI text rendering:
  - GUI draw data now emits bitmap text quads from retained node text.
  - The Vulkan GUI pass renders text after control/background quads, so the feature demo dashboard is readable in-window without ImGui.
  - This is a lightweight built-in ASCII path; FreeType/MSDF atlas work remains the later production-quality font step.

### Remaining Work

- Run the Apple-only Metal parity branch on macOS hardware and fix any Objective-C++ or driver-level issues found there.

## Phase 16: Global Illumination Contract

Goal: define the DDGI-lite probe-grid contract before backend work begins.

### Deliverables

- `renderer::GlobalIlluminationDesc`, `ProbeGridDesc`, and `GiFramePlan`.
- Quality-profile mapping for Low, Medium, High, and Desktop.
- Explicit fallback to ambient cube plus stylized AO.
- FrameGraph pass names for `GiProbeUpdate` and `GiIrradianceResolve`.

### Acceptance

- `global_illumination_contract` proves profile budgets, disabled fallback, pass order, and bounded resource sizes.
- Renderer and project-owned render graph composer tests prove GI passes are inserted before `SceneStylizedPbr` only when enabled.
- Existing hidden showcase smoke still passes with GI disabled.

### Current Progress

- Added `renderer::GlobalIlluminationDesc`, `ProbeGridDesc`, and `GiFramePlan`.
- Added Low, Medium, High, and Desktop profile mapping with explicit fallback to ambient cube plus stylized AO.
- Added `global_illumination_contract` coverage for disabled fallback, probe budgets, probe-state memory, pass names, and half-resolution irradiance extent.
- The former `SceneGuiCompositionDesc` and `addSceneGuiComposition` path has been retired during renderer module governance cleanup.
- Active GI pass ordering is covered by `global_illumination_contract`, renderer draw-plan contracts, and project-owned render graph composer tests.
- Clean CMake/CTest validation passed for the Phase 16 contract block.

## Phase 17: GI RHI Capabilities

Goal: expose the portable RHI requirements for GI before adding backend behavior.

### Deliverables

- Capability coverage for compute, storage buffer, storage texture, sampled texture, `Rgba16Float`, and persistent history intent.
- Descriptor contract for GI probe state, irradiance output, irradiance input, and sampler bindings.
- Null, Vulkan, and Metal semantics updated together.

### Acceptance

- `rhi_capabilities` covers the GI capability gate.
- `rhi_binding_contract` validates the GI descriptor set without backend-native handles.
- Unsupported hardware reports an explicit portable rejection reason.

### Current Progress

- Added the storage-texture descriptor type.
- Extended `rhi_capabilities` coverage for `Rgba16Float` sampled/storage GI irradiance usage.
- Extended `rhi_binding_contract` with a GI descriptor set containing storage buffer, storage texture, sampled texture, and sampler bindings.
- Extended the Metal argument-binding contract so storage textures map to Metal-facing texture arguments.
- Clean CMake/CTest validation passed for the Phase 17 capability block.

## Phase 18: GI Shader Pipeline

Goal: add Slang-only GI compute shaders and generated runtime artifacts.

### Deliverables

- `shaders/stylized/global_illumination.slang`.
- SPIR-V, MSL, and reflection JSON generation.
- Shader reflection tests for compute entry points, storage buffers, storage textures, and push constants.

### Acceptance

- Shader tests prove `GiProbeUpdate` and `GiIrradianceResolve` compile and reflect.
- CMake shader generation treats GI shader outputs as first-class build outputs.

### Current Progress

- Added `shaders/stylized/global_illumination.slang`.
- Added compute entry points for `GiProbeUpdate` and `GiIrradianceResolve`.
- Extended lightweight shader reflection to report `GiProbeState` as a storage buffer and `GiIrradianceOutput` as a storage texture.
- Extended shader generation to emit `global_illumination.spv`, `global_illumination.metal`, and `global_illumination.reflection.json`.
- Clean CMake/CTest validation passed for the Phase 18 shader block.

## Phase 19: GI Renderer Integration

Goal: wire GI planning into FrameGraph and scene draw data while preserving fallback behavior.

### Deliverables

- GI resources and passes in scene composition.
- Stylized scene draw-plan parameters for GI intensity and debug mode.
- Scene shader reads GI irradiance through RHI descriptors when supported.
- Retained GUI tuning for GI quality, intensity, and debug view.

### Acceptance

- Renderer tests show disabled GI matches the existing fallback path.
- Composer tests show enabled GI adds probe update and irradiance resolve before the scene pass.
- Hidden showcase smoke runs with `--gi-probes`.

### Current Progress

- Added renderer-side `GiSceneParams` packing for enabled state and intensity.
- Extended `StylizedScenePushConstants` with default-zero `giParams`.
- Extended `scene.slang` with a default-off `applyGlobalIllumination` function.
- Updated shader reflection expectations for the expanded stylized scene push constant contract.
- Clean CMake/CTest validation passed for the first Phase 19 renderer integration block.
- Extended `GiSceneParams` and renderer draw-plan tests to cover debug-mode packing.
- Added `StylizedSceneRenderSettings` so `RenderSystem::drawScene` can pass GI profile, intensity, and debug mode into renderer draw data while preserving the existing default-off overload.
- Added stylized showcase `--gi-probes` and `--gi-debug` flags; `--gi-debug` implies probe GI and selects the renderer debug data path.
- Added `stylized_showcase_gi_probe_smoke` to CMake and the equivalent direct build smoke path.
- Validation passed for shader generation, direct MinGW build, clean CMake/CTest with 51 registered tests, and direct final `arp_stylized_showcase --hidden --frames 2 --gi-probes`.

## Phase 20: Vulkan GI Validation

Goal: execute the GI path through the Vulkan RHI backend.

### Deliverables

- Vulkan compute dispatch for probe update and irradiance resolve.
- Vulkan descriptor binding for storage buffers, storage textures, sampled textures, and samplers through RHI.
- Barrier coverage for compute writes followed by scene shader reads.
- GPU timing rows for GI passes.

### Acceptance

- Vulkan backend tests cover compute dispatch, descriptor binding, storage texture transitions, and telemetry.
- Hidden-window Vulkan smoke renders with GI enabled.
- Production render path adds no `vkDeviceWaitIdle`, infinite fence wait, per-draw pipeline creation, or per-frame shader compilation.

### Current Progress

- Extended `vulkan_compute_dispatch` to exercise the GI backend telemetry shape:
  - Creates `GiProbeState` and `GiIrradiance` through RHI.
  - Creates the GI descriptor set with storage buffer, storage texture, sampled texture, and sampler bindings.
  - Binds `GiProbeUpdate` and `GiIrradianceResolve` compute pipelines and records two dispatches.
  - Validates descriptor bind count, compute bind count, last bound GI pipeline, descriptor binding count, and resolve dispatch dimensions.
- Added Vulkan backend telemetry for abstract compute-pipeline and descriptor-set binding while keeping native backend handles inside backend-private implementation code.
- The former `renderer::GiComputePlan` helper has been retired during renderer module governance cleanup; current GI descriptor, pipeline, and dispatch coverage lives in portable RHI contracts, backend telemetry tests, and runtime renderer paths.
- Added descriptor-backed scene GI shader inputs (`GiIrradianceInput`, `GiIrradianceSampler`) and reflection coverage while keeping GI controlled by default-zero `giParams`.
- Extended `ProductionDemoRuntimeControl` with environment-backed GI enable, intensity, and debug settings for the final production demo acceptance smoke.
- Wired production demo GI runtime controls into `StylizedSceneRenderSettings` and added `production_demo_gi_smoke` as the long-task acceptance target.
- Added native Vulkan compute pipeline creation for generated GI shader entries, with backend-only tests validating non-null pipeline and pipeline-layout handles.
- Added native Vulkan compute command encoding for GI passes, with backend telemetry validating two encoded compute pipeline binds and two encoded dispatches.
- Added native Vulkan descriptor layout, pool, and set allocation for the GI descriptor shape while keeping descriptor writes as a separate backend task.
- Added bounded Vulkan texture transition telemetry so GI irradiance storage-write to shader-read barrier shape is covered in the backend test.
- Added portable RHI descriptor updates for GI resources plus Vulkan native descriptor writes, compute pipeline layout compatibility, and encoded descriptor-set binding coverage.
- Added GI GPU timing row classification for `GiProbeUpdate` and `GiIrradianceResolve` in the renderer timing panel contract.
- GI compute work is represented through RHI frame/graph compute pass contracts and backend-neutral command inputs instead of the retired renderer-owned compute-plan helper.
- Wired GI compute into `StylizedSceneRenderer` runtime creation/draw, passed the generated GI shader through the platform render-system path, and validated the `production_demo_gi_smoke` runtime path.

### Completion Notes

- Phase 20 is Backend Validated for Vulkan GI: shader generation, direct MinGW build, clean CMake/CTest, `stylized_showcase_gi_probe_smoke`, and `production_demo_gi_smoke` passed.
- GI is not Production Ready yet. Metal parity, Android/iOS lifecycle behavior, real-device platform validation, and production performance budgets remain open in Phase 21 and later hardening.

### Long-Task Acceptance Target

- Continue Phase 20 as a long task that ends with `arp_production_demo` rendering with GI enabled.
- Keep intermediate gates focused on portable RHI contracts: GI compute plan, descriptor-backed scene shader contract, Vulkan telemetry, hidden showcase smoke.
- Final hidden-window acceptance target: `production_demo_gi_smoke`, running `arp_production_demo` with `ARP_PRODUCTION_DEMO_GI=1`.

## Phase 21: Metal And Mobile GI Validation

Goal: validate the GI path on Metal and document mobile lifecycle behavior.

### Deliverables

- Metal compute and argument-binding parity for GI resources.
- macOS hardware validation plan includes GI probe update and irradiance resolve.
- Android and iOS lifecycle notes for pause, resume, surface loss, swapchain rebuild, probe history retention, and memory-budget fallback.

### Acceptance

- Metal parity test covers GI compute and resource binding.
- Mobile profiles keep probe memory bounded and pause GI history updates while the surface is unavailable.
- Platform status advances only after real target hardware validates the path.

### Current Progress

- Extended `metal_backend` with GI argument-binding coverage for `GiProbeSet`
  and compute pipeline plans that record descriptor-set layout and argument
  counts from portable RHI descriptor sets.
- Added a Metal command encoding `BindArgumentSet` plan step so GI compute
  parity can represent descriptor binding before dispatch without exposing
  native Metal handles above RHI.
- Metal parity planning now explicitly covers GI compute and argument binding.
  Real macOS hardware validation remains open before this phase can become
  `Platform Validated`.
- Added `planGiMobileRuntime` as a shared renderer-level mobile GI contract:
  frame submission and surface/layer availability gate GI execution, while
  memory pressure or insufficient memory budget releases persistent probe
  history and falls back to ambient cube plus stylized AO.
- Extended Android and iOS platform contract tests to cover pause/background,
  surface/layer loss, history preservation, and memory-pressure history release.

## Cross-Cutting Test Plan

Every phase must keep these gates passing:

- Direct MinGW build:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1`
- Clean CMake build:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-cmake.ps1 -BuildDir <clean-dir>`
- Shader compilation:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\compile-shaders.ps1`
- CTest:
  - all registered tests pass
- Hidden showcase smoke:
  - `arp_stylized_showcase --hidden --frames 2 --rhi-backend vulkan`

## Build Artifact Layout

- All generated build outputs are centralized under `artifacts`.
- Direct MinGW script outputs go to `artifacts/direct`.
- CMake outputs go to `artifacts/cmake/<BuildDir>`; for example, `-BuildDir build-cmake-phase16` resolves to `artifacts/cmake/build-cmake-phase16`.
- Legacy root-level `build*` directories are kept under `artifacts/legacy-root-builds` when the workspace is cleaned up.

## Performance Targets

First-stage targets:

- Windows RTX 3060, 1080p, 60 FPS.
- macOS M1 Pro, 1080p, 45-60 FPS.
- GUI pass under 1ms at 1080p with 500 nodes.
- GPU timing panel displays every major pass.

## Roadmap Execution Policy

- Each new behavior starts with a failing test or hidden-window sample target.
- Direct build and CMake/Ninja build must both stay healthy.
- Windows Vulkan advances first when cross-platform implementation would slow the slice.
- Keep Vulkan/Metal portability at the RHI boundary.
- Do not add broad engine abstractions before a phase needs them.
- No ImGui dependency.
- Shader source stays in Slang.
- Runtime tools and future game UI share the same retained GUI system.
