# IndirectArgumentConsumption/1

Status: Contract Complete / Prototype Complete for the host content/range
prerequisite; four focused tests and Apple C++17 syntax check pass. This is the
bounded ordinary indirect prerequisite in M4e5b3c4l, independent of previously
validated bindless indirect extensions. It grants no GPU execution authority.

The L2 input is an explicitly HostAuthoredImmutable cold byte source, explicit
LittleEndian/BigEndian order, byte offset, nonzero record count and byte stride.
The physical schema is DrawIndexedIndirectCommand: uint32 indexCount,
uint32 instanceCount, uint32 firstIndex, int32 vertexOffset and uint32
firstInstance at offsets 0/4/8/12/16, total 20 bytes. Offset and stride are
four-byte aligned; stride is at least 20. The finite last record footprint
must fit the source extent without overflowing any multiply/add. Padding is
excluded from the snapshot; selected records become dense little-endian bytes
in original order. Signed vertexOffset is decoded from its explicit 32-bit
two's-complement representation, independent of host casts or alignment.
Unknown schema version, byte order, origin, missing bytes or invalid extent
diagnose. ApprovedGpuProduced names a separate required proof domain; it is
rejected by this host snapshot constructor. GPU readback, cached CPU counts,
CpuReplay, the existing scalar atomic proof and packet storage leases cannot
substitute for a producer proof.

The snapshot owns canonical argument bytes and one immutable observation per
selected record. For an active record (both indexCount and instanceCount are
nonzero), cold analysis uses the exact IndexInputConsumption/1 selected span,
disabled primitive restart and signed baseVertex. Its inclusive index extrema
determine the conservative actual vertex fetch range. VertexInputConsumption/1
then validates each explicit Vertex/Instance stream using that range or the
record's firstInstance/instanceCount. The logical instance end is widened;
active records whose last instance exceeds uint32 reject instead of wrapping.
A record with either count zero performs no index or
vertex fetch, while the indirect argument record itself still must exist.
No-op records preserve all five argument fields and order; they are never
removed or converted into a cached direct draw. Their unused index/base/instance
fields do not require a fetch range. Pipeline/layout/binding validity remains
a separate requirement, including for no-op records. No scan, allocation,
readback or source access occurs in record-time observation/range validation.

Preparation requires caller budgets for maximum selected records, source bytes
and retained snapshot bytes. Snapshot bytes account for the canonical argument
bytes plus the actual sizeof each stored observation; actual container/object
allocation uses core CPU quota/PMR. Budget overflow rejects before allocation;
quota exhaustion throws the existing allocation failure to the owning cold
factory. Failure publishes no partial bytes/observations. No growth/retry or
unlimited argument cache is allowed on the command path.

Arguments require IndirectBuffer usage and ShaderRead / DrawIndirect /
IndirectCommandRead (ShaderStage::None). Canonical snapshot byte extent is the
only authorized dense argument-read extent; immutable host preparation requires
an exact backend upload and write seal, canonical issuer/native allocation
identity and upload-complete state before record. A CPU snapshot alone never
proves the bytes in an existing GpuBuffer. A later prepared root must own the
exact argument snapshot, uploaded argument/index wrappers, original canonical
payloads, device/registry/object/allocation control blocks, selected pipeline,
vertex streams and their complete L2 content/layout bindings. Caller-authored
observations/min-max values cannot issue that root. Full owner union and command
budget admission precede encoding, with deterministic one-less rejection and
no native mutation. Immutable prepared root revalidation occurs before each
draw; replacement, seal/state loss, registry retirement or foreign issuer
poisons/rejects recording under the existing command contract. Open/finished
cancellation rolls back the shared MutationJournal before owner retirement.
Accepted submission retains original owners until real completion, even if
caller wrappers die or native identity is replaced. CPU frame retirement and
completion callback metadata are not GPU completion proofs.

An approved GPU producer instead needs a nonforgeable proof of the entire
20-byte schema and every reachable active index/vertex/instance range (including
any mutated index/vertex source), exact source/output generation and owner
identity, finite output capacity, explicit producer scope and an ended producer
encoder on the same recording identity. An exact approved dependency must
consume producer scope into ShaderRead / DrawIndirect / IndirectCommandRead
before graphics opens. BindlessIndirectPacket/processor intent and recording
leases are reused for their original protected-storage and exclusive recording
semantics; their generations and snapshot authorization must match exactly.
Producer proof must also cover the consumer shader's native firstInstance ABI
or the existing IndirectInstanceBase/1 zero-base mapping; capability and proof
select that lowering before execution. GPU counts, secondary encoders,
unproven producers, cross-command consumption and mutable fetch data remain
outside v1 until their own approved contracts/payloads exist. No native path
may fill these gaps with host replay, guessed bounds or capability inference.

| Backend | Current bounded ordinary indirect conclusion | Future payload requirements |
| --- | --- | --- |
| Null | Equivalent, validator only, nonexecutable | Validate exact records/fetch/scope/budget; no GPU authority |
| Vulkan | DiagnosticFailure, payload missing | Exact sealed upload, vkCmdDrawIndexedIndirect, explicit stride/count, drawIndirectFirstInstance feature or independently proven zero-base shader ABI, real dependency/completion owners |
| Metal | DiagnosticFailure, payload missing | Exact sealed upload, actual indexed-indirect encoder payload using the fixed five-field ABI; each explicit record consumed in order, no CPU decode/replay; native consumer instance ABI/proof, tracked resource ordering and real completion owners |
| WebGPU | DiagnosticFailure, payload missing | Exact sealed upload, drawIndexedIndirect per explicit record, indirect-first-instance explicitly enabled or independently proven zero-base ABI, pass ordering and submitted-work completion owners |

IndirectArgumentLowering/1 records these current conclusions without accepting
caller-provided capability booleans. New ordinary native execution requires
versioned payload evidence and execution gates. Public CommandRetentionBudget
and prepared Graphics versions remain zero. Existing legacy/extension paths
retain their existing contracts and are not evidence for this new domain.

Required fast tests cover schema/range/endianness, source destruction/mutation,
signed extrema, no-op records, per-stream vertex/instance bounds, all budgets,
quota exact/one-less, no allocations during observation, four-backend gates and
unknown/GPU provenance negatives. Each phase also runs Null hidden-frame smoke.
Native Backend Validated, RenderDoc visual acceptance, Platform Validated and
Production Ready need their independent actual evidence. The Apple host's
RenderDoc tool absence must be recorded; host math tests cannot close those gates.

## Immutable native preparation prerequisite (M4e5b3c4l2a)

Status: Contract Complete / Prototype Complete; scoped Backend Validated for
actual private Metal immutable argument storage only. A private Metal prepared indirect
root consumes the exact issuer-owned prepared index root and source/budgets
above. It uploads the dense canonical argument bytes to a dedicated
IndirectBuffer-only CpuToGpu buffer in ShaderRead state and seals host, GPU
storage and GPU transfer writes. Reuse the index preparation's exact upload,
canonical registry/native identity and write-seal checks through one shared
backend utility; preserve the existing index diagnostics/owner projection.
The new root retains the exact index content/root, argument wrapper, registry,
native payload, original device/object/allocation identity and unique owner
projection under the index/provider's existing core CPU quota. Construction is
nonforgeable; registry/device mismatch, diagnostic runtime, stale index/provider,
seal or canonical payload/control-block replacement reject. Actual creation,
upload, source destruction, byte readback, write refusal, quota exact/one-less,
and original owner retirement are required evidence. Preparation is not an
executable draw ticket: the new bounded indirect recording entry remains absent
and public versions stay zero. Null remains Equivalent validator-only;
Vulkan/WebGPU and Metal bounded ordinary indirect recording remain
DiagnosticFailure. Absent Metal runtime preparation itself fails diagnostically.
Fast contract and Null hidden-frame smoke accompany native preparation smoke.
This storage prerequisite does not establish Backend Validated execution,
RenderDoc visual, Platform Validated or Production Ready.

Evidence: `ordinary-indirect-preparation-report.yaml` and matching build/test/
syntax/capture diagnostics in
`build/mac-metal-debug/artifacts/bindless-prepared-graphics/`. Both Uint16 and
Uint32 issuer-owned index roots bind two ordered records to exact 40 native
bytes. Source mutation cannot change CPU or GPU content. The root retains 27
unique child owners; measured CPU factory peak is 2064 bytes on this build,
used only by the exact/one-less quota test, never a runtime heuristic. Actual
GPU allocation budget permits exactly 40 remaining bytes and rejects 39 with
BUFFER-CREATION-FAILED, returning no placeholder and restoring all charges.
Sealed host/writable descriptor refusal, canonical issuer/state/registry/native
object/device/allocation and control-block negatives, actual native extent/
Shared storage/tracked queries through the existing physical buffer observer,
L2 memory/initial state consistency, original identity
replacement/retirement and allocation0 revalidation under exhausted quota pass.
The shared upload utility preserves old index diagnostics and actual command
owner unions 37/43/49. The current SDK's native five-field schema is also checked
by exact sizeof/offsetof assertions. Raw bounded indirect still rejects before
owner admission or encoding with OPERATION-UNIMPLEMENTED. 43 build targets,
62 regressions, focused fast/Null/native smoke and 29 SDK C++17 syntax checks
pass. New shaders are not introduced in these two preparation prerequisites.

## Host indirect recording audit (M4e5b3c4l2b)

Status: Contract Complete / Prototype Complete; scoped Backend Validated for
actual private opted-in Metal immutable host indexed-indirect recording only.
Public execution gates remain zero. The existing legacy Metal indirect method
already emits native indexed-indirect calls at each finite argument offset,
but its unbounded resource vectors/index bind do not satisfy this domain and
its retention entry rejects. The approved private path must consume the issued
root and the exact dense argument-read scope, the same provider/registry/quota,
current prepared pass/descriptors/push and all declared prepared Vertex/Instance
streams. Validate every active record's precomputed fetch range before admitting
the complete root/child owner union and before the first native call. Native
argument records, including no-op records, are consumed in original order;
there is no CPU argument decode, replay, compaction or direct-draw substitute.
Explicit per-recording call/record bounds must prevent unbounded native work or
uint32 command accounting overflow. Actual argument/index native extents and
device/control-block identity must match their frozen canonical uploads.
The private path inherits the ordinary Graphics TriangleList/sampleCount1
domain. TriangleStrip rejects with RHI-METAL-ORDINARY-GRAPHICS-DOMAIN-UNIMPLEMENTED
before pass encoding. A real native audit proves that disabled-restart maximum
Uint16 values participate as ordinary indices in TriangleList when a negative
baseVertex maps them into the prepared stream; no index rewriting is performed.

The current validateOrdinaryGraphicsDraw combines pass/descriptor/push checks
with nonempty vertex/instance fetch checks. A no-op record cannot be validated
by substituting a fake one-element draw. Extract/reuse the common fixed binding
validation and preserve no-op's separate lack of fetch while still requiring
valid pass/pipeline/descriptors/push/stream bindings. Reuse existing retention
and actual completion/cancellation machinery; resource transitions, if needed,
belong to the same MutationJournal, not a parallel state cache. Raw index/indirect,
GPU-counted, secondary, writable producer and foreign roots remain rejected.

Acceptance needs native data/pixels for Uint16/Uint32, signed bases, nonzero
firstIndex/firstInstance and multiple records; a native no-op control with unused
extreme fields; exact/one-less union and cold/record quotas; whole-batch rejection
without partial encoding for stale or out-of-range later records; changed
pipeline/descriptors/push/streams/registry/native identities/seals/state; actual
shared-event delayed completion with commands retained/dropped and original
owner retirement; open/finished cancellation, allocation0 record/finish at
exhausted quota and independent RenderDoc diagnostics. A native failure cannot
be repaired by rewriting/removing no-op records. Only actual native evidence may
change the private host execution conclusion; all public gates remain zero.
GPU producer/schema/order/generation/fetch acceptance is a subsequent independent
c4l3 phase and does not follow from host execution success.

Before implementation, IndirectRecordingBudget/1 fixes one prepared render-pass
pair, explicit opt-in/version and independent maximum native command units,
indirect calls and native argument records. Pass begin/end and each accepted
prepared vertex or Graphics rebind consume one command unit; an indirect batch
consumes one call and exactly its record count in both record and command units,
including no-op records. Every accepted state/draw command reserves room for
the final pass end. Arithmetic uses remaining-capacity checks before increment.
Failure changes no counters/owners/native commands. An ended pass requires at
least one accepted indirect call; finish requires that ended pair and adds no
native command unit. A second pass, viewport/scissor mutation, direct/indexed
draw, Compute, blit, writable, query, barrier, secondary and raw descriptor
entries are outside this private prototype allowlist and deterministically
reject. Cancellation is terminal. The factory must consume explicit intent and
fresh bounded retention storage with no queries; a normal private command
factory cannot issue this execution authority. Null validates the value budget;
public GPU lowering stays DiagnosticFailure. The approved Metal prototype uses
the exact actual native indexed-indirect ABI/payload and existing real completion
ownership, never a CPU replay or public capability promotion.

Evidence: ordinary-indirect-recording-report.yaml and matching logs/XML/capture
diagnostic under build/mac-metal-debug/artifacts/bindless-prepared-graphics/.
Eight actual Metal scenarios cover Uint16/Uint32, signed bases and retained/
dropped commands. Each records32 native batches/128 records/132 command units
including two extreme-field no-ops per batch. Exact green output, original
argument/index object/token retirement after real delayed event completion,
owner unions37/47/57 and push40/41, exact/one-less budgets and measured cold
factory peak11520 bytes pass. Later-record Vertex/Instance failure admits no
native draw; subsequent only-no-op commands preserve clear51/102/153/204.
Push freshness/captured native ABI, domain/scope/identity/state/descriptor
negatives, open/finished cancellation and allocation0 record/finish under
exhausted quota pass.43 build targets,62 regressions,4 focused quick/native/Null
tests and29 SDK syntax checks pass. RenderDoc tools are absent on this host;
visual acceptance, Platform Validated, Production Ready and full plan remain
open. No shader was added and no GPU producer is authorized by this phase.

## GPU producer acceptance language (M4e5b3c4l3a)

Status: Contract Complete / Prototype Complete for the binary/L2 prerequisite;
no executable GPU output. ComputeFixedIndirectProducer/1 is an independent initial
producer domain: one selected Compute entry, LocalSize(1,1,1), one dispatch
group(1,1,1), exactly one storage buffer, five unconditional uint32 literal
stores to elements0..4 in that order. Array stride4/member offset0 and the
DrawIndexedIndirectCommand field offsets0/4/8/12/16 establish the full20-byte
schema. The vertexOffset word is signed two's-complement schema data; the
producer stores its exact uint32 bits. No loads, atomics, branches, calls,
aliases, push constants, inputs, other writers or dispatch dimensions are
accepted. The selected actual SPIR-V must independently prove every address,
type and literal. Names/reflection/caller-authored footprint cannot issue proof.
Scalar atomic proof and packet/processor ownership leases cannot satisfy this
language. The shared scalar SPIR-V acceptance machinery is extended rather
than duplicating its instruction/type/decoration resolver.

This first domain leaves every index and Vertex/Instance source immutable.
The five proven words feed the existing IndirectArgumentConsumption/1 cold
schema/fetch analysis for the exact issuer-owned index root; every prepared
stream must satisfy that resulting active/no-op range. This is an expected
GPU-output proof, not a host snapshot that authorizes existing GPU bytes.
Missing producer execution/order/generation/native output identity still fails.
Mutable fetch sources and dynamic/count-driven outputs remain rejected until
separate binary range proofs and native payloads are implemented.

Execution requires a backend-issued nonforgeable output root owning the exact
selected producer artifact/function/pipeline, protected20-byte argument buffer,
index/consumer provider and complete original native/device/registry/token
projection. A finite budget and exclusive recording lease bind exact output
generation, recording identity and consumer authorization. No host mutation,
second writer, cross-command reuse or snapshot/generation substitution is
allowed. The compute encoder must end after the one admitted dispatch; an
exact ShaderWrite/ComputeShader/ShaderWrite to ShaderRead/DrawIndirect/
IndirectCommandRead dependency precedes opening the consumer pass. State and
cancellation use the existing resource MutationJournal. The selected consumer
must independently prove native firstInstance ABI or the approved zero-base
mapping. Original owners/leases retire only at real completion. GPU readback
is test evidence only and can never supply production record-time bounds.

| Backend | Current conclusion | Required future native execution |
| --- | --- | --- |
| Null | Equivalent validator-only, nonexecutable | Full binary/schema/fetch/order/generation/budget validation |
| Vulkan | DiagnosticFailure | Selected compute payload, exact storage descriptor, one dispatch, explicit compute-to-indirect barrier and tested firstInstance gate |
| Metal | DiagnosticFailure | Selected compute payload/queried writable ABI, one dispatch, ended tracked compute encoder/dependency, real native indexed-indirect consumer and completion |
| WebGPU | DiagnosticFailure | Selected WGSL compute payload, exact storage binding/one workgroup, ended compute pass, indirect-first-instance or proven zero-base consumer, submitted-work completion |

Binary proof alone cannot change these gates or public versions. Fast hostile
binary contract tests, generated SPIR-V/MSL/WGSL/reflection and Null hidden-frame
smoke precede native preparation/recording. Backend Validated, RenderDoc visual,
Platform Validated and Production Ready remain independent acceptance gates.

The l3b1 cold Metal pipeline prerequisite compiles only the exact retained
single-entry MSL bytes after full selected SPIR-V proof and L2 layout/reflection
validation. It reuses Metal's versioned metadata lowering and compute pipeline
creation. The root owns the sealed artifact/interface/layout and complete
original native pipeline/library/function/device control blocks under an
explicit core quota. The real selected Compute buffer argument must be uint32,
ReadWrite, offset0 with nonzero queried alignment and required native scalar
span4; that scalar ABI does not replace the independently proved20-byte store
footprint. The native workgroup dimensions and physical mapping must match the
proof; replacements or changed ABI/owner mapping diagnose. Null remains
validator-only, Vulkan/WebGPU preparation fails explicitly, and absence of real
Metal payload returns a diagnostic with no placeholder. Preparation has no
argument output, dispatch, generation lease or indirect recording authority.

l3a evidence: ordinary-indirect-producer-proof-report.yaml. Actual selected
SPIR-V proves words3/2/1/fffffffe/1, signed baseVertex-2 and all five ordered
addresses, types and literals. The shared ScalarStorageBinaryProof preserves
Fragment atomic/read contracts and rejects unknown extensions. Hostile alias,
reorder, missing-store, stage/local-size/type/value and ABI/decoration mutations
reject with cleared facts; scalar/producer modules cannot prove one another.
The L2 contract validates exact20-byte layout/range/scopes, one dispatch and
compute-to-indirect dependency intent, with explicit four-backend fail-closed
conclusions. Proof/observation allocation0 and unaligned binaries pass.
Slang publishes SPIR-V/MSL/WGSL/reflection and single-entry YAML, with explicit
matrix layout; spirv-val Vulkan1.3 passes. This does not prove GPU bytes/order.

l3b1 evidence: ordinary-indirect-producer-pipeline-report.yaml. Status: Contract
Complete / Prototype Complete; scoped Backend Validated only for actual cold
private Metal Compute pipeline creation/queried ABI. The root retains six child
owners and original native library/function/pipeline/device control blocks.
Queried slot0/ReadWrite1/UInt33/alignment4/scalarSpan4 matches the separately
proved store footprint20 and workgroup1. Native identity/control-block aliases,
changed/extra ABI facts, stage/mapping/layout/quota/backend negatives, measured
cold factory peak4424 exact/one-less, exhausted-quota validation allocation0 and
original owner release/final quota0 pass. Shared writable uint32 ABI validation
preserves the earlier Fragment atomic diagnostics.47 targets,65 regressions,
fast contracts/Null hidden-frame/native cold smoke and32 SDK syntax checks pass.
The compiler helper's provenance change requires rebuilding the bindless package
runtime verifier; final regression target list includes that dependency.
No output, dispatch, recording/generation lease or native indirect consumer is
issued here. Continue l3b2 with protected output/schema/fetch/instance ABI,
exclusive recording, actual dependency/execution and completion evidence.
RenderDoc is absent; independent visual/platform/Production/full-plan gates
remain open. Reports/logs/XML/capture diagnostic are in the same artifact folder.

### ProtectedIndirectOutputPreparation/1 (M4e5b3c4l3b2a)

