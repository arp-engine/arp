# RHI WebGPU Backend Roadmap

## Goal

Keep the project on the self-owned RHI path while making WebGPU a first-class backend that reports only validated runtime capabilities, shares explicit contracts with Vulkan/Metal/Null, and avoids leaking backend details into renderer, GUI, render graph, or samples.

## Status Language

- Contract Complete: public RHI model, validation tests, and backend plans agree on the API semantics.
- Prototype Complete: a path exists but may be incomplete or intentionally conservative.
- Backend Validated: native backend implementation passes focused contract and smoke tests.
- Platform Validated: browser/window/lifecycle/surface behavior is verified in a hidden-window or browser smoke path.
- Production Ready: performance, lifetime, validation, and cross-backend semantics are stable enough for normal renderer use.

## Phase 0: Capability Truthfulness

Priority: highest

Status: Backend Validated for current WebGPU character-viewer draw path.

Work:
- Split planned requirements from runtime truth.
- Keep WebGPU character-viewer requirements separate from `DeviceCapabilities`.
- Report WebGPU compute as unavailable until `WGPUComputePipeline` and compute pass dispatch are implemented.
- Mark storage texture/format capabilities conservatively, especially `Bgra8Unorm` storage.
- Keep canvas, sampled textures, CPU-to-GPU uploads, depth-tested indexed draw, and push constant emulation as validated WebGPU runtime features.

Tests:
- `webgpu_backend` must assert required capabilities and runtime capabilities are distinct.
- `rhi_capabilities` must not assume every backend reports compute.
- Format capability tests must prove `Bgra8Unorm` is not blindly storage-capable on WebGPU.

## Phase 1: Command Semantics

Priority: high

Status: Backend Validated for WebGPU draw semantics; compute dispatch is explicitly diagnosed as unavailable.

Work:
- Add an explicit non-indexed `Draw` command encoding op.
- Make `CommandList::draw()` record `Draw` and issue native `wgpuRenderPassEncoderDraw`.
- Keep `drawIndexed()` as the only source of `DrawIndexed`.
- Make WebGPU compute commands fail or diagnose cleanly while runtime compute is unavailable; do not silently claim success.

Tests:
- `webgpu_backend` must distinguish draw count from draw-indexed count.
- Dispatch without WebGPU compute support must be observable through plan/telemetry/validation rather than a silent no-op.

## Phase 2: Explicit Pipeline Layout

Priority: high

Status: Backend Validated for Character Viewer WebGPU pipeline layout and push-constant slot mapping.

Work:
- Introduce an explicit pipeline layout contract that declares system-reserved bind groups and user bind groups.
- Move WebGPU push constant emulation into a declared system bind group.
- Remove dummy descriptor sets used only to align bind group slots.
- Preserve Vulkan real push constants and Metal argument/constant-buffer mapping.

Tests:
- Pipeline layout tests must verify system set slots and user set slots do not collide.
- Existing material/IBL bind group layouts must keep stable user slot numbers.

Current implementation notes:
- `PipelineLayoutDesc` declares system push-constant slots and user descriptor-set slots explicitly.
- WebGPU requires an explicit layout when push constants and user descriptor sets coexist.
- Character Viewer no longer creates or passes a dummy descriptor set for WebGPU slot alignment.
- Vulkan preserves explicit user set indices by inserting an empty descriptor set layout for system push-constant slots.

## Phase 3: Shader Package Contract

Priority: medium-high

Status: Contract Complete for shader format, stage entry points, and descriptor binding reflection; backend validation complete for current WebGPU/Vulkan/Metal pipeline creation paths.

Work:
- Extend shader inputs beyond opaque bytecode/source.
- Add shader format, stage entry points, and reflection-driven descriptor layouts.
- Treat Slang outputs as a package: SPIR-V, MSL, WGSL, reflection, entry points, push constant metadata, and descriptor view dimensions.
- Remove backend guesses such as hard-coded `vertexMain` and `fragmentMain` when package metadata is available.

Tests:
- Shader tests must validate entry point metadata for SPIR-V, MSL, and WGSL.
- Material preview and stylized shader contracts must validate descriptor dimensions, including cube textures.

Current implementation notes:
- `ShaderDesc` now carries `ShaderFormat` and per-stage entry points.
- `ShaderDesc` also carries `ShaderReflectionDesc` with push constant size and descriptor bindings.
- `Shader` stores shader package metadata and exposes stage entry-point lookup.
- RHI validates shader reflection against explicit pipeline layouts, descriptor set bindings, descriptor types, shader stages, and texture view dimensions.
- WebGPU shader/pipeline plans use explicit metadata and reject non-WGSL explicit formats.
- Vulkan and Metal graphics pipeline creation consume shader entry-point metadata instead of fixed names when provided.

