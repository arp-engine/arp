# CommandRecordingInput/1

Status: Contract Complete for the command vocabulary, structural validation,
dependency input, uniform-state safety, scalar/variable input and original descriptor composition prerequisites. This contract does not establish Backend
Validated, Platform Validated or Production Ready execution. The WebGPU generic
recording capability remains closed. RenderDoc diagnostic:
`RHI-RENDERDOC-TOOL-UNAVAILABLE`.

The ownership vocabulary must enumerate every recording family on CommandEncoder,
RenderPassEncoder, ComputePassEncoder and TransferPassEncoder, including default
unsupported methods. Informational getters and independent bundle/secondary
construction are outside this command stream. Convenience overloads normalize
to the same complete descriptor; the size-only copy overload cannot identify
original resources and always fails the production signature contract.

The existing numeric command/domain values are preserved. Added operations are
appended. DescriptorTable and SecondaryCommandBuffer have distinct ownership
domains: an ordinary Descriptor physical-group projection or RenderBundle cannot
impersonate either. A buffer upload names the original source buffer, destination
buffer and automatic Upload owner, in that order. An alias barrier names both
original resources, each explicitly Buffer or Texture. Memory dependencies name
the original resource without masquerading as a state transition. Query writes
distinguish Timestamp and PipelineStatistics.

| L2 recording family | Ownership kind / original inputs |
| --- | --- |
| transition overloads | BufferBarrier / TextureBarrier; complete ResourceBarrierDesc |
| memoryBarrier overloads | BufferMemoryBarrier / TextureMemoryBarrier; complete ResourceMemoryBarrierDesc |
| aliasBarrier | AliasBarrier; complete AliasingBarrierDesc and two typed original owners |
| prepareBindlessGraphicsPass | PrepareBindlessGraphicsPass; original prepared binding, access declarations and fault scope |
| begin/end Render, Compute, Transfer | Corresponding Begin/End kinds; complete descriptors, labels, original attachments and automatic owners |
| setPipeline | BindGraphicsPipeline / BindComputePipeline; original complete pipeline observation |
| setViewport/setScissor | SetViewport / SetScissor; all explicit scalar fields |
| bindVertexBuffer/bindIndexBuffer/bindTessellationFactorBuffer | Corresponding kinds; original buffer, slot/format/offset/stride |
| bindDescriptorSet/bindDescriptorTable | Render/Compute distinct kinds; original revision/layout/physical closure and complete ordered dynamic offsets |
| pushConstants | PushConstants; stages, offset, size, original immutable bytes and Upload owner |
| executeSecondary/executeRenderBundle | Distinct kinds/domains; original native command closure |
| draw/drawIndexed/indirect/count | Corresponding kinds; every count, offset, signed base vertex, stride and original argument/count buffers |
| dispatch/dispatchIndirect | Corresponding kinds; group counts or original argument buffer/offset |
| writeTimestamp/writePipelineStatistics | Distinct kinds; original typed Query owner and index |
| nextSubpass | NextSubpass; original pass/subpass/pipeline compatibility |
| transfer copies | Four typed copy kinds and copied descriptors; explicit mip layouts must have a proven lowering |
| uploadBuffer/uploadTexture/readbackTexture | BufferUpload / TextureUpload / TextureReadback; complete descriptors and automatic original staging/readback owners |
| push/pop debug group, marker | Corresponding kinds; immutable exact text and independent depth/text budgets |
| finish | Sole final Finish; original recording reservation and complete synchronous scope evidence |

Existing backend-private bindless producer/processor/fault/retention operations
remain explicit kinds with their own original owner domains. No empty pass or
Finish signature authorizes hidden uploads, query resolves or callback creation.

## CommandRecordingOrder/1

This allocation-free structural validator consumes an immutable L2 stream before
native opening. It rejects unknown kinds, nested/mismatched/open passes, graphics
commands outside Render, compute commands outside Compute, L2 copies outside
Transfer, encoder dependencies or debug commands during an active pass, missing
pipeline/index binding, unbalanced debug groups, and commands after Finish. Debug
depth has an independent caller-supplied ceiling, including a zero-depth profile.
Pipeline and index presence reset at each Render/Compute pass boundary; structural
presence never proves identity, layout compatibility, subpass compatibility,
resource state, descriptor completeness or scalar legality. Vertex bindings may
precede the graphics pipeline, as required by VertexStreamBinding/1. Rejected
structural input leaves the validator state unchanged and consumes no command.

CommandUsage/1 continues to describe ownership only. Its old metadata fixtures
are not native recordings and must not be reclassified as full valid L2 streams.
The future complete input plan must consume both contracts; neither can issue
the original recorded-command proof. A private raw transfer batch must normalize
its explicit L2 Transfer boundaries before claiming this complete contract.

| Backend | Result of this prerequisite |
| --- | --- |
| Null | Common vocabulary/order reference validator; no native execution proof |
| Vulkan | Existing native lowering unchanged; new metadata cannot grant execution |
| Metal | Existing native lowering unchanged; new metadata cannot grant execution |
| WebGPU | Added unsupported owner domains reject before projection allocation; whole generic recording remains DiagnosticFailure |

The fast fixture must cover every vocabulary entry and structural scope,
transactional rejection, pass state reset, debug/count boundaries and allocation0.
Null hidden-frame and actual browser enabled/disabled smoke remain required.
Completion of this prerequisite cannot close the full generic argument/state/
source/synchronous-recording/submission objective.

## ResourceDependencyArguments/1

The immutable pointer-free field snapshot preserves the complete original
ResourceBarrierDesc, ResourceMemoryBarrierDesc or AliasingBarrierDesc, tagged
with its exact command kind. It copies inactive texture-range metadata on buffer
transitions as well as active fields; it cannot turn an access-only dependency
into a transition. Alias input includes group, both queue roles and ownership
handoff, in addition to both resource IDs and stage/access masks. Snapshot
creation performs no inference, normalization or legality check. Comparison and
typed output are allocation-free and wrong-kind output leaves its destination
unchanged. The complete future plan must separately bind original resource owners,
validate enum/mask/state/range/queue semantics and charge its independent storage.

WebGPU native dependency entry signatures must accept the complete original L2
descriptor. Separate buffer/texture memory entries accept ResourceMemoryBarrierDesc;
they cannot call transition entries with invented same-before/after states.
Until an approved resource-state/batch/implicit-order lowering exists, every raw
entry fails through the existing recording-language gate, or, if that gate is
eventually satisfied, RHI-WEBGPU-BARRIER-LOWERING-UNPROVEN /
RHI-WEBGPU-MEMORY-BARRIER-LOWERING-UNPROVEN. A stats counter or native encoder
presence cannot prove a dependency. The former stat-only success bodies must be
removed, so lifting an unrelated future gate cannot accidentally authorize them.

| Backend | Complete dependency input / execution conclusion |
| --- | --- |
| Null | Immutable field/negative reference validation; existing state validator unchanged |
| Vulkan | Existing explicit transition/memory/alias lowering still consumes L2 descriptors; new snapshot grants no execution |
| Metal | Existing transition/memory/alias gates unchanged; new snapshot grants no execution |
| WebGPU | Complete transition and memory signatures; explicit DiagnosticFailure before recording/count/state mutation; alias lowering remains unsupported |

Required evidence includes every field mismatch, original caller mutation,
wrong-type/kind output, no allocation, both raw memory gates, public unchanged
state/count, actual browser no-native-call instrumentation and Null hidden-frame.
This is an input-preservation/safety prerequisite, not an Equivalent implicit
barrier implementation or complete native generic recording.

## WebUniformTextureStateAdmission/1

The current WebGPU texture has one uniform whole-resource logical state, with
one core MutationSnapshot/MutationJournal owner. That representation can accept
only the exact original defaultTextureSubresourceRange(texture.desc()). Any
different mip/layer/aspect range, including malformed ranges, rejects with
RHI-WEBGPU-BARRIER-SUBRESOURCE-STATE-UNPROVEN before journal enlistment or state
mutation. A partial request cannot update the single whole-state slot and report
subresource success. All valid subrange queries remain uniform because no partial
transition can change them. Exact full-range metadata transitions preserve the
existing before-state, independent-journal conflict and rollback/commit contract;
their success is CPU metadata evidence only. The public native transition gate
from the previous section still rejects before this helper can execute.

Null retains its existing bounded subresource validator; Vulkan and Metal retain
their existing explicit range/native/metadata mappings. This repair changes no
other backend and establishes no new validation claim for those mappings.
WebGPU partial state is DiagnosticFailure and native barrier execution remains
DiagnosticFailure independently. Full subresource support requires a separately
budgeted original state representation and tested lowering before admission.

## CommandScalarArguments/1 and CommandVariableArguments/1

Scalar fields have typed representations for all draw variants, dispatch variants,
vertex/index/tessellation binding, descriptor set/table number, viewport, scissor,
query index, push stages/offset/size and processor consumerRequired. Signed base
vertex, 64-bit offsets and all float bits are preserved, including signed zero
and inactive NaNs; storage does not sanitize or validate execution legality.
Wrong kind/type output is transactional. Unknown kinds and the legacy size-only
copy have no accepted representation. Buffer/texture upload and readback scalar
descriptors remain explicitly unsupported here until their complete original
automatic owner/descriptor contract is composed. No partial descriptor is captured.

The None representation means there are no *additional* scalars in this component.
Render-pass, transfer-copy and dependency commands still require their complete
existing original descriptor storage and ordered binding. Pipeline, bundle,
secondary and bindless operations still require the complete original source
projection, including automatic owners, access declarations and authorization.
None cannot supply or bypass any of those contracts.

The cold variable store copies one scalar record plus exact pass/debug text,
ordered descriptor-set dynamic offsets and push bytes. Its explicit independent
ceilings are command count, total text bytes, total offset elements, total push
bytes and exact CPU quota. Zero variable counts disable that payload family.
Caller-owned input arrays/bytes are live and serialized during preparation and
are not retained afterward. The copied stream is immutable and canonical
quota-bound ownership is mandatory. Text uses its explicit length, with no
termination inference or normalization. Offsets and bytes cannot be supplied to
unrelated kinds; push byte count must exactly match the typed scalar size.
Count, null-pointer, payload and aggregate-budget rejection precede allocation.
Storage and comparison use core BoundedSequence/PMR and allocate0 during matching;
no native encoder, shader compilation, command or completion callback is created.

## WebFrozenCommandScalars/1

Bind every copied scalar/variable record to the same position/kind in the
complete original usage stream. Counts must be identical. Validate the complete
L2 structural order with its separate debug-depth limit. Pipeline binds resolve
the original projected pipeline and require the matching Render/Compute family;
pass boundaries reset the bound pipeline identity. Descriptor-set number must
exist in the original physical-group observed-set closure, whose original
pipeline owner must match the currently bound pipeline. Offset count/order/values
must equal the original descriptor observation; arbitrary copied offsets cannot
authorize a different native binding range. No record-time native group creation
or guessed slot mapping is allowed.

The binding factory retains canonical original usage and argument owners under
its own command/CPU quota. Every raw comparison checks the complete original
graph, copied scalar/text/offset/byte input and exact ordered raw references.
Missing, aliased, reordered, mutated or foreign sources reject. Comparison issues
no ticket, native command proof, resource state mutation or execution permission.
The generic execution diagnostic remains recording-usage-unproven, even for an
entire valid scalar stream. Completing native input preparation additionally
requires complete pass/copy/dependency storage, scalar/resource-state legality,
all source families, one synchronous native recording batch and the original
reservation/error-scope/queue evidence envelope.

| Backend | Scalar/variable prerequisite conclusion |
| --- | --- |
| Null | Common immutable field/order/negative validator only |
| Vulkan | Existing native descriptors/commands unchanged; these snapshots grant no execution |
| Metal | Existing native descriptors/commands unchanged; these snapshots grant no execution |
| WebGPU | Explicit Prototype original scalar binding; generic/public native recording remains DiagnosticFailure |

Required evidence includes every scalar field, wrong type/kind and enum boundary,
signed/64-bit/float-bit preservation, independent quotas and hostile default PMR,
copied source/text/offset/byte mutation/drop, full usage order and raw-reference
negatives, original pipeline/descriptor relationship, actual native-source drop/
loss, no native call or hot allocation, quick unit/Null and both browser modes.
Status is limited to Contract Complete / Prototype Complete for these components;
Backend/Platform/Production and RenderDoc acceptance remain separate.

## WebOriginalCommandInputs/1

