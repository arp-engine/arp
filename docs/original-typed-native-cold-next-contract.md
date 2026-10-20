# Original typed native cold admission — next boundary

Current status (2026-10-06): Contract Complete / Prototype Complete for the
selected explicitly opted-in original typed cold, frozen synchronization,
seal/reservation/callback and execution contracts. Backend Validated for genuine
WebGPU whole and finite scalar-storage compute/graphics through the original
Recording/Submission, scopes/queue and same Pending journal. Source3 retains its
runtime-length profile; independent Source4 target-memory finite compute produces
A43/B36. Null is ValidatorOnly / DiagnosticFailure; Vulkan and Metal return stable
missing-payload failures. ColdAdmission1–5 remain State1/NativeClosure1-only and
all public typed cold validators remain Prepared-only. No general bindless,
NativeSync, visual, Platform Validated or Production Ready promotion.

The requirement-by-requirement closure is recorded in
`original-typed-native-closure-audit.yaml`, with final native evidence in
`target-finite-execution-validation.yaml`. The following design requirements and
checkpoint sections describe their original implementation boundaries; their
historical closed-gate statements are preserved and do not supersede this current
status or the separately versioned OriginalTypedNativeExecution/1 contract.

The next request must have its own explicit contract version and Prototype opt-in.
It must retain the exact bound NativeClosure2, Stream2, State2 and transaction,
and consume the existing one-shot recording seal, lifecycle reservation and
callback custody. Its payload mapping must remain backend-private. It must not
adapt an ordinary pipeline/group observation or reconstruct a second source
inventory, state graph, mutation journal or native proof issuer.

Count derivation must use the authentic immutable Stream2's positional steps,
original descriptor indices and scalar records. Independently author ceilings
for original commands, render/compute passes, pipeline/group binds, draws,
dispatches, dynamic offsets, explicit dependencies, implicit descriptor accesses,
native method calls, encoders, command buffers and original callback/registration
counts. An absent category requires zero intent and zero lowering work. No
fixed sample-length counter can replace inspection of the exact admitted stream.
Overflow, wrong version/opt-in, forged or foreign owner, wrong root/stream,
missing/unbound closure and quota/count pressure must clear the derived output
and fail before allocation or encoder creation. Existing core bounded storage,
threshold measurement and owner/allocation projections remain the reuse entries.

The existing `TransferDependencyLowering/1` proves only exact Transfer-stage
CopySrc/CopyDst scopes. It cannot authorize shader stage/access mappings. Typed
synchronization needs an approved versioned mapping for every dependency position,
resource state, stage/access pair, range and usage scope. The mapping must prove
WebGPU equivalence, or reject deterministically; a CPU transition, error-scope
success or empty native barrier payload cannot prove shader visibility. Unsupported
within-scope hazards, aliasing, access/range ambiguity and profiles must fail
closed. Render and compute usage scopes require separate verified conclusions.

The frozen decision must contain every executable native operation/payload join
and be consumed by the existing executor. Availability cannot be enabled merely
because counts or CPU descriptors validate. `Stream2::validate()` retains its
committed-state contract. Once the recording seal applies the plan, pending
validation must use that exact original seal and transaction; bypassing Stream2
validation or rebuilding operations from a mutable graph is forbidden.

Recording and Submission remain the only full native command proof issuers.
Before possible submit, issued scopes drain before rollback and owner release.
After possible submit, uncertain execution quarantines the exact root/native
owners until actual original queue drain or captured loss. Existing original
subscription/domain owners are deduplicated; separately admitted runtime loss,
recording/submission scopes and queue callback slots must all have authored
capacity and CPU budgets. CPU commit or cancellation never implies GPU completion.

| Backend | Initial cold checkpoint result | Required evidence before availability |
| --- | --- | --- |
| Null | ValidatorOnly / DiagnosticFailure | Complete intent/negative tests and hidden-frame validation; no mock native success |
| Vulkan | DiagnosticFailure | Approved original typed payload and synchronization lowering |
| Metal | DiagnosticFailure | Approved original typed payload and synchronization lowering |
| WebGPU | DiagnosticFailure | Frozen exact counts, approved positional shader synchronization, real executor consuming the decision |

