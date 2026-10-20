# OrderedGraphicsEngine/1

P4-A covers P4-01 through P4-05 as one accepted execution chain: Contract
Complete / Prototype Complete; Backend Validated only on Chrome WebGL1 / ANGLE
D3D11. Evidence: `build/webgl1-evidence/p4/ordered-native-final-2/checkpoint.yaml`:
18 MinGW targets / 19 CTests, 6 browser CTests, 24 native captures / verified draws,
zero replay errors and AI-inspected `capture/ordered-inspection.png`. The frozen
root contains 7,871 unchanged named inputs; its JS/Wasm/data/HTML hashes match the
capture bundle. NativeSync, ordinary production factory, mini-program / Apple /
mobile Platform Validated and Production Ready remain closed. P4-B (06–12) follows.

L2 intent is one explicitly selected graphics workload: exact resource formats,
attachment/sample dependencies, original pipeline/artifact owners, bounded command
and CPU staging storage, original acquired surface and a single ordered queue.
`GraphicsWorkloadRequirements/1` describes requirements and compares query facts;
its public values never grant execution or select a different pipeline silently.
The original device must issue a controlled owner that consumes its original
context/profile, quota and pipeline payloads. Public legacy deployment reports,
including `RendererDeploymentEvidenceAdmission/1`, remain rejected.

| Backend | Ordered execution entry | Existing completion entry |
|---|---|---|
| Null | DiagnosticFailure / validator only | Validator only |
| Vulkan | DiagnosticFailure / completion path required | Unchanged native fence path |
| Metal | DiagnosticFailure / completion path required | Unchanged native completion path |
| WebGPU | DiagnosticFailure / completion path required | Unchanged native completion path |
| WebGL | Explicit Prototype original-context owner, real GL commands | NativeSync remains unavailable |

FrameGraph uses its existing compiler, dependency ordering, load/store and barrier
replay. The ordered entry rejects compute/transfer passes, async/timeline queues,
alias heaps, input attachments/subpasses, unsupported formats/usages, readback,
external completion publication and completion observers before recording. A
normal attachment-to-sampled edge remains two ordinary physical passes. Caller
flags, copied compiled metadata and capability reports cannot supply an encoder.

`OrderedGraphicsDependency/1` maps whole-resource same-state memory
dependencies to retained original command-order positions on WebGL. Both access
scopes must match the current ColorAttachment, DepthAttachment, or read-only
ShaderRead state. Conservative shader-stage masks describe ordering scopes;
they never authorize a compute operation. It does
not mutate resource state, create a completion primitive, or authorize CPU or
external visibility. The exact resource/domain and command position are checked
again before native submission; capacity, stale custody or recording in a pass
fails closed. Other backends keep their original completion/barrier entry and
reject this separate ordered entry, as specified in the table above.

Resource pooling keeps the same logical wrapper and original native domain.
Every ordered return and checkout must query that original issuer, check live
context/surface generation and no pending recorder/read custody. The result is
permission for a later use on the same ordered queue, never GPU completion.
No release after N frames, CPU signal, dummy fence, readback publication or alias
authority is introduced. Existing fence-based pool paths remain independent.

Uniform snapshots and upload source bytes may be reclaimed only once consumed
by the actual native call and after all original recorder readers retire. Uniform
ring completion notifications stay rejected; any new ordered cursor reset must
be validated against the original backing buffer. Prepared command caches are
bounded, original-generation-specific and discarded on pause/loss/resize.
Deferred transient textures/buffers and alias-heap materialization remain
rejected before compile/prepare in this ordered entry. The accepted pool path
uses already materialized graph-owned ordinary resources; no undefined initial
state is invented for a cached transient owner. Existing transient allocator,
timeline frame slots and device prepared-command caches keep their original
completion/generation gates. Uniform subranges remain subject to the original
descriptor contract; this workload uses the canonical whole-buffer binding.
Frame, command, execution, device, native resource metadata and upload quotas
must have independent original ownership. Cache-return allocation failure
destroys only the wrapper after original recorder custody retires, records a
stable diagnostic without allocating in teardown, and closes the frame path.
Pool byte/resource budgets use the exact graph-owned logical descriptor charges;
the legacy report's compiledTransient fields hold this ordered graph cache
bound and do not claim transient aliasing or resident GPU bytes.

The shared graph compiler starts materialized textures from their original
recorded state, including pooled owners. A real state transition consumes its
previous scope; access/stage accumulation applies only within that same state.

Required acceptance is fast L2/five-backend/negative contracts, Null hidden frame,
the real RenderSystem -> compiled FrameGraph -> native queue -> canvas path,
repeated frames with pool/staging reuse, stale/lost/pause/resize rejection and
RenderDoc replay plus AI inspection. Backend fixtures alone cannot close P4-A.
Target mini-program and Apple/mobile Platform Validated and Production Ready
remain independent, evidence-based outcomes.