Cold preparation binds the selected ComputeFixedIndirectProducer/1 pipeline's
proven five words to the exact immutable index root and every declared vertex
and instance stream. `GpuOutputExpectation` is a numeric projection using the
same signed-base, instance-overflow, no-op and fetch-range analysis as immutable
host arguments. It is distinct from `ImmutableHostUpload`; raw GPU input still
fails with GPU-PROOF-REQUIRED. Numeric projection never certifies existing GPU
bytes or authorizes host argument upload.

Each stream must retain the actual GpuBuffer wrapper and its exact prepared
binding under the same quota, provider, issuing registry and native device.
Host, GPU shader and transfer writes must already be sealed; preparation does
not seal caller resources. Missing/duplicate/foreign streams, mismatched wrapper
and native payload identity, changed extent/usage/state/owners and any active
record fetch overflow fail before output creation. No-op words skip fetch
arithmetic but do not waive source identity, ownership or seals.

The output is a dedicated exactly 20-byte StorageBuffer|IndirectBuffer,
CpuToGpu, ShaderWrite buffer. Its initial contents are all-zero suppressed
arguments, not the expected producer words. Cold upload uses explicit Shared
tracked native storage, then permanently seals all ordinary writes. No dummy
output, raw descriptor write, or host overwrite is permitted. The selected
producer/root, exact native storage and all original wrappers/allocation tokens
are retained in a complete bounded owner projection. Explicit opt-in budget
bounds output bytes, expectation records/bytes, stream roots and child owners;
CPU allocations use the existing same-quota prepared budget and GPU creation
uses the device allocation ledger. Validation allocates nothing.

Lowering assessment: Null is Equivalent validator-only with no GPU payload;
Vulkan and WebGPU are DiagnosticFailure (payload missing); the private opted-in
Metal cold preparation is Prototype, subject to real storage/identity tests.
Public GPU producer/CommandRetention capabilities remain unavailable on all
GPU backends. This preparation grants no generation identity, writer/recording
lease, instance ABI proof, dispatch, dependency, indirect draw, completion or
visual authority. Native execution must fail closed until those prerequisites
are separately implemented and validated. Status: Contract Complete for this
cold prerequisite; backend/platform/production status requires separate evidence.

`CanonicalSharedBufferOwner/1` makes retained wrapper ownership verifiable:
GpuBuffer inherits reusable `core::SharedObjectIdentity`, whose weak canonical
owner is issued by `core::shareUnique` or `core::makeShared`. The core factories
bind the identity explicitly, including the existing aliasing UniquePtr holder;
`enable_shared_from_this` cannot observe ownership created by alias constructors.
The same pointer in an unrelated alias control block is rejected. This adds
no allocation; unique-only wrappers have no shared owner. All four backends
share the same C++ wrapper rule; native payloads/capabilities are unchanged.

### ProtectedIndirectOutputRecordingLease/1 prerequisite (M4e5b3c4l3b2b1)

Ownership admission is distinct from writer/consumer execution. A cold output
root has one immutable, non-recycling generation sourced from its issued buffer
ID and exactly one atomic recording reservation. A lease can only be acquired
for a factory-issued real native command whose canonical shared control block,
recording identity, registry and CPU quota match. Caller-authored integer IDs,
raw constructed command wrappers, empty/finished/abandoned/poisoned/submitted
commands, foreign roots and a second concurrent lease are rejected.

Acquisition and revalidation allocate nothing. The move-only lease holds the
output root and a weak canonical command observation, avoiding a command/lease
ownership cycle. Every record validates the exact root generation and live
original command. Destruction or overwrite releases only its own atomic unit;
moving leaves the source empty. Cancellation may release an unsubmitted lease.
An external lease does not automatically disappear when its observed command
is cancelled: it becomes invalid for recording and remains exclusive until its
owner releases it. Native submission must not consume this prerequisite until
the approved execution path transfers the lease to real completion ownership;
CPU completion flags alone cannot authorize reuse.

Four-backend public lowering remains Null validator-only, Vulkan/Metal/WebGPU
DiagnosticFailure. The private Metal ownership prerequisite encodes no work and
grants no selected consumer firstInstance ABI, dispatch/dependency/draw or
completion authority. Its execution gate is preparation-only. Status: Contract
Complete for ownership semantics only; b2b remains open until selected ABI
proof, and b2c remains open until completion transfer/execution evidence.

### Selected consumer instance ABI (b2b2 acceptance)

The following acceptance requirements are validated for the restricted selected
ABI; broader producer/dependency/completion execution remains open. The current
fixture `vertex_stream_instancing.vertexMain` has only location inputs: float2
position0, float2 offset1, float4 color2. Retained generated MSL consumes one
stage-in struct and has no instance/base-instance builtin parameter. Native
creation in `metal_native_pipelines.mm` explicitly writes the lowered physical
slot, stride, attribute formats/offsets, PerInstance step function and stepRate1.
These facts and existing host firstInstance1/2 output controls are audit evidence,
not a generic instance ABI authorization.

The intended restricted proof must bind the actual selected vertex SPIR-V entry
and all input IDs/location/type declarations to the retained artifact interface.
Reject builtin input, alias/group decoration, duplicate or absent locations,
unknown storage/type/interface mapping and unsupported additional entry inputs
with cleared proof facts. VertexIndex/InstanceIndex/BaseInstance/BaseVertex/
DrawIndex paths need independent semantics and remain closed. Reuse existing
SPIR-V framing/instruction and single-entry interface validators, and extend a
shared parser if required; no source-text guessing or backend-name inference.

Native approval must consume the exact lowerer's descriptor facts queried from
the actual descriptor used for pipeline creation, independently bind rate/stride/
stepRate/physical slot/attributes to the provider and owner observation, and
reject every mismatch before output lease admission or native encoding. Add
hostile binary/metadata/native-fact mutations, allocation0 revalidation, quick
contract and Null smoke, then actual selected firstInstance output controls.
Null remains validator-only, Vulkan/WebGPU remain explicit payload-missing,
and private Metal execution remains closed until this proof and b2c order/
journal/budget/completion transfer pass. Public capability/version stays0.

### InstanceAttributeFetch/1 and VertexLocationFloatInput/1

L2 firstInstance denotes the first element ordinal of each Instance-rate stream;
instance j fetches firstInstance+j with stepRate1. Vertex-rate streams continue
using their proven signed index/base-vertex range. This private ABI prerequisite
requires an explicit version1 opt-in and LocationFloatOnly domain. It grants no
GPU producer, ordering, dependency, generation lease or execution payload.

Selected vertex proof reuses SpirvInstructionView's framing, unique-definition
and direct-decoration resolver. Its acceptance language has exactly one Vertex
entry/function/label/return, Logical GLSL450 Shader capability, float32 scalars/
vectors, Input/Output pointers, direct location input variables and exactly one
Position float4 output. Input declarations/IDs/types/locations must match the
retained single-entry reflection, with complete entry interface membership.
Only direct loads, output stores, float addition and float composite construction
are admitted in the straight-line body. Builtin inputs (including InstanceIndex,
VertexIndex, BaseInstance, BaseVertex and DrawIndex), resources, pointer aliases,
group/member decoration, branches/calls/unknown instructions and duplicate or
missing IDs/interfaces/decorations reject with cleared proof facts. This is an
interface proof, not a proof of visual shader behavior; broader builtin and
shader languages require separate contracts rather than guessed compatibility.

The approved Metal native mapping is PerVertex1/PerInstance1 according to each
L2 stream rate. Shader inputs, vertex attribute formats/offsets/physical slots,
strides and step rates must be queried from the actual MTLFunction and
MTLVertexDescriptor used to create the selected pipeline, then bound to its
provider and immutable owner projection. Unknown/missing/mismatched facts fail
closed. These are descriptor-creation observations, not fictitious driver
reflection of pipeline properties that Metal cannot query after creation.

Four-backend current public outcome: Null Equivalent validator-only; Vulkan,
Metal and WebGPU DiagnosticFailure (execution payload absent). Native mappings
when approved: Vulkan INSTANCE input rate + draw firstInstance; Metal
PerInstance/stepRate1 + native baseInstance; WebGPU instance step mode requires
queried indirect-first-instance support for nonzero indirect firstInstance and
must reject without that feature. No public capability is inferred from a
backend/OS name or enabled by this cold prerequisite. Contract Complete for
these restricted semantics; native/ABI evidence and execution remain separate.

b2b2 evidence: Contract Complete / Prototype Complete; scoped Backend Validated
for cold private Metal selected function/descriptor ABI and immutable host
firstInstance controls. Actual queried locations0/1/2 are Float2/Float2/Float4,
all active. Captured streams3->0 stride8 PerVertex/stepRate1 and8->1 stride24
PerInstance/stepRate1 match the retained provider. Complete consumer projection15,
cold CPU peak836 exact/one-less, exhausted-quota allocation0, native-fact/owner/
intent mutations and original function/provider retirement pass. Reordered
equivalent facts invalidate an existing captured root; a new cold root may
capture that order. Native firstInstance0/1/2 immutable-host controls give exact
clear/red/green RGBA.50 targets,68 regressions and37 unique SDK syntax checks
pass. Evidence: ordinary-instance-consumer-report.yaml and corresponding logs/
XML in build/mac-metal-debug/artifacts/bindless-prepared-graphics. No GPU producer
execution or completion transfer is authorized. RenderDoc discovery fails with
RHI-RENDERDOC-TOOL-UNAVAILABLE; numerical texture readback does not close visual,
Platform Validated, Production Ready or full bindless gates. Continue b2c.

### GpuIndirectOutputSequence/1 (b2c1)

The restricted recording domain admits exactly one fixed producer dispatch and
one native indexed-indirect record. It composes GraphicsStorageWriteSequence/1
with IndirectRecordingBudgetState/1; it adds no resource-state cache, parser,
allocator or completion primitive. The value identity contains the exact issued
resource, immutable output generation and factory-issued recording ID. Backend
validation must also bind canonical owners, registry, device and quota; equality
of caller-authored IDs is structural validation only and grants no authority.

The ordered command units are producer encoder begin, dispatch(1,1,1), producer
encoder end, exact Compute ShaderWrite -> DrawIndirect IndirectCommandRead
dependency, consumer render-pass begin, optional exact sealed stream bindings,
one indexed-indirect record, consumer render-pass end. Producer commands reserve
four units; the remaining explicit maxCommands budget is consumed by the existing
indirect recorder with maxCalls=maxRecords=1. The minimum is seven units, or nine
for the current two-stream fixture. No-op output still requires the same producer,
dependency, identities and finite record accounting. Every rejection preserves
the original sequence and counters; native encoding may occur only after complete
owner/state/ABI/order/budget admission. Finish requires both composed sequences
complete, including the producer dispatch and indirect record.

Resource state belongs to the existing recorded resource MutationJournal.
Before GPU submission, cancellation or poisoned/native encoder failure must
discard encoded work, rollback ShaderRead to original ShaderWrite and release
only this command's reservation/owners. A fresh command can then retry the same
unsubmitted output generation. Successful submission commits the journal and
consumes the generation permanently; this version provides no reset, second
producer, overwrite or resubmission of the submitted generation. Subsequent work
requires a fresh issued output. Queue rejection before commit follows rollback;
failure after submission retains ownership until the real native completion
callback, records the failure diagnostic and never reopens the generation.

The move-only exclusive lease must transfer into preallocated native submission
retention before commit. Retained and dropped command wrappers must preserve the
original output, producer, selected consumer, index, all stream wrappers/native
allocations, pass/binding roots and encoder slots until real completion. CPU flags,
host waits/no-ops, state changes, finished encoders or a destroyed command are not
completion evidence. Failed preflight/admission must retain no partial new owner
union; failures after native mutation poison and discard rather than replay.
All cold metadata uses the same core quota; record/finish allocate nothing and
perform no IO, compilation, pipeline creation or blocking wait.

Four-backend lowering is explicit: Null Equivalent validator-only; Vulkan,
Metal and WebGPU public DiagnosticFailure with sequence-payload-missing. The
private opted-in Metal target is Prototype and must consume this exact sequence,
tracked Shared output storage, real Compute encoder and dispatch, ended-encoder
ordering, exact resource barrier and native indexed-indirect render encoder.
Tracked resource hazard ordering is an SDK contract, never inferred from an OS
or backend name. Private native execution remains closed until b2c2/b2c3 prove
the complete root, journal, command budget, transfer and real completion payload.
Vulkan/WebGPU native payloads remain absent and cannot execute this prerequisite.
No CpuReplay, cached-count, host-upload expectation or dummy output substitution
is permitted. Public capability/version remains0. This value sequence cannot
close Backend Validated, Platform Validated, Production Ready or full-plan gates.

### ProtectedIndirectExecutionPreparation/1 (b2c2 acceptance)

Cold execution preparation requires a dedicated private factory-issued command
with explicit GpuIndirectOutputRecordingBudget/1 and CommandRetentionBudget/1,
same-quota preallocated journal, compute/render encoder owner slots and existing
submission/completion storage. All generic record and finish paths remain closed
for this preparation-only command until approved b2c3 execution exists. Foreign,
raw, already-used or closed command payloads cannot be upgraded by preparation.

The preparation root retains the protected output, proven selected instance
consumer, exact prepared graphics binding/pass, journal and encoder slots. The
output's immutable index and every sealed stream remain transitively retained.
Every root must use its canonical factory ownership, same provider, registry,
device and CPU quota. Pass and binding roots gain the same reusable canonical
shared identity as the existing provider/output roots. The exact exclusive lease
is acquired transactionally and held by the root; any failed preparation releases
only that lease without changing command retention or journal. The command is
observed weakly, preventing a future command/root/completion cycle. Completion
storage is checked through its original command, never retained by this root.

Cold capture has an explicit maximum child-owner projection size, then uses the
existing retention preflight to check the deduplicated complete root/child union
against the command owner budget. It must not publish a partially admitted union.
Validation rechecks input observations, native identities, command freshness,
same journal/slots, lease and whole union without allocation. This root issues
no dispatch, dependency, draw, state mutation, submission or completion ticket.
Null remains validator-only; Vulkan/WebGPU/public Metal remain payload-missing.
Status requires actual cold tests; b2c3 and all public readiness gates stay open.

b2c2 cold evidence: Contract Complete / Prototype Complete; scoped Backend
Validated for actual private Metal command payload/metadata and root admission
only. Projection168, deduplicated union59, command CPU peak12976 and root peak6464,
exact/one-less child/union/command/CPU ceilings and allocation0 validation pass.
Canonical pass/binding/provider/output/command roots, foreign/raw/alias/quota/
issuer/second-lease negatives, weak command cancellation/destruction, original
pass/binding/consumer/native descriptor retirement and final quota0 pass. Child
projection ceiling is checked before allocation/capture/lease. Preparation admits
no command-retention owners and every native record/finish/submit entry remains
PREPARATION-ONLY.50 final build targets,68 regressions,4 focused quick/Null/native
tests and38 unique SDK syntax checks pass. Evidence is
ordinary-indirect-execution-preparation-report.yaml plus logs/XML in the bindless
artifact folder. No journal mutation, GPU execution, native completion transfer,
RenderDoc, Platform Validated, Production Ready or full-plan closure is claimed.

Native b2c3 implementation audit: use the existing retention projection admission,
prepared render encoder slot, ordinary fixed graphics/descriptor/push validation
and private immutable indexed-indirect native lowering. The new compute slot and
journal are already cold allocated. Admit the complete execution root once before
any native encoder creation, then consume the approved producer/end/dependency/
consumer sequence. Generic raw buffer, descriptor, host indirect and dispatch
paths remain unable to bypass the protected root. State mutation must use the
existing prepareMetalRhiRecordedState/setMetalRhiBufferStateForBarrier journal.

SubmissionRetention and its existing MTLCommandBuffer completion closure are the
ownership transfer point. The completion closure must release the exclusive lease
explicitly even if a caller still retains the preparation root, then retire the
original owner union; root destruction alone is insufficient completion evidence.
The root must never retain preparedCompletionState (an alias of SubmissionRetention)
because that would create a root/completion cycle. The submitting owner thread
must also retain the original root through native commit and journal commit, as
the current ordinary-write path does, so a fast completion cannot destroy an
enlisted buffer while journal commit is detaching it. These are pending native
implementation/validation requirements, not delivered b2c3 execution behavior.

### ProtectedIndirectExecution/1 (b2c3 native acceptance)

The private opted-in recorder accepts one canonical cold execution root and the
exact producer scope, dependency and indirect consumer scope from its selected
GpuIndirectProducerContract. One call admits the complete sequence and owner
union before native mutation, then encodes the real Compute pipeline/buffer,
dispatch(1,1,1), encoder end, tracked-buffer dependency, prepared render pass,
all sealed streams, native indexed-indirect record and pass end. The exact
selected binary proves all five GPU-written words; numeric expectation is used
only for preflight fetch bounds and never uploaded or replayed as commands.
Admission failure leaves command counters/owners/journal/native encoding intact.
Failure after admission or native mutation poisons/discards the entire command,
rolls back state and releases its unsubmitted reservation; it cannot retry the
same native command buffer. A fresh command/root may retry an unsubmitted output.

GpuIndirectOutputLifetimeContract/1 defines Cold, Recorded, Submitted, Completed,
Failed and Abandoned. Cold may only become Recorded; Recorded may become
Submitted, Failed or Abandoned; Submitted may become Completed or Failed only
with proven native completion. Terminal states cannot reopen. This structural
validator issues no execution/completion ticket. Only the private native
recorder/submission/completion owns root transitions; changing an unrelated CPU
completion flag cannot release the lease. Finish consumes the actual complete
sequence and recorded ShaderRead state. Generic raw record paths remain closed.
Submission consumes the frozen recorded owner projection and real command payload;
native observation replacement after encoding cannot release original objects.
Only pre-submit state/seal/lifecycle changes reject submission; no rebinding from
mutable native fields occurs. The output's generation becomes permanently
consumed at native commit and cannot be reopened by resetting ResourceState.

Before commit, move the exact move-only lease from root to preallocated
SubmissionRetention. The real completion handler explicitly releases that lease,
publishes success/failure and retires the original owner projection even when a
caller still retains the root. No root owns completion storage. Keep a submitting
thread root reference through command commit/journal commit to avoid fast
completion destroying enlisted buffers. Pre-commit rejection rolls back and
releases the unsubmitted lease; failures after commit never permit generation
retry and retain owners until real completion. Event-delayed retained/dropped
commands and caller-retained/dropped roots must prove all lifetime combinations.
Submit admission rejection after recording is terminal even before native
commit: unfinished commands, changed quota, foreign/missing event payload and
missing completion storage poison/discard the recorded stream, restore the
resource journal, publish Failed and release the reservation. A new command/root
can retry the unchanged, unconsumed generation; the rejected command cannot.

Null remains Equivalent validator-only. Vulkan/WebGPU/public Metal remain
DiagnosticFailure without execution payloads. Private Metal uses SDK-guaranteed
hazard tracking on the verified tracked Shared output across ended Compute and
Render encoders; no CPU replay/count readback/dummy fence or host signal
substitution exists. The selected active and zero-index/zero-instance literal
producer fixtures require generated Slang SPIR-V/MSL/WGSL/reflection, fast proof
tests and Null smoke. GPU-written output readback is acceptance evidence only,
after real completion; it never determines production draw submission.

b2c3 acceptance: Contract Complete / Prototype Complete; scoped Backend
Validated for this actual private selected Metal sequence. Twenty-four active
and extreme zero-index/zero-instance controls across Uint16/Uint32 and command/
root lifetimes write all five GPU words and return exact green or clear RGBA.
Four cancellation and four replaced-original owner retirement combinations,
five early submit rejection/retry controls and real wait-owner allocation
failure pass. Record/finish/no-wait submit allocate zero core objects at
exhausted quota. Projection168/union59, command/root/output cold peaks
13192/6464/4512. 51 targets, 69 regressions, four focused tests, 39 unique SDK
syntax checks and two new spirv-val modules pass. Evidence is
ordinary-indirect-execution-report.yaml with logs/XML/capture diagnostic in
build/mac-metal-debug/artifacts/bindless-prepared-graphics/. RenderDoc tooling
is absent; exact offscreen pixel data does not establish capture/visual,
Platform Validated, Production Ready or full-plan acceptance. Continue b2d
fixed-producer boundary coverage; public versions remain zero.

### Fixed producer boundary coverage (b2d)

