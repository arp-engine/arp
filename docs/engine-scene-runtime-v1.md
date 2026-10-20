# Engine Scene Runtime V1

Status: Contract Complete for the object/component contract, renderer/RHI lowering contracts, the scene-runtime custom-pipeline payload contract, generic prepared renderer-frame submit, and custom-pipeline/RHI parity. Prototype Complete for runtime extraction, 3D visibility, 2D chunk planning, input-control, UI, RenderFrameDirector frame orchestration, scene-runtime pass provider composition, and the custom RenderGraph scene-runtime adapter. Vulkan hidden-window direct scene-runtime submit, Vulkan hidden-window custom RenderGraph scene-runtime submit, and the scene-runtime demo director path are Backend Validated on the local Windows MinGW host. Platform Validated and Production Ready are not claimed.

## Contract Scope

Scene Runtime V1 defines the runtime-facing scene object model, platform-neutral input/action flow, and CPU-side extraction contracts that feed renderer-facing queues. It validates the local surface-backed hidden-window Vulkan direct-submit path and the local surface-backed hidden-window Vulkan custom RenderGraph path for scene-runtime renderables. This document does not validate Metal, WebGPU, Android, iOS, visible-window platform behavior, broad hardware/driver behavior, or production deployment behavior, and it does not add shader, serializer, prefab, editor, physics, scripting, async IO, or GPU-culling scope.

The shared object model lives in `src/scene`. Scene inherits the GameObject contract: the root has normal identity, active state, transform ownership, lifecycle participation, and deterministic diagnostics. GameObjects form a deterministic parent-child hierarchy with stable child order, explicit reparent operations, cycle rejection, and deterministic invalid-ID diagnostics.

Transforms use local/world-global transforms as explicit concepts. Local space is owned by the object. World/global space is derived from the parent chain and from the stable root traversal snapshot used by extraction systems.

## Camera Contract

Cameras are ordinary GameObject camera with shared CameraComponent, not a special camera object class. A camera object is a `GameObject` with `TransformComponent` plus shared `CameraComponent` metadata from `src/scene`.

`CameraResolver` consumes the stable root traversal snapshot and produces `ResolvedCamera` records. Each record carries projection kind, viewport/aspect policy, visible `RenderQueue` mask, clear/load intent metadata, world transform, and stable priority/object ordering. Camera position and orientation come from the owning object's world/global transform.

The default mixed 2D/3D one orthographic camera contract uses a single orthographic camera whose visible queue mask includes both 2D and 3D queues. A perspective observing 2D requires explicit 2D projection semantics for plane, pixel density, sort/depth, and projection behavior; missing semantics must fail with deterministic diagnostics instead of guessed fallback behavior.

## Input And Control

Input/control code lives in `src/input`. The input-control prototype is platform-neutral: platform raw events are represented as explicit `InputEvent` records inside an `InputFrame`, then input flows from raw events to InputActionState through `InputSystem` and registered `InputActionMap` bindings.

`PlayerInputComponent` binds a GameObject to a deterministic player slot and action map. It exposes high-level `InputActionState` only; it does not move objects, modify camera state, inspect renderer data, or expose platform-native keycodes.

`CharacterControllerComponent` is a V1 deterministic kinematic controller. The contract is that character controllers consume action intent only from `PlayerInputComponent`, update movement state and the owning Transform during scene update, and do not read raw input or camera projection state. Physics integration remains outside this status and is not claimed.

`CameraFollowComponent` consumes a target GameObject transform plus high-level look/zoom actions. The camera follow/input runs in late update after character movement, updates the camera GameObject transform deterministically, and leaves shared `CameraComponent` projection semantics to explicit camera metadata and `CameraResolver`.

The tested control order is platform raw event conversion, `InputSystem` action snapshot, optional UI input routing consumes before gameplay, `PlayerInputComponent`, `CharacterControllerComponent` update, `CameraFollowComponent` late update, stable root traversal snapshot, `CameraResolver`, then 2D, 3D, and UI `RenderQueue` extraction.

## UI Contract