Implement in four stages: separately versioned L2 request/count/synchronization
contract; backend-private immutable counts/sync/payload join; original
seal/reservation/callback admission; then real Recording/Submission. Each stage
keeps a quick contract test and Null hidden-frame/native smoke. Native validation
uses genuine Slang accepted compute/graphics and finite/whole/two-set actors,
exact/one-less/exhausted counts/owners/bytes/callbacks, full-quota hostile PMR
hot0/query0, payload/epoch/revision corruption, caller drop/cancel/scope error,
submission uncertainty and actual physical loss. Preserve previous numerical and
stream evidence. RenderDoc is unavailable (`RHI-RENDERDOC-TOOL-UNAVAILABLE`);
no visual, Platform Validated or Production Ready promotion is permitted.

## TypedNativeCold1 value contract — implementation boundary

The separate `OriginalTypedNativeColdRequest` selects contractVersion1 and
explicit Prototype opt-in, plus synchronizationVersion1 /
OriginalUsageScopeBoundaries1. This intent grants no cold, recording or execution
availability. Counts are independently authored for commands, compute/render
passes, pipeline/group binds, draws/dispatches, dependencies, dynamic offsets,
implicit accesses, viewports/scissors, attachment references and native calls.
It also independently bounds encoder/command-buffer and scope/queue/runtime-loss
callbacks/registrations, owner retention and Recording CPU metadata.

The selected vocabulary contains ordinary non-indexed Draw/Dispatch, exact
accepted pipeline and scalar storage group binds, explicit viewport/scissor,
Buffer/Texture state/memory dependencies and Finish. Its count contract charges
begin/end per pass, one pipeline call per accepted bind, one native scoped group
per admitted logical group bind, one call per draw/dispatch/viewport/scissor,
one Finish and one encoder creation. A dependency is only eligible to contribute
zero native API calls after its separate immutable usage-scope equivalence exists;
these counts alone cannot authorize that equivalence or any empty native payload.
General transfer/upload/readback, indexed/indirect/table/push-constant/debug and
other commands require complete additional mappings and are not adapted here.
This is a staged prerequisite; full bindless scope remains unchanged.

Count requirement validation is pure value validation, never trusted provenance or
publication. It must check internally consistent exact totals, integer overflow,
all independent ceilings and callback/registration capacity. Genuine derivation
must later validate the complete same bound Closure2/Stream2/root and every
operation and native identity before returning counts. Legacy NativeCold1–5 and
their typed failure gate stay unchanged.

W3C WebGPU §3.4.4 specifies one usage scope per compute dispatch and one for an
entire render pass; render setBindGroup contributes all referenced resources even
when shadowed or unused by a draw. A future frozen sync mapping must therefore
inspect every render bind, rather than rely solely on State2's implicit draw
accesses. §3.4.2 is explicitly non-normative and cannot prove shader visibility.
The retrieved specification is preserved as `webgpu-typed-sync-spec.html` under
the continuation evidence directory, SHA256
`7c67681430753937fd878fcbab9c2f0e93d757bdc24de6f3789ea0ff6c6f2438`.
No normative usage-list validation is substituted for actual native ordering,
visibility or completion proof. All four typed native lowering gates remain
closed until the complete frozen mapping and real executor consume it.

## Positional usage-list prerequisite (f4eb2c2a2a)

Status: Contract Complete / Prototype Complete for the value prerequisite only.
Quick/Null5/5, Wasm10/10, inventory224/224 and sequential browsers2/2 passed;
all prior flags/stable message multisets retained. Evidence:
`typed-usage-value-validation.yaml`. No new native payload or backend/platform
promotion. Original contribution completeness and frozen sync remain pending.