This phase changes no L2 producer language or backend capability. Each selected
binary still proves five ordered unconditional literal stores, one invocation
and one exact20-byte output. The accepted fixed words must independently pass
the existing immutable index and every Vertex/Instance fetch bound. Test a
positive signed baseVertex with correspondingly authored immutable indices and
firstInstance zero with a single instance, in both index formats and all delayed
command/root lifetime combinations. Exact green/red pixels and all GPU words
must match after native completion. Proven index-count and instance-count
overruns must fail preparation before a protected output resource is created;
successful binary proof does not authorize unsafe fetch. The public four-backend
conclusions remain Null validator-only and GPU DiagnosticFailure. Dynamic
producers and mutable fetch inputs remain unsupported with deterministic
diagnostics, and require their own range proof/lowering before implementation.

b2d acceptance: Contract Complete / Prototype Complete; scoped Backend Validated
for these selected private Metal boundaries. Sixteen additional active cases
cover both formats and all delayed command/root lifetimes; positive base+1
words3/1/1/1/2 yield exact green, and base-2/firstInstance0 with the explicit
immutable instance binding offset yields exact red. All GPU words match.
Four selected native producer pipelines with valid binary proof but unsafe
index/instance counts reject output preparation with
CORE-UNSIGNED-SPAN-RANGE-INVALID / RHI-VERTEX-INPUT-FETCH-RANGE. Neither GPU output
allocation nor net CPU quota grows; all case quotas return to zero. 51 targets,
69 regressions, three focused proof/native/Null tests, three changed test SDK
syntax checks and four new spirv-val modules pass. The preceding unchanged
runtime39 syntax evidence remains valid. Evidence:
ordinary-indirect-boundaries-report.yaml and corresponding logs/XML/capture
diagnostic. Public/capture/platform/Production/full-plan gates remain open.

### ZeroBaseIndirectSourceConsumption/1 (l3c1)

The source-reading canonical producer requires a separate domain from five
literal stores. This cold value contract snapshots the complete explicitly
immutable uint32 source, normalizes its declared byte order through core's
unsigned-element copy, selects exactly one dense20-byte indexed record at
sourceWordOffset, and retains exact ZeroBaseIndirectProducerParams/1 and
BindlessIndirectInstanceConsumer/1 values. The parameter reserved word is zero,
both versions are1, capacity is nonzero and equal to the consumer capacity,
and consumer recordIndex is0. Unknown origin/order, partial words, missing
source, count/stride/offset mismatch and absent budgets fail deterministically.

Use the shared indexed-record schema parser and IndirectInstanceBaseLowering
to derive numeric physical/mapping intent. Physical firstInstance is always0.
Valid instance ranges yield the complete original logical range mapping;
invalid ranges explicitly yield FaultSuppressed, physical instanceCount0 and
all-zero mapping. FaultSuppressed predicts the canonical producer fault latch;
it never reports Clean or substitutes execution. Physical active index/fetch
bounds reuse IndirectArgumentConsumption/1 and immutable index/stream contracts.
Suppressed or zero-count records perform no fetch; the complete selected words
still remain owned and observable. A changed consumer tuple cannot consume the
prepared source. No bytes/facts are published after preparation rejection.

Budgets bound the entire source snapshot plus the separately retained physical
expectation bytes/observation; actual object/container memory must be charged
to the caller's core CPU quota. Observation/mapping validation allocate zero.
No file/IO, shader compilation, native allocation, source readback, execution,
generation lease or caller-authored proof is created by this contract.

| Backend | New immutable source-domain conclusion | Required payload |
| --- | --- | --- |
| Null | Equivalent validator-only, no execution | Cold schema/range/correspondence/budget validation |
| Vulkan | DiagnosticFailure | Canonical selected binary/source seal, descriptors, barriers and mapping consumer proof |
| Metal | DiagnosticFailure | Canonical selected binary/source seal, queried buffer ABI, producer/mapping consumer and native completion |
| WebGPU | DiagnosticFailure for this new source-proof domain | Bind new immutable source/geometry proof to the existing separately validated canonical processor |

This does not rename or disable the existing WebGPU processor contract, nor
promote its historical selected consumer evidence into this new immutable
source/geometry domain. All public extension versions remain unchanged.
The next prerequisite proves the actual canonical source-reading binary and
exact retained source/native ownership before any execution integration.

l3c1 acceptance: Contract Complete / Prototype Complete for cold immutable
source value intent only. Full32-byte source normalization, exact selected
offset, logical/physical/mapping facts, explicit fault suppression, physical
fetch bounds, source-shape/write seals, hostile origin/schema/budgets and changed
consumer/parameter tuples pass. Actual cold quota348 bytes exact/one-less,
observation allocation0 and final quota0 pass. 52 targets,70 regressions,three
focused quick/Null checks and40 unique SDK syntax checks pass; evidence is
zero-base-source-contract-report.yaml plus logs/XML/capture diagnostic. No
Backend Validated execution, selected canonical package/native source-byte
identity/consumer proof, visual/platform/Production/full-plan authority follows.

The next canonical package prerequisite must reuse BindlessShaderPackage's
compiler-owned ZeroBaseIndirectProducer/1 source and complete package digest,
including actual compiled target bytes. The package loader rejects rehashed
caller substitution. A loaded fixed producer or arbitrary Compute package is
insufficient. Selected entry, stages, canonical descriptor roles/buffer ABI and
the immutable source's parameters/consumer tuple must agree. This validates
the approved algorithm against cold numeric intent, with no native producer,
source upload, generation or mapping-consumer execution authority.


### CanonicalZeroBaseIndirectSource/1 (l3c2)

BindlessShaderPackage is the only construction provenance: its loader verifies
the compiler-owned full source/package fingerprints and every retained target.
The selected entry is arpZeroBaseIndirectMain, one Compute module/workgroup1,
no push constants and six exact reflected descriptor roles. Metadata is a
ReadOnlyStorageBuffer with wordCount-derived span; latch is writable4 bytes;
normative table IDs1/2/3/4 select full immutable source/physical20/mapping16/
params16. Capacity1, Load|Query/Store/Store/Load and scalar uint32/uint4 ABI
are revalidated. Source facts, exact params and consumer tuple are copied only
after all checks; any diagnostic publishes empty facts. No allocation or
native handle, pipeline, dispatch, fault-domain or consumer authorization occurs.

l3c2 status: Contract Complete / Prototype Complete only. Native/Emulated
actual packages and spirv-val pass; all target/rehashed substitutions remain
rejected by compiler-owned provenance. Hostile role/layout/entry/reflection
mutations and exhausted/null PMR observation allocation0 pass. 52 targets/70
regressions, quick/Null3 and changed syntax2 pass. See
canonical-zero-base-source-report.yaml, retained canonical-zero-base-packages
and corresponding logs/XML. All four new-domain execution gates remain closed.

### Native immutable source preparation prerequisite (l3c3)

Before native ABI/pipeline integration, a private cold preparation owns the
loaded canonical package, immutable native index proof, full normalized source
and source/params uploads. Reuse the sealed-buffer upload algorithm with exact
StorageBuffer/UniformBuffer usage and ShaderRead state. Host/GPU/transfer writes
are sealed after successful native upload. Revalidation compares registry
issuer plus native device/object/allocation control-block identities and owner
projection; size equality alone is insufficient. Every byte and child owner
has an explicit budget and CPU allocations use the index owner's quota.
Failures destroy all partial resources and restore charges. Validation allocates
zero and never reads back. No command encoder, generation lease, dispatch or
mapping consumer authority follows. Null remains a validator, Vulkan/WebGPU
reject this native preparation, and Metal execution remains DiagnosticFailure
until all ABI/packet/fault/consumer/completion prerequisites are implemented.


l3c3 status: Contract Complete / Prototype Complete / Backend Validated for
private immutable source/params uploads only. Eight actual Native/Emulated,
Little/Big and Produced/FaultSuppressed cases pass; full native32+16 bytes,
33 child owners, cold CPU peak3924 bytes, exact/one-less quotas/GPU budgets/
owner budgets, seals and hostile identities/issuers/states/registry pass.
Child owner count derives from the independent buffer's four mandatory owners
and refuses before native/registry allocation. Registry is a shared cold
issuer object; the CPU factory probe releases the accepted root before
measuring a retry, avoiding attribution of issuer bucket growth to the root.
Observation under exhausted/null PMR allocates0; final quota0.53 targets/71
regressions, quick/Null/native3 and changed syntax4 pass; evidence
canonical-zero-base-source-native-report.yaml. Native dispatch/consumer,
public/four-backend execution, capture/platform/Production remain open.

### Canonical producer native ABI prerequisite (l3c4)

Only the explicitly selected compiler-owned Emulated package has six active
scalar Metal mappings. The canonical Native package's retained target marks
four resource-array roles inactive with no executable array payload: native
preparation must reject it, without selecting or compiling the other package.
Reuse authorized package-to-entry/layout conversion and native Compute
compilation, with actual pipeline-reflected buffer facts. Each of the six
roles must have a unique active Compute slot, exact type and scalar access/
element/parameter footprint. Retain and revalidate device/library/function/
pipeline control-block identities and the source/index ownership. Bounded
cold allocations use the same quota. This is an ABI/pipeline prerequisite,
not fault-domain/metadata/output lease/dispatch/consumer authorization.
All four public new-domain execution conclusions stay unchanged.

Canonical Metal scalar ABI lowering is explicit: Slang emits device uint* for
all five structured buffers, including L2 read-only metadata/source, and the
real Metal reflection reports ReadWrite/UInt/alignment4/minimum4. The exact
compiler-owned algorithm performs no stores to read-only roles, and source
writes stay sealed. Uniform params are constant struct uint4 with native
ReadOnly/Struct/alignment16/minimum16. This difference in native pointer ABI
does not authorize writable L2 descriptors, a different shader, or dispatch.


l3c4 status: Contract Complete / Prototype Complete / Backend Validated for
private selected scalar pipeline/ABI only. Four real Emulated pipelines and
four Native inactive-array refusals pass. Exact six ABI facts, 39 child owners,
cold CPU peak7330, exact/one-less quota/owner bounds, changed ABI/mappings/
workgroup/native function owners and observation allocation0 pass. Final quota0.
53 targets/71 regressions, quick/Null/native3 and changed syntax3 pass; see
canonical-zero-base-pipeline-report.yaml and native evidence/XML. No packet,
source-reading dispatch, mapping consumer, public/capture/platform/Production
authority follows.

### Existing processor intent correspondence prerequisite (l3c5)

Reuse BindlessIndirectProcessorIntent's complete authorization, snapshot/plan/
generation and protected packet validation. Cold correspondence additionally
requires the exact canonical package/control block and an independently
proven immutable source owner, full source extent/state/seals, exact packet
ZeroBaseIndirectProducerParams and consumer authorization's mapping tuple.
The expected source owner is supplied by a trusted preparation boundary; this
check cannot prove bytes supplied by a caller. The native source root supplies
its own uploaded source owner, while Null proves only validator correspondence.
A failure publishes no facts. This grants no native packet, GPU completion,
recording or mapping-consumer authority; Null packets never become GPU payloads.

The processor chain requires core SharedObjectIdentity for package, logical
source, packet, consumer snapshot, authorization and intent. Preparation and
recording-lease admission reject a shared alias with a different control block
before acquiring capacity. Authorization also rejects a non-owning package
alias. These ownership checks apply identically to Null/Vulkan/Metal/WebGPU;
they publish no capabilities and never turn Null ownership into native payload.
The packet's parameter native bytes/issuer remain a separate prerequisite:
the source root's private parameter upload cannot replace packet access IDs.


l3c5 status: Contract Complete / Prototype Complete for cold processor/packet
correspondence and canonical owner admission only. Actual Native/Emulated
packages and Null intent, full40-byte source, exact parameters/mapping/
generation, unsealed/alias/tuple/generation negatives and empty failure facts
pass. Core canonical identity covers packet/snapshot/authorization/intent;
lease aliases refuse before capacity.100 exhausted/null-PMR checks allocate0.
53 targets/71 regressions, four focused quick/Null/Metal/codegen and changed
syntax6 pass; retained packages/spirv-val and
zero-base-processor-correspondence-report.yaml record evidence. Native bridge
remains cold-only; native packet parameters/metadata/fault/scratch and mapping
consumer execution are unproven. Public/capture/platform/Production gates open.

The next admission audit covers other bindless pipeline/pool/record boundaries:
canonical authorization, snapshot and pipeline control blocks must be checked
before any reservation or native mutation. Reuse the common Compute/Graphics
binding owner validator, core identity and existing recording gates. All four
backends reject aliases deterministically; no capabilities change.


l3c6 status: Contract Complete / Prototype Complete for canonical owner
admission. Common pools reject pipeline/snapshot/authorization aliases before
reservations; prepared owner bundles and typed recording/pass scopes reject
aliases before native encoding. Fault domains share the canonical identity
base with snapshots, and fault lease/clear/readback admission rejects aliases
before capacity or blit mutation. Private retained Graphics package admission
also checks canonical ownership. Borrowed backend projections preserve their
borrowed contract after the typed public admission has checked ownership.
53 targets/71 final regressions, initial7 focused tests,20 portable/SDK syntax
paths and actual native Objective-C++ build pass. Real Metal Compute alias
probes leave native argument reuse and fault outcome consumption unchanged;
fault aliases leave blit creation unchanged. See canonical-owner-report.yaml
and final XML/logs. No new source-reading dispatch/packet/consumer/public
capability or Platform/Production/full-plan closure. RenderDoc remains absent.

The next storage audit found BindlessIndirectPacket::validate only checked
liveness and resource presence. Logical storage roles, exact extents/usages,
write seals and aggregate accounting need a reusable revalidation contract
before a native same-issuer adapter can rely on the packet. That contract
will not establish native byte correspondence or registry ownership.


### Logical protected packet storage prerequisite (l3c7)

BindlessIndirectPacketStorage/1 revalidates the existing packet's three ordered
roles: physical indexed args (20 bytes, Storage|Indirect|CopySrc|CopyDst),
instance mapping (16 bytes, Storage|CopySrc|CopyDst), and producer params
(16 bytes, Uniform|CopySrc|CopyDst). All have CpuToGpu memory intent; args and
mapping are initially ShaderWrite, params initially ShaderRead. These are
normative schema sizes, not inferred allocation sizes. Current resource state
is tracked separately by recording/journal transitions and is not reset or
inferred from initial state. Labels are not semantic identity.

Each role requires a distinct nonzero issued resource ID and host, writable
descriptor and transfer write seals. Each reported allocation covers its exact
logical extent, the overflow-safe sum fits the packet's byte reservation and
matches the recorded accounting. Packet count/byte leases must remain charged.
Parameters retain contractVersion1, reserved0 and a nonzero consumer capacity.
A failed check clears all facts; observation performs no allocation or readback.
The common pool creates storage from this same role schema and publishes only
a fully initialized, sealed packet. No new packet class or writer is added.

Four-backend lowering: Null provides logical validator storage with existing
count/byte/epoch semantics. Vulkan and Metal keep packet-storage capability0
and deterministic preparation rejection; logical validation grants no native
payload. WebGPU consumes the common logical contract and separately requires
its issuing registry, exact native extents/usages and sealed initialized
content under the existing packet-storage contract. This host has no browser
validation for that native path. The storage contract does not prove bytes,
registry/control-block ownership, source-reading dispatch, privileged writes,
generation terminality, mapping-consumer ABI or completion order. Same-sized
foreign storage may satisfy the value contract and still cannot replace the
packet's actual owners. Existing execution/capture gates remain unchanged.


l3c7 status: Contract Complete / Prototype Complete for logical packet storage.
Exact20/16/16 schema, distinct IDs, seals, charged reservations and overflow-safe
aggregate accounting revalidate. Hostile role/parameter/seal/underreporting/overflow
checks clear facts and preserve pool charges.100 exhausted/null-PMR observations
allocate0.53 targets/71 regressions, initial5 focused quick/Null/native checks
and3 SDK syntax paths pass. Vulkan's native device test target is absent in this
Metal configuration; its source syntax passes. See packet-storage-report.yaml
and XML/logs. Same-sized foreign logical storage deliberately grants no packet
ownership; no native packet/bytes/same-issuer/dispatch/mapping-consumer authority.
The typed native prepared-root audit remains a separate owner admission gate.


### Backend-private typed prepared-root admission (l3c8)

Canonical ownership is checked independently at direct native typed admission,
not only at the public Compute/Graphics wrapper. Metal prepared Compute and
Graphics roots use core SharedObjectIdentity and are issued by the existing
core factories. Compute revalidation checks both its native root and the typed
public owner before retention or native argument encoding. Graphics storage
revalidation checks its native root before scope/fault/render mutation. Aliased
control blocks carrying the same address are rejected deterministically with
RHI-METAL-PREPARED-COMPUTE-OWNER-MISMATCH or
RHI-METAL-PREPARED-GRAPHICS-OWNER-MISMATCH.

Generic native shared owner projections intentionally retain their existing
contract, including public-wrapper-owned function/stage aliases. This change
checks typed root admission and grants no authority to such projections. Null
keeps the already-tested public typed validator checks; Vulkan/WebGPU have no
private Metal typed root and preserve their prepared-binding execution gates.
No shader, capability version, packet writer or public execution path changes.


l3c8 status: Contract Complete / Prototype Complete; scoped Backend Validated
for actual private Metal typed root admission only. Direct native Compute
payload/public-owner aliases reject before argument reuse; Graphics root aliases
reject standalone validation and scope preparation before blit/render/fault
outcome mutation. Canonical positive encoding/submission/completion/retirement
still pass.100 quick identity observations allocate0.53 targets/71 regressions,
four focused quick/Null/native tests, two portable syntax paths and real native
Objective-C++ build pass. See native-prepared-owner-report.yaml and XML/logs.
No new packet/producer/mapping-consumer/public capability, capture, Platform,
Production or full-plan closure follows.

Next is same-issuer native packet issuance and exact packet-parameter bytes,
then canonical metadata/fault/scratch ownership. Metal public packet storage,
canonical mapping consumer and source-reading producer versions remain0; the
common pool therefore rejects before reservations. The private source root's
parameter upload and its Emulated scalar pipeline cannot substitute for the
packet's actual parameter ID, a valid consumer plan or device-issued execution
payload. This needs an explicit lowering/issuance contract and native evidence;
RenderDoc/public Graphics capture acceptance remains independently pending.


### Prototype packet storage-only cold issuance (l3c9a)

The existing public packet path requires a complete consumer lowering plan;
Metal's mapping consumer is unavailable. Cold preparation must not synthesize
that plan, copy Null ownership, mutate capability inputs or grant a privileged
writer. The existing BindlessIndirectPacket and its pool gain an explicit
PrototypeStorageOnly state. A backend-private opted-in factory reuses the same
count/byte reservations, role descriptors, initialization, epoch and seals.
Its consumerPlan remains unissued. validateStorage observes logical storage;
validate, consumer/intent/recording admission fail deterministically with
RHI-BINDLESS-INDIRECT-PACKET-PROTOTYPE-STORAGE-ONLY. There is no promotion or
unseal operation in this contract. Public preparation and capability versions
stay unchanged. Failed cold preparation releases all packet/root reservations.

Parameters use one shared little-endian uint32 serializer for both the packet
and immutable source root. Suppressed physical args and version-zero mapping
are all-zero bytes. Native Metal cold issuance consumes the canonical selected
scalar producer pipeline/source root, exact same issuing pool device/registry,
tracked Shared native buffers and complete control-block owner projection.
Every packet role has its own actual native object and allocation token; private
source parameters never replace the packet's distinct parameter ID. Explicit
CPU/GPU/pool/owner bounds and preflight apply before allocations. Revalidation
reads no native content; successful deterministic upload plus sealed original
owners establishes byte provenance, and real cold readback tests verify bytes.

Four-backend new cold-state lowering: Null remains logical validator-only;
Vulkan rejects this unimplemented private cold issuance; Metal provides an
explicit Prototype opt-in with actual queried native payload checks; WebGPU
rejects this new private cold issuance while preserving its independently
validated existing public packet path. All consumer/producer/draw execution
for this new cold state fails closed. Contract/source facts alone cannot grant
Backend/Platform/Production validation. Native metadata/fault/scratch, issued
consumer correspondence, dispatch/generation/dependency/completion follow.


