# Shared Storage/MRT execution contract / 1

Status: Contract Complete / Prototype Complete / bounded Backend Validated.
WebGPU shared scenarios 4/4; P4-14 remains open for Metal/Apple. No Platform
Validated, Production Ready, ordinary factory or NativeSync promotion.

The L2 source is `ConformanceStorageMrtWorkload/1`: immutable YAML, twelve float
values per frame, original Slang graphics/compute artifacts, one RGBA16F storage
output, two RGBA8/BGRA8 color outputs, three resize frames, no transient aliasing.
The dependency is input/artifact -> finite native recording -> actual output and
same canvas present -> sealed audit. Audit/readiness never authorizes execution.

| Backend | Deterministic lowering |
|---|---|
| Null | validator only, no original-output audit |
| Vulkan | existing native compute, explicit image barriers, two color attachments and three owned copies |
| Metal | diagnostic failure `RHI-CONFORMANCE-METAL-ORIGINAL-OUTPUT-JOIN-UNAVAILABLE` until an Apple original-output issuer exists |
| WebGPU | explicit Prototype opt-in original issuer; original WGSL creation actors; immutable storage input; compute write-only storage binding -> pass boundary -> fragment unfiltered texture load; two explicit color attachments; three owned map/read/unmap copies; original canvas present |
| WebGL | capability closed, `RHI-CONFORMANCE-STORAGE-MRT-WEBGL-UNSUPPORTED` |

WebGPU requires actual runtime compute/storage/rgba16float-format support and
native limits for two color attachments, storage binding ranges and dispatch
dimensions. Aliasing requests reject before execution. Ordinary recording,
production factories and NativeSync are unchanged and remain closed where unproven.

The finite journal admits exactly one compute pass/dispatch and one render pass/
nonindexed fullscreen draw per frame. Both pipelines, the 48-byte effective input
range, exact texture parents/views, complete viewport/scissor, all states/scopes
and attachment operations must match their original owners. Foreign objects,
duplicates, extra passes/draws/dispatches, push constants, missing dependencies
and malformed copies reject before native submission. Final transition to Present
emits the three issuer-owned copies from these same textures; no supplied bytes
or claims can be imported into the audit.

Budgets are declared before preparation: CPU metadata, original artifact input/
creation/native/callback/wrapper owners, bounded WGSL bytes, native input bytes,
texture allocation ledger bytes, padded readback bytes and compact output bytes.
Texture accounting reuses `estimateWebGpuRhiTextureBytes`, the existing
WebGPU allocator ledger contract; WebGPU does not expose physical allocation
bytes, and ledger bytes must never be described as measured physical VRAM.
Pipeline creation and accepted completion have independent finite deadlines.
No per-frame shader compilation or per-draw pipeline creation is permitted.

The existing queue/error-scope/device-loss transaction owns all three map callback
userdata. Accepted work retains exact command/resource owners through genuine
queue completion, all error scopes and every map/unmap, or authenticated loss with
safe callback suppression. A timeout produces no audit and does not fabricate
completion. Issuer destruction, quota/artifact invalidation and physical device
loss revoke the audit. Output visibility requires all three real readbacks.

Acceptance requires MinGW fast contract + Null smoke, Vulkan regression, browser
positive/negative/loss/deadline runs, existing Clear/Sampled/PingPong regressions,
and independent AI RenderDoc replay joining dispatch storage, fragment source,
both color outputs, three readback copies and exact presented canvas. Dawn DXIL
is recorded separately from original observed WGSL. Count WebGPU 4/4 only after
the complete package passes; P4-14 retains Metal/Apple obligations.

Final evidence: `build/webgl1-evidence/p4/p4j-webgpu-shared-storage-checkpoint.yaml`
and its custody index; `p4j-webgpu-shared-storage-final5-capture/` contains exact
original publications, observed WGSL relationships, native DXIL hashes, three
frames, nine owned native copies and three same-canvas presents. Independent
replay verifies all 155,648 output bytes and the same-input Vulkan hashes. Three
Dawn alpha-only presentation passes are recorded separately; writeMask=8 and
full before/after image equality are required, never counted as authored draws.
AI inspected the nine-output contact image. Final MinGW Debug/Release each23/23,
WebGL/private WebGPU each3/3 and seven shared browser paths pass with source and
bundle custody. Thirty Storage/MRT negatives and actual device loss preserve
accepted owners and reject failed audits. Persistent browser CTest retains this
positive/negative/loss path. The unit test and Null smoke remain in CMake.

Negative fixtures acquire the canvas only after cold pipeline creation has drained.
The mismatch must report RHI-CONFORMANCE-STORAGE-MRT-OUTPUT-MISMATCH;
accepted timeout must report RHI-CONFORMANCE-CLEAR-NATIVE-DEADLINE. A generic
native callback failure cannot satisfy either case. The same acquisition lease
and exact deadline requirement also protect Sampled.