`semantics::TypedShaderUsageScopeContract` validates value records against a
complete supported positional command sequence. It reuses CommandRecordingOrder,
RecordedResourceStateSequence, ResourceAccessScope and core unsigned range/product
helpers. It clears scope/count outputs on failure and independently bounds commands,
contributions and pair checks. It creates no allocation, state owner, journal,
original resource projection, synchronization receipt or native payload.

A dispatch contribution is scoped only to that dispatch. A render binding or draw
contribution is scoped to the complete original render pass; every supplied render
bind participates even if shadowed or unused. Buffer usage is determined by the
binding type (storage-read versus storage), rather than inferred from byte ranges
or whether the shader happens to write. Whole-buffer usage compatibility rejects
storage-read mixed with storage, including disjoint ranges. The all-storage
exception permits disjoint writable ranges. Overlapping storage binding ranges
at the same draw/dispatch are refused separately, while overlapping shadowed
render binds alone do not establish a per-draw alias. Tests retain this distinction.

This value prerequisite does not establish that all genuine contributions were
supplied. The next original projection must cover every render bind/attachment,
all compute pipeline-used bindings and exact original source/binding positions.
Textures, transfer, indirect, vertex/index, descriptor tables, dynamic arrays and
other profiles cannot be authorized by these buffer-only value records. Valid
shader transitions or memory barriers still return
`RHI-TYPED-USAGE-SCOPE-DEPENDENCY-NOT-FROZEN`; usage-list success is neither
ordering nor visibility nor completion proof. Null/Vulkan/Metal/WebGPU retain
all existing closed typed cold/recording/execution gates. No backend or platform
status is advanced by this prerequisite.

## Original resource contribution projection (f4eb2c2a2b)

Status: Contract Complete / Prototype Complete for the selected original CPU
projection. Backend Validated actual WebGPU accepted actor projection/refusal only;
no new native command payload or execution availability. Final quick/Null5/5,
Wasm10/10, inventory225/225 and browsers2/2 passed. All prior stable flags/messages
retained. Evidence: typed-usage-projection-validation.yaml. Next:
typed-shader-dependency-next-audit.yaml (DefinedOnly). The new
OriginalTypedUsageProjection1 request independently bounds command traversal,
Draw/Dispatch consumption, logical shader buffers, every render binding/logical
buffer, attachment references and aggregate contributions. Compute-only intent
requires zero render/attachment work. Null is ValidatorOnly/DiagnosticFailure,
Vulkan and Metal are DiagnosticFailure (accepted actors absent), WebGPU provides
only explicit Prototype CPU projection. All four typed native gates stay closed.

The projection requires the authentic immutable Stream2, which keeps its existing
committed State2/Inputs3 validation and access/state prerequisites. It walks all
render bindings through the shared existing full-bound-view validator, active
Draw/Dispatch bindings through the existing consumption visitor, and attachments
through OriginalCommandTextureRanges. It retains no points or second inventory.
Exact source positions, pipeline and descriptor bind positions, registration and
content revisions, resolved ranges, whole-buffer identities, usage binding types
and pass/dispatch boundaries are emitted as ephemeral values after complete
preflight. Shader and attachment counts must match original Stream2 counts.
Failures clear all aggregate counts and preflight refuses before visitor output.

Supported attachment usage is the existing writable color/depth attachment
profile, with canonical view ranges and complete render-pass scope. Combined
Depth24Stencil8 is refused with STENCIL-PLANE-UNPROVEN because the existing
single-view range projection does not establish both aspect planes. No guessed
second plane or native payload is generated. Other command/resource profiles
retain explicit diagnostics. This is resource usage projection; ordering,
visibility, completion, frozen synchronization and native execution remain
separate. The shared two-set actor must still fail whole-buffer usage validation.

## Dependent shader native prerequisite (f4eb2c2a2c1)