## Phase 4: WebGPU RAII Resources

Priority: medium

Status: Backend Validated for buffer, texture, sampler, shader, graphics pipeline, and descriptor set ownership used by the Character Viewer path.

Work:
- Replace device-owned `unordered_map<const Resource*, NativeHandle>` lifetime ownership with WebGPU-specific RHI resource subclasses.
- Release native handles in resource destructors.
- Keep device-side maps only for lookup/telemetry where needed, not as native lifetime owners.
- Align WebGPU resource ownership style with Vulkan/Metal backend resources.

Tests:
- Resource lifetime tests must destroy buffers/textures before device shutdown without leaking native ownership semantics.
- Pointer reuse must not associate a new RHI resource with a stale native handle.

Current implementation notes:
- WebGPU buffer, texture, sampler, shader, graphics pipeline, and descriptor set resources are backend-specific RHI subclasses.
- Native WebGPU handles are released from resource destructors instead of `WebGpuNativeState` device-side pointer maps.
- `WebGpuNativeState` keeps device/surface/push-constant infrastructure, not per-resource native lifetime ownership.
- The Character Viewer HTML smoke path compiles the Emscripten WebGPU branch and renders with the RAII resource model.

## Phase 5: WebGPU Compute

Priority: medium, after Phase 2 and Phase 3

Status target: Backend Validated, then Platform Validated.

Status: Platform Validated for command encoding, native `WGPUComputePipeline` creation, compute pass dispatch encoding, submit telemetry, and a browser-observable storage-texture smoke in the Character Viewer HTML build.

Work:
- Implement `WGPUComputePipeline`.
- Add compute pass encoding and native `wgpuComputePassEncoderDispatchWorkgroups`.
- Bind compute descriptor sets using explicit pipeline layout slots.
- Re-enable `features.compute` only when a runtime probe reports a native WebGPU device and the backend native compute dispatch path is available.

Tests:
- A minimal compute contract test must create a compute pipeline and encode dispatch.
- A browser smoke path validates a small compute workload by writing a deterministic storage texture and rendering it to the canvas.

Current implementation notes:
- WebGPU compute pipelines are backend-specific RAII resources.
- `CommandList::bindComputePipeline()` begins a native compute pass and `dispatch()` emits `wgpuComputePassEncoderDispatchWorkgroups` when running in the Emscripten native WebGPU path.
- Submit-plan validation rejects dispatch without a bound compute pipeline and rejects render/compute pass overlap.
- Submit telemetry records dispatch count and last workgroup dimensions for contract tests.
- Character Viewer HTML build and runtime smoke pass with the compute-capable backend code compiled in.
- Runtime capability reporting remains conservative on host/null-profile probes and reports WebGPU compute only for native browser devices through `WebGpuRuntimeFeatureProbe::nativeComputeDispatch`.

## Phase 6: Runtime Format And Device Capability Query

Priority: medium-low

Status: Backend Validated for Null default policy, WebGPU runtime probe policy, Vulkan physical-device format feature derivation, and Metal pixel-format policy. Platform Validated for the Character Viewer WebGPU smoke path and Vulkan hidden-window path on Windows; Metal hardware validation is deferred.

Work:
- Replace optimistic static capability tables with backend-derived data where available.
- Use adapter/device features for WebGPU.
- Use physical device format properties for Vulkan.
- Use Metal feature/pixel-format policy for Metal.
- Keep Null backend deterministic and contract-driven.

Tests:
- Per-format capability tests must cover sampled, color attachment, storage, filterable, blendable, and depth attachment semantics.

Current implementation notes:
- Default/Null format policy no longer marks `Bgra8Unorm` storage-capable and keeps `Rgba16Float` blendability conservative.
- WebGPU uses an explicit `WebGpuRuntimeFeatureProbe`; native Emscripten builds fill queried limits and optional `BGRA8UnormStorage` when a device is available.
- WebGPU runtime format storage is enabled only when the probe says storage texture binding and the relevant optional format support are available.
- Vulkan format capabilities are derived from `vkGetPhysicalDeviceFormatProperties(...).optimalTilingFeatures` instead of the generic table.
- Metal has an explicit pixel-format capability policy shared by tests and the Apple device wrapper.
- Runtime `features.compute` remains false for host/null-profile WebGPU probes and true for native browser devices with the validated compute dispatch path.

## Phase 7: Browser Observable Compute Validation

Priority: medium

Status: Platform Validated for browser-observable compute smoke in the Character Viewer HTML build.

