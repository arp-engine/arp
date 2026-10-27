# RHI Capability Matrix

> Snapshot: 2026-10-20
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
| `P` | Explicitly opted-in, bounded Prototype execution; not general L2 equivalence or production capability. |
| `—` | The feature is not included in the current device requirement set/report. |

Null, Vulkan, Metal, and WebGPU remain the shared four-backend L2 contract
matrix. `Backend::WebGL` is a separate fifth backend identity with a bounded,
explicitly opted-in WebGL 1.0 Prototype profile; its `P` cells do not promote
generic device capability bits or claim conformance with the full four-backend
product profile. L2 declares engine semantics; backend paths may use native
commands, contract-proven equivalent lowering, explicit Prototype execution,
or deterministic diagnostic failure. They must not silently succeed with
different behavior.

## Current L2 feature lowering policy

Source: `src/rhi/semantics/native_backend_capabilities.cpp`.

| L2 feature | Null | Vulkan | Metal | WebGPU | WebGL1 | Notes |
|---|---:|---:|---:|---:|---:|---|
| Push constants | N | N | N | E | P | WebGL1 accepts only reflected uniform data in the opted-in ordered graphics profile; generic capability remains false. |
| Secondary command buffer | N | N | N/C | C | F | WebGL1 rejects secondary encoders and render bundles. |
| Descriptor table | N | N | N | D | F | WebGL1 has bounded traditional descriptor-set objects for its profile, not bindless/descriptor-table execution. |
| Transient aliasing | N | N | N (native heap/probe gated) | D | F | WebGL1 has no heap/placed-texture aliasing path. |
| Timeline synchronization | N | N | N (SharedEvent/probe gated) | F | F | WebGL1 does not claim GPU completion or timeline ordering. |
| Compute dispatch | N | N | N | runtime N/F | F | WebGL1 compute pipelines and dispatch fail with `RHI-WEBGL1-COMPUTE-UNSUPPORTED`. |
| Storage texture binding | N | N | N | runtime N/F | F | WebGL1 has no storage-image execution path. |
| Storage buffer binding (SSBO) | N | N | N | N | F | WebGL1 has no SSBO execution path. |
| Scene geometry ray query (software BVH) | N | N | N | F | F | WebGL1 lacks compute/SSBO execution for this path. |
| Native synchronization | N | F | N (SharedEvent/probe gated) | F | F | WebGL1 fence, semaphore, and timeline factories reject deterministically. |
| Subpass input attachment | N | N | N (reflection-gated) | E | F | WebGL1 prototype supports bounded sequential graphics passes, not input-attachment semantics. |

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

## WebGL1 Prototype profile

Source: `src/platform/web/render_system_webgl.cpp`,
`src/platform/render_system_ordered_profile.h`,
`src/rhi/semantics/webgl_runtime_profile.h`, and `src/rhi/backends/webgl/`.
The profile is admitted only for a queried WebGL 1.0 context with validated
limits, precision, formats, identity, context attributes, explicit budgets,
and caller extension opt-in. A valid `WebGlRuntimeProfile` admission is not by
itself executable authority. Runtime execution requires the explicitly opted-in
`OrderedRenderSystem/1` profile and its bounded ordered-graphics workload.

| WebGL1 profile surface | Result | Boundary |
|---|---|---|
| Ordered graphics | `P` | Browser-executed scene2d, GUI, basic-forward, ordered graphics, and bounded conformance workloads; not a general FrameGraph/device feature claim. |
| Shader input | `P` | Slang-derived WebGL program artifacts targeting GLSL ES 1.00, with reflected interfaces, precision checks, and explicit extension opt-in. |
| Push/uniform data | `P` | Reflected uniform snapshots are accepted only through the profile's bounded pipeline ABI; generic capability publication remains false. |
| Traditional sampled resources | `P` | Bounded buffers, textures, samplers, descriptor allocations, uploads, and framebuffer attachments with explicit ownership and CPU budgets. No bindless table execution. |
| Compute, SSBO, storage images, GPU indirect, queries | `F` | Deterministic unsupported diagnostics; no inferred WebGL2 extension path. |
| GPU completion, fences, semaphores, timelines | `F` | CPU retirement and `glFlush`/presentation do not prove GPU completion. Shutdown GPU drain remains unavailable. |
| Canvas and lifecycle | `P` | Browser-scheduled single-use canvas leases, bounded resize/suspend-resume handling, and explicit context-loss/recovery paths; no native window or generic swapchain claim. |
| Memory and aliasing | `P` / `F` | Explicit CPU metadata/upload/logical resource budgets are enforced; physical VRAM accounting and heap aliasing are unavailable. |