l3c9a status: Contract Complete / Prototype Complete; scoped Backend Validated
for actual private Metal cold52-byte packet/initial content/seals/issuer/owners
only. Four Little/Big x Produced/FaultSuppressed cases, exact/one-less CPU/GPU/
pool/owner budgets, childOwners56 coldPeak4864 and100 allocation0 observations
pass.53 targets/71 regressions, focused quick/Null/native smoke and SDK syntax
pass. See cold-packet-report.yaml. Consumer plan remains unissued; consumer,
producer access and record lease refuse the explicit Prototype storage state.
RenderDoc unavailable (RHI-RENDERDOC-TOOL-UNAVAILABLE); no new visual acceptance,
public execution/capability, Platform/Production or full-plan closure. Continue
canonical metadata/fault/scratch correspondence l3c9b.

### Cold six-role producer resource correspondence (l3c9b)

Before native binding, the canonical Emulated pipeline and original same-issuer
PrototypeStorageOnly packet must revalidate. A private cold root retains these
roots and the existing BindlessOwnedFaultDomain. Its actual generation labels
prototype producer metadata only; it never stands for an issued consumer
generation, packet completion or writer ticket. The latch is awaiting an
explicit lease-bound clear; cold preparation claims no initialized latch or
fault result. Existing fault submission lease exclusivity is retained.

Six roles use queried scalar ABI slots and exact original native owners:
metadata, fault latch, immutable logical source, independent zeroed arguments
scratch20, independent zeroed mapping scratch16, original packet parameters16.
Scratch never aliases protected packet destinations. Metadata uses the shared
RHI builder and single-buffer resource source, deriving element counts from
actual resource IDs/extents and approved table strides. Metadata serialization
is little-endian. Fault readback4 is retained alongside latch4. All newly owned
bytes and complete child ownership are explicitly bounded before allocation;
reported native allocation footprints must fit the ceiling. CPU quota scope is
the same source/packet quota. Failure releases all new allocations. Metadata
and scratch are host/GPU/transfer sealed; fault domain retains its existing
latch/readback seal and initial state contract. All observations are read-only
and allocation-free; original owners survive payload mutation.

Null provides builder/value/lease validation only; Metal has explicitly opted-in
Prototype cold resources verified against native registry, tracked object and
complete control blocks. Vulkan/WebGPU reject this new private cold path with
RHI-CANONICAL-COLD-BINDINGS-PAYLOAD-MISSING; existing WebGPU public producer
metadata adopts the same RHI resource-source helper without changing consumer
generation semantics. No encoder creation, source-reading dispatch, privileged
write, consumer plan, dependency or completion authority is supplied here.
Retry requires valid unchanged roots and budgets after releasing failed cold
allocations. Capability versions remain0. This phase cannot close execution,
RenderDoc, Platform or Production gates.

### Mapping-consumer admission audit (l3c10a)

The existing pure attribute consumer cannot authorize the canonical mapping
consumer. VertexLocationFloatInput/1 explicitly forbids builtins, resources,
branches and helper calls; the generated mapping helper consumes SV_InstanceID,
reads uint4 mapping, checks version/capacity/range and reconstructs the logical
instance. Reusing that proof would discard the required mapping/fault semantics.
The existing WebGPU BindlessIndirectConsumer fixture is specialized to binding0,
record0, capacity2. The current immutable source proof is binding7, record0,
capacity3; both binding and capacity must match exactly. Source
validateConsumer already rejects either mismatch with
RHI-ZERO-BASE-SOURCE-CONSUMER-MAPPING. No capacity rewrite, fixture replacement,
implicit binding remap or Runtime array fallback is permitted.

The compiler/package loader proves descriptor names, stages, reflection and
retained artifact hashes. Its canonical closed-source and complete-package
fingerprint checks are currently specific to ZeroBaseIndirectProducer/1.
Generic consumer artifact metadata does not prove that an arbitrary vertex
entry calls or obeys the mapping helper. The next contract must establish a
closed, versioned consumer Slang source/specialization and selected package
provenance (SPIR-V/MSL/reflection, plus WGSL where applicable), or a precise
selected binary semantic proof using the existing instruction-view tools.
Matching declared descriptor names alone is insufficient.

Then establish actual native vertex uint4 mapping ABI, exact original packet
resource/native ownership, independent consumer metadata/fault domain, stage
mapping and concrete descriptor-table resource semantics. The prototype
producer fault generation from l3c9b cannot replace consumer snapshot generation
or authorization. A complete device-issued consumer plan and same-encoder
sequence/completion proof precede any public packet bridge; storage-only
Prototype packets remain non-promotable. Producer scratch may not be copied
into those packets by ordinary transfer or an unrelated fixed-literal writer.

Four-backend scope: Null retains existing tuple/intent validator only; Vulkan
and Metal have no approved new canonical consumer execution payload and must
reject; WebGPU keeps its existing scoped capacity2 native consumer path, which
is not proof for the new capacity3/binding7 specialization. Existing operation
capabilities and capture/public authorization gates stay unchanged. Audit
verification reuses the existing consumer-tuple negative unit path, Null
hidden-frame, compiler/package/source regressions and native cold resource
smoke. This audit establishes no native mapping-consumer execution or visual
acceptance. Continue l3c10b closed consumer provenance before native expansion.


l3c9b status: Contract Complete / Prototype Complete; scoped Backend Validated
for real private Metal cold six-role resources and native byte/ABI/owner proof
only. Four native endian/outcome cases, metadata96 scratch20+16 fault4+4
owned140,83 child owners, cold CPU peak9064, exact/one-less budgets and100
exhausted allocation0 observations pass.53 targets/71 regressions, focused4
quick/Null/native and4 SDK syntax paths pass (WebGPU host branch only). Native
owners and charged packet reservations survive pool destruction under the
original device issuer epoch; all final quota/owner retirement checks pass.
See cold-bindings-report.yaml and native log/XML. Fault awaits clear; no fault
result, source-reading dispatch, privileged copy, consumer plan or public
capability change. RHI-RENDERDOC-TOOL-UNAVAILABLE preserves visual/capture gates;
Platform/Production/full-plan remain open. l3c9 cold issuance/correspondence is
complete at this scoped Prototype level; l3c10a admission audit is recorded,
and closed mapping-consumer provenance l3c10b is the next implementation gate.

### Closed consumer provenance work in progress (l3c10b)

Status: contract draft. Contract Complete, Prototype Complete, Backend Validated,
Platform Validated and Production Ready are pending for this new consumer scope.

Implementation must extend the existing bindless compiler and compiler-owned
identity generation. A closed consumer specialization is described by the
explicit existing consumer tuple and resource-table authoring metadata, not by
backend names or a caller assertion. The selected vertex/fragment entries must
have retained compiled SPIR-V/MSL/reflection (and WGSL when supported), exact
source and complete-package fingerprints, and a selected source/entry proof.
No handwritten consumer artifact, source-name check or generic reflection-only
check may certify mapping semantics. Unsupported specializations fail before
native compilation/resource allocation. The compiler-owned allowlist and
specialization identity must be versioned and matched by the package loader.

The closed vertex algorithm must load record0 mapping from the declared uint4
resource, check exact instance capacity, contract version, firstInstance/range
and physical instance bound, then reconstruct the logical instance without
overflow. The fragment algorithm must preserve the mapping failure signal and
report the actual consumer fault latch; table accesses must use the reconstructed
logical instance and the existing metadata bounds/active-prefix guards. A
producer fault cannot be reported as Clean by a suppressed consumer. Tests must
reject rehashed altered mapping checks, helper omission, entry substitution,
wrong tuple/resource stage/role and missing consumer fault propagation. A new
binary proof, if required, extends the shared instruction-view utilities rather
than implementing a second parser in a backend or sample.

This provenance phase supplies no consumer snapshot issuance or native dispatch.
The actual native vertex mapping uint4 ABI, queried active stages/descriptor
slots, exact packet ownership, independent metadata/fault generation and
completion-owned leases remain later admission requirements. Null can validate
provenance/value contracts only. Vulkan/Metal/new WebGPU specialization retain
deterministic execution failure until their own tested payloads consume the
same approved contract. Existing WebGPU specialization stays independently
scoped. Quick package/tuple regressions and Null hidden-frame remain the
verification paths while this draft is completed.

#### ClosedImageMappingConsumer/1 selected compiler contract (l3c10b1)

The first explicitly supported specialization is set0, mapping binding7,
record0, instanceCapacity3, metadata binding1 and fault binding2. Logical
table1 Images is SampledTexture2D<float4>, capacity3, first binding3, Fragment
Load only. This finite tuple is compiler-owned version1; all other closed
specializations reject deterministically, with no caller rewrite or implicit
slot/capacity conversion. Generic existing consumer packages retain their
existing independent scope and cannot satisfy this new closed provenance.

The compiler emits both selected entries. The Vertex entry consumes location0
float2 position and SV_InstanceID, writes position=(x,y,0,1), invokes the existing
guarded mapping reader, and forwards logical instance plus a flat failure bit.
It uses no VertexID-derived geometry or inferred index offsets. The Fragment
entry reports the consumer latch on mapping failure and emits the explicit
magenta failure diagnostic; otherwise it invokes Images_load(logicalInstance,
int3(0,0,0),fault), which retains the existing active-prefix/metadata guards
and reports rejected indexed access. Texture extents/formats/native ownership
must be admitted by the later actual resource binding contract. The position
stream must satisfy the existing immutable index/vertex fetch contracts.
No matrix or push constant is present.

Only the compiler owns this source and exact ordered Vertex/Fragment entry
list. CompilerInput and TargetReflectionValidated artifacts carry the new
zeroBaseIndirectConsumerContractVersion1 alongside the existing consumer tuple;
this is shader provenance metadata, never a device capability. The build-time
identity generator emits exact source and complete compiled package digests
for Native and Emulated targets. No shader toolchain leaves empty executable
package identities, causing deterministic loader refusal. The runtime loader
requires both retained selected stages and complete compiler package identity
when that version is present; stripping it cannot pass the new closed contract.
All rehashed changes to source, mapping checks, stage entries, SPIR-V/MSL/WGSL,
reflection, tuple or consumer fault flow must fail provenance admission.

Native/Emulated Slang targets remain SPIR-V/MSL/reflection, with WGSL only for
the existing Emulated target contract. All use explicit row-major compilation.
Null validates package/value intent only. Vulkan, Metal and new WebGPU closed
consumer execution stay DiagnosticFailure until approved native ABI, resources,
consumer authorization and sequence/completion gates pass. Successful offline
compilation does not publish capability versions or provide a packet promotion.
This compiler phase is Contract/Prototype work only; no native or visual
acceptance follows, and RenderDoc remains required for later output validation.

The shared L2 planner includes this provenance version in the complete plan
identity and rejects unsupported versions or missing consumer ABI. While native
execution proof is absent, every non-Validator execution mode rejects the new
version with RHI-BINDLESS-ZERO-BASE-INDIRECT-CONSUMER-EXECUTION-UNAVAILABLE.
No generic consumer execution plan may authorize the new specialization merely
because its physical descriptor shape matches. Null snapshots reserve the
schema's52-byte ceiling; actual metadata44+latch4 and optional readback4 are
accounted according to the existing queried faultReadback contract, never
assuming Null has native readback.

### Native closed consumer pipeline query (l3c10b2)

The next cold native root must retain the actual compiler-owned consumer package
and original source root/control blocks under the same CPU quota; it issues no
consumer snapshot or packet plan. Reuse existing appendBindlessShaderEntry,
per-module MetalBindingSpace lowering, vertex stream reservation, complete L2
render-state validation, native pipeline creation, buffer/texture/vertex input
queries and function owner projection. The current public retained Graphics
builder validates the Native buffer-only domain, and lower prepared stage
requires Native. This closed image/mapping specialization cannot enter either
through a fixture exception or weakened generic admission. Its private versioned
Prototype factory may accept the proven Emulated scalar package after independent
closed value/provenance validation; Native array specialization remains a
separate queried ABI path and must fail deterministically if unimplemented.

Vertex mapping is StructuredBuffer<uint4>, independent from uint32 Compute
source and pure attribute consumer proofs. Its exact queried scalar vector
pointee/access/alignment/extent must be observed; do not infer it from nominal
buffer size or uint32 ABI. Fragment metadata/fault are independent scalar uint32
roles, and three images are actual queried Fragment texture2D<float> slots.
Each stage's mapping is module-local: slots may repeat across separate stage
functions but cannot collide within one stage/resource namespace. The Vertex
location0 Float2 descriptor, stream slot/rate/stride/offset and shader input
query must match the existing VertexInputConsumptionContract and native
reservation map. Exact authored render state and selected stage outputs must be
validated before native creation; no altered native default is allowed.

All package/source/device/library/function/pipeline/layout/interface/stage
owners and reflection facts are bounded and captured before publication, and
revalidated without allocation or native compilation. Changed function or
control block, missing/duplicate/altered ABI, inactive stage mapping or exhausted
quota rejects. No renderer, command, render pass, encoder, draw or dispatch is
created by this root. The existing four-backend new-domain execution gate stays
closed. Actual original packet mapping binding, texture/table metadata and
consumer fault generation, immutable fetch/draw correspondence, generation and
completion leases follow in distinct resource/execution stages; no cold packet
promotion. Fast closed-package negative tests and Null hidden-frame remain the
stage paths, alongside actual native query smoke when implemented.

ClosedConsumerPipelinePreparation/1 refines this draft for the first scalar
Emulated Metal path. It retains the selected consumer package's actual CPU quota
(identity captured by the shared package loader), the original source root,
module-local stage layouts, authored vertex input and the eight mandatory Render
owners (payload/device/two libraries/two functions/pipeline/depth-stencil). The
existing native factory always creates the depth-stencil object, including its
explicit disabled-state equivalent. Its actual owner is retained. Owner
ceilings are preflighted before native compilation; all cold allocations run
under the retained source quota. Native array packages fail with the stable
RHI-METAL-CLOSED-CONSUMER-SCALAR-PACKAGE-REQUIRED diagnostic, without conversion.

Native ABI values follow the Metal SDK: mapping uint4 pointee MTLDataTypeUInt4,
16-byte minimum extent and ReadWrite pointer ABI; metadata/fault uint pointee,
4-byte minimum extent and ReadWrite ABI. Alignment is the authoritative native
query value and is retained exactly; no element-size alignment inference is
allowed. This host reports4-byte binding alignment for uint4*. Closed compiler provenance preserves
read-only L2 mapping/metadata despite Slang's device pointer ABI. Images require
three distinct direct Fragment texture slots, texture2D/float/non-depth query
facts. No extra active stage resource or array is admitted. Buffer facts may
also expose exactly one actual authored Vertex descriptor stream, whose slot
is reserved. This first queried domain requires its readonly Struct ABI and
exact authored fetch extent; independent descriptor/input facts must satisfy
the existing vertex contract. Absent or extra facts fail deterministically. No inferred uint32-to-uint4 substitution is allowed.

All projected native render-state fields are captured by a reusable backend
observation, and exact native owners, stage layouts, buffer/texture facts and
vertex descriptors/inputs are rechecked without allocation. This root is only
cold preparation, with RHI-METAL-CLOSED-CONSUMER-PREPARATION-ONLY; Null remains
value validator, Vulkan/WebGPU private query remains DiagnosticFailure, Metal
requires explicit Prototype opt-in. Failure never issues public capabilities,
consumer snapshot/fault generation, packet plan, encoder or execution lease.

Status: Contract Complete / Prototype Complete; scoped Backend Validated for
actual private Metal scalar consumer pipeline query only. Eight source
mode/byte-order/fault prerequisites each pass with43 projected child owners,
cold CPU peak11580 and100 exhausted-quota allocation0 observations. Actual
Vertex mapping query is UInt4/ReadWrite/alignment4/minimum16, readonly stage-in
Struct/alignment4/minimum8, Fragment metadata/fault UInt/ReadWrite/alignment4/
minimum4 and three direct Fragment Texture2D/Float/non-depth slots. Exact and
one-less CPU/owner budgets, altered input/state/ABI/mapping/stage namespace/
control blocks and foreign quota reject.53 build targets/74 regressions and
focused6 quick/Null/native plus six SDK syntax paths pass. The new reusable
RenderPipelineStateCopy preserves nested core allocations even under a null
ambient PMR resource. Evidence closed-consumer-pipeline-report.yaml; no resource
generation/snapshot, consumer plan, encoder, draw or dispatch is issued.
RenderDoc unavailable, visualAcceptedfalse; Platform/Production/full-plan gates
remain open.

### Closed consumer resource prerequisites (l3c10b3a audit; resource contract draft)

The completed native query root is not a BindlessSnapshot. The actual Metal device has no public executable payload opt-in and the shared
planner rejects both compiled closed targets at its generic execution gate
before public snapshot leases, descriptors or GPU allocations. The separate
new-domain refusal was proved by the c10b1 negative planner contract; actual
cold private query facts cannot enable either public gate. Null can validate
logical metadata but cannot supply native generation/storage or convert a
PrototypeStorageOnly packet. The public pool/seal, ordinary Graphics provider
and public Native buffer-only builder therefore remain closed. A new cold
resource root must retain the original private consumer pipeline and original
Metal packet storage; their source roots, device, quota and shared control blocks
must match exactly. No replacement packet or consumer tuple rewrite is legal.

Reuse boundaries established before implementation:

- BindlessIndirectPacketStorageContract and MetalRhiZeroBaseIndirectPacketStorage
  prove original args20/mapping16/params16. Consumer Vertex binding7 must name the
  exact original mapping resource/control blocks at offset0 and range16; zero
  contents do not prove producer generation or safe consumption.
- BindlessSnapshotMetadataSource and the shared builder encode the existing
  metadata version/header/table/active-prefix contract. The new texture source
  must derive active count from an explicit ordered inventory of actual admitted
  views; texture element-count words are0 by the existing non-buffer metadata
  contract. No source-private or producer metadata may substitute for consumer
  metadata. Reuse the common little-endian element encoder.
- BindlessOwnedFaultDomain provides independent actual latch4/readback4,
  generation, storage diagnostics and exclusive submission lease. Consumer
  metadata44 must carry this independent generation; neither producer generation
  nor a synthetic counter may label it. Creation does not initialize the latch
  or issue a fault result; clear/copy/completion remains lease-bound.
- First explicit resource domain is three ordered live SampledTexture views,
  Rgba8Unorm, Texture2D, Color, sampleCount1 and one array layer. Reuse the existing
  MetalRhiBindlessComputeImageContract value boundary by extracting/shared use
  rather than duplicating its format/dimension rules. Actual extents must contain
  texel(0,0) at the selected mip, and parents, ShaderRead state, usage and native
  Texture2D/Float/non-depth binding correspondence must be verified. Explicit
  repeated read-only views may alias; no inactive-slot padding, inferred resource
  or dummy texture is admitted in this first full-capacity inventory.
- DescriptorConsumptionContract, DescriptorResourceLiveness, Metal native
  descriptor identity/registry observation, per-stage prepared binding locations,
  buffer range validators and core owner projection already supply reusable
  boundaries. The ordinary Graphics domain rejects writable storage; it cannot
  be weakened to accept the consumer fault latch. A private closed factory must
  consume those helpers under its explicit Prototype contract.

The incremental resource ceiling is52 bytes (metadata44 + independent latch4 +
readback4); borrowed original mapping and images remain charged under their
original ownership/budgets. Actual reported allocation size, complete retained
child owner graph and CPU cold peak must be preflighted/validated without a
magic owner count or inferred placement size. Revalidation must check resource
wrapper and native object/control block identities, parent liveness, descriptors,
registry, states, seals, exact metadata/ABI/generation and budgets without IO,
compilation or allocation. Pool/arena/view/parent retirement behavior must be
proved; independent producer and consumer fault leases must not alias.

Four-backend resource implementation remains pending: Null value validator only;
Vulkan/WebGPU new private cold resource payload DiagnosticFailure; Metal explicit
Prototype cold resource preparation only. No consumer snapshot/plan, packet
promotion, producer dispatch, draw, dependency/generation or GPU completion may
be issued by this cold root. Stable private execution diagnostic is reserved as
RHI-CANONICAL-CONSUMER-COLD-RESOURCES-PREPARATION-ONLY. Resource contract tests
must cover cleared facts, exact original tuple/resource/generation, forbidden
alias/fallbacks and budgets across four backend conclusions before native work.

Audit verification: real Metal Native and Emulated public snapshot admission
rejects RHI-BINDLESS-EXECUTION-UNAVAILABLE100 times
per target under exhausted quota/null ambient PMR, with allocation0, zero
snapshot leases/bytes and unchanged GPU allocations. Existing quick closed
package and Null hidden-frame paths pass. This is Contract Complete for the
admission/reuse audit only, not resource implementation or new Backend/Platform/
visual/Production acceptance. See consumer-resource-admission-audit.yaml.

