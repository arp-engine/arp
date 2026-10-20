# Global Illumination Roadmap

Status: Contract Complete for Phases 16 and 17. Phase 18 is Prototype Complete. Phase 19 is In Progress with runtime smoke validated. Phase 20 is Backend Validated for Vulkan GI. Phase 21 is In Progress: the Metal/mobile lowering and lifecycle contracts are complete, while Apple hardware and mobile device smoke evidence remain open.

This roadmap adds a mobile-aware global illumination path without leaking native
backend handles above RHI. The first production direction is a DDGI-lite probe
grid: bounded irradiance probes, low-frequency bounce lighting, time-of-day
updates, and deterministic fallback to the existing ambient cube and stylized AO
path on constrained devices.

## Direction

Use probe-grid GI as the main path:

- Stable for open-world camera motion because lighting exists outside the
  current screen.
- Expressible through RHI devices, command encoders, FrameGraph resources,
  compute dispatch, storage buffers, storage textures, sampled textures, and
  renderer draw-plan data.
- Portable to Vulkan and Metal without exposing `Vk*` or `MTL*` objects to
  `src/renderer`, `src/gui`, `src/render_graph`, samples, or runtime gameplay
  code.
- Scalable from mobile to desktop by changing probe count, update cadence,
  history usage, and resolve resolution.

Screen-space GI may become a later additive detail pass. Voxel cone tracing and
hardware ray tracing are intentionally out of the first GI line because they
would require larger RHI and platform commitments before the probe contract is
validated.

## Phase 16: GI Contract

Status: Contract Complete.

Goal: define renderer-facing global illumination data and frame shape before any
backend implementation.

Deliverables:

- `renderer::GlobalIlluminationDesc` with enabled flag, quality profile, probe
  spacing, probe counts, update budget, history mode, and fallback policy.
- `renderer::GiFramePlan` with bounded resource sizes and pass names.
- Mobile profile mapping for Low, Medium, High, and Desktop.
- FrameGraph contract for `GiProbeUpdate`, `GiIrradianceResolve`, and
  `SceneStylizedPbr` reading GI irradiance.

Acceptance:

- Unit test covers profile budgets, pass order, disabled fallback, and bounded
  resource sizes.
- Smoke path keeps the existing hidden showcase rendering with GI disabled.
- Documentation marks the phase `Contract Complete` only after tests pass.

Completion notes:

- `src/renderer/global_illumination.h` defines the first renderer-facing GI
  contract without backend-native handles.
- `global_illumination_contract` validates profile budgets, fallback behavior,
  probe-state sizing, and irradiance extent planning.
- The historical engine-level `scene_gui_composer` validation has been retired.
  Current GI frame-shape coverage remains in `global_illumination_contract`,
  project-owned render graph composer tests, and hidden-window smoke paths.

## Phase 17: RHI Capability Contract

Status: Contract Complete.

Goal: make GI requirements explicit in RHI before adding backend behavior.

Deliverables:

- Capability contract for compute, storage buffer, storage texture,
  `Rgba16Float` sampling, `Rgba16Float` storage, and persistent frame history.
- Null, Vulkan, and Metal capability semantics updated together.
- Descriptor and format tests proving GI resources can be represented without
  backend-native handles.

Acceptance:

- `rhi_capabilities` validates the GI feature gate.
- `rhi_binding_contract` validates the descriptor layout used by probe update
  and irradiance resolve.
- Vulkan and Metal paths may reject unsupported hardware explicitly, but the
  renderer only observes the portable capability result.

Completion notes:

- RHI descriptor contracts now include `StorageTexture`.
- `rhi_capabilities` validates `Rgba16Float` as the sampled/storage format for
  GI irradiance resources.
- `rhi_binding_contract` validates a GI descriptor set with probe storage
  buffer, irradiance storage texture, sampled irradiance texture, and sampler.
- The Metal argument-binding plan maps storage textures to texture arguments
  while preserving renderer platform independence.

## Phase 18: GI Shader Pipeline

Status: Prototype Complete.

Goal: add Slang-only GI shader sources and reflection validation.

Deliverables:

- `shaders/stylized/global_illumination.slang`.
- SPIR-V, MSL, and reflection generation through the existing shader script.
- Reflection tests for compute entry points, bindings, and push constant sizes.
- No runtime shader compilation in frame or draw paths.

Acceptance:

- Shader test proves `GiProbeUpdate` and `GiIrradianceResolve` entry points are
  reflected for compute.
- CMake shader generation lists `.spv`, `.metal`, and `.reflection.json` as
  first-class outputs.

Completion notes:

- Added `shaders/stylized/global_illumination.slang` with `GiProbeUpdate` and
  `GiIrradianceResolve` compute entry points.
- Shader reflection tests validate `GiProbeState` as a storage buffer and
  `GiIrradianceOutput` as a storage texture.
- `scripts/compile-shaders.ps1` and CMake shader outputs now generate
  `global_illumination.spv`, `global_illumination.metal`, and
  `global_illumination.reflection.json`.

## Phase 19: Renderer Integration

Status: In Progress.

Goal: wire GI into renderer planning while preserving existing scene rendering
behavior when GI is disabled or unsupported.

Deliverables:

- Scene composition creates GI resources through FrameGraph.
- Draw-plan data carries GI intensity, fallback mode, and debug mode.
- Stylized scene shader consumes GI irradiance through RHI descriptors when the
  capability gate is available.
- Retained GUI tuning exposes GI quality, intensity, and debug view selection.

Acceptance:

- Renderer tests show GI disabled matches the current ambient-cube path.
- Composer tests show GI passes appear only when the plan enables them.
- Hidden-window smoke runs `arp_stylized_showcase --hidden --frames 2 --gi-probes`.

Current progress:

- Renderer draw-plan tests validate GI scene parameter packing with default
  disabled behavior plus enabled intensity and debug-mode packing.
- Stylized scene push constants now reserve `giParams`; the default zero value
  keeps existing lighting unchanged until GI is explicitly enabled.
- `scene.slang` includes a default-off `applyGlobalIllumination` function so the
  shader contract can evolve before descriptor-backed irradiance sampling is
  wired into the runtime path.
- `StylizedSceneRenderSettings` carries GI profile, intensity, and debug mode
  from platform runtime code into the renderer draw plan without exposing
  backend-native handles.
- The stylized showcase parses `--gi-probes` and `--gi-debug`; debug mode
  implies probe GI and sets renderer debug data.
- CMake and the direct build script now include the hidden-window
  `--gi-probes` smoke path.
- Validation passed with shader generation, direct MinGW build, clean CMake
  build/CTest, and a direct final GI smoke.

## Phase 20: Vulkan Backend Validation

Status: Backend Validated.

Goal: validate the probe update and irradiance resolve path on the Vulkan RHI
backend.

Deliverables:

- Vulkan compute pipeline creation and dispatch for GI shaders.
- Vulkan descriptor binding for storage buffers, storage textures, sampled
  textures, and samplers through the existing RHI surface.
- Vulkan barrier coverage for probe update writes followed by scene shader reads.
- GPU timing rows for GI passes.

Acceptance:

- Vulkan backend tests cover compute dispatch telemetry, descriptor binding, and
  storage texture transitions.
- Hidden-window Vulkan smoke renders with GI enabled.
- Production render path adds no `vkDeviceWaitIdle`, infinite fence waits,
  per-draw pipeline creation, or per-frame shader compilation.

Current progress:

- `vulkan_compute_dispatch` now creates GI probe-state and irradiance resources
  through RHI, builds the GI descriptor set shape, binds `GiProbeUpdate` and
  `GiIrradianceResolve` compute pipelines, and records two GI dispatches.
- `VulkanBackendDevice` telemetry now records abstract compute-pipeline binding
  and descriptor-set binding without exposing Vulkan native handles above RHI.
- The test validates the last GI descriptor set name, binding count, compute
  pipeline name, and resolve dispatch group counts.
- The historical `renderer::GiComputePlan` helper has been retired during
  renderer module governance cleanup. GI descriptor, pipeline, and dispatch
  coverage now stays with portable RHI contracts, backend telemetry tests, and
  runtime renderer paths.
- `scene.slang` now declares descriptor-backed `GiIrradianceInput` and
  `GiIrradianceSampler`; shader reflection tests validate the sampled texture
  and sampler contract while preserving default-off GI behavior.
- DDGI runtime initialization no longer creates the legacy combined probe-update
  pipeline after pass decomposition. The compatibility shader entrypoint is
  retained for ABI/reflection consumers, while production dispatch uses the
  independently measurable trace, blend, border, and resolve pipelines.
- The `L05-ddgi` Render Feature Lab now keeps one `DdgiRenderer` and its
  ping-pong history alive for the full `--frames N` sequence. A local Vulkan
  run at 32x32 for four frames passed all available intermediate readbacks and
  reported `submitted_fixture_frames: 16` with GPU timing. This is multi-frame
  smoke evidence only; it does not close long-run convergence, resize/device-
  loss, or 1440p performance acceptance.
