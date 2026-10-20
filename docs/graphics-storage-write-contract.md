# GraphicsStorageWrite/1

Status: Contract Complete for the initial bounded intent, validation and
fail-closed lowering. Selected-binary proof and owning immutable preparation are
Prototype Complete, with scoped Metal preparation observation Backend Validated;
native barrier consumption and writable execution remain incomplete.
No writable Backend Validated, Platform Validated or Production Ready
claim follows from this contract or Null validation. Public
CommandRetentionBudget and MetalPreparedGraphicsBinding versions remain zero.

`FragmentAtomicWriteProof/1` implements a cold, allocation-free binary verifier.
It reuses the core unsigned element view through a reusable SPIR-V instruction
framing view; unaligned input is valid and malformed headers/framing fail.
The initial accepted compiler shape is deliberately branchless: one selected
Fragment entry, one function/block, one constant-address atomic exchange, one
literal float4 output store and return. Additional inputs/control flow/calls/
storage operations are unsupported. It checks exact descriptor decorations,
Block/memberOffset0/runtime uint32 array stride4, pointer/member/index0,
uint32 literal replacement, Device scope and Relaxed semantics. Every used id
must have one definition; conflicting decoration, storage store, old-value use,
unknown opcode/type or extra variable/function rejects. Failure exposes no
partial facts. Supplied entry name is matched to the binary entry; debug names
are irrelevant. The value-only proof result grants no native execution.

The built tool `arp_fragment_atomic_write_verifier` consumes an explicit file
byte limit and selected entry and emits YAML facts. The compiler uses this same
C++ verifier with the exact generated file extent. Runtime artifact loading
recomputes the proof from hash-sealed SPIR-V; it cannot trust an author assertion
or rehashed replacement that fails proof. There is no parallel Python proof
algorithm. Shader/entry/binding/value ownership remains a native preparation
obligation before execution can open.

Slang 2026.17 actually rejects Fragment InterlockedExchange for WGSL with
E36107. `WgslGraphicsAtomicDiagnostic/1` is a separate explicit publication
policy: the compiler must prove FragmentAtomicWriteProof/1 and selected
authoritative storage binding metadata, then receive only classified E36107
errors. Other errors remain fatal. Publication v2 names
SHADER-WGSL-GRAPHICS-ATOMIC-UNSUPPORTED with no WGSL path/hash/bytes; actual
SPIR-V/MSL/source/reflection remain hash sealed. The loader recomputes proof,
matches Fragment/scalar StorageBuffer/binding metadata and requires matching
Unavailable reflection, empty WebGPU maps and null WGSL target. Stale WGSL is
removed and cannot grant availability. v1 still requires all five artifacts;
WgslBufferArrayDiagnostic/1 remains separate. No ignored-capability compilation
or manufactured target is allowed. WebGPU bounded atomic execution stays
DiagnosticFailure; emitted MSL and SPIR-V are not native execution evidence.

The first supported semantic domain is
`FragmentFixedUint32AtomicExchange/1`: one scalar ordinary StorageBuffer
descriptor, visible to Fragment only, non-table, non-dynamic, binding offset0.
The selected fragment entry atomically exchanges uint32 element0 with one
literal uint32 value. No shader output, control flow, address or other memory
operation may depend on the returned old value. Atomic exchange makes duplicate
or concurrent fragment invocations well defined and independent of their order.
Empty coverage may leave the buffer unchanged; the contract never invents a
fragment invocation. Non-atomic stores, nonconstant addresses/values, other
writers, buffer arrays, vertex writes, writable textures, indexed/indirect draws
and multiple instances are outside v1 and must fail closed. Direct nonempty
draws with one instance remain subject to ordinary pipeline/pass validation.

`GraphicsStorageWriteContract` owns the existing DescriptorConsumptionContract
projection and the value-only footprint. It validates exact reflection/layout
binding/type/stage/cardinality, byte4 minimum physical range, zero offsets and
the draw domain. These supplied values are intent, not proof. sizeof(T), an
author annotation, a matching source name or a caller-provided proof boolean
cannot authorize execution. Cold preparation must prove the *selected binary*
implements this domain and freeze its artifact hash/entry/stage/binding/value
alongside the independently queried native ABI. Every matrix-free Slang fixture
must emit actual SPIR-V/MSL/WGSL/reflection under the existing sealed publication
contract, or use only an applicable explicit target-diagnostic policy.

