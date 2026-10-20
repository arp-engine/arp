# RHI Capability Matrix

> Snapshot: 2026-08-09
>
> Purpose: provide one source-aligned baseline for cross-backend RHI work. This
> document separates L2 lowering policy, boolean device capability reporting,
> and implementation/validation maturity. A declared capability is not proof of
> backend or platform validation.

## Reading the matrix

| Label | Meaning |
|---|---|
| `N` | Native lowering is selected by the current capability policy. |
| `E` | Backend emulation/rewrite is selected. Equivalence is not implied unless a contract test proves it. |
| `C` | CPU replay lowering is selected. |
| `D` | Degraded lowering is selected; it must not be treated as semantic equivalence. |
| `F` | Deterministic diagnostic failure is selected. |
| `—` | The feature is not included in the current device requirement set/report. |

The intended RHI model is **not** the intersection of Vulkan, Metal, and
WebGPU. L2 declares engine semantics; each backend can implement the same L2
contract through native commands, backend-owned rewrites, or equivalent
emulation. If a lowering cannot preserve the contract, it must report a
diagnostic failure or require explicit `PrototypeLegacy` opt-in. It must not
silently succeed with different behavior.

## Current L2 feature lowering policy

Source: `src/rhi/semantics/native_backend_capabilities.cpp`.

| L2 feature | Null | Vulkan | Metal | WebGPU | Notes |
|---|---:|---:|---:|---:|---|
| Push constants | N | N | N | E | Metal uses reflected `setVertexBytes`/`setFragmentBytes`/`setBytes`; WebGPU uses its equivalent lowering. |
| Secondary command buffer | N | N | N/C | C | Metal uses ICB when the native contract is available; secondary push constants are replayed into the consuming encoder with full validation, while explicit CPU replay remains opt-in. |
| Descriptor table | N | N | N | D | Metal Argument Buffer tables are a separate lowering from direct descriptor sets. |
| Transient aliasing | N | N | N (native heap/probe gated) | D | Metal uses native `MTLHeap` placement plus completion-gated alias barriers; capability is false when the probe/contract is unavailable. |
| Timeline synchronization | N | N | N (SharedEvent/probe gated) | F | Metal uses `MTLSharedEvent`; WebGPU remains fail-closed for the native timeline contract. |
| Compute dispatch | N | N | N | runtime N/F | Metal lowers to `dispatchThreadgroups:threadsPerThreadgroup:` using reflected workgroup metadata. WebGPU is promoted only when its runtime probe reports support. |
| Storage texture binding | N | N | N | runtime N/F | Metal direct descriptor lowering binds native `MTLTexture`; per-format probes gate Rg16Float/Rgba16Float. |
| Storage buffer binding (SSBO) | N | N | N | N | Metal direct buffer binding is native when the reflection slot and resource payload are valid. |
| Scene geometry ray query (software BVH) | N | N | N | F | DDGI software BVH lowers to SSBO + compute dispatch; Metal also requires its native shader/resource probes. Unsupported paths retain the explicit analytic terrain fallback. |
| Native synchronization | N | F | N (SharedEvent/probe gated) | F | Vulkan binary `GpuSemaphore` remains diagnostic; WebGPU queue-side semaphore/timeline waits are unavailable; Metal uses `MTLSharedEvent`. |
| Subpass input attachment | N | N | N (reflection-gated) | E | Vulkan has native subpass path; Metal uses verified framebuffer fetch; WebGPU rewrites to separate passes. |

`*` ~~The current capability contract does not declare Vulkan/Metal compute or
storage-texture features even where command interfaces exist.~~ Resolved for
Vulkan in Phase 1 (`vulkanNativeFeatures()` now declares `ComputeDispatch` +
`StorageTextureBinding`); Metal now has native command, direct descriptor, and
per-format probe lowerings. The remaining Metal restrictions are explicit
contract boundaries documented below.

## Metal lowering boundaries

The following are the current lowering boundaries. Each path is fail-closed
when its required artifact or runtime contract is absent:

- RenderBundle selects a native Metal ICB when the runtime capability is
  available. `allowCpuReplay=true` selects the explicit CPU replay IR instead;
  both paths carry index-buffer state and validate the consuming topology.
- Native secondary ICB recording supports push constants by replaying each
  validated push update into the consuming render encoder immediately before
  the associated draw. `allowCpuReplay=true` remains an explicit alternative.
