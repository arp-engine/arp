# Engine Technical Evolution Blueprint

Status: Contract Complete for planning.

Date: 2026-05-13

## Purpose

This blueprint turns the current Anime Render Pipeline codebase into a commercial-engine-shaped runtime and toolchain. The user-facing mental model should feel close to Unity: Project, Scene, GameObject, Component, Prefab, Material, Shader, Animator Controller, Import Settings, Addressables, Quality Settings, and Render Pipeline Asset. Internally, the implementation should borrow the strongest ideas from Unity, Unreal, Godot, O3DE, modern frame graphs, and mobile-first renderers while preserving this repository's existing boundaries:

- Rendering work is expressed through RHI devices, command encoders, FrameGraph, renderer draw plans, or render pipeline features.
- `src/renderer`, `src/gui`, and `src/render_graph` remain free of Vulkan, Metal, Android, and iOS native graphics headers.
- New RHI capability work starts with contract tests, then Null, Vulkan, and Metal semantics.
- Shader source stays Slang-only, with generated SPIR-V, MSL, and reflection.
- Runtime hot paths avoid unbounded allocation, file IO, network IO, shader compilation, and blocking GPU waits.
- Documentation status uses: Contract Complete, Prototype Complete, Backend Validated, Platform Validated, Production Ready.

## Current Engine Reading

The repository already has a solid rendering-library core:

- RHI: Null, Vulkan, and Metal contract shape, capability reporting, buffers, textures, descriptors, graphics and compute pipelines, command lists, swapchain contracts.
- FrameGraph: compiled planning for pass order, resource lifetime, barriers, aliasing, load/store policy, queue roles, and imported resources.
- Renderer: backend-neutral render pipeline interfaces, scene and GUI draw plans, GI compute planning, runtime scene handles, material and lighting contracts.
- Shader: Slang compile path, SPIR-V/MSL/reflection generation, reflection cache and pipeline validation.
- GUI: self-authored retained GUI with layout, input, draw data, text quads, and control tests.
- Assets: glTF/GLB/FBX/Unity character import contracts, material/UI JSON schemas, package manifest planning, offline importer CLI.
- Animation: skeleton, animation clips, CPU skinning, animator controller, Unity character importer/retargeting contracts.
- Platform: SDL3 windowing, Windows Vulkan render system, Metal split units, Android/iOS lifecycle contracts, platform matrix tooling.
- Samples/games: stylized showcase, production demo, material editor, character viewer, Project Lane gameplay slice.

The main gap is not "more rendering features"; it is a user-facing engine layer. The project needs a stable content model, asset database, scene/prefab serialization, editor/runtime split, package format, and system ownership vocabulary that content authors can understand without reading RHI or sample code.

## North Star

Build a compact stylized 3D game engine where the authoring experience is Unity-compatible in spirit and close in file semantics, while the runtime remains a C++17, mobile-first, RHI/FrameGraph-backed engine.

The engine should be pleasant for three groups:

- Game users: author scenes, prefabs, materials, animation controllers, UI documents, and addressable content with Unity-like concepts.
- Engine developers: extend systems through small object-oriented modules with explicit contracts and fast tests.
- Platform/backend developers: validate capabilities and lifecycle behavior without leaking native handles to renderer, samples, or gameplay code.

## Content Format Policy

Unity is the anchor for user-facing content. The engine does not need to be binary-compatible with every Unity asset, but names, concepts, import settings, and workflows should be familiar.

| User Concept | Preferred Format | Notes |
| --- | --- | --- |
| Project | `ProjectSettings/*.asset`, `Packages/manifest.json`, `Assets/` | Match Unity project layout where practical. |
| Asset identity | `.meta` file with GUID | GUID is stable identity; path is mutable location. |
| Scene | `.unity`-style YAML first, compiled package later | Authoring format can be text; runtime package is binary/cache-friendly. |
| Prefab | `.prefab`-style YAML first | Must support nested prefab and variant concepts before production. |
| GameObject | serialized object with name, active flag, transform, component list | Unity-compatible mental model; runtime may store compact ECS-like data. |
| Component | serialized object with type, enabled flag, fields | C++ components first; future scripting bridge can reuse this schema. |
| Material | `.mat`-style YAML/JSON with shader reference and properties | Runtime material table is reflection-validated. |
| Shader | Slang source plus Unity-like shader metadata | Do not introduce HLSL/ShaderLab as source; use metadata to express passes/properties. |
| Model import | `.fbx`, `.glb`, `.gltf` plus `.meta` import settings | Continue asset-layer import; runtime never parses source assets in frame hot paths. |
| Animator | `.controller`, Avatar, clips, masks | Existing Unity controller/importer work becomes the official authoring path. |
| UI | Unity UI Toolkit-inspired document or current retained GUI JSON | Runtime uses self-authored retained GUI. |
| Addressables | catalog + groups + labels | Runtime package loader uses deterministic catalogs and dependency bundles. |
| Quality settings | `ProjectSettings/QualitySettings.asset` | Drives render pipeline, mobile budgets, shadows, GI, postprocess, texture compression. |