UI is Prototype Complete only. Screen-space UI lives in `src/ui` as an explicit `Canvas` and `Widget` tree with stable widget IDs, parent-child widget hierarchy, viewport metadata, safe-area metadata, scale policy, layout rect resolution, deterministic hit-test order, focus/capture state, and deterministic diagnostics. Screen-space UI is not modeled as a GameObject hierarchy and does not require `CameraComponent`.

`UIInputRouter` consumes high-level pointer, keyboard, and text data from `InputSystem` before gameplay bindings are applied. It returns `InputConsumption` so consumed UI input does not also drive `PlayerInputComponent`, `CharacterControllerComponent`, or camera follow/input behavior.

World-space UI uses explicit anchor bridge plus `ResolvedCamera`: scene objects may expose `WorldUiAnchorComponent` metadata, and `WorldUiAnchorProjector` combines the stable traversal snapshot with a resolved camera to produce screen-space anchor rectangles for widgets. The UI widget tree remains in `src/ui`; nameplates, health bars, and prompts do not turn the retained UI tree into scene children.

UI extraction emits `UIRenderQueue` records with widget render items, clip/scissor metadata, texture/font/material references, and stable order keys rather than backend-native draw state. It does not expose renderer/RHI backend handles or generate raw static vertex data in scene hot paths.

## Extraction And Queues

Extraction starts from the scene root and consumes a stable root traversal snapshot. Systems derive behavior inferred from components and queue masks, not from scene dimension enums, scene-mode flags, filename conventions, or guessed fallbacks.

3D runtime code lives in `src/3d`. Mesh, Terrain, and Animation components emit renderer-facing handles, metadata, world transforms, bounds, and sort keys into 3D `RenderQueue` records. `TileMap and Terrain are components`; neither is a special scene subclass, native renderer handle, asset-loader side channel, or scene-global mode switch.

2D runtime code lives in `src/2d`. SpriteRenderer and TileMap components emit sprite and chunk items into 2D `RenderQueue` records, with chunk load/keep/unload planning driven by explicit tile/chunk metadata, the resolved orthographic viewport, preload margin, and resident budget diagnostics.

Render extraction emits render items and `RenderQueue` records with no raw static vertex data copied or synthesized in the frame hot path. Dynamic or batched rebuild paths, such as sprite batches or TileMap chunks, must be explicitly modeled with deterministic metadata before producing results.

V1 keeps separate logical 2D/3D/UI queues because collection, visibility, sorting, hit-test, layout, and pipeline states differ. Compatible queues should be submitted inside one physical world render pass by default for mobile friendliness, with deterministic queue order and pipeline switching inside that pass. Multiple physical passes require explicit attachment, resolve, postprocess, offscreen compositing, or capability contracts; ambiguous cases must fail diagnostically.

## Renderer Integration Status

Scene-runtime renderer frame: Contract Complete. The renderer-owned bridge consumes `SceneTraversalSnapshot`, `ResolvedCamera`, `Scene3DRenderQueue`, `Scene2DRenderQueue`, and `UIRenderQueue`, then produces a bounded renderer scene-runtime frame with deterministic queue order, draw counts, descriptor/material resource counts, one-pass compatibility intent, and native-handle/raw-static-vertex-data rejection.

Scene-runtime RHI lowering: Contract Complete. `RendererSceneRuntimeFrameRhiPlanner` lowers one compatible world/UI frame into explicit renderer/RHI frame-plan semantics before backend execution, including descriptor budgets, material resource counts, push-constant capacity, and diagnostic failure for unsupported multi-pass requirements without explicit attachment, postprocess, or offscreen contracts.

Scene-runtime custom pipeline payload: Contract Complete. `SceneRuntimePipelinePayload` is the explicit `renderer/SceneRuntimePipelinePayload` version 1 contract passed through `CustomRenderFrameWorkload` into `RenderFrameRequest`. It carries the traversal snapshot, resolved cameras, 3D/2D/UI queues, validated renderer frame, and explicit multi-pass metadata. Validation rejects missing contract metadata, backend handle exposure, static geometry payload copies, missing cameras or queues, and non-contract renderer frame status with deterministic diagnostics.