Status: Contract Complete / Prototype Complete for immutable input composition.
Quick/Null6/6 and Wasm/browser5/5 pass in both feature modes. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/original-inputs-summary.yaml`.
This component has no native recording, state, Backend/Platform/Production claim.
One canonical quota-bound owner composes the complete original scalar/variable
stream, complete original pass binding, and ordered typed full copy/dependency
records. The pass and scalar components must retain the exact same original
usage object and control block. Every descriptor-consuming command needs one
record at its original command position; wrong kind, missing, extra, reordered
or foreign original resource IDs reject before this owner's allocation. Full
pass descriptors stay in RenderPassArguments with their independent quotas;
no second pass parser/storage is introduced. TransferCopyArguments and
ResourceDependencyArguments remain the sole typed field snapshots.

Copies must resolve the original transfer observations and consume the existing
shared WebGpuTransferCopyValidation, including original write-seal gates; no
new copy algorithm, alignment guess or silent mip-layout omission is permitted.
The canonical graph must bind the source and destination in the original
ordered command references. Dependency records preserve every original field
and bind the explicitly typed resource IDs to those same original references.
Dependency storage is not resource-state or barrier-lowering proof, and native
barrier entry points continue to reject through their existing stable gates.
The descriptor-record count and exact CPU quota are independent of callback,
resource and scalar budgets. Caller records may disappear after preparation;
validation/comparison must be allocation-free and retain the entire usage graph.

All 58 command kinds have an explicit composition classification: complete
scalar input, complete pass, complete copy, complete dependency, or deterministic
unsupported input/source. Unknown/legacy and upload/readback descriptors cannot
be silently represented by an empty scalar. This owner grants no completion
reservation, native encoder, command proof, state change or queue authorization.
All source families and full state/scalar/pipeline compatibility are still
required for the eventual complete recording preparation. Only the existing
WebGpuOrdinaryRecording can consume the final batch and issue actual proof.

| Backend | Composition contract conclusion |
| --- | --- |
| Null | Common typed field and order validators; metadata only |
| Vulkan | Existing native execution unchanged; no new execution authority |
| Metal | Existing native execution unchanged; no new execution authority |
| WebGPU | Explicit Prototype input composition; general/native entry remains DiagnosticFailure |

Quick/Null plus both browser modes must cover original same-owner binding,
complete descriptor coverage, actual resource/source identity, exact budgets,
source mutation/drop/loss and zero hot allocation before completion. No native
or visual validation claim follows from successful metadata composition.

## Original current-state admission: c3i3 audit and draft invariants

Status: draft; not Contract Complete. The input composition above preserves
fields and original sources but cannot prove their current L2 state. In the
current WebGPU implementation, WebGpuRhiBuffer/WebGpuRhiTexture own state_ and
MutationSnapshot<ResourceState> inside the public wrapper. Registered resources
and transfer/ordinary observations retain original native/allocation/descriptor
facts, not that current-state owner. bufferDesc.initialState and
textureDesc.initialState remain creation intent after a metadata transition.
They cannot be used as current-state facts. Keeping a native payload alive after
wrapper drop does not retain its wrapper's state or journal record.

The next contract must define an original stable state owner, source lifetime,
conflicting journal admission, externally serialized snapshot/record/submit
boundaries, exact state revision/ordering and deterministic rollback/commit.
The owner must outlive every retained command that observes or changes it;
MutationRecord destruction currently detaches a journal record and is therefore
not command-owned state retention. Uniform whole-texture state must remain an
explicit profile with the existing exact full-range boundary; a partial request
must continue to fail. State ownership/count/CPU budgets must be independent of
native payload and callback budgets, use core allocation and remain bounded.

Reuse core MutationJournal/MutationSnapshot, existing
rhi::semantics::ResourceAccessScope and validateResourceAccessScope, original
resource observations and common recorded-state command lifetime. Add only the
missing reusable control/observation boundary; do not create another state table
inside a sample/renderer or infer state from usage flags/native implicit barriers.
All scalar buffer ranges, pipeline/pass compatibility, descriptor access,
query/push/indirect and automatic owner families still need their exact existing
L2 validators or a versioned lowering contract. Unsupported sources remain
DiagnosticFailure before native opening, without creating an empty payload.

| Backend | New retained state/control contract conclusion today |
| --- | --- |
| Null | DiagnosticFailure for an unproven new retained-state execution contract; existing validator unchanged |
| Vulkan | DiagnosticFailure until complete original state/control and lowering are proven; existing execution unchanged |
| Metal | DiagnosticFailure until complete original state/control and lowering are proven; existing execution unchanged |
| WebGPU | Generic/public recording remains DiagnosticFailure; current observation graph has no retained current-state proof |

Required tests for the next stage include creation state differing from current
state, mismatched original state control, wrapper drop, journal conflicts,
rollback/commit, stale revision and wrong range, complete scalar/resource access
legality and no hot allocation. Keep fast contract/Null hidden-frame and actual
browser native-source/drop/loss paths. Only after this contract and all source
families are proven may the existing WebGpuOrdinaryRecording consume one full
synchronous batch with the original completion/submission envelope.

## OriginalResourceStateControl/1: c3i3 implementation contract

L2 semantics: current ResourceState is a mutable, original resource fact. A
retained original resource must retain the same stable state/control lifetime,
not a copied initial descriptor or another control under the same ID. All
mutation, journal preparation and observation are externally serialized. Every
explicit write, including a same-value write, invalidates older state facts.
Rollback restores the original value but advances the revision; it must not
revive a pre-transaction observation. Pending journal values cannot be captured
as committed state. Conflicting journals reject before value/revision changes.
A non-recycling revision sequence uses zero as the permanent exhausted sentinel;
exhaustion rejects writes and observations, while pending rollback still restores
the value and closes the exhausted revision domain. Commit makes the current
value observable; no native work or synchronization follows from it.

Storage lowering: reuse core MonotonicIdSequence and MutationSnapshot. Add an
optional non-owning noexcept post-restore observer to MutationSnapshot and put
bounded scalar versioned mutation in core. Embed one control by value in each
original WebGPU native buffer/texture payload, initialized explicitly from its
creation descriptor (surface textures from the explicit surface contract).
Texture views share the original parent control. Existing payload allocation
already uses core and pays for the embedded bytes; the control adds no allocation.
CPU-only WebGPU wrappers retain a separate explicitly documented metadata profile
when no native payload exists; they cannot supply an original native-state fact.
No native payload creation failure may manufacture a state-backed native resource.

Original graph observation requires independent limits for state controls,
source references and CPU metadata, deduplicates only the same original parent
pointer/control block/resource identity, and rejects ambiguous controls. Preserve
the original graph and resource anchor; snapshot value/revision is useful only
while that exact owner/control still matches. Wrapper drop must not destroy a
retained original state. Unknown state enums and texture-only states on buffers cannot form original
facts. All pending, changed, exhausted, foreign, device-lost or
uncovered sources fail before a new recorder can open native work. This state
observation is a prerequisite, not complete command state/access/scalar legality.

| Backend | This control/storage lowering conclusion |
| --- | --- |
| Null | Common scalar/journal validator; no new native execution capability |
| Vulkan | New retained-control execution profile remains DiagnosticFailure; existing range/state lowering unchanged |
| Metal | New retained-control execution profile remains DiagnosticFailure; existing range/state lowering unchanged |
| WebGPU | Embedded original payload control and explicit uniform whole-texture metadata; new generic execution remains DiagnosticFailure |

Tests precede integration: direct write and same-value invalidation, pending and
foreign journal rejection, rollback ABA rejection, commit, sequence exhaustion,
owner drop/destruction, exact resource/control identity, full-range-only texture
state, quota boundaries and hot allocation0. Fast contract/core tests and Null
hidden-frame remain mandatory. Actual browser original resource/descriptor/pass/
transfer graphs must verify wrapper drop, stale state and real device loss in both
feature modes; state metadata alone never promotes NativeSync or recorded proof.

## Original ordered dependency/copy state validation: next c3i3 prerequisite

Contract-first scope: a retained original input stream is interpreted in original
command order against the committed observations from OriginalResourceStateControl/1.
Validation must never change the actual controls, their revisions or journals.
A barrier's `before` must equal the state immediately before its original command;
its stage/access pairs must satisfy ResourceAccessScope. Undefined is permitted
only as an explicit discard source with TopOfPipe/None; it is not an access or a
barrier destination. Texture state transitions must name the exact whole-resource
range for the uniform profile. Memory dependencies preserve current state and
must satisfy both explicit access scopes. Copies require CopySrc and CopyDst at
their original command position and reuse full typed copy validation. Structural
scope/order validation remains CommandRecordingOrder, rather than a second parser.

Use an allocation-free reusable state-sequence query over the original bounded
transition records, initialized from the retained controls. No guessed initial
state, inferred barrier, replay GPU work, automatic state mutation or independent
native proof is allowed. This is deterministic validation, not equivalent native
barrier lowering. Every other command family must return an explicit diagnostic
until its full scalar/pipeline/access/owner contract is connected. Aliasing is
not a state assignment and must be rejected by this profile.

| Backend | Ordered dependency/copy validation conclusion |
| --- | --- |
| Null | Common state/access sequence validator only; no native proof |
| Vulkan | New original-input execution profile DiagnosticFailure; existing lowering unchanged |
| Metal | New original-input execution profile DiagnosticFailure; existing lowering unchanged |
| WebGPU | Common validation plus original source adapter; barriers and generic recording still DiagnosticFailure |

Acceptance: quick numeric state/order/access tests with transactional outputs,
unknown enums, missing resources, same-resource independent domains, before/after
and whole-range mismatch; original browser input graphs, allocation0, unchanged
real revisions and journal state; Null hidden-frame and both browser modes. Native
batch/scope/reservation/common submission remain the subsequent obligations.

## Verification 2026-10-03: c3i3a/c3i3b

Status: **Contract Complete / Prototype Complete** for original control storage
and ordered dependency/copy state validation only. The earlier c3i3 source audit
is superseded for WebGPU current-state ownership by the embedded payload control;
its remaining scalar/source/native-recording obligations are still open.

Final quick/Null7/7 and Wasm/browser5/5 pass. Both actual browser feature modes
run14 state inventories,10 typed Transfer state suites (five copies/all four
copy kinds plus one memory dependency), and actual retained Transfer/pass/Graphics
device-loss retirement. Five-source inventory deduplicates to two controls,
including shared views and state-free sampler. Snapshot cold Wasm peaks are
80 empty,160 inventory/Graphics,200 pass and280 Transfer bytes; hot validation
allocates0. Real control revisions and values remain unchanged during sequence
validation. Initial quota-fixture measurement failure and repaired final evidence
are preserved in original-state-* artifacts under bindless-continuation.

The new validation does not issue native proof or imply equivalent native barrier
lowering. Generic/public execution, Platform Validated and Production Ready are
unchanged. RenderDoc commands are unavailable: RHI-RENDERDOC-TOOL-UNAVAILABLE;
no capture or visual acceptance is claimed.

Next c3i3c audit: Compute/Graphics closed cross-target range proofs contain exact
footprints, but their current dispatch/draw validation checks source/range and
scalar language, without the new original committed-state controls. Physical
groups retain the logical mapping and native range; the next join must bind the
selected proven footprint to that exact original descriptor/resource/state owner.
A physical binding number or byte size alone cannot establish this identity.
Automatic owner families, synchronous recording and the original completion
envelope remain mandatory; every unproven native lowering stays closed.

## OriginalScalarAccessBinding/1: verified selected access contract

A selected proven dispatch/draw footprint must resolve through the *same original*
range validation groups, pipeline creation contract, physical slot mapping,
logical descriptor write, original resource observation and retained state
control. Physical slot/byte count or same-device pipeline similarity cannot
substitute for original pointer/control-block agreement. Read-only range accessors
may expose the already-retained original group owner; they grant no recording
friendship. Physical buffer lookup must inspect immutable original mapping and
native entry agreement under external serialization, produce a transactional
result, and allocate no additional inventory or native objects.

The current physical-group profile emits whole buffers at offset0 and exact
ordered zero dynamic offsets. Lookup verifies those facts rather than assuming
native defaults. Unsupported texture/array/push/table mappings fail before an
output is produced. The original input stream must have bound this exact
pipeline and original group/logical set before the selected operation in the
same pass. Reuse existing scalar dispatch/draw and cross-target range proofs,
containsUnsignedRange, descriptor/group validation and ResourceAccessScope.
ComputeFixedWordsWrite requires ComputeShader/ShaderWrite; the proven triangle
fragment equality read requires FragmentShader/ShaderRead (ShaderWrite state may
permit read under the existing access scope contract). State is resolved at the
original operation position, with resource owner/control equality verified.
This selected-operation join does not certify all preceding/following commands,
attachment legality, native barriers, automatic owner families or native batches.
Those remain independently mandatory and default closed.

| Backend | Original selected scalar-access binding |
| --- | --- |
| Null | Common scalar/access validation; no new native execution proof |
| Vulkan | New original-input execution profile DiagnosticFailure; existing lowering unchanged |
| Metal | New original-input execution profile DiagnosticFailure; existing lowering unchanged |
| WebGPU | Exact original physical-buffer/range/state join; generic execution DiagnosticFailure until full stream/native batch |

Tests must cover actual original Compute and Graphics ranges, transactional missing
slot/type/resource failures, forged owners, foreign same-device pipeline/group,
wrong scalar and state/revision/pending values, descriptor/native/source mutation,
wrapper drop and actual device loss; allocation0/native creation0 during lookup.
Keep a quick common contract path, Null hidden-frame and both actual browser modes.

## OriginalComputeBatch/1: verified private native contract

Explicit backend-private Prototype opt-in only. Supported original language is
exactly BeginComputePass, BindComputePipeline, BindComputeDescriptor, Dispatch,
EndComputePass, Finish, with one original proven ComputeFixedWordsWrite pipeline,
one original group/logical set and one original buffer source/control. Require
full canonical scalar/variable/pass/input/state owners and selected scalar-access
validation. Reject every extra command/source/group/pipeline, dependency, upload,
readback, query, bundle, push/table, producer/processor or unsupported scalar
before native opening. This does not declare general recording available.

The copied label must be canonical Unicode-scalar UTF-8 without embedded NUL.
The version-controlled emdawn StringView bridge uses UTF8ToString with a length
limit but stops at the first NUL; invalid UTF-8 also permits replacement. Reject
both cases with RHI-WEBGPU-ORIGINAL-COMPUTE-LABEL-ENCODING before native opening.
Reuse allocation-free core::Utf8Decoder for cold and hot revalidation; do not
rewrite labels or guess an encoding. Empty and canonical multibyte labels retain
their exact UTF-8 semantics. Generic core UTF-8 decoding still permits U+0000.

Cold preparation validates all original owners and reserves the existing
completion/submission envelope, command payload and bounded callback storage.
It opens no command encoder or pass. A single synchronous record call consumes
all six original commands and copied pass label, opens one encoder/pass, binds
exact original pipeline/group/offsets, dispatches exact frozen scalars, ends and
finishes under one actual Validation/OutOfMemory/Internal error-scope batch.
No await, allocation, file/network IO, pipeline/group/shader creation is permitted
inside that batch. Scope completion remains asynchronous and blocks recorded
proof until all actual callbacks succeed. Native creation failure rejects; no
placeholder can be recorded or submitted. Independent original-command, native
encoder/pass/buffer/group/callback and CPU quotas are caller supplied.

The original state/input owner is retained in both the recorder and its original
command payload, checked by pointer/control block and carried by the existing
completion reservation. Legacy scalar recording APIs cannot add work to this
mode. Submit rechecks original current-state facts and the existing reservation,
uses WebGpuOrdinarySubmission unchanged, and publishes completion only with its
actual queue+scope+signal epoch proof. State is ShaderWrite throughout this closed
language; no resource transition is silently synthesized. Native WebGPU encoder
resource-usage ordering is the specification-backed lowering for this exact
single-pass buffer write, not evidence for arbitrary dependency lowering.

| Backend | OriginalComputeBatch/1 |
| --- | --- |
| Null | DiagnosticFailure for native execution; common input/state validation only |
| Vulkan | DiagnosticFailure for this new private original-input batch profile |
| Metal | DiagnosticFailure for this new private original-input batch profile |
| WebGPU | Explicit Prototype Native payload only for the exact six-command proven language; generic/public capability remains false |

Acceptance requires quick version/opt-in/budget/forbidden-owner assertions and
Null hidden-frame; actual browser one-batch API order, frozen scalar use, no
preparation encoder, recording allocation0 and no native group/pipeline growth,
wrong/stale/pending state and unsupported stream rejection before encoder opening,
real native write readback, existing common submission proof, retained wrapper
ownership and actual device-loss retirement in both feature modes. No broader
Backend/Platform/Production promotion may follow from this private profile.


## OriginalGraphicsStreamValidation/1: verified full-profile metadata contract

The closed language is exactly BeginRenderPass, BindGraphicsPipeline,
SetViewport, SetScissor, BindRenderDescriptor, Draw, EndRenderPass, Finish.
It owns one original proven GraphicsTriangleStorageRead range, pipeline,
physical group/logical set/buffer and one original color view/parent texture.
Require canonical full input/state graph, exactly three original references,
two state controls/two resource sources and no transfer/dependency/extra owner.
Reuse SingleColor2DRendering/1 for exact Clear/Store RGBA8 2D single-sample pass,
view extent and compatible static pipeline. Frozen viewport/scissor must match
the original static pipeline and original proven draw scalars. Resolve texture
state by the original parent ID/control/owner at BeginRenderPass and require
ColorAttachmentOutput/ColorAttachmentWrite; selected original fragment buffer
read must independently pass OriginalScalarAccessBinding/1. Unknown commands,
extra states/sources, depth/resolve/subpass or incompatible target fail closed.

This allocation-free full-profile validator creates no native payload, mutates
no state, grants no recorded proof and does not lower dependencies. All outputs
are diagnostic metadata only; existing general execution gate remains closed.

| Backend | OriginalGraphicsStreamValidation/1 |
| --- | --- |
| Null | DiagnosticFailure for execution; common L2/profile invariants only |
| Vulkan | DiagnosticFailure for this private original-stream profile |
| Metal | DiagnosticFailure for this private original-stream profile |
| WebGPU | Prototype metadata validation of real original sources; native execution is separately gated by OriginalGraphicsBatch/1 |

Acceptance: quick missing/forged owner test plus Null hidden-frame; both browser
feature modes with actual target view/parent/buffer, correct full profile,
wrong color/buffer state, pending/rollback stale state, original owner forgery,
frozen viewport/scissor negatives, no allocation/native creation and actual
retained device-loss diagnostics. No Backend/Platform/Production promotion.


OriginalGraphicsStreamValidation/1 verified evidence (2026-10-03):
original-graphics-stream-* YAML/log/XML under
build/web-bindless-apple-debug/artifacts/bindless-continuation. Host/Null8/8,
Wasm/browser5/5 in both feature modes, diagnostics=[]/nativeErrors=[].
Contract Complete / Prototype Complete for validation only. OriginalComputeBatch/1
also reverified, including all four pre-encoder rejection cases. No native
original-Graphics execution, visual/Platform/Production promotion.

## OriginalGraphicsBatch/1: verified private native contract

Explicit backend-private version1 Prototype opt-in; independent capacity of at
least8 original commands plus existing ordinary native Graphics recording
encoder/pass/command-buffer/group/callback and CPU quotas. Supported language
and exact original source ownership are OriginalGraphicsStreamValidation/1.
Cold preparation validates the entire original profile, allocates the existing
recorder/command/callbacks and reserves the existing ordinary completion envelope,
but opens no native encoder/pass. It must retain the exact original state/input
and proven Graphics range in both recorder and original command payload.

One synchronous call revalidates all original facts before native opening,
consumes the copied color clear/store/target and frozen viewport/scissor/draw,
and emits exactly encoder, beginRenderPass, setPipeline, setViewport,
setScissorRect, setBindGroup, draw, end, finish in original command order.
Use existing attachment-operation lowering for explicit validated Clear/Store,
and SingleColor2DRendering for target/extent/pipeline semantics. No native
default policy, await, hot allocation, IO or shader/pipeline/group creation.
One actual Validation/OOM/Internal scope batch publishes recorded proof only
when all callbacks succeed. No metadata-issued proof or new recorder friend.
Native creation failure rejects with actual scopes preserved. All legacy scalar
record APIs and original Compute record API must reject this distinct mode.

Submission remains WebGpuOrdinarySubmission with actual original reservation,
current-state revalidation, queue+scope+signal epoch completion. A retained ready
or submitted batch must keep original texture/view/allocation/descriptor/buffer/
range/state/input owners; actual device loss retires through existing owners.
Exact native API order, frozen arguments, quota boundaries, stale/pending and
unsupported input rejection before encoder, allocation0, native readback and
real loss in both browser modes are required; also quick contract/Null smoke.
RenderDoc capture remains required for visual acceptance; unavailable tools
leave that acceptance and Platform/Production gates open with stable diagnostic.

| Backend | OriginalGraphicsBatch/1 |
| --- | --- |
| Null | DiagnosticFailure for native execution; common profile validation only |
| Vulkan | DiagnosticFailure for this private original-input batch profile |
| Metal | DiagnosticFailure for this private original-input batch profile |
| WebGPU | Prototype Native only after original full-stream recording and actual scope proof; exact implementation verified below |

Public/general recording, bindless executable payload and NativeSync capabilities
remain closed. This exact profile does not close broader streams, dependency
lowering, remaining source families or four-backend/platform obligations.


OriginalGraphicsBatch/1 implementation evidence (2026-10-03): original-graphics-
batch-* YAML/log/XML under build/web-bindless-apple-debug/artifacts/bindless-
continuation. Host/Null8/8 and Wasm/browser5/5 in both modes; six original input
negatives reject before encoder/reservation; two actual invalid native encoder/
pass operations fail real scopes and cannot record/submit. Four native pixels
are RGBA(0,255,0,255), original API order/scalars/pass bindings observed, hot
allocation0/group creation0, queue+scope+signal completion, stale-state submit
rejection and actual device-loss retirement. The shared recorder cold peak is
now880 bytes for both original Compute and Graphics. Status is Contract Complete /
Prototype Complete for this exact native profile; RenderDoc tool-unavailable
remains RHI-RENDERDOC-TOOL-UNAVAILABLE. Public/general/bindless/NativeSync gates
and visual/Platform/Production acceptance remain closed.


## OriginalTransferStreamValidation/1: verified full-profile metadata contract

Closed language: BeginTransferPass, one BufferCopy/BufferToTextureCopy/
TextureToBufferCopy/TextureToTextureCopy, EndTransferPass, Finish. No dependency,
upload/readback request, debug/query/bundle or pipeline/descriptor command.
Own one canonical original Transfer retention graph under its existing explicit
entry/owner/allocation/CPU budgets; retain every declared original Buffer/Texture
source, including known unused transfer resources. Require two exact ordered
copy references and one original copy descriptor at command1; no ordinary
resource projection, pipeline/group or other source family. Every declared
Transfer source must have its exact original state owner/control; duplicate or
ambiguous identities remain rejected by original state graph preparation.

Reuse WebGpuTransferCopyValidation for exact kind, format, subresource, extent,
pitches, offsets, byte ranges, write seal and distinct source/destination. Reuse
WebGpuOriginalTransferStateValidation for complete command order and current
source CopySrc/destination CopyDst at the copy position. No inferred barriers,
state mutation, copy descriptor rewriting, native creation or recorded proof.
The frozen BeginTransferPass label is metadata here; native consumption must
separately specify its exact mapping/encoding or rejection before opening.

| Backend | OriginalTransferStreamValidation/1 |
| --- | --- |
| Null | DiagnosticFailure for native execution; common copy/state/order invariants |
| Vulkan | DiagnosticFailure for this private original-stream profile |
| Metal | DiagnosticFailure for this private original-stream profile |
| WebGPU | Prototype metadata validation of original native sources; native execution separately gated by OriginalTransferBatch/1 |

Acceptance: quick owner failure + Null hidden-frame; both browser modes with
real original Buffer/Texture sources, all4 copy kinds (5 descriptors including
offsets), wrong source/destination states, pending/rollback stale facts, forbidden
dependency language, forged owners, allocation0/immutable source revisions and
actual retained device-loss diagnostics. Backend/Platform/Production gates remain
open; validator success cannot permit recording or submission.


OriginalTransferStreamValidation/1 evidence (2026-10-03): original-transfer-stream-*
YAML/log/XML under build/web-bindless-apple-debug/artifacts/bindless-continuation.
Host/Null8/8 and Wasm/browser5/5 in both feature modes; 50 actual original source
streams per mode cover all4 copy kinds/5 authored descriptors, wrong source/
destination states, pending/rollback stale facts, forged owner and forbidden
dependency streams. Validation leaves original revisions unchanged and allocates0.
Five graphs retained until actual device loss retire their independent quotas.
Contract Complete / Prototype Complete for metadata validation only; the
separate OriginalTransferBatch/1 contract below is mandatory for native execution. RenderDoc unavailable;
no broad Backend/Platform/Production promotion or public capability change.

The completed native Transfer route extracts a shared backend-private copy
emitter while preserving producer-owned dispatch/frozen cursor admission gates
and telemetry. The common recorder has an explicit original Transfer resource/
completion-owner loss branch. Begin/EndTransferPass map to exact-label debug
group operations; see OriginalTransferBatch/1 below. Dependency lowering remains
a separate unapproved profile with deterministic failure before native opening.

## OriginalTransferBatch/1 and NativeTransferCopyLowering/1

Status: Contract Complete / Prototype Complete for the private Transfer batch. These are
backend-private Prototype opt-in profiles. Public/general recording, bindless
execution payload and NativeSync capabilities remain closed.

The complete language is BeginTransferPass, exactly one approved BufferCopy,
BufferToTextureCopy, TextureToBufferCopy or TextureToTextureCopy, EndTransferPass,
Finish. OriginalTransferStreamValidation/1 is mandatory. All declared Transfer
resources, allocation/native owners and current-state observations remain in the
original graph, including unused declared resources. Dependencies, upload,
readback, other source families and extra commands fail before encoder opening.

WebGPU has no transfer-pass encoder. BeginTransferPass lowers to
GPUCommandEncoder.pushDebugGroup with the exact frozen label; EndTransferPass
lowers to popDebugGroup. These API-defined debug commands delimit the copy without
changing ordering or resource lifetime. Labels must be canonical UTF-8 without
embedded NUL (emdawn UTF8ToString stops at NUL). Reject encoding before native
opening. Empty labels are legal; no label substitution. Finish produces one real
command buffer. A single synchronous Validation/OOM/Internal scope batch surrounds
encoder creation, push, copy, pop and finish; all callbacks must succeed before
existing WebGpuOrdinaryRecording can issue its original recorded proof.

NativeTransferCopyLowering/1 reuses four backend-private emitters from both legacy
guarded producer recording and this common recorder. It grants no proof or
submission authority and cannot toggle ordinaryTransferDispatch. Buffer offsets
and size map exactly to copyBufferToBuffer. Texture copies preserve mip, aspect,
buffer offset, bytesPerRow and rowsPerImage. Texture origin x/y are zero as required
by the L2 descriptor; 2D native origin z is baseArrayLayer and depth is
arrayLayerCount, while 3D z is zero and depth is extent.depth. Compressed width/
height round to the format-defined block extent with checked uint32 conversion.
Unknown aspect/dimension, nonzero/invalid extent, unknown format, overflow or absent
texel-copy API reject deterministically. Full format/range/footprint/write-seal
validation remains mandatory through WebGpuTransferCopyValidation, using
TextureTransferValidation's versioned row-pitch alignment rather than a new magic
constant. No default aspect, dropped layouts or fallback resource.

| Backend | OriginalTransferBatch/1 | NativeTransferCopyLowering/1 |
| --- | --- | --- |
| Null | DiagnosticFailure: native-unavailable; full metadata validator remains usable | DiagnosticFailure: native-unavailable |
| Vulkan | DiagnosticFailure: WebGPU-private profile unavailable; no capability promoted | DiagnosticFailure: WebGPU-private lowering unavailable |
| Metal | DiagnosticFailure: WebGPU-private profile unavailable; no capability promoted | DiagnosticFailure: WebGPU-private lowering unavailable |
| WebGPU | Prototype opt-in; native copies and equivalent debug-group pass delimiters, real scopes/completion required | Native four-copy payload when required API exists; otherwise DiagnosticFailure |

Original command count4, command object1, encoder1, debug scope1, command buffer1
and six preallocated recording callbacks are independently bounded by explicit
recording budgets. Graph/input/state, completion-owner, recording, signal and
submission quotas remain separate. Cold preparation allocates metadata and reserves
completion storage but opens no encoder. Hot recording/submission allocate0 in the
core allocator, create no groups/pipelines/shaders, perform no host wait or IO.
No API argument is read from a mutable caller descriptor. Real queue completion plus
submission scopes publish the reserved signal; receipt alone never completes it.
Dropped wrappers retain the exact graph through common completion storage. Native
errors produce no proof or queue work; actual device loss closes the owner and
releases reservations deterministically. Pending/stale/forged owners reject before
record or submit. Retry requires new cold preparation and fresh state observations;
failed native batches cannot be replayed. Tests must cover all four copies with
readbacks, nonzero offsets, frozen arguments, budget boundaries, negative actual
native scopes and actual loss in both browser feature modes. This contract supplies
no RenderDoc, Platform Validated or Production Ready claim.


OriginalTransferBatch/1 evidence (2026-10-03): original-transfer-batch-* YAML/log/
XML in build/web-bindless-apple-debug/artifacts/bindless-continuation. Host/Null8/8,
Wasm/browser5/5 and source inventory6/6 pass. Both modes execute six real native
copies spanning four kinds, five original descriptors and an explicitly authored
nonzero-offset BufferCopy; each has actual byte readback. Exact frozen descriptor/
label mappings include multibyte UTF-8 and empty labels. NUL/overlong UTF-8,
forged owners, incorrect current states, stale admission, capacity and CPU quota
boundaries reject. Cold encoder0, peak880 recording bytes, hot allocation0 and group
creation0. Twelve actual scope failures per mode (finished encoder and unmatched
debug pop for each copy) produce no proof or submission. Pending/rollback stale
state rejects common submission before queue work. Five original graphs and their
recording/owner quotas retire on actual device loss; the extra fully empty owner
has only its independently bounded device-loss observer metadata until loss,
consistent with existing observeWebGpuNativeOwnerDeviceLoss's strong owner contract.
No dummy payload, new proof friend, dispatch flag or incompatible cursor is used.
RenderDoc is unavailable (RHI-RENDERDOC-TOOL-UNAVAILABLE). No overall Backend
Validated, Platform Validated or Production Ready claim follows this profile.
Dependencies and remaining original source families are still required.

## Original dependency batch: state-publication prerequisite

Status: design in progress; native dependency execution is DiagnosticFailure on
this private profile across Null/Vulkan/Metal/WebGPU. Existing backend barrier
contracts and capabilities are unchanged. The selected Transfer mapping below
has an approved normative metadata contract; equivalent native dependency
execution still requires the common recorder/submission and failure/loss proof.

The existing original state snapshot accepts committed observations only.
VersionedMutationValue writes within a MutationJournal are pending, and rollback
changes the revision even when it restores the value. Consequently a recorder
cannot use the initial committed snapshot to authorize its own pending writes,
nor can it re-prepare a snapshot after rollback and pretend the old command is
current. RecordedStateCommandBuffer commits at ownership acceptance, so the
submission-before/after-commit checks need explicit evidence for the original
resource, exact journal, expected revision and planned final value. Failed scope,
unsubmitted drop and loss must undo only the command's own pending changes; an
unproven submitted result cannot invent restored native contents. All initial,
pending and final observations must remain tied to the exact retained resource
owner, with independent budgets and terminal revision exhaustion.

The first prerequisite extends the existing core MutationSnapshot and
VersionedMutationValue with an allocation-free pending observation query. It is
bound to that exact value object and journal and valid only while that journal
owns the pending record at the observed revision/value. Ordinary observe/matches
remain committed-only. Foreign journal/value, rewrite, commit, rollback and
revision exhaustion invalidate pending evidence; address identity alone cannot
replace the external retained owner/lifetime contract. This is scalar metadata,
never GPU recording, completion, synchronization or submission authority.
Further RHI transaction ownership/publication and four-backend native lowering
contracts must precede integration into the common recorder.


Pending mutation prerequisite evidence (2026-10-03): Contract Complete / Prototype
Complete for core scalar observation only. Existing MutationSnapshot now exposes
its exact journal membership, and VersionedMutationValue offers observePending/
matchesPending without changing committed observe/matches. A distinct PendingObservation
carries the exact source and journal privately and exposes only scalar value/
revision. Tests reject a different object with identical facts, foreign journal,
rewrite, commit, rollback ABA and revision exhaustion with allocation0. Host/Null
8/8 and both browser modes5/5 pass; six real original dependency streams per mode
reject common recording before encoder/reservation. Evidence original-dependency-
pending-* YAML/log/XML; no GPU/proof authority, native dependency equivalence,
Backend/Platform/Production claim or public capability change is introduced.

## OriginalDependencyStateTransaction/1

Status: Contract Complete for the CPU ownership/publication prerequisite; native
dependency lowering and common recorder integration remain incomplete. This
backend-private Prototype opt-in transaction does not issue GPU authority.

The immutable original input/state graph is retained throughout the transaction.
Cold preparation validates complete Transfer-only original sources and ordered
copy/dependency semantics using OriginalTransferStateValidation/1. It preallocates
one entry per original control and binds each entry to the exact resource,
control and containing payload owner. Explicit independent control-count,
transition-count, debug-depth and CPU budgets are mandatory; the original graph's
resource/allocation budgets remain separate. No encoder or GPU payload is opened.

Prepared validates the unchanged committed initial observations. Apply first
revalidates every original source and complete planned sequence, then writes the
exact original transition descriptors into the transaction's embedded
MutationJournal. Memory barriers never write state. The final pending observation
of each changed control is specific to that exact object/journal/revision; all
untouched controls must still match their original committed observations. No
initial snapshot is rewritten, reissued or relaxed to accept arbitrary pending
facts. Owner-only validation is explicitly separate from committed current-state
validation; neither query supplies recording authority.

Commit requires all exact pending/final facts and unchanged untouched controls,
then commits only this journal and retains the same final value/revision as
committed evidence. Ordinary initial snapshots of changed resources stay stale.
Repeated apply/commit, foreign owners/journals, conflicting writes, stale source
facts or revision exhaustion reject. Unsubmitted abandonment and destruction roll
back only this journal, restore prior values and advance revisions, so the old
initial graph stays stale. Committed state cannot be rolled back to imply native
contents were restored. Terminal invalidation after an unproven submitted result
quarantines every declared original control; invalidation is permanent, including
through a later rollback. Recovery requires new resource/control owners. Before
native integration, callers can only exercise this as explicit CPU Prototype
metadata; a CPU commit is never native completion proof.

The later common recorder must own this exact transaction in its command payload,
apply it within one synchronous native recording batch, abandon it on failed
recording scopes/unsubmitted drop and consume commit atomically with submission
admission. Unproven submission must quarantine it; device loss must retire its
owned pending or committed state under the same rules. Core completion storage,
real queue completion and actual scope evidence remain independently mandatory.

| Backend | CPU transaction prerequisite | Native dependency execution for this profile |
| --- | --- | --- |
| Null | Common control/journal invariants; no native proof | DiagnosticFailure |
| Vulkan | Common control/journal invariants; WebGPU graph is private | DiagnosticFailure |
| Metal | Common control/journal invariants; WebGPU graph is private | DiagnosticFailure |
| WebGPU | Prototype CPU transaction on exact original owners | DiagnosticFailure until separate approved lowering and recorded proof |

Validation requires fast core/control tests and Null hidden-frame, plus actual
browser original Buffer/Texture owners in both feature modes: exact/one-less/
exhausted budgets, forged owner, foreign journal/value, repeated admission,
unchanged controls, commit/drop/rollback ABA, retained original allocation owners,
terminal invalidation and actual device loss. Hot metadata operations allocate0
and do no IO, waiting or GPU work. No Backend/Platform/Production promotion or
public/general/bindless/NativeSync capability change follows this prerequisite.

CPU state transaction evidence (2026-10-03): host/Null8/8 and Wasm/browser5/5
pass. Both feature modes validate6 exact original Buffer/Texture streams, cold
peak476 CPU bytes, hot allocation0, committed/pending/untouched control checks,
foreign journal conflict, rollback ABA and actual loss pending-journal rollback
with independent graph/transaction quota retirement. Terminal invalidation is
covered in core/control tests; submitted native quarantine remains unimplemented
and unverified. Evidence original-state-transaction-* YAML/log and consolidated
original-stream-continuation-summary.yaml. Contract Complete / Prototype Complete
only; no native dependency equivalence, Backend/Platform/Production promotion.

## OriginalTransferDependencyProfile/1 and TransferDependencyLowering/1

Status: Contract Complete / Prototype Complete for lowering metadata; native
payload integration and validation remain incomplete. Public/general/bindless execution and NativeSync remain closed.

L2 intent is an ordered whole-resource dependency on one exact registered source,
followed by one copy. The selected full original stream contains exactly six
commands: two dependencies, BeginTransferPass, one typed copy, EndTransferPass,
Finish. Each dependency is BufferBarrier/TextureBarrier or the matching memory
barrier; their exact original references/descriptors must precede the copy and
refer respectively to its source and destination. Aliasing, ownership handoff,
partial texture state, extra stages/access, host visibility, shader/attachment/
present state or commands outside this language fail deterministically. CopySrc
uses exactly Transfer/TransferRead, CopyDst exactly Transfer/TransferWrite.
Undefined-before discard uses exactly TopOfPipe/None and cannot be a memory
barrier state. Texture transitions use the declared complete resource range.
Discard permits losing old contents; it does not request zero initialization.

WebGPU specification Resource Usages, Synchronization and GPUQueue.submit
provide standalone copy validation and ordered queue command semantics on one
device queue. GPUQueue.submit requires: "For each commandBuffer in commandBuffers:
Execute each command in commandBuffer.[[command_list]]." Copy commands are
standalone operations with their own self-race validation. This is the
version-controlled normative basis
for an ImplicitOrderedUsage decision on these exact transfer scopes. Such a
barrier needs no explicit WebGPU API command; equivalent semantics come from
actual copy commands in the original encoder on its retained device queue, not
from a CPU replay or a successful empty barrier. Memory barriers preserve logical
state. Transition publication is exclusively OriginalDependencyStateTransaction/1
plus atomic CompletionRetentionPool/2 admission. A lowering decision is planning
metadata and never a recorded proof, capability bit, completion or native sync.

Normative references: https://www.w3.org/TR/webgpu/#programming-model-resource-usages,
https://www.w3.org/TR/webgpu/#programming-model-synchronization and
https://www.w3.org/TR/webgpu/#dom-gpuqueue-submit. Version1 deliberately narrows scope
to Transfer; no inference from OS/backend name, optional features or vendor limits
is permitted. The WebGPU mapping is a versioned contract guarantee; native device,
resource usage, copy format/range and queue identity still require exact checks.

| Backend | Version1 selected profile lowering decision | Execution before native integration |
| --- | --- | --- |
| Null | DiagnosticFailure (validator only; no native payload) | DiagnosticFailure |
| Vulkan | DiagnosticFailure (this selected private profile has no payload) | DiagnosticFailure |
| Metal | DiagnosticFailure (this selected private profile has no payload) | DiagnosticFailure |
| WebGPU | Equivalent / ImplicitOrderedUsage after exact scope validation and explicit profile opt-in | DiagnosticFailure until common recorder and submitted/loss evidence |

Stable diagnostics distinguish version, opt-in, unavailable backend payload,
unsupported scope/state/range, owner/source coverage and language. No fallback
changes output or memory budgeting: one original encoder, one copy, one debug
scope and one command buffer use separate existing native quotas; state entries
and transition counts use the separate transaction CPU quota. Recovery after
failed unsubmitted recording abandons only the own journal; after submitted
unproven work all declared controls stay terminally quarantined and recovery
requires new resource owners. Actual device loss retires pending or committed
transactions under those same rules. Scope callbacks, queue completion, resource
lifetime and signal publication remain existing independent proof requirements.

Tests first verify exact descriptor decisions across all4 backends, default
opt-out/unknown version/backend failure, transfer state/scope/range negatives,
immutable original six-command binding and allocation0. Fast contract/unit and
Null hidden-frame plus both browser modes must pass before marking this lowering
prerequisite Contract Complete. Actual native results, scopes/drop/loss and
submitted quarantine remain mandatory for Backend Validated; RenderDoc remains
required for visual acceptance, and this step supplies no Platform/Production
promotion.

Selected dependency lowering evidence (2026-10-03): fetched WebGPU normative
Synchronisation and GPUQueue.submit text is preserved in original-dependency-
normative-source.txt. Host/Null10/10, Wasm/browser5/5 and inventory8/8 pass. Both
modes validate6 exact original graphs, reject forged owner and unsupported shader
state, preserve initial revisions and allocate0; all6 native recorder attempts
still reject before encoder/reservation. Reports have errors=[] and
transferDependencyContractPassed=true / originalDependencyLoweringCases=6.
Evidence original-dependency-lowering-* YAML/log/XML. Mapping metadata only; no
native dependency execution proof or Backend/Platform/Production promotion.

## OriginalDependencyInitialMapping/1

Status: Contract Complete / Prototype Complete for CPU mapping validation.
Native dependency execution remains closed; e2c2 integration remains incomplete.

Initial mapping queries validate exact immutable original input/resource owners,
command order, descriptor-to-copy references, initial state sequence, whole-range
Transfer lowering and frozen labels. They deliberately do not assert that those
initial observations are current committed resource state. Explicitly named
validateInitialSequence/validateInitialMapping APIs express this distinction;
there is no skip-validation flag, reissued state snapshot or metadata-to-proof
path. Default validate continues to require exact current committed observations.

Only the separately owned OriginalDependencyStateTransaction can validate the
current changed controls against its exact own pending or accepted committed
final observations. Every untouched control must remain at its exact initial
committed facts. Its validate combines immutable initial-sequence validation
with those current facts. Initial mapping success alone cannot pass recording,
submission, capability, generic execution or NativeSync admission. Device loss,
resource-owner/descriptor mismatch still fail the immutable mapping query.

All4 backends retain the previous outcomes: CPU mapping metadata only, native
selected dependency execution DiagnosticFailure. Tests must show initial mapping
stays stable across own pending/commit/rollback while default committed validation
rejects stale initial snapshots and transaction current validation rejects foreign
or untouched pending/stale facts. Fast unit/contract, Null hidden-frame and both
actual browser modes retain allocation0 and actual loss paths. No native or
Backend/Platform/Production claim is introduced by this separation.

Initial mapping evidence (2026-10-03): host/Null10/10, Wasm/browser5/5 and
inventory9/9 pass. Both feature modes report originalInitialMappingCases=6,
originalStateTransactionCases=6, actual transaction device-loss cleanup=true and
nativeErrors=[]. Allocation-free original mapping stays stable through own
pending/commit/drop/rollback and foreign untouched stale facts, while committed
initial validation and exact current transaction admission reject. Actual loss
rejects immutable source mapping and retires independent source/transaction
quotas. Evidence original-initial-mapping-* YAML/log/XML. Native integration is
still unimplemented and closed; no Backend/Platform/Production promotion.

## OriginalTransferDependencyBatch/1 native integration

Status: Contract Complete / Prototype Complete / Backend Validated for this
selected private WebGPU profile only. Default public
and general execution/NativeSync remain closed. This explicit private Prototype
profile consumes OriginalTransferDependencyProfile/1, TransferDependencyLowering/1,
OriginalDependencyStateTransaction/1 and CompletionRetentionPool/2.

Cold preparation validates the complete original six-command input, exact native
resource/device/queue and copy semantics, then freezes both dependency decisions
in the existing recorder. It creates one independent quota-bound transaction and
stores that exact owner in recorder, native command payload and completion graph.
Transaction control/count/CPU quota is separate from recorder encoder/command/
debug-scope/callback CPU quota and retained original allocation budgets. Cold
preparation opens zero encoders. Missing/forged/stale owners or unsupported
language/decision fail before reservation or GPU calls. There is no new proof
issuer, generic dispatcher flag, frozen cursor bypass or metadata-issued GPU proof.

The existing recorder alone may synchronously apply its exact Prepared transaction
and consume each frozen Equivalent/ImplicitOrderedUsage decision while recording
one real copy, the exact frozen Transfer label and Finish on the original queue.
No barrier API no-op is treated as work: the approved ordered usage mapping is
consumed alongside the actual native copy payload. Original initial mapping is
immutable; current changed controls must match this own Pending transaction and
all untouched controls remain committed/current. All actual recording error scope
callbacks must succeed before the existing recorded proof is usable. Failed scopes
or unsubmitted wrapper drop abandon only the own journal, advancing revisions.

Common submission verifies the same proof/native command/transaction/completion
graph and exact own Pending state, then commits the transaction through submitWith
on the exact reserved ticket. Admission rejection never publishes state; no other
fallible pool admission step follows accepted commit. Commit does not prove GPU
completion. The existing actual queue+scope evidence batch still governs signal
publication and graph retirement. Unproven submitted work permanently quarantines
all declared original controls before retaining the graph; it never claims rollback
restored GPU contents. Device loss calls the same transaction invalidation before
pool retirement: Pending/Prepared abandons, Committed terminally invalidates. Graph
identity validates immutable owners, not mutable state that later accepted queue
commands may advance. New submission always validates its own exact current facts.

| Backend | Selected private native integration |
| --- | --- |
| Null | DiagnosticFailure; validator tests cannot issue native proof |
| Vulkan | DiagnosticFailure; selected profile payload not implemented |
| Metal | DiagnosticFailure; selected profile payload not implemented |
| WebGPU | Prototype opt-in, Equivalent ordered transfer usage plus actual copy/recording/submission scopes; real backend tests passed for this selected profile |

Tests must cover all four copy kinds and the access-only dependency, explicit
budgets/owners, cold encoder0, hot allocation0/group0, actual readback, real native
recording errors with own rollback, untouched pending/stale submission rejection,
unsubmitted drop, actual submitted-unproven quarantine on isolated original owners,
and actual pending/committed device loss graph+quota retirement. Fast contract/Null
and both real browser modes are mandatory; visual acceptance still requires
RenderDoc and is unavailable with RHI-RENDERDOC-TOOL-UNAVAILABLE. No Platform or
Production promotion follows this private profile, and remaining source families
and four-backend/platform obligations remain in the full bindless plan.


Verified 2026-10-03: host/Null10/10, Wasm/browser5/5 and source inventory10/10.
Both actual browser modes passed six original batches / four copy kinds / actual
readback, twelve native recording errors with own rollback, wrapper drop,
untouched pending/stale admission and completion of A while B owns pending state.
Two isolated actual submitted-unproven probes permanently quarantine all five
controls; actual pending/committed loss retires four original resource graphs and
all independent CPU quotas. Cold encoder0, recorder peak920 bytes, independent
exact/one-less transaction quota and hot allocation0/group0. Cold test profile
frames were separated; Asyncify capacity stays16384 bytes. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/original-dependency-native-report.yaml`
and corresponding YAML/log/XML. RenderDoc unavailable with the diagnostic above;
no visual acceptance, Platform Validated or Production Ready claim.