The binary proof must trace the atomic pointer from the exact selected-entry
StorageBuffer variable with DescriptorSet/Binding, struct member offset0,
runtime-array uint32 element stride4, and constant member/index0. It must verify
the literal replacement, atomic uint32 type, valid device storage semantics,
absence of other storage writes/atomics, and absence of any observable use of
the old value. Unknown pointer aliasing, calls, indirect access, unsupported
instructions, missing/conflicting decorations or unproven control flow reject
proof. This is a deliberately small, versioned acceptance language; broadening
it requires contract and negative-test changes before native lowering.

Producer access is exactly the four-byte range of the canonical resource,
ShaderWrite state, FragmentShader stage, Fragment visibility and
ShaderRead|ShaderWrite access (an exchange is a read-modify-write). Reuse
ResourceAccessScope, validateBindlessBufferAccess and bounded
BindlessRetainedDeclarations(GraphicsPass); ordinary writes do not borrow the
bindless fault-domain/latch authority. The complete canonical descriptor owner
mapping must forbid the writer resource aliasing any other producer descriptor
or writable attachment. Duplicate declarations for the writer reject; enlarged
or stage/access-inexact declarations cannot widen the shader footprint. Seals
are monotonic: a GPU write-sealed buffer cannot become a writer. Host writes must
be sealed before publishing preparation, with exact issuing registry/device,
logical id, owner/control block, native identity and write-seal token captured.

Consumers must occur after the producer pass/encoder ends. v1 allows only exact
four-byte shader reads (ShaderRead state and matching shader stages) or transfer
reads (CopySrc state, Transfer/TransferRead). The explicit ResourceBarrierDesc
must match canonical resource, before/after states and exact source/destination
stage/access. Validation reuses ResourceMemoryBarrier/1 and ResourceAccessScope;
it does not certify that a dependency was recorded. Same-pass feedback,
CPU-immediate completion, indirect reads, implicit state changes or a
stats-only barrier cannot satisfy the contract. Existing recorded resource-state
machinery must enforce the actual sequence; no duplicate state tracker belongs
in a fixture or ordinary binding adapter.

Native preparation must own the shader proof, pipeline/provider, immutable
declarations, canonical descriptor/buffer/seal owners and actual hazard facts.
Use existing owner projection, bounded retention and core quota accounting.
Reserve metadata at preparation from explicit cardinality; no guessed CPU-byte
charge or unlimited growth. Count/byte budgets for argument tables and complete
owner unions stay independent. Accepted recording must not allocate, wait, scan
shader binaries, upload, perform IO or compile/create pipelines. Admit the full
owner union and dependency payload before native/state mutation. Rejection
before mutation is retryable with corrected preparation/budgets; rejection after
mutation poisons the recording. Open/sealed abandonment retires unsubmitted
owners; real native completion retains all original replaced owners through
completion with command wrappers retained or dropped. Owning diagnostic strings
retain their separately documented rejection-allocation limitation.

| Backend | Required eventual mapping | Current bounded conclusion |
| --- | --- | --- |
| Null | Validate proof metadata, range/scope/sequence/owners only; never synthesize GPU data or completion | Equivalent, validator only; `RHI-GRAPHICS-STORAGE-WRITE-VALIDATOR-ONLY` |
| Vulkan | Queried fragmentStoresAndAtomics, actual selected SPIR-V atomic payload; exact shader read/write to consumer buffer memory dependency and queue completion | DiagnosticFailure, `RHI-GRAPHICS-STORAGE-WRITE-PAYLOAD-MISSING` |
| Metal | Actual selected MSL atomic payload and independent native access/alignment/span; explicit tracked hazard mode selected and queried for each independent writer/argument buffer; ended encoder followed by consumer with retained actual owners/completion | DiagnosticFailure, `RHI-GRAPHICS-STORAGE-WRITE-PAYLOAD-MISSING` |
| WebGPU | Actual WGSL atomic/storage binding and device limits; separate ordered render/consumer passes, exact usages and completion | DiagnosticFailure, `RHI-GRAPHICS-STORAGE-WRITE-PAYLOAD-MISSING` |