### ClosedConsumerColdResourceValues/1 (l3c10b3b contract)

This shared allocation-free value contract consumes the exact closed consumer
package and retained source content, original common packet plus its original
mapping object, an explicit ordered three-row image inventory, the original
producer and independent consumer owned fault domains, and actual ownership/
resident totals under a versioned Prototype budget. It issues facts/metadata
only. Native registry/object/control-block proofs are separately mandatory in
the backend factory; input value facts cannot grant execution or native readiness.
Null may supply actual logical packet/fault objects for validator tests only.
A Null object must never be converted into a native resource root.

The mapping pointer must be the packet's original instanceMappings object, with
exact source parameters and capacity3/record0/binding7. Original cold packet
storageDiagnostic must pass; mapping state stays ShaderWrite, host/GPU/transfer
sealed, range16 at offset0. ShaderRead transition, consumer descriptor publication
and snapshot/plan issuance remain forbidden until actual production/generation
contracts. Cold resource preparation must not fake a readable state to satisfy
a public descriptor validator.

Each image inventory row has explicit version1 view/parent IDs, parent and view
format/usage, dimension, range, extent, mip/layer/sample counts and current range
state. The reusable shared scalar image domain retains existing Rgba8Unorm
sampled / R32Float storage Texture2D Color sample1/layer1 values; consumer accepts
only sampled rows, matching parent/view Rgba8Unorm, positive 2D parent extent,
valid one-mip/one-layer view range and ShaderRead state. Missing facts fail; no
parentLive default, inferred format, default resource or padded slot exists.
Explicit repeated readonly rows are allowed, but equal view IDs must name equal
logical facts; a view ID cannot identify conflicting parents/ranges/formats or
alias a parent ID. Metadata active count is the number of actual admitted rows
(three for this closed full-capacity domain), and non-buffer element words are0
by SnapshotMetadata/1. A shared typed word encoder writes the existing builder's
words to the exact little-endian byte footprint without host-order assumptions.

Both fault domains must be original factory-owned control blocks with valid
storage diagnostics and distinct generations/latch/readback identities. Consumer
latch remains ShaderWrite, readback CopyDst and fault prior state Undefined
(awaiting lease-bound clear). Its generation cannot equal producer or packet
generation. Metadata carries the consumer generation only; no initialized latch,
fault result or completion claim is issued. Cross-domain buffer aliases reject.

Budget version1, Prototype opt-in, nonzero finite owned-byte and owner ceilings
are mandatory. Actual owned bytes must cover metadata44+latch4+readback4 (minimum
52), stay within the authored ceiling, and actual owner count must be nonzero and
fit its ceiling. Backend allocation accounting and owner projections supply these
actual totals; the shared contract does not guess placement size or owner count.
Every failure clears all published facts/words/bytes. Observation allocates0.

| Backend | Value contract | New private resources | Execution |
|---|---|---|---|
| Null | Validator only | Not issued | DiagnosticFailure |
| Vulkan | Shared values | DiagnosticFailure | DiagnosticFailure |
| Metal | Shared values | Explicit Prototype cold preparation only, after actual proofs | DiagnosticFailure |
| WebGPU | Shared values | DiagnosticFailure | DiagnosticFailure |

RHI-CANONICAL-CONSUMER-COLD-RESOURCES-PREPARATION-ONLY is the stable execution
diagnostic; missing Vulkan/WebGPU payload uses
RHI-CANONICAL-CONSUMER-COLD-RESOURCES-PAYLOAD-MISSING. No public capability,
snapshot/plan, packet promotion, binding-ready state or GPU work follows.

Value verification: 53 targets and76 regressions, focused4 quick/Null and6 SDK
syntax paths pass. Actual Null logical resource values and100 exhausted-quota/
null ambient PMR observation attempts allocate0. Status Contract Complete /
Prototype Complete for values only; no new Backend/Platform/visual/Production
acceptance. Evidence consumer-cold-values-report.yaml. Continue actual private
Metal resource preparation with guarded borrowed wrapper lifetimes.

### MetalGuardedTextureInventory/1 (l3c10b3c1 prerequisite)

Before the cold consumer factory, the private Metal registry must expose weak
LifetimeObserver tickets issued by the concrete texture/view wrappers and the
original view parent ID. Borrowed wrappers are externally serialized with
observation/destruction. Strong native owners do not extend logical wrapper
lifetime; expired tickets fail before any dereference. No caller shared_ptr or
aliasing control block is accepted as proof that a borrowed wrapper is owned.
This adds no global L2 lifetime API or public capability. Other backends retain
the shared value contract and DiagnosticFailure for this Metal-private payload.

An allocation-free observation captures exact original guarded wrapper pointers,
shared logical inventory values, issuing registry, native parent/view payload
control blocks, native device/object/allocation tokens and any placement heap.
Revalidation checks tickets first, current registry entries and parent ID,
current descriptor/range state, original payload and complete owner graph.
Parent/view native resources must be real, same issuing device and original
allocation token; actual MTLTexture queries must match sampled Rgba8Unorm 2D
Color sample1/layer1, explicit native parent view/range, queried dimensions and
payload metadata. Absent native API fails deterministically; no stub success.
Retirement/unregistration, descriptor/state or payload replacement clears capture
facts or invalidates observation. Retained storage remains charged until final
native owner release; a stale logical ID can never be reused by an observation.

This prerequisite publishes no descriptors, consumer generation, plan, snapshot,
production completion or GPU work. Quick shared contract and Null hidden smoke
remain required; real Metal no-draw texture query/retirement tests provide
scoped Backend Validated evidence only. RenderDoc visual gates remain open.

Guarded inventory verification: 53 targets/77 regressions, focused4 quick/Null/
native and6 SDK syntax paths pass. Actual Metal mip1 parent/range/extent queries,
original owner substitutions, wrapper retirement before dereference, retained
charge release and100 allocation0 observations pass. Status Contract Complete /
Prototype Complete; scoped Backend Validated for queries/lifetime only. Evidence
guarded-texture-inventory-report.yaml. Continue the cold resource factory;
consumer execution, RenderDoc, Platform and Production gates remain open.

### MetalClosedConsumerColdResources/1 (l3c10b3c2 contract)

The private cold resource factory consumes the original factory-owned producer
cold root and closed consumer pipeline. Their retained source pointer and
control block, quota and issuing registry must match exactly. Explicit image IDs
are resolved into three guarded MetalGuardedTextureInventory/1 observations;
logical/native lifetime and repeated readonly row rules remain mandatory.

Only independent consumer owned fault latch4/readback4 and sealed immutable
metadata44 are created. The shared value contract derives metadata from that
fault generation and exact common packet/source/image facts. Resident accounting
uses actual allocation reports, bounded by the authored version1 Prototype
budget; schema minimum is52. Original mapping and image allocations remain
charged to their original owners. Owner admission projects all actual producer/
pipeline child references and image native owners. Before allocation, the three
independent buffer roles require wrapper+four mandatory independent native
owners plus the new fault-domain owner; after creation, the complete actual
projection must equal that admitted graph and fit the finite owner ceiling.
No hardcoded total, dummy native owner or implicit budget growth is permitted.

The mapping is the original common packet mapping16, still protected ShaderWrite
at binding7/Vertex with its queried UInt4 extent16/alignment4 fact. Metadata44
at binding1/Fragment and latch4 at binding2/Fragment consume the exact queried
UInt buffer ranges. Three ordered image rows consume binding3/4/5 Fragment
Texture2D/Float/non-depth facts. Immutable binding facts are cold correspondence
only; no descriptor set, binding-ready object, encoder or function binding is
published. The protected mapping must not be published as a readable descriptor by this
cold root; later production/generation transition contracts remain mandatory.

Revalidation first proves original roots/control blocks, then shared values,
guarded image lifetimes/native queries, exact captured ABI/stage/resource facts,
original registered native buffers, seals/state/generation, independently
reported resident totals and complete owner projection. It allocates0, performs
no file/network IO or compilation and has bounded loops. Original pool/arena
retirement may preserve retained roots; view/parent wrapper retirement always
fails. Device issuer retirement fails through the retained original packet.
New fault leases must be independent of producer fault leases. Creation or
budget failure releases all new charges without replacing a failed resource.

Null remains shared validator only; Vulkan/WebGPU private native resource
payload remains DiagnosticFailure; Metal explicit Prototype cold resources
only. Execution remains RHI-CANONICAL-CONSUMER-COLD-RESOURCES-PREPARATION-ONLY
on every path. Actual cold resource tests require exact/one-less CPU, GPU and
owner budgets, sealed metadata bytes, original substitutions/retirement,
independent faults and100 exhausted-quota allocation0 observations, plus quick
shared contract and Null smoke. Scoped Backend Validated is for cold resources
only; RenderDoc visual, Platform/Production and full-plan gates stay open.

Cold resource verification: 53 targets/78 regressions, focused6 quick/Null/native
and8 SDK syntax paths pass. Four actual Metal Little/Big by Produced/Suppressed
source cases retain original mapping16 ShaderWrite, metadata44+latch4/readback4,
actual owned52, projected168 owners and CPU coldPeak12748. Exact/one-less budgets,
original control blocks and even equal-content source mismatch, seals, hostile
ABI/state/registry/accounting, guarded wrapper retirement, pool/arena retention,
issuer retirement and100 allocation0 observations pass. Scoped retirement quota0
also guards the shared command initializer CPU ownership correction described
in command-retention-contract.md. Evidence consumer-cold-resources-report.yaml.

Status Contract Complete / Prototype Complete, scoped Backend Validated for
private Metal cold correspondence only. l3c10 closes for its package/resource
prerequisite scope. No readable mapping descriptor, consumer snapshot/plan,
packet promotion, initialized image/fault result, source dispatch/draw, public
capability or RenderDoc/Platform/Production/full-plan closure is granted.
Continue l3c11 canonical writer/sequence/completion audit and shared contract;
the existing fixed-literal single-output lease does not cover this six-role
source-reading args20+mapping16 domain.

### Canonical source producer recording admission audit (l3c11a)

GpuIndirectProducerContract/1 and the existing fixed-literal Metal executor
accept one20-byte output and ComputeFixedIndexedRecord proof. Their one-binding
layout, pure attribute consumer and GpuIndirectOutputSequence identity name a
single resource. They cannot authorize the closed canonical six-role producer,
which reads immutable source words and parameters and writes a correlated
args20+mapping16 pair plus its fault latch. Copying its literal footprint,
changing a resource ID or using the existing output lease would lose tuple,
metadata/range or completion semantics. The existing native fixed-literal
recorder is therefore a reuse reference only, never a canonical fallback.

Reuse boundaries for the next shared contract are explicit:

- Original compiler-owned canonical source/package, six queried scalar roles,
  original common packet parameters and exact closed consumer source/control
  blocks. Source expectation values are an oracle for tests, never production
  output or proof that GPU writes occurred.
- GpuBuffer monotonic host/descriptor/transfer seals remain set. A separate
  versioned privileged recording contract must consume a closed root/lease;
  no public unseal API or permissive writable descriptor is allowed. Any first
  Prototype writer must name its exact two scratch outputs, source and fault
  generation before native mutation. Existing PrototypeStorageOnly common
  packet remains unwritten/unpromoted with no consumer plan.
- Existing fault submission lease supplies exclusive actual latch/readback
  owners. Fault clear precedes dispatch; native copy/completion must determine
  fault result. Independent consumer fault cannot inherit producer result or
  generation. A test terminal mark remains a hostile admission probe only.
- Core MutationJournal, ExternalObjectSlot, AtomicCapacityBudget and
  SharedOwnerProjection, common CommandRetentionStorage, recorded resource
  state helpers and GPU lifetime transition contract supply reusable mechanisms.
  Extend the shared ordered value protocol to an exact correlated two-output
  tuple before backend binding; do not duplicate a native-local state machine.
  Command budgets must derive from explicit protocol operations, not reuse the
  single-output executor's seven-unit minimum as a guessed capacity.
- Native submission retention already preallocates completion storage, moves
  owner/lease graphs before commit, validates actual command-buffer completion,
  releases owners/leases and rolls back recorded state on failure/abandonment.
  Reuse these mechanisms only after new domain admission; absence of a
  submission owner or actual completion is deterministic failure. No CPU replay,
  immediate signal or no-op wait is equivalent to native producer completion.

WebGPU's existing processor owns private metadata/fault/scratch arguments/
mapping and exact packet destinations, and requires an issued consumer snapshot/
authorization/intent. Its generic fixture tuple and metadata generation policy
do not satisfy the new closed capacity3/binding7, independent owned fault and
PrototypeStorageOnly constraints. That code supplies ownership/ordering
references only; neither browser-native success nor a host branch syntax check
is proof of this closed domain. Vulkan has no approved private canonical
recording payload. Null may validate ordered values but cannot synthesize native
produced bytes or completion. Metal has cold correspondence only. All four
therefore keep this canonical recording domain non-executable until c11b and
actual payload validation; public capability versions remain0.

Reserved missing-domain diagnostic for c11b is
RHI-CANONICAL-PRODUCER-SEQUENCE-CONTRACT-MISSING; the audit adds no runtime API
claiming to emit it. Current tested runtime gates remain preparation-only cold
contracts, PrototypeStorageOnly common packet and generic public execution
refusal. Phase status Contract Complete for reuse/admission audit only. Current
53-target/78-test evidence retains fast shared contracts, Null hidden frame and
actual cold seal/owner/lease/retirement negatives. No GPU producer/consumer work,
new Backend/Platform/visual/Production acceptance follows. Continue c11b shared
write/sequence/lifetime values, keeping execution closed until native proof.

### CanonicalProducerRecording/1 (l3c11b shared contract)

The new allocation-free value protocol admits the exact compiler-owned producer
and closed consumer packages, one original immutable source, original common
packet, independent producer/consumer fault domains and nine named buffer roles:
metadata, latch, source, scratch args20, scratch mapping16, packet parameters,
args readback20, mapping readback16 and original producer fault readback4.
Source/metadata/scratch writes remain monotonically sealed; packet parameters
and fault buffers retain their original owning control blocks. Scratch outputs
must be independent from both common packet destinations. All named resources,
packet storage, consumer fault buffers and image rows have explicit alias checks.
Cold shape/state/metadata correspondence is admission evidence only; no byte
oracle can prove a GPU dispatch. Captured pointer/control-block identities and
package/packet/fault/recording generations must still match at every observation.
Backend registry, issuer, guarded image lifetime, native ABI, complete ownership
graph and exclusive command/fault leases remain independent requirements.

The only version1 sequence is fault-clear begin/fill-zero/end/dependency, compute
begin/one dispatch/end, args/mapping/fault dependencies, readback begin/args copy/
mapping copy/fault copy/end. Its15 units are derived from the enumerated protocol,
not the older single-output minimum. Each copy consumes the matching correlated
write; no draw, common packet copy or consumer dependency is present. Failed
identity/order/budget checks are transactional. Resource state remains in the
existing command journal; shared values never mutate resources. Three bounded
readbacks have a schema minimum40 bytes; actual allocation reports and captured
shared-reference slots must fit explicit finite ceilings. Full backend owner
projection is separately budgeted; shared-reference counts are not native owners.

Lifecycle composes GpuIndirectOutputLifetimeContract/1. Cold to Recorded requires
the complete exact sequence. The canonical extension allows Cold cancellation
(Failed/Abandoned) before or during partial recording; both are terminal and
cannot submit or continue recording. Recorded may submit/fail/abandon, and submitted
terminal transitions require structurally asserted native completion evidence.
That assertion is a value test only and cannot issue a completion ticket: the
backend must bind it to the original command's real completion callback and
retained ownership. Terminal states cannot retry, migrate generations or revive
retired wrappers. Readback bytes/fault results are not exposed by shared values.

Lowering conclusions are explicit: Null validator only; Metal DiagnosticFailure
until a separately admitted private Prototype native recorder exists; Vulkan and
WebGPU DiagnosticFailure payload missing. Every shared execution gate rejects,
including explicit Prototype opt-in; public capability versions remain0. No
unseal, packet promotion, dummy resource, CPU replay or synthetic signal is an
approved lowering. Required evidence is actual Null logical resource admission,
exact/one-less budgets, changed control blocks/resources/generations, hostile
command order and missing/foreign/duplicate completion,100 exhausted-quota
allocation0 observations, quick contract tests and Null hidden-frame smoke.
Contract Complete does not imply Backend/Platform/Production or visual acceptance.

### MetalCanonicalProducerRecordingPreparation/1 (l3c11c)

Before privileged recording, an explicit Prototype private preparation retains
the original Metal closed consumer root and complete child graph. It creates
only args20/mapping16 managed readbacks (schema minimum36 new bytes), checks
actual allocation/query/registry/storage policy and seals host writes. Existing
producer fault readback4 remains owned by its original domain. Every producer
binding and native observation is inherited from and revalidated through the
original cold root. Shared CanonicalProducerRecording/1 admission names the
actual original command recording identity and exact immutable source owner.

The private preparation accepts only a genuine open native command on the same
registry/native device/CPU quota with bounded prepared retention. The original
producer fault submission lease is exclusively reserved before publication;
consumer fault remains independently available. The command is weakly observed
to avoid ownership cycles. Preparation failure releases the lease and new
allocations; command destruction/abandonment, foreign command, issued-root
substitution, wrapper retirement or changed native/control-block facts reject.
No command owners are retained or GPU commands emitted by preparation. The
exclusive lease lasts until root release; actual recording must later move it
into completion storage. This phase has no native completion/result claim.

New resource owner slots derive from two wrapper+mandatory four-owner native
schemas, added to the actual retained consumer-root projection, all15 captured
shared-admission references and the original fault-lease owner. Captured final
projection must match preflight count exactly and fit the authored owner ceiling.
Actual resident totals must match shared readback admission and authored byte
ceilings. Validation is allocation0 and bounded; backend native/query provenance
cannot be replaced by shared scalar values. Null remains validator; Vulkan and
WebGPU payloads remain DiagnosticFailure. Metal execution still rejects with
RHI-METAL-CANONICAL-PRODUCER-RECORDING-PREPARATION-ONLY. Public versions0,
protected scratch/common packet seals and cold consumer gates remain unchanged.
Required evidence: actual Metal exact/one-less CPU/GPU/owner budgets, exclusive
producer versus independent consumer fault leases, changed originals/native
facts, abandoned/foreign command, retirement and100 allocation0 observations,
plus shared quick tests and Null hidden-frame smoke. Status scoped cold resource/
lease Backend Validated only after tests; native producer execution follows.

### MetalCanonicalProducerRecording/1 (l3c11d native lowering contract)

The private implementation consumes the original private preparation under a
separately versioned Prototype command intent. A generic open native command,
cold root or shared sequence is insufficient writer authority. Admission must
freeze the15-operation protocol before native mutation, prove all six original
queried buffer slots, original registry/device and actual native extents, and
preflight the complete retained owner graph. Preallocate three encoder ownership
slots, bounded three-output resource journal and completion storage in cold
preparation; recording and finish must allocate0 under an exhausted caller quota.
No public buffer unseal or standard writable descriptor is introduced.

Only the original producer latch may receive native fill-zero. Its clear-to-
compute dependency precedes the one1x1x1 source-reading dispatch (workgroup shape
comes from the verified compiler-owned package). Producer bindings retain exact
metadata/source/parameters and independent scratch args20/mapping16. Explicit
ComputeShader/ShaderWrite to Transfer/TransferRead dependencies transition all
three outputs to CopySrc through the existing resource journal. All copies are
zero-offset/full-schema writes to the prepared20/16/4-byte readbacks. Tracked
hazard native storage plus ended serial encoders can implement dependency order;
managed readback synchronization must derive from the queried native storage
mode. An untracked resource or unknown storage mode rejects before encoding.
The common packet destinations remain untouched ShaderWrite and sealed. No draw,
consumer fault clear or readable consumer mapping is part of this protocol.

Any native encoder/adoption/order failure poisons the command, ends owned native
encoders and rolls back unsubmitted journal states. The root becomes Failed or
Abandoned, and the original fault reservation is released exactly once. A fresh
admitted recording may retry only an unsubmitted and unconsumed producer tuple;
submitted tuples are terminal even after native failure. At commit, completion
storage receives the root, full native resource graph and move-only fault lease
before GPU work can execute. The owner thread commits journal entries while
keeping wrappers alive against fast callback destruction. The callback accepts
only the actual original MTLCommandBufferStatusCompleted; CPU completion flags,
shared lifetime assertions and source expectation values cannot retire it.
Fault readback terminal marking follows actual native copy/completion only.