Validation evidence includes `arp_rhi_webgl_profile_contract_tests`,
`arp_rhi_webgl_descriptor_lowering_contract_tests`,
`arp_rhi_webgl_resource_lifetime_contract_tests`, shader/profile contract tests,
and real-browser smoke reports under `build/webgl1-evidence/p0/`. Captures in
that evidence tree are explicitly `ProbeOnly` and report
`productionBackendValidated: false` and `gpuCompletionClaimed: false`; they are
not production backend or GPU-completion evidence. Current maturity is
**Prototype Complete for the bounded profile**, not **Production Ready**.

## Pipeline state lowering (PSO gate)

Source for the shared four-backend gate: `src/rhi/semantics/native_pipeline_state_lowering.{h,cpp}`,
`src/rhi/gpu_pipeline.h` (`validateRenderPipelineState`,
`validateComputePipelineState`), and the Null, Vulkan, Metal, and WebGPU
`createRenderPipeline` / `createComputePipeline` overrides. WebGL1 uses its
separate `OrderedRenderSystem/1` Prototype gate described above.

Each of the four shared-profile backend `createRenderPipeline` / `createComputePipeline` overrides runs
`lowerRenderPipelineState` / `lowerComputePipelineState` as a capability gate
before any native allocation. The gate runs the shared L2 validator first,
then records per-backend vocabulary semantics, then applies capability checks.
A descriptor the device cannot lower fails closed: `RhiErrorHandler::report(
Error, ...)` + `return nullptr`. The gate does NOT replace the existing
`validateRenderPipelineState` / per-backend `validateXxxRenderPipeline`; it is
an additional capability-layer check that runs first.

| PSO lowering facet | Null | Vulkan | Metal | WebGPU | WebGL1 | Evidence |
|---|---|---|---|---|---|---|
| Render pipeline capability gate | N (reference validator) | N | N | N | P (ordered graphics only) | `arp_rhi_pipeline_state_parity_tests`, `arp_rhi_pipeline_contract_tests`, `arp_rhi_depth_bias_clamp_contract_tests`; WebGL1 profile contract and browser workloads |
| Compute pipeline lowering contract | N | N | N | N | F | `arp_rhi_pipeline_state_parity_tests`; WebGL1 rejects compute pipeline creation |
| `depthBiasClampSupported` capability | true (parity) | runtime `VkPhysicalDeviceFeatures::depthBiasClamp` query (VUID-00754); static fallback false | true (spec) | true (spec) | false / unavailable | `native_backend_capabilities.cpp`, `render_system_vulkan.cpp`, `device_capability_query.cpp` |
| depthBiasClamp fail-closed gate | N/A (true) | F when feature unsupported + non-zero clamp used | N/A (true) | N/A (true) | F for unsupported state | `arp_rhi_depth_bias_clamp_contract_tests`; WebGL1 pipeline-state validation |
| depthBias native payload | stat-only reference | native `VkPipelineRasterizationStateCreateInfo` | payload fields + `setDepthBias:slopeScale:clamp:` encoder call | native `WGPUDepthStencilState` fields | no generic payload claim | source-text assertions in parity test; Vulkan game smoke |

