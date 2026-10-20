# WebGlDeviceExecution/1 — P2 implementation contract

Status: implementation in progress. P1's program component is not a device or
queue. No P2 task is closed by this document, host checks or a queried profile.

L2 intent remains the existing resource/pipeline/descriptor/command/surface API.
A new composed creation request supplies the original P1 shader publication and
explicit bounded ownership policy without changing existing public layouts,
vtables or artifact schemas. Native handles remain backend-private.

The implementation order is context and original profile; resource and cold
pipeline ownership; immutable descriptor/draw preparation; CommandEncoderBase
payload and queue issue; FBO/canvas lease; isolated Wasm build and real smoke.
P2-01..13 are accepted together after the complete execution chain passes.

ResourceOwnership/1 and ProgramCreation/1 are composed cold inputs. They do
not amend the frozen P0 planner, existing descriptors or artifact schemas.
The native resource component consumes one original runtime, a fixed object
inventory and distinct, fixed CPU metadata/upload quotas. Its fixed generation
pool contains weak original payload registrations; resource IDs come only from
allocateGpuResourceId. Views, FBOs and eventual commands retain the original
payload, context and logical charge. Releasing a public wrapper does not refund
retained storage. Releasing the resource factory retires its admission epoch;
surviving wrappers cannot revive it. Native deletion temporarily selects only
the original context and restores a foreign current context. A lost context's
objects are already retired by the browser; no completion is inferred.

RGBA8 client uploads require an explicit CopyDst role and versioned top-left,
straight-alpha byte layout. P0's frozen texture-role validator remains unchanged.
The cold client path handles declared row padding by consuming each row without
an implicit colorspace/premultiplication transform or temporary staging copy.
CPU initialization coverage and native upload acceptance are separate facts;
zero-filled allocation proves neither. Uniform buffers are explicitly CPU-owned
byte sources for ES100 uniform lowering, never native UBO placeholders. Packed
depth/stencil creation requires an observed exact 24/8 renderbuffer; no alternate
format is admitted. FBO attachment combinations and completeness are cold facts.

ProgramCreation/1 retains the exact quota-bound P1 publication and consumes only
the owning runtime's enabled extension/precision/limit facts. Native locations
are queried once after real compile/link. Every live attribute/uniform must
match the declared name, shape, type and location; absent required leaves,
duplicates, aliases and cross-stage source mismatches reject. Interface names
and compiler diagnostics have explicit byte budgets. Cold shader temporaries
and failed programs are deleted; no failed program is published or charged.
The shared production shader library is built unchanged for the isolated Wasm
component smoke. This fixture is not the production product dependency closure.

PipelineCreation/1 consumes the original shared layout and exact P1 publication.
It reuses the existing selected-graphics interface hydration, layout/reflection
validator, pipeline-state copy and VertexInputConsumptionContract. It does not
accept a caller-authored replacement shader package. All float attributes must
match original reflection; ES2's 255-byte stride and float alignment constraints
are explicit admission conditions. One RGBA8 color target and optional exact
packed depth/stencil are admitted. Other topology, sample masks, alpha-to-coverage,
blend min/max, depth-bias clamp, invalid enums and undeclared stencil reference
reject. Both stencil faces, their masks/ops/compare/reference, color/alpha blend,
write mask, depth, cull/front-face and polygon offset are explicit lowering output.
Disabled stencil has the contract-fixed non-consuming reference zero. Single-
sample triangles explicitly disable dither/sample coverage; viewport/scissor
remain target-relative executor work. Pipeline identity includes the original
artifact bytes and the composed stencil reference. State issue rechecks original
ownership and native error/loss and fails on host; it cannot be a no-op success.