Tests must delay native execution with an actual shared-event prologue, retain
leases/resources while callers drop command/root, reject CPU completion spoofing,
then release the event and compare real args/mapping/fault readbacks to the test
oracle. Four Little/Big by Produced/Suppressed cases, changed source values,
exclusive leases, negative admission, recording failure/abandonment and finite
owner/CPU/command budgets remain required. No readback, resource or generation
is published as a production consumer ticket. Null remains value validator;
Vulkan/WebGPU remain deterministic missing-payload failure. Metal's approved
Prototype payload may be marked scoped Backend Validated only after actual
native evidence. RenderDoc remains RHI-RENDERDOC-TOOL-UNAVAILABLE; no visible draw
or visual, Platform/Production/full-plan acceptance follows from this protocol.


c11b/c11c verification: 53 targets/81 regressions, focused4 shared quick/Null/
actual native and8 SDK syntax paths pass (WebGPU host only). Four actual Metal
source cases retain36 new readback bytes,40 total readback schema bytes and195
complete child reference slots including15 shared-admission references and the
original fault-lease owner; CPU coldPeak9520. Exact/one-less CPU/GPU/owners,
original command/source/control blocks, native/registry/descriptor/accounting
mutations, exclusive producer versus independent consumer lease, CPU completion
spoofing, command abandonment/destruction and100 allocation0 observations pass.
Final scoped quota0 and command capacity leases0. Shared cancellation is terminal
at every partial recording boundary. Evidence canonical-preparation-report.yaml.

c11d verification: all53 targets build and82 regressions pass; focused5 shared
quick/Null/native and9 SDK syntax paths pass (WebGPU host branch only). Actual
Metal API Validation executes8 successful source-reading submissions: four
Little/Big by Produced/Suppressed with real event-gated completion, and four
independent-generation no-wait submissions under exhausted quota. Args20,
mapping16 and fault4 readbacks match changed source values exactly. Producer
metadata/source/parameters and original native function/ABI remain bound;
common packet args/mapping stay zero, sealed and ShaderWrite. Consumer fault
is never cleared or marked completed by producer execution.

Private native roots project199 child reference slots, retaining123 unique
owners;123 succeeds and122 rejects before mutation. Measured command/root CPU
peaks are15848/9592 bytes; generic cold preparation's current peak is9592.
Record/finish/no-wait submit allocate0. Original root/command retained/dropped
combinations, actual wait-prologue allocation failure, null-completion rejection,
CPU completion spoof, abandonment/destruction, journal conflict and fresh
unsubmitted retry pass. Actual callback releases the original fault lease even
when the caller retains the root, and submitted generations are terminal.
Forced GPU error completion has not been injected; no such test claim is made.
Final scoped CPU quota and command-capacity leases are0. Evidence:
canonical-native-producer-report.yaml, focused/regression logs and JUnit XML.

Status: Contract Complete / Prototype Complete; scoped Backend Validated for
this explicit private Metal source-reading clear/dispatch/dependency/readback
and actual native completion only. Shared values issue no native ticket;
public capability versions remain0, packet storage-only and closed consumer
execution unavailable. No draw/visual, RenderDoc, Platform Validated,
Production Ready or full-plan closure. Continue c12 consumer resource/geometry/
image-content/generation and bounded submission correspondence.


c11d implemented phase admission: shared structural
storage phase is explicitly Cold (three outputs ShaderWrite) or ReadbackReady
(three outputs CopySrc). The same original IDs/owners/metadata are revalidated;
this is not a capability or a consumer authorization. Public cold validators
still require Cold. Only the private recorder/finish/submit path may select
ReadbackReady after all15 operations. Native command intent blocks every ordinary
recording entry, and its graph preallocates one journal and three ownership slots.
Those four actual owners are included in the private preparation's full graph.
A consumed bit belongs to the original producer cold generation and is set only
at commit admission; submitted failure remains terminal. The original callback
publishes atomic root status and fault readback marking, without allocating or
reading files; caller CPU completion flags have no authority over either.
Four-backend execution stays closed at public L2: Null validator, Vulkan/WebGPU
missing payload DiagnosticFailure, Metal only this explicit private version1
Prototype. No upper-layer native handle or writer API is added.


Private execution lowering decision is separately versioned from shared values:
CanonicalProducerPrivateRecordingLowering/1 consumes explicit Prototype budget
before command creation. Null yields ValidatorOnly; Vulkan/WebGPU yield
DiagnosticFailure; Metal yields Prototype only for valid version1 opt-in. That
frozen decision is retained by the private command and consumed by its recorder.
Native API absence/creation/query/ABI failure still rejects before mutation;
Prototype never sets a public Native/Equivalent capability. The shared value
lowering remains non-executable on all backends. Tests cover opt-in/version/
budget denial and all four disposition labels independently of native evidence.

### Closed consumer post-producer admission audit (l3c12a)

Actual c11 completion establishes source-derived scratch args20/mapping16 and
producer latch4 only. Scratch Arguments has StorageBuffer|CopySrc|CopyDst,
without IndirectBuffer intent. Both scratch outputs end CopySrc. The cold
consumer Vertex binding7 retains the different original common packet mapping,
which remains zero/ShaderWrite; args remain zero as well. PrototypeStorageOnly
packet validate/consumer leases stay closed. Native status or a successful
readback cannot substitute scratch pointers, mutate usage or promote that
generation. c11 completion is terminal for that producer generation and grants
no later writable authority or cross-command consumer ticket.

Existing VertexLocationFloatInput/1/InstanceAttributeFetch/1 cannot authorize
the resource-reading closed Vertex entry. Reuse source.validateStream with the
existing IndexInputConsumptionContract/VertexInputConsumptionContract to prove
immutable Float2 position fetches, then admit the original closed pipeline's
queried readonly stream Struct ABI. Geometry upload uses reusable
MetalRhiImmutableBufferUpload with explicit VertexBuffer usage; do not broaden
ordinary Graphics provider admission for resource-reading/fault-writing shaders.
Actual sealed original native index/source/pipeline controls must remain bound.

Existing sampled image inventory proves format, dimensions, selected mip/view,
state, native object/control blocks and live parent/view, but not initialized
texel contents. Two cold-only images are allocated without uploads. GpuTexture
currently has no immutable write seal. A later execution contract must bind
explicit deterministic image content and its upload/subresource dependencies,
permit controlled mutation or retain an exclusive private content lifecycle,
and test retired/replaced original views; allocation/state alone is insufficient.
The independent consumer fault generation requires its own clear/lease/readback
and actual callback, not the producer fault result.

A production packet must originate through existing common issuance and an
approved consumer plan; old storage-only packets are permanently excluded.
Before native bridging, define original destinations, scratch-to-packet transfer
authority, complete barriers, same-command producer/consumer ordering or actual
cross-command sync, suppression visibility and independent fault outcomes.
No host readback/upload replay, dummy resource, inferred instance capacity or
handwritten packet is permitted. Original fetch, images/pass/pipeline and all
completion owners need protocol-derived finite CPU/GPU/command/reference budgets.

| Domain | Null | Vulkan | Metal | WebGPU |
| --- | --- | --- | --- | --- |
| Existing completed producer | Value validator | DiagnosticFailure | Private Prototype actual scratch data/completion | DiagnosticFailure |
| Closed geometry values | Planned validator | Values, execution DiagnosticFailure | Values, native upload pending | Values, execution DiagnosticFailure |
| Readable original packet + consumer plan | Validator only | DiagnosticFailure | DiagnosticFailure | New tuple DiagnosticFailure |
| Closed consumer image content/draw/completion | Validator only | DiagnosticFailure | DiagnosticFailure | New tuple DiagnosticFailure |

Audit status: Contract Complete for reuse and missing-authority boundaries only.
82 current regressions preserve quick contracts/Null hidden-frame and actual
producer evidence. No new consumer Backend/Platform/Production or visual claim.
Continue c12b geometry values and c12c native sealed geometry; c12d must define
packet/generation/image-content/sequence admission before any consumer draw.

### ClosedConsumerGeometryValues/1 (l3c12b shared contract)

Contract before implementation: first finite layout is exactly one PerVertex
stream, one Float32x2 attribute at shader location0/offset0, stride8, authored
stream slot retained. Input declares IEEE754 binary32 scalar bits and explicit
Little/Big byte order. Nonempty full payload must be a multiple of the Float2
stride, normalized bit-exact to little endian by core::UnsignedElementSpan.
No floating-point approximation or visual coverage is inferred from these bits.
Version1 opt-in and finite geometry/snapshot ceilings precede copying. Snapshot
retains full normalized positions and original source bytes; subsequent checks
match original bytes, package identity and authored layout and re-use source's
existing immutable index/fetch proof. Active draws require all referenced vertex
fetches; suppressed/zero draws keep their explicit no-fetch semantics.

Shared shape/seal validation requires a distinct valid VertexBuffer resource,
CpuToGpu, exact full bytes, initial/current ShaderRead and all monotonic seals.
It is not native uploaded-byte/control-block proof or a consumer ticket. Actual
c12c preparation must additionally retain original closed pipeline/source/index,
resource/device/registry/quota and authoritative native stream ABI. Null is
validator only; all GPU execution lowerings remain DiagnosticFailure. Quick
Native/Emulated package/fetch negatives and actual Null sealed-buffer smoke
precede native implementation. Status pending implementation and validation.

Shared c12b validation passes both retained compiler package targets, source
and position byte orders, Produced/Suppressed fetch cases, full exact normalized
32-byte positions +32-byte source snapshot, exact/one-less snapshot/CPU quotas,
malformed layout/encoding/moved/correspondence/storage and actual Null sealed
VertexBuffer.100 allocation0 validations per case and quick3/Null pass. Status
Contract Complete / Prototype Complete for values only; GPU executions closed.

### MetalClosedConsumerGeometryPreparation/1 (l3c12c cold lowering)

Native contract before implementation: original factory-issued closed pipeline
retains source/index/package/quota and independently queried Vertex stream ABI.
The factory consumes c12b values under that quota, exact complete source/layout
correspondence and original device/registry. No ordinary provider or generic
attribute proof substitutes the closed resource-reading entry. Extend the
existing sealed upload policy only with explicit ImmutableVertex, VertexBuffer,
CpuToGpu and ShaderRead; exact bytes and all writes sealed. Native upload helper
must reject unsupported usage rather than default to another domain.

Preflight the protocol-independent full pipeline owner graph plus one buffer
wrapper and its actual independent native owners before upload/publication.
Finite maxUploadBytes constrains real allocation bytes, and maxChildOwners
constrains the full captured projection. Capture exact original native stream
slot, queried alignment/access/pointee/minimum extent and native object/control
blocks using existing stage range/native observation helpers. Revalidate actual
wrapper/descriptor/allocation/seals/registry/ABI/device/owners/source without
IO, compilation or allocation; weak original issuer retirement must close
admission while retained storage remains alive. Read native uploaded bytes in
the smoke, test exact/one-less CPU/GPU/owners, hostile aliases and native/state/
registry mutations, null ambient PMR/exhausted quota and full retirement.

Four backend conclusions: Null values only, Vulkan/WebGPU deterministic missing
private cold payload, Metal explicit Prototype actual immutable geometry upload
only after these native tests pass. No readable packet, consumer generation,
image-content proof, command/draw/completion ticket or public version follows.
Visual acceptance remains false; RHI-RENDERDOC-TOOL-UNAVAILABLE persists.

The native geometry root observes the existing original BindlessCrossExtension
LifetimeEpoch through a weak ticket, under a narrowly scoped backend friendship.
This reuses device issuer lifetime instead of adding a second epoch, retaining
the device, inferring native-device liveness or exposing a new L2 handle/API.
RHI-METAL-CLOSED-GEOMETRY-ISSUER-RETIRED closes validation before original
pipeline/resource observation. Native retention alone cannot revive admission.

### Packet/consumer execution continuation (l3c12d1 contract draft)

This draft defines the next implementation scope; it grants no execution. The
current public planner rejects ClosedImageMappingConsumer/1 even if an existing
indirect-consumer capability is present. A private Prototype issuance must be
separately versioned and validated inside the existing common packet pool. It
may allocate a new eligible generation only after approved complete source,
closed package, original geometry/pass/images/consumer fault and finite budget
admission. Existing null-plan storage-only generations are permanently excluded.
A caller-supplied plan, alternate scratch pointer, extra native usage flag or
backend name cannot manufacture eligibility. Public capability versions stay0.

Before implementing that issuance, define a distinct eligibility disposition
and tests: Null ValidatorOnly; Vulkan/WebGPU new-tuple DiagnosticFailure; Metal
private Prototype only after all below prerequisites exist. Publish no success
while an admission path or actual payload is missing. Eligibility belongs to
issued common storage and private consumer authority, never a fabricated public
Native/Equivalent BindlessLoweringPlan. Full shared identities must include
contract/source/package, original producer and independent consumer fault
generations, packet generation, one exact recording and all original resource
IDs/control blocks. The lowerer's frozen decision must be consumed by recording.

Prefer a single actual native command for source-reading production and closed
consumption, avoiding a host readback bridge and an unproved cross-command
synchronization contract. A new producer tuple is required; completed c11
generations are terminal. Explicit native copies transfer exactly args20/map16
from original source-derived scratch to the newly eligible common destinations,
whose existing schema includes IndirectBuffer and CopyDst intent. End producer,
explicit ShaderWrite/TransferRead, CopyDst/TransferWrite and final args
DrawIndirect/IndirectCommandRead plus mapping VertexShader/ShaderRead dependencies
precede consumption. Record/finish/submit must reject partial or reordered
operations; speculative failures preserve original state, post-mutation failures
poison and roll back unsubmitted journals. No unseal of ordinary write paths.

The independent consumer latch clear ends before opening Render; it cannot
interrupt/restart an attachment pass. Exact complete attachment load/store/clear,
viewport/scissor, authored native render state, original sealed index/geometry
and module-local uint4/uint32/texture ABI are admitted before GPU work. Both
producer and consumer fault leases and any packet reservation move into the
existing actual completion storage; CPU signals never retire them. Producer
suppression produces zero physical instances and no fragment invocation, so
consumer Clean alone cannot classify the frame: the correlated result requires
both independent outcomes, with producer FaultSuppressed retained explicitly.
Readback is validation evidence, never command replay input.

Image-content prerequisite remains pending. First finite accepted content must
name original parent/view and exact subresource, format, mip extent and tightly
specified upload layout/bytes. Reuse existing transfer staging/copy layout
validators and core bytes; no guessed row pitch, placeholder texture or padded
slot. GPU completion and explicit copy-to-ShaderRead dependency establish the
content revision; a writable/mutable texture without a tracked revision/exclusive
lifecycle must fail admission. Lifetime/native identity and deterministic sample
bytes must remain consistent while recording and until actual completion.
Texture byte size/source/CPU/GPU/staging/command/owner budgets must derive from
those descriptors and protocol, including all native owners and temporary peak.

Implement shared eligibility/order/image-content values and forbidden paths
with quick contract tests and Null hidden-frame before private Metal cold
execution roots. Then prove source-derived logical image selection, clear/
suppressed/mapping-fault controls, all consumer leases/owners and delayed
completion with actual GPU results. AI RenderDoc capture remains a separate
required visual gate on a supported backend; this host reports
RHI-RENDERDOC-TOOL-UNAVAILABLE. No visual/Platform/Production/public allowlist or
full-plan closure follows from byte-only prerequisites or this draft.

### c12a-c verification status

Contract Complete / Prototype Complete for consumer admission audit and geometry
values/preparation; scoped Backend Validated for private Metal native immutable
VertexBuffer upload only. Eight actual source-mode/endian/outcome cases produce
exact normalized32-byte native content with49 child references, repeatable cold
CPU peak3056 (existing registry capacity established), readonly native stream
slot0/alignment4/minimum8. All host/GPU writes sealed; first-use and repeat
allocations stay under the original quota. Exact/one-less CPU/GPU/owner/snapshot,
foreign/alias/native/ABI/registry/state/storage/correspondence and device
retirement probes pass.100 exhausted-quota allocation0 observations percase,
final scoped quota0 and command capacity leases0.53 targets/85 regressions,
focused4 quick/Null/native and9 SDK syntax paths pass (WebGPU host only).
See consumer-geometry-report.yaml, logs/JUnit and capture diagnostic.

No packet promotion, initialized image proof, consumer generation/draw/completion,
public version, visual/Platform Validated/Production Ready or full-plan closure.
Private issuance and native revision/consumer execution remain a draft.
The c12d1 shared eligibility/image-content/sequence values below are implemented;
their verification is recorded separately from native execution acceptance.

### CanonicalClosedConsumerRecordingValues/1 (c12d1 shared contract)

Implement only structural values in this step. Every execution lowering remains
closed until original issuance, native revision/exclusive texture lifecycle and
real source/consumer payload exist. Never assign a successful public lowering
plan to private eligibility. The shared admission takes original c11 cold roles,
a genuine non-storage-only common packet (Null validator is supported), c12b
geometry values and sealed original geometry buffer, independent consumer
metadata44/fault4/readback4, three ordered explicit image snapshots and three
structural revision identities. Snapshot is authoring evidence, not GPU content
or completion proof; supplied revision identities have no native authority.

Image snapshot captures exact original view/parent/subresource metadata plus
RGBA8 upload bytes, explicit full selected mip extent, zero origin, offset0,
rowsPerImage equal to height and an explicitly supplied row pitch satisfying
the existing L2 kTextureUploadRowPitchAlignment. Full pitched payload is retained
bit-exact including padding; no repacking or guessed row size. Format-derived
row footprint, mip extent and bounded source/snapshot size are validated before
copy. Existing texture usage must include Sampled and CopyDst; unsupported
formats/layouts, missing rows, wrong mip/layer/IDs and byte overflow fail closed.
Snapshot retains no texture wrapper/native object; later native revision root
must prove upload, original control blocks and exclusive mutation lifecycle.

Freeze complete original source/package/packet/producer+consumer generation/
recording IDs, geometry and consumer metadata/fault/readback IDs and ordered
view/parent/revision tuples. Existing storage-only packet fails before consumer
admission. Conflicting content/revision for aliased selected views fails, while
explicit identical readonly aliases preserve the existing inventory contract.
Admission captures original shared-reference control blocks and revalidates
them without allocation. No source/readback/geometry/metadata/fault/packet
resource may alias a writable destination or image identity.

The fixed protocol begins with the existing c11 clear/dispatch/dependency prefix,
then prepares both actual common packet destinations, copies full args20/map16,
ends transfer and emits explicit args DrawIndirect/IndirectCommandRead and
mapping VertexShader/ShaderRead dependencies. Independent consumer latch clear
and dependency precede Render begin. Full pipeline/descriptors/vertex/index/
viewport/scissor state precedes one actual indirect command; Render end precedes
consumer-fault readback dependency. Final transfer copies producer args/map/fault
and independent consumer fault into original readbacks. No producer readback
may supply draw arguments, no open Render pass is restarted to clear a latch.
Command/readback/reference ceilings derive from exact protocol/roles. Transactional
wrong order/ID/budget leaves sequence unchanged; partial cancellation is terminal.
Completed assertions remain validator values only and cannot release a lease.

Correlated outcome values require both independent fault words. Producer fault
requires actual zero instanceCount and zero mapping; consumer fault cannot be
ignored. Consumer Clean alone never certifies producer success or a clean frame.
Classifier consumes values only and issues no ticket; native completion and
actual readback provenance are later required. Quick Native/Emulated package
contracts, actual Null upload/seal/barrier path and hidden-frame smoke precede
private native issuance. Public versions0 and all execution gates closed.

Contract Complete / Prototype Complete for shared values only. Four endian/
source-outcome cases per Native/Emulated package verify exact immutable pitched
bytes, selected NPOT mip extent,26 projected references,38 operations and44
readback bytes. Exact/one-less snapshots/CPU/count ceilings, original canonical
source/geometry/layout owners, replacement control blocks, aliased-view content/
revision conflicts, wrong sequence/cancellation/completion and both independent
faults pass.100 exhausted-quota/null ambient PMR validations and full sequence
runs allocate0. Actual Null upload/barrier intent records and hidden-frame pass;
Null establishes no texel content. Genuine Metal storage-only packets are
rejected by the same shared entry.53 targets/87 regressions and focused7 pass.
See closed-consumer-values-report.yaml and associated logs/JUnit.