This table is the versioned implementation conclusion, not backend-name-based
capability discovery. GraphicsStorageWriteLowering::current never returns an
executable plan. Existing runtime writable/bounded barrier gates stay closed.
Independent Metal buffers now use MetalIndependentTrackedBuffer/1 below;
heap hazard evidence cannot replace this explicit selection/query. Native
creation or query failure produces diagnostics, never a dummy or assumed mode.

Quick contract tests cover immutable layout, exact/one-less ranges, invalid
domain/layout/offset/draw, exact producer scopes/aliases, missing/mismatched
dependencies, open producers, consumer negatives, bounded declaration capture,
quota ownership/exhaustion, accepted validation allocation0 and every backend
execution rejection. The existing Null hidden-frame smoke remains mandatory.
Before any native domain opens, tests must also prove actual atomic results and
producer/consumer visibility, untouched out-of-footprint bytes, native tracking,
complete owners/budgets/stale identities/seals, cancellation and real delayed
completion. Visual acceptance still requires AI RenderDoc evidence; native
readback alone cannot close that gate.

The c4k2b1 native query on this host independently reports direct Fragment
slot0, MTLBindingAccessReadWrite1, MTLDataTypeUInt33, alignment4 and span4.
Direct buffer captures now freeze access/type alongside existing slot/alignment/
span. The query-only atomic ABI validator rejects readonly/unknown/struct/array/
other-stage/missing/duplicate facts and short ranges; no pointee/readonly ABI is
inherited. The initial k2b1 query did not establish production hazard ownership;
the k2b2 explicit tracked factory below supplies that prerequisite. Native query tests
retain rejection of ordinary writable render begin and bounded buffer barrier
before encoder/draw/owner admission. Status: scoped Backend Validated for actual
ABI/query only; shader write data, visibility, lifetime and production hazard
ownership remain unaccepted.

`MetalIndependentTrackedBuffer/1` now implements the c4k2b2 creation prerequisite.
The same backend-owned helper creates independent ordinary resource buffers
and prepared function argument tables. It checks the real device maxBufferLength
and NSUInteger representation, selects complete validated Private/Shared/Managed
storage options plus explicit HazardTrackingModeTracked, then queries exact
length/storage and hazard mode. ManagedReadback's existing iOS lowering is
explicit Shared, preserving the CPU-visible intent; this host supplies no iOS
platform evidence. None/memoryless/unknown policy, zero/over-limit size, missing
device/API, allocation failure or query mismatch returns a stable diagnostic
and no native object/placeholder. The absent-runtime helper also fails. Native
resource observations freeze the explicit tracking fact in addition to existing
device/object/allocation owner identities; prepared table identity retains the
same actual queried tracked object. Creation remains cold and the existing
allocatedSize/table count/bytes and complete core quota charges remain separate.

Four-backend conclusions for this prerequisite remain explicit: Null validates
metadata only; Metal Native applies only to successfully created and queried
independent objects in this helper, otherwise DiagnosticFailure; Vulkan keeps
its existing explicit buffer memory-dependency mapping and does not consume a
Metal hazard fact; WebGPU keeps its existing ordered pass/resource-use mapping
and does not consume a Metal hazard fact. Bounded GraphicsStorageWrite execution
on all GPU backends remains DiagnosticFailure. Other native buffer factories or
heap/staging domains do not gain this prerequisite by implication.

C4k2 evidence: 37 rebuilt targets,55 regressions including shader/quick/Null
smoke,24 Apple SDK syntax checks and2 actual SPIR-V validations pass. Actual
native query confirms explicit tracked Private/Shared/Managed buffer creation,
policy/size/device failure and immutable observation; readonly storage/Uniform
array tests query every prepared argument table as tracked and retain exact/
one-less budgets, original owners, cancellation, delayed completion and data.
This is scoped Backend Validated for ABI/tracked creation/query and retained
readonly execution only. `ordinary-writable-tracking-report.yaml` records the
evidence. RenderDoc tools remain unavailable and diagnostic capture metadata
keeps visual/platform/Production gates open. Writable execution has not opened.

C4k3 audit: the generic bounded buffer barrier method still takes no canonical
resource or dependency payload and remains OPERATION-UNIMPLEMENTED. Tracking
success cannot authorize a stat-only barrier. Prepared writable consumption
must own the selected proof/artifact and exact ABI/scope, canonical issuer/
buffer/seal/lifetime and ended-producer consumer dependency. Reuse the existing
RecordedStateCommandBuffer, prepareMetalRhiRecordedState and state-for-barrier
helpers and complete immutable owner admission; do not duplicate a state map in
ordinary adapters/tests. Real producer-write/consumer visibility, guard bytes,
budgets, cancellation and delayed completion are still mandatory before private
execution. Public versions remain zero.