DescriptorCreation/1 uses the existing L2 arena/set/write interfaces and final
DescriptorSet revision transaction. Fixed cells retain exact original buffer,
texture-view and sampler payloads from the same domain's bounded registry. A
rejected write preserves the old cell and revision. Arena reset/destruction
invalidates old epoch tickets while their owners and quotas remain charged until
the final reference, including weak observer storage, is released. Arrays, tables,
storage/compare/depth bindings and undeclared view reinterpretation reject.
Metadata publication is not initialized shader content or draw authority.
Generic finite buffer writes and nonzero whole-buffer dynamic offsets retain
their existing rejection; no alternate range interpretation is introduced.

The pipeline component smoke checks real state issue and a complete A/B/A switch,
descriptor transaction/capacity/reset, and exact owner retirement. It submits no
draw and makes no visual or GPU-completion claim. These cold components do not
constitute WebGlDevice, command/queue, canvas leases or whole-stage acceptance.

Five-backend routing for these composed inputs is explicit: Null is
validator-only; Vulkan, Metal and WebGPU reject as not applicable and retain
their existing paths; WebGL validates the input before joining original native
ownership. A host build cannot manufacture a WebGL payload. No generic factory,
graphics capability, NativeSync or ordered-command gate is opened by these
resource/program components. Whole-stage acceptance below remains mandatory.

| Domain | Required invariant and lowering |
| --- | --- |
| Context | One original WebGL1 context on the browser main runtime thread; exact attributes and sRGB drawing-buffer query; extension auto-enablement disabled. Caller-constructed profiles never authorize execution. Lost, foreign-current or retired context rejects. |
| Resources | Runtime-wide IDs from allocateGpuResourceId, exact context/generation custody, caller-bounded CPU/logical bytes/counts, no inferred physical-memory bound. No resource is published on native allocation or quota failure. |
| Texture/upload | P0 format facts, exact RGBA8 texture or queried 24/8 packed renderbuffer, explicit pixel-store state/row layout, exact initialization coverage. No D16 replacement, reinterpreted view or implicit mip generation. |
| Shader/pipeline | Original P1 publication, strict reflection hydration and cold native compile/link/interface checks. Complete L2 raster/blend/depth/stencil/MSAA state. Stencil-enabled input must supply an explicit versioned reference; existing descriptors do not define one. Unsupported state rejects before link/draw. |
| Descriptors | Original owned resources and revision, no foreign identity adoption. All required bindings/ranges/initialization and same-texture sampler conflicts checked before mutation; immutable draw-time bindings and uniform/push snapshots. |
| Recording | CommandEncoderBase, one-shot finish, fixed operation/reference/snapshot budgets, retained native objects and exact context epoch. No recording-time GL calls, shader compilation, file I/O or unbounded container growth. |
| Submit | One ordered graphics queue consumes the selected lowering and original bounded payload once. Complete preflight before first side effect; preserve accepted count on native OOM/loss. No rollback claim, native completion primitive or host signal. |
| Draw | Explicit vertex layout/offset/range; validated index contents bound actual vertex fetch. Unsupported base vertex, first instance, restart, indirect/compute/storage/bindless reject. |
| FBO | Cold-owned attachment combination and completeness, single color plus optional exact packed depth/stencil. Load requires initialized content. Clear overrides write masks/scissor explicitly. Store/DontCare controls subsequent logical content validity. |
| Canvas | Acquire/present/discard is a single logical lease, not a freely sampleable texture or controllable image count. Canvas Load across frames rejects; alpha/premultiplication/antialias/preservation/color space must match actual context. Present means issue to host, never GPU completion or physical display. |
| Lifetime | Commands retain original owners until all GL calls consume them. Ordered GL deletion is issue, not completion. Multiframe reclamation, restore/reload and expanded budgets remain P3. |
| Build | arp_rhi_webgl, actual C++ device smoke, final MIN/MAX_WEBGL_VERSION=1; no emdawnwebgpu or navigator.gpu requirement. Host factory returns DiagnosticFailure, never Null delegation. |