- `scripts/verify_ddgi_vulkan_smoke.ps1` now captures a hidden eight-frame
  Vulkan run into raw stdout JSON, stderr, and a checked report. The local
  report passed all seven L05 acceptance rows with GPU timing and no Vulkan
  validation errors; Metal and complete visual-capture evidence remain open.
- The same L05 fixture checks renderer recreation with a changed GI output
  extent after valid history; the new lifecycle metric confirms history is
  invalidated before the recreated renderer is sampled. This is a resize
  contract only, not device-loss recovery evidence.
- `scripts/verify_ddgi_temporal_reference.ps1` now saves and checks the CPU
  reference metrics for camera residuals, near/distant lighting steps, and
  dynamic occluder invalidation. GPU stability captures remain required.
- `scripts/verify_ddgi_evidence.ps1` now combines that CPU reference check with
  the standard 2560x1440 Vulkan L05 smoke into one report. The report preserves
  separate evidence scopes and explicitly enumerates the remaining WebGPU,
  Metal, hardware ray-query, GPU-parity, device-loss, 1080p WebGPU,
  visual-capture, and CPU-budget gates.
- The DDGI runtime now exposes fixed-size CPU reference helpers for wrapped
  probe indexing, trilinear weights, and Chebyshev visibility. These are ready
  for backend readback comparison; no Vulkan/WebGPU parity claim is made yet.
- `HardwareRayQuery` is now an explicit opt-in runtime selection. When the
  backend has not supplied an executable hardware implementation, GI planning
  fails closed with `GI-HARDWARE-RAY-QUERY-UNAVAILABLE`, releases persistent
  history, and submits no partial workload. The current renderer still has no
  hardware ray-query RHI path; this guard does not close the optional backend
  acceptance item.
- Standard 2560x1440 Vulkan L05 smokes now run without the former 64x64 fixture
  clamp and report finite readback. The FrameGraph execution path reuses the
  compiled pass topology instead of compiling the same graph twice when pass
  metadata is collected. Cached L05 callbacks now rotate the descriptor slot
  with the ping-pong frame sequence, avoiding repeated writes to one slot while
  preserving the same descriptor contract. The latest Debug eight-frame run
  measured a `0.006176 ms` GPU timestamp and `1.581 ms`
  compiled-topology-reuse CPU time. The latest direct optimized Release
  8-frame smoke measured `1.041 ms` CPU and `0.007072 ms` GPU after skipping
  unchanged transient-pool budget scans
  (`artifacts/ddgi/ddgi-vulkan-20260815-214306.report.json`); the 32-frame
  smoke measured `2.118 ms` CPU and `0.005856 ms` GPU
  (`artifacts/ddgi/ddgi-vulkan-20260815-211055.report.json`).
  All samples are explicitly scoped to
  compiled-topology reuse. The evidence report now records the GPU/CPU target
  pass states directly under `performance`.
  The GPU datapoint is below target, while CPU exceeds the `0.5 ms` target even
  for the synthetic 8-probe scene; the formal production performance budget
  remains open. The Vulkan native semantic-command recorder no longer emits
  an empty `TOP_OF_PIPE -> BOTTOM_OF_PIPE` barrier for every semantic command;
  explicit resource transitions and alias barriers remain responsible for
  ordering. Vulkan now batches pending image, buffer, and alias barriers into
  one `vkCmdPipelineBarrier` at pass/readback/end boundaries. This removes
  avoidable command-recording work but does not by itself close the CPU budget.
 - DDGI blend passes now skip a redundant `ShaderRead -> ShaderRead` transition
   on the persistent trace buffer. The change preserves the explicit transition
   when the state actually changes; the Release smoke and Vulkan hidden tests
   still pass, but measured CPU variance remains above the `0.5 ms` target.
 - Transient alias-budget accounting now keeps the logical completion-gated
   pool budget separate from the aligned physical placement peak used by the
   alias proof. This preserves Vulkan alignment correctness without inflating
   the pool budget and suppressing eviction coverage; Null, aliasing
   integration, and Vulkan resource-management smoke all pass.
 - Repeated compiled-topology execution now skips an unchanged transient-pool
   budget scan while retaining per-frame completion retirement. The Release
   1440p smoke after this change measured `1.041 ms` CPU and `0.007072 ms` GPU;
   the optimization is measurable hot-path cleanup but does not close the
   `0.5 ms` CPU budget.
 - RenderDoc 1.4 on Windows produced a real L05 Vulkan capture at
   `artifacts/ddgi/ddgi-l05-renderdoc-20260815-201606_capture.rdc`, including the
   independent DDGI compute dispatches and a present boundary. Its dark
   synthetic-clear thumbnail is command/resource evidence only; complete
   visual-phase inspection remains open. Browser WebGPU execution evidence is
   recorded in the executable capture listed below.