RenderFrameDirector frame orchestration: Prototype Complete. `RenderFrameDirector` is the renderer-owned orchestration point for custom graph frames. It validates `CustomRenderFrameWorkload`, adapts enabled payload sections through registered payload adapters, invokes pass providers, records provider-declared RenderGraph passes, and rejects legacy `RenderPipeline::composeGraph` fallback unless the workload explicitly declares a `CustomResourceSemantic::PrototypeLegacyFallback` resource contract with a diagnostic reason.

Scene-runtime pass provider: Prototype Complete. `SceneRuntimePayloadAdapter` converts the scene-runtime payload contract into a renderer frame intent, and `SceneRuntimeWorldUiPassProvider` declares the one-pass `SceneRuntimeWorldUiPass` with explicit backbuffer attachment state and ordered 3D, then 2D, then UI semantics. Implicit multi-pass requirements still fail unless attachment, postprocess, or offscreen metadata is explicit.

Generic prepared renderer-frame submit: Contract Complete. Director decisions carry generic prepared renderer-frame requirements: `RendererRhiFramePlanDesc`, bridge settings, expected draw counts, max draw command policy, shader artifact root policy, and native-handle/backend-branching status. Vulkan `RenderSystem::renderGraphFrame` selects prepared submit from this decision rather than platform-side payload type checks, while WebGPU reports deterministic diagnostic-only behavior for unsupported prepared execution.

Scene-runtime custom RenderGraph adapter: Prototype Complete. `SceneRuntimeRenderPipeline` remains a compatibility profile, but it is no longer the only scene-runtime extension point. Scene-runtime custom graph composition is provider-driven through `RenderFrameDirector`; legacy pipeline composition is quarantined behind the explicit `PrototypeLegacyFallback` workload resource contract and otherwise fails deterministically. The scene-runtime demo exercises acquire, custom workload, `renderGraphFrame`, and present through the director/provider path, while the old direct RHI submit remains an explicit validation mode. WebGPU source contracts now mirror the custom-graph pending-present/report path and deterministic output-hash telemetry handoff, but that parity is Contract Complete only until browser/runtime smoke executes the equivalent backend path.

Scene-runtime custom pipeline/RHI parity: Contract Complete. The renderer-owned parity helper derives `RendererSceneRuntimeFrameRhiPlanRequest` from the custom pipeline payload, verifies the custom pass against the RHI planner draw-plan contract, checks descriptor layouts, descriptor set count, material and texture counts, push-constant budget, backend-owned staging, and input contract, then emits backend semantic reports for Null, Vulkan, Metal, and WebGPU. Null, Vulkan, and Metal report semantic support for the current one-pass contract; WebGPU reports a deterministic diagnostic while push-constant lowering or bind-group emulation lacks an explicit runtime contract.

The scene-runtime renderer frame, RHI lowering, custom payload, and parity paths are Contract Complete only from their respective contract evidence. The custom RenderGraph adapter is Prototype Complete from custom-graph composition evidence; it is Backend Validated only for the named local Windows MinGW/Vulkan hidden-window path below. These statuses do not promote Metal, WebGPU, Platform Validated, or Production Ready.

Null scene-runtime submit is Prototype Complete only after a fast hidden-frame smoke. This document does not promote Null submit from renderer/RHI contract evidence alone.

Vulkan hidden-window direct scene-runtime submit: Backend Validated. The local MinGW smoke creates a hidden SDL Vulkan window, builds a surface-backed Vulkan `RenderSystem`, submits a scene-runtime frame through the renderer/RHI production path, presents, captures deterministic render-output hash telemetry, and keeps native handles hidden from scene, renderer-bridge, and upper runtime layers.

Vulkan hidden-window custom RenderGraph scene-runtime submit: Backend Validated on local Windows MinGW/Vulkan. The hidden SDL smoke installs `SceneRuntimeRenderPipeline`, prepares the scene-runtime custom graph backend resources before hot submit, calls `renderGraphFrame` with a typed scene-runtime payload for two frames, submits the prepared RHI Vulkan path to the acquired swapchain backbuffer, presents, captures deterministic render-output hash telemetry, draws 3D, 2D, and UI procedural scene-runtime roles, and verifies no native handle exposure, no resource or pipeline creation during submit, no hot-path file/network IO, and no direct-submit validation path use.