Null keeps validator-only semantics and its hidden smoke. Vulkan/Metal/WebGPU
keep their original native paths and explicitly reject these WebGL-only composed
creation inputs. WebGL's admitted operations are Equivalent mappings of the same
L2 intent; unsupported capabilities stay false and execution rejects. Merely
constructing a context or passing a request validator changes no generic factory
or capability gate. Shared L2 changes require tests for all five identities.

Acceptance: quick host contracts and Null hidden smoke; isolated Wasm in a real
browser with WebGPU unavailable; indexed textured geometry, depth, blend,
scissor, pipeline/descriptor switches, distinct per-draw constants, offscreen
sampling and canvas leases; negative requests before draw side effects; RenderDoc
capture/replay and AI image inspection. Only this entire batch can close P2.


## PreparedRenderCommands/1 implementation boundary

A versioned, explicit Prototype opt-in prepares one original pipeline and one
cold FBO per command. It implements the existing CommandEncoderBase and render
pass APIs; it is backend-private and opens no generic device/graphics gate.
The caller supplies draw/index-analysis/uniform/ordered-operation and CPU quota
bounds. All pass, finished-command, draw, binding and snapshot storage is cold.
Recording performs no GL call or growing allocation. Unsupported pipeline
switches, static/fractional rectangles, extra passes, barriers, extensions, transfer/compute/indirect and debug
labels reject with stable diagnostics; no state is silently dropped.

Each successful draw freezes uniform and atomic push bytes, exact vertex/index
owners, texture/sampler pairs and explicit viewport/scissor. Required descriptor
cells use the original set epoch and lifetime during recording; completed draw
snapshots survive later descriptor writes/reset/destruction. Whole-buffer dynamic
range rejection is unchanged. Sampled initialization, feedback, sampler conflicts,
actual index extrema and vertex fetch are validated before a draw commits.
Native resource writes are locked until command custody ends. No GPU-produced
pixels are copied into CPU shadow storage or presented as initialized CPU bytes.

A sealed queue preflights every operation, original context, owner, revision,
attachment Load validity and native payload before its first GL issue. Inspection
cannot mark work accepted. Real GL errors retain exact accepted operation counts,
poison the domain and invalidate attachment contents; no native rollback or GPU
completion is claimed. Clear sets every consumed write mask and scissor explicitly;
only full clears or proven CPU uploads initialize a whole attachment. Store/DontCare
updates separate native color/depth/stencil validity. GL ordered deletion retains
native issued references by ES2 specification; CPU acceptance is not completion.

Five-backend conclusion: Null validates input only; Vulkan/Metal/WebGPU reject this
WebGL-only prepared input and keep their existing execution; WebGL joins original
runtime/resource custody and issues real ES2 calls only on Wasm. Host creation
fails. This bounded Prototype remains short of Device/product/canvas closure and
cannot close P2 or claim Platform/Production validation.


Prepared execution validation includes exact red/green per-draw pixels after
uniform mutation and descriptor reset/destruction; indexed fetch extrema;
same-texture sampler conflict; sealed stale-source full preflight with zero
accepted operations; single finish/submit; and Store/DontCare followed by rejected
Load. Successful pass/binding/draw recording is allocator-count invariant. Native
C++ ES100 upload consumes columns==4 as a matrix and columns==1 as scalar/vector.
Front-face lowering applies the existing WebGlShaderCoordinates/1 winding reversal.
The common CommandEncoderBase guards borrow diagnostic views and allocate text
only on failure, preserving their existing public encoder interfaces.

The original resource owner also cold-owns one real L2 GpuQueue. Ordinary submit
consumes only these prepared original command buffers. Fence/timeline primitives
or nonzero semaphore values reject before any execution; no Dummy sync is created.
Queue count advances only after native calls may have side effects, including a
later native failure. QueueSubmitResult preserves workAccepted on such failure;
its success count is the original command count, and a failed partial issue count
is the exact accepted native-operation count. The backend-private issue report
keeps possible side effects separate from accepted count and never claims completion.