Status: Contract Complete / Prototype Complete selected scope/test prerequisite;
Backend Validated actual WebGPU dependent storage-write numerical experiment only.
Quick/Null5/5, Wasm10/10, inventory226/226 and sequential browsers2/2 passed.
Flags354/355 preserve all351/352 and exact prior stable BINDLESS message multisets.
Evidence: `typed-shader-dependency-probe-validation.yaml`. Frozen synchronization
and typed execution remain unavailable; parent/full bindless stays incomplete.

The new test reuses the unchanged `descriptor_state_slots` Slang publication,
accepted factory and Registration7 native-loss binding. The original root contains
A/B buffer leases, one accepted pipeline and four genuine WholeWrite1/Source2
groups: pass1 reads A and writes B; pass2 reads B and writes A. Every declaration
explicitly uses ShaderWrite state; readonly bindings still declare ShaderRead
access. The two passes use distinct buffers within each dispatch usage scope.
No shader source, compiled artifact, reflection or native proof issuer changes.

The shared original state/stream fixtures now accept an explicit bounded
consumption intent and count original pipeline/descriptor binds. They no longer
choose descriptor count from sample command length. The new original Stream2 has
19 commands, two dispatches/four logical accesses, four descriptor binds and six
explicit dependencies. The same State2 graph, prototype transaction plan and
bound Closure2 are used for the actual accepted source joins. The independent
verification readback belongs to the test experiment and is deliberately absent
from the typed root; neither its copies nor seed writes are represented as typed
Stream2 transfer commands or admitted by its hypothetical native-call ceiling.

The native experiment records two compute passes using the original accepted
pipeline/group payloads, target-memory native binding facts and exact original
dispatch scalar values. It submits one actual command buffer without waiting
between passes, waits for the original queue callback and maps verification
bytes. Seed A=29 produces B=36 and A=43. The original CPU plan stays Prepared,
its journal remains empty and `nativeExecutionProven()` remains false. This
experiment creates no Recording/Submission receipt and does not lower or approve
the six original dependencies. Each dependency retains the exact
`DEPENDENCY-NOT-FROZEN` diagnostic. The independent usage-scope quick contract
checks A/B identity exchange, wrong scope, same-dispatch alias and one-less pair
capacity; existing unit and Null hidden-frame entrypoints invoke it.

This is Backend Validated numerical evidence for this selected genuine WebGPU
actor, not a complete L2 synchronization contract, Platform Validated or
Production Ready result. The parent f4eb2c2a2c remains incomplete. Its next work
is `typed-shader-dependency-freeze-next-audit.yaml`: exact initial/middle/final
clauses at positions 0/1, 8/9 and 16/17, with dispatches at 6/14. Initial state
mapping cannot fabricate external initialized content. Middle dependencies must
prove source/destination access coverage and actual read-to-write/write-to-read
visibility. Final state bookkeeping cannot claim an external read or completion.
The independently versioned frozen mapping must be consumed by the real existing
executor; unsupported profiles remain closed and graphics/attachment numerical
evidence is still required. Null/Vulkan/Metal/WebGPU typed native cold/recording/
submission, general bindless and NativeSync remain unavailable.

## State-bound complete usage projection (f4eb2c2a2c2a)

This prerequisite removes the need for a frozen decision to retain or dereference
a `GpuDevice&` during hot validation. StateBoundUsage1 has its own version and
explicit Prototype opt-in and reuses the independently authored original usage
projection budget. It requires authentic committed State2 and Stream2 owners,
then compares Stream2's exact original state owner. The captured State2 lifetime
ticket and approved projection decision guard the existing original shader and
render-binding walkers. Cold device-joined projection remains available and
continues to perform explicit original registered-device validation.

There is one original render-binding walker and one contribution mapper. The
state-bound route includes every render bind, all active Draw/Dispatch bindings
and the same canonical supported attachments. It preserves the existing source,
bind, registration/content revision, byte-range and complete scope values. It
retains no device, second source inventory, state owner, journal, projection array
or native object. Complete preflight/replay clears output counts and emits no
contribution for invalid intent, wrong authentic state, forged owner, stale source
or independently insufficient budgets. The committed Stream2 validation and
combined depth/stencil refusal stay intact; no pending state adaptation exists.