## Architecture Layers

1. Authoring Layer

Editor-facing project, asset database, inspectors, import settings, scene/prefab files, and build profiles. This layer can use file IO, source formats, and importer dependencies outside runtime hot paths.

2. Runtime Content Layer

Compiled packages, manifests, GUID maps, serialized scene instances, prefab instantiation plans, material tables, animation graphs, UI layouts, and addressable catalogs.

3. Gameplay Layer

GameObject/Component API, scene lifecycle, transform hierarchy, update phases, input events, animation binding, audio/physics hooks, and save/load integration.

4. Renderer Layer

Render pipeline assets, renderer features, draw plan builders, material/light/world data extraction, culling, batching, GI/shadow/postprocess systems, and GUI render integration.

5. FrameGraph Layer

Compiled pass/resource plan, transitions, transient allocation, load/store policy, mobile tile hints, queue roles, imported backbuffer protection, and future async compute scheduling.

6. RHI and Backend Layer

Backend-neutral resources, descriptors, pipelines, command lists, capability model, synchronization, deferred destruction, Vulkan/Metal/Null implementations, and backend-only native tests.

7. Platform Layer

Windows, Android, macOS, iOS, and future Linux/Web runtime bootstrap: window/surface/layer lifecycle, swapchain, pause/resume, safe area, DPI, input, logging, and memory pressure.

## System Blueprints

### 1. Project, Asset Database, and GUID System

Commercial-engine concept:

- Unity: AssetDatabase, `.meta` GUIDs, import settings, project settings.
- Unreal: Asset Registry and cooked asset dependencies.
- Godot: resource paths and import remap files.

Target user model:

- A project has `Assets/`, `ProjectSettings/`, `Packages/`, `Library/`, and `Build/`.
- Every source asset has a `.meta` file with GUID, importer type, importer version, labels, bundle/addressable group, and platform overrides.
- Runtime never identifies content by raw path after import; it uses GUID and local file identifiers.

Implementation direction:

- Add an object-oriented `assetdb` module around `AssetDatabase`, `AssetGuid`, `AssetMeta`, `ImporterRegistry`, `ImportContext`, and `DependencyGraph`.
- Reuse existing `src/assets` import contracts and `tools/asset_importer` package planning.
- Keep source import and cache writes outside frame/update hot paths.
- Add deterministic cache keys from GUID, importer version, source hash, platform profile, and shader/material variants.

Milestones:

- Contract Complete: `.meta` parser/writer, GUID map, dependency graph, asset lookup tests.
- Prototype Complete: import folder scan and cache manifest generation for current samples.
- Backend Validated: packaged assets load through renderer/material/animation/UI runtime contracts.
- Production Ready: incremental import, corruption recovery, dependency invalidation, and build reproducibility.

Fast tests:

- Unit: parse `.meta`, preserve GUID across rename, detect duplicate GUID.
- Smoke: hidden sample loads a package by GUID without source glTF/JSON parsing.

### 2. Scene, GameObject, Component, and Prefab System

Commercial-engine concept:

- Unity: Scene, GameObject, Transform, MonoBehaviour-like component, Prefab, Prefab Variant.
- Unreal: World, Actor, Component, Blueprint class defaults.
- Godot: Scene tree and packed scenes.

Target user model:

- Users author `.unity` scenes and `.prefab` files with GameObjects and Components.
- `Transform` is mandatory and hierarchical.
- Prefabs support nested instances, overrides, variants, and apply/revert semantics.
- Runtime can internally flatten into component arrays and renderer-friendly handles.

Implementation direction:

- Add `src/scene` with `Scene`, `GameObject`, `Component`, `TransformComponent`, `SceneSerializer`, and `PrefabInstance`.
- Add `src/runtime` or `src/world` for runtime scene instantiation, lifecycle, and update phases.
- Keep renderer-facing `RuntimeScene` as the render extraction target, not the authoring scene itself.
- Use object-oriented component descriptors first; defer data-oriented storage until behavior is stable.

Milestones:

- Contract Complete: scene/prefab schema, stable local IDs, transform hierarchy, component registry tests.
- Prototype Complete: load a simple `.unity` scene into current renderer runtime scene.
- Backend Validated: hidden-window smoke renders a packaged scene with material, mesh, light, camera, and GUI.
- Production Ready: nested prefab variants, diff/merge-safe serialization, async scene load, unload, and additive scenes.

Fast tests:

- Unit: instantiate prefab variant and verify override precedence.
- Smoke: hidden sample loads scene package and renders two frames.

### 3. Runtime Package, Addressables, and Streaming

Commercial-engine concept:

- Unity: Addressables, AssetBundles, Build Profiles, StreamingAssets.
- Unreal: cooked packages, asset registry, IO store.
- O3DE: asset processor and product assets.

Target user model:

- Users mark assets Addressable, group them, add labels, and build platform-specific content.
- Runtime loads by address, GUID, label, or scene dependency set.
- Build profiles select platform, quality, compression, shader variants, and package layout.

Implementation direction:

- Extend `src/core/package_manifest.h` and `tools/asset_importer`.
- Add `ContentCatalog`, `AssetBundleManifest`, `RuntimePackageReader`, and `AsyncLoadQueue`.
- Use deterministic package build from the asset database.
- Runtime package files contain meshes, textures, materials, shaders, reflection, animation clips/controllers, scenes, prefabs, UI layouts, and dependency metadata.

Milestones:

- Contract Complete: catalog schema, dependency closure, package reader interface.
- Prototype Complete: sample loads meshes/materials/shaders/UI from package catalog.
- Platform Validated: Android/iOS package loading from app bundle/storage with pause/resume behavior documented.
- Production Ready: async streaming, memory budget eviction, versioned catalog updates, and failed-load diagnostics.

Fast tests:

- Unit: resolve address to GUID and dependency closure deterministically.
- Smoke: package-only hidden-window startup succeeds with source asset directory absent.

### 4. Rendering Pipeline and Renderer Features

Commercial-engine concept:

- Unity: Scriptable Render Pipeline, URP Renderer Features, Render Pipeline Asset, Render Pipeline Settings.
- Unreal: render graph, view family, scene renderer, material system, Nanite/Lumen-style specialized systems where useful.

Target user model:

- Project selects a `RenderPipelineAsset`.
- Quality profile controls shadows, GI, postprocess, resolution scale, light budgets, and mobile fallbacks.
- Renderer features can be enabled per pipeline asset: shadows, character NPR, outline, GI, particles, postprocess, GUI, debug views.

Implementation direction:

- Evolve existing `renderer::RenderPipeline`, `RenderPipelineBuilder`, and `RenderFeature`.
- Define pipeline assets as serialized content, not hardcoded sample setup.
- Keep sample-owned stylized data out of engine renderer; promote only generic render concepts into `src/renderer`.
- Use FrameGraph for pass declaration and RHI command lists for execution.

Milestones:

- Contract Complete: pipeline asset schema and feature registration contract.
- Prototype Complete: current stylized showcase configured by a pipeline asset.
- Backend Validated: Vulkan hidden smoke for scene, GUI, GI, shadow, postprocess feature set.
- Platform Validated: Metal/macOS, Android Vulkan, and iOS Metal frame-shape parity.
- Production Ready: no per-draw pipeline creation, no frame shader compile, no normal-frame GPU idle waits, bounded allocations.

Fast tests:

- Unit: pipeline asset adds expected FrameGraph passes for each quality profile.
- Smoke: hidden showcase renders with low, medium, high, and desktop profiles.

### 5. RHI, Synchronization, and Backend Capabilities

Commercial-engine concept:

- Unity: GraphicsDevice abstraction and SystemInfo capability gates.
- Unreal: RHI plus Render Dependency Graph.
- Modern mobile engines: explicit frames-in-flight, deferred destruction, transient allocators, descriptor pools.

Target user model:

- Users see backend availability and feature support through diagnostics and quality fallback messages.
- Engine developers see clear capability contracts before adding render features.

Implementation direction:

- Continue growing RHI capability contracts, format capabilities, descriptors, resource views, upload paths, and shader variant/cache keys.
- Add production frame synchronization: frames-in-flight, frame serials, deferred destruction, explicit shutdown drain.
- Add backend allocator contracts and transient resource memory reporting.
- Keep native handles in backend implementation or backend-only tests.

Milestones:

- Contract Complete: capability and synchronization tests for Null.
- Backend Validated: Vulkan resource lifetime, descriptor, compute, upload, timestamp, and pipeline cache behavior.
- Backend Validated: Metal parity behavior on Apple builds.
- Platform Validated: real Android/iOS/macOS devices.
- Production Ready: stable frame pacing, deferred destruction, budget reporting, and capture labels.

Fast tests:

- Unit: unsupported feature rejects early with portable reason.
- Smoke: hidden frame loop destroys/recreates resources without waiting for device idle.

### 6. Shader, Material, and Visual Authoring

Commercial-engine concept:

- Unity: Shader, Material, ShaderGraph, keywords, material inspector, SRP batcher.
- Unreal: material graph and shader permutation system.

Target user model:

- Users create Materials with properties, textures, render queue, keywords, and shader references.
- Shader source remains Slang, but metadata exposes Unity-like Properties, SubShader/Pass concepts, and inspector categories.
- Material variants are explicit and validated against reflection.

Implementation direction:

- Keep Slang as source of truth for code.
- Add `ShaderAsset`, `MaterialAsset`, `ShaderMetadata`, `MaterialPropertyBlock`, `ShaderVariantCollection`, and `PipelineCacheManifest`.
- Generate SPIR-V, MSL, and reflection for every shader.
- Reflection drives descriptor layouts, material validation, vertex layouts, push constants, and pipeline cache keys.

Milestones:

- Contract Complete: material/shader asset schema, property validation, variant key tests.
- Prototype Complete: material editor writes Unity-like `.mat` files and preview renders through current renderer.
- Backend Validated: Vulkan and Metal use reflection-derived descriptors/pipelines.
- Production Ready: variant stripping, warmup/cache, inspector UI, and runtime material property overrides.

Fast tests:

- Unit: material missing required texture fails before draw.
- Smoke: material preview hidden render loads `.mat` + Slang reflection from package.

### 7. Animation, Avatar, and Character System

Commercial-engine concept:

- Unity: AnimationClip, AnimatorController, Avatar, Humanoid/Generic rigs, blend trees, layers, masks.
- Unreal: Skeleton, Animation Blueprint, state machines, montages, retargeting.

Target user model:

- Unity controller and character folder imports remain first-class.
- Users can import FBX/GLB, configure rig type, avatar mapping, clips, masks, and animator controllers.
- Character preview shows mesh, skeleton, materials, animation state, and retarget diagnostics.

Implementation direction:

- Continue `src/animation` and `src/assets/unity_*` work.
- Add runtime `Animator`, `AnimatorStateMachine`, `BlendTree`, `AvatarMask`, `RetargetPose`, and `AnimationEvent`.
- Keep CPU skinning as correctness baseline; add GPU skinning after RHI buffer/descriptor contracts are production-ready.
- Renderer consumes skinned mesh draw plans through abstract buffers and material bindings.

Milestones:

- Contract Complete: avatar mapping, controller graph, blend tree, and clip binding tests.
- Prototype Complete: character viewer loads Unity-style character package and plays controller state.
- Backend Validated: Vulkan hidden smoke renders animated skinned mesh.
- Platform Validated: mobile memory and update budgets documented and tested.
- Production Ready: animation compression, event dispatch, root motion, masks, layers, and GPU skinning.

Fast tests:

- Unit: controller transition and blend tree evaluation.
- Smoke: hidden character preview advances 60 frames and validates pose bounds.

### 8. GUI, UI Documents, and Editor UI

Commercial-engine concept:

- Unity: UI Toolkit, UXML/USS, IMGUI for editor legacy, Canvas for runtime UI.
- Godot: Control tree, themes, anchors.

Target user model:

- Runtime/game UI and tools use the same retained GUI system.
- UI documents have stable ids, styles, layout, controls, and optional data binding.
- Mobile supports touch, safe area, DPI scaling, virtual keyboard, and accessible text sizes.

Implementation direction:

- Evolve current `src/gui` retained tree.
- Add UI document schema inspired by Unity UI Toolkit but mapped to existing retained GUI.
- Add production text: FreeType/MSDF or another explicit font path, Unicode plan, fallback fonts, batching, scissor/clip rects.
- Editor UI should use the same GUI primitives unless a temporary tool-only bridge is explicitly documented.

Milestones:

- Contract Complete: document/style schema, layout, input, focus, and theme tokens.
- Prototype Complete: material editor and character viewer run on retained GUI documents.
- Backend Validated: GUI batching and text render through Vulkan/Metal RHI.
- Platform Validated: touch/safe area/virtual keyboard on Android/iOS.
- Production Ready: Unicode, font fallback, accessibility scale, 500-node budget under profile limits.

Fast tests:

- Unit: layout and input for desktop and mobile viewport sizes.
- Smoke: hidden GUI sample renders 500 nodes and reports timing row.

### 9. Physics, Collision, and Navigation

Commercial-engine concept:

- Unity: Rigidbody, Collider, CharacterController, PhysicsScene, NavMesh.
- Unreal: Chaos physics, collision channels, navigation system.
- Godot: PhysicsServer and NavigationServer.

Target user model:

- Users add Collider, Rigidbody, CharacterController, Trigger, and NavMesh components.
- Collision layers and masks follow Unity-like project settings.
- Physics simulation has fixed update, interpolation, scene query, and deterministic debug draw hooks.

Implementation direction:

- Start with contracts and component data; do not rush a full solver into the renderer.
- Use a proven backend such as Jolt, Bullet, PhysX, or a deliberately tiny custom collision layer after requirements are explicit.
- Keep physics debug rendering expressed as renderer draw plans, never native backend calls.

Milestones:

- Contract Complete: component schema, collision layers, scene query interface, fixed-step scheduler.
- Prototype Complete: character controller and trigger volumes in Project Lane.
- Backend Validated: debug draw through RHI renderer path.
- Production Ready: broadphase, serialization, deterministic tests, mobile CPU budgets.

Fast tests:

- Unit: raycast, overlap, trigger enter/exit ordering.
- Smoke: hidden gameplay scene runs fixed update with collision debug draw enabled.

### 10. Audio

Commercial-engine concept:

- Unity: AudioSource, AudioListener, AudioMixer, clips, spatialization.
- Unreal: Audio components, sound cues, submixes.

Target user model:

- Users add AudioSource and AudioListener components.
- Audio clips are imported through asset database and packaged by platform.
- Mixer groups expose volume, mute, ducking, and simple effects.

Implementation direction:

- Add `src/audio` as a runtime system independent of renderer.
- Use miniaudio, SDL audio, or another small backend after contract tests define clip streaming, source state, and mixing needs.
- Keep audio file decoding in import/package path where possible; runtime streams packaged formats.

Milestones:

- Contract Complete: source/listener/mixer schemas and playback state tests.
- Prototype Complete: Project Lane audio presenter consumes engine audio components.
- Platform Validated: Android/iOS pause/resume/audio focus behavior.
- Production Ready: streaming, spatialization, mixer snapshots, memory budgets.

Fast tests:

- Unit: source play/pause/stop and mixer volume.
- Smoke: hidden runtime advances audio graph without opening renderer window.

### 11. Scripting and Gameplay Framework

Commercial-engine concept:

- Unity: MonoBehaviour lifecycle, serialized fields, coroutines, ScriptableObject.
- Unreal: C++/Blueprint gameplay framework.
- Godot: nodes and scripts.

Target user model:

- Short term: C++ components with Unity-like lifecycle names.
- Medium term: optional scripting bridge only after serialization, component registry, and build pipeline are stable.
- `ScriptableObject`-like data assets are useful before full scripting.

Implementation direction:

- Add `ComponentTypeRegistry`, `SerializedField`, `RuntimeBehaviour`, and lifecycle phases: `Awake`, `OnEnable`, `Start`, `FixedUpdate`, `Update`, `LateUpdate`, `OnDisable`, `OnDestroy`.
- Treat reflection as explicit C++ registration first.
- Candidate scripting options: C#/.NET for Unity familiarity, Lua for lightweight embedding, or visual graph later. Do not choose until runtime component and asset schemas settle.

Milestones:

- Contract Complete: component registration, lifecycle order, serialized field tests.
- Prototype Complete: Project Lane gameplay moves from sample-specific app code into engine component model.
- Production Ready: hot reload policy, script asset packaging, sandboxing, diagnostics, and debugger story.

Fast tests:

- Unit: lifecycle order under instantiate/enable/disable/destroy.
- Smoke: hidden gameplay scene updates scripted components for fixed frame count.

### 12. Editor and Tools

Commercial-engine concept:

- Unity: Project Browser, Hierarchy, Inspector, Scene View, Game View, Profiler, Package Manager, Build Settings.
- Unreal: editor modules, details panel, content browser, viewport.

Target user model:

- Users manage assets, scenes, prefabs, materials, animations, and build profiles through editor tools.
- Editor concepts mirror Unity names where possible.
- Tools produce runtime packages; runtime does not depend on editor-only import libraries.

Implementation direction:

- Grow existing material editor and character viewer into focused editor modules.
- Add `EditorApplication`, `AssetBrowser`, `HierarchyWindow`, `InspectorWindow`, `SceneView`, `GameView`, `ImportSettingsInspector`, `BuildProfileWindow`, and `ProfilerWindow`.
- Use retained GUI and renderer pipeline; editor viewports are render targets managed through RHI/FrameGraph.
- Keep tools modular so command-line importer/build remains available without GUI.

Milestones:

- Contract Complete: editor document model and command pattern for undo/redo.
- Prototype Complete: edit material, prefab, and scene transform; save Unity-like files.
- Backend Validated: editor viewport renders through same renderer pipeline as runtime.
- Production Ready: undo/redo, asset reimport, diagnostics, multi-scene editing, build pipeline integration.

Fast tests:

- Unit: command undo/redo and dirty-state tracking.
- Smoke: hidden editor mode opens project, imports assets, saves scene, builds package.

### 13. World, Streaming, and Open-World Systems

Commercial-engine concept:

- Unity: additive scenes, Addressables, terrain, LODGroup, occlusion culling.
- Unreal: World Partition, HLOD, streaming levels.

Target user model:

- Users split worlds into scenes/cells, mark streaming groups, and assign LODs.
- Terrain, vegetation, props, lights, and navigation data have import/build settings.
- Runtime streams by camera position, gameplay triggers, and memory budget.

Implementation direction:

- Start with additive scene loading and addressable dependency sets.
- Add world cells, bounds, LOD groups, visibility sets, and streaming budget policy.
- Renderer extraction builds compact visible sets and draw batches.

Milestones:

- Contract Complete: cell manifest, dependency groups, LOD selection tests.
- Prototype Complete: 512m stylized world slice streams cells from packages.
- Platform Validated: mobile memory-pressure path unloads cells and preserves gameplay state.
- Production Ready: async IO, hysteresis, HLOD, vegetation budgets, and save/load integration.

Fast tests:

- Unit: cell selection and LOD choice under camera movement.
- Smoke: hidden world sample streams in/out cells under fixed memory budget.

### 14. Particles, VFX, and Timeline

Commercial-engine concept:

- Unity: ParticleSystem, VFX Graph, Timeline, Playables.
- Unreal: Niagara, Sequencer.

Target user model:

- Users add ParticleSystem components, author emitters, curves, bursts, material bindings, and timeline events.
- VFX quality is profile-controlled on mobile.

Implementation direction:

- Extend existing particle system contracts.
- CPU particles first for correctness; GPU particles after compute/storage buffer contracts are production ready.
- Timeline can be a lightweight track system for animation, audio, camera, VFX, and scripted events.

Milestones:

- Contract Complete: emitter schema, curve sampling, deterministic seed tests.
- Prototype Complete: Project Lane VFX presenter maps to engine particle components.
- Backend Validated: GPU particle path through RHI compute and draw plans.
- Production Ready: batching, bounds, culling, mobile budgets, timeline editor.

Fast tests:

- Unit: emitter spawn count and curve sampling.
- Smoke: hidden VFX sample renders bounded particles with no hot-path allocation spikes.

### 15. Input, Platform Services, and Lifecycle

Commercial-engine concept:

- Unity: Input System, Application lifecycle, Screen, QualitySettings.
- Unreal: platform abstraction, input devices, app lifecycle.

Target user model:

- Users bind actions, axes, touch, pointer, keyboard, gamepad, and UI navigation.
- Platform lifecycle is predictable: pause, resume, surface loss, memory pressure, orientation, safe area.

Implementation direction:

- Add `InputActionMap`, `InputBinding`, `InputDevice`, `PointerEvent`, `TouchEvent`, and `GamepadState`.
- Platform layer owns native events and feeds backend-neutral input events.
- Mobile lifecycle controls render submission, swapchain/layer recreation, asset memory budgets, and audio focus.