- GPU-counted indirect draw is `EquivalentLowering` on Metal when the generated
  shader carries `MetalGpuCountedIndirect/1`: Metal emits the caller-bounded
  indirect records and the reflected `[[draw_id]]` mask suppresses IDs beyond
  the GPU count. Pipeline creation and draw recording both reject missing or
  inconsistent ABI metadata; the device capability advertises the substrate,
  not an ABI-free shader.
- PatchList is `Native` on Metal only when the generated
  `MetalTessellation/1` artifact supplies the vertex entry point, factor
  format, factor byte layout, and control-point count. The shared RHI binds a
  dedicated tessellation-factor buffer and Metal issues `drawPatches` /
  `drawIndexedPatches`; missing ABI or factor state is `DiagnosticFailure`.
  PatchList indirect draw commands are a separate `DiagnosticFailure`: Metal
  has no native indirect-patch encoder for the shared indirect-record ABI, so
  the backend must not perform a CPU readback or reinterpret the records as
  triangles.
  Vulkan and WebGPU remain fail-closed, and no backend may reinterpret
  PatchList as triangles.
- Direct descriptor-set arrays are source-complete only when the generated
  `MetalArgumentBuffer/1` reflection contains one unambiguous mapping with
  sufficient capacity. Missing or ambiguous array metadata fails closed;
  physical slots are never inferred by incrementing direct bindings. Argument
  Buffer tables remain a separate lowering.
- Metal layered render attachments are source-complete and require explicit
  `RenderPassDesc::layeredRendering`, matching `Texture2DArray` views, and the
  reflected fragment array-index ABI. Metal explicit multi-mip/layer transfer
  and readback layouts are also lowered per subresource. Vulkan, WebGPU, and
  Null reject these new forms instead of silently applying their legacy
  single-mip/single-layer lowering.

## Pipeline state lowering (PSO gate)

Source: `src/rhi/semantics/native_pipeline_state_lowering.{h,cpp}`,
`src/rhi/gpu_pipeline.h` (`validateRenderPipelineState`,
`validateComputePipelineState`), and the four backend
`createRenderPipeline` / `createComputePipeline` overrides.

Every backend `createRenderPipeline` / `createComputePipeline` override runs
`lowerRenderPipelineState` / `lowerComputePipelineState` as a capability gate
before any native allocation. The gate runs the shared L2 validator first,
then records per-backend vocabulary semantics, then applies capability checks.
A descriptor the device cannot lower fails closed: `RhiErrorHandler::report(
Error, ...)` + `return nullptr`. The gate does NOT replace the existing
`validateRenderPipelineState` / per-backend `validateXxxRenderPipeline`; it is
an additional capability-layer check that runs first.

| PSO lowering facet | Null | Vulkan | Metal | WebGPU | Evidence |
|---|---|---|---|---|---|
| Render pipeline capability gate | N (reference validator) | N | N | N | `arp_rhi_pipeline_state_parity_tests`, `arp_rhi_pipeline_contract_tests`, `arp_rhi_depth_bias_clamp_contract_tests` |
| Compute pipeline lowering contract | N | N | N | N | `arp_rhi_pipeline_state_parity_tests` (compute parity + fail-closed) |
| `depthBiasClampSupported` capability | true (parity) | runtime `VkPhysicalDeviceFeatures::depthBiasClamp` query (VUID-00754); static fallback false | true (spec) | true (spec) | `native_backend_capabilities.cpp`, `render_system_vulkan.cpp`, `device_capability_query.cpp` |
| depthBiasClamp fail-closed gate | N/A (true) | F when feature unsupported + non-zero clamp used | N/A (true) | N/A (true) | `arp_rhi_depth_bias_clamp_contract_tests` |
| depthBias native payload | stat-only reference | native `VkPipelineRasterizationStateCreateInfo` | payload fields + `setDepthBias:slopeScale:clamp:` encoder call | native `WGPUDepthStencilState` fields | source-text assertions in parity test; Vulkan game smoke |

Status:
- **Null**: Backend Validated (gate runs on real pipeline creation in contract tests).
- **Vulkan**: Backend Validated (real device smoke `arp_world_of_claudecraft --hidden --frames 5 --enable-shadows` exercises shadow + overlay depthBias pipelines through the gate; `depthBiasClamp` feature queried and enabled).
- **Metal**: Native source lowering complete (payload fields and `setDepthBias:slopeScale:clamp:` call); no Apple-platform validation in this environment.
- **WebGPU**: Prototype Complete (`WGPUDepthStencilState` depthBias/stencil fields lowered; no emscripten/real-device validation).