- `scripts/verify_ddgi_renderdoc_capture.ps1` now reproduces that capture,
   validates all independent L05 pass names, and writes a report that keeps
   `visual_acceptance_complete: false` explicit.
- A real Edge 151 WebGPU device now executes the Windows L05 fixture at
  1920x1080 for eight frames. The browser report
  `artifacts/ddgi/ddgi-webgpu-browser-20260816-webgpu-executable.json` reports
  `Prototype Complete`, the ten-pass graph trace, 8/8 finite irradiance,
  distance, and final-HDR readback frames, and a real timestamp-query result of
   `0.009984 ms`, with no uncaptured WebGPU or page errors. This closes the
   WebGPU device-smoke/timing/readback evidence item, but not CPU-reference
   parity or the production 1080p budget. The matching screenshot is
   `artifacts/ddgi/ddgi-webgpu-browser-20260816-webgpu-executable.png`.
- The refreshed Edge 151 presentation capture is
  `artifacts/ddgi/ddgi-webgpu-visual-readback-final.json`, with screenshot
  `artifacts/ddgi/ddgi-webgpu-visual-readback-final.png`. It records a
  non-background 1920x1080 canvas, `DDGI.Present`, zero page/request/console
  errors, and finite readback from the acquired canvas texture
  (`presentation_readback_available: true`,
  `presentation_readback_finite: true`). The WebGPU browser canvas/readback
  sub-gate is closed; RenderDoc evidence for every visual phase remains open.
- Vulkan native command pools and command buffers now use a bounded reusable
  cache (up to eight payloads), retaining submitted payloads until their fence
  is complete and draining the cache during device shutdown. DDGI descriptor
  fingerprints likewise skip unchanged resource writes while preserving full
  validation when identities change. These are measured Vulkan CPU-path
  optimizations; they do not close the cross-platform or 0.5 ms CPU gates.
- `ProductionDemoRuntimeControl` now accepts GI enable, intensity, and debug
  controls from explicit values and `ARP_PRODUCTION_DEMO_GI*` environment
  variables.
- `production_demo` now converts runtime GI controls into
  `StylizedSceneRenderSettings` and passes them through `RenderSystem::drawScene`.
- CMake registers `production_demo_gi_smoke` with
  `ARP_PRODUCTION_DEMO_GI=1`, making production demo GI the long-task acceptance
  target.
- `VulkanBackendDevice::createComputePipeline` now creates native Vulkan compute
  pipeline and pipeline-layout resources for the generated GI shader entries,
  with backend-only tests validating non-null native handles.
- `VulkanCommandList` now records native `vkCmdBindPipeline` and `vkCmdDispatch`
  for GI compute passes, with backend telemetry proving two GI compute binds and
  two GI compute dispatches were encoded.
- Vulkan descriptor sets now allocate backend-native descriptor layout, pool,
  and set objects for the GI descriptor shape. Reused scalar binding writes are
  validated and skipped when resource identity is unchanged; changed resources,
  physical lowering tickets, and descriptor arrays still take the full native
  update path. The cache is invalidated by renderer reset and remains a CPU
  optimization, not evidence of WebGPU/Metal descriptor validation.
- Vulkan texture transition telemetry now records bounded name/before/after data
  for GI irradiance transitions, validating the storage-write to shader-read
  barrier shape used by compute resolve output before scene sampling.
- RHI descriptor updates can now express the GI storage buffer, storage texture,
  sampled texture, and sampler writes portably. Vulkan validates native
  descriptor writes, compute pipeline layout compatibility, and
  `vkCmdBindDescriptorSets` encoding through backend tests.
- GPU timing panel rows now classify `GiProbeUpdate` and `GiIrradianceResolve`
  as global-illumination timings while preserving total frame GPU timing rows.
- The former `renderer::recordGiComputePlan` helper was removed with the
  obsolete compute-plan module. GI compute work is now represented through the
  RHI frame/graph compute pass contracts and backend-neutral command inputs.
- `StylizedSceneRenderer` now creates GI shader, descriptor, sampler, and
  compute pipelines at renderer creation, then records GI compute before the
  scene draw when runtime settings enable GI.
- The platform render-system helper passes `global_illumination.spv` beside
  `stylized_scene.spv`, so `production_demo_gi_smoke` exercises the runtime GI
  compute path.
- The GI compute shader now pins Vulkan bindings to the RHI descriptor contract
  and guards irradiance resolve writes against ceil-dispatch overrun.

