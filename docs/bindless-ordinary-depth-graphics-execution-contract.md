# Ordinary Graphics depth execution

Status: Backend Validated for the authored Vulkan public Graphics2 workload.
The common production engine gate remains false; WOCC remains Traditional.

Validation findings: the initial GPU attempts exposed two fixture admission
omissions and one actual private Pending8 repeat-read mismatch. Recording-owner admission
now includes `additionalOwnerCountFor`; the independent attachment declares
one non-recycled generation for each CPU rollback, recorded cancellation and
real submission. Pending8's private capture check now accepts an already
ShaderRead color input, matching its existing L2 state/revision contract.
The final producer re-clear names both Compute and Fragment readers.
The corrected Windows build and fast contract/Null3/3 pass. Real public clean/fault
execution6 and capture1 pass, with independent shader hashes, original D32 bytes,
40 result lanes per draw, color bytes and fault0/1; validation messages are zero.
Affected RGBA8/HDR/WOCC IBL/Compute-GI regression4/4, shader codegen1/1,
Emscripten rebuild and Wasm/Node1/1, source421/421, style6/6 and pinned formatting
pass. All failed attempts and the memory-contended build remain retained in
`build/bindless-graphics-depth-execution2-1`. The report verifies105 affected
inputs and explicit artifacts; it is not a dependency-closure or integrated
source-tree seal. The broader failure/loss/reload/resize matrix and Metal/WebGPU
comparison execution remain open. No current Apple or browser execution is
inferred from host or Emscripten compilation.

## L2 contract

Public GraphicsExecution2 selects Lifecycle19, Capacity10, ContentPlan18,
Closure16, completed-depth Pending3, color Pending8 and Submission13 together.
These are independent profiles. GraphicsExecution1 and earlier Compute/color
profiles retain their original scopes and reject Content18. Caller intent must
match the exact prepared public view, including record and submit calls.

The authentic Root2 retains its selected original Fragment, Source6/Input3,
the exact completed Source2/control4 depth proof and ordinary color descriptors.
The same canonical inventory, stream, graph, state/content journal and original
private recording/submission issuers are reused. Preparing metadata grants no
completion authority. CPU commit remains forbidden after the native claim.

Depth Pending3 requires Fragment ShaderRead, the exact ordinary descriptor's
proof pointer/control block, canonical view, registration/content generation,
cell revision and the same pending journal. Physical proof ordinals include
indexed elements, the optional readback and every ordinary element, including
samplers. Memory-access ordinals must not substitute for physical slots.
Earlier Compute Pending1/2 cannot inspect ordinary depth or Content18.

Color Pending8 accepts only Content18 and completed-read0 (RGBA8) or2 (HDR).
An already ShaderRead input retains its revision without an invented upload.
CopyDst inputs require the original authored transition to ShaderRead. Native
recording verifies both color and depth against the exact original closure.

Submission13 requires the original callback-free Graphics19 command and a real
successful native completion. Only writable attachment cells become Completed;
sampled color/depth content remains unchanged. Fault bytes are copied from the
original mapped readback and published only after the same journal commits.
Cancellation rolls back unsubmitted pending state. Submitted uncertainty retains
custody and retires unpublished; wrappers, CPU flags and timeouts prove nothing.

All native objects, retained owners, metadata quotas and the pending submission
slot are admitted cold. Rejection before the native claim must preserve the
prepared transaction and return every quota. Hot recording/submission must not
allocate. No new executor, journal, resource registry or completion pool is added.

## Backend mapping

| Backend | GraphicsExecution2 / Lifecycle19 |
| --- | --- |
| Null | Validator only; no payload or native completion |
| Vulkan | Original command/pass/queue/fence, explicit prototype opt-in; capability requires queried D32/sample/color limits and enabled Fragment stores |
| Metal | Explicit `RHI-GRAPHICS-DEPTH-EXECUTION-METAL-UNAVAILABLE` |
| WebGPU | Explicit `RHI-GRAPHICS-DEPTH-EXECUTION-WEBGPU-UNAVAILABLE` |
| WebGL | Explicit `RHI-GRAPHICS-DEPTH-EXECUTION-WEBGL-UNAVAILABLE` |

The new capability is independent of GraphicsExecution1 and Compute depth6.
No corresponding Apple or browser comparison execution is inferred.

## Required evidence

Fast L2 profile/capacity/refusal tests and Null hidden-frame smoke; real Vulkan
clean/fault public execution, recorded cancellation, CPU-commit refusal, exact
and one-less submission owners, zero-allocation recording/submission and quota
return; independent RenderDoc replay authenticating shader binaries, original
D32 bytes,40 result lanes, independent color attachment and fault0/1.

The shader loads each original depth texel to form exact less/equal/greater
comparison references. Independent replay separately checks the producer's
interpolated D32 values, so equality does not rely on approximate CPU raster
reconstruction. Both distinct strict comparison samplers must produce mixed
zero/one results. Existing producer/Compute capture alone cannot prove this path.

Affected color/HDR/actual-size IBL/Compute regression, Emscripten contract build,
source guards, pinned formatter and style checks are required. Whole-workload
budgets, the broader failure/loss matrix, Metal/WebGPU implementation and the
production renderer/scene gates remain separate unfinished work.