Only the existing State2 facade gains access to the private device-joined render
walker; the consumption facade still has exactly one friend. State2 grants the
original usage projector access to its guarded visitors. These are CPU metadata
validation relationships. Recording and Submission remain the only native command
proof friends. No native cold, synchronization or execution availability changes.
Four-backend conclusions are explicit: Null ValidatorOnly/DiagnosticFailure,
Vulkan and Metal DiagnosticFailure, WebGPU Equivalent CPU projection under
explicit Prototype intent and genuine original State2 admission.

Status: Contract Complete / Prototype Complete selected CPU projection only;
Backend Validated actual WebGPU accepted original actor equivalence and refusal,
with no new native command payload. Quick/Null5/5, Wasm10/10, inventory227/227 and
sequential browsers2/2 pass. Flags358/359 preserve all354/355 and exact prior stable
BINDLESS messages. Genuine actors7, stale/loss refusals22 and one real wrong-State2
same-Source rejection emit0/counts-cleared, with full-quota hostile PMR hot0/query0.
Evidence: `typed-state-bound-usage-validation.yaml`. No Platform/Production claim.
Genuine finite compute/graphics and whole/two-set actors compare state-bound output to cold projection, including every render bind
and attachment. The independent-pass numerical actor also rejects a second
original State2 built from the same Source root. Existing quota/pending/revision/
arena epoch/actual native-loss refusals cover state-bound projection. The next
frozen dependency stage consumes this guarded projector with the same authentic
loss-bound Closure2; metadata equivalence remains distinct from approved shader
ordering/visibility and real native executor consumption. The implementation
reuse audit is `typed-shader-dependency-state-bound-next-audit.yaml` (DefinedOnly).
Original graphics authoring must supply a distinct exact second Load/Store pass
owner and intervening TextureMemoryBarrier; derive texture point/cell ceilings
from existing original range/geometry helpers. This authoring/budget prerequisite
was completed in f4eb2c2a2c2b1 below; native second-pass fields match the immutable
original descriptor.
The parent dependency freezing phase and full bindless goal remain incomplete.


## Original attachment numerical prerequisite (f4eb2c2a2c2b1)

Status: Contract Complete / Prototype Complete for the selected original
pass-intent prerequisite; Backend Validated actual WebGPU native attachment
Store/Load preservation and independent readback only. Quick/Null5/5,
Wasm10/10, inventory228/228 and sequential browsers2/2 passed. Flags361/362
preserve all358/359 and all prior1051 stable BINDLESS message types with their
exact multiplicities. Evidence: `typed-attachment-dependency-validation.yaml`.
RenderDoc remains unavailable (`RHI-RENDERDOC-TOOL-UNAVAILABLE`); this phase
makes no Platform Validated or Production Ready claim.

`OriginalTypedAttachmentDependencyProbeFixture` reuses the unchanged accepted
runtime_length_graphics_probe Slang publication, genuine Registration7,
WholeWrite1/Source2, State2/Stream2 and loss-bound Closure2. The two original
RenderPassArguments have distinct owners: Clear/Store followed by Load/Store.
The complete original input comparison refuses changed load/store/target;
the pure descriptor contract also refuses changed format. Existing range and
geometry helpers derive actual texture point/cell budgets, including the
original TextureMemoryBarrier; the shared State2 fixture no longer assumes
four points. Old genuine actors and stable results remain unchanged.

The original stream has16 commands, render scopes2..8 and11..12, first draw7
and attachment memory dependency9. It projects two render-binding buffer uses,
two active shader buffer uses and two attachments. Texture points5 give sequence
cells6 and steps10 with the one buffer. The native numerical experiment consumes
the original view and pass load/store/clear fields through the existing attachment
operation lowering; the second pass contains no draw. One submitted command
buffer, no inter-pass wait, actual queue completion/map and four verified pixels
prove that the second Load/Store preserves the first pass shader output. Expected
channels3,16,3,12 come from published original target runtime-array lengths and
stride for the authored48-byte buffer, never a guessed native stride.