`HostBufferWriteSeal/1` exposes the existing monotonic host-only restriction as
an L2 method. Calling sealHostWrites prohibits GpuDevice::writeBuffer uploads
with RHI-BUFFER-HOST-WRITES-SEALED on Null/Vulkan/Metal/WebGPU. It neither seals
nor authorizes shader or transfer writes, changes state, signals completion,
nor undoes an existing GPU seal. sealWrites remains the stronger all-write
restriction. All four writeBuffer entrypoints check the host seal before native
work. Null validates this restriction only; GPU backends reject host uploads
without fabricating a native result. Shader descriptors still require their
existing validation/capability gate. Caller serialization of uploads versus
sealing remains required; the method does not introduce a concurrent upload
protocol.

`MetalOrdinaryGraphicsStorageWritePreparation/1` is cold preparation only.
The factory takes an existing ordinary prepared descriptor binding and an
explicit shared owner of the actual L2 GpuBuffer wrapper; the caller must finish
initialization and sealHostWrites first. Failed preparation never changes the
caller resource/state/seals. The root owns the wrapper (and its existing
embedded state-journal snapshot), GPU seal token, complete original binding/
provider/native owners, binary proof and immutable producer/consumer dependency
intent. Caller owner reset cannot unregister the resource while this root lives;
this is explicit shared ownership, not a synthetic raw-pointer lifetime ticket.
Descriptor arena/revision/lifetime observations remain mandatory.

The initial preparation subset requires exactly one scalar Fragment storage
binding in the provider reflection, authored set and native descriptor snapshot,
no push constants, one exact producer declaration, offset0 and a successfully
queried direct Fragment uint32 read-write ABI. Only the selected hash-sealed
Fragment SPIR-V is proved. A temporary L2 layout is reconstructed from the
provider-owned exact set contract for GraphicsStorageWriteContract validation;
no caller-authored replacement layout can grant authority. Native tracking,
issuer/device, physical buffer extent, usage, native/control-block identity,
GPU seal token/flags and stage mapping/ABI are frozen. Enlarged/duplicate/
wrong-resource declarations, mismatched dependency, unsealed host writes,
GPU-write seals, stale identity/metadata or unsupported domain reject.

Dependency preparation validates exact values with producerEnded=true only to
check its shape; it does not claim a producer actually ended or a barrier was
recorded. A future recorder must prove the sequence using the existing resource
state journal and atomically admit every owner before native mutation. The
root's observation validation intentionally does not scan binaries or require
the resource to stay forever in the producer state: ShaderWrite is required at
preparation and must be checked again by producer admission, while dependency
consumption must check the actual source state and ended encoder. GPU transfer
seal flags are frozen, and a later stronger seal invalidates the root.

Metadata objects/contract/declaration storage and the full immutable owner
projection use core allocation under the existing shared explicit quota. This
root reserves one declaration from the proven cardinality; no estimated byte
charge or hot vector growth. Accepted observation validation must allocate0
under exhausted quota and a hostile default PMR. Rejected diagnostics retain
existing string allocation limitations. Recovery is explicit fresh preparation
with valid owners/contracts and sufficient quota; no retry/fallback occurs.
Null remains validator-only Equivalent. Metal preparation observation is now
verified against actual payloads; writable execution remains DiagnosticFailure.
Vulkan and WebGPU have no preparation/execution payload for this subset and
remain DiagnosticFailure. Public versions remain0. Resource retention alone
cannot prove native completion, consumer visibility, visual/Platform validation
or Production Ready. Those remain c4k3b obligations.