Status:
- **Null**: Backend Validated (gate runs on real pipeline creation in contract tests).
- **Vulkan**: Backend Validated (real device smoke `arp_world_of_claudecraft --hidden --frames 5 --enable-shadows` exercises shadow + overlay depthBias pipelines through the gate; `depthBiasClamp` feature queried and enabled).
- **Metal**: Native source lowering complete (payload fields and `setDepthBias:slopeScale:clamp:` call); no Apple-platform validation in this environment.
- **WebGPU**: Prototype Complete (`WGPUDepthStencilState` depthBias/stencil fields lowered; no emscripten/real-device validation).
- **WebGL1**: separate `OrderedRenderSystem/1` Prototype gate; generic PSO/device capability publication remains closed. No compute pipeline path.

The `depthBiasClamp` fail-closed path is verified at the contract level only
(Vulkan fallback contract `depthBiasClampSupported == false` + non-zero clamp).
It has not been exercised on a real Vulkan device that lacks the
`depthBiasClamp` feature (the test machine's GPU supports it).

## Boolean device capability reporting

Source: `src/rhi/gpu_common.h`, backend `capabilities()` implementations, and
`src/rhi/semantics/fallback_plan.h`.

| Item | Null | Vulkan | Metal | WebGPU | WebGL1 |
|---|---|---|---|---|---|
| `explicitFeatures.pushConstants` | true | native feature query | false | false | false |
| `features.pushConstants` | true | true | true via native setBytes | true via E | false; profile-only reflected uniform ABI |
| `explicitFeatures.secondaryCommandBuffers` | true | native feature query | indirect-command-buffer fact | true | false |
| `features.secondaryCommandBuffers` | true | true | true via ICB probe | true via C | false |
| `memory.transientAliasing` | true | native feature query | native `MTLHeap` placement, probe gated | false | false |
| `features.transientAliasing` | true | true | true when native aliasing contract is available | false via D | false |
| Compute core capability | shared default | shared default | shared default | runtime profile | false |
| Compute/storage runtime feature | true | true | native runtime probe | runtime probe | false |
| Query pool factory | null | native timestamp | native timestamp + optional pipeline statistics | runtime timestamp | unavailable |

The current `RuntimeFeatureCapabilities` fields remain booleans for fast gates.
The shared `DeviceCapabilities::executionStatuses` and
`queryExecutionStatuses` vectors preserve lowering mode and query lifecycle
without exposing native handles. Active FrameGraph execution copies both
vectors into `RhiFramePlan` and `FrameGraphRhiCommandExecutionReport`; a
created timing pool may replace the capability snapshot with its per-pool
state.

## Command and resource implementation matrix

| Capability | Null | Vulkan | Metal | WebGPU | WebGL1 |
|---|---|---|---|---|---|
| Primary command encoder | L2 validation/reference | native command payload path | native command payload path | browser native payload path | P: prepared ordered-graphics commands only |
| Render / compute / transfer passes | validation | implemented | implemented | implemented, runtime dependent | P: bounded render-only profile; compute and transfer command paths fail |
| Push constants | strongest current validation reference | native | native reflected `set*Bytes`; secondary updates are consumed on the render encoder with revalidation | equivalent upload-ring lowering | P: reflected uniform snapshot only; generic capability false |
| Resource transitions | state validation | pipeline barrier lowering | resource-barrier lowering | usage-transition lowering | P: bounded state journal; no general barrier/visibility claim |
| Buffer/texture copy | validation; explicit per-mip layouts rejected | native single-mip layout path; explicit per-mip layouts rejected | native per-mip/layer layout path | native single-mip layout path; explicit per-mip layouts rejected | F: general transfer commands unavailable; bounded cold texture upload is separate |
| Indirect draw | not a current conformance claim | fixed-count native path; GPU-count native path | fixed-count native path; GPU-count `EquivalentLowering` with reflected mask ABI | fixed-count native path; GPU-count path runtime dependent | F |
| PatchList tessellation | validation only | diagnostic failure | native draw path with reflected Metal tessellation ABI and factor-buffer lifecycle | diagnostic failure | F |
| Indirect dispatch | not a current conformance claim | native path | native path | native `dispatchWorkgroupsIndirect` when the runtime command payload is available; otherwise diagnostic failure | F |
| Secondary commands | reference/soft path | native secondary path | ICB or explicit CPU replay | CPU replay | F |
| Render bundle | recording fixture | recording object | native ICB or explicit CPU replay | no executable submission path | F |
| Query/timestamp | unavailable | timestamp query path | native timestamp query path | timestamp query path when runtime support exists | F |
| Pipeline statistics query | unsupported | unsupported | optional native counter set, per-pool/per-query readback | unsupported | F |

## Render pass and subpass matrix

| Semantic | Null | Vulkan | Metal | WebGPU | WebGL1 |
|---|---|---|---|---|---|
| Single render pass | validated | native | native | native | P: bounded ordered graphics pass |
| MRT / depth-stencil / resolve | validated | lowering path | lowering path | lowering path | F for general MRT/resolve; profile uses its declared framebuffer shape |
| Multi-subpass / input attachment | validates structure and bounds | `VkRenderPass` + `vkCmdNextSubpass` path | framebuffer-fetch path, reflection/probe gated | declared E; FrameGraph plans separate passes + sampling | F |
| Current evidence | Null smoke | source/contract coverage; real RenderDoc subpass evidence remains pending | no completed native framebuffer-fetch validation | no completed real-browser visual validation | bounded browser graphics/conformance smoke; no general render-pass conformance claim |

Metal multi-subpass input attachment use is valid only when the framebuffer-fetch
shader artifact and native probe satisfy the lowering contract. Layered Metal
passes are valid only through the explicit layered contract above; other
backends reject `layeredRendering` until they define an equivalent lowering.

## Synchronization, memory, and lifecycle

| Capability | Null | Vulkan | Metal | WebGPU | WebGL1 |
|---|---|---|---|---|---|
| Fence / binary semaphore factory | dummy | native fence; public binary semaphore factory is unavailable (`DiagnosticFailure`) while swapchain semaphores remain private | completion fence + SharedEvent binary, probe gated | callback-backed completion fence; public binary semaphore factory unavailable | unavailable (`DiagnosticFailure`) |
| Timeline semaphore factory | dummy | native Vulkan timeline semaphore | SharedEvent, probe gated | unavailable (`DiagnosticFailure`); queue completion callbacks are not L2 timelines | unavailable (`DiagnosticFailure`) |
| Capability policy: timeline sync | N | N | N (probe gated) | F | F |
| Capability policy: native sync | N | F | N (probe gated) | F | F |
| Explicit memory control | validation | native capability | backend-owned/tile hint | backend-owned | P: bounded CPU and logical-resource budgets, not physical VRAM control |
| Transient aliasing | N | N | N (probe gated) | D | F |
| Swapchain | simulated | native path | native/stub path | browser canvas/runtime-profile path | P: single-use browser canvas lease; no GPU completion claim |
| Lifecycle diagnostics | test-oriented | present | present | resize, surface-loss, hidden-tab coverage | P: visibility, resize, suspend/resume, context-loss/recovery contracts |

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
| Resolved | PSO creation bypassed capability lowering; WebGPU/Metal depthBias/stencil silently dropped; compute pipeline had no lowering contract. | The shared four-backend `createRenderPipeline`/`createComputePipeline` paths now gate on `lowerRenderPipelineState`/`lowerComputePipelineState`; `depthBiasClampSupported` capability + fail-closed gate added; WebGPU/Metal depthBias+stencil native payload fields filled; compute lowering contract added. WebGL1 remains under its separate ordered-graphics Prototype gate. See the "Pipeline state lowering (PSO gate)" section. |
| Resolved (source contract) | WebGPU generated descriptor groups can exceed the guaranteed four-group baseline. | `WebGpuBindingSpace/5` generates the compact group/binding map and exact physical group count from the WGSL artifact; pipeline lowering consumes that map explicitly and accepts groups above four only with the queried `maxBindGroups` limit. Browser validation of `binding_array` and its native header ABI remains pending; capability promotion stays fail-closed until platform evidence exists. |

## SSR (Screen-Space Reflection) capability status

Source: `src/renderer/postprocess/ssr_renderer.h` (capability gate),
`shaders/renderer/ssr.slang`, and `tests/contract/shader_tests.cpp` (SSR
shader contract test).

The WoCC SSR renderer (`SsrRenderer`) is a two-pass reflection chain (compute
trace + graphics composite) that depends on `EngineFeature::ComputeDispatch`
+ `EngineFeature::StorageTextureBinding`. `SsrRenderer::create()` runs an
explicit capability gate and returns `{false, diagnostic}` when either is
unsupported, so the pipeline falls back to the no-SSR path (bloomColor flows
straight to fog/tonemap, matching `WOCC_DISABLE_SSR=1`).

| SSR backend status | Null | Vulkan | Metal | WebGPU | WebGL1 |
|---|---|---|---|---|---|
| `SsrRenderer::create()` | reference-validator path | enabled when compute + storage-texture capabilities are published | enabled only when both runtime capabilities are published | enabled only when compute and the probed `rgba16FloatStorage` profile capability are published | deterministic capability-gate failure (`SSR requires EngineFeature::ComputeDispatch`) |
| Trace compute pass | reference validation | native `vkCmdDispatch` + storage image write | native dispatch when the capability gate passes | native `dispatchWorkgroups` if probes pass | `RHI-WEBGL1-COMPUTE-UNSUPPORTED` |
| Composite graphics pass | reference validation | native fullscreen triangle draw | reached only after capability gate passes | native fullscreen draw if probes pass | not reached |
| WGSL storage texture format | N/A | N/A | N/A | `rgba16float, write` (rewritten by `compile_shaders.py` to match the Rgba16Float storage view and WebGPU write-only constraint) | N/A |
| Contract test | SSR shader contract in `tests/contract/shader_tests.cpp` | same shared shader contract | same shared shader contract | same shared shader contract | unsupported-feature/profile contract tests; SSR is not admitted |

Status:
- **Null**: Contract Complete as a reference validator; this is not native GPU
  execution evidence.
- **Vulkan**: Contract Complete with native dispatch/storage lowering. Backend
  visual validation remains pending a current RenderDoc capture of the SSR
  workload; source presence and compile success do not establish it.
- **Metal**: Native lowering is capability-gated; no Apple-platform validation
  is recorded here.
- **WebGPU**: Prototype Complete with runtime compute, storage-texture, and
  `rgba16Float` storage probes. No current browser SSR capture is recorded, so
  do not claim Platform Validated or Production Ready.
- **WebGL1**: deterministic diagnostic failure by contract; SSR is outside the
  ordered graphics Prototype profile.

These statuses apply to the shared Null/Vulkan/Metal/WebGPU contract. WebGL1 is
evaluated separately and remains fail-closed for SSR; its bounded graphics
Prototype does not imply compute or storage-texture support.

## Update procedure

When an RHI feature changes, update this document in the same change set as:

1. the L2 contract and lowering policy;
2. Null, Vulkan, Metal, and WebGPU shared-profile mapping/diagnostic
   behavior; also record WebGL1's separate Prototype-profile result where
   applicable;
3. device capability and execution-status reporting;
4. focused unit tests plus Null hidden-window smoke;
5. backend smoke and RenderDoc/native capture evidence where visual behavior
   is affected;
   WebGL1 browser evidence must retain its `ProbeOnly`/Prototype boundary unless
   the specific contract and GPU-completion claims are independently proven.

Do not change a matrix cell to `N` merely because an interface exists or a
source-level lowering is present. `N` requires the declared L2 contract,
deterministic diagnostics, relevant contract tests, and backend evidence.