Source content now has its own core canonical owner; Metal's private source
root retains it and shared admission consumes that original reference. An
unbound subobject alias cannot certify first-capture ownership. Revalidation
still preserves original pointer and control block, and no native authority
is added. Existing source/geometry/preparation/command projections now have
34/50/197/201 slots respectively; geometry cold CPUpeak3072, producer command/
root peaks15848/9592 unchanged. Earlier stage reports retain historical counts.
Real producer/geometry regressions pass; consumer native revision/issuance,
draw/completion, visual/Platform/Production/full-plan acceptance remain pending.

### c12d2a Native canonical image upload evidence (contract before validation)

Use the existing ordinary L2 transfer contract to validate c12d1's exact upload
values against actual Metal texture storage. This step produces test evidence,
not a production revision issuer. Null validates declared layout/state only;
Metal must perform native staging/upload/readback/completion; Vulkan/WebGPU
remain DiagnosticFailure for this private canonical evidence tuple until actual
payload evidence exists. All four closed consumer execution lowerings stay
non-executable. Caller-supplied structural revision values cannot release leases
or authorize any consumer, even when the ordinary upload/readback succeeds.

Test original real RGBA8 Sampled|CopyDst|CopySrc parent/view/subresource tuples,
including NPOT mip extents. Capture authoring snapshot before recording and
retain the exact pitched payload including padding. A single ordinary command
must record ten distinct operations: ShaderRead→CopyDst dependency, upload
transfer begin/upload/end, CopyDst→ShaderRead dependency, ShaderRead→CopySrc
dependency, readback transfer begin/full selected-mip texture-to-buffer copy/end,
and CopySrc→ShaderRead dependency. Stages/access derive from these actual roles:
FragmentShader/ShaderRead and Transfer/TransferWrite or TransferRead. State
restoration without a real native texture and native completion proves nothing.

Budgets derive from the largest explicit selected-mip payload, one staging page,
one texture and one readback active at a time, with original reported allocation
charges observed before/after retirement. Tests may block at their explicit
validation boundary; no hot-path/no-wait/production claim follows. Initialize
readback storage to a different pattern before GPU work and compare all RGBA8
texel bytes in every selected row after native completion. Row padding is not
texture content and must not be compared to source padding. Verify actual blit
stats and native completion, original parent/view query/owner observations,
no shader/draw, hostile metadata/layout/budget cases via the existing quick
shared contract, Null hidden-frame and explicit missing revision diagnostic.

The resources remain mutable. Success must still refuse consumer admission as
`RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-REVISION-PAYLOAD-MISSING`. Next c12d2b must
establish exclusive original mutation lifetime or tracked content revision before
new private packet issuance; it cannot promote old storage-only packets. In
particular sealing after an upload is insufficient if earlier admitted external
write commands can still execute. A future exclusive root must create/own the
texture before exposing its IDs, prevent every ordinary write escape, retain
original wrapper/native allocation owners, and bind revision changes to actual
submission/completion. Backend Validated here is limited to ordinary native
image byte upload/readback; visual/Platform/Production/full-plan acceptance is
pending and RenderDoc's unavailable diagnostic remains mandatory.

### c12d2b exclusive native image lifecycle audit (not implemented)

The ordinary native evidence uses CopyDst-capable mutable textures. Neither
GpuTexture nor the registered texture/view currently carries an immutable write
seal or an exclusive revision issuer. Metal native buffer-to-texture, staging
upload-to-texture and texture-to-texture paths resolve writable destinations
through MetalRhiResourceRegistry and check usage/layout; these checks do not
establish exclusive original content. A registry snapshot or parent/view
allocation-retention token is insufficient. Image correctness must not be
inferred from the successful c12d2a readback.

The finite next Prototype domain should create fresh privately owned image
parent/view resources before publishing their IDs. It must refuse externally
supplied mutable parents/views rather than assume no earlier writer exists.
Authoring bytes use c12d1's explicit format/mip/row contract; snapshots are captured
against these original newly issued IDs, with actual core wrapper ownership,
original registry/device/native object/allocation tokens and weak issuer epoch.
Do not reuse old c11 image or packet generations. A full three-image root belongs
to a fresh consumer tuple and binds ordered views/revisions to actual completed
upload commands. No public capability/version or native-handle upper API changes.

Before publishing readiness, establish an original backend-owned immutable
content marker observed by every ordinary destination write path, including
parent/view aliases. Transfer writes (buffer→texture, backend staging upload,
texture→texture), storage descriptors, attachments/resolve and clear/mipmap
escape paths must each have a deterministic supported or rejected conclusion.
Descriptor usage restrictions must be validated, not merely inferred from the
new root's preferred creation descriptor. Existing objects and registry aliases
cannot be made immutable after external commands have already admitted writes.
Retained native payloads cannot revive a retired wrapper or issuer.

Do not create an unlocked interval between upload completion, marker installation
and root publication. Cold preparation may use an explicit blocking validation
boundary, but hot validate/record/finish/submit cannot wait. A revision ticket can
only be issued by the concrete private root after actual native upload completion
and dependency; caller revision numbers, CPU fence/event signals or a mutable
snapshot cannot issue it. Pending/failed/abandoned uploads reject consumer
admission, with submitted ownership retained to actual completion. The marker
cannot be reset, repaired by a placeholder, or silently downgraded.

Derive bytes and staging/command/owner bounds from exact snapshots and actual
reported allocations, with independent core CPU quota and rollback. Reuse
TextureSubresourceExtent, Content snapshots, guarded inventory observations,
shared owner projection, native transfer retention/completion and issuer epochs.
Test exact/one-less budgets, every writable escape, substituted owners/registry/
revision/issuer, wrapper retirement, precommit/partial failure and delayed native
completion. Keep quick shared/Null hidden-frame paths. Null remains ValidatorOnly;
Metal is DiagnosticFailure until this complete private payload exists, then only
explicit Prototype cold immutable content; Vulkan/WebGPU remain deterministic
missing-payload failures. Closed consumer execution stays closed independently
until actual new packet issuance and the38-operation native executor exist.

### c12d1-d2a verification status

Contract Complete / Prototype Complete for c12d1 shared values and c12d2a
ordinary image evidence; scoped Backend Validated for ordinary Metal upload,
selected-mip readback and native completion only.9 actual submissions (three
images × three NPOT selected mips) compare every RGBA8 texel/channel exactly:
extents7x5/3x2/1x1, pitch256, retained payload1280/512/256. Source padding is
retained in the authoring snapshot and never treated as texture content. Native
readback starts poisoned; authoring is reused after record. Actual command count10,
blit stats, native readback Completed status, explicit ShaderRead restoration and
original parent/view queries pass.100 exhausted-quota/null ambient PMR inventory
and snapshot validations allocate0. Reported resource GPUpeak1448 and final0;
backend staging uses one separately budgeted1280-byte page. CPU live before
exhaustion22808/19736/18712 by mip, caller quota262144 and final scoped0. The
historical quota high-water includes deliberate exhaustion and is not a native
recording peak.54 targets/88 regressions, focused4 and11 SDK syntax paths pass
(WebGPU host branch only). See canonical-image-upload-report.yaml, logs/JUnit,
and RHI-RENDERDOC-TOOL-UNAVAILABLE capture diagnostic.

The v1 canonical helpers atomically report bit0 (word1); classifier fault words
are explicitly0 or1. Unknown bits reject correspondence. Clean consumer alone
never classifies a clean producer, and none of these value outcomes is a frame,
revision or completion ticket. Native exclusive image lifecycle has scoped c12d2b evidence below. Native fresh packet/consumer issuance,
actual closed draw/execution/completion, four-backend payload readiness,
RenderDoc/Platform Validated/Production Ready and full bindless plan are open.

### ExclusiveClosedConsumerImages/1 (c12d2b implementation contract)

Inputs are exactly three explicit authoring tuples: RGBA8 parent extent/depth1,
legal mip count, selected one-mip/layer0 view, declared selected extent, aligned
row pitch, exact rows and full pitched bytes. No resource ID/native object or
external mutable texture is an accepted input. Shared preflight validates
source/snapshot/command count before native allocation, using the existing image
payload and mip helpers. Root retains the original validated closed geometry,
its original issuer/registry/native device/quota and newly core-owned parent/view/
snapshot objects. The private root returns only after real upload completion.
No ordinary texture can be retroactively promoted into this domain.

Single-command upload is11 operations: three ShaderRead→CopyDst dependencies,
one transfer begin, three exact uploads, one transfer end, three explicit
CopyDst→ShaderRead dependencies. All snapshots remain owned during recording;
the staging workload contains their summed bytes and uses caller-supplied backend
pool bounds. Cold native validation may explicitly drain the queue; hot root
validation/consumer admission cannot wait. Root creation failure is deterministic
and rolls back unsubmitted resources; accepted work keeps existing native
completion-owned transfer storage until real retirement. Upload Pending/Failed/
Abandoned cannot publish readiness. Readback is test evidence, never issuance
input or CPU replay. No shader/draw occurs here.

After actual native command Completed, before IDs escape, issue monotonic
immutable revision markers through a construction key private to the concrete
root. Parent and original view native payloads retain the same original marker;
future views inherit it from the actual parent. Marker owns no native object
cycle and cannot be unsealed. Registry observations obtain it from original
native payloads, so unregister/re-register cannot discard the seal and retained
views cannot bypass it after parent retirement. Hot validation checks marker
control blocks, exact view/parent/subresource/revision values, original snapshot,
wrapper tickets/native allocation owners and original weak issuer epoch.

Ordinary buffer→texture, staging upload→texture and texture→texture destinations
must reject a marker before recording/count/staging/native mutation. Storage
texture descriptors and color/depth/resolve attachments also reject it; render
clear therefore cannot write. No mipmap/standalone texture clear API is present;
future write APIs must consume the same marker gate. Readonly sampling/readback
is allowed through its existing independent state/lifetime contract, and an
incorrect resource state rejects root validation. Usage checks remain required
for all ordinary resources and never substitute for the marker gate. The backend
private native write entry checks it independently of the L2 adapter.

Bounds comprise shared source/snapshot11-operation limits, actual summed texture
allocation charge, core CPU quota and exact complete owner projection: original
geometry root/projection, registry/device/quota, and per image parent/view wrapper,
snapshot/marker plus both independent native ownership graphs. No guessed owner
count, staging/default budget or fallback texture is allowed. Exact/one-less
CPU/source/snapshot/GPU/owner tests and all destination escape/alias/registry/
issuer/retirement negatives precede consumer integration.

Null is ValidatorOnly for the authoring values. Vulkan/WebGPU private immutable
image payload is DiagnosticFailure. Metal is explicitly opted-in Prototype cold
upload/seal only after the native checks above; every closed consumer draw
lowering remains non-executable. This root may prove immutable native content
for future fresh consumer issuance, but cannot promote existing storage-only
packets, manufacture a public plan or release packet/fault leases. Status is Contract Complete / Prototype Complete, with scoped Backend Validated
for this cold Metal lifecycle; consumer issuance/execution remains open.


### c12d2b verification and bounded scope

Quick shared Native/Emulated value tests, Null hidden-frame and real Metal native
lifecycle pass;54 targets/89 regressions and14 SDK syntax paths pass (WebGPU host
branch only). Eight source-mode/endian/fault cases use fresh parent7x5 mips3,
selected extents7x5/3x2/1x1, pitch256 and total retained snapshots2048. Reported
texture charge504, original projection90 slots, measured factory cold CPUpeak26320.
Native upload11 operations has real Completed before seals escape; each readonly
5-operation poisoned readback matches every texel/channel after authoring reuse.
All ordinary/native destination escapes, storage descriptors and color/depth/
resolve clears fail before encoding. Future views inherit the marker; registry
re-registration and parent retirement cannot discard a retained view seal.
Original control-block/native owner/state/registry/issuer mutations reject;
exact/one-less CPU/GPU/source/snapshot/commands/owner checks and partial-creation
rollback pass,100 exhausted-quota/null ambient PMR hot checks allocate0, final
scoped CPU0 and command leases0. External mutable textures remain unsealed.

The root requires its explicit texture budget not exceed the original reported
device capacity and requires the per-parent base extent and conservative
mip-count representability proof before reusing the existing size estimator.
Nonrepresentable/oversized descriptors reject with the texture-budget diagnostic;
no guessed native footprint or fallback is produced. This is cold queue-drain
preparation only. No asynchronous factory publication API or separately delayed
factory instrumentation is claimed. Pending/Failed/Abandoned never publish a
root; accepted upload storage uses the existing real native retirement path.

Evidence: exclusive-images-report.yaml, focused/regression logs and JUnit,
SDK syntax log and RHI-RENDERDOC-TOOL-UNAVAILABLE capture diagnostic under
build/mac-metal-debug/artifacts/bindless-prepared-graphics. Contract Complete /
Prototype Complete and scoped Backend Validated only; no consumer draw,
RenderDoc/Platform Validated/Production Ready or full bindless acceptance.
Continue c12d3 fresh original private issuance, then c12d4 native execution.

### FreshClosedConsumerPacket/1 (c12d3 implementation contract)

The existing PublicConsumer and PrototypeStorageOnly issuance purposes are
immutable creation values. Add a distinct PrototypeClosedConsumerCold purpose,
issued only during fresh construction by the private closed-consumer root;
there is no purpose mutator or promotion API. Public validate/consumer/recording
leases reject both Prototype purposes with distinct stable diagnostics. Private
cold storage has no successful/executable public lowering plan or authorization.
Unknown purpose values fail closed. Shared eligibility continues to reject every
old storage-only packet; a fresh private cold packet can participate in structural
38-operation admission only, which grants no native draw or write authority.

Cold factory input is the original validated ExclusiveClosedConsumerImages/1
root, an original same-source queried producer pipeline and the original packet
pool/device/registry/quota. The factory validates complete source, geometry,
consumer pipeline and immutable image revisions before allocating any packet;
no existing packet/generation, external mutable image or caller-authored native
proof is accepted. Reuse the common packet storage algorithm with an explicit
creation purpose, full budget/count/byte leases, original weak issuer epoch and
fresh monotonic generation. Initialize suppressed physical args and invalid
mapping exactly once and seal all host/ordinary GPU writes. Retain original
native allocation/control blocks and all owners in a complete bounded projection.

Reuse original producer and consumer cold correspondence builders, shared
fault-domain creation and metadata codecs. Both fault generations must be
fresh, distinct and independent of the new packet generation. Consumer metadata
must reproduce the original three immutable image inventories and consumer fault
generation. Producer parameters are the new packet's exact original buffer;
producer output scratch remains distinct from protected packet destinations.
A second construction always creates another packet and both fault generations;
no shared completed bytes or readback is an issuance input. All resulting objects
remain cold preparation, with no submission, native completion, descriptor/draw
activation or lease release. Hot validation/admission performs no allocation,
wait or IO; cold pipeline/resource creation remains explicit and budgeted.

Null is ValidatorOnly for purpose/eligibility/storage values. Metal has explicit
Prototype cold creation only after the private immutable-root factory and tests
exist. Vulkan/WebGPU have deterministic private-payload-missing failures. All
public packet/storage/processor/consumer capability versions stay0. c12d3a
establishes shared purpose, public exclusion and four-backend values; c12d3b
implements and validates native cold ownership, exact/one-less CPU/GPU/pool/
projection budgets, partial rollback, substituted original owners/state/metadata/
registry/issuer, independent generations and100 exhausted allocation0 checks.
Only c12d4 may add completion-owned private writes and actual38-operation draw.


c12d3a status: Contract Complete / Prototype Complete for shared immutable
purpose/public-exclusion values only.54 targets/89 regressions, focused4 shared/
Null/native guard regressions and15 SDK syntax paths pass (WebGPU host branch).
All four and unknown backend/version0,1,2/opt-in matrices reject executable use.
Unknown purposes and purpose/plan inconsistency fail closed. No private cold
native construction entry has been added yet; no Backend/Platform Validated or
Production Ready claim for c12d3. Evidence fresh-packet-purpose-report.yaml and
logs/JUnit/capture diagnostic. Continue c12d3b actual immutable-root-bound factory.


### c12d3b verification and remaining recording ownership

Contract Complete / Prototype Complete; scoped Backend Validated for actual
fresh Metal private cold allocation/correspondence only. Four Emulated scalar
source/endian/fault cases pass; four Native-array producer cases reject with
RHI-METAL-ZERO-BASE-PIPELINE-SCALAR-PACKAGE-REQUIRED. The factory consumes original
validated immutable image and same-source producer pipeline roots, reuses the
existing storage/cold/fault/metadata algorithms and freezes a new
PrototypeClosedConsumerCold purpose at creation. New charge244=packet52+
producer140+consumer52, consumer metadata44, original complete projection262
slots and cold CPUpeak37528. Exact suppressed/invalid args/mapping/parameter and
metadata bytes, independent original generations, host/ordinary GPU write seals,
exact/one-less CPU/GPU/root and component owners, pool count/resident/partial
rollback, original source/control-block/native/registry/metadata/issuer negatives
and100 exhausted allocation0 validations pass. Factory submits0 native commands;
final pool0/bytes0/scoped CPU0/command leases0.54 targets/90 regressions, focused4
quick/Null/native and16 SDK syntax paths pass (WebGPU host only). Evidence
fresh-cold-issuance-report.yaml, logs/JUnit/syntax/capture diagnostic.

This root proves cold immutable input/packet ownership, not c12d1 original shared
recording admission. Native geometry content and consumer vertex layout remain
inline subobjects; they cannot be passed as allegedly canonical independent
shared roots via an alias control block. c12d4a must give these values genuine
core-owned shared identities and test the original native fresh packet through
shared admission before any native38-operation recording. c12d4b adds exclusive
completion-owned packet and both fault leases plus actual source-derived GPU
transfer/indirect draw/readbacks. All public versions, old storage exclusions,
consumer execution gates, RenderDoc/Platform Validated/Production Ready and full
bindless acceptance remain separate and open.

### FreshClosedConsumerRecordingAdmission/1 (c12d4a implementation contract)

Native geometry content and consumer vertex layout must each be independently
created with core::makeShared and contribute their original control blocks to
all affected projections. Preserve the existing const reference accessors and
add const original shared-owner accessors. Validate pointer plus control-block
identity before dereferencing. Inline subobject aliases are not admissible as
canonical standalone values. This ownership correction changes exact native
projection counts and CPU factory peaks, not shader bytes or vertex semantics.

The cold admission preparation consumes the original fresh private issuance
root and an original empty, unsubmitted same-issuer native command with original
bounded retention storage and CPU quota. Reuse c11's shared producer-input builder
and c12d1 CanonicalClosedConsumerRecordingAdmission. Create exactly two original
GpuToCpu CopyDst readbacks,20+16 bytes, seal host writes, retain their native
allocations and prove their complete original identities. Existing producer and
consumer fault readbacks contribute4+4, so structural readback accounting is44.
Original geometry/layout/storage/metadata, source/packages/packet/fault domains,
immutable snapshots/revisions and exact inventories must reproduce the existing
26-reference shared admission and38-operation protocol values.

Preparation grants no packet/fault recording lease, privileged writer, command
encoding, submission/completion or draw. Native command ownership is weak and
must reject command retirement, mutation, finish/submission/abandon/poison or
foreign retention/quota/registry/device. Candidate creation is cold and bounded;
hot validation and shared-admission observation allocate0 and perform no wait/IO.
Root retains the original issuance and all children, shared26 references and
both independent readback wrapper/native graphs in an exact bounded projection.
Exact/one-less source CPU/GPU/owner/contract budgets, substituted root/source/
geometry/layout/snapshot/revision/buffer/native/registry/command owners, issuer/
command retirement and100 exhausted validations must pass before c12d4b.

Null remains ValidatorOnly. Metal is explicitly Prototype cold admission only;
Vulkan/WebGPU deterministically fail private-payload absence. Consumer execution
and every public version remain closed. c12d4b must separately consume original
completion-owned packet and both fault leases into a private38-operation command.


### c12d4a1 original shared inputs verification