C4k3a evidence: 38 rebuilt targets,56 regressions,final7 focused checks and26
Apple SDK syntax checks pass, including quick contracts and Null hidden frame.
The actual Metal preparation test proves selected proof/ABI/tracked physical
buffer observation, original control blocks, stronger GPU seals, descriptor
revision/arena reset/destruction, wrong owned wrapper, scope/dependency source
mutation, measured exact/one-less cold metadata quota and full owner union,
metadata cancellation and allocation0 accepted observation under exhausted
quota/hostile PMR. Preparation/failed generic recorder leaves all initial buffer
bytes unchanged. This is scoped Backend Validated for preparation observation;
it executes no native write or dependency and proves no writable delayed
completion. Report ordinary-writable-preparation-report.yaml and associated
logs/XML record the boundary. RenderDoc is absent and visual/platform/Production
acceptance remains open. C4k3b must consume the existing recorded state journal
and actual native producer/dependency/consumer payload; preparation cannot open
that path by itself.

C4k3b recorded-state prerequisite: reuse MetalRhiBuffer's embedded
MutationSnapshot<ResourceState>, prepareMetalRhiRecordedState and
setMetalRhiBufferStateForBarrier. One pending journal owns the snapshot; another
journal must fail before state/native mutation. A journal ownership move must
preserve that conflict. Unsubmitted destruction/rollback restores the initial
state; commit detaches it and a later journal may record. Existing L2 encoder
ownership transfers to RecordedStateCommandBuffer, and Metal queue calls
acceptRecordedCommandOwnership only after submit succeeds. A private recorder
must borrow this exact journal or explicitly transfer its ownership into the
finished command; a second parallel resource-state map is prohibited.

K3b1 tests manipulate only this existing metadata substrate on a real created
Metal buffer, deliberately without native encoding/submission. K3a's root must
keep the actual wrapper/snapshot alive when caller ownership is dropped;
accepted prepare/state-update/rollback/commit must allocate0 under exhausted
quota/hostile PMR. Observation validity cannot replace current-state producer
admission: a ShaderRead buffer still has immutable valid owners, but a fresh
producer preparation requires ShaderWrite. Null/Core tests validate journal
semantics only; Vulkan/WebGPU keep their existing journal ownership and current
bounded write DiagnosticFailure. Passing these prerequisites grants no native
write, consumer visibility, completion or readiness claim on any backend.

For atomic native owner admission, canPrepareMetalRhiRecordedState exposes the
existing buffer snapshot's allocation-free, non-mutating canPrepare query.
It returns false for a non-Metal wrapper or another pending journal, true for
an unowned snapshot or the same journal. Checking it neither enlists nor changes
state. Preflight, complete owner admission and prepare must run on the same
serialized recording owner; the recorder must still check prepare's result.
This is a backend-private prerequisite, not native dependency execution or a
new L2 capability. It reuses the snapshot's ownership logic instead of copying
it into a recorder or local state map.

C4k3b1 evidence: 40 build targets,58 regressions,final5 focused quick/state/Null
smoke tests and26 Apple SDK syntax checks pass. Real Metal buffer metadata tests
prove preflight leaves both journals/state unchanged, same-journal enlist is
stable, another journal rejects, owner transfer preserves the pending conflict,
root ownership preserves the actual wrapper through rollback, and commit/
rollback detach and restore as specified. Accepted operations allocate0 under
exhausted quota/hostile PMR. A valid immutable preparation observation still
cannot prepare a fresh producer in ShaderRead state. The physical substrate
observer checks actual MTLBuffer/MTLDevice protocols, rejecting an actual texture
object with RHI-METAL-TRACKED-BUFFER-OBSERVATION-KIND before buffer-only queries.

Status: Contract Complete / Prototype Complete for the recorded-state metadata
prerequisite. ordinary-writable-state-journal-report.yaml is the final evidence
and preserves the scoped actual preparation observation Backend Validated
boundary. No native writable encoding/dependency/submission is performed in
these tests. All execution gates/public versions stay closed/0; c4k3b2 must
consume a separate prepared-write entrypoint with complete owner admission,
same-command producer-end/dependency/consumer identity and one existing journal
ownership transfer. Actual data/guard/visibility/cancellation/delayed completion
and RenderDoc visual acceptance remain required. Platform Validated/Production
Ready remain incomplete.

`GraphicsStorageWriteSequence/1` is command sequencing only, never another
resource-state map. The initial command admits one canonical prepared writer,
then actual producer begin, at least one proved-domain direct draw, actual
producer end, exact dependency consumption, and one actual consumer. Wrong
identity, repeated/out-of-order transitions and unfinished sequence reject.
Actual state belongs exclusively to the existing resource MutationSnapshot and
one command-owned MutationJournal. Value transitions certify no native work.
Null validates sequence only; all public GPU execution lowerings stay closed.