Independent readback resources/copies stay outside the typed root. The experiment
does not execute or approve any of its six original dependency descriptors;
it keeps `DEPENDENCY-NOT-FROZEN`, Prepared transaction, empty journal and
nativeExecutionProven false. Full-quota hostile PMR/hot0/query0, genuine quota
reclamation and all prior compute A43/B36 evidence are retained. Recording and
Submission remain the only native proof friends. Null is validator-only;
Vulkan/Metal fail deterministically; WebGPU typed native cold/recording/submission
also remain unavailable. A numerical prerequisite is not a frozen sync mapping.

Continue f4eb2c2a2c2b with `typed-shader-dependency-attachment-next-audit.yaml`.
The audit now distinguishes initial dependencies0/1, actual between-attachment
memory9, buffer memory10 with no following buffer contribution, and final
transitions13/14. Command10 must not acquire a fabricated next shader endpoint.
Exact dependency records, independent endpoint/traversal/pair/owner/CPU ceilings,
immutable native operation payload and actual existing executor consumption
remain pending. The complete bindless goal remains incomplete.

## Frozen original CPU dependency prerequisite (f4eb2c2a2c2b2)

Status: Contract Complete / Prototype Complete for the selected CPU positional
projection/frozen candidate. Backend Validated covers actual WebGPU original
actor metadata/refusal and retained numerical prerequisites only. Quick/Null5/5,
Wasm10/10, inventory230/230 and sequential browsers2/2 pass. Flags367/368 preserve
all361/362 and the exact prior1054 stable message strings/counts. Eight compatible
actors freeze, the shared readonly/writable whole-buffer actor refuses, and two
additional retained candidates reach real physical loss. Twelve genuine refusals
cover quota, pending, revision, arena epoch and finite/whole physical loss with
the exact NATIVE-LOST diagnostic, allocation0 and query0. Evidence:
`typed-dependency-decision-validation.yaml`. The complete native boundary stays
DefinedOnly; no Platform Validated, Production Ready or NativeSync claim.

`OriginalTypedDependencyProjectionContract` independently bounds dependency,
captured mapper pass/visit, compatibility pair, endpoint and texture-cell work.
The complete StateBoundUsage1 join has its own original usage bounds and validates
before the captured mapper traversals. Private ephemeral join/timeline reuse
keeps full public guards and the same contribution/range/state algorithms.
`WebGpuOriginalTypedDependencyDecision` retains the authentic loss-bound Closure2
and full original descriptor/resource/registration/scope/state fields. It unions
every matching endpoint contribution. Initial/final bookkeeping remains separate
from between-scope candidates. The attachment command10 has no following buffer
endpoint; command9 binds actual render scopes2..8 and11..12. Compute dependencies
8/9 bind dispatches6/14. All three candidate operations remain non-executable and
return `RHI-WEBGPU-TYPED-DEPENDENCY-EXECUTOR-PAYLOAD-MISSING`.

## Exact native operation binding — active next boundary

The next work consumes the existing CPU candidate rather than reconstructing
its source inventory or authorizing its enum. The preserved W3C specification
defines normative enqueue, finish and submit steps: append commands, assign
the encoder command list to the finished buffer, then execute each submitted
command. Its `beginRenderPass` queue steps load the original texture subregion
or clear framebuffer memory; `end` queue steps store framebuffer contents into
that same subregion. Rendering modifies attachment texture data and compute
results are storage-binding side effects. The exact nine anchors and source
digest are verified in `typed-dependency-native-operation-next-audit.yaml`.
The explicitly non-normative §3.4.2 is excluded from synchronization proof.