## OriginalBufferUploadInput/1 metadata prerequisite

Status: Contract Complete / Prototype Complete for CPU metadata only. This freezes
input only; staging provenance/readiness/native execution remain DiagnosticFailure
on Null, Vulkan, Metal and WebGPU. No capability, native proof or queue admission
may be inferred from this capsule. Original full input still rejects BufferUpload
and generic projection still rejects the Upload domain until exact source ownership
is implemented.

The source is the existing L2 UploadBufferDesc. Capture preserves its source upload
buffer, destination resource, exact size, staging intent, backendOwnedStaging and
frameSerial. It composes the existing TransferCopyArguments value capture for the
explicit zero-offset source/destination/size, with the remaining staging fields;
there is no second transfer descriptor parser or implied staging reservation.
Capture is representation only: zero size, invalid resource IDs, unknown intent,
opt-out and maximum frame/size values are preserved rather than repaired. Later
original provenance and native lowering must validate them deterministically.

Label belongs to the independently quota-bound CommandVariableArguments text
store. BufferUpload accepts None scalar only because complete descriptor/source
fields live in their separate capsule; None and a label never prove a staging
owner. Text remains an exact byte string at this metadata layer; native profiles
must additionally enforce their encoding contract. Reconstruction fills only
capsule-owned scalar fields, preserving the output label; comparison requires
both capsule fields and independently frozen label. Hot capture/compare/write
allocate0; cold label storage uses existing explicit command/text/CPU quotas.

CommandUsage/1 requires exactly Buffer(source), Buffer(destination), Upload(staging)
references. The Upload reference must eventually bind the backend-owned source
reservation/pool, frame serial and original allocation lifetime. CopySrc usage,
MemoryUsage, backend name, a caller boolean or another ordinary owner alone cannot
supply that evidence. BufferUpload cannot be silently treated as BufferCopy.
Staging retirement, completion and all native profile budgets remain independent.

Quick contracts and both real browser smoke paths must verify all field mutation,
unknown/maximum values, caller label mutation/drop, exact/one-less text/CPU quota,
allocation0 and unchanged source/execution rejection. No visual output is changed;
RenderDoc remains RHI-RENDERDOC-TOOL-UNAVAILABLE, without visual acceptance.


Verified 2026-10-03: six scalar fields preserve exact invalid/unknown/maximum
values; capture/compare/writeScalarFields allocate0. Existing variable store cold
label peak207 bytes on the pinned Wasm ABI; independent exact/one-less quotas,
Unicode caller label mutation/drop and hostile-default/exhausted hot allocation0
pass. Original BufferUpload family stays Unsupported and Upload domain fails
before allocation. Host/Null10/10, Wasm/browser5/5 and source inventory11/11,
both bufferUploadInputPassed=true, diagnostics=[]/nativeErrors=[]. Evidence
`build/web-bindless-apple-debug/artifacts/bindless-continuation/original-buffer-upload-input-report.yaml`
and corresponding YAML/log/XML. These are metadata checks, without Backend,
Platform or Production promotion. Next: exact original staging provenance;
existing WebGPU pool/slice/serial metadata does not bind a sourceUploadBuffer to
an original allocation/native buffer/ready payload. MemoryUsage, CopySrc, pool
presence or the caller-owned boolean cannot replace that missing binding.


## OriginalBufferUploadSource/1 provenance and readiness

Status: Contract Complete / Prototype Complete / Backend Validated for the
selected private WebGPU source preparation only (2026-10-03). This is
an explicit backend-private Prototype source profile, with public Upload-domain,
default/public input dispatch, recording and NativeSync still closed. The selected
private full input/recorder integration is documented below. Source readiness
cannot issue recorded command proof.

L2 source intent is immutable initialized upload storage for one exact nonempty
byte payload and caller frame serial. The backend-owned source pool creates its
own original buffer and allocation; it never adopts caller buffers, guesses role
from CopySrc/MemoryUsage, or converts an accounting-only slice into a GPU source.
The source purpose is the versioned factory/ConstructionKey binding. Pool profile
version/opt-in, simultaneous source count and native allocation budget are explicit
and independent of existing slice/queue-write budgets. Source metadata/callback
quota, CPU payload-copy quota and original RHI resource-metadata quota are
independently supplied. Cold prepare copies bytes and reserves count plus native
allocation budget without GPU calls; explicit initialize performs native creation/
write/scopes under the resource quota. Resource-metadata allocation failure during
initialize rejects the source and drains its actual scopes; it never returns ready
placeholder storage. Every exact owned
resource, allocation release token, pool, registry, write seal, device and queue is
retained. Failed cold admission opens no GPU calls; full source budget returns a stable
diagnostic and does not temporarily allocate outside the ledger.

Selected WebGPU lowering creates exactly sized CopySrc|CopyDst storage with
CpuToGpu memory intent and initial CopySrc state, initializes bytes with the
existing native queue-write helper at zero offset before publication, and waits
for actual queue completion plus the Internal/OutOfMemory/Validation scopes that
cover create/register/write. WebGPU GPUQueue.writeBuffer validates four-byte size
alignment and copies supplied data before return; no CPU-immediate completion or
unassociated semaphore is accepted. The explicit four-byte alignment derives
from WebGPU queue-write validation; payloads are never rounded, padded or truncated.
The initialized source is immutable: all host, shader and transfer writes are
sealed before original observation capture. The captured exact native payload,
allocation, seal, original resource ID and factory pool define provenance.
Frame serial is equality-only input identity, never inferred native completion.

Callback metadata is allocated cold before GPU work. CompletionEvidenceBatch joins
four named evidence participants once each. Prepared and Pending return stable non-ready
diagnostics. Native failure returns Rejected; unresolved native evidence never
publishes Ready. The source's own original observation is revalidated before Ready
and at each hot lookup. Queue callback receipt alone is insufficient. Actual device
loss fails closed regardless of earlier status; pending callbacks retain original
source owners until their actual completion, and no later callback can promote
Ready. Dropping the unconsumed source retires its exact native allocation and pool
source count after the last original/callback owner. No aliasing, pooling reuse or
source rewrite is introduced. Recovery after rejection/loss requires a new factory
source. The identical source owner is retained by the selected full original
Upload graph/recorder/submission described below; general integration stays closed.

| Backend | Selected OriginalBufferUploadSource/1 lowering |
| --- | --- |
| Null | DiagnosticFailure; validator-only cannot issue ready native source |
| Vulkan | DiagnosticFailure; selected original source payload not implemented |
| Metal | DiagnosticFailure; selected original source payload not implemented |
| WebGPU | explicit Prototype opt-in / Equivalent queue-write initialization plus actual native buffer, queue and scopes; actual byte-readback/error/loss tests pass |

Required tests: cold count/GPU/metadata/payload/callback budgets; exact/one-less and
exhausted quota before native work; original/forged pool/registry/source identities;
pending scope/queue evidence; real initialized-byte readback; failed native create
or write without ready promotion; frame/size/resource/allocation/native mutation;
source and pool wrapper drop; actual pending/ready device loss and exact native
allocation/count/CPU quota retirement. Hot observation uses exhausted quotas and
null default PMR with allocation0. Fast contract/Null and both actual browser modes
are mandatory. RenderDoc remains unavailable with RHI-RENDERDOC-TOOL-UNAVAILABLE;
no visual/Platform/Production or generic/public/native-sync capability promotion.

Verification: Host/Null10/10, Wasm/browser5/5 and source inventory12/12 pass.
Both actual browser modes pass all four source markers with diagnostics=[] and
nativeErrors=[]. Actual source creation and byte initialization, independent
quota pressure, callback joins, native failure, Prepared/Pending/Ready loss and
exact retirement are verified. Evidence: original-upload-source-* under
build/web-bindless-apple-debug/artifacts/bindless-continuation/. Subsequent
projection/full-input/native stages below complete selected private Upload
graph/recorder/submission ownership.

## OriginalBufferUploadProjection/1 ownership prerequisite

Status: Contract Complete / Prototype Complete for ownership metadata, verified
2026-10-03 with the actual WebGPU source payload. This is an explicit
private Prototype ownership profile. It does not open original BufferUpload
input dispatch, native recording, public execution or NativeSync.

The generic command graph may expose an Upload reference only with independently
declared nonzero maxOriginalUploadSources and maxOriginalUploadBytes. Default
budgets preserve the Upload-domain unproven diagnostic. The appended source
inventory retains original Ready OriginalBufferUploadSource/1 owners; raw caller
pointers, aliased control blocks, Prepared/Pending/Rejected/lost sources and
uncovered transfer resources fail closed before graph allocation. Every source
must join exactly its original ID, buffer payload/control block and allocation
owner to the original Transfer retention root. It must use the same native device
as the complete graph. Duplicate source IDs are rejected rather than consuming
an inferred or ambiguous staging role. Upload references contain that exact ID
and source index and resolve to the source capsule, while Buffer references
continue to resolve to the original Transfer native payload.

Source count, Upload reference count, upload bytes, complete allocation-reference
count, total retained allocation bytes and graph CPU metadata have independent
limits. Each source contributes an allocation record to SharedAllocationProjection;
its identical Transfer record deduplicates to one original ledger charge. The
source does not add a second state-control observation: its exact Transfer join
already supplies that control. Source frame/size are immutable facts, but their
command descriptor equality remains a separate original-input prerequisite.

All hot graph lookup/usage consumption revalidate source readiness, original
owner, exact Transfer join, registry liveness and device loss without allocation.
The graph retains the complete source capsule, pool/registry, copied bytes,
native allocation and seals through wrapper drop; final graph drop or actual
device loss retires them only after their last original owner. No command proof
issuer or execution payload is added.

Four-backend outcome: Null, Vulkan and Metal DiagnosticFailure (selected native
source absent); WebGPU explicit Prototype / Equivalent ownership projection of
the actual source. Required quick/Null and both actual browser modes cover all
independent quotas, forged owners, wrong transfer/resource/index, readiness,
byte-accounting deduplication, mutation, hot allocation0, wrapper drop and actual
loss. RenderDoc remains RHI-RENDERDOC-TOOL-UNAVAILABLE; no visual/Platform/
Production acceptance.

Host/Null10/10, Wasm/browser5/5 and source inventory13/13 pass. Both browser
modes validate exact source/Transfer joins,3 allocation references deduplicated
to2 original charges/32 bytes, frozen usage, hostile-default/exhausted-quota hot
allocation0 and actual graph loss/drop retirement. Evidence:
original-upload-projection-* in bindless-continuation. This ownership prerequisite
alone grants no recording or full-input authority; those stages are below.

## OriginalBufferUploadFullInput/1 descriptor and state joins

Status: Contract Complete / Prototype Complete, verified 2026-10-03. Metadata-only private
Prototype profile; recording, submission and public/general execution stay closed.

The full original input has a distinct BufferUpload descriptor family, preserving
all six scalar fields through BufferUploadArguments and its exact separate frozen
label in CommandVariableArguments. Exactly three positional references bind
Transfer source Buffer, Transfer destination Buffer and the original Upload source
capsule. The Upload source index/ID must bind the same source as the descriptor.
Only StagingIntent::Upload and explicit backendOwnedStaging=true are accepted;
neither issues provenance. Source frame serial and size must equal the immutable
Ready factory source exactly. This profile uploads the complete payload at zero
source/destination offset; no truncation, guessed frame or slice semantics.
TransferCopyArguments is composed for existing native resource/range/write-seal
validation; it is not used to erase the descriptor's upload semantics. Self copy,
sealed destination, missing/wrong resource and arbitrary ordinary source fail.

The complete state graph observes exactly the existing source and destination
Transfer controls once each. Ordered state validation requires source CopySrc and
destination CopyDst at the BufferUpload command, using existing stateBefore and
RecordedResourceStateSequence. Pending/stale/lost controls remain independent
failure gates. Source readiness does not imply current CopySrc state or recording.
Exact frozen descriptor and label mutation, independently bounded full-input and
state CPU quotas, positional references and original owner/loss/drop are tested
in quick/Null and both actual browser modes. No proof issuer is added.

Four-backend outcome: Null/Vulkan/Metal DiagnosticFailure for this selected
source-dependent input; WebGPU private Prototype / Equivalent exact metadata
joins with actual source. This prerequisite grants no native emission authority;
the separately validated recorder/common submission integration is below.

Host/Null10/10, Wasm/browser5/5 and inventory14/14 pass. Both actual browser
modes validate two complete input graphs, all6 raw scalar/label changes,
independent exact/one-less input/state CPU quotas, state-control deduplication,
CopySrc/CopyDst sequence, pending/stale rejection, hot allocation0 and full graph
loss/drop retirement. Evidence: original-upload-full-input-* under
bindless-continuation. The existing copy-only OriginalTransferStreamValidation
still rejects BufferUpload with RHI-WEBGPU-ORIGINAL-TRANSFER-LANGUAGE.

## OriginalBufferUploadBatch/1 native recorder and submission

Status: Contract Complete / Prototype Complete / Backend Validated for the selected
private WebGPU profile (2026-10-03). Selected private
Prototype profile only; public/general execution and NativeSync remain closed.

The exact stream is BeginTransferPass, BufferUpload, EndTransferPass, Finish.
Its complete graph has one Ready source capsule, two original Transfer buffers,
three positional references, one frozen BufferUpload descriptor and exactly two
original state controls. Source and destination are distinct, with CopySrc and
CopyDst at the upload command. Separate version/opt-in, original command count,
native encoder/copy/debug-scope/command-buffer/callback/CPU limits and original
transaction CPU/control limits are mandatory. Two native debug scopes preserve
both the frozen transfer label and upload label. Both labels must be valid UTF-8
without embedded NUL before reserving any native work.

Only existing WebGpuOrdinaryRecording issues the recorded proof. Cold prepare
retains the identical original state/input/graph/source owner, creates no native
encoder or copy, and allocates callbacks and an existing original state
transaction with two controls and zero transitions. Recording applies that
transaction, opens one native encoder, emits the existing exact
WebGpuNativeTransferCopyLowering copy at both zero offsets and full source size,
and finishes one real command buffer. Three actual scopes prove native recording;
any failure abandons the unsubmitted transaction and denies command proof.
Upload metadata is never rewritten into a copy-only original input graph.

Existing common submission consumes the exact recorded graph and commits the
original transaction through submitWith before actual queue submission. Actual
queue plus all three error scopes join completion; CPU commit or queue receipt
alone cannot publish a signal. Recording/submission/drop retain the original
upload capsule through its copied CPU bytes, pool/registry, GPU allocation and
write seals. Pending/stale/foreign state rejects before submit. Submitted-unproven
completion quarantines both original controls and retains the submitted graph
until actual device loss. Loss before submit abandons the unsubmitted transaction;
loss after submit invalidates both original controls and retires all original
source/resource/count/GPU/CPU owners. No new recorded-proof friend is permitted.

Null/Vulkan/Metal selected native payload outcome is DiagnosticFailure. WebGPU
explicit Prototype / Equivalent upload is the proven Ready queue-write source
followed by the actual native exact buffer copy and common queue/scopes. This
profile does not claim pooling reuse, general Upload, other source families or
production platform readiness. Required fast contract/Null and both actual
browser modes cover real bytes/labels/arguments, cold encoder0, hot allocation0,
quota failure, native recording errors, drop, pending/stale submit, actual
submitted-unproven quarantine and actual loss retirement. RenderDoc remains
RHI-RENDERDOC-TOOL-UNAVAILABLE; no visual/Platform/Production promotion.

Verification: Host/Null10/10, Wasm/browser5/5 and source inventory15/15 pass.
Both actual modes pass exact16-byte native readback, both frozen labels,4 label
encoding failures,2 actual recording errors, prepared/recorded drop, pending/
stale submit, queue-only receipt rejection, actual submitted-unproven quarantine
of both controls and recorded-unsubmitted/submitted-pending actual loss. Cold
encoder0, recording CPU peak920 bytes, independently bounded transaction peak260
bytes and hot allocation0. All source/count/GPU/CPU owners retire after actual
callbacks. Evidence: original-upload-native-* in bindless-continuation. No new
proof issuer or public/general/NativeSync capability is introduced. The complete
bindless plan and remaining original source families remain open.

## OriginalTextureUploadInputSnapshot/1 complete CPU input

Status: Contract Complete / Prototype Complete for CPU input, verified 2026-10-03. Metadata-only
explicit Prototype prerequisite. TextureUpload full original family, native
recording/submission, public/general execution and NativeSync remain closed.

L2 input capture preserves all UploadTextureDesc scalar fields: destination ID,
extent, destination offset, format, row pitch, rows per image, independent mipLevel
and arrayLayer, complete subresources, pixelDataSize, staging intent, backend-owned
flag, frame serial and resource generation. Capture performs no semantic repair,
format inference, effective-mip selection, zero-origin expansion or validation
by destination usage. Unknown/zero/maximum values remain exact metadata, not GPU
permission. Labels use the existing separately bounded CommandVariableArguments
TextureUpload/None text record; only that operation's metadata representation is
extended. Pixel bytes never occupy the PushConstants byte domain.

One original snapshot retains pointer-free scalar fields, every ordered per-mip
layout (including exact extent/pitches/offset), the exact label owner and an
independent immutable copy of all pixelDataSize bytes, including declared padding.
The raw caller pointer address is not retained and cannot issue source provenance.
Its presence is recorded as an explicit CPU input fact. A nonzero size with null
pointer fails deterministically before allocation; zero-byte null and nonnull
inputs retain their distinct presence fact without inventing storage. Readable
caller memory is externally serialized during capture/compare. Equal bytes at a
different pointer are equal content; pointer address is never an allocation or
resource identity. Per-mip data is never flattened, sorted, rounded or discarded.
Malformed/unsupported format/layout semantics remain representable metadata and
must be rejected by the subsequent source/region/lowering contract.