`RecordedStateSharedOwner/1` additionally permits one core-allocated shared
journal to be owned by a backend-private recording payload and its finished
RecordedStateCommandBuffer. Both refer to the same journal, not a copy or
parallel map. Existing unique journal paths remain allocation-free at transfer;
shared construction occurs cold before recording. Missing state owner rejects
acceptance. Abandonment explicitly rolls back before retained buffer wrappers
retire; successful native queue acceptance commits that same journal. Owning
metadata does not prove GPU completion. Null/Vulkan/Metal/WebGPU retain their
existing resource-state snapshots/queue acceptance semantics; only the private
Metal prototype uses this additional owner form now.

`MetalOrdinaryGraphicsStorageWriteExecutionPrototype/1` is an explicit private
Prototype opt-in, independent of public capabilities and never enabled by
applyTo/default descriptors. Cold preparation requires version1, explicit
prototypeOptIn, positive maxProducerDraws and explicit maxConsumerDraws (default0,
transfer only). Transfer has four fixed commands; shader has five and requires
positive consumer draws. The combined budget plus fixed commands must fit uint32.
Cold preparation also requires the existing opted-in CommandRetentionBudget and exact matching
core quota/registry/native device. It prepares native command/completion and
encoder slots plus one core-owned state journal before recording. Failure
returns no command or placeholder. Public CommandRetentionBudget/prepared
Graphics versions and GraphicsStorageWriteLowering execution gates remain0/
closed. Null sequence validation remains Equivalent-only; Vulkan/WebGPU and
non-Apple Metal execution are DiagnosticFailure. This prototype changes visible
output and remains explicitly opt-in until full backend/platform/visual gates.

The native recorder must accept an unforgeable k3a preparation, check the actual
ShaderWrite state and existing journal's non-mutating conflict query, then admit
the entire pass/write/journal/encoder owner union before enlistment or native
mutation. Native pass creation/binding failure after admission poisons the
command. Each direct draw revalidates exact owners/ABI/seals/descriptors and
GraphicsStorageWriteContract draw intent. Rebinding a different ordinary
pipeline, indexed/indirect writes, additional writers and missing draw reject.
Actual encoder end must precede dedicated exact dependency consumption. Generic
readonly recorder and payload-free barrier do not gain writable authority.

The initial k3b2a consumer is the already declared CopySrc/TransferRead exact
four-byte footprint. `MetalOrdinaryGraphicsWriteReadbackPrototype/1` cold owns
one real, exactly four-byte CopyDst/GpuToCpu buffer, host-only seal, original
resource/registry/native/control-block identities and a single-use atomic
Pending/Completed/Failed/Abandoned result. It owns the writer preparation.
Readback does not manufacture completion or data. Recording validates actual
source CopySrc and destination CopyDst states/owners/transfer seals, preflights
journal ownership, admits its complete original owner union and cold blit slot,
enlists the destination, then encodes one real four-byte native copy at offset0
in a subsequent encoder on the same command buffer. Managed readback requires
explicit synchronizeResource after the copy; unsupported storage/API rejects.
The dependency lowers via ended encoder order plus actual explicitly tracked
buffer hazards, consumed by the real subsequent copy. No stat-only generic
barrier, CPU replay or immediate signal is equivalent.

The native completion callback records Completed only on actual successful
command/wait command completion, otherwise Failed; it retires result ownership
explicitly before publishing completion. Open/sealed abandonment records
Abandoned and rolls back metadata before resource owner retirement. A result
may be recorded once; recovery requires fresh cold preparation/command, never
silent reuse. Reading Pending/Failed/Abandoned rejects with output zero; successful
post-completion read revalidates original output identity and decodes the exact
uint32 using the core unsigned element view. Readback is outside the hot record
path. All record/finish paths must allocate0 under exhausted core quota/hostile
PMR; metadata/owner quotas are actual core charges and exact cardinality, native
allocatedSize remains separately charged to the existing device ledger. Submit
rechecks source writable and destination transfer seals and CopySrc/CopyDst
states before native commit; it keeps its existing explicit completion/foreign
API allocation limitations.