The immutable native payload must bind the original pipeline/group/resource
payloads held by this same Closure2, complete original scalar/attachment fields,
and every actual encoder/pass/dispatch/draw/finish ordinal in one command list.
For attachment visibility, it must bind the exact original stored and loaded
view/range and operation fields through `WebGpuAttachmentOperationLowering`.
For shader visibility, both original endpoint dispatch/draw operations and their
complete pipeline/group bindings must actually execute. Initial/final state
bookkeeping consumes the existing genuine seal/transaction lifecycle; it grants
no external initialization, consumer or completion evidence. A zero barrier API
call is eligible only as part of this complete consumed operation payload.

The existing `WebGpuOrdinaryRecording::recordCanonical` supports the versioned
transfer/attachment vocabulary and refuses unproven default operations. Existing
ordinary compute/graphics recording bodies supply the native emission reuse
points, while their single-pass languages provide no canonical State2 authority.
Factor backend-private emission rather than add a second executor or proof issuer.
`WebGpuCanonicalColdAdmission` retains its legacy State1/Closure1 guard; introduce
independent TypedNativeCold1 admission using the existing count schema, genuine
dependency selection and full original operation payload. Seal, reservation,
callback quotas and possible-submit quarantine must remain the existing lifecycle.

Null remains ValidatorOnly/DiagnosticFailure; Vulkan and Metal remain
DiagnosticFailure for missing original typed join/executor; WebGPU remains
DiagnosticFailure pending the complete native payload and executor consumption.
No capability default or native execution gate is promoted by this audit.

## Original native input payload prerequisite (f4eb2c2a2c2b3)

Implementation uses the independent `OriginalTypedNativePayloadRequest` and
`WebGpuOriginalTypedNativePayload` on the authentic dependency decision's same
loss-bound Closure2/Stream2/root. Direct pipeline/group/resource handles,
original scalar/offset spans and every original operation/dependency index are
frozen. Shared `WebGpuOriginalRenderPassPayload` preserves exact original
attachments and draw bounds, including draw0 for the second attachment-only
pass. Its draw count uses the existing native descriptor field, preserving
legacy metadata size. Detailed contract and validation status are maintained in
`docs/original-typed-native-payload-contract.md`.

Selected native-input preparation is Contract Complete / Prototype Complete.
Quick/Null5/5, Wasm10/10, inventory231/231 and sequential browsers2/2 pass. Flags
370/371 preserve all367/368 and exact prior1071 stable messages/multiplicities;
eight actors freeze and12 stale/loss refusals include two retained actual physical
loss payloads. Backend Validated is limited to native input binding/refusals and
preserved original probes; no payload execution, Platform or Production promotion.
Evidence: `typed-native-payload-validation.yaml`.

The complete typed native boundary is still DefinedOnly. Continue with
`typed-native-payload-lifecycle-next-audit.yaml`: an independent cold admission
must derive callback demands from the existing error-scope/loss contracts and
bind the original seal/reservation. Public Closure2/decision/payload validation
must still reject Pending. Live validation must be a distinct private contract
bound to the same genuine seal/root/journal, reusing planned-final validation
and exact native binding checks. Never persist the CPU CapturedJoin or introduce
a caller boolean to bypass the public cold guards. Existing Recording and
Submission remain the only native proof issuers; the old profiles1–5 keep their
State1/Closure1 contract.


## Independent cold metadata admission prerequisite (f4eb2c2a2c2b4a)

`WebGpuOriginalTypedColdAdmission` now binds immutable typed original inputs to
same-root genuine seal/reservation under the independent TypedNativeCold1 request.
Exact Work comes from frozen input; scope3/3 + queue1 + retained original loss1
requires eight tickets/registrations. Before claim/connection it checks authored
Source payload/encoding-action/lifecycle authority and original per-consuming-pass
viewport/scissor completeness. No Source mutation, native default, legacy cold
adapter, callback, encoder or receipt is introduced. A separately authored compute
Source reuses the extracted original actor builder and supplies genuine quota and
reservation ownership; public cold Prepared guards remain unchanged.