Version/opt-in, maximum mip records, label bytes, copied pixel bytes and three
independent exact CPU quotas (scalar/mip/object metadata, label owner metadata,
copied pixel storage) are explicit. Count/size/pointer/quota failures precede copy
and native calls. Original owner uses QuotaBoundSharedObjectIdentity and a private
ConstructionKey. Hot scalar/mip/label/content comparisons allocate0 and never
read native state. Snapshot lifetime outlives caller label/mips/pixels; its final
original owner releases all three quotas. Retaining it across actual device loss
does not grant GPU authority: CPU input remains readable until its own retirement.

Null/Vulkan/Metal/WebGPU all support Equivalent CPU input representation only;
all four selected native TextureUpload source/execution outcomes remain
DiagnosticFailure. No ready source, native payload, completion epoch, proof friend,
generation authority or generic Upload reference is added. Required quick unit/
Null and both actual browser modes cover each scalar/subresource/mip/label/pixel
mutation, unknown/maximum values, copied nonempty per-mip records, independent
exact/one-less/exhausted quotas and declared counts/bytes, missing pointers, forged
owners, caller drop, retained snapshot/actual loss and hostile-default hot
allocation0. RenderDoc remains RHI-RENDERDOC-TOOL-UNAVAILABLE; no visual/Backend/
Platform/Production promotion for CPU input capture.

Verification: Host/Null10/10, Wasm/browser5/5 and inventory16/16 pass. Both actual
modes retain2 nonempty ordered mip records and24 pixel bytes with unknown/maximal
metadata; all22 scalar and7 per-mip field mutations reject, label/pixel mutation
and caller drop are verified. Independent CPU peaks268/212/24 bytes on pinned
Wasm, exact/one-less/exhausted quotas, hot allocation0 and actual retained input
loss retirement pass. Evidence: original-texture-upload-input-* in
bindless-continuation. No GPU/source/generation authority or backend readiness.

## OriginalTextureUploadPacking/1 deterministic CPU layout

Status: Contract Complete / Prototype Complete for deterministic CPU layout and
packing, verified2026-10-03. This explicit Prototype prerequisite grants CPU
layout facts only, never GPU source authority.

L2 requires a nonempty original input, destination ID, Upload intent and
backendOwnedStaging=true. A destination layout value supplies exact extent,
format, usage, memory, initial state, mip count, physical layer count, sample count
and cube compatibility; it is a comparison value, not a registered resource or
generation owner. Destination format must equal the input color format, CopyDst
must be declared, samples must be1, and all dimensions/counts must be nonzero.
Depth/stencil and unknown formats fail. Depth>1 denotes3D under the existing L2
texture contract, requiring one array layer and no cube compatibility; otherwise
depth=1 denotes2D with explicit physical layers. Cube-compatible2D requires square
extent and physical layers divisible by6. Mip count cannot exceed the bit width of
the largest destination dimension. Format byte/block size comes only from shared
RHI format metadata, including BC1/3/7 and sRGB variants.

Independent mipLevel and arrayLayer must equal subresources.baseMipLevel and
baseArrayLayer, including zero. No effective-mip/layer fallback is allowed. Empty
mipLayouts means exactly one declared mip with source offset0 and top-level
extent/pitches. Nonempty layouts must contain exactly mipLevelCount entries,
ascending contiguous mip numbers starting at baseMipLevel; the first record's
extent/pitches must equal the top-level fields. Every later extent/pitch is
explicit, never inferred from the first. One destinationOffset applies verbatim
to every mip in texels, without implicit scaling. Every mip region must fit its
actual destination mip; the complete layer range is validated independently.
3D uses extent.depth slices;2D uses arrayLayerCount with extent.depth=1. BC origins
must align to blocks; partial terminal blocks are permitted only at the actual
mip edge, and BC3D is rejected. This CPU rule is not evidence of a native BC copy.

Source rows are block rows for BC and texel rows otherwise. bytesPerRow must be
nonzero, a multiple of the format byte unit, and cover rowBytes; rowsPerImage
must cover rowCount. Every source mip declares a full footprint
bytesPerRow*rowsPerImage*sliceCount, including the last slice's padding. Offsets
must align to the format byte unit, footprints must fit copied pixel bytes, and
source footprints must occur in nonoverlapping ascending order. Gaps/trailing
bytes and all source padding remain in the retained exact original snapshot.
Malformed fields, arithmetic overflow, overlaps and truncation fail before
packing/allocation/native calls. No layout repair or pitch inference occurs.

The versioned lowering output explicitly repacks meaningful rows into immutable
CPU staging bytes. Packed row pitch is ceil(rowBytes/256)*256, using the existing
L2 kTextureUploadRowPitchAlignment (the normative WebGPU buffer-copy alignment).
Packed rowsPerImage=rowCount, each mip follows the previous full packed footprint,
and every packed mip offset is consequently256-aligned. All new padding is zero,
explicitly defined as staging bytes outside the copied texels, never copied source
padding or invented image data. Checked core ranges/strides prove every footprint
and row address before copying. The plan retains every original mip's range,
extent, origin, source and packed footprint, exact original input owner, and the
destination layout comparison value. Caller drop/mutation cannot change it.

Version/opt-in, maximum mip records/packed bytes and independent exact CPU metadata
and packed-byte quotas are mandatory. Hot validation and comparison allocate0;
packing occurs only in cold preparation. There is no GPU allocation, queue work,
readiness, generation authority, graph Upload source or proof issuer. Execution
fails RHI-TEXTURE-UPLOAD-ORIGINAL-SOURCE-UNPROVEN. Actual device loss cannot turn
this CPU value into a ready source; retained CPU owners retire their exact quotas.

| Backend | CPU region/packing result | Selected native execution |
| --- | --- | --- |
| Null | Equivalent shared validation/immutable packing | DiagnosticFailure; validator only |
| Vulkan | Equivalent shared validation/immutable packing | DiagnosticFailure; native mapping unproven |
| Metal | Equivalent shared validation/immutable packing | DiagnosticFailure; native mapping unproven |
| WebGPU | Equivalent shared validation/immutable packing | DiagnosticFailure; generation/readiness/recording unproven |

Required verification: quick contract/unit and Null hidden frame, both actual
browser feature modes, all mip/region/pitch/range/format negative cases,2D arrays,
3D slices, BC edge blocks, source padding/gaps and explicit zero staging padding,
exact/one-less/exhausted independent quotas, forged original owner, caller drop,
retained CPU packing across actual loss, and hostile-default hot allocation0.
No Backend/Platform/Production or visual promotion from CPU packing.

Verification: Host/Null10/10, Wasm/browser5/5, inventory17/17; both browser modes
clean. Two mips retain160 original pixel bytes and1536 packed bytes. Wasm
metadata/packed peaks344/1536 bytes, independent exact/one-less/exhausted quotas,
all color formats,2D arrays/3D slices/BC edges, negative layout cases, caller drop,
actual retained CPU packing loss retirement and hot allocation0 pass. Initial
missing-quota fixture dereference was corrected; initial failure logs/XML remain.
Evidence: original-texture-upload-packing-* in bindless-continuation. Native
TextureUpload, generation authority and Backend/Platform/Production remain open.

## OriginalTextureUploadGeneration/1 admission boundary

Status: Contract Complete / Prototype Complete for the missing-authority rejection
boundary only; original native destination/content revision owner is not implemented.

Repository producers use UploadTextureDesc.resourceGeneration as authored content
revision: terrain tile generation, terrain DDGI generation and
CanonicalConsumerImageContentContract::Revision. It is not a native-object
incarnation ID, resource ID, frame serial, queue completion epoch or recorded
ResourceState revision. Reinterpreting it as a newly minted texture creation
counter would silently change existing L2 meaning and is prohibited.
CanonicalConsumerImageContentContract explicitly says supplied revision IDs are
not GPU proof and describeUpload grants no writable resource/native ticket.
Its single-slice view revision cannot authorize arbitrary multi-mip/multi-layer
original uploads by taking just the first mip/view. TextureUploadPacking's exact
CPU input and destination layout comparison also grant no generation authority.

The versioned admission gate requires explicit Prototype opt-in and the original
quota-bound packing owner. For each known backend it returns executable=false
and RHI-TEXTURE-UPLOAD-ORIGINAL-GENERATION-OWNER-UNPROVEN, regardless of the exact
preserved generation value (zero, nonzero or maximum). Null is validator-only;
Vulkan/Metal/WebGPU have no approved revision payload. Unknown backend/version,
missing opt-in and missing/forged packing owners have separate deterministic
diagnostics. No caller descriptor, canonical structural Revision, matching
native handle or content bytes may mint the missing owner. The gate does no
allocation, mutation or GPU work and cannot issue any recorded-command proof.

The remaining binding implementation must retain an original content revision
authority and exact destination/resource/allocation/native-device owners. Its
domain includes the complete ordered mip/layer/aspect regions, explicit origins,
format/layout and immutable source content; it must preserve external authored
revision semantics without substituting a creation/state/queue counter. Define
expected and proposed versions plus exclusive writer ownership before any native
work. Reuse core::VersionedMutationValue/core::MutationJournal for bounded,
serialized revision transitions; preserve original authority identity across
prepare/record/submit/completion. Other writes to overlapping regions must
invalidate or refuse admission. Abandon/error/stale/drop/loss/quarantine must
retire or terminally invalidate the exact version transaction, and GPU content
readiness requires actual accepted submission/completion/error-scope evidence.
No CPU version write or rollback can prove native content. Four backend lowering
contracts and independent count/CPU/GPU budgets precede this implementation.

Initial resource content must use a typed Uninitialized state and an explicit
initialization transaction, not infer a prior generation from zero/default bytes.
Authoring revision and internal transaction sequence are separate domains. A
completed content revision becomes observable only after its exact submitted
write is accepted and native completion/error evidence succeeds. Pending versions
never masquerade as completed content, and overlapping writes cannot reuse the
same authority by merely echoing a caller revision. This required transaction is
the next implementation stage; no such owner is issued by the current gate.

Required current verification: all four explicit failures, zero/nonzero/maximum
generations, structural canonical Revision without native authority, forged/missing
packing owner, version/opt-in and unknown backend, and hot allocation0 in the
quick contract and both actual browser smoke modes. Full f2b2 remains open.

Verification2026-10-03: Host/Null10/10, Wasm/browser5/5, inventory18/18; both modes
clean and generation gate markers present. All4 backends reject each of zero,
nonzero and maximum content revisions despite a valid original CPU packing;
structural canonical revision cannot grant execution. Missing/forged original
packing, unknown backend/version, missing opt-in, hostile-default allocation0 and
the unchanged gate on retained packing across actual loss are verified. Evidence:
original-texture-upload-generation-gate-* and final packing-* in
bindless-continuation. Remaining original authority/transaction/native join is
f2b2b/f2b2c, followed by f2b3 native source/recording. Full bindless plan open.

## OriginalTextureUploadContentTransaction/1 original CPU intent

Status: Contract Complete / Prototype Complete for original CPU content intent,
domain and unsubmitted transaction, verified2026-10-03. This explicit Prototype
CPU prerequisite is not a native destination/content revision owner.

An original TextureUploadContentSource retains an original quota-bound packing
and independently supplied versioned AuthoringClaim (destination ID and nonzero
external contentGeneration). The claim must equal the complete preserved upload
input's destination/generation; the factory never derives a claim by echoing the
descriptor. The result proves only original immutable proposed authoring input,
not that GPU content exists. UINT64_MAX is a valid external content version;
external versions are opaque equality values, not transaction sequence numbers.

An original TextureUploadContentLedger is explicitly initialized as a CPU intent
ledger for one destination ID/layout comparison value. It owns a bounded set of
stable content domains, each retaining its original source/packing and a
core::VersionedMutationValue<ContentValue>. Domain identity covers every ordered
mip, layer range, aspect, texel origin and extent. Source row layout/bytes/version
remain exact in the separate proposed source owner. Domains with any overlapping
destination texels conflict, including BC partial terminal blocks (compare their
physical format blocks), even if mip-list coverage differs. Identical complete
domains reuse one slot; partial overlap is refused. Adjacent disjoint domains,
different mip/layer ranges and3D z slices can coexist. Shared core checked
unsigned interval overlap/containment primitives avoid overflowing end addresses.

Cold ledger/source/domain/transaction preparation requires explicit versions,
Prototype opt-in, exact independent source/ledger/transaction CPU quotas, maximum
domains/live transactions and independent retained domain/pending source byte
limits. Retained source bytes account complete original pixel bytes plus packed
staging bytes; CPU metadata is separately quota charged. Domain slots are stable
owned objects in BoundedSequence; metadata growth occurs only during cold
preparation. AtomicCapacityLease reserves transaction count and pending source
bytes until the original transaction retires, independently of GPU completion.
Failure cannot leak a reservation or mutate a version. One ledger does not
authorize another ledger merely because destination IDs/layouts match; native
binding must later enforce a single original ledger for the original resource.

An expectation is a privately minted value bound to the exact original ledger,
domain slot and control observation. It cannot be made from a caller generation
or scalar revision pair. Cold prepare retains expected/proposed/source/domain
owners; hot apply first validates freshness and exclusively enlists that domain
in the original transaction's embedded MutationJournal. The value becomes
Proposed with its exact external generation while the internal control sequence
advances separately. Other prepared/pending writers to that domain reject.
Unsubmitted abandon rolls back to typed Uninitialized and advances the internal
sequence, invalidating old expectations. Domain/ledger invalidation is terminal,
including during rollback, and cannot revive validity. Dropping a pending
transaction abandons it; original source/domain/ledger owners outlive its journal.

ContentValue reserves Uninitialized/Proposed/Submitted/Completed states, but this
CPU prerequisite has no API that issues Submitted or Completed, no GPU signal and
no successful native admission. Domain observation reports Invalidated when the
original control is terminal, independently of rolled-back bytes. Default/zero
memory never stands for completed content. Native submission/completion will need
the exact original binding/source/recorded-command/submission/evidence chain in
f2b2c/f2b3 before any completed content can be observed. No test friend or Dummy
completion may promote this CPU state. Hot apply/validate/abandon/invalidate and
domain overlap/equality allocate0, do no native work and never block.

Null/Vulkan/Metal/WebGPU: Equivalent CPU original intent/domain/transaction only;
selected native generation/recording/execution remains DiagnosticFailure with
RHI-TEXTURE-UPLOAD-ORIGINAL-GENERATION-OWNER-UNPROVEN. Existing
OriginalTextureUploadGeneration/1 gate and TextureUpload full original dispatch
remain closed. Required quick unit/Null and both actual browser modes cover
independent authoring claim mismatch, forged/missing owners, complete multi-mip/
layer/z/block overlaps, adjacency, exact/one-less/exhausted CPU/count/retained-byte
limits, stale/foreign expectations, competing writers, rollback/drop/terminal
invalidation, opaque external versions, caller drop, actual device loss CPU
retirement, and hostile-default hot allocation0. No Backend/Platform/Production
or visual readiness promotion.

Verification: Host/Null10/10, Wasm/browser5/5, inventory19/19; both browser modes
clean. CPU peaks92/488/196 bytes on pinned Wasm for source metadata, ledger with
one domain, and transaction metadata. Independent exact/one-less/exhausted quotas,
two domain/count and1696/3392 pending retained byte boundaries, all ordered
multi-mip/layer/3D/BC overlap and adjacency cases, foreign/forged/missing owners,
competing writers, stale expectations, rollback/drop/terminal invalidation and
opaque UINT64_MAX source version pass. Core unsigned overlap has exhaustive small
interval oracle plus maximum/overflow/empty boundaries; hot allocation0.
Actual loss retains prepared/pending CPU owners, then terminal invalidation,
rollback and final retirement release all8 CPU quotas plus count/byte leases.
Initial fixture checked quotas while a weak observer still retained the original
shared control block; observer retirement was corrected and failure log retained.
A direct repository-cwd test invocation also hit the existing relative shader
artifact fixture boundary; CTest's configured build cwd passes. Evidence:
original-texture-content-transaction-* in bindless-continuation. Submitted and
Completed remain unissuable; native generation binding and full plan stay open.

## Original native texture content binding prerequisites

Status: implementation pending. Transfer observation proves original registered
resource/allocation/native object/device and scalar layout facts. It contains no
authored content revision, writer exclusion or creation-only content history.
RecordedResourceStateControl is a layout/state transaction, not content evidence.
Consequently an arbitrary existing texture cannot become content-Uninitialized
by attaching a fresh CPU ledger, checking its initialState or reading zero bytes.

The next binding must either create and retain a fresh original destination with
an explicit creation-only initialization contract, or consume an already proven
original completed content authority. Fresh binding owns one exact destination
CPU layout, registry/native resource/allocation/device, immutable source intent
and original content ledger under independent quotas. Resource creation must use
actual native error scopes; resource-creation readiness never claims authored
content completion. A unique private writer contract must reject or terminally
invalidate every overlapping transfer/upload, render attachment or writable
descriptor path before GPU recording; a mutable handle/usage flag is insufficient.
Existing GpuBufferWriteSealState is a reusable policy model, not a texture seal
and cannot be inferred from a buffer/source pool or Dummy resource.

One original ledger must be bound to that original destination. A second ledger
with equal caller ID/layout/source bytes must fail; prepare/record/submit must
consume the exact original binding. Claimed external content version remains
separate from internal control revision, resource ID, frame and completion epoch.
Submitted/Completed publication then requires the existing original recorder and
common submission/completion evidence, with no new proof issuer or test friend.
Prepared drop/abandon restores uninitialized intent; pending submit/error/loss
must follow explicit quarantine/terminal-invalidation rules. No CPU commit,
immediate queue.writeTexture during record, or metadata-only source may publish
completed native content. Null validator and Vulkan/Metal/WebGPU selected native
bindings remain DiagnosticFailure until their own original payload evidence
exists. f2b2c must define these four mappings before backend mutation; f2b3 then
integrates immutable source, full original graph/input and command submission.

## OriginalTextureUploadDestination/1 fresh creation and exclusive access

Status: Contract Complete / Prototype Complete. Backend Validated for selected
WebGPU native resource creation with actual scopes/getters only. Explicit private
Prototype creation/binding prerequisite; authored content readiness remains closed.

The factory consumes an original content Source and original registry under
separate metadata/resource/content-ledger CPU quotas, maximum creation callbacks,
retained source bytes and one explicit GPU texel/block footprint budget. This
budget is per destination and accounts exact logical mip/layer storage; WebGPU
exposes no native heap-size query, so it is not resident-memory evidence. It is
paired with a real native texture and never substitutes a size-only resource.
Destination ID,
all physical layout scalars and CPU source/domain/authoring version are exact.
Memory must be GpuOnly, initial state CopyDst, sample count1, and declared usage
may contain only CopyDst/CopySrc/Sampled (CopyDst required); no attachment/storage
usage is added. Format/dimension/mip/layer semantics remain the validated packing
contract. BC creation requires the actual native compression feature query. Native
texture getters must confirm dimensions, format, usage, mips and samples after
all three scopes succeed; copied payload fields alone do not prove this mapping.
Unknown/mismatched fields, occupied resource ID, quota/byte/count failures reject
before native creation; no existing texture/native payload adoption overload exists.

A backend-private native texture factory shares the existing actual create path
and emits an immutable OriginalUploadExclusive access policy only from its private
fresh creation entry. An ordinary constructor cannot attach, unseal or replace
that policy on an existing texture. The native payload is core-owned under the
resource quota and owns the original allocation token. Original destination owns
one CPU ledger/domain expectation with typed Uninitialized intent; a second
equal-ID/layout ledger cannot replace it. CPU claim/ledger alone cannot claim
native creation, GPU content or command-buffer proof.

Initialize allocates no hot callback storage: preparation creates exactly three
core-owned callbacks for the actual Validation/OutOfMemory/Internal error scopes.
Cold initialize pushes all scopes, creates a real native texture/allocation/RHI
wrapper, registers/captures exact original resource/native object/device/allocation
facts, and pops those scopes. CreationReady requires all actual callbacks to
succeed and original facts still match. Registry insertion is atomic and never overwrites an occupied ID; destruction
unregisters only its own original payload. Registry lifetime/mutation and content
intent preparation remain externally serialized. Hot validation uses a try-lock,
original transfer observation and exact scalar/owner join without label copies.
No queue write, queue submit, CPU signal,
Dummy handle or metadata-only payload can substitute. Pending/rejected/lost
objects fail closed. Pending callbacks retain the original graph until retirement;
prepared/ready drop and actual loss release exact CPU/GPU budgets. CreationReady
means fresh resource creation only; Source generation remains proposed/uninitialized.

The immutable policy rejects every ordinary use of the exclusive payload before
native recording: buffer-to-texture/texture-to-texture writes, immediate uploads,
attachment/resolve/depth targets, storage/sample descriptor binding, readback and
texture copy reads. Source/destination and texture-view parent edges are checked
at validation and native boundaries, including reused physical groups. Rejection
is RHI-TEXTURE-CONTENT-EXCLUSIVE-USE, changes no command/stat/queue state and does
not infer a privileged writer. Only a later approved existing-original-recorder
path may consume the exact destination/source/ledger/transaction join for upload;
this phase provides no ordinary bypass and no new recorded-command proof issuer.
Before content completion, ordinary reads cannot expose blank bytes as authored
content. Existing ordinary textures retain their explicitly ordinary policy.

| Backend | Fresh selected native destination |
| --- | --- |
| Null | CPU validator only; DiagnosticFailure native creation/binding |
| Vulkan | DiagnosticFailure; no approved original destination payload |
| Metal | DiagnosticFailure; no approved original destination payload |
| WebGPU | Native cold texture creation with actual error scopes; Prototype exclusive access and content/source/recording gates closed |

WebGPU creation availability comes from actual native device/format/feature facts
and callback/native-payload evidence, never its backend name or an opt-in boolean.
This private profile changes no public executablePayload/NativeSync capability.
Required quick contract/Null and both actual browser modes cover budget/claim/
owner/occupied ID failures, actual scoped creation, all ordinary-use negative
assertions, identity mutation and foreign ledger rejection, callback failure/drop,
prepared/ready/pending actual loss and exact CPU/GPU retirement. Native resource
creation evidence must not promote upload/content/Platform/Production readiness.

Verified 2026-10-03: host/Null10/10, Wasm/browser5/5 and source inventory20/20.
Both native browser modes report diagnostics=[] and nativeErrors=[]. Wasm
prepared destination metadata/resource/content-ledger peaks552/208/480 bytes;
source retention1696 bytes and exact logical texel/block budget1360 bytes. Tests
include occupied/competing and replaced registry identity, forged/foreign owners,
CPU proposal/stale rollback, actual native scope error, all-success scopes with
wrong actual native usage, hot validation allocation0, caller drop, pending callback
retention, actual loss in Prepared/Pending/CreationReady and exact CPU/GPU ledger
retirement. Weak test observers are explicitly released before checking shared
control-block quotas. OriginalTransfer observation is reused for whole textures;
ordinary descriptor observation supports views and is not broadened here.
Evidence: build/web-bindless-apple-debug/artifacts/bindless-continuation/
original-texture-destination-report.yaml and preserved native reports/build/tests.
ResourceGeneration execution, original TextureUpload recording, Submitted and
Completed content states remain unavailable. RenderDoc diagnostic remains
RHI-RENDERDOC-TOOL-UNAVAILABLE; no new visual output, visual/Platform/Production
acceptance or public execution/NativeSync promotion. Continue f2b3 staging/source
ownership before original recording/common submission/content completion.

### Next-stage original packed-source ownership audit

Implementation pending: f2b3a reuses the existing original BufferUpload source
factory for immutable packed bytes; packing is already 256-row-aligned and its
size meets GPUQueue.writeBuffer's normative4-byte size constraint. It cannot
adopt an arbitrary buffer merely because it is sealed or Ready. The new original
source must join the exact CreationReady destination, original Source, region
layout, authored generation claim and original Uninitialized expectation.
Independent wrapper/source metadata, CPU payload/resource, retained graph and
GPU source buffer/count budgets remain mandatory.

The existing BufferUpload callbacks retain their source object, not a texture
wrapper or its destination graph. Therefore a wrapper owning a BufferUpload
source is insufficient for pending destination lifetime. Define an acyclic,
private factory-owned preparation context retained by the original native source
before initialization; callbacks then retain the exact original destination/CPU
content graph after wrapper drop. Existing source registration also uses ordinary
registerBuffer; the packed-source producer must reject occupied IDs atomically,
including an internally issued source ID colliding with an authored destination
ID, without replacing the original native owner. Neither ownership retention nor
source queue completion issues target content Submitted/Completed evidence.

Four backend conclusions for this next selected source: Null validates CPU intent
but rejects selected native source; Vulkan/Metal reject unimplemented native
source deterministically; WebGPU may use only the approved original factory,
actual scoped creation/writeBuffer/queue completion and exact original joins.
No target queue.writeTexture, public capability, native upload recorder or content
completion is enabled by this design. Contract/implementation validation of the
new source remains pending; proceed to quick/Null and actual browser readback,
error/drop/loss tests before marking f2b3a complete.

## OriginalTextureUploadSource/1 immutable packed native staging

Status: Contract Complete / Prototype Complete. Backend Validated only for the
selected WebGPU immutable native staging source. Private explicit Prototype
source preparation only, not target upload/content completion.

The factory accepts only an original CreationReady destination and original
staging pool, with independent wrapper metadata, BufferUpload metadata/payload/
resource CPU quotas, retained CPU source bytes, retained destination logical texel
bytes, packed byte and source count/GPU byte budgets. It consumes that destination's
exact original content Source, packing/mip layouts, authored claim and original
Uninitialized domain expectation; no arbitrary native-buffer adoption overload.
It reuses the existing BufferUpload original factory for one immutable copy of all
packed bytes and the original frame serial, including zero staging padding. No
texture queue.writeTexture or target write is performed.