K3b2a is a prerequisite, not closure of k3b2: selected shader consumer visibility,
all original full-plan four-backend/platform/renderer/WOCC/performance gates and
AI RenderDoc acceptance remain required. Actual atomic/readback/guard data and
native delayed completion may establish scoped Backend Validated evidence for
this explicit Prototype only; they cannot establish Production Ready or visual/
Platform Validated acceptance.

C4k3b2a evidence: 41 targets,59 regressions including quick contracts and Null
hidden frame,27 Apple SDK syntax checks pass. Actual native value0x12345678,
three unchanged guard words and all offscreen RGBA8 pixels [32,64,128,255] are
verified automatically. Begin/combined owner counts36/43 are measured from the
real recorder; exact/one-less admission rejects atomically before encoder or
destination journal mutation. Cold command/readback measured high-water quotas
pass exact/one-less; accepted32 draws/end/dependency/copy/finish allocate0 at
exhausted quota/hostile PMR. Identity/control-block/ABI/tracking/descriptor/seal,
scope/order/draw-budget, foreign registry/quota and journal conflict negatives
fail closed. Open/sealed cancellation and poisoning roll back before original
owner retirement. Actual shared-event delayed native completion retains replaced
original objects/allocation blocks until completion with either retained or
dropped command wrappers. The native callback clears its scalar result owner
before publishing completion; recording fields protect wrappers until the owner
thread commits the shared journal even if the callback completes immediately.
Results cannot read before completion or after failure/abandonment; completed
reads retain exact output observation without requiring live producer descriptors.

Status: Contract Complete / Prototype Complete; scoped Backend Validated for
this explicit private Metal transfer prerequisite only. Evidence is
ordinary-writable-execution-report.yaml and associated regression/syntax logs.
RenderDoc executables are unavailable; capture diagnostic explicitly keeps
visual, Platform Validated and Production Ready acceptance incomplete. K3b2b
shader consumer and the entire bindless plan remain open; no public version or
execution gate changes.

`FragmentStorageReadProof/1` is the selected-binary consumer acceptance language,
sharing the scalar-storage SPIR-V verifier's framing, unique definitions,
entry/interface, exact descriptor decorations and element-zero address proof
with FragmentAtomicWriteProof/1. The reader has exactly one Fragment entry,
one NonWritable StorageBuffer containing a stride4 uint32 runtime array and
one Location0 float4 output. Its sole body is entry/label/address/load/uint32
literal equality/select/store/return/end; select chooses literal green [0,1,0,1]
only when the loaded word equals the proved expected uint32, otherwise literal
red [1,0,0,1]. All other loads/writes/atomics/aliases/control flow/functions and
unknown operations/decorations reject. Debug names and authoring metadata cannot
mint proof. The selected expected word and exact set/binding/byte0..3 are facts,
not guessed from source or native ABI. No execution authority follows from proof.
Slang is the only source; publish SPIR-V/MSL/WGSL/reflection with the existing
single-entry pipeline and validate all actual artifacts. No matrices are used.

`MetalOrdinaryGraphicsWriteShaderConsumerPrototype/1` must cold-own that sealed
consumer artifact, readonly binding, owned same writer buffer and separate
consumer pass. Actual Fragment ABI must independently be ReadOnly/UInt/span4,
mapped directly from exact reflection set/binding to native slot. Resource,
registry/device/quota, tracked creation, original native/control blocks, exact
expected word matching writer replacement, state dependency and all owner facts
must match. The sole dependency is actually ended same-command producer into
ShaderRead/Fragment/ShaderRead; the existing same shared journal owns the state.
Native tracked buffer ordering between two actual render encoders is Equivalent;
the second Fragment must consume the real buffer, not a CPU read or copied value.
Admission must preflight the entire pass/consumer/encoder owner union and allocate
zero before any native mutation. Recording/end/draw/finish/cancel/commit and native
completion require the same bounded, freshness, seal and lifetime obligations as
the transfer prototype. Null validates semantics only; Vulkan/WebGPU/non-Apple
Metal remain DiagnosticFailure. Public gates remain closed and this private
Prototype opt-in cannot establish four-backend/RenderDoc/Platform/Production
acceptance. Until preparation/actual native tests complete, this is contract only.