Selected cold metadata is Contract Complete / Prototype Complete. Backend
Validated is limited to genuine WebGPU cold input/seal/reservation binding and
refusals, plus preserved original numerical probes. Quick/Null5/5, Wasm10/10,
inventory232/232, sequential browsers2/2; flags373/374 and all prior1085 message
strings/counts preserved. Eight metadata actors refuse, one compute actor admits
19 commands/14 encoding actions/demand8 under exact-less-exhausted hot0/query0.
Genuine wrong root, wrong demand, native mutation and loss quota alias refuse;
real reservation counts release on pre-record cancellation. No typed native
execution, Platform Validated, Production Ready or NativeSync promotion.
Detailed evidence: `typed-cold-admission-validation.yaml` and
`docs/original-typed-cold-admission-contract.md`.

Continue c2b4b through `typed-cold-admission-live-next-audit.yaml`. Existing root
planned-final validation provides exact Pending journal ownership without an
alternate ledger. Native identity/scalar/ordinal checks must be factored behind
same-seal private shell/custody authority without replaying committed CPU usage
projection. The existing legacy callback custody unconditionally registers its
own `device.lost` promise handler. It cannot be used unchanged while claiming
original loss deduplication under demand8: define bounded original binding
observer delivery and teardown, or separately revise the contract and demand
before issuance. Live command, Recording, Submission and generic execution
remain closed; RenderDoc remains `RHI-RENDERDOC-TOOL-UNAVAILABLE`.


## Independent typed cold command — selected connection boundary

f4eb2c2a2c2b4b1 is Contract Complete / Prototype Complete for the independent
TypedNativeColdCommand1 opaque shell. Backend Validated is limited to actual
WebGPU original device/queue cold connection and identity/budget/claim/refusal/
reclamation tests. Quick/Null5/5, Wasm10/10, inventory233/233, browsers2/2;
flags375/376 and exact prior1092 message strings/counts preserved. Shell752 bytes,
owner-add3, failed-claim recovery, sticky peer/drop/replacement-reservation and
hot0/query0 verified. No typed encoder/callback/recording/submission/proof is issued.
No Platform Validated or Production Ready claim; RenderDoc unavailable.

The shared implementation's opaque owner retains lifetime and exact identity only;
legacy and typed wrappers preserve independent authentic typed budgets/authority.
No cast, additional completion pool, state ledger or proof issuer was added.
See `docs/original-typed-cold-command-contract.md` and
`typed-cold-command-validation.yaml`. Continue b4b2 loss observer request/capacity/
delivery/teardown via `typed-cold-command-live-next-audit.yaml` and
`docs/original-typed-loss-observer-next-contract.md` (DefinedOnly), then b4b3 private
same-shell/custody Pending validation. Existing loss1 has no observer schema;
authored CPU/count ceilings cannot silently become observer authority or a second
native handler. Parent phases and full bindless goal remain incomplete.


## Observed original subscription preparation prerequisite

b4b2a/b are Contract Complete / Prototype Complete for bounded CPU terminal
mechanics and independently opted-in original observed native binding preparation.
Backend Validated is scoped to real WebGPU subscription/device/domain/terminal/
suppression/reclamation with zero issued typed observers. V1 layout/budgets and
all old messages remain exact; one original promise handler is reused. Quick/
Null5/5, Wasm10/10, inventory235/235 and browsers2/2 pass; flags379/380 preserve all
prior375/376 and exact1094 distinct message strings/counts. Observed binding328
bytes; three native deliveries/two suppressed callbacks clear userdata and all
quotas reclaim. RenderDoc unavailable; no platform/production claim.

Continue b4b2c with `typed-loss-observer-binding-next-audit.yaml`. The empty-table
logical terminal path cannot release a possible-submit receiver while later
physical-loss/queue/foreign-drain evidence is still needed. Define executable
local/native participant counts and an authentic same-command/seal/root observer
before issuance. Reuse the existing custody State/completion pool and sole proof
issuers; public Prepared guards stay unchanged. Typed observer/live/Recording/
Submission and full bindless remain incomplete.