Work:
- Add a tiny WebGPU compute workload that writes deterministic data to a storage texture.
- Add a browser smoke path that observes the result through a rendered diagnostic canvas output.
- Promote WebGPU runtime `features.compute` after the browser smoke proves pipeline creation, pass dispatch, queue submit, and observable output.
- Keep the Character Viewer renderer independent of native WebGPU handles; expose the smoke through RHI resources and command lists.

Tests:
- A fast native contract test covers resource usage, descriptor binding, dispatch dimensions, and promotion gating.
- A browser smoke validates the compute result without relying on frame timing or manual inspection.

Current implementation notes:
- `WebGpuObservableComputeSmokePlan` gates runtime compute promotion on four explicit facts: native compute path, runtime compute capability, storage-texture output capability, and browser-observable output.
- `games/character_viewer/web/webgpu_compute_smoke.cpp` creates a WebGPU RHI device, dispatches `webgpu_compute_smoke.wgsl`, samples the storage texture, and presents the observable result to the Character Viewer canvas when `?compute-smoke=1` is present.
- `tests/character_viewer_web_runtime_smoke.js --require-compute-smoke` exercises the browser path and fails if `document.body.dataset.webGpuComputeSmoke` is not `passed`.
- WebGPU runtime compute is promoted only for native browser devices; host profile tests still report `features.compute=false`.

## Phase 8: Production Hardening For HTML Character Viewer

Priority: medium

Status: Platform Validated for browser-facing runtime diagnostics and resize-reload smoke. Live canvas reconfiguration is deferred to Phase 9.

Work:
- Add explicit runtime diagnostics for WebGPU capability probes, canvas configuration, storage-texture availability, and compute smoke status.
- Verify resized viewport reload does not regress the Character Viewer frame loop.
- Keep promoted compute capability behind RHI contracts; renderer and sample code must not depend on WebGPU native handles.
- Add a hidden/browser smoke path for resize and refresh reload so the HTML app remains stable outside the single happy-path frame.

Tests:
- Browser smoke captures capability diagnostics and exercises one resize/reload lifecycle path.
- Focused contract tests keep capability truthfulness and command semantics covered.

Current implementation notes:
- `character_viewer_web_main.cpp` publishes native WebGPU availability, runtime compute, storage-texture support, RGBA/BGRA storage flags, and runtime limits through `document.body.dataset`.
- `samples/character_viewer_web/index.html` surfaces compute/storage status in the runtime panel without leaking native handles into renderer or sample draw logic.
- `tests/character_viewer_web_runtime_smoke.js --exercise-lifecycle` changes browser metrics, reloads, and requires the app to render again with complete WebGPU diagnostics.

## Phase 9: Dynamic Canvas And Surface Lifecycle

Priority: medium

Status: Platform Validated for no-reload browser resize, WebGPU canvas reconfiguration, and resize-reload regression smoke. Hidden-tab pause/resume remains a future platform lifecycle item.

Work:
- Track browser canvas CSS size and device-pixel ratio through the web shell instead of hard-coded render extents.
- Reconfigure WebGPU canvas/surface only when extent changes, outside hot draw command construction.
- Make Character Viewer runtime tolerate no-reload resize and refresh without recreating pipelines per draw.
- Keep Android/iOS surface lifecycle unaffected; this phase changes only desktop SDL window bookkeeping and the WebGPU platform path.

Tests:
- Browser smoke resizes without reload and verifies a later frame renders at the new canvas extent.
- Existing reload lifecycle smoke remains a separate regression path.

Current implementation notes:
- `SdlWindow` now exposes bounded size bookkeeping through `resize(width, height)`.
- `CharacterViewerRuntimeSession::resizeViewport()` updates the platform window size and asks `RenderSystem` to resize before the next frame.
- WebGPU `RenderSystem::resize()` reconfigures the canvas, rebuilds the depth target, forwards resize to the active render pipeline, and recreates GUI/particle pipelines only when the extent changes.
- `character_viewer_web_main.cpp` synchronizes the canvas element pixel size from CSS size and device-pixel ratio, publishes canvas extent diagnostics, and keeps the frame loop running after compute smoke.
- `tests/character_viewer_web_runtime_smoke.js --exercise-live-resize` verifies no-reload resize; `--exercise-lifecycle` verifies resized reload.

## Non-Goals

- Do not turn renderer or sample code into WebGPU-specific code.
- Do not leak native handles into renderer, GUI, render graph, or character viewer.
- Do not add production rendering paths that silently fall back when resource semantics are unknown.
- Do not implement WebGPU compute on the old implicit push-constant/dummy-set layout.

## Current Next Step

Next recommended phase: add hidden-tab pause/resume diagnostics and a performance pass that checks resize does not create pipelines on every frame.