Milestones:

- Contract Complete: action map and lifecycle event ordering tests.
- Prototype Complete: current samples use action maps instead of direct SDL checks.
- Platform Validated: Android/iOS surface/layer loss, pause/resume, and memory warning.
- Production Ready: rebinding UI, gamepad hotplug, IME/virtual keyboard, accessibility hooks.

Fast tests:

- Unit: input binding resolution and action phase transitions.
- Smoke: hidden lifecycle simulation pauses/resumes renderer and GUI without resource leaks.

### 16. Performance, Telemetry, and Diagnostics

Commercial-engine concept:

- Unity: Profiler, Frame Debugger, Memory Profiler, RenderDoc markers.
- Unreal: stat commands, Insights, RDG events.

Target user model:

- Users and developers can see frame time, pass timings, memory, draw counts, asset load time, and warnings.
- Diagnostics explain fallback reasons in user terms.

Implementation direction:

- Extend existing performance telemetry, GPU timing rows, and debug view catalog.
- Add CPU scopes, GPU timestamp queries, memory budget report, draw-call/material/texture counters, asset load diagnostics, and frame capture labels.
- Make budget thresholds part of quality settings and build profiles.

Milestones:

- Contract Complete: telemetry data model and budget tests.
- Backend Validated: Vulkan/Metal timestamp and debug labels where available.
- Platform Validated: mobile thermal/memory budget reporting.
- Production Ready: profiler UI, capture integration, regression thresholds.

Fast tests:

- Unit: budget warning triggers for over-limit resources.
- Smoke: hidden sample emits pass timing and memory summary.

### 17. Build, CI, Distribution, and Public API

Commercial-engine concept:

- Unity: player build pipeline, package manager.
- Unreal: build targets, modules, cooked builds.

Target user model:

- Users select build profile and platform, then build runtime and content package.
- Engine consumers include public headers and link libraries without depending on backend internals.

Implementation direction:

- Keep Windows MinGW as the local default.
- Expand CMake presets for Android, macOS, iOS, Linux as documented.
- Define public vs internal headers from `PackageManifest`.
- Add install/export rules, versioned feature manifest, sample integration docs, and package validation tool.

Milestones:

- Contract Complete: public API manifest and platform matrix diagnostics.
- Prototype Complete: sample project consumes installed engine package.
- Platform Validated: Android/macOS/iOS builds from documented presets.
- Production Ready: reproducible builds, content/package versioning, SDK diagnostics, release artifacts.

Fast tests:

- Unit: package manifest excludes backend/internal headers.
- Smoke: out-of-tree sample configures and links against installed package.

## Recommended Evolution Tracks

Track A: Unity-Facing Content Spine

Goal: make user-authored content coherent before adding more visual complexity.

1. AssetDatabase + `.meta` GUID system.
2. Scene/GameObject/Component/Prefab schema.
3. Material/Shader asset schema.
4. Package catalog and addressable loading.
5. Current samples load through package/GUID paths.

Why first: this gives every later system a stable user-facing format and avoids hardcoding sample data into engine APIs.

Track B: Production Runtime Spine

Goal: remove prototype runtime risks that block mobile and shipping.

1. RenderSystem split and backend factory refinement.
2. Frames-in-flight and deferred destruction.
3. RHI resource/view/upload model v2.
4. Descriptor/shader interface v2.
5. FrameGraph backend execution hardening.

Why second: this protects the renderer from accumulating features on top of blocking waits or incomplete resource lifetime rules.

Track C: Authoring Tools Spine

Goal: turn current viewers/editors into a small Unity-like editor.

1. Project browser and asset inspector.
2. Material editor writes `.mat` assets.
3. Character viewer becomes animation/avatar inspector.
4. Scene hierarchy and inspector.
5. Build profile and package builder UI.

Why third: tools become useful once the asset database and package spine exist.

Track D: Gameplay Systems Spine

Goal: make the engine usable for a game beyond renderer demos.

1. Component lifecycle and transform hierarchy.
2. Input action maps.
3. Animation runtime and character controller.
4. Audio and physics contracts.
5. World streaming and save/load.

Why fourth: gameplay should sit on the stable scene/content/runtime foundation.

Track E: Visual Production Spine

Goal: mature the stylized renderer without making it sample-specific.