A private BufferUpload factory entry attaches the exact original destination
before initialization. Only OriginalTextureUploadSource is its additional friend;
there is no test friend or new command-proof issuer. The buffer source retains
the destination, which retains its CPU Source/packing/ledger/allocation/native
object/device; the texture wrapper owns the buffer source. This is an acyclic
ownership graph. Pending original buffer callbacks retain the buffer source and
therefore the destination graph after caller/wrapper drop. They do not retain a
weak observer as a substitute for ownership. Before initialization and on final
readiness, exact original destination identity, expected content, device and
registry joins must still validate. An invalid destination prevents source
initialization/readiness; no privileged writer or content state is issued.

All original BufferUpload sources reserve a monotonic resource ID during cold
preparation. The reserved ID is a CPU comparison value, never a native resource
observation/readiness/command proof. Exhaustion returns RHI-RESOURCE-ID-EXHAUSTED.
Initialization rejects an occupied reserved ID before native creation and write;
registry insertion is atomically checked again and never overwrites an existing
owner. Buffer wrappers unregister only their own original native owner, so a
later replacement cannot be deleted by the retired source. No retry/remapping or
placeholder registration is allowed. Registry/content preparation is externally
serialized as in OriginalTextureUploadDestination/1.

Ready requires the existing actual queue completion and all three actual native
error scopes, exact original/native buffer facts (including native size/usage),
sealed writes, original destination/device/registry and immutable packed layout
joins. Source queue completion proves packed source bytes only. It cannot issue
TextureUpload recorded-command proof, target content Submitted/Completed or
ResourceGeneration execution authority. Unknown version/opt-in/budget/quota/
owner/source/ID/native facts fail deterministically; rejected source objects
cannot be reinitialized. A fresh factory may retry after the declared cause has
been removed and independent budgets permit it. Hot validation allocates0 and
uses existing try-lock registry validation; source initialization remains cold.

| Backend | Selected original packed native source |
| --- | --- |
| Null | CPU validation only; DiagnosticFailure native source |
| Vulkan | DiagnosticFailure; original packed source unimplemented |
| Metal | DiagnosticFailure; original packed source unimplemented |
| WebGPU | Actual immutable BufferUpload native staging with exact original destination graph; Prototype, public/target recording/content completion gates closed |

Required quick/Null and both real browser modes: original owners/foreign joins,
independent quotas/count/byte budgets, reserved-ID collision before native create,
original-owner conditional retirement, full1536-byte native readback against the
original packed bytes, scopes/queue partial evidence, native creation/write/size/
usage errors, destination mutation before/pending/after readiness, caller drop,
Prepared/Pending/Ready actual loss and exact context/CPU/GPU ledger retirement.
No source/Null/capture-free success promotes Platform/Production or visual
acceptance; RenderDoc capture remains mandatory for later visual changes.

Verified 2026-10-03: host/Null10/10; Wasm/browser5/5; source inventory21/21.
Both real feature modes diagnostics=[] and nativeErrors=[]. Full1536-byte actual
readback includes original zero padding. Four independent cold CPU quota peaks
(wrapper/source/payload/resource) are56/1028/1536/56 bytes; original content1696,
destination logical texel1360 and staging1536 remain independently budgeted.
Actual queue/scopes partial evidence, native errors, all-success scopes with
wrong native size/usage, pre-creation reserved-ID collision, owner-conditional
replacement retirement, destination mutation, CPU Proposed/stale abandon across
Prepared/Pending/Ready, callback-retained destination graph on wrapper drop and
actual device loss retirement all pass. Hot validation allocates0. Initial CPU
proposal fixture expected domain-pending after native CreationReady; the existing
contract first rejects changed content state with CONTENT-JOIN. Only the fixture
expectation changed. Evidence original-texture-source-* in bindless-continuation.
RenderDoc RHI-RENDERDOC-TOOL-UNAVAILABLE; no Platform/Production/visual acceptance.
Continue f2b3b exact original input/packed-copy mapping and graph/state/content/
recording/submission integration. General/public/bindless/NativeSync stay closed.

## OriginalTextureUploadPackedCopy/1 original input-to-packed-copy mapping

Status: Contract Complete / Prototype Complete. Comparison-only metadata
prerequisite for f2b3b; not recorded-command or texture-write authority.

Reuse original TextureUploadPacking mip metadata and its already validated
layout mapper. Do not rebuild regions, pitches or packing in backend/sample code.
A packed copy carries the comparison staging-buffer/destination IDs, exact
logical extent and destination origin, single-mip range with original layers,
format, packed offset/bytes-per-row/rows-per-image, complete packed footprint,
original frame and authoring contentGeneration. rowsPerImage means block rows as
in the packing/texture-transfer L2 contract. Arrays preserve range.baseArrayLayer
separately from origin.depth; 3D preserves original origin.depth and extent.depth.
Compressed edge extents remain logical; native block rounding belongs to the
future approved backend lowering and cannot silently replace this value.

Mapping requires version1 and explicit Prototype opt-in, original packing owner,
valid distinct comparison IDs, bounded mip index and immutable packed footprint
within the original byte span. It never copies source pixels or allocates. Output
is assigned only after complete validation; failure leaves caller output intact.
Unknown version/opt-in/owner/ID/index/range returns a stable diagnostic. A separate
exact matching entry rejects changes to any returned field; it is still metadata.

The WebGPU source binding requires its original ready shared owner and the exact
original input snapshot pointer/control block from packing, not an equal snapshot,
foreign content source or arbitrary buffer. It delegates to the shared mapper
using the actual registered staging ID. Prepared/Pending/rejected/lost source and
changed native destination fail closed; hot binding/mapping allocate0. A copied
mapping cannot grant native command proof, privileged target access or content
Submitted/Completed. Public/general/ordinary Transfer gates stay unchanged.

| Backend | Shared packed metadata | Original target write execution |
| --- | --- | --- |
| Null | Equivalent CPU mapping, validator only | DiagnosticFailure / unproven |
| Vulkan | Equivalent CPU mapping | DiagnosticFailure / unimplemented original chain |
| Metal | Equivalent CPU mapping | DiagnosticFailure / unimplemented original chain |
| WebGPU | Equivalent CPU mapping bound to original ready staging source | DiagnosticFailure / original graph/transaction/recorder chain pending |

Quick/unit + Null hidden-frame and both actual browser modes cover original
multi-mip/array mappings, 3D depth origin, BC edge logical geometry, every changed
field, forged control blocks, missing/identical IDs, unbounded indices, source
Prepared/Pending/loss gates, exact original input and caller drop, untouched output
on failure and allocation0. No target command is emitted; scoped source readiness
is not target Backend Validated, Platform Validated or Production Ready.

Verified 2026-10-03: host/Null10/10, Wasm/browser5/5 and source inventory22/22;
both feature modes diagnostics=[] and nativeErrors=[]. Every20-field mutation,
2-mip arrays, 3D nonzero depth origin, BC logical edge geometry, wrong version/
opt-in/owner/ID/index, unchanged failed output, exact original ready native source/
input identity, Prepared/Pending/lost source and destination mutation rejection
pass. Hot shared mapping/native source binding allocate0. The host fixture found
a fixed authoring destination ID colliding with newly allocated comparison source;
independent packing fixtures now allocate their destination IDs, while explicit
content-domain fixtures keep their authored bindings. Production mapping remains
strict. Evidence original-texture-mapping-* YAML/log/XML in bindless-continuation.
No target command/content promotion or target Backend/Platform/Production claim.
Continue f2b3b2 original graph ownership projection.

## OriginalTextureUploadProjection/1 typed original graph ownership

Status: Contract Complete / Prototype Complete. Actual selected WebGPU original
ownership pair validated only. TextureUpload full input/state/recording is still
unsupported.

Extend GenericCommandProjection/1 rather than create a parallel graph/container.
The graph accepts an explicitly bounded array of original ready TextureUpload
sources. It retains their staging plus exclusive destination pair directly;
ordinary Transfer roots must keep rejecting these destinations. Additional
OriginalTextureUpload references resolve Texture to the original destination
native payload, Upload to the original source capsule (keyed by destination ID),
and Buffer to the exact original staging payload. Indices/domains/IDs must match;
equal resource IDs or pointer aliases do not substitute for source ownership.

Source count, total packed bytes, original content retained bytes and destination
logical texel bytes are independent explicit quotas. Duplicated original staging
or destination IDs reject before graph allocation. Existing shared allocation
inventory inspects two original allocation records per source, sums/deduplicates
with other roots by their actual allocation/native owners and enforces the
existing independent allocation-count/retained-byte bounds. Never infer any quota
from callback counts or reinterpret logical bytes as resident-memory query data.
All native source/destination/device/registry/content joins are revalidated with
the source, and graph device join includes the pair. Source Pending/Rejected/loss,
foreign control blocks and changes to native/original destination fail closed.
All hot reference/owner/allocation checks allocate0.

| Backend | Original graph source pair |
| --- | --- |
| Null | Validator-only metadata; DiagnosticFailure original native sources |
| Vulkan | DiagnosticFailure original native sources unimplemented |
| Metal | DiagnosticFailure original native sources unimplemented |
| WebGPU | Explicit Prototype ownership-only original ready native pair |

No full input family, state transaction, privileged native emission or proof
issuer is added. TextureUpload family remains Unsupported; generic/public/native
sync gates stay closed. Quick/Null and both actual browser modes cover exact/
one-less/exhausted cold quota, source/payload/content/texel/allocation bounds,
duplicates and forged source owners, wrong references, original caller drop,
mutation, allocation0 and graph-retained actual loss with exact retirement.

The original shared preparation entry includes both graph metadata and the
original shared control block in the same quota and failure boundary. If either
allocation fails, it returns RHI-WEBGPU-COMMAND-PROJECTION-ALLOCATION and destroys
the partial cold graph. No source/native ownership or quota charge is published.
This reuses existing core quota-bound shared ownership, without test-only catches
or native placeholder success. Initial one-less pressure exposed an uncaught
control-block allocation in the fixture's two-step prepare/share; the complete
runtime factory now covers both operations deterministically.

Verified 2026-10-03: host/Null10/10, Wasm/browser5/5, source inventory23/23.
Both actual feature modes diagnostics=[] and nativeErrors=[]. Original shared
cold graph peak420 Wasm bytes,3 Texture/Upload/Buffer references and2 original
allocation records retain2896 ledger bytes (1536 staging+1360 logical texel).
The CPU content1696 is independently bounded. Exact/one-less/exhausted cold CPU
quota, independent source/packed/content/texel/allocation count/byte bounds,
duplicate source and different sources sharing one original destination, forged
source owners, uncovered IDs/indices, caller drop, destination mutation, hot
allocation0 and actual graph-retained device loss with exact retirement pass.
Original shared control-block allocation failure returns the same graph allocation
diagnostic and leaves no partial graph. Evidence original-texture-projection-*
YAML/log/XML and original-stream summary in bindless-continuation. Public/general/
bindless/NativeSync, ordinary exclusive target rejection and the two command
proof issuers remain unchanged. No target recording/content promotion, target
Backend Validated, Platform Validated or Production Ready claim.
Continue f2b3b3 exact full positional TextureUpload input, then f2b3b4 approved
current resource/content transactions, native recorder/common submission/completion
and real target readback. The initial Uninitialized source admission must stay
immutable; future current-transaction validation is a distinct approved path.

## OriginalTextureUploadInputs/1 full positional original inputs

Status: Contract Complete / Prototype Complete. Full immutable input/state-owner
metadata only; no current transaction or native command proof. Host/Null10/10,
Wasm/browser5/5 and inventory24/24 pass in both feature modes, with empty native
errors and diagnostics. Wasm cold inputs/states peaks268/160 bytes; hot allocation0.
Evidence: original-texture-inputs-* YAML/log/XML in bindless-continuation. Actual
device loss retains and retires all six independent quota owners. Target upload
is not Backend Validated; Platform Validated/Production Ready remain open and
RenderDoc reports RHI-RENDERDOC-TOOL-UNAVAILABLE.

Reuse TextureUploadInputSnapshot through a lightweight original-input argument
value. Its capture accepts only an original snapshot owner and retains that exact
pointer/control block. It reports TextureUpload kind and compares original input
identity; it does not recapture caller pixels or infer native readiness. Extend
existing OriginalCommandInputs descriptor/RawInput variants and family vocabulary
with this value rather than create a parallel command stream or descriptor store.

For each TextureUpload command, its two positional references must be Texture
then Upload, both OriginalTextureUpload from the same source index and original
destination ID. Require the descriptor's exact original snapshot from that ready
source, scalar source/frame/generation/layout binding and every packed mip mapping.
The frozen variable command label must equal the immutable snapshot label. Scalar
kind/order/variables and raw original-reference comparison use existing helpers.
Source input byte equality, generation echo or an independently captured equal
snapshot is not sufficient original ownership. Changed/foreign/null input or
wrong positional references fail with deterministic diagnostics before allocation.

Append independent original texture descriptor count, total mip-mapping count and
retained snapshot pixel-byte limits to the explicit full-input budget. Defaults
are zero, so existing descriptor budgets do not enable this new family. These
limits are distinct from graph/source/allocation/content/mip metadata CPU quotas;
no values are inferred from callback slots. Original descriptors retain immutable
snapshots already owned by the graph, without re-copying pixels/labels/mip arrays.

ResourceStates must count and retain both actual staging-buffer and original
whole-texture state controls from each typed graph source. Reuse shared identity
inventory and registered Transfer observations; never manufacture a new state
control, silently omit an implicit staging owner or use CPU content generation
as state revision. Current ResourceStates remains a snapshot/owner projection;
TextureUpload transfer-state language and native recording remain rejected until
f2b3b4 provides the approved state/content transaction/native lowering.

| Backend | Full original TextureUpload input |
| --- | --- |
| Null | Validator only; native-source binding DiagnosticFailure |
| Vulkan | DiagnosticFailure original native source unimplemented |
| Metal | DiagnosticFailure original native source unimplemented |
| WebGPU | Explicit Prototype exact native-source/full-input/state-owner metadata; target execution DiagnosticFailure |

Required quick/Null and both actual browser modes: original argument owner/kind,
full4-command stream with2 references and2 state controls, independently bounded
full-input/mip/pixel budgets, exact/one-less/exhausted CPU admission, frozen labels,
wrong descriptor order/kind/ref/index, independently captured equal input and
forged control blocks, changed target/content/state, hot allocation0, caller drop,
state pending/stale observations and actual device loss with exact quota retirement.
Existing original BufferUpload/Transfer/Compute/Graphics recorders must reject this
TextureUpload stream; proof issuers and public/general/bindless/NativeSync gates
remain unchanged. No queue.writeTexture, native encoder or content promotion.

## OriginalTextureUploadTransaction/1 initial and current validation

Status: Contract Complete / Prototype Complete. This prerequisite is CPU
transaction ownership and metadata only. Host/Null10/10, Wasm/browser5/5,
inventory25/25 pass. Wrapper/content cold peaks68/196 Wasm bytes; hot allocation0,
encoder0/writeTexture0. Both modes nativeErrors=[]/diagnostics=[]. Native recording/
submission and Submitted/Completed promotion remain a separate subsequent phase.
Evidence: original-texture-current-* YAML/log/XML in bindless-continuation.
No target upload Backend Validated, Platform Validated or Production Ready;
RenderDoc reports RHI-RENDERDOC-TOOL-UNAVAILABLE.

Cold preparation accepts one exact original TextureUpload source in a previously
validated immutable four-command BeginTransferPass/TextureUpload/EndTransferPass/
Finish input/state graph, with exactly two original references and both actual
CopySrc staging and CopyDst destination controls. No ordinary transfer/resource/
pipeline/descriptor sources or extra commands are admitted. Independently bound
source count, mapped mip count, retained source bytes and wrapper/content CPU
quotas; defaults close admission. Retain the sealed inputs rather than reconstruct
their private immutable fields during pending validation.

The default destination/source/map/graph/input/state validators continue requiring
the original Uninitialized content expectation and revision. Refactor common
native declaration identity checks into private methods accessible only through
the original source and this transaction. They still verify successful creation
and staging completion/scopes, device, original registered resource/allocation,
sealed source, native size/usage/layout, exclusive destination and original ledger/
domain identity. Removing current content checks from declaration validation must
never remove them from initial admission or expose an execution entry point.

Prepare the existing TextureUploadContentTransaction from the destination's exact
ledger/source/initial expectation. Current validation requires its same original
owners, exact captured expectation revision and journal-owned pending observation
(including Proposed generation), plus both original frozen resource observations.
An unrelated/equal/forged transaction, pending resource journal, stale rollback,
native identity mutation, or terminal invalidation fails closed. Applying CPU
intent and mapping packed regions grant no recording proof or completion state.
Unsubmitted abandon rolls back content; rollback advances its revision and makes
the original admission stale. Actual loss invalidates the owned content domain.
The original graph and six quotas must survive caller drop and retire exactly.

| Backend | Current original TextureUpload transaction |
| --- | --- |
| Null | CPU validator only; selected native-source DiagnosticFailure |
| Vulkan | DiagnosticFailure original native source unimplemented |
| Metal | DiagnosticFailure original native source unimplemented |
| WebGPU | Explicit Prototype exact CPU transaction/current mapping; native recording DiagnosticFailure |

Quick contract/Null and both browser feature modes must cover budget/owner gates,
cold exact/one-less/exhausted quota failures with no encoder/writeTexture calls,
initial rejection during Proposed, valid current ownership and untouched mapping
on failure, foreign transaction/domain, resource/content stale and pending state,
native mutation, hot allocation0, caller drop, abandon and actual-loss retirement.
Command proof friends remain exactly OrdinaryRecording and OrdinarySubmission;
public/general/bindless/NativeSync and ordinary exclusive guards stay closed.

## OriginalTextureUploadCopyLowering/1 exact packed geometry decisions

Status: Contract Complete / Prototype Complete. This prerequisite defines metadata
lowering only; decisions never emit commands or publish content. Host/Null10/10,
Wasm/browser5/5 and inventory26/26 pass; both browser modes diagnostics=[] and
nativeErrors=[]. Original20 mapped fields and13 decision fields, original packing
pointer/control block, 2D arrays/3D nonzero slices/BC edges, actual Prepared/Pending
native fact binding, allocation0 and actual loss gate are verified. Evidence:
original-texture-copy-lowering-* YAML/log/XML in bindless-continuation. No target
upload Backend Validated/Platform Validated/Production Ready; RenderDoc remains
RHI-RENDERDOC-TOOL-UNAVAILABLE.

First validate each exact original packed-copy mapping against its immutable
packing/input owner and staging ID. A version1 explicit Prototype opt-in decision
binds backend, original mapping, destination layout and mip index. Reject unknown
backend/version/opt-in, foreign packing, mutated mapping and unsupported geometry
before publishing a decision. Recompute and compare the complete decision at
consumption; a decision is intent, never native/source/completion authority.

For WebGPU copyBufferToTexture, preserve original buffer offset, bytesPerRow and
rowsPerImage (block rows), texture mip and aspect. Preserve logical X/Y origin.
For a 2D texture, origin Z is the explicit base array layer and copy depth is
arrayLayerCount; for 3D it is logical origin.depth and extent.depth, with original
baseArrayLayer0/arrayLayerCount1 required. WebGPU copies physical compressed mip
extent: round logical width/height to blocks only after original packing proves
the logical edge region. Use the existing blocksForExtent/blockExtent helpers and
checked unsigned bounds; no heuristic expansion, source stride adjustment or
origin truncation. Logical extent/origin remain in the bound original mapping.

| Backend | Version1 selected original packed texture-copy |
| --- | --- |
| Null | DiagnosticFailure native payload; CPU geometry validator only |
| Vulkan | DiagnosticFailure original private payload unimplemented |
| Metal | DiagnosticFailure original private payload unimplemented |
| WebGPU | Prototype WebGPU physical texel-copy decision; actual recorder still closed |

The backend-private native fact validator consumes the exact current transaction,
original mip mapping and frozen decision. Verify device, source/destination native
payloads, explicit dimension/format, row pitch and actual native creation/readiness
facts through that transaction; BC requires its existing queried native feature.
An ordinary transfer copy continues rejecting the exclusive destination. No
queue.writeTexture, encoder call, proof issuer, capability promotion or content
state change is introduced by metadata lowering.

Quick/Null geometry tests cover all mapped fields, invalid decision/version/opt-in/
backend, equal foreign packing, 2D arrays and nonzero origins, 3D nonzero slices,
BC edge rounding and untouched outputs on failure. Both actual browser modes
validate selected original decisions in Prepared/Pending states, device mismatch,
wrong mip/decision and actual loss, with allocation0 and no native copy calls.

## OriginalTextureUploadNative/1 recorder and common completion join

Status: Contract Complete / Prototype Complete / Backend Validated for the
selected private WebGPU uncompressed upload profile (2026-10-03, f2b3b4b2).
Host/Null10/10, Wasm/browser5/5 and inventory27/27 pass. Actual two-mip arrays
and 3D nonzero origins preserve original bytes and untouched fresh zero texels.
BC is Degraded: queried feature false rejects RHI-TEXTURE-DESTINATION-BC-FEATURE
before creation; its native execution is not validated. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/original-texture-native-report.yaml`.
No Platform Validated/Production Ready or public/general/bindless/NativeSync
promotion. RenderDoc: RHI-RENDERDOC-TOOL-UNAVAILABLE.

Reuse WebGpuOrdinaryRecording as the only recorder/proof issuer, and the existing
command payload, completion reservation, retained graph and OrdinarySubmission.
Cold preparation must bind the exact four-command state graph/current texture
transaction, retain bounded frozen copy decisions for every original mip, validate
UTF-8 labels and reserve existing callbacks/encoder/command-buffer/debug budgets.
No encoder creation or target write occurs during CPU preparation. Publish the
transaction into the command before constructing the completion graph so that
the reservation captures its original pointer/control block.

The completionDeviceMatches/current recording validator must use this exact
transaction when texture work is present, retaining all default initial validators
for other paths. Completion graph matching additionally compares the original
texture transaction and all frozen decisions. It may not skip validation merely
because an initial graph rejects Proposed content. The old dependency/buffer
state transaction and new texture content transaction are exclusive profiles.

Recording applies the owned Proposed journal and emits copyBufferToTexture only
through a backend-private primitive accessible to OrdinaryRecording. Validate and
consume every frozen decision before emission; fill buffer, layout, texture,
mip, aspect, origin and extent exclusively from it and the exact retained source.
Native finish plus three recording scopes establishes Recorded proof. Unsubmitted
recording failure/abandon rolls back its CPU intent; initial admission then remains
stale. queue.writeTexture and ordinary Transfer exclusive bypass remain forbidden.

Submitted/Completed content transitions require private authority from the existing
OrdinarySubmission flow. CPU apply/generation equality/current metadata cannot
publish either state. The exact-ticket submitWith admission must check matching
command, device, original texture transaction, pending observation and native
finished command buffer before accepting submission. Submitted intent accompanies
the actual wgpuQueueSubmit; completion requires its actual queue receipt and all
three successful native submission scopes. Retain and validate committed content
observations after the CPU journal is consumed. Commit content completion only
through this proven common completion path, with signal publication and failure
quarantine retaining the same command/resource graph.

After submitted errors or actual loss, invalidate content and both original
resource controls and quarantine through the existing retention pool. Never roll
back submitted texture work to Uninitialized, publish Completed/signal from a
queue receipt alone, or free retained native owners while callbacks remain.
Before submission, exact stale observations or closed completion admission must
reject deterministically without queue work; retry follows the existing exact
reservation policy, never reuses a stale content observation.

Required quick/Null plus both browser modes: cold quota/capacity/owner/label gates,
recording native finish/scope failures, actual two-mip target readback at nonzero
origins/array layers against original bytes and fresh untouched texel contract,
allocation0 in record/submit/completion, caller drop while callbacks are pending,
submission error quarantine, stale source/control/content, actual loss and exact
CPU/GPU/retention retirement. Validate 3D/BC execution or report deterministic
unsupported scope; CPU geometry tests alone never prove those native paths.
Null/Vulkan/Metal remain DiagnosticFailure for this selected private profile;
WebGPU is explicit Prototype until real native evidence is complete. The two
recorded proof friends and public/general/bindless/NativeSync gates stay unchanged.

## OriginalTextureReadbackInput/1

Status: Contract Complete / Prototype Complete for CPU input (2026-10-03, f3a).
Host/Null10/10, Wasm/browser5/5 and inventory28/28 pass, including actual-loss
CPU owner retirement. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/original-readback-input-report.yaml`.
No readback Backend Validated/Platform Validated/Production Ready promotion.
A cold immutable descriptor snapshot represents input only. It cannot issue native
source, backend-owned readback buffer, result publication or map/completion proof.

Preserve every ReadbackTextureDesc scalar: source and destination IDs, size,
staging intent, backend-owned flag, all five source-range fields, three extent
components, row pitch, rows per image and destination offset (16 scalar values).
Preserve exact label bytes and every ordered seven-field mip record. Unknown
enums, maximum values and the documented legacy zero-extent form are represented
without inferring texture format/extent/layout or repairing metadata. A later
source/layout validator must reject ambiguity before GPU work. Scalar writes
leave the caller's label and mip collection intact.

Use bounded mip storage, the existing quota-bound shared identity and variable
label store. Metadata/shared-owner/mip storage and label storage have independent
CPU quotas, explicit mip/text bounds, version1 and Prototype opt-in. Preparation
failure retires all partial ownership; exact, one-less, exhausted and oversized
capacity cases must return diagnostics. Hot scalar/snapshot comparison allocates0
and compares exact ordered values, including unsupported metadata.