Scene-runtime demo director path: Backend Validated on local Windows MinGW/Vulkan. The hidden SDL smoke installs a lightweight compatibility pipeline whose legacy `composeGraph` path fails if used, prepares the scene-runtime frame plan through the compatibility facade over generic prepared renderer-frame preparation, calls `renderGraphFrame` with a typed scene-runtime payload for two frames, and succeeds only when `RenderFrameDirector` adapters/providers produce the scene-runtime pass and prepared submit decision.

Vulkan hidden-window submit is Backend Validated only after surface-backed Vulkan hidden-window smoke evidence for the named path. Synthetic Vulkan, Null submit, custom-pipeline parity, or renderer/RHI contract evidence cannot promote this status.

Metal and WebGPU are not promoted by Vulkan evidence. Their scene-runtime status remains support-label or deterministic diagnostic-only until each backend has its own equivalent scene-runtime renderable validation.

Backend Validated is not Platform Validated. Platform Validated and Production Ready are not claimed. Those statuses still require visible-window presentation, Android/iOS lifecycle, macOS Metal runtime, WebGPU browser runtime, broader hardware/driver validation, memory and telemetry lifetime evidence, migration evidence, visual validation, and production performance gates with current evidence.

## Renderer Integration Contract

The renderer integration slice connects scene-runtime logical queues to the renderer/RHI production frame path. The bridge belongs to `src/renderer`, not `src/scene`: it consumes `SceneTraversalSnapshot`, `ResolvedCamera`, `Scene3DRenderQueue`, `Scene2DRenderQueue`, and `UIRenderQueue`, then produces a bounded `RendererRhiFramePlanDesc` with explicit queue order, draw counts, descriptor/material resource counts, push-constant capacity, pass compatibility, and deterministic diagnostics.

The scene-runtime renderer frame contract, RHI lowering contract, custom pipeline payload contract, and custom pipeline/RHI parity contract are Contract Complete only after their contract tests and smoke paths have current evidence. Vulkan scene-runtime rendering is Backend Validated only after a surface-backed hidden-window Vulkan smoke executes the named scene-runtime path, submits through a Vulkan RHI device, presents, and records deterministic render-output hash telemetry. The custom RenderGraph path additionally requires explicit backend preparation before hot submit and telemetry proving prepared resources, pipelines, and descriptors are reused for submitted frames. Metal scene-runtime contracts must report support labels or deterministic diagnostic-only status until backend-specific evidence exists. WebGPU custom-graph report/hash parity is Contract Complete from source-contract evidence, but it is not Backend Validated or Platform Validated until browser/runtime smoke proves the equivalent present-time telemetry path.

The planned default physical submission is one compatible world/UI render pass ordered as 3D, then 2D, then UI. Separate physical passes require explicit attachment, resolve, postprocess, offscreen, shadow, or capability metadata. Missing resource semantics, camera visibility, renderer handles, bounds, descriptor resources, or pass compatibility fail with diagnostics instead of guessed fallback behavior.

Renderer integration status remains below Platform Validated and Production Ready until visible-window presentation, Android/iOS lifecycle, macOS Metal runtime, WebGPU browser runtime, broader hardware/driver validation, memory/telemetry lifetime evidence, and production performance gates have current evidence.

## Guardrails

`src/scene`, `src/input`, `src/ui`, `src/2d`, and `src/3d` stay backend-neutral. They must not include native Vulkan, Metal, WebGPU, Android, or iOS headers; expose native handles; accept backend-only parameters; perform file or network IO in frame hot paths; create shader or pipeline work per frame; consume platform-native input/window/device handles in scene/control components; or use scene-mode/dimension flags instead of component-driven classification. `src/ui` must not add GameObject-only screen-space UI shortcuts, require cameras for screen-space UI, or generate raw static vertex data in scene hot paths.

The same runtime roots must keep CPU allocation explicit: runtime dynamic containers use core/cpu_allocator through `core::pmr` container/string aliases, and owned object storage uses `core/cpu_allocator` helpers such as `core::makeUnique` or `core::makeShared`. New runtime production code must not use default STL dynamic containers, default STL owned allocation helpers, or raw object allocation for scene/input/UI/2D/3D runtime results.

Status promotion requires new evidence. Contract Complete and Prototype Complete do not imply Backend Validated, Platform Validated, or Production Ready.