Contract Complete / Prototype Complete for the structural input bridge only.
Geometry content and consumer vertex layout now have genuine independent core
shared identities and participate in exact projections. Current pipeline45,
geometry52, images92 and fresh cold265 slots supersede prior graph counts for
current code; historical stage reports preserve their actual earlier evidence.
Geometry factory coldPeak3144, image factory26320 and fresh cold37528. Native
readbacks36 use existing ManagedReadback lowering and actual tracked-buffer
queries; fault readbacks8 yield shared44. Original c11 input builder feeds c12d1
admission with original source/geometry/layout/metadata/snapshots/revisions and
fresh private packet/fault generations. Four Emulated cases pass; four Native
array producer cases still refuse. Shared references26 and protocol38 remain
unchanged. Readback factory coldPeak1675, exact/one-less CPU/GPU/shared budgets,
alias/native registry/runtime/storage/device/revision/recording negatives and
100 exhausted allocation0 checks pass.54 targets/91 regressions, focused7 quick/
Null/native and17 SDK syntax paths pass (WebGPU host only). Evidence
fresh-recording-inputs-report.yaml, logs/JUnit/syntax/capture diagnostic.

The bridge emits structural values and grants no native command authority.
After a native command is abandoned/retired, shared admission may still validate
these numeric values; that is not native liveness/completion evidence. c12d4a2
must own and freeze original readback/native observations and a weak original
command/retention/registry/device/quota binding before c12d4b consumes exclusive
packet and both fault leases. No native encoding/submission/draw occurs here;
public versions and old storage-only exclusion remain unchanged. RenderDoc,
Platform Validated, Production Ready and full bindless acceptance remain open.

### ClosedConsumerCommandAdmission/1 (c12d4a2)

Contract Complete: the existing closed shared admission is preparation only.
A backend-private cold root owns two original 20/16-byte readbacks and all26
shared reference roles, freezes their native allocation/device/object/hazard
observations, and weakly binds the original native command, buffer/device/stats
control blocks, retention address, recording identity, registry and CPU quota.
The command must remain original, unsubmitted, unfinished, unpoisoned and
unabandoned with recording retention under the original issuer/quota. The
readbacks remain sealed against host writes, CopyDst, independently allocated
and disjoint from original producer/consumer/packet resources. Validation
reuses the existing source, image, cold and shared admission contracts.

Explicit prototype opt-in and exact bounded GPU bytes/child-owner roles apply.
All cold allocations use the issuer CPU quota. Factories roll back on failure;
hot validation performs no allocations, waits or IO. This root obtains no
packet or fault lease and records/submits no commands. It grants no completion
or public consumer authority. Original command retirement fails closed;
retaining the root must not retain the command. Native object substitutions,
issuer loss and budget/state/identity mismatch yield stable diagnostics.

Four backend conclusions remain those of ClosedConsumerRecording/1: Null is
structural validation only, Metal permits this explicit private prototype
preparation, Vulkan/WebGPU reject private execution deterministically. Public
capability versions stay0. Native38-operation encoding, completion-owned
leases, RenderDoc, platform and production acceptance remain separate gates.

Verified c12d4a2 evidence: Contract Complete / Prototype Complete; scoped
Backend Validated for original Metal command-bound preparation only. Readback
charge36 plus existing fault8=44, shared roles26 and full child roles302, cold
CPU peak16800. Original native identity/control-block/hazard/registry mutations,
exact/one-less CPU/GPU/owner/shared budgets, command finish/poison/abandon and
weak retirement fail closed.100 exhausted validations allocate0. Factories
encode/submit0 and acquire no packet/fault lease.54 targets/92 regressions,8
focused quick/Null/native checks and18 SDK syntax paths pass (WebGPU host only).
Evidence: `build/mac-metal-debug/artifacts/bindless-prepared-graphics/command-admission-report.yaml`
and its logs/XML. RenderDoc remains `RHI-RENDERDOC-TOOL-UNAVAILABLE`; no visual,
Platform Validated, Production Ready or full-plan closure. Continue c12d4b.

### ClosedConsumerReservation/1 (c12d4b1)

Contract Complete: private cold reservation is issued only from the original
command-bound admission, with explicit opt-in/version and child-owner budget.
It uses the same embedded exclusive packet recording capacity as the public
packet lease, through a core AtomicCapacityLease whose alias owner is the
original packet. It separately acquires the original producer and consumer
fault submission leases. Acquisition is transactional: all partial capacities
roll back before command publication. StorageOnly purpose and successful public
plan substitution are rejected by the original cold issuance/admission. Public
BindlessIndirectPacketRecordingLease keeps rejecting both prototype purposes.

The original command owns the reservation; the admission weakly observes it.
Cold command destruction/abandon/poison cancels reservations even when a caller
retains the preparation object. No caller API releases reservations or asserts
completion. Recording/submit/finish remain closed with a stable payload-missing
diagnostic until the fixed38-operation encoder and real completion owner exist.
This stage neither records GPU work nor publishes Native completion evidence.
Reusing the shared lifetime contract allows only Cold->Abandoned/Failed here.

Metal: explicit Prototype cold reservation only. Null: ValidatorOnly shared
values, no native reservation. Vulkan/WebGPU: DiagnosticFailure private payload
missing. Public capability versions remain0. CPU allocation uses the issuer
quota; exact/one-less CPU/child roles and exhausted allocation0 hot validation
are required. No new GPU allocation beyond original admission/readbacks.

Verified c12d4b1: Contract Complete / Prototype Complete; scoped Backend
Validated for original Metal cold command-owned reservations only. Child roles307,
combined command/admission/reservation CPU peak37160, incremental reservation
GPU0. Exact/one-less CPU/owner, transactional partial fault acquisition, packet
contention and command destruction/poison/abandon (caller retaining terminal root)
pass.100 exhausted validations allocate0. Finish/ordinary encoding fail closed.
54 targets/93 regressions,9 focused quick/Null/native and19 SDK syntax paths pass
(WebGPU host only). Evidence `build/mac-metal-debug/artifacts/bindless-prepared-graphics/closed-reservation-report.yaml`.
No GPU encoding/submission/completion, public capability, storage promotion,
visual/platform/production/full-plan acceptance. Continue c12d4b2.

### Canonical producer prefix reuse (c12d4b2a)

Contract Complete: the c11 fixed protocol remains15 operations. Its first10
operations (fault clear/dependency, compute dispatch, three write-to-copy-source
dependencies) have identical meaning and original resources in the c12 fixed38
protocol. A backend-private encoder may implement this existing common prefix
once, taking original cold bindings, command buffer, bounded original encoder
slots and state/sequence callbacks. It must not change native barriers, thread
group shape, buffer slots, fault ownership or the ordering of deferred readbacks.
The c11 path still consumes its original complete15-operation sequence and
completion-owned fault lease. This refactoring introduces no new L2 capability
and does not enable the c12 consumer. All four shared lowering conclusions and
public capability versions remain unchanged. Actual native c11 output/completion
and quick/Null regressions are required; source syntax alone is insufficient.

Verified c12d4b2a: Contract Complete / Prototype Complete; Backend Validated
for unchanged real c11 producer15 through the reusable native prefix10. Original
barriers/encoder slots/buffer ABI/dispatch and deferred readbacks remain intact.
Source-derived output/fault and actual delayed completion/drop-owner/transactional
regressions pass.54 targets/93 regressions,8 focused quick/Null/native tests and19
SDK syntax paths pass (WebGPU host only). Evidence
`build/mac-metal-debug/artifacts/bindless-prepared-graphics/producer-prefix-report.yaml`.
No new c12 draw/38-operation authority, public capability, visual/platform/production
acceptance or full-plan closure. Continue c12d4b2b.

### ClosedConsumerTargetPreparation/1 (c12d4b2b)

Contract Complete: authoring explicitly supplies positive 2D extent, finite
viewport/scissor inside that extent, normalized finite clear RGBA and a bounded
byte budget. Shared validation reuses the existing dynamic viewport/scissor
classifier without allocating diagnostics. The sole output profile is a fresh
GpuOnly RGBA8Unorm texture with RenderAttachment|CopySrc, one mip/layer/sample,
initial ColorAttachment, clear/store attachment without resolve, explicit dynamic
viewport/scissor. Pixel-byte arithmetic is overflow checked against the budget;
native reported allocation must also fit. No native defaults select these values.

Lowering: Null validates values only; Metal allows an explicitly opted-in private
cold target/native descriptor and bounded original state journal/encoder slots;
Vulkan/WebGPU reject private preparation deterministically. All are non-executable
and public capabilities stay0. The target owns no production consumer authority.

The backend root retains the original command-owned reservation, fresh genuine
target wrapper/native allocation/device/object, original render descriptor, core
mutation journal and four initially empty external encoder slots (clear, compute,
blit, render). Validation checks native render descriptor load/store/clear/texture
and pipeline format/sample compatibility, registry/resource/native/control-block
identity, target state, issuer and budgets. Cold failure rolls back all new
allocations; hot observation is allocation0. No GPU encoding/completion is issued
until the fixed38-operation protocol explicitly consumes these original owners.

Target preparation additionally requires explicitly selected Metal texture hazard
tracking and an actual tracked independent-texture query. Native descriptors
select `MTLHazardTrackingModeTracked`; native observation verifies device,
kind, dimension/format/usage/storage and actual hazardTrackingMode. Missing API
availability yields `RHI-METAL-TRACKED-TEXTURE-NATIVE-API-UNAVAILABLE` for this
private target profile. No default mode supplies proof. Vulkan keeps explicit
barrier intent, WebGPU uses validated resource usage/scopes and Null validates
state only; no four-backend public synchronization capability is added. Original
authored target values are frozen in shared facts, including viewport/scissor.

Verified c12d4b2b and parent b2: Contract Complete / Prototype Complete;
scoped Backend Validated for real original Metal cold target/native descriptor
and bounded encoder ownership only. Explicit target16x8 RGBA8 charge512, child
roles319, cold CPU peak19152. Shared frozen authored input, existing dynamic
viewport/scissor and render-pass identity validation, explicit standalone tracked
texture creation and actual native tracked-texture observation are tested.
Exact/one-less CPU/GPU/owner/target budgets, descriptor/native/control-block/registry/
state/issuer negatives and100 exhausted allocation0 checks pass. Original
preallocated completion storage is present; spoofed CPU completion neither
authorizes recording nor releases leases.55 targets/95 regressions,11 focused
quick/Null/native and22 SDK syntax paths pass (WebGPU host only). Evidence
`build/mac-metal-debug/artifacts/bindless-prepared-graphics/consumer-native-preparation-report.yaml`.
No c12 GPU encoding/submission/completion, public capability, visual/platform/
production acceptance or full-plan closure. Continue c12d4b3.

### ClosedConsumerStorageObservation/1 (c12d4b3a)

Contract Complete for logical values: cold admission capture remains mandatory.
The closed consumer has a distinct explicit `ReadbackReady` storage observation:
producer scratch arguments/mapping and latch are CopySrc, protected packet
arguments/mapping are ShaderRead, consumer latch is CopySrc, and all four
readbacks stay CopyDst. Metadata, parameters, geometry and images retain their
original immutable ShaderRead contracts. Unknown phases and incomplete state
transitions fail deterministically with cleared output facts. ShaderRead here is
the existing L2 buffer state; no new indirect state or public capability is added.

The original c11 `ReadbackReady` keeps its existing cold packet/mapping/consumer
latch semantics. A separately named closed-readback validation reuses the same
producer validation and frozen original owner/identity logic, without broadening
c11 or recapturing an admission from final states. Legacy storage-only packet
purposes remain rejected. Null can observe these logical transitions only; Metal
must separately prove original native resource/command/encoding/completion
correspondence. Vulkan/WebGPU retain deterministic missing-payload diagnostics.
Shared values never authorize execution, release leases or prove completion.

Verified: four source/endian cases in each package profile, actual Null journal
transitions, partial-state/unknown-phase/owner/revision/cold-recapture negatives,
preserved c11 Ready and100 exhausted allocation0 observations.5 quick/c11/Null
hidden-frame tests pass, evidence
`build/mac-metal-debug/artifacts/bindless-prepared-graphics/closed-storage-phase-focused-results.xml`.
No Backend Validated, Platform Validated or Production Ready acceptance from
these logical state tests. Continue c12d4b3b native resource observation.

### ClosedConsumerNativeStorageObservation/1 (c12d4b3b)

Contract Complete / Prototype Complete for original resource observation only.
Metal packet/producer/consumer/immutable-image issuance roots now expose a
separately named closed-readback observation that reuses original validators,
native allocation/device/object/control-block and registry observations. Their
public cold validation remains unchanged; c11 Ready retains cold packet/mapping/
consumer latch behavior. The closed packet observation requires the original
fresh private issuance purpose; legacy storage-only purpose is deterministically
rejected. It returns no command ticket or native completion authority.

The original input builder can observe this exact terminal graph and validate
the frozen cold admission. Production recording must still prove the weak
original command and original journal/slots/sequence/retention, before finish or
submission. Logical final states alone are insufficient: this stage intentionally
keeps finish/ordinary recording closed and all three leases held, even when a
caller signals CPU completion storage. A real Metal command38 and native callback
ownership remain required by c12d4b3c/d.

Verified four Emulated original Metal resource cases and four Native-array
producer refusals. Every partial state and intermediate packet CopyDst fails;
native/control-block/registry/owner and cold-recapture negatives, journal rollback
and100 exhausted allocation0 observations pass.55 targets/96 regressions,10
focused quick/Null/native tests and25 SDK syntax paths pass (WebGPU host only),
including genuine c11 source output/delayed completion/drop-owner regressions.
Evidence `build/mac-metal-debug/artifacts/bindless-prepared-graphics/closed-storage-observation-report.yaml`.
This resource test encodes/submits no new GPU work; it does not prove closed
consumer GPU execution, Platform Validated, visual or Production Ready acceptance.
RenderDoc remains `RHI-RENDERDOC-TOOL-UNAVAILABLE`. Continue c12d4b3c.

### ClosedConsumerPrivateRecording/1 (c12d4b3c contract)

The private intent explicitly supplies version1, Prototype opt-in, fixed38
command units, readback bytes44 and shared reference slots26. A separate private
lowering selects Null ValidatorOnly, Metal Prototype, and Vulkan/WebGPU
DiagnosticFailure; unknown backend and invalid budgets fail closed. Public
capability versions and public non-executable lowering remain unchanged.

The native executor accepts only the original fresh issuance, command-bound
admission, reservation and prepared target/journal/slots. It validates native
command/device, exact resource/binding/pipeline/geometry/indirect correspondence,
preflights the complete shared38 sequence and full immutable native retention
projection before encoding. It then reuses producer prefix10, prepares/copies
scratch output into the original protected packet, establishes TransferWrite to
IndirectCommandRead/vertex ShaderRead dependencies, clears the original consumer
latch, binds the original render pipeline/buffers/images/geometry/index and
explicit viewport/scissor, performs one actual indirect draw, and schedules four
original readbacks only after FragmentShader-write to TransferRead dependency.
Managed readback synchronization follows the existing queried memory policy.

The direct indexed-indirect API is the existing `IndirectDraw` native substrate,
derived in cold command preparation from the version-controlled Metal capability
contract and guarded by SDK API availability. Recording consumes that frozen
decision without rebuilding the dynamic capability contract. The pipeline's
`supportsIndirectCommands` flag describes indirect command buffers (ICB), a
separate optional feature; this direct draw neither enables nor consumes ICB.

Successful recording is Cold to Recorded with exactly38 consumed operations,
all native encoder slots empty and original journal still pending. Finish verifies
the original Recorded root/command/sequence/native identities and seals retention.
Preflight rejection performs no encoding or state/owner mutation; any rejection
after encoding is terminal, closes native encoders, rolls back original journal,
and releases all three leases. Recorded-command destruction/abandon/poison does
the same, even with caller-retained roots. Submit remains fail-closed until the
completion-owned original root/leases are implemented and verified by c12d4b3d.
CPU completion or logical final states alone cannot grant recording or retirement.

Verified c12d4b3c: Contract Complete / Prototype Complete; scoped Backend
Validated for genuine Metal native38 encoding and bounded recorded-command
validation only. Raw native projection roles320 deduplicate to154 retention
owners; combined cold factory CPU peak55072, exact/one-less CPU/native owners,
aliases/binding/foreign-journal preflight and recorded abandon/destruction/finish
corruption/closed-submit rollback pass. Recording and finish under exhausted
quota/default PMR perform allocation0.4 Emulated source cases and4 Native-array
producer refusals;55 targets/97 regressions,12 focused quick/Null/native tests and
25 SDK syntax paths pass (WebGPU host only), including actual c11 output and
delayed completion regression. Evidence
`build/mac-metal-debug/artifacts/bindless-prepared-graphics/closed-native-recording-focused-results.xml`
and associated logs/XML/syntax. No new GPU submission/output/native completion,
public capability, visual/platform/production acceptance or full-plan closure.
Continue c12d4b3d.

### ClosedConsumerNativeCompletion/1 (c12d4b3d contract)

Submission preflights the same original finished Recorded command/root, all38
operations and three live original leases, and validates Recorded to Submitted
before any commit. The preallocated native SubmissionRetention retains the
original prepared root; that root exclusively owns the original reservation and
packet/producer/consumer leases. These objects transfer to callback ownership as
one graph, with no replacement lease, CPU replay or second consumer command.
Only after all pre-commit checks succeed does the executor mark Submitted and
consume the original producer generation. Earlier admission or native wait/signal
failure poisons the uncommitted recording, rolls back the journal and releases
all three reservations. The generation cannot be reused after actual submission.

The actual Metal completed handler proves native success/failure, validates the
Submitted to Completed/Failed transition, marks both fault readbacks complete
only on native success, and releases all three leases exactly once. It retires
the complete original retention graph before publishing completion. Caller-held
roots/commands may be dropped while a real native event delays execution; CPU
completion storage cannot release the submitted leases or mark the generation
complete. A local committing root keeps wrappers alive through owner-thread
journal commit even if native completion races commit. Recording-only references
are cleared afterwards. Source-derived scratch/packet/mapping/fault numerical
checks require actual native completion; Null logical state is never evidence.
Any subsequent read-only target validation must occur after completion and
must not bridge or replace this single-command producer/consumer protocol.

### ClosedConsumerNativeCompletion/1 verification checkpoint (2026-10-02)

Status: Contract Complete / Prototype Complete; scoped Backend Validated for
selected private Metal native38 execution and native success retirement. The
assertion-enabled Metal API Validation fixture performs20 genuine native38
submissions across Little/Big source byte order and clean/FaultSuppressed
outcomes: four event-delayed root/command retention combinations and one
exhausted-quota no-wait submission per case. Scratch and original protected
packet arguments/mappings match the source-derived oracle; producer fault0/1
and consumer fault0 match their independent outcomes. The original completion
callback owns all three leases until native success. CPU completion spoofing
cannot retire them. Genuine completion releases the original graph before
publishing its result; consumed generations cannot be prepared again.

A separate ordinary texture-to-buffer read-only command runs after native38
completion. The fixture's interior pixel(11,3) equals the source image's last
logical instance texel(0,0), including alpha, or the explicit target clear for
suppressed draws. This checks a numerical GPU output and does not substitute
for RenderDoc visual acceptance. Missing completion, corrupted finished input
and native wait-owner allocation failure reject before submission with journal
rollback. Recording/finish/no-wait submission add zero core allocations under
exhausted quota. Deduplicated native retention remains154 owners; the added
preallocated completion-root field raises combined cold CPU peak to55088 bytes.
No new shader or altered matrix-layout contract is involved.

55 targets,98 regressions,13 focused quick/Null/native checks and25 SDK syntax
paths pass; WebGPU syntax covers the host branch only. Evidence is under
`build/mac-metal-debug/artifacts/bindless-prepared-graphics/closed-native-completion-*`.
Native GPU error injection was not performed: native success payload and
precommit failures are validated, while Submitted→Failed still requires actual
native completion proof. RenderDoc tools are unavailable on this macOS ARM64
host (`RHI-RENDERDOC-TOOL-UNAVAILABLE`), so no capture/screenshot, visual acceptance,
Platform Validated or Production Ready claim is made.

The next public adapter boundary remains the original device-issued Native
Graphics authorization/snapshot/typed owners and its capture gate. A private
Emulated scalar package, this Prototype root, successful target sample or CPU
completion object cannot publish public Graphics capability. Null stays
validator-only; Vulkan/WebGPU closed private payloads deterministically fail.
All public versions remain0 and legacy PrototypeStorageOnly packets remain
non-promotable. Renderer/WOCC/performance and four-backend/platform acceptance
remain outstanding.