All four backend conclusions are deterministic: CPU representation is identical;
Null is validator-only, while Null/Vulkan/Metal/WebGPU native readback authority
remains DiagnosticFailure RHI-TEXTURE-READBACK-ORIGINAL-OWNER-UNPROVEN. The original
Readback reference domain and TextureReadback full-input family remain closed.
No default capability, prototype recorder friend or native result is added.
CommandScalarArguments accepts TextureReadback None only as a separate descriptor
marker, and CommandVariableArguments accepts its label only. Neither marker is a
complete full-input admission; the Unsupported family and Readback owner gate
continue rejecting before native preparation.

Reuse existing transfer mip layout field comparison rather than reproducing it
in another backend/sample. Existing TransferCopyArguments intentionally refuses
ordered mip layouts and lacks readback size/staging/label fields, so it cannot
represent the full input. Existing generic scheduleTextureReadback uses dynamic
pending/global result collections and record-time native buffer creation; those
cannot supply the new bounded original result owner. Test quick/Null and both
browser modes, caller label/mip drop, every scalar/mip mutation, count/order/label
mutation, forged owner, exact quotas and actual-loss CPU owner retirement. No
native readback Backend Validated/Platform Validated/Production Ready declaration.

## OriginalTextureReadbackLayout/1

Status: Contract Complete / Prototype Complete CPU layout (2026-10-03, f3b1).
Host/Null10/10, Wasm/browser5/5 and source inventory29/29 pass. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/original-readback-layout-report.yaml`.
No Backend Validated/Platform Validated/Production Ready promotion. Source texture
descriptor is a comparison value only. Exact original input is retained under a
separate cold layout CPU quota, mip capacity and destination-byte capacity. None
of these values can establish registered native identity or result readiness.

`sizeBytes` is the declared destination staging-buffer capacity measured from
byte zero, matching the existing readback bounds check. `destinationOffsetBytes`
is the first mip's absolute buffer offset. Every explicit mip bufferOffsetBytes
is also absolute; it is never silently rebased by destinationOffsetBytes. A
nonempty mip sequence contains exactly sourceSubresources.mipLevelCount entries
in consecutive ascending mip order, and the first record exactly matches the
scalar extent/pitch/rows/offset. Empty mip records require exactly one explicit
mip with nonzero scalar extent/pitch/rows. Legacy zero extent fails with a stable
diagnostic; no native full-texture inference or Legacy opt-in is added here.

Source requires known color format, CopySrc usage, valid mip/array/cube/3D shape
and sampleCount1. Depth/stencil are explicitly outside version1 color semantics.
Each mip starts at source XY0; arrays preserve baseArrayLayer/count, while 3D
preserves explicit logical depth and requires layer0/count1. Reuse normative
texture-transfer row/block/slice and mip extent checks. BC partial blocks are
allowed only at the logical mip edge and produce format-derived rounded physical
texel extents; 3D BC remains invalid. Native format/BC capability is a subsequent
queried factory gate, never inferred from this comparison layout.

Rows use the explicit shared 256-byte texture staging alignment; offsets obey
the shared four-byte copy alignment and format texel-block alignment. Pitch and
rowsPerImage remain exactly authored, including padding. Full footprint is
bytesPerRow * rowsPerImage * slices, checked with reusable core strides. Ordered
mip footprints may have gaps but cannot overlap and must fit sizeBytes. This full
footprint is the declared retained/readback ownership budget, independent of the
native API's smaller last-row minimum. Padding is not authored texture content
and cannot be published as source bytes without a separate result contract.

Validate every mip before allocating/publishing an immutable layout using core
PreflightReplay. On failure no mapping/output is published and partial quota
ownership retires. Every frozen mapping preserves logical range/extent, physical
origin/extent, exact offsets/pitches/rows, meaningful row bytes/count, slice count
and padded footprint. Hot comparison and mapping checks allocate0. Reuse one
shared physical texture descriptor comparison type for upload and readback.

Null/Vulkan/Metal/WebGPU share identical CPU layout semantics; Null remains
validator-only and all native ownership/result execution outcomes remain
DiagnosticFailure RHI-TEXTURE-READBACK-ORIGINAL-OWNER-UNPROVEN. Unknown backend,
version and missing opt-in reject deterministically. Test arrays/multi-mip/3D/BC
edges, source/layout/input mutations, first-mip conflicts, bounds/overlap/order/
overflow, independent exact/one-less/exhausted quotas and CPU owners surviving
actual device loss. No native/Platform/Production readiness claim.

## OriginalTextureReadbackSourceBinding/1

Status: Contract Complete / Prototype Complete for selected original lifetime
binding, verified 2026-10-03. Host/Null10/10, Wasm/browser5/5, inventory30/30;
both browser modes clean. Cold wrapper peak268 Wasm bytes; hot allocation0.
Evidence original-readback-source-binding-* YAML/log/XML. No source content,
Readback execution, Backend/Platform/Production promotion. The ordinary
source profile accepts the exact original registered WebGPU texture object by
core UniquePtr reference and an original readback layout/registry. Native payload
and allocation snapshots alone cannot keep the original public object/registration
alive. Only after all admission, shared metadata allocation and validation succeed
does the factory move the caller's original object into its sealed owner. Failure
must preserve the caller's unique object, original registration and allocations.

Require exact source ID, complete source descriptor, original native payload and
allocation owner/control block, registered device/object identity and a live
committed known whole-resource state observation. Independently bound CPU metadata,
retained source owner count (original object plus native payload/allocation), mip
count and original allocation ledger bytes. Use existing transfer observation and
try-lock registry validation; no blocking wait, native creation, encoder, GPU work
or payload recapture during hot validation. Retained ledger bytes are not a queried
physical heap footprint. Unknown/foreign/stale/pending inputs reject before owner
transfer. Same-state rollback remains revision-stale.

This binding preserves ordinary content policy. OriginalUploadExclusive targets
reject RHI-TEXTURE-CONTENT-EXCLUSIVE-USE even after CPU content equality; a future
typed completed-content authorization profile must prove their exact actual upload
completion without weakening ordinary guards. Likewise, a bound source does not
prove authored contents or historical native creation scopes; actual source use
must still be validated by the future private recorder's native copy scopes.
No source content/readiness success or mapped result is synthesized here.

Null is validator-only; Null/Vulkan/Metal selected native binding remains stable
DiagnosticFailure. WebGPU is explicit Prototype for original lifetime binding
only. Device loss rejects binding/validation and dropping this unsubmitted owner
retires the original object and allocation. Full-input/Readback domain/general/
bindless/NativeSync and the two recorded proof friends remain unchanged. Cold
destination creation evidence, native readback recording/common completion,
mapping, bounded publication and exclusive completed-content source profiles
remain required before closing f3b2/f3.

## OriginalTextureReadbackDestination/1

Status: Contract Complete / Prototype Complete / Backend Validated only for
selected WebGPU cold buffer creation, verified 2026-10-03. Host/Null10/10,
Wasm/browser5/5, inventory31/31; both browser modes clean. Cold Wasm CPU peaks
328/248/512 metadata/resource/result bytes; hot allocation0.
Evidence original-readback-destination-* YAML/log/XML. No Readback recording,
map/result or Platform/Production acceptance. An explicit
Prototype cold factory retains the exact ordinary source binding and its original
immutable readback input/layout. Independently bound source owners/ledger bytes,
metadata CPU bytes, resource CPU bytes, result CPU bytes, GPU staging capacity and
three creation-error callbacks. The original destination ID is reserved metadata;
this backend-owned staging is never registered as a public writable buffer.

Exact capacity is input.sizeBytes, measured from zero with all authored gaps and
padding. V1 maps the whole capacity at offset0, so it must be nonzero and a
multiple of4 (normative map size alignment); offset0 meets the normative8-byte
map-offset requirement. Reject native queried maxBufferSize overflow and size_t
CPU overflow before allocation. No rounding, rebasing or fallback is permitted.
Reject native labels containing NUL or non-canonical UTF8 before allocation using
core::Utf8Decoder; retain the exact CPU snapshot without text repair.
Use CopyDst, GpuToCpu, initial CopyDst L2 buffer intent; native mapping is exactly
COPY_DST|MAP_READ, mappedAtCreation=false. Cold CPU result storage has the same
capacity but is private and unpublished; allocation never constitutes evidence.

Prepare all CPU metadata/result/callback/accounting storage before initialize.
Initialization creates one actual native buffer under Internal/OutOfMemory/
Validation error scopes using existing native resource lowering. CreationReady
requires all three successful scope observations plus queried exact size/usage,
unmapped state and immutable native object/device/allocation identity, and an
unchanged source binding. Callbacks retain the owner until terminal delivery;
rejected or lost owners cannot recover readiness. No GPU writes, command encoder,
queue work, mapping or publication occurs in this stage. Public/full/general/
bindless/NativeSync gates and recorded proof issuers remain unchanged.

Null is validator-only, Vulkan/Metal are deterministic DiagnosticFailure for this
selected native destination profile; WebGPU uses explicit Prototype creation-only
lowering. Every failure returns its stable diagnostic, never dummy payload/bytes.
Actual copy scopes, exact original recording/common completion, map evidence and
bounded immutable publication remain required; f3b2 and f3 remain incomplete.

## OriginalTextureUploadCompletedDeclaration/1

Status: Contract Complete / Prototype Complete / Backend Validated only for
selected actually completed WebGPU upload declaration, verified 2026-10-03.
Host/Null10/10, Wasm/browser5/5, inventory32/32; both browser modes clean and
hot allocation0. Evidence original-texture-completed-declaration-* YAML/log/XML.
No readback adapter/copy/map/result or Platform/Production promotion. A reusable
validation boundary accepts only the exact quota-bound original backend upload
transaction whose private status is Completed after actual common queue/scopes
completion. Prepared/Pending/Submitted and abandoned/invalidated or forged owners
reject. Validate original upload source/staging/destination/native allocation and
registry identity via the existing ready declaration, plus the exact current
content transaction/ledger/original generation and Completed status. CPU intent,
queue receipt alone or equal bytes cannot synthesize this evidence.

The immutable upload initial resource observations stay strict in validate() and
existing record/submit entry points. Completed-content declaration independently
validates current live native/content ownership; later legitimate committed resource
state revisions do not revoke a completed content fact. Active/known controls,
original native/registry ownership and exact Completed content remain mandatory.
A future typed readback source must separately freeze/validate current committed
CopySrc intent and exact region coverage. This helper cannot open ordinary
exclusive access, issue a readback source owner/command/map/result proof or change
any of the two recorded command proof issuers. Null/Vulkan/Metal have no actual
completed native upload profile and fail closed; WebGPU is explicit Prototype
current declaration validation for its tested original common completion path.

## OriginalTextureReadbackContentCoverage/1

Status: Contract Complete / Prototype Complete for CPU comparison only, verified
2026-10-03. Host/Null10/10, Wasm/browser5/5, inventory33/33; both modes clean.
Linear bounded merge and core checked unsigned ranges; hot allocation0 under
all exhausted original quotas. Evidence original-readback-content-coverage-*
YAML/log/XML. No native/Backend/Platform/Production promotion. The
existing upload packing stores exact logical origin/extent, mip/aspect and array
range. Readback V1 has no x/y source-origin field: x/y origin is explicitly0;
its source z is the declared array base or volume0. Join the exact source ID,
complete physical descriptor and format before checked per-mip logical containment.
Use core::containsUnsignedRange; preserve separate array layer and3D depth facts.
Every readback mip must be fully covered by one unambiguous original uploaded
region in the same format/aspect/mip. Physical BC block rounding does not authorize
additional logical pixels; no synthetic union, rebasing, clipping or guessed extent.

Existing original upload fixtures include nonzero x/y origins; their raw test
readback verifies actual native bytes including untouched zeros, but cannot prove
an original content domain outside the uploaded region. Do not turn that fixture
observation into production source authority. Native validation of the future
exclusive source requires an actual Completed original upload whose declared
region covers the requested readback, or a separately approved explicit native
initialization contract. Coverage metadata alone is CPU comparison, all4 native
source/command/map/result authority outcomes remain DiagnosticFailure until the
typed original Completed source owner/region/state/budgets and private recorder
are implemented. Also avoid an include cycle when connecting readback source to
the generic graph: the completed upload transaction currently includes that graph;
keep typed source declarations and completion validation implementation separated.

## OriginalCompletedTextureUploadView/1

Status: Contract Complete / Prototype Complete / Backend Validated only for
selected actual completed WebGPU upload view, verified 2026-10-03. Host/Null10/10,
Wasm/browser5/5, inventory34/34; both modes clean, hot allocation0 and separate
view allocation0. Original native upload cold peaks936/412/228 recording/
transaction/content bytes on current Wasm ABI. Evidence original-completed-upload-
view-* YAML/log/XML. No readback source/map/result or Platform/Production promotion.
A backend
private sealed abstract declaration view has a private constructor with only the
final original texture upload transaction as friend. Its quota-bound original
shared identity is the same identity as the concrete transaction; a view is a
shared owner conversion, never a separately fabricated object or status enum.
External construction/copy and fake derived declarations are forbidden.

Its validation gate checks the exact original quota-bound shared owner and calls
the existing actual Completed native/content declaration validator. Conversion
alone does not imply Completed. Typed read-only getters expose the original live
GpuTexture object, registered native/resource/allocation observation, registry,
original packing and existing tracked content/native allocation totals. The
immutable original upload graph already enforces one original source/two native
allocation references and independent packing/content/metadata quotas; no new
ownership totals are guessed or inferred from callback capacity. The view retains
that complete graph by the exact shared control block.

The declaration header cannot depend on the concrete upload transaction or
GenericCommandProjection. This prevents the future graph -> readback source ->
completed upload transaction -> graph include cycle. Actual transaction implements
the view in its own module; source binding consumes the sealed interface. Only
WebGPU's tested original common submission may reach actual Completed; other3
selected native profiles fail closed as before. Ordinary exclusive policy,
strict initial upload states, source state/access/region proof, readback adoption,
copy/submit/map/result publication and the two recorded proof friends stay separate.

## OriginalCompletedTextureReadbackSource/1

Status: Contract Complete / Prototype Complete / Backend Validated for selected
actual completed WebGPU source ownership only, verified 2026-10-03. Host/Null10/10,
Wasm/browser5/5, inventory35/35, both modes clean; cold wrapper peak276 Wasm
bytes, hot allocation0. Evidence original-completed-readback-source-* YAML/log/XML.
No copy/map/result or Platform/Production promotion. Reuse
WebGpuOriginalTextureReadbackSource's original source/native/registry/state binding
and add a separately sealed completed-upload profile. Ordinary adoption remains
unchanged: OriginalUploadExclusive fails its ordinary access policy. The new cold
factory accepts an original quota-bound CompletedTextureUploadDeclaration view
and original TextureReadbackLayout; validate actual Completed declaration and
OriginalTextureReadbackContentCoverage/1 before adopting any owner. Sealed private
construction chooses the profile; no caller bool can bypass ordinary guards.

The completed graph retains the actual original GpuTexture object; payload and
allocation snapshots alone are insufficient. Validate exact texture pointer,
original native payload/allocation/registry and source ID/descriptor, immutable
packing/content generation/coverage, and a fresh committed known state observation.
Later source state mutation/pending/same-state rollback is stale for this binding;
a future current readback transaction must explicitly own its state intent and
validate the declaration separately. This profile alone proves no CopySrc command,
native readback copy, queue/map/result readiness.

Independently bound one completed upload graph, projected source owners (original
texture object plus native payload/allocation and retained completed graph),
readback/upload mip counts, target native ledger bytes, complete retained graph
native allocation bytes, retained content CPU bytes and wrapper metadata quota.
Use the view's original tracked content and shared-allocation projection totals;
never derive limits from callbacks or byte sizes of opaque native heaps. Failure
leaves caller shared owners untouched and returns a stable diagnostic. Source
factory creates no native object/encoder, submits no work, maps/publishes nothing.

Native fixtures must actually complete an original upload at x/y/z origin0 whose
logical region covers the readback. Existing nonzero-origin target tests must
reject coverage; untouched native zeros remain outside this declared source
profile. Null is validator-only; Vulkan/Metal selected native profile is explicit
DiagnosticFailure; WebGPU is Prototype typed original completed ownership only.
Ordinary exclusive access and public/full/general/bindless/NativeSync stay closed.
Destination adoption and original full/current recording/common completion/map/
immutable bounded result publication remain required before closing f3b2/f3.

## OriginalCompletedTextureReadbackDestination/1

Status: Contract Complete / Prototype Complete / Backend Validated selected
WebGPU private creation only, verified 2026-10-03. Host/Null10/10, Wasm/browser5/5,
inventory36/36, both modes clean; cold peaks328/200/1536 Wasm
metadata/resource/result bytes. Evidence original-completed-readback-destination-*
YAML/log/XML. No copy/map/result or Platform/Production promotion. The original
ordinary destination factory fails closed for a completed source profile. A
separate completed factory accepts only the original quota-bound typed completed
source. This grants private CopyDst|MapRead buffer creation and unpublished CPU
result capacity only. Reuse the existing destination allocator, native factory,
three creation error scopes, original native identity and explicit mapping layout.

An explicit completed-source budget independently limits retained completed graph
count and content CPU bytes. The existing destination source-owner and native-byte
budgets now charge source wrapper plus its source owners and the entire tracked
upload graph's native allocations, rather than only the target texture. Bound
source and destination GPU/result/resource/metadata/callback quotas independently;
no inference from callback slots or opaque native heap sizes. Factory failure
preserves callers. Ordinary factory rejects completed source and completed factory
rejects ordinary source; profile selection occurs only inside private factories.

Null is validator-only. Vulkan/Metal native creation is DiagnosticFailure. WebGPU
selected completed source/private destination creation is Prototype; no native
copy, common readback completion, map/result publication or public capabilities.
Full/current readback input/transaction/recording/map/publication remain required.

## OriginalTextureReadbackPairProjection/1

Status: Contract Complete / Prototype Complete / Backend Validated selected
WebGPU original ready pair/native fact binding only, verified 2026-10-03.
Host/Null10/10, Wasm/browser5/5, inventory37/37; both modes clean, cold peak68
Wasm bytes, hot allocation0. Evidence original-readback-pair-* YAML/log/XML.
No copy/map/result or Platform/Production promotion. Cold-freeze
one exact original completed source/CreationReady private destination pair. Reuse
original TextureReadbackLayout and TextureReadbackMip as immutable physical copy
facts; do not recapture or reinterpret ordinary TextureToBufferCopy as Readback.
Private destination identity and its original source retain the original upload
graph/object/registry, original native buffer/payload/allocation and unpublished
CPU result capacity. Independently bound source/completed graph count, per-mip
count, projected native/object/wrapper owner count, complete native ledger bytes,
retained content CPU bytes, unpublished result bytes and metadata quota. Native
bytes equal tracked source graph plus exact private destination allocation; owner
count includes source's projected owners and source/destination wrapper, native
buffer payload and original allocation owner. Checked sum must reject overflow.

Allocation-free per-mip mapping requires exact quota-bound projection/layout,
CreationReady destination, unchanged original source state/identity and original
mip index/value. Validate actual source/destination device/dimension/format/layout
and native API availability. Return original physical facts without synthetic
geometry; failure preserves output. No native emit/encoder/queue/map/result or
recorded command proof. Future current readback transaction must own pending state
separately; this initial frozen pair's strict state validation cannot serve as that
proof. Null validator-only; Vulkan/Metal selected native binding DiagnosticFailure;
WebGPU Prototype original pair/fact binding only. Public capabilities stay closed.

## OriginalTextureReadbackAllocationRecords/1

Status: Contract Complete / Prototype Complete / Backend Validated selected
original WebGPU allocation records only, verified 2026-10-03. Host/Null10/10,
Wasm/browser5/5, inventory38/38; both modes clean, hot allocation0. Evidence
original-readback-allocation-records-* YAML/log/XML. No Readback graph domain,
copy/map/result or Platform/Production promotion. The sealed
completed view retains the actual selected upload graph with exactly the original
staging and target native allocations. Expose those original allocation/backing
records through the sealed interface, using core SharedAllocationProjection::Record;
this does not prove completion or grant resource access. Only the actual final
upload transaction implements this method. The readback source/pair consumes
those records without depending on the concrete transaction or generic graph.

A ready pair exposes source staging, target and private destination records in
stable order. Original payload and tracked allocation release owner identity must
match; no opaque aggregate token may replace this inventory. Independently bound
record count and native bytes before pair wrapper allocation, use the existing
SharedAllocationProjection inspector for missing/alias/deduplicated byte validation,
and require exact agreement with the tracked source-plus-destination total. A
foreign original allocation owner is an error, never a guessed compatible record.
Record getters are immutable retention metadata; callers must validate the exact
original pair/view. Pending/stale/lost source or destination rejects; no graph
Readback domain, native command, map/result, or public capability is opened.
Null validator-only; Vulkan/Metal selected native view is DiagnosticFailure;
WebGPU selected original native records are Prototype retention metadata only.

## OriginalTextureReadbackCommandProjection/1

Status: Contract Complete / Prototype Complete / Backend Validated selected
original WebGPU graph membership only, verified 2026-10-03. Host/Null10/10,
Wasm/browser5/5, inventory39/39; both modes clean, cold graph peak436 Wasm
bytes, hot allocation0. Evidence original-readback-projection-* YAML/log/XML.
Full input/current/copy/map/result and Platform/Production remain unproven.
Reuse WebGpuGenericCommandProjection cold source/reference arrays, quota/shared
identity, native device validation and core SharedAllocationProjection inventory.
Append OriginalTextureReadback source enum; preserve all prior enum values. Retain
the exact quota-bound CreationReady pair, its sealed completed source graph,
original texture object/registry/layout and private buffer/result graph. Selected
V1 admits exactly one ready pair; multiple pair/shared content/state ownership
requires a separate proved contract and otherwise fails with a stable diagnostic.

TextureReadback L2 has exactly two references: Texture with the original source
texture ID, then Readback with the descriptor's private destination buffer ID.
Both resolve to the same original pair index/source kind. Texture resolves to the
original native texture payload; Readback resolves to the original pair owner,
not the private buffer payload. Buffer reference/ordinary registry membership
cannot impersonate this owner. Cold count/domain/reference validation must reject
uncovered, swapped, duplicated, forged or foreign references before recording.

Independent pair/mip/content/result/count/CPU budgets remain separate from the
three exact allocation record count and deduplicated native byte budget. Include
source staging, target and private destination records in the existing original
allocation mapper, preserving release/backing owner identity; no opaque aggregate
record replaces them. Loss/native/state identity mutation fail closed, graph
retirement preserves original source object/registration until the last owner
retires. Null is validator-only; Vulkan/Metal selected native pair is explicit
DiagnosticFailure; WebGPU selected graph/reference ownership is Prototype only.
Full positional TextureReadback input, original current owned transaction and
native recording/common submission/map/immutable result publication remain closed.

## OriginalTextureReadbackFullInputAndStateGraph/1

Status: Contract Complete / Prototype Complete / Backend Validated selected
WebGPU initial metadata only, verified 2026-10-03. Host/Null10/10, Wasm/browser5/5,
inventory40/40; both reports clean. Original input/state peaks284/200 Wasm bytes;
hot allocation0, exact original drop/loss/retirement verified. Evidence
original-readback-full-input-* YAML/log/XML. Current/native/map/result and
Platform/Production unpromoted. Full positional
TextureReadback descriptor retains only the exact original quota-bound input
snapshot. Matching recaptured scalar/label/mip bytes do not prove identity. Bind
original Texture and Readback references to the same exact pair/source ID/private
buffer ID, and retain the layout's original snapshot without rebuilding geometry.
Validate every original physical mip through the existing pair mapper and original
label through frozen scalar arguments. Independent descriptor count, mip mapping
count, unpublished result byte and metadata budgets gate preparation.

Use the existing full command input variant/cold descriptor array/raw comparison;
CPU descriptor capture is allocation-free and does not issue native authority.
Full source state inventory includes all3 native resources retained by the original
readback graph: completed upload staging and target, plus private destination.
Expose original typed source observations from the sealed view; do not recover
resource semantics by casting opaque allocation records or native handle guesses.
Reuse original SharedIdentityProjection identity/count/dedup and state observation;
private buffer state comes from the exact destination's original payload/control
and descriptor resource ID. Source/control budgets must cover the entire graph,
including retained resources not directly copied by the readback operation.

Both readback references remain Texture then Readback; the private buffer never
becomes an ordinary public Buffer reference or registry entry. A four-command
BeginTransfer/TextureReadback/EndTransfer/Finish fixture proves exact full-input
composition only. Initial states must be known committed live originals; pending,
state revision, same-state rollback, native identity and loss reject. Future
current transaction must own/validate pending state separately; this initial
state graph grants no copy/access/current/native submission/map/result proof.
Null validator-only; Vulkan/Metal selected native original pair is DiagnosticFailure;
WebGPU selected full-input/state metadata is Prototype. Public capabilities,
general/bindless recording, NativeSync and Production/Platform stay unpromoted.

## OriginalTextureReadbackDeclarationSeparation/1

Status: Contract Complete / Prototype Complete / Backend Validated selected
WebGPU immutable declarations only, verified 2026-10-03. Host/Null10/10,
Wasm/browser5/5, inventory41/41; both reports clean. Real mapAsync and exact pending/
rollback/native-mutation/invalidation/drop/loss verified, hot allocation0. Evidence
original-readback-declaration-* YAML/log/XML. Current/native/map-result and
Platform/Production remain unpromoted. Declaration
validation is immutable original identity/content/creation metadata only. It must
not borrow the initial state observation, claim pending ownership, require a map
state, or issue copy/map/result/recorded authority. Retain exact quota-bound
layout/source/destination, actual Completed upload declaration and original native
allocation/registration/device/object identities. Validate active known controls
without accepting them as current access; an owned current transaction must prove
its own pending/committed observations separately. CreationReady and all three
successful native creation scope observations are mandatory for destination/pair.
Native buffer size and CopyDst|MapRead usage remain exact; current map state is
separate from immutable declaration. Default validation, factories, original mip
mapper and public execution diagnostics retain strict initial state and Unmapped
checks. No caller policy bit selects relaxed state validation. No new proof friend,
CPU completion, private registry entry or native emission is allowed.

Null remains validator-only, Vulkan/Metal selected native declaration is explicit
DiagnosticFailure and WebGPU selected declaration metadata is Prototype. Tests
use actual completed upload/native private destination, prove pending and rollback
separation without promoting authority, reject changed identities/invalidation/loss,
retain allocation-free checks, quick contract and Null/both browser smoke paths.

## OriginalTextureReadbackSourceStateIntent/1

Status: Contract Complete / Prototype Complete / Backend Validated selected
WebGPU initial state intent only, verified 2026-10-03. Host/Null10/10, Wasm/browser5/5,
inventory42/42; both reports clean. Cold graph/usage/arguments/scalars/input/states
peaks468/160/491/92/428/200 Wasm bytes, hot allocation0. Actual original caller drop,
loss and exact retirement verified. Evidence original-readback-state-intent-*
YAML/log/XML. Current/native/map-result and Platform/Production unpromoted. Source
CopySrc intent must be expressed by an explicit original TextureBarrier descriptor
and positional reference. Append OriginalTextureReadbackState without changing
existing reference enum values. This reference resolves only the exact original
source texture; it never exposes the private destination as an ordinary Buffer.
Independently bound state references with a default-zero budget. The exact
Texture/Readback pair references remain unchanged and are required once only.
Reuse frozen dependency arguments, original complete state controls, whole-texture
range, explicit before/after/stage/access validation and ordered state resolution.
Check CopySrc target and CopyDst private destination at the actual readback command
position. A no-barrier CopyDst source fails access validation deterministically;
full-input metadata success alone grants no state transition, current journal,
copy/submit/map/result proof. Explicit unknown/partial ranges, wrong source/state/
scope/position and mismatched domain fail. Null validator-only; Vulkan/Metal
selected original pair DiagnosticFailure; WebGPU selected state intent Prototype.
Public/general/bindless/NativeSync and Platform/Production remain closed.

## OriginalTextureReadbackOwnedStateTransaction/1

Status: Contract Complete / Prototype Complete / Backend Validated selected
original WebGPU current CPU metadata only, verified 2026-10-03. Host/Null10/10,
Wasm/browser5/5, inventory43/43; both reports clean. Cold transaction peak340 Wasm
bytes; hot apply/validate/commit/rollback allocation0. Original caller drop,
terminal invalidation and actual Prepared/Pending loss/exact retirement verified.
Evidence original-readback-current-* YAML/log/XML. Scoped original graph/input/
current ownership parent f3c complete; native recording/submission/map/result,
Platform/Production unpromoted. Extend the
existing original state transaction and reuse its Entry, MutationJournal, pending/
committed observations and lifecycle. A separate readback factory selects a sealed
private profile after exact original five-command TextureBarrier/BeginTransfer/
TextureReadback/EndTransfer/Finish validation. No caller boolean relaxes validation.
Require one exact original pair, original full input and complete staging/target/
private destination state inventory; reject additional transfer/resource/pipeline/
group/upload sources. Independently bound original pair count, mip count, unpublished
result bytes, state controls, transitions, private CopyDst access claim and metadata.
The target final state is solely the explicit original L2 barrier's CopySrc state;
private destination acquires one same-state CopyDst journal observation to own its
write access, without adding an inferred state transition. Staging stays unchanged.

Current validation reuses exact immutable native/source/pair creation declarations;
sealed initial intent was fully validated at admission. Require unchanged staging
observation and exact object/journal/value/revision PendingObservation for target
and private destination; a foreign journal, subsequent write/revision or replaced
native/allocation identity fails. CPU commit validates then commits its own journal,
retaining committed observations; it grants no GPU submission/completion/result
proof. Abandon rolls back only owned uncommitted mutations, staling old initial
snapshots. Terminal invalidation and actual loss invalidate all retained original
controls and rollback without restoring active authority. Common proof issuers
remain exactly two; native recording/submission/map/result remains separately
unproven. Null validator-only, Vulkan/Metal selected native pair DiagnosticFailure,
WebGPU selected current CPU metadata Prototype. Preserve quick/Null/both native
browser paths, allocation-free apply/validate/rollback/commit, full caller drop and
exact quota/native graph retirement. Platform/Production remain unpromoted.

## OriginalTextureReadbackCopyLowering/1

Status: Contract Complete / Prototype Complete / Backend Validated selected
original WebGPU frozen copy facts only, verified 2026-10-03. Host/Null10/10,
Wasm/browser5/5, inventory44/44; both reports clean. Cold copies peak332 Wasm
bytes, hot allocation0. CPU arrays/3D/BC/cube, original mutations and actual current
RGBA8 arrays/native/drop/loss verified. Evidence original-readback-copies-*
YAML/log/XML. Native recording/submission/map/result and Platform/Production
unpromoted. Cold decisions
retain the exact quota-bound original readback layout and mip index/physical facts,
with explicit backend/version/Prototype opt-in/operation/dimension/physical origin/
extent. Reuse existing texture copy physical geometry; extract and share an existing
helper if upload/readback would duplicate block rounding or array/3D lowering.
Preserve original authored padded buffer offsets, pitches, rows and complete mip
range; no new geometry guess, temporary staging or ordinary-copy impersonation.
Lowering failure leaves caller output untouched. Null validator-only, Vulkan/Metal
selected native DiagnosticFailure, WebGPU selected original copy Prototype.

Current mapper must prove exact quota-bound original readback transaction profile,
Pending/Committed CPU journal observations, original input owner/layout, creation/
allocation/resource/native-device identity and native Unmapped state before binding
facts. Current metadata cannot issue native emit/submit/queue/map/result proof.
Frozen independent decision/mip/metadata capacity and exact native query facts are
mandatory. Mutation of any decision/original mip or expected device, foreign owner,
Prepared/Abandoned/Invalidated, stale/foreign resource revision, map state or actual
loss fails closed and preserves output. Proof issuers remain exactly two. Quick
contract/Null and both feature modes, original drop/retirement and allocation-free
mapping stay required. Platform/Production remain unpromoted.

## OriginalTextureReadbackCommonColdRecordingOwnership/1

Status: Contract Complete / Prototype Complete / Backend Validated selected cold
WebGPU ownership only, verified 2026-10-03. Host/Null10/10, Wasm/browser5/5,
inventory45/45; both reports clean. Cold recording/current/copies/reservation
peaks952/340/332/88 Wasm bytes; regression owner/reservation152/88. Hot allocation0,
recorder-only drop and actual loss verified. Evidence original-readback-recording-
cold-* YAML/log/XML. Native record/submit/map/result and Platform/Production remain
unpromoted. The existing
common recorder alone cold-admits the exact original state graph, readback current
transaction and frozen copies. Store and verify identical quota-bound owner/control
blocks in command, recorder and retained completion graph. Use the existing native
command preparation/device/queue identity and bounded completion reservation;
reserve callbacks/native command/debug/mip/CPU capacities independently before any
record call. No encoder/copy/finish/submit/map work is permitted by this cold phase.
Prepared ownership must remain observably Unrecorded with deterministic native
recording-unproven and submission rejection; empty command payload never substitutes
for recorded GPU work. Other original/generic record entries reject the readback
profile. Completion graph matches the exact copies and transaction and preserves
original abandonment/quarantine/loss lifecycle. Null validator-only; Vulkan/Metal
selected original native DiagnosticFailure; WebGPU cold owner Prototype. No new
proof issuer, public capability, NativeSync, result publication, Platform or
Production promotion. Preserve quick/Null/both modes, original mutation/drop/loss,
independent exact/one-less/exhausted CPU budgets and allocation-free hot checks.

## OriginalTextureReadbackNativeRecording/1

Status: Contract Complete / Prototype Complete / Backend Validated selected original
WebGPU recording only, verified 2026-10-03. Host/Null10/10, Wasm/browser5/5,
inventory46/46; both reports clean. Four real native recording cases (success,
copy error, finish error, held-callback caller drop), original physical fields/UTF8
labels, hot allocation0 and actual Pending native loss verified. Evidence original-
readback-native-recording-* YAML/log/XML. Submission/map/result and Platform/
Production remain unpromoted. Current initial intent mapping revalidates exact
owned transaction observations; default graph still rejects Pending. Rollback
restores values and advances revision; retry requires a fresh original snapshot.
L2 language
remains exactly source TextureBarrier, BeginTransferPass, original TextureReadback,
EndTransferPass, Finish. Freeze the source barrier's TransferDependencyLowering/1
Equivalent/ImplicitOrderedUsage decision at cold admission; validate its exact
original descriptor/range/current intent again before apply and consume it explicitly
before emitting GPU work. WebGPU orders copy usages within the same original
encoder/queue under WebGPU resource usage tracking; this is the versioned equivalent
mapping, never an omitted barrier heuristic. Only Transfer/CopySrc or CopyDst full
original texture scope is admitted. Null validator diagnostic; Vulkan/Metal selected
native dependency/readback payload diagnostic. WebGPU explicit Prototype only.

After owned current transaction apply, native emitter requires Pending state and
exact current copies/native device/Unmapped identities, then emits frozen original
mipLevel/Color aspect/physical origin/extent/padded buffer offset/row pitch/rows per
image verbatim. No rounding, native defaults, queue/writeTexture/submit/map work.
Use original labels for two debug scopes, one native encoder, actual finish and all
three actual recording callbacks. Prepared/Pending/error/abandoned/loss or foreign
original owners cannot issue recorded proof. Callback failure rolls back the owned
journal; native loss terminally deactivates original controls. All capacity and
callbacks are reserved cold; record hot path allocates0. A successful native
Recorded command still has no native completion/map/result authority. The common
submission entry explicitly rejects this profile until f3d2b2 introduces a private
native lifecycle controlled only by the existing two common issuers. Quick/Null and
both feature modes, exact native fields, native finish/copy errors, callback caller
drop, actual loss and exact retirement are required. Public/general/bindless/
NativeSync/Platform/Production remain unpromoted.

## OriginalTextureReadbackNativeCompletion/1

Status: Contract Complete / Prototype Complete / Backend Validated selected original
WebGPU native completion only, verified 2026-10-03. Host/Null10/10, Wasm/browser5/5,
inventory47/47; both modes clean. Cold copies/submission380/196 Wasm bytes; hot
allocation0. Both callback orders, copied original2 mip/layer row bytes, actual
submission error, post-submit native mutation and four loss/drop paths verified.
Evidence original-readback-native-completion-* YAML/log/XML. Production map/result
and Platform/Production remain unpromoted. Keep exactly
WebGpuOrdinaryRecording and WebGpuOrdinarySubmission as native proof issuers. The
frozen copies retain a private native lifecycle independent of CPU transaction
Prepared/Pending/Committed. Initial native status is Unrecorded; only the common
recording callback observing all3 successful actual recording scopes and exact
finished original command/device/queue/identity may mark Recorded. CPU apply or
commit never changes that status. Common submission may admit only exact Recorded
readback copies bound to its retained command and ticket. Reserve its existing4
callbacks and quota cold; submission requires original graph matches, commits the
owned state and privately captures original command/device/queue and exact retained
ticket index/generation as Submitted before actual queue submission. Any partial
commit failure invalidates the original graph and emits no GPU work.

Only successful actual queue completion AND all3 submission scopes AND exact
retained original ticket/finished command/device/queue/current transaction/copies
may mark native Completed. Queue receipt, CPU Committed, only queue, only scopes,
forged owners or stale/mapped/current mutations cannot. Native map eligibility
requires Completed plus exact original current controls/native Unmapped resources;
production map/result publication remains closed for f3d2b3. Copy result bytes are
verified with test-only native map/extraction from explicit original padded layout,
never fabricated proof or production bypass. All error/quarantine/loss paths revoke
native eligibility and invalidate original controls before retaining/retiring graph;
actual loss remains terminal through callbacks. No new public/default capability,
NativeSync or Platform/Production promotion. Null validator-only diagnostic;
Vulkan/Metal selected native readback payload deterministic DiagnosticFailure;
WebGPU explicit Prototype native completion only. Measure changed copy CPU quota;
quick/Null/both modes, CPU-commit negative, callback permutations/partial arrival,
original mutation/caller drop, native submission error and actual loss with exact
retirement and hot allocation0 are required.

Native completion additionally binds one explicitly budgeted original completion
owner (maxNativeCompletionOwners defaults0). A private weak owner observes its
stable closed/expired state without an ownership cycle; that owner is registered
with the existing actual native-loss observer and survives until loss. The status
becomes terminal DeviceLost on that observation, even if a later native device
resets the global loss flag. Completed copies cannot regain map eligibility through
CPU metadata or an unrelated new device. Existing four submission participants are
queried individually for deterministic scope-only/queue-only negative tests; no
timing assumption grants completion. Native error quarantine and post-submit
native mutation each retain the exact original graph until actual loss retirement.

## OriginalTextureReadbackColdMapOwnership/1

Status: Contract Complete / Prototype Complete selected cold ownership only,
verified 2026-10-03. Host/Null10/10, Wasm/browser5/5, inventory48/48; both modes
clean. Mapping108/declaration64 Wasm bytes, existing result1536 bytes reused;
hot allocation0. Exact exclusive storage reservation, cancellation/retry, caller
drop, mutation and actual device loss verified. Evidence original-readback-cold-map-*
YAML/log/XML. Native map/read/unmap/publication remain unimplemented. The private
readback destination already owns real bounded PMR CPU result byte storage, cold
resized under its existing original result quota. Reuse that exact storage and
quota; do not allocate or account a second byte buffer. Initial bytes remain unpublished and
have no copied-data proof. Retain original layout and complete source/private
native graph; reserve one backend map operation, one actual native callback and one
immutable result declaration under independently explicit counts/metadata/bytes.
Map operation construction is cold and requires sealed original copy owner/current
Prepared transaction with strict initial Unmapped access. No callback invocation,
map/read/unmap/publication or completion capability is granted by cold ownership.

A future start requires exact original native Completed and original committed
controls, calls native mapAsync(Read) once and uses a separate explicit owned-map
current contract for real Pending/Mapped states; default current/copy gates remain
strict Unmapped. Result representation is explicit original padded buffer bytes
with retained versioned mip/layer/row-pitch/rows-per-image/offset metadata. No
implicit repacking or interpretation; any packed representation needs an explicit
future result contract and reusable checked row conversion. Publication needs
actual map success plus native range-read success and unmap, never zero-filled
cold storage or queue receipt. Map/result failures are deterministic and keep the
result unpublished. Map callback ownership is preallocated and retains exact graph
through actual loss/late arrival/caller drop. Null validator diagnostic; Vulkan/
Metal selected original native map/result DiagnosticFailure; WebGPU cold Prototype
only. Proof issuer friends remain exactly two; no new public/general/bindless/
NativeSync/Platform/Production promotion. Quick/Null/both modes, independent exact/
one-less/exhausted quotas, original native mutation/drop/loss and exact retirement
are required before closing this scoped stage.

Cold storage exclusivity reuses core::runtime::CompletionSignal reservation:
exact original destination ticket stays reserved through the map operation and
immutable declaration; no callback work begins in this stage. Cancellation releases
the exact reservation. Only the future real map/read/unmap success may complete
it, making resultPublished true and permanently preventing a second writer. Public
code receives no host-signal or reservation mutation API. Separately budget one map
owner, one callback, one immutable declaration, retained original native/content/
result bytes, and two CPU metadata quotas; callbacks do not imply object capacity.

## OriginalTextureReadbackCommittedDeclaration/1

Status: Contract Complete / Prototype Complete selected declaration validation,
verified 2026-10-03. Host/Null10/10, Wasm/browser5/5, inventory49/49, both modes
clean; actual Pending/Mapped separation, CPU commit negative proof, forged/stale/
mutation rejection and allocation0 verified. Evidence original-readback-committed-
declaration-* YAML/log/XML. Map/read/unmap/publication remain next stage.
Extract the existing exact original observation loop without changing default
transaction access validation. A separately named committed readback declaration
validator requires sealed original readback transaction, Committed status, current
ready source/destination declarations and exact committed control observations.
It never grants map access, copy execution or native completion. Do not add a
boolean allowMapped to a general validator.

An original native completed declaration validator additionally requires existing
issuers' Completed identity/device/queue/ticket and stable live completion owner,
all frozen copy decisions and current original texture/buffer facts. It may inspect
a Pending/Mapped original destination as a declaration, but does not claim ownership
of that map. Default transaction/copies/native completion gates continue requiring
Unmapped. Future map executor separately proves exclusive original result ticket,
its issued real native map request, Pending/Mapped state and successful callback.
Null validates declarations only; Vulkan/Metal selected native proof DiagnosticFailure;
WebGPU selected Prototype original proof. No new proof issuer, public/general/
bindless/NativeSync/Platform/Production promotion. Quick/Null/both browser modes,
negative Prepared/Pending/forged/mutated controls/native facts, actual map-state
separation, zero hot allocation and exact retirement required.

## OriginalTextureReadbackNativeMapping/1

Status: Contract Complete / Prototype Complete / Backend Validated selected original
WebGPU data mapping only, verified 2026-10-03. Host/Null10/10, Wasm/browser5/5,
inventory52/52; no unexpected diagnostics/native errors, one actual expected range
error explicitly recorded. Mapping228/result72 Wasm metadata bytes; PMR1536 bytes
reused, hot allocation0. Both orders/partial receipt, original2 mip/layer bytes,
real map/read/scope/unmap errors, mutation, canceled draft generation, forged result,
full callbacks/result-only drop and actual Pending/post-map/snapshot loss verified.
Evidence original-readback-native-map-* YAML/log/XML. Platform/Production and public/
general/bindless/NativeSync remain unpromoted; proof issuers2. Selected
WebGPU backend-private Prototype execution only. The caller cold reserves one map
callback and three independent native error-scope callbacks; maxCallbacks still
bounds the map callback, a new default-zero maxScopeCallbacks separately bounds the
native scope batch. Retain all original graph and the exclusive result ticket.
Native map submission is exactly once after strict original native Completed proof;
Read mode, offset zero and entire original aligned byte capacity are frozen.

Join native map success and three successful native request scopes. At the real
successful map callback require the original committed declaration, exact result
storage/ticket and actual Mapped state; read the exact mapped range into existing
private mutable PMR result bytes with checked native status, unmap, and verify
Unmapped. These unpublished bytes cannot escape while any scope is pending. Only
all evidence and revalidated original Completed/Unmapped identity permit completing
the exclusive publication ticket. No host signal, placeholder bytes, repacking,
second buffer or native proof issuer. Result is immutable original padded bytes and
original layout metadata, a CPU snapshot that remains readable after device loss;
loss before publication cannot publish. Failed operations are terminal and cannot
retry on the same map owner. Later callback arrival may only retire its exact owners.

Callback ownership is transferred before issuing native map to handle synchronous
errors; each preallocated callback retains the map owner until real arrival. Reject
forged/repeated starts, foreign Pending/Mapped access, stale native/control identity,
map status failure, native range-read failure, failed scopes, unmap failure and
actual device loss with stable diagnostics. Cleanup unmaps only the original issued
request's captured buffer; declarations never grant cleanup of a foreign map.
Successful result ownership retains original CPU storage and layout even when map
owner and all caller graph references are dropped. No hot allocation or blocking.
Null validator-only diagnostic; Vulkan/Metal selected original mapping payload
DiagnosticFailure; WebGPU actual Prototype payload. Public/general/bindless/
NativeSync/Platform/Production stay closed; visual RenderDoc unavailable diagnostic
retained. Tests require original two-mip/layer content bytes, partial and both callback
orders, real map/range errors, scopes failure, Pending/post-map loss, mutation/full
drop/late callbacks/exact retirement and both browser feature modes.

Pinned emdawnwebgpu GetMapState reports its C++ bridge state, which Unmap updates
after the JS call; it is not an independent browser buffer observation. Native map
state acceptance therefore requires both the WebGPU C API bridge state and the
spec-defined original GPUBuffer.mapState property to match. Backend-private
WebGpuNativeBufferMapState reuses this deterministic check in strict destination
access and native map/read/unmap publication. Missing original object/property or
bridge/browser disagreement rejects; no inferred state or compatibility fallback.
The no-op unmap negative fixture must be rejected even when the bridge says Unmapped.

Result publication must be bound to its exact original reservation generation,
not just a destination-wide completed flag. Cancellation releases the ticket and
resets the private signal generation; retained canceled drafts remain unpublished
if a later owner reserves and publishes the same storage. The completed immutable
result retains its exact ticket; core CompletionSignal::isComplete(Ticket) observes
that generation after reservation release. No host signal or generation inference.
Result::validatePublishedOriginal requires its sealed quota-bound owner, exact
original storage pointer/size and completed original ticket; forged and canceled
owners fail. CPU snapshot validation remains independent of later GPU device loss.

## OriginalQueryPoolInput/1

Status: Contract Complete / Prototype Complete CPU metadata only, verified
2026-10-03. Host/Null10/10, Wasm/browser5/5, inventory53/53; both modes clean.
Independent metadata104/label32/scope24 Wasm bytes, hot allocation0. Fields5,
ordered duplicate/zero/max scope IDs, pointer presence, binary/Unicode labels,
all3 quota pressure, forged/mutation/drop/loss and distinct type/index contracts
verified. Evidence original-query-input-* YAML/log/XML. Query/native/Available
and Backend/Platform/Production remain unpromoted. Reuse existing
QueryPoolDesc, shared L2 query validator, CommandScalarArguments::Query and core
bounded sequence/PMR ownership. Scalar capture retains exact QueryType/count/
timingScopeIdCount/pipelineStatisticsMask and explicit caller pointer presence;
label bytes and ordered uint64 scope IDs are cold copied without sorting, pairing
inference, deduplication or repairing invalid values. A nonempty inaccessible scope
pointer rejects before any allocation. Empty nonnull pointer remains explicit CPU
metadata and fails descriptor legality at the separate shared L2 validation boundary.

Independent version/Prototype opt-in, scope and label capacities plus three original
metadata/label/scope CPU quotas bound all storage. Unknown types, zero/max counts,
unknown masks and binary/Unicode labels remain exact metadata; representation alone
never grants descriptor legality, query owner, native payload or Available result.
Shared L2 validator gains a scalar entry with explicit pointer-presence facts; the
existing descriptor wrapper delegates to it without any semantic change. Snapshot
validation consumes that shared entry without hot string/descriptor allocation.
Timestamp/PipelineStatistics commands remain distinct and exact index comparison
reuses existing scalar arguments. Query reference/full-original-input/native/public/
general/bindless/NativeSync gates remain closed until original native source binding.

Null CPU validator only; Vulkan/Metal/WebGPU CPU capture supported but original
native source DiagnosticFailure. The actual WebGPU command-point V1 facts currently
have timestamp feature true and encoderWrite false: Degraded with existing stable
RHI-WEBGPU-TIMESTAMP-COMMAND-API-UNAVAILABLE. Do not substitute pass boundary timing
or infer support from feature/name. Quick/Null/both browser modes must check every
scalar/ordered scope/label mutation, forged ownership, independent exact/one-less/
exhausted/missing/oversized quotas, full caller drop, actual loss CPU retirement,
zero hot allocation and unchanged source/execution gates. No shader/native proof
issuer or Backend/Platform/Production promotion.

## OriginalQuerySourceLowering/1

Status: Contract Complete / Prototype Complete diagnostic source gate only,
verified 2026-10-03. Host/Null10/10, Wasm/browser5/5, inventory54/54; both modes
clean unexpected diagnostics/native errors, hot allocation0. Missing9 facts,
complete-API/no-payload negative, all4 outcomes, exact owner/kind/index/version/
opt-in and actual loss retirement verified. Actual WebGPU remains Degraded
RHI-WEBGPU-TIMESTAMP-COMMAND-API-UNAVAILABLE. Evidence original-query-source-gate-*
YAML/log/XML. Original native source remains unimplemented; no Backend/Platform/
Production promotion. A decision
consumes the sealed original QueryPoolInputSnapshot and its exact distinct query
command/index declaration after version1 and explicit Prototype opt-in admission.
It cannot accept arbitrary raw descriptor facts as original query ownership.

Null is validator-only with deterministic no-native-source diagnostic. Vulkan and
Metal selected original bounded query payloads are DiagnosticFailure until exact
native owner/recording/completion paths exist; do not reuse public query pools as
unbounded original owners. WebGPU PipelineStatistics is Degraded under the V1
backend contract (standard QuerySet lacks those counters). WebGPU Timestamp reuses
all nine queried WebGpuTimestampCommandPointGate/1 facts. Missing feature, command
point or readback APIs yield that gate's existing Degraded diagnostic. Complete API
facts still yield original-source DiagnosticFailure because no sealed bounded
native payload exists. No caller-supplied nativeImplemented override or API-only
Native result. Decisions retain exact original input/command identity without
allocating or changing query/reference/recording/completion/public/general gates.
Actual loss additionally rejects native source admission. Quick/Null/both browser
modes verify missing each native fact, all-complete APIs fail closed, version/opt-in,
invalid/forged/type/index/cross-owner cases, current actual Degraded observation,
caller drop/loss retirement and allocation0. Issuer friends remain exactly two;
no native/Backend/Platform/Production promotion.

## OriginalQueryCreation/1

Status: Contract Complete / Prototype Complete; Backend Validated selected private
WebGPU cold creation only, verified 2026-10-03. Host/Null10/10, Wasm/browser5/5,
inventory55/55, both reports clean. Wasm owner156/callback36 bytes, initialization/
joins/validation allocation0. Real creation/scopes, reverse partial receipts,
failed native validation, altered type/count/device/API, quotas/forged/full-drop
and prepared/pending/ready actual loss verified. After loss, native QuerySets are
destroyed once and original pinned emdawn wrapper entries removed; exact CPU
retirement verified. Evidence original-query-creation-* YAML/log/XML. Timestamp
execution remains Degraded; source/Available/public/general/NativeSync/Platform/
Production unpromoted. This private
Prototype contract separates L2 query creation/identity/lifetime intent from
command execution and results, matching the existing creationSupported distinction.
A successful creation is NotRecorded; it cannot prove Recorded, Pending, Available,
command completion, original source admission, TimestampQueries or NativeSync.
OriginalQuerySourceLowering/1 and all nine execution/readback fact gates stay intact.

Creation consumes a sealed valid original CPU query input, preserving exact type,
count, label bytes and ordered caller timing scope map without retaining caller
pointers. Version1 and Prototype opt-in are required. Null gives validator-only
DiagnosticFailure; Vulkan/Metal selected bounded creation is DiagnosticFailure
until implemented. WebGPU PipelineStatistics is Degraded (standard QuerySet has
no such counter payload). WebGPU timestamp creation requires actual enabled
GPUDevice timestamp-query feature, createQuerySet, pushErrorScope/popErrorScope,
QuerySet count/type access and destroy APIs. Execution API presence is independent.
WebGPU specification GPUQuerySetDescriptor count is at most 4096; this normative
bound is validated before native creation, never guessed from a device/OS name.
The shared L2 validator has a stricter GpuTimingSample::kMaxScopes*2 count bound;
creation preserves that bound and does not expand the L2 descriptor vocabulary.

Cold preparation binds the immutable decision to the identical original input and
reserves one native object, exact original query slots, retained scope/label counts,
and three callback records. Independent CPU owner and callback quotas pay actual
core allocations. No size-only native heap or invented resident bytes: the API
exposes object/slot counts, not resident memory. The opt-in prototype declares
resident bytes unobservable and makes no public memory-budget guarantee. Device
and QuerySet wrapper lifetime use preallocated core ExternalObjectSlot ownership;
no allocations occur between native creation and adoption or in callback joins.

Exactly one creation attempt runs under real Internal/OutOfMemory/Validation
scopes. All three successful scope receipts and matching native original device,
QuerySet count/type/identity are required for CreationReady. Partial/failed receipts,
missing payload, altered count/type, identity mismatch and device loss fail closed
with stable diagnostics. Failed owners cannot retry; recovery requires fresh cold
preparation on a live admitted device. Callbacks retain original ownership through
caller drop; final retirement destroys/releases each adopted native object once.
Device loss closes prepared/pending/ready owners and lets real late callbacks
retire CPU storage; it does not manufacture availability or completion. All access
is externally serialized under the existing WebGPU browser-thread contract.

Quick contract/Null hidden-frame and both actual browser modes must validate
version/opt-in/input/facts/four-backend outcomes, every independent quota and count,
forged/stale/cross-input ownership, partial/failed actual scopes, altered native
count/type, full caller drop and prepared/pending/ready actual loss. Real positive
creation is required; CPU mocks cannot prove Backend Validated. No shader changes,
new recorded proof issuer or public execution promotion. RenderDoc unavailable:
RHI-RENDERDOC-TOOL-UNAVAILABLE; no visual/Platform/Production acceptance.

## OriginalQueryBinding/1

Status: Contract Complete / Prototype Complete original binding only, verified
2026-10-03. Host/Null10/10, Wasm/browser5/5, inventory56/56; both reports clean.
Wasm fixed value56 bytes, extra heap0, hot allocation0 with all5 original graph
quotas full. Cross-input/forged/type/index/counts and same-input/distinct-native
owner identity, actual prepared/pending/rejected/ready, full drop and actual loss/
late retirement verified. Evidence original-query-binding-* YAML/log/XML. Native
creation prerequisite Backend Validated; query recording/source/Available/public/
Platform/Production unpromoted, issuers2. Bind the exact
Timestamp/PipelineStatistics command kind/index declaration to its identical sealed
WebGpuOriginalQueryOwner and retained original CPU input. Two independently created
native owners of the same CPU snapshot are distinct and never interchangeable.
CreationReady does not repair an invalid query index or substitute caller metadata.
Reuse QueryPoolOriginalInputArguments identity/validation and native owner validation.

An immutable, privately constructed fixed-size value owns the existing shared graphs;
copy/move/capture/current comparison do not allocate, create native objects, clone
scope maps or issue callbacks. Existing CPU/native budgets remain owned and charged
by the original factories. Independent binding retention counts cover one native
owner, exact query slots, original scope IDs and label bytes; no count derives from
callback capacity or bindless budgets. Version1/Prototype opt-in default closed.
Cold binding may retain Prepared/Pending/Ready original owners, but cold declaration
never proves readiness. Rejected/lost originals cannot be admitted. Current validation
requires exact sealed native CreationReady evidence; execution additionally consumes
the unchanged OriginalQuerySourceLowering/1 and actual queried command-point gate.
Even complete APIs plus creation evidence do not prove recording, results or completion.

Null validator-only DiagnosticFailure; Vulkan/Metal selected original binding is
DiagnosticFailure until implemented; WebGPU supports the cold declaration only,
with execution still Degraded or deterministic original-command-payload failure.
No Query source reference, full-original-input completion, native command emission,
new recorded proof issuer, public TimestampQueries/NativeSync or Available result.
Tests require exact cross-input and same-input/different-native-owner negatives,
forged owner, invalid type/index, counts/version/opt-in, prepared/pending/ready/rejected
separation, no-allocation pressure, complete-API execution negative, full caller drop
and actual device loss with late callback/native/CPU retirement. Quick/Null/both
browser modes; no shader or visual change, no Platform/Production promotion.

## Original query lifecycle continuation — f4b4 review checkpoint

Status: implementation contract under review; not Contract Complete. Existing
WebGpuRhiQueryPoolPayload stores copied scope IDs and mutable QueryExecutionState
in PMR vectors, but uses a mutex and exposes a public native constructor. Its
WebGpuRhiQueryPool descriptor retains the caller timingScopeIds pointer. Those
objects cannot provide original sealed ownership or nonblocking hot-path evidence.
Its reset rule rejects Recorded/Pending; the original path must preserve that rule
and additionally retain command/submission/mapping ownership until exact completion
and snapshot publication permit retirement. A created query remains NotRecorded.

Reuse candidates inspected: core VersionedMutationValue and MutationJournal for
revision/journal-specific observation and rollback ABA rejection; bounded owner/
sequence/retention utilities for cold stable slot storage; CompletionSignal tickets
and CompletionEvidenceBatch for distinct actual submission/scope/map receipt.
OriginalQueryBinding/1 supplies exact kind/index/native-owner input. Do not duplicate
those algorithms in query code, and do not let CPU state writes issue native proof.

Before native recording, define a sealed slot generation/reset contract, explicit
per-slot metadata quota and completion-owner budgets, record/submit rollback,
failed submission quarantine, exact completion/readback publication and loss
retirement. Query completion and Available must remain distinct: native completion
without original mapped bytes cannot publish results. Source Query references and
common recorded proof stay closed until actual command-point lowering is validated.
Current actual execution facts have encoderWrite=false; method presence in pass
prototypes cannot authorize an experimental inside-pass extension or pass-boundary
substitution. Four backend conclusions and negative CPU/native gates are required
before implementation; native positive execution remains unvalidated.

## OriginalQuerySlotDeclaration/1

Status: Contract Complete / Prototype Complete CPU declaration only, verified
2026-10-03. Host/Null10/10, Wasm/browser5/5, inventory57/57, both reports clean;
hot allocation0. Quarantine/reset, exact/foreign journals, rollback ABA/commit,
terminal invalidation and exhaustion verified. Evidence query-slot-declaration-*
YAML/log/XML. No Backend/Platform/Production/native execution or Available proof.
This shared L2 CPU declaration
control is not GPU evidence. Phases are NotRecorded, Recorded, Submitted, Completed,
SnapshotDeclared, Quarantined and FailedIdle. Record requires NotRecorded; Submit
requires Recorded; Complete requires Submitted; DeclareSnapshot requires Completed.
Fail sends Recorded/Submitted/Completed to Quarantined, preserving outstanding
ownership; failing NotRecorded/SnapshotDeclared produces FailedIdle. RetireFailure
requires Quarantined. Reset only accepts NotRecorded/SnapshotDeclared/FailedIdle,
with no pending journal mutation; Recorded/Submitted/Completed/Quarantined reject.
Unknown/repeated/out-of-order events fail with stable diagnostics and no phase write.

Completion, snapshot and failure-retirement events describe CPU intent only. The
future native executor must consume sealed exact completion/scopes/map/read/publication
or device-loss ownership before declaring these phases. No method returns native
execution, query Available, source-reference or completion proof. Null supports the
validator model only; Vulkan/Metal/WebGPU consume the same declarations but native
lifecycle payload is DiagnosticFailure until implemented and validated. CPU tests
cannot promote Backend Validated. VersionedMutationValue supplies stable revision,
exact-journal Pending observation, conflict detection, rollback ABA and terminal
exhaustion/invalidation; no local generation or mutation algorithm is duplicated.

## OriginalQuerySlots/1

Status: Contract Complete / Prototype Complete cold slot metadata only, verified
2026-10-03. Host/Null10/10, Wasm/browser5/5, inventory58/58; both reports clean.
Wasm graph92/stable slots304 bytes, original query owner188/callback36 after adding
exclusive reservation. Previous156 owner measurement is historical. Exact quotas,
partial allocation retirement, same-input/different-owner/forged identity, exclusive
replacement, all4 slot reset revisions, full drop and actual loss retirement pass;
hot reset/capture/current allocation0. Evidence original-query-slots-* YAML/log/XML.
Generic control exhaustion tested; graph overflow rollback/invalidation branch
source-verified, not exercised by UINT64_MAX iterations. Native lifecycle/Available/
Query reference/public/Backend/Platform/Production gates remain closed. An original native owner admits
one exclusive cold slot graph, reserved through its lifetime using the existing core
CompletionSignal ticket utility solely for exclusivity (never signal/complete it).
Independent owner metadata and slot-storage quotas pay a bounded sequence of stable
core-owned declaration controls for exactly the original descriptor count. No native
objects, command encoders, callbacks or results are created by slot preparation.
Version1/explicit Prototype opt-in plus native-owner/query-slot/scope/label retention
counts are independent; no callback capacity determines query-slot capacity.

Cold declaration may retain Prepared/Pending/CreationReady owners; rejected/lost
owners fail. Privately constructed observations retain the graph and exact binding,
index, phase/revision and exclusive original ticket. Forged owner/control blocks,
other CPU inputs or independently created native owners cannot satisfy this graph.
CPU current observation is separate from native-ready/execution admission. Default
recording/source/Available gates stay closed; execution consumes original binding's
actual command-point diagnostic even when every CPU declaration is current.

Pool reset preflights every slot's legal phase and journal state. Serialized reset
uses one existing MutationJournal; logical failure rolls back and terminally
invalidates the graph rather than exposing a partially reset pool. Successful reset
advances every slot revision even from NotRecorded, so old observations never revive.
Original loss and terminal invalidation close capture/reset/current admission; graph
and observations retain original ownership until retirement. Final graph release
releases the exclusive ticket and advances its generation. Old retained observations
also retain their original graph, preventing replacement until they retire. There
are no locks, waits, file IO, hot allocation or default STL owned allocation.

Null validator-only DiagnosticFailure; Vulkan/Metal selected original slot graph
DiagnosticFailure until implemented; WebGPU explicit Prototype cold metadata graph,
execution currently Degraded. Quick/Null/both browser modes verify phase legality,
quarantine/reset, journal conflict/rollback/commit/exhaustion, independent quotas,
exact counts/forged/cross-input/cross-native owner, exclusive duplicate graph,
reset-stale observations, full drop and actual loss retirement. No native lifecycle,
new proof issuer, shader, visual, Platform or Production promotion.

## Original query recording — f4b4c native API review checkpoint

Status: contract review in progress; not Contract Complete. Isolated actual browser
API observation archived as original-query-command-point-api-probe.yaml plus browser
log, 2026-10-03. It uses a real adapter/device, timestamp QuerySet, command encoder
and compute pass; it never submits, reads results or proves L2 original recording.
Baseline device enabled timestamp-query; encoder.writeTimestamp absent, render and
compute pass writeTimestamp methods present. The real compute pass call threw:
"Use of the writeTimestamp() method on compute pass requires the
'chromium-experimental-timestamp-query-inside-passes' feature to be enabled".
The real adapter did not advertise that extension, so explicitly requested probe
opt-in was rejected before requesting an unsupported device. Uncaptured errors
were empty; all3 actual error scopes returned null while the API call itself failed.
Therefore successful creation/scopes or method presence alone cannot prove recording.

Pinned emdawn library_webgpu.js directly forwards wgpuCommandEncoderWriteTimestamp,
wgpuComputePassEncoderWriteTimestamp and wgpuRenderPassEncoderWriteTimestamp to the
matching JavaScript methods; it supplies no equivalent command-point lowering.
CommandRecordingOrder permits L2 Timestamp/PipelineStatistics only in Render/Compute
scopes. Existing V1 nine-method API observation remains unchanged and currently
Degraded (encoderWrite=false). It must not become complete native command authority
from method presence alone; any future pass-specific execution contract must consume
actual device feature enablement and a separate versioned caller extension opt-in,
as well as exact original point/pass/index/slot and native payload proof. This opt-in
must never enable a base/default capability or appear in applyTo/default descriptors.

Next implementation must define the exact original stream point, distinct active
pass ownership, slot recording reservation and revision-journal rollback before
emitting native work. For an extension profile, declare its stable default-closed,
version/opt-in/unavailable diagnostics, correctness/lifetime/budgets and four backend
outcomes before implementation. Standard pass-descriptor timestampWrites cannot
replace an arbitrary point. Missing admitted API/extension means deterministic
rejection; no native fixture/mock/CPU state completion substitutes for positive
native record/submit/map/publication evidence. Current original Query references,
recorded proof issuers2, public/default TimestampQueries and NativeSync stay closed.

## OriginalQueryCommandPoints/1 and OriginalQueryRecordReservation/1

Status: f4b4c1 Contract Complete / Prototype Complete CPU command points, verified
host/Null10/10, Wasm/browser5/5, inventory59/59, clean reports, Wasm304 metadata
bytes and hot original matching allocation0. Evidence original-query-command-points-*.
f4b4c2 Contract Complete / Prototype Complete CPU reservation only, verified
host/Null10/10, Wasm/browser5/5, inventory60/60; reports clean, Wasm464 metadata
bytes, hot reserve/validate/abandon allocation0. Exact duplicate exclusion,
rollback ABA, retained caller drop and actual creation Pending/Prepared/Ready
loss all pass. Evidence original-query-record-reservation-* YAML/log/XML.
Native original recording/pass/pipeline/completion remains f4b4c3. Explicit Prototype
opt-in, version1 and separate quotas are required; these objects only declare CPU
command points and reservations. Null validates CPU semantics; Vulkan/Metal/WebGPU
share the command-point metadata with DiagnosticFailure for native execution. The
selected WebGPU reservation retains real original query owners when available but
still has no native execution payload. Existing actual V1 admission stays Degraded.

Command points consume one sealed CommandVariableArguments object and complete
strictly ordered QueryPoolOriginalInputArguments coverage. Every point retains its
original ordinal, index/type/input owner, enclosing full pass interval and optional
latest pipeline-bind ordinal. CommandRecordingOrder validates every command, Finish
and debug depth; timestamp does not require a pipeline. Entire original stream is
retained, including variable payloads; equal-looking foreign streams are distinct.
Source retention counts bound commands, text, offsets and push bytes. Retained input
slot/scope/label budgets count each query reference (including repeats), with no
native resident-byte claim. Cold object/pass/point storage has its own CPU quota.
No original native pass/pipeline/descriptor ownership is inferred from CPU ordinals.

Reservation consumes immutable points and exactly one current original slot
observation per point in order. The original binding must match the point input;
duplicate exact native graph/index is rejected before mutation, while distinct
native owners created from one CPU snapshot remain distinct. One journal owns all
pending Record declarations. Full preflight precedes mutation; native calls are
absent. A cold independent reservation metadata quota pays stable entries and
journal ownership. Pending validation compares exact journal observations; normal
slot observations and pool reset reject while pending. Abandon/destruction rolls
back all entries, advances revisions and never revives old observations. Exhaustion
or invalid original owner/loss closes validation; terminally invalidated controls
stay invalid through rollback. There is no submit/complete/Available authority or
completion payload. Actual recording requires separate admitted native profile,
original pass/pipeline ownership, execution error scopes and completion budget.

Externally serialized operations are bounded by declared counts, allocate no hot
memory and perform no blocking wait or file IO. Native creation failures/loss and
API/extension absence fail deterministically; no boundary timestamp substitution,
experimental feature inference, CPU native proof or recorded-proof issuer is added.
Quick/Null/both browser paths cover ordered coverage, distinct passes, legal
pipeline absence, source quotas, exact native binding/duplicate-slot, rollback ABA,
retained drop and actual device loss. RenderDoc unavailability remains a diagnostic;
these phases change no visual output and grant no Backend/Platform/Production status.

2026-10-03 f4b4c3 continuation review: a stale final observation in a distinct
original native graph now proves full preflight leaves earlier slots and revision1
unchanged. Final regressions remain host/Null10/10, Wasm/browser5/5, inventory60/60,
clean reports; updated original-query-record-reservation evidence is archived.
Generic control exhaustion is tested; multi-slot reserve partial-exhaustion rollback
has source evidence only, with no injected UINT64_MAX slot graph test. Native active
pass and any actually bound pipeline (timestamp may have no pipeline) remain a
separate prerequisite. Real adapter/API extension unavailability cannot be bypassed
by closed descriptor/record/submission subsets, CPU reservation or successful
creation scopes. No parent generic/public/Platform/Production closure is granted.

Original committed Buffer control /1 verified 2026-10-04: host/Null10/10,
Wasm/browser6/6, inventory109/109. Both originalBufferStatePassed and prior graph
flags true, diagnostics/nativeErrors empty. Null/Vulkan/Metal original Buffer
creation/write boundaries use shared versioned control; WebGPU binds its existing
control only with actual native Buffer and device. Sealed weak observations bind
exact canonical registration/generation/control and committed state/revision.
Pending/foreign/stale/unregistered/dead/retired/illegal-state negatives, original
journal preparation/no-write commit/rollback ABA/destruction detach and exhausted
hot allocation0 pass on actual Null/WebGPU resources. Metal compiles but test
exit0 is availability-gated, with no fixture marker; Vulkan target/SDK absent.
Evidence original-buffer-state-* YAML/log/XML. Contract Complete / Prototype
Complete committed Buffer scope only. Exact frozen Source/input binding, combined
registry/record owner union, Texture ranges, retained pending transaction, native
loss/completion and full execution remain pending. Four full state/generic gates
remain closed; no Backend Validated, Platform Validated or Production Ready.
RenderDoc diagnostic: RHI-RENDERDOC-TOOL-UNAVAILABLE.

Original committed Buffer input graph /1 verified 2026-10-04: host/Null10/10,
Wasm/browser6/6, inventory110/110. Both originalBufferStatesPassed and all prior
flags true, reports clean. The graph retains exact original input shared ownership,
canonical device and every canonical Buffer committed registration/control/revision.
Source10+records4+fixed3=17; Buffer input9+root1+records2+fixed3=15; repeated upload
input11+root1+records2+fixed3=17, with no repeated GPU record charge. Independent
observation/registration/owner/exact state quota, forged/equal-fact foreign graph
and device, unsupported Texture sources, pending/stale/rollback ABA/commit changes,
caller-drop/shutdown and exhausted hot allocation0 pass. Cold exact/one-less/
exhausted tests pass. Initial accessor compile and fixture peak-order failures are
preserved and repaired. Metal compiles but availability-gated fixture did not run;
Vulkan target/SDK absent. Evidence original-buffer-states-* YAML/log/XML.
Contract Complete / Prototype Complete committed Buffer graph scope only.
Normalized Texture controls/ranges, retained pending transaction, native loss,
completion and full execution remain pending; all four full state/generic/native
capability gates stay closed. No Backend Validated, Platform Validated or Production
Ready promotion. RenderDoc: RHI-RENDERDOC-TOOL-UNAVAILABLE.

Original Texture state range geometry /1 verified 2026-10-04: host/Null10/10,
Wasm/browser6/6, inventory111/111. Both originalTextureStateRangePassed and all
prior flags true, reports clean. Reusable core checked row-major grid plus existing
format/layout/aspect validators derive exact canonical mip/layer/aspect cells.
Overlapping ranges share cells; Depth/Stencil are independent; cube faces are
counted once and 3D depth does not create array layers. Empty/overflow/unknown-format/
aspect/range/cell-capacity checks and exhausted hot allocation0 pass. Initial
fixture immutable assignment/BC7 enum build failures preserved and repaired.
Metal compiles but actual fixture availability-gated; Vulkan target/SDK absent.
Evidence original-texture-state-range-* YAML/log/XML. Contract Complete geometry
only; normalized original Texture controls, registration observations and frozen
input graph binding remain pending. Four full original-state/generic issuers stay
closed and WebGPU partial barrier gate unchanged. Creation-state mapping audit
requires original Null/Metal descriptor state, Vulkan Undefined/native-acquired
state and existing WebGPU payload control, never a second control seeded from
state(). No Backend Validated, Platform Validated or Production Ready promotion.
RenderDoc: RHI-RENDERDOC-TOOL-UNAVAILABLE.

Original normalized Texture control /1 verified 2026-10-04: host/Null10/10 plus
extra5/5, Wasm/browser6/6, inventory112/112; both originalTextureControlPassed and
required prior flags true, reports clean. Actual normalized Null/Vulkan/Metal
creation/write boundaries and existing actual WebGPU uniform native payload control
are bound to sealed weak factory registration/control/range/revision observations.
Core VersionedMutationArray allocates exact descriptor-derived states and equal
snapshot once; original journal, mixed/overlapping/independent logical planes,
rollback ABA, no-write commit/conflicts, destruction/retirement, cold exact-less-
exhausted ledger rollback and hot allocation0 pass. Any write conservatively
invalidates all ranges. WebGPU partial and independent Depth24Stencil8 observations
reject. Metal compiles, actual fixture availability-gated; Vulkan pinned official
v1.4.350 header syntax passes only. Initial return-type and diagnostic assertions
repaired with logs retained. Evidence original-texture-control-* YAML/log/XML.
Contract Complete / Prototype Complete logical control only; graph binding,
pending transactions/native loss/completion/execution remain pending. No Backend
Validated, Platform Validated or Production Ready promotion. Full issuers4 remain
closed; RenderDoc: RHI-RENDERDOC-TOOL-UNAVAILABLE. Continue exact frozen Texture/View
range/input ownership binding.

Original committed resource input graph /1 verified 2026-10-04: host/Null10/10
plus extra5/5, Wasm/browser6/6, inventory113/113. Both originalResourceStatesPassed
and required prior flags true, reports clean. Ordered allocation-free shared RHI
projection binds explicit copy declarations (including exact full-mip owner),
barriers, whole memory/alias/retain intents, exact view-parent ranges and original
upload/readback inputs to original registered controls. Mixed Buffer1/ranges5 graph
owns input16+root1+records4+fixed3=24; whole intent graph22; repeated upload15 and
readback21. Independent Source/state range/observation, registrations, ownership
and CPU domains, cold exact-less-exhausted, foreign/equal-input/forged owners,
invalid/partial/view mutation, pending/stale/rollback ABA/commit/caller-drop/shutdown
and exhausted hot allocation0 pass. Fixture initialization and peak-measurement
order failures repaired with original logs/XML retained. Metal compiles but actual
fixture availability-gated; pinned Vulkan header syntax only. Evidence original-
resource-states-* YAML/log/XML. Contract Complete / Prototype Complete committed
logical graph only; copy geometry, native plane access, retained pending journals,
native loss/completion and complete execution remain independent obligations.
Four full issuers/generic/bindless/NativeSync remain closed. No Backend Validated,
Platform Validated or Production Ready promotion. RenderDoc tool unavailable.
Continue exact original device native-loss binding; audit shows private WebGPU
loss subscriptions and Vulkan/Metal completion errors cannot prove canonical
public L2 domain invalidation.

Original L2 device native-loss subscription /1 verified 2026-10-04: host/Null10/10
plus extra5/5, Wasm/browser6/6, inventory115/115; both originalDeviceNativeLossPassed
and required prior flags true, clean reports. Sealed weak original receiver,
NativeLost terminal epoch, independent explicit registration1/callback1/owner1
and exact CPU quota, before-registry one-shot binding and backend-owned native
source are implemented. Actual WebGPU subscription cold peak108 exact-less-
exhausted/retry/failure compensation, changed quota/count/owner/detach/destruction
and factory parent native-owner negative gate pass. Real unrelated device loss
leaves the original domain active. Actual browser GPUDevice.destroy/lost revokes
the same original domain, Buffer/Texture/View registrations, committed observations
and pending graph owner20; rollback cannot revive authority. Failed/detached/
destroyed subscriptions safely handle late delivery; six quotas finally0. Hot
validation/detach/rollback allocate0 and delivery code has no allocation. Initial
legacy emdawn header, immutable fixture and ownership split failures repaired;
original logs preserved. Vulkan pinned header syntax only; canonical native binding
is explicit DiagnosticFailure. Metal compiles/availability-gated, binding explicit
DiagnosticFailure; Null ValidatorOnly rejects native binding. WebGPU exact native
subscription has actual browser evidence, without broader Backend Validated,
Platform Validated or Production Ready promotion. Full original/generic/bindless/
NativeSync gates remain closed. Evidence original-device-native-loss-* YAML/log/XML.
RenderDoc tool unavailable. Continue one-shot original registered control/domain
binding and terminal active/prepare/write/pending observation gates; complete
retained transaction quarantine/completion/execution remains pending.