1. Render pipeline assets and renderer features.
2. Material reflection and shader variants.
3. Lighting/shadows production pass.
4. Character NPR/outline production pass.
5. Mobile quality profiles, telemetry, and platform validation.

Why fifth: visual systems then become configurable engine features, not hardwired showcase code.

## Suggested Phase Order

| Phase | Name | Main Systems | Target Status |
| --- | --- | --- | --- |
| 1 | Content Identity Foundation | AssetDatabase, `.meta`, GUID, import registry | Contract Complete |
| 2 | Scene Object Model | Scene, GameObject, Component, Transform, Prefab | Contract Complete |
| 3 | Runtime Package v1 | Package catalog, Addressables, package reader | Prototype Complete |
| 4 | Pipeline Asset v1 | RenderPipelineAsset, renderer features, quality profiles | Prototype Complete |
| 5 | Runtime Synchronization v2 | frames-in-flight, deferred destruction, shutdown drain | Backend Validated |
| 6 | Shader/Material Asset v1 | `.mat`, Slang metadata, variants, reflection validation | Backend Validated |
| 7 | Editor Shell v1 | project browser, inspector, material editor, scene hierarchy | Prototype Complete |
| 8 | Animation Character v1 | Avatar, Animator, blend trees, character preview | Backend Validated |
| 9 | Gameplay Component v1 | lifecycle, input actions, C++ components | Prototype Complete |
| 10 | Mobile Platform v1 | Android Vulkan, iOS Metal lifecycle and package loading | Platform Validated |
| 11 | GUI Production v1 | text, Unicode plan, batching, touch, safe area | Platform Validated |
| 12 | World Streaming v1 | additive scenes, cells, LOD, memory budget | Prototype Complete |
| 13 | Physics/Audio v1 | component contracts, fixed update, playback/mixer | Prototype Complete |
| 14 | Visual Production v1 | lighting, shadows, character NPR, outline, postprocess | Backend Validated |
| 15 | Distribution v1 | public API, install/export, sample project, build profiles | Production Ready candidate |

Each phase must keep at least one fast unit test and one smoke or hidden-window validation path. Mobile phases must document Android/iOS lifecycle, surface/layer, swapchain, pause/resume, and memory-budget impact.

## Naming Alignment

Prefer these user-facing names even if internal storage differs:

- `Project`
- `AssetDatabase`
- `AssetImporter`
- `ImportSettings`
- `Scene`
- `GameObject`
- `Component`
- `Transform`
- `Prefab`
- `PrefabVariant`
- `Material`
- `Shader`
- `RenderPipelineAsset`
- `RendererFeature`
- `QualitySettings`
- `AnimationClip`
- `AnimatorController`
- `Avatar`
- `Addressables`
- `BuildProfile`
- `PlayerSettings`
- `InputAction`
- `AudioSource`
- `Rigidbody`
- `Collider`
- `Camera`
- `Light`
- `Canvas` or `UIDocument`

Internal names may be more precise, but public content files and editor UI should use this vocabulary.

## Non-Negotiable Verification Gates

Every implementation phase should include:

- A focused unit or contract test for the changed system.
- A hidden-window or non-window smoke path.
- Shader generation when shader-visible behavior changes.
- RHI capability/contract tests before backend feature work.
- Null, Vulkan, and Metal semantics for new formats, resource states, descriptors, or pipeline states.
- Architecture boundary coverage for native header leaks.
- Documentation status updates using the status vocabulary.
- Windows MinGW build path validation.

Recommended commands remain:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\compile-shaders.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-cmake.ps1 -BuildDir <clean-dir>
ctest --test-dir .\artifacts\cmake\<clean-dir> --output-on-failure
.\artifacts\direct\arp_stylized_showcase.exe --hidden --frames 2
```

## First Concrete Slice

The best first slice is small but structural:

1. Add `src/assetdb` with GUID, `.meta`, importer registry, dependency graph, and tests.
2. Add a minimal `src/scene` with Scene, GameObject, Transform, Component registry, serializer, and tests.
3. Add a package catalog that maps GUIDs to runtime artifacts using existing `tools/asset_importer` contracts.
4. Convert one current sample scene into Unity-like authoring files plus package manifest.
5. Add a hidden-window smoke that loads by GUID/package, builds `renderer::RuntimeScene`, and renders two frames.

This slice proves the engine can become Unity-shaped without compromising the RHI/FrameGraph renderer core.