Backend validation notes:

- Final verification passed shader generation, direct MinGW build, clean
  CMake/CTest, `stylized_showcase_gi_probe_smoke`, and
  `production_demo_gi_smoke`.
- Phase 20 is not `Production Ready`: Metal parity, mobile lifecycle behavior,
  real-device platform validation, and performance budgeting remain Phase 21
  and production-hardening work.

Long-task continuation:

- Harden GI compute planning as a renderer-owned plan before adding more backend
  execution details.
- Add descriptor-backed scene GI sampling while preserving the current default
  disabled path.
- Keep `stylized_showcase --gi-probes` as the fast runtime smoke.
- Add `production_demo` GI runtime settings and make
  `production_demo_gi_smoke` the final hidden-window acceptance target for this
  GI implementation line.

## Phase 21: Metal And Mobile Validation

Status: In Progress.

Goal: bring the accepted GI path to Metal and document mobile lifecycle behavior.

Deliverables:

- Metal compute pipeline and argument binding parity for GI resources.
- macOS hardware validation plan extended to include probe update and irradiance
  resolve.
- Android and iOS lifecycle notes for pause, resume, surface loss, swapchain
  rebuild, probe history retention, and memory budget fallback.

Acceptance:

- Metal parity test covers GI compute and resource binding.
- Platform validation records whether GI is enabled, degraded, or disabled per
  device profile.
- Mobile profiles keep probe memory bounded and can pause GI history updates
  while the surface is unavailable.

Current progress:

- `metal_backend` now covers the GI descriptor/argument shape used by
  `GiProbeSet`: probe storage buffer, irradiance storage texture, irradiance
  sampled texture, and sampler.
- Metal compute pipeline creation plans record descriptor-set layout count and
  total argument binding count from RHI descriptor sets, allowing
  `GiProbeUpdate` and `GiIrradianceResolve` parity checks without native Metal
  handles in renderer code.
- Metal command encoding plans can record a `BindArgumentSet` step between
  compute pipeline binding and dispatch, matching the backend-neutral
  `CommandList::bindDescriptorSet` shape used by the runtime GI path.
- macOS parity test planning now explicitly includes GI compute and argument
  binding coverage. Real Apple hardware validation is still required before
  claiming `Platform Validated`.
- `planGiMobileRuntime` now converts portable Android/iOS lifecycle inputs into
  GI enabled/degraded/paused decisions: frame submission must be allowed,
  surface/layer must be available, and memory budget must fit probe history.
- `GiMobileRuntimePlan` now carries a typed runtime status and stable diagnostic
  ID. `Native`/`Equivalent` capability is the only executable path; `Degraded`
  and `DiagnosticFailure` close the GI gate before submission and release stale
  history. Pause, memory pressure, and budget exhaustion are reported as
  distinct statuses for platform telemetry.
- Android contract coverage pauses GI history updates on pause/surface loss and
  releases GI history under memory warning or budget exhaustion.
- iOS contract coverage pauses GI history updates while inactive/backgrounded
  and releases GI history under memory warning or budget exhaustion.
- `global_illumination_contract_tests`, `android_platform_contract_tests`, and
  `ios_platform_contract_tests` are registered in CMake/CTest and cover enabled,
  paused, degraded-capability, diagnostic-failure, and degraded-memory plans.

This closes the Phase 21 portable decision contract. Phase 21 remains `In
Progress` until a Mac runner executes native Metal GI compute with timing and
readback evidence, and Android/iOS devices validate pause/resume, surface/layer
recreation, and memory-warning recovery.

## Quality And Fallback Matrix

| Profile | GI Mode | Probe Budget | Update Cadence | Fallback |
| --- | --- | --- | --- | --- |
| Low | Disabled | 0 | None | Ambient cube + stylized AO |
| Medium | Probe grid | 16 x 8 x 16 | Partial update | Ambient cube if compute/storage is missing |
| High | Probe grid | 24 x 10 x 24 | Partial update | Half-resolution irradiance resolve |
| Desktop | Probe grid | 32 x 12 x 32 | Higher budget partial update | Probe grid with debug views |

## Status Rules

- `Contract Complete`: renderer/RHI data contracts and tests exist.
- `Prototype Complete`: Null and at least one visible smoke path can exercise the
  feature shape.
- `Backend Validated`: Vulkan or Metal backend executes the path through RHI.
- `Platform Validated`: real target platform hardware has run the smoke path.
- `Production Ready`: performance, lifecycle, quality fallback, tooling, and
  hidden-window validation are complete.