Actual Slang2026.17 StructuredBuffer<uint> MSL uses device uint*, and actual
Metal reflection reports ReadWrite1/UInt33/span4. This storage reader variant
therefore deterministically fails RHI-METAL-ORDINARY-READ-CONSUMER-ABI-DOMAIN;
SPIR-V NonWritable must not silently normalize native access to ReadOnly.
`FragmentUniformReadProof/1` is a separately authored consumer variant, not a
fallback chosen after that failure. The source buffer explicitly carries both
StorageBuffer and UniformBuffer usage, producer uses the storage descriptor and
the consumer uses a UniformBuffer descriptor into the same uint32 byte0..3.
ShaderRead/Fragment/ShaderRead dependency intent is identical. Slang's actual
ConstantBuffer<uint> is SPIR-V Uniform Block{uint at offset0}, direct MSL constant
uint* and WGSL uniform data. The shared strict verifier separately accepts only
that Uniform storage class and single-member scalar address; no runtime array,
NonWritable reinterpretation or resource conversion occurs. Expected full word
and literal green/red output are identical. Consumer preparation must select the
proof by its explicit reflection descriptor type and require corresponding
actual source usage and ReadOnly0/UInt33/span4 native ABI. No try-storage-then-
uniform retry exists. Null validates both variants; Vulkan/WebGPU private writer
execution stays DiagnosticFailure; Metal storage read ABI stays DiagnosticFailure
and uniform native consumer execution still requires all recording/completion
tests. Neither variant changes public capability or readiness gates.

`MetalCompletionOutcome/1` distinguishes Pending, Completed and Failed on the
existing backend-private completion owner. Terminal readiness includes native
failure; it is not evidence of successful shader consumption. Actual successful
command and optional wait-command completion publish Completed, actual errors
publish Failed. A terminal precommit rejection publishes Failed without claiming
GPU work was accepted. Unsubmitted cancellation cannot publish Completed. Reset
and publication share the waiter's mutex so checking the atomic predicate and
entering the condition-variable wait cannot lose notification. Null/Vulkan/WebGPU
private shader execution remains unavailable and gains no sync capability from
this Metal observation. This changes no public L2 fence/NativeSync availability.

Private shader consumer execution consumes a cold canonical read root and actual
ShaderRead source state only after the same writer's actual encoder end/exact
dependency. It reuses the existing pass begin/binding machinery and preallocated
render encoder slot, admitting the whole root/pass/native owner union before
native encoder creation. Generic readonly begin/rebind, viewport/scissor mutation,
vertex/indexed/indirect/compute/blit work remain outside this bounded schema.
Each consumer direct draw and actual end reobserve identities/ABI/descriptors/
seals/current state. At least one consumer draw and actual end are mandatory;
unfinished/repeated/out-of-order/wrong-root sequences cannot finish or submit.
Abandon/poison rolls back the same journal before original roots retire. Native
acceptance commits it while fields still own both wrappers; completion alone
retires submitted ownership even if the command wrapper survives.

C4k3 final evidence:43 targets,61 regressions,6 focused quick/Null smoke,
28 SDK syntax checks and4 new reader SPIR-V validations pass. The actual
Uniform consumer ABI is access0/UInt33/span4/alignment4. Measured owner counts
36/59 prove exact/one-less atomic admission; accepted32 producer and32 consumer
draws plus finish allocate0 under exhausted quota/hostile PMR (69 commands).
Exact same-command output is green for0x12345678 and the0x12345679 full-word
mismatch control is red, with three unchanged original guards. Cold consumer
factory quota exact/one-less, identity/ABI/tracking/descriptor/seal/root/order/
budget negatives, open/finished rollback and two actual shared-event delayed
completion cases prove original roots/wrappers/native/control-block retirement
with command retained or dropped. Actual precommit wait-owner quota rejection
publishes Failed, rolls back and accepts no GPU work. Storage reader native ABI
still diagnoses; no source/native normalization or automatic retry is introduced.

Status: Contract Complete / Prototype Complete; scoped Backend Validated for
this explicit private Metal writer/transfer/Uniform shader consumer only.
ordinary-writable-shader-consumer-report.yaml is authoritative. Public versions0,
Null validator/Vulkan-WebGPU diagnostic conclusions and required RenderDoc,
Platform Validated, Production Ready, renderer/WOCC/performance/full bindless
plan gates remain open. The next c4l audit must reuse existing indirect packet,
instance lowering/index/vertex contracts and owner projection; scalar literal
atomic proof alone never authorizes GPU-written indirect fetch bounds.