The `depthBiasClamp` fail-closed path is verified at the contract level only
(Vulkan fallback contract `depthBiasClampSupported == false` + non-zero clamp).
It has not been exercised on a real Vulkan device that lacks the
`depthBiasClamp` feature (the test machine's GPU supports it).

## Boolean device capability reporting

Source: `src/rhi/gpu_common.h`, backend `capabilities()` implementations, and
`src/rhi/semantics/fallback_plan.h`.

| Item | Null | Vulkan | Metal | WebGPU |
|---|---|---|---|---|
| `explicitFeatures.pushConstants` | true | native feature query | false | false |
| `features.pushConstants` | true | true | true via native setBytes | true via E |
| `explicitFeatures.secondaryCommandBuffers` | true | native feature query | indirect-command-buffer fact | true |
| `features.secondaryCommandBuffers` | true | true | true via ICB probe | true via C |
| `memory.transientAliasing` | true | native feature query | native `MTLHeap` placement, probe gated | false |
| `features.transientAliasing` | true | true | true when native aliasing contract is available | false via D |
| Compute core capability | shared default | shared default | shared default | runtime profile |
| Compute/storage runtime feature | true | true (Phase 1) | native runtime probe | runtime probe |
| Query pool factory | null | native timestamp | native timestamp + optional pipeline statistics | runtime timestamp |

The current `RuntimeFeatureCapabilities` fields remain booleans for fast gates.
The shared `DeviceCapabilities::executionStatuses` and
`queryExecutionStatuses` vectors preserve lowering mode and query lifecycle
without exposing native handles. Active FrameGraph execution copies both
vectors into `RhiFramePlan` and `FrameGraphRhiCommandExecutionReport`; a
created timing pool may replace the capability snapshot with its per-pool
state.

## Command and resource implementation matrix

| Capability | Null | Vulkan | Metal | WebGPU |
|---|---|---|---|---|
| Primary command encoder | L2 validation/reference | native command payload path | native command payload path | browser native payload path |
| Render / compute / transfer passes | validation | implemented | implemented | implemented, runtime dependent |
| Push constants | strongest current validation reference | native | native reflected `set*Bytes`; secondary updates are consumed on the render encoder with revalidation | equivalent upload-ring lowering |
| Resource transitions | state validation | pipeline barrier lowering | resource-barrier lowering | usage-transition lowering |
| Buffer/texture copy | validation; explicit per-mip layouts rejected | native single-mip layout path; explicit per-mip layouts rejected | native per-mip/layer layout path | native single-mip layout path; explicit per-mip layouts rejected |
| Indirect draw | not a current conformance claim | fixed-count native path; GPU-count native path | fixed-count native path; GPU-count `EquivalentLowering` with reflected mask ABI | fixed-count native path; GPU-count path runtime dependent |
| PatchList tessellation | validation only | diagnostic failure | native draw path with reflected Metal tessellation ABI and factor-buffer lifecycle | diagnostic failure |
| Indirect dispatch | not a current conformance claim | native path | native path | native `dispatchWorkgroupsIndirect` when the runtime command payload is available; otherwise diagnostic failure |
| Secondary commands | reference/soft path | native secondary path | ICB or explicit CPU replay | CPU replay |
| Render bundle | recording fixture | recording object | native ICB or explicit CPU replay | no executable submission path |
| Query/timestamp | unavailable | timestamp query path | native timestamp query path | timestamp query path when runtime support exists |
| Pipeline statistics query | unsupported | unsupported | optional native counter set, per-pool/per-query readback | unsupported |

## Render pass and subpass matrix

| Semantic | Null | Vulkan | Metal | WebGPU |
|---|---|---|---|---|
| Single render pass | validated | native | native | native |
| MRT / depth-stencil / resolve | validated | lowering path | lowering path | lowering path |
| Multi-subpass / input attachment | validates structure and bounds | `VkRenderPass` + `vkCmdNextSubpass` path | framebuffer-fetch path, reflection/probe gated | declared E; FrameGraph plans separate passes + sampling |
| Current evidence | Null smoke | source/contract coverage; real RenderDoc subpass evidence remains pending | no completed native framebuffer-fetch validation | no completed real-browser visual validation |

Metal multi-subpass input attachment use is valid only when the framebuffer-fetch
shader artifact and native probe satisfy the lowering contract. Layered Metal
passes are valid only through the explicit layered contract above; other
backends reject `layeredRendering` until they define an equivalent lowering.

## Synchronization, memory, and lifecycle

| Capability | Null | Vulkan | Metal | WebGPU |
|---|---|---|---|---|
| Fence / binary semaphore factory | dummy | native fence; public binary semaphore factory is unavailable (`DiagnosticFailure`) while swapchain semaphores remain private | completion fence + SharedEvent binary, probe gated | callback-backed completion fence; public binary semaphore factory unavailable |
| Timeline semaphore factory | dummy | native Vulkan timeline semaphore | SharedEvent, probe gated | unavailable (`DiagnosticFailure`); queue completion callbacks are not L2 timelines |
| Capability policy: timeline sync | N | N | N (probe gated) | F |
| Capability policy: native sync | N | F | N (probe gated) | F |
| Explicit memory control | validation | native capability | backend-owned/tile hint | backend-owned |
| Transient aliasing | N | N | N (probe gated) | D |
| Swapchain | simulated | native path | native/stub path | browser canvas/runtime-profile path |
| Lifecycle diagnostics | test-oriented | present | present | resize, surface-loss, hidden-tab coverage |

Vulkan timeline submission is lowered through `VkTimelineSemaphoreSubmitInfo`.
The broader `NativeSync` feature remains diagnostic because the public binary
`GpuSemaphore` factory is unavailable; swapchain binary semaphores remain
backend-private. WebGPU queue completion callbacks only retire backend-owned
resources and signal CPU fences; they do not implement L2 timeline ordering.

## Known alignment gaps and required governance action

| Priority | Gap | Required action |
|---|---|---|
| P0 | Push constant E paths can hide differing behavior. | Execute the authoritative Push Constant plan in `TODO.md`; introduce shared validation and `PushConstantLoweringPlan`. |
| Resolved | Lowering planner hid fallback detail and boolean capabilities flattened E/C/D. | `DeviceCapabilities::executionStatuses` and bounded status telemetry are synchronized from final probes and carried into active `RhiFramePlan`/execution reports without native handles. |
| Resolved | Metal compute, storage texture, fixed-count indirect, timestamp, timeline, and framebuffer-fetch paths were previously described as stubs. | Native source lowerings and runtime/reflection probes are now present; capability publication remains probe-gated. |
| Resolved | WebGPU transfer/push paths could report success without executable backend payload. | Command recording now gates native payload presence and native operation success before incrementing command counts; remaining browser validation is platform evidence only. |
| P1 | Metal RenderBundle and secondary push constants need an explicit replay mode. | Keep `allowCpuReplay=false` fail-closed; use the implemented CPU replay IR only when the RHI descriptor opts in. |
| Resolved | Vulkan/Metal compute and storage facts were absent from the capability contract. | Vulkan and Metal now publish executable native facts; Metal remains runtime/probe gated. Compute pipeline and dispatch lowering are both covered. |
| P1 | Secondary-command explicit flags and CPU-replay lowering differ on Metal/WebGPU. | Metal now reports/uses native ICB by default and explicit CPU replay for the payloads that need it; WebGPU remains explicit CPU replay. |
| Resolved | Timeline/native synchronization policy disagreed with dummy factories. | Vulkan timeline submission now uses `VkTimelineSemaphoreSubmitInfo`; Vulkan public binary semaphores fail closed until queue-bound lowering; WebGPU timeline and binary factories are unavailable while callback completion remains backend-private. |
| Resolved | Pipeline-statistics query results had no shared RHI result contract. | `GpuPipelineStatisticsSample` and explicit availability fields now define the shared contract; Metal has an optional native counter lowering, while Vulkan/WebGPU remain deterministic Unsupported. |
| Resolved | Transient aliasing paths lacked a shared budget/lifetime equivalence proof. | Allocation validates lifetime intervals, size metadata, physical peak budget, and completion-gated retirement before heap/placed-texture execution. Backend/platform validation and dedicated telemetry fixtures remain. |
| Resolved | Vulkan/WebGPU query pools exposed only a capability-level `NotRecorded` snapshot. | Both backends now track per-query `Recorded`/`Pending`/`Available`/`DiagnosticFailure`, reject premature reset, and use native completion-gated readback; pipeline statistics remains explicit unsupported on WebGPU/Vulkan. |
| Resolved | PSO creation bypassed capability lowering; WebGPU/Metal depthBias/stencil silently dropped; compute pipeline had no lowering contract. | Four-backend `createRenderPipeline`/`createComputePipeline` now gate on `lowerRenderPipelineState`/`lowerComputePipelineState`; `depthBiasClampSupported` capability + fail-closed gate added; WebGPU/Metal depthBias+stencil native payload fields filled; compute lowering contract added. See the "Pipeline state lowering (PSO gate)" section. |
| Resolved (source contract) | WebGPU generated descriptor groups can exceed the guaranteed four-group baseline. | `WebGpuBindingSpace/5` generates the compact group/binding map and exact physical group count from the WGSL artifact; pipeline lowering consumes that map explicitly and accepts groups above four only with the queried `maxBindGroups` limit. Browser validation of `binding_array` and its native header ABI remains pending; capability promotion stays fail-closed until platform evidence exists. |

## SSR (Screen-Space Reflection) capability status

Source: `games/world_of_claudecraft/app/ssr_renderer.h` (capability gate),
`games/world_of_claudecraft/assets/shaders/ssr.slang`,
`tests/contract/shader_tests.cpp` (SSR contract test).

The WoCC SSR renderer (`SsrRenderer`) is a two-pass reflection chain (compute
trace + graphics composite) that depends on `EngineFeature::ComputeDispatch`
+ `EngineFeature::StorageTextureBinding`. `SsrRenderer::create()` runs an
explicit capability gate and returns `{false, diagnostic}` when either is
unsupported, so the pipeline falls back to the no-SSR path (bloomColor flows
straight to fog/tonemap, matching `WOCC_DISABLE_SSR=1`).

| SSR backend status | Null | Vulkan | Metal | WebGPU |
|---|---|---|---|---|
| `SsrRenderer::create()` | success (reference validator) | success (Phase 1 + Phase 5) | runtime compute/storage probe gate | runtime probe (rgba16FloatStorage gate) |
| Trace compute pass | reference validation | native `vkCmdDispatch` + storage image write | native `[MTLComputeCommandEncoder dispatchThreadgroups:...]` when the capability gate passes | native `wgpuComputePassEncoder.dispatchWorkgroups` if probe passes |
| Composite graphics pass | reference validation | native fullscreen triangle draw | not reached (gate failed) | native fullscreen draw if probe passes |
| WGSL storage texture format | N/A | N/A | N/A | `rgba16float, write` (rewritten by `compile_shaders.py` from slangc's default `rgba32float, read_write` to match the C++ Rgba16Float texture + WebGPU write-only constraint) |
| Contract test | `tests/contract/shader_tests.cpp` SSR block (source, SPV decoration, PC size, bindings, artifact existence, WGSL format) | same | same | same |

Status:
- **Null**: Contract Complete (reference validator runs the SSR passes in the
  hidden-frame smoke without crashing).
- **Vulkan**: Backend Validated on the contract + shader level (Phase 1
  capability declaration + Phase 4 shader contract test + Phase 5 SsrRenderer
  compile-clean). Phase 8 RenderDoc capture is the final visual validation.
- **Metal**: Capability-gated native source lowering; no Apple-platform
  validation in this environment.
- **WebGPU**: Prototype Complete (storage texture format bug fixed in Phase 2,
  rgba16FloatStorage probe added to the browser profile). SSR is NOT
  Production Ready on WebGPU until a runtime `wgpuDeviceHasFeature` probe
  replaces the hardcoded `probe.rgba16FloatStorage = true` assumption.

Per AGENTS.md status declaration rules: SSR is Contract Complete on all four
backends, Backend Validated on Vulkan + Null, Platform Validated on Vulkan
pending Phase 8 RenderDoc capture. Metal/WebGPU remain DiagnosticFailure /
Prototype - NOT Production Ready.

## Update procedure

When an RHI feature changes, update this document in the same change set as:

1. the L2 contract and lowering policy;
2. Vulkan, Metal, WebGPU, and Null mapping/diagnostic behavior;
3. device capability and execution-status reporting;
4. focused unit tests plus Null hidden-window smoke;
5. backend smoke and RenderDoc/native capture evidence where visual behavior is affected.

Do not change a matrix cell to `N` merely because an interface exists or a
source-level lowering is present. `N` requires the declared L2 contract,
deterministic diagnostics, relevant contract tests, and backend evidence.
