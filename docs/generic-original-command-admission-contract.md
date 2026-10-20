# GenericOriginalCommandAdmission/1

Status: Contract Complete for L2 admission requirements, budget intent and closed
lowering only. Complete retained graph, backend execution, Platform Validated and
Production Ready remain pending. The existing selected
TextureReadbackOwnedPreparation/1 is an independent explicitly configured
Prototype; its success does not implement this contract.

## Scope and reusable boundaries

This contract governs admission of a complete immutable original-source command
stream into public generic recording. No public executable facade is provided
until the exact source, input, resource-state and completion graph can be retained
and consumed. `OriginalCommandAdmissionBudget` is value intent only. Its validation
does not validate a resource, reserve memory, authorize recording, prove native
completion, or change `DeviceCapabilities`.

Reuse `CommandUsageContract` for positional reference signatures,
`CommandInputContract` for positional input families, `CommandScalarArguments` and
`CommandVariableArguments` for immutable scalar/variable fields,
`RenderPassArguments`, `TransferCopyArguments`, `ResourceDependencyArguments`,
`BufferUploadArguments`, `TextureUploadOriginalInputArguments` and
`TextureReadbackOriginalInputArguments` for their existing field contracts. Reuse
`CommandRecordingOrder` for scope/order. A classifier result of Scalar for a query,
bundle, producer, processor or bindless operation grants no original-source or
native execution proof. LegacySizeOnlyBufferCopy, Count and unknown kinds reject.

Reuse core bounded sequences, retention, owner projections, original quota-bound
shared identity and mutation journals. Reuse `ColdOwnedObjectSlot/1` for original
unique object adoption. A slot address comparison is not provenance; L2 must
recheck stable non-recycled identity and descriptor semantics. Never reconstruct a
wrapper from an ID, clone a native payload, derive generation from a state revision,
or retain an arbitrary caller `shared_ptr<void>` as source authority.

## Original source requirements

### Bindless snapshot internal buffer ownership bridge/1

Sealed snapshots may explicitly prepare their original metadata, fault-latch and
fault-readback buffers as existing `OriginalCommandBufferLease/1` sources. This
is cold, externally serialized work, not snapshot publication or GPU execution.
The genuine core-shared snapshot owner, live descriptor allocation, exact logical
device registrations and exact Source quota owner must be validated. Allocate
all lease custody and preflight all children before moving any original UniquePtr;
failure preserves all children, descriptor contents, write seals and budgets.
The snapshot retains those same leases and its existing borrowed buffer API must
continue to name the same wrappers, IDs, layouts and access declarations.
Repeated preparation is allocation-free and accepts only the original quota
owner. Absent optional children remain absent; no placeholder is created. Adopted
children retain the exact count/resident capacity leases and their original
budget owners. The canonical CPU-owner projection deduplicates these shared
accounting controls; Source metadata still charges its independent exact quota.

The returned leases enter the existing canonical Buffer inventory and participate
in its liveness, registry, layout, owner and quota validation. They may outlive
the snapshot as authentic buffer owners, but do not retain or authorize its
descriptor table, generation, package or fault domain. Snapshot count/resident
charges retire only after the last snapshot or original child source releases
the original reservation, never while a canonical source still owns storage.
BindlessRoot must separately retain the exact snapshot and authorization, prove
all external descriptor dependencies and join per-use accesses/state/completion.

| Backend | Internal CPU buffer-source mapping | Generic execution |
| --- | --- | --- |
| Null | Equivalent original lease adoption and command validator | Validator only |
| Vulkan | Equivalent original lease adoption, subject to genuine factory registration | DiagnosticFailure until complete original command graph |
| Metal | Equivalent original lease adoption, subject to genuine factory registration | DiagnosticFailure until complete original command graph |
| WebGPU | Equivalent original lease adoption, subject to genuine factory registration | DiagnosticFailure until complete original command graph |

No bindless or NativeSync capability is enabled by this bridge. Unregistered or
foreign children, stale allocation, wrong owner/quota, disabled opt-in and quota
exhaustion reject before adoption. Negative hot validation uses stable diagnostics
and must not allocate a temporary ValidationResult string.

Each ordered reference selects a typed retained source and explicit element or
subresource identity. Repeated references must resolve to the same original owner,
logical device registry and stable identity. Equal IDs, labels, content hashes,
native device handles or physical devices do not prove that relationship.

| L2 domain | Original ownership and required binding |
| --- | --- |
| Buffer | Adopt exact core UniquePtr<GpuBuffer>; non-recycled resource ID and complete physical descriptor; registry/liveness, write seals and state separately observed |
| Texture | Adopt exact core UniquePtr<GpuTexture>; non-recycled ID and TextureResourceLayout; retain original state/lifetime and exact mip/layer/aspect coverage |
| TextureView | Adopt exact GpuTextureView and retain its original parent Texture source; exact view ID, parent ID, view descriptor and subresource closure |
| Sampler | Adopt exact GpuSampler with full descriptor and registry binding; explicitly state-free, never reinterpret a missing texture/buffer as a sampler |
| Pipeline | Retain exact core-shared RenderPipeline or ComputePipeline with SharedObjectIdentity owner; reflection, layout and complete immutable pipeline binding, never synthesize a resource ID |
| Descriptor | Adopt original DescriptorSet plus original arena allocation/lifetime and content revision; retain full written binding/array-element resource closure; revalidate generation, writes and dynamic offsets |
| DescriptorTable | Adopt original DescriptorTable plus original arena generation/content revision and exact indexed binding closure |
| Upload | Retain quota-bound frozen variable/pixel/byte input, exact layout and source ownership; BufferUpload source and destination remain separately named |
| Readback | Retain original Request/layout/source lease, destination and completion/mapping/publication owners; output correlation is original Request/snapshot ownership, never just a caller label or requestId |
| Query | Retain original prepared pool/slots/generation/reservation, exact command point and result owner; metadata/scalar capture does not create or record a query |
| BindlessRoot | Retain approved original snapshot/root/authorization/resource closure and generation; per-use access proof, fault domain and command scope required |
| IndirectProducer | Retain approved original producer/output and exact write/access/command proof; no inferred native dispatch from scalar capture |
| IndirectProcessor | Retain approved original processor/input/output/fault owners with exact producer/consumer/access binding |
| RenderBundle | Adopt original bundle and complete recorded source closure; exact attachment/pipeline compatibility and native lowering; CpuReplay requires its independent explicit versioned Prototype contract |
| SecondaryCommandBuffer | Adopt original command buffer and complete immutable command/source closure; consuming scope, compatibility and completion owners retained |

Unsupported typed source adapters fail before ownership transfer or any native
work. These requirements do not assert that the adapters already exist. In
particular the initial Buffer/Texture CPU lease prerequisite cannot admit views,
descriptors, queries or bindless sources by converting them to a generic pointer.

## Complete graph invariants

The frozen plan contains one ordered CommandUsage stream, exact positional typed
source references, one scalar/variable record per command and every required
typed descriptor. References are consumed exactly once in positional order;
missing, extra, out-of-range, aliased or foreign owners reject. Bindings are
matched to the original source rather than a convenient equal field copy.
Render-pass attachment order, resolves, formats, load/store/clear, depth/stencil,
subpasses and layered intent remain fully explicit. Draw/dispatch additionally
prove bound pipeline/descriptor/vertex/index state and access declarations.

Resource observations are complete for every stateful retained resource,
including descriptor, upload/readback, indirect and nested-command closures.
State identity, current revision, pending ownership, transition ranges,
stage/access and memory intent are distinct facts. No whole-resource expansion
of a partial intent is permitted. Samplers are the only explicitly state-free
resource family. Shared controls are deduplicated through core identity projection;
one identity with different owners/domains/controls rejects. The immutable graph
and all original registry bindings are revalidated before final admission.

Preparation is synchronous cold work. All fallible graph/source/native staging,
callback registration and submission preparation precedes original adoption or
hot execution. Rejected preparation preserves the caller's candidate UniquePtr,
original quota reservations, pending slots and existing graph. A multi-source
adoption preflights every candidate before committing any move. Failure after an
already issued native callback retains exact native owners until callback drain
or actual loss; it cannot report admission success or return a Dummy payload.

The accepted frozen plan retains the exact original owners. A recording ticket
consumes that plan once and compares incoming public commands and fields against
it before native recording. Failure invalidates further recording. State changes
use the existing owned transaction/journal rather than eager public mutations.
Finish must consume the complete stream; it is not submission or completion.

Submission preflights the exact sealed recording, original state transaction,
completion ticket and queue ownership. A real ordered queue completion plus
required native error scopes is mandatory for state commit and completion output.
No CPU signal, host no-op wait, empty payload or metadata completion substitutes.
Native callback issuers remain backend-private; WebGPU proof issuer friendship
remains exactly WebGpuOrdinaryRecording and WebGpuOrdinarySubmission.

Abandonment before native issuance releases original owners deterministically.
Cancellation/drop after issuance suppresses publication and retains owners through
the complete original callback drain. Device loss uses original registration and
native loss evidence; a host shutdown flag cannot manufacture loss. Partial,
duplicate, reversed or late callbacks cannot recycle an active ticket or publish
a peer's result. Quarantined submissions remain owned until actual safe retirement.
Submitted resources cannot be released on a metadata-only completion claim.

## Independent bounds and quota ownership

Every field defaults to zero; zero disables that category and never implies an
unbounded/default allocation. `maxCommands`, `maxGraphOwners` and Graph CPU bytes
are required even for a Finish-only declaration. Per-domain `maxSourceOwners`
counts distinct typed source owners; references and graph owners are independently
bounded. The plan measures actual allocator charges; source/command counts cannot
be converted into guessed CPU or native bytes.

Source, Graph, Recording, Submission, Callback and Result CPU quotas are independent
exact original core quota owners, each with its declared limit. Source adapters,
frozen graph, recording state journals, pending submission owners, callback tickets
and output allocations charge their corresponding domain. Sharing a physical
device never shares logical plan/quota authority. Existing resource allocation
ownership stays with the adopted wrapper; source metadata does not silently
recharge or forgive that allocation. Cross-domain quota substitution rejects.

Commands, references, graph owners, input descriptors, state observations,
transactions, debug depth, dynamic offsets, mip mappings and pending submissions
have explicit independent capacities. Labels, push bytes, retained upload bytes
and unpublished result bytes are separately bounded. Callback tickets and native
callback registrations have separate capacities; canceled partial callbacks still
charge them until drain. Nested sources consume the complete transitive closure.

Native payload object count, staging bytes and transient bytes are independent.
Native bytes derive from exact native requirement queries or a versioned native
contract, never size estimates, format guesses or caller count multipliers.
The aggregate original device owner charges concurrent and retained preparations,
not just submitted requests. Failure cannot bypass a cap by temporary allocation.
Completed output retaining a resource/quota continues charging its declared owner.

Hot admission/record/submit/callback/poll/cancel does allocation0, no blocking
wait, file/network IO, per-draw pipeline creation or per-frame shader compilation.
All needed arrays, journals, submission records, native payloads and callbacks
are reserved in explicit cold preparation. Retry requires the original still-live
owner and revalidation; released capacity alone does not restore stale authority.

## Four-backend lowering version 1

The following decisions describe the full public generic bridge, independently
of narrower already tested backend-private execution languages. The executor must
consume this decision; neither capability booleans nor successful CPU declarations
can select another path. No full generic execution decision is currently available.

| Backend | Disposition | Stable diagnostic | Public execution |
| --- | --- | --- | --- |
| Null | ValidatorOnly, CPU structural/ownership tests only | RHI-ORIGINAL-COMMAND-NULL-VALIDATOR-ONLY | Closed; no fake GPU completion |
| Vulkan | DiagnosticFailure, full retained original bridge missing | RHI-ORIGINAL-COMMAND-VULKAN-UNIMPLEMENTED | Closed |
| Metal | DiagnosticFailure, full retained original bridge missing | RHI-ORIGINAL-COMMAND-METAL-UNIMPLEMENTED | Closed |
| WebGPU | DiagnosticFailure, full retained original bridge missing | RHI-ORIGINAL-COMMAND-WEBGPU-UNIMPLEMENTED | Closed; selected owned readback Prototype independent |
| Unknown backend | DiagnosticFailure | RHI-ORIGINAL-COMMAND-BACKEND-UNKNOWN | Closed |

Malformed version/opt-in/budget intent diagnoses before backend lowering. Valid
intent still receives the closed backend diagnostic. No ExtensionOptIn or base
command-retention/bindless/NativeSync capability is automatically enabled.

## Required evidence and remaining work

Fast tests cover all domains and input kinds, malformed and independent budget
intent, four closed decisions, unsupported enums and allocation0. Null hidden
frame and actual Wasm/browser regression paths must stay green at each stage.
Follow with typed CPU source lease/adoption tests, exact frozen complete graph
tests and genuine backend-private source/recording/submission/callback payload
evidence, including native-call0 forbidden paths. CPU declaration success must
never be reported as Backend Validated.

Backend Validated is per real backend payload. Platform Validated additionally
requires the target platform smoke, including mobile pause/resume, surface/layer,
device/surface loss and resource/budget retirement. Visible output acceptance
requires AI RenderDoc capture analysis; unavailable RenderDoc is recorded as
RHI-RENDERDOC-TOOL-UNAVAILABLE and does not satisfy visual validation. Production
Ready requires complete four-backend contracts, required platform evidence and
hot-path proof. All those broader gates remain pending.

Verified 2026-10-04: host/Null10/10, Wasm/browser6/6, inventory91/91; both
actual browser reports require genericOriginalAdmissionContractPassed=true and
remain clean. Tests cover all15 domains, full input vocabulary, independent CPU
budget negatives, four closed decisions and allocation0. Evidence
`generic-original-admission-*` YAML/log/XML. Typed retained source/graph and
backend execution are pending; no broader capability or production promotion.

## OriginalCommandResourceLease/1 Buffer/Texture prerequisite

Contract Complete / Prototype Complete typed CPU lifetime prerequisite only.
Only GpuBuffer and GpuTexture are admitted to typed CPU
ownership storage. A quota-bound original lease freezes the exact observed
nonzero ID and full physical descriptor (`BufferResourceLayout` or existing
`TextureResourceLayout`). Labels remain debug metadata owned by the adopted
wrapper. Physical layout capture/comparison does not validate creation legality,
registry membership, current state, native allocation or content generation.

Cold preparation binds one caller-owned core UniquePtr without taking ownership.
Hot validation requires that exact still-owned candidate, original quota-bound
lease/control block, unchanged source metadata quota limit and frozen ID/layout.
Failure preserves the candidate. Final adoption uses `ColdOwnedObjectSlot/1`,
allocation0 and original deleter/quota; resource allocation is not moved into the
source metadata quota. Original adopted objects stay alive with the lease and
physical mutation continues to reject. Dead cold candidates are never dereferenced.

Per-lease metadata quota and retained lease/wrapper owner count are explicit.
This source budget is a separate prerequisite; the full graph must bind it to
its original Source domain and per-type owner count without inferred sizing.
Neither a caller-defined wrapper with a matching ID nor a CPU lease can prove
ResourceIdentity/1 engine origin. The future backend observer must verify this
exact wrapper in the original logical registry and retain exact native/state
observations before any recording. Unsupported types do not instantiate this
lease and full generic four-backend execution remains closed.

Verified 2026-10-04: host/Null10/10, Wasm/browser6/6, inventory92/92; both
actual browser genericOriginalResourceLeasePassed=true, reports clean. Actual
Null/WebGPU wrappers preserve original CPU allocation/quota through adoption;
physical mutations, forged/foreign owners, changed quota limit, dead candidate,
cold exact/one-less/exhausted and two-domain hot-exhausted allocation0 pass.
Evidence `generic-original-resource-lease-*` YAML/log/XML. CPU source lifetime only;
this does not constitute generic Backend Validated or registry/state/native proof.

## OriginalCommandResourceSources/1 immutable typed inventory

Contract Complete / Prototype Complete typed CPU inventory only. The source table retains only approved original Buffer and
Texture leases, using a typed variant; it cannot accept arbitrary erased owners.
Each lease must have its canonical quota-bound owner and be fully adopted and
unchanged. All leases and table metadata must use the exact same original Source
quota. Per-domain source counts are independent; exceeding a disabled category,
duplicate original table entry, forged owner or foreign quota rejects before table
allocation. Core SharedIdentityProjection checks stable key/domain/owner identity;
the table does not implement another local deduplication algorithm.

The caller's ordered source table is copied into bounded core storage under the
Source quota, with capacity equal to the validated explicit table count. Destroying
caller arrays/lease references preserves the original adopted wrappers. Hot
revalidation checks the complete retained table and allocates0 under quota pressure.
The immutable original budget remains attached. This is a CPU source inventory,
not the full command reference/input/state/completion plan, and grants no original
registry or native execution proof on any backend.

Verified 2026-10-04: host/Null10/10, Wasm/browser6/6, inventory93/93; both
actual browser genericOriginalResourceSourcesPassed=true, reports clean. Typed
canonical ownership, independent domain bounds, duplicates/forged/foreign quota,
cold exact/one-less/exhausted rollback, caller-drop lifetime and mutation/hot-
exhausted allocation0 pass. Evidence `generic-original-resource-sources-*` YAML/log/XML.
Full command/input/state/completion graph and generic execution remain pending.

## OriginalCommandUsage/1 frozen ownership sequence

Contract Complete / Prototype Complete CPU ownership sequence only. The usage root retains the exact original typed source table,
freezes caller command/reference arrays in bounded core storage and derives all
limits from that table's immutable original budget. Source and Graph quotas must
have distinct control blocks, even with equal byte limits. Graph owner capacity
counts this root, source table and each source lease's explicitly declared retained
owner closure; it never estimates bytes from those counts.

Every reference names an explicit original source index. `CommandUsageSequenceContract`
shares the existing WebGPU contiguous span/signature validation through core
OrderedSpanCursor; WebGPU supplies its original stable diagnostic labels. Finish
is exactly last, every positional reference is consumed, invalid ranges reject
before indexing and source domains satisfy the existing command signature. The
L2 usage also reuses CommandRecordingOrder for scopes/debug nesting. Missing
pipeline/query/bundle/bindless/producer/processor sources cannot satisfy signatures
by scalar classification. No full input/access/state semantics are inferred.

Incoming Buffer/Texture references compare exact retained wrapper addresses and
validated frozen identity/layout, with no erased caller owners. Caller array
mutation cannot change the frozen graph. Hot revalidation and comparison allocate0.
Execution remains closed on all four backends; input, registry/state/native
recording/submission/completion ownership must still be supplied independently.

Verified 2026-10-04: host/Null10/10, Wasm/browser6/6, inventory94/94; both
actual browser genericOriginalUsagePassed=true, reports clean. Preserved WebGPU
sequence diagnostics, exact source/ref binding, frozen arrays, independent quota/
owner/command/reference bounds, forged/foreign and forbidden-family rejection,
cold exact/one-less/exhausted rollback and hot allocation0 pass. Evidence
`generic-original-usage-*` YAML/log/XML. Full original inputs/state/completion
and backend execution remain pending.

## OriginalCommandScalars/1 original field owner binding

Contract Complete / Prototype Complete original CPU field binding only. The scalar root retains the exact canonical usage and original
quota-bound CommandVariableArguments; both and the scalar root must use the same
original Graph quota control block. Actual stored text, offset and push categories
must fit the immutable graph budget, even when a separate local argument-store
budget allowed more. CommandVariableArguments supplies shared count/kind binding
for both this L2 root and existing WebGPU frozen scalars; WebGPU preserves its
stable diagnostics and independent native pipeline/descriptor binding checks.

The usage caches its validated exact ownership-node count. The scalar root adds exactly
itself and the original argument store, charged to the independent graph owner
capacity; overflow or insufficient capacity rejects. Exact scalar/variable
comparison and revalidation allocate0 under Source/Graph quota pressure. Dropping
caller argument references preserves their original store until final retirement.
This is a field ownership binding; full typed descriptor, source registry/state/
access and native recording/completion remain mandatory independent prerequisites.

Verified 2026-10-04: host/Null10/10, Wasm/browser6/6, inventory95/95; both
actual browser genericOriginalScalarsPassed=true, reports clean. Exact canonical
usage/store and Graph quota, ownership capacity, all four actual variable-category
bounds, forged/foreign count/kind/input, caller store drop and hot Source/Graph
exhausted allocation0 pass. Existing WebGPU diagnostic order is preserved. Initial
nested fixture high-water measurement followed an outer exhausted probe; test
ordering repaired and failure logs retained, with no production budget relaxation.
Evidence `generic-original-scalars-*` YAML/log/XML. Full typed input/state/completion
and backend execution remain pending.

## OriginalCommandInputs/1 positional CPU binding

Contract Complete / Prototype Complete for scalar, single-mip copy and dependency
field ownership only. The original input root retains exact canonical scalars,
usage and typed sources under the same original Graph quota. Sparse descriptor
indices and kinds consume the ordered command stream exactly. Four copy kinds
and five dependency kinds preserve every scalar field; source/destination and
alias operands bind to exact positional source IDs. Incoming typed Buffer/Texture
references must match the original retained wrappers, independently of ID fields.
Descriptor capacity and the one added root ownership node are bounded separately.

The descriptor vocabulary also includes canonical RenderPassArguments and full
texture upload/readback original input owners. Missing view/parent, staging,
generation and readback source adapters reject before input allocation; those
representations cannot open admission. Explicit copy mip layouts currently reject
at capture rather than being truncated. Transfer legality, logical registry/state/
access, native recording and completion remain independent pending contracts.
Shared CommandInputContract sparse descriptor consumption and dependency operand
accessors preserve the existing WebGPU per-command diagnostic order.

Verified 2026-10-04: host/Null10/10, Wasm/browser6/6, inventory96/96; both
actual browser genericOriginalInputsPassed=true, reports clean. Full stored field
mutations, exact incoming wrappers, owner/descriptor bounds, forged/foreign and
order/kind/source rejection, cold exact/one-less/exhausted rollback and caller-drop/
hot-exhausted allocation0 pass. Evidence `generic-original-inputs-*` YAML/log/XML.
Four generic execution gates remain closed; no Backend/Platform/Production claim.

## TransferCopyInputSnapshot/1 complete copy field contract

The complete frozen copy input retains the original scalar fields and every
explicit mip in caller order under one canonical Graph quota owner. The shared
TextureCopyMipLayoutArguments stores mip, extent, row pitch, image rows and buffer
offset; texture-to-texture mappings contain only their specified mip/extent.
There is no inferred layout, dropped mapping, guessed geometry or native payload.
The legacy pointer-free TransferCopyArguments capture still rejects explicit mips.
Its explicitly named scalar projection may only be consumed with the complete
snapshot's retained mapping and original owner.

The input root must bind snapshot source/destination IDs to positional typed
resources, compare incoming complete copy fields and retain the exact canonical
snapshot owner under its original Graph quota. Each command's mapping counts
against the independent full-graph maxMipMappings, even when two commands retain
the same snapshot. Core SharedIdentityProjection counts each distinct validated
live snapshot owner once against maxGraphOwners. Its object key only deduplicates
retained CPU nodes and never proves GPU registry membership or provenance. A
foreign/forged snapshot, quota mismatch, insufficient owner/mapping budget or
changed original mip scalar/order/count rejects deterministically.

Null remains ValidatorOnly; Vulkan, Metal and WebGPU retain DiagnosticFailure for
full generic execution. Complete CPU copy capture does not validate geometry,
format/ranges, memory/state/access or backend row-pitch rules, and cannot expose
recording/submission/completion authority. Source/Graph hot validation/comparison
must allocate0. Cold failure releases every metadata charge; original snapshots
live until the final canonical input graph retires. Backend/Platform/Production
validation remains pending.

Verified 2026-10-04: host/Null10/10, Wasm/browser6/6, inventory97/97; both
actual browser genericOriginalCopyMipsPassed=true and all preceding original
graph markers remain true; reports clean. Per-snapshot and aggregate owner/mip
limits, all original mip field/order/count mutations, foreign/forged quota,
reused snapshot dedup, wrong/null incoming copy type, cold exact/one-less/exhausted
rollback and complete caller-drop/hot-exhausted allocation0 pass. Evidence
`generic-original-copy-mips-*` YAML/log/XML. Contract Complete / Prototype Complete
CPU full-copy fields only; transfer legality/state/native execution remains unproven.
The earlier explicit-mip rejection limitation applies to pointer-free capture only;
complete original snapshots now retain those fields without truncation.

## Original TextureView source prerequisite

Before pass admission, a typed view source must retain the exact original
quota-bound view lease and its exact canonical original texture parent lease.
The complete physical TextureViewDesc fields and observed view.textureId must
remain unchanged; matching IDs are declarations, never registry/native proof.
Both leases and the view-source root must share the original Source quota.
The view-source has three exclusive nodes (source root, view lease and wrapper)
and retains its parent's two-node closure, independently bounded at five.
The immutable source table must contain that exact canonical parent lease once,
so graph counting includes the parent through its own table entry, without
duplicating it in the view source contribution. Missing or substituted parents,
forged/foreign leases, quota mixing or physical mutations reject.

Reuse the existing generic original resource lease and core cold ownership slot
for view wrapper capture/adoption; the view-parent binder must not clone a view,
reparent it, adopt the parent's texture a second time, or infer engine origin.
Failure to prepare a view-parent binder preserves caller-owned canonical leases.
All metadata goes through core allocation and explicit cold quota. Source
validation and incoming typed view comparison allocate0. Pass input attachment/
resolve/format/subpass binding and logical registry/state remain separate.
Null remains ValidatorOnly; Vulkan/Metal/WebGPU remain DiagnosticFailure for
full generic execution. No native view/pipeline or completion payload is granted.

Verified 2026-10-04: host/Null10/10, Wasm/browser6/6, inventory98/98; both
actual browser genericOriginalViewSourcePassed=true and all preceding graph
markers remain clean. Actual Null/WebGPU wrappers reuse the original generic
lease/core cold slot and preserve original CPU charge; complete physical view
fields, exact canonical parent/quota and required parent table entry pass. Graph
owner count7 includes parent2 and exclusive view-source3 exactly once. Missing/
substituted parent, forged/foreign quota, per-view bounds, cold pressure and
caller-drop/hot-exhausted allocation0 pass. Full pass input still rejects at the
stable adapter gate. Evidence `generic-original-view-source-*` YAML/log/XML.
Contract Complete / Prototype Complete CPU view-parent lifetime only; registry/
state/native/Platform/Production evidence remains pending.

## Original canonical pass input binding

Each BeginRenderPass descriptor must retain a canonical RenderPassArguments owner
under the exact original Graph quota. Color, resolve and depth references bind
in their specified order to canonical retained TextureView sources and their exact
original texture parents; no texture ID, raw pointer or equal descriptor can
replace an original view owner. The shared positional sequence must preserve the
existing WebGPU reference/view diagnostic order. Format Unknown in an attachment
retains its explicit L2 value and binds against the exact observed view format;
an unknown observed view format is rejected. No native format default is chosen.

The graph budget adds independent maxPassColorAttachments, maxPassAttachmentReferences,
maxPassSubpasses, maxPassAttachmentIndices and maxPassDependencies. Each pass
command charges its exact referenced descriptor categories, including repeated
references to a canonical pass; text charges all target/pass/color/depth labels
per pass plus actual stored command-variable text against maxLabelBytes. Larger
local snapshot bounds cannot bypass these frozen full-graph limits. Distinct
canonical pass owners count once through core SharedIdentityProjection, independently
of descriptor positions or metadata bytes. Zero disables every new category.

Full load/store/clear, resolve, depth/stencil, layered, subpass/dependency/index
and label fields remain immutable and compare exactly on incoming input. Shared
capacity consumption must preserve the prior local RenderPassArguments diagnostics
and leave a caller's remaining budget unchanged on failure. Source metadata
mutation, foreign/forged snapshots, reference count/order and quota substitution
reject before allocation or native work. Preparation failure retires all new
charges; caller snapshot/source drops preserve the original complete graph.

This stage binds CPU fields and typed ownership only. It neither validates
registry origin, state/access, pipeline compatibility, attachment geometry or
MSAA lowering, nor grants layered/subpass/resolve native execution. Null remains
ValidatorOnly; Vulkan/Metal/WebGPU remain DiagnosticFailure for the full generic
bridge. NativeSync and all generic/tracked/bindless capabilities stay closed.
Backend/Platform/Production and RenderDoc visual validation remain separate.

Verified 2026-10-04: host/Null10/10, Wasm/browser6/6 after one targeted
Chrome profile-cleanup retry, inventory99/99; both actual browser
genericOriginalPassInputsPassed=true and all preceding graph markers remain
clean. Shared positional sequence/failure-preserving capacities, exact Graph
snapshot/view/parent fields, eight aggregate one-less bounds, distinct-owner
dedup, cold rollback and caller-drop/hot-exhausted allocation0 pass. Initial
harness errno66 and retry log/XML preserved; the failing runner already produced
a successful native report. No production budget/gate/test relaxation. Evidence
`generic-original-pass-inputs-*` YAML/log/XML. Contract Complete / Prototype
Complete CPU pass ownership/field binding only; native legality/state/pipeline/
completion, Backend/Platform/Production and visual validation remain pending.

## Original upload input quota provenance prerequisite

A complete canonical TextureUploadInputSnapshot must retain the exact original
pixel allocation quota as a factory-bound owner, independently of metadata and
label quotas. Its original per-category budget remains immutable; every quota
limit must remain equal to that original declared limit. The complete label
store is canonical and quota-bound, with its one TextureUpload record and all
actual variable categories validated. Frozen pixel byte count, label count and
explicit mip count must continue to match their original bounds and scalar fields.

Capture/field comparison must revalidate the canonical snapshot, label owner and
all three original quota domains; a changed quota limit or forged owner cannot
become valid by copying an equal descriptor. Exposing the exact pixel quota avoids
allocating or consulting the allocator's inspection registry in the hot path.
Source/Graph integration must consume those actual owners, not a caller's equal
byte-count claim. The existing snapshot factory and byte/mip/label storage are
reused; no second pixel copy, staging placeholder or new native authority exists.
This remains Contract/Prototype CPU provenance, independently of registered
destination generation, backend-owned staging, native write, recording and
completion. All four generic lowering decisions remain closed.

## Original texture upload source and positional input binding

The typed Upload source retains the exact canonical full texture upload input
and exact original destination texture lease. Its source metadata and frozen
pixel allocation use the original Source quota; input metadata and label store
share the original Graph quota. Those Source/Graph owners must be independent
control blocks with their exact declared limits. The source table must contain
the same canonical destination lease. The standalone closure has five nodes;
the Upload source contributes three nodes (binder, input and label store), while
the required destination entry contributes its own two nodes once.

A frozen input identity occupies an explicit CPU-input key namespace, separately
from GPU resource IDs. Core SharedIdentityProjection must treat namespace plus
key as a pair, while preserving strict pointer/control-block/domain agreement
for any shared owner or identity. Default namespace retains all previous behavior.
A namespace never manufactures GPU origin; source records use factory-validated
canonical ownership before projecting keys. No Upload source publishes a fake
GpuResourceId. Equal numeric keys in independent namespaces cannot alias an
unrelated GPU source; changing a shared owner's namespace must reject.

TextureUpload positional references are the original destination Texture and
exact canonical full input. The descriptor retains that same original input,
including immutable pixels/mips/scalars/label and quota owners. Full-graph mip,
retained-pixel and label limits charge each upload command, independently of
source-domain and ownership bounds. The snapshot label must match the command
variable record; extra independent snapshot label bytes also count against the
label budget. Source construction reuses deterministic TextureUploadLayoutValidation
and its full mip mapper, without a second local range/pitch algorithm.

This is CPU ownership, layout and positional field binding only. Caller generation
and backendOwnedStaging values remain original declared fields, not native facts.
Native destination generation/write access, backend-owned staging initialization,
recording/submission/completion and full original registry/state must be proven
independently. Null is ValidatorOnly and Vulkan/Metal/WebGPU are DiagnosticFailure
for generic execution. The BufferUpload CPU contract is defined below; the
Readback adapter remains unavailable until its complete original source contract
is implemented.

Original upload snapshot quota provenance and canonical TextureUpload CPU input
binding verified 2026-10-04: host/Null10/10, Wasm/browser6/6, inventory101/101.
Both reports require originalUploadInputQuotasPassed,
genericOriginalTextureUploadInputsPassed and sharedIdentityNamespacePassed=true;
all preceding graph markers true, diagnostics/nativeErrors empty. Exact Source
pixel/destination and Graph metadata/label owners, required destination entry,
CPU key namespace, full fields/positional refs and per-command mip/pixel/label
bounds pass. Source closure is usage7/scalars9/input10 for one shared upload used
twice; cold rollback/caller-drop and exhausted hot allocation0 pass. Shared layout
mapper is reused; no fake GPU ID or staging/generation authority exists. Initial
fixture lacked transfer scope and was rejected by the existing order contract;
corrected fixture passes, production gate/budgets unchanged. Contract Complete /
Prototype Complete CPU binding only; generic registry/state/native completion
and all four execution gates remain closed. Evidence texture-upload-original-
input-quotas-* and generic-original-texture-upload-inputs-* YAML/log/XML.
RenderDoc remains RHI-RENDERDOC-TOOL-UNAVAILABLE; no Backend Validated, Platform
Validated or Production Ready promotion. BufferUpload/readback adapters pending.

## Original BufferUpload CPU ownership binding

The BufferUpload declaration has no CPU byte pointer. Its canonical CPU binder
therefore retains the exact original source and destination Buffer leases, frozen
BufferUploadArguments and one original quota-bound label store. It does not invent
bytes or claim that the source buffer contains the declared upload. Both leases
must appear canonically in the Source table; source CopySrc/destination CopyDst
usage and the complete zero-offset declared copy range must fit their frozen
layouts. Same source/destination identity, zero size, wrong staging intent and
missing source ownership deterministically reject before allocation.

The binder uses the original Source quota; its label uses the independent original
Graph quota. Immutable declared limits and original factory ownership must remain
valid. Its exclusive contribution is two nodes (binder plus label store); source
and destination entries each contribute their two-node closure once. Positional
operands are source Buffer, destination Buffer, canonical BufferUpload binder.
The Upload-domain binder uses the same explicit CPU-input key namespace as texture
inputs, without publishing a fake GPU ID. Graph admission requires its exact label
quota, full scalar comparison and equality with the command variable label. Each
upload command consumes its declared byte size and extra retained label bytes
against independent graph caps; byte accounting proves no native write.

Null remains ValidatorOnly; Vulkan, Metal and WebGPU remain DiagnosticFailure for
generic execution. Native staging allocation, initialized contents, resource
revision, state/access, native recording/submission and completion remain separate
requirements. Existing WebGPU backend-private original staging source is retained
as its own scoped prototype and cannot be constructed from this CPU binder.
This stage can achieve Contract Complete / Prototype Complete CPU binding only.

## Original readback input quota provenance prerequisite

Before adapting a leased readback source, its existing canonical full input must
retain immutable original metadata/label budgets and validate both actual quota
owners and declared limits. Its one TextureReadback label record and complete
explicit mip count remain bounded; scalar/mip/label comparison and canonical
argument capture must reject changed quota limits or forged owners. This reuses
the existing snapshot, requires no second source texture or result buffer, and
creates no native source/completion/map/publication authority. Null remains
ValidatorOnly and Vulkan/Metal/WebGPU remain DiagnosticFailure for generic
execution. Contract Complete / Prototype Complete applies only to this CPU
prerequisite; full Request/layout/source/destination closure remains pending.

Original BufferUpload CPU declaration/source binding verified 2026-10-04:
host/Null10/10, Wasm/browser6/6, inventory102/102; both reports require
genericOriginalBufferUploadInputsPassed=true and all preceding graph markers
true, diagnostics/nativeErrors empty. Exact source/destination Buffer table
entries, canonical frozen scalar/label Source/Graph ownership, full incoming
fields/refs, independent byte/label/descriptor/owner caps and original caller-drop
pass. Closure is usage8/scalars10/inputs11; cold exact-less-exhausted rollback and
hot exhausted allocation0 pass. UploadBufferDesc has no CPU byte pointer; this
binder invents no byte copy or native staging initialization. Contract Complete /
Prototype Complete CPU upload adapters only; registry/state/native generation/
recording/completion and four generic gates remain closed. Readback full adapters
are next. Evidence generic-original-buffer-upload-inputs-* YAML/log/XML; no Backend
Validated, Platform Validated or Production Ready promotion.

Original readback full-input quota provenance prerequisite verified 2026-10-04:
host/Null10/10, Wasm/browser6/6, inventory103/103; both reports require
originalReadbackInputQuotasPassed=true and all upload/previous graph markers
true, diagnostics/nativeErrors empty. Immutable original metadata/label budgets,
canonical original snapshot/label owners and complete mip/label bounds pass.
Forged/changed-limit/equal-owner comparisons reject; cold rollback/caller-drop and
exhausted hot allocation0 pass. Contract Complete / Prototype Complete CPU quota
prerequisite only; full readback Request/layout/source/destination closure and
state/completion/map/publication remain pending. Evidence texture-readback-original-
input-quotas-* YAML/log/XML. Four generic execution gates remain closed; no Backend
Validated, Platform Validated or Production Ready promotion.

## Existing readback ownership chain revalidation

The canonical readback Layout, Request and SourceLease must each retain their
original immutable per-instance budget and original metadata quota. Revalidation
checks the original limit and each retained category, then validates the exact
canonical lower-level owner. Layout reuses the deterministic mip mapper to compare
all frozen mappings; Request checks original correlation, layout and category
bounds; SourceLease checks its request and the still-adopted original texture.
A cold receipt validates metadata without claiming texture ownership and remains
closed until allocation-free adoption. Failure preserves the caller's original
UniquePtr. Original object allocation charges remain unchanged by adoption.

This extension preserves existing independent quota arrangements. Generic Source/
Graph domain relationships are an additional adapter admission requirement; they
cannot be inferred from equal numeric limits. The full chain proves neither native
texture contents nor destination/staging/result/completion. Generic lowering
remains Null ValidatorOnly and Vulkan/Metal/WebGPU DiagnosticFailure. This stage
has Contract Complete / Prototype Complete CPU ownership-chain scope only.

Existing original readback CPU ownership-chain quota revalidation verified
2026-10-04: host/Null10/10, Wasm/browser6/6, inventory104/104; both reports
require originalReadbackOwnershipQuotasPassed=true and all prior graph markers
true, diagnostics/nativeErrors empty. Layout/Request/source lease keep immutable
original budgets; five exact quota limits and complete canonical chain validate.
Shared deterministic mip mapper rejects frozen mapping corruption; forged/equal
owner substitutions and changed limits fail. Failed cold adoption preserves caller
UniquePtr; allocation-free adoption retains the same single original texture,
caller-drop and hot exhausted allocation0 pass. Contract Complete / Prototype
Complete existing CPU ownership-chain scope only; typed Source/Graph table and
readback positional/destination closure remain pending. Evidence texture-readback-
original-ownership-quotas-* YAML/log/XML. Full registry/state/native recording/
completion/map/publication and all four generic gates remain closed; no Backend
Validated, Platform Validated or Production Ready promotion.

## Canonical readback typed source and positional input binding

OriginalCommandReadbackTextureSource retains the existing canonical
TextureReadbackSourceLease and its exact Request/layout/input/label chain. It
publishes the original source texture ID/object/layout. It never re-adopts that
texture into another UniquePtr. Adapter and adopted lease metadata share the
original Source quota; Request, layout, input and label metadata share the exact
independent Graph quota. All original per-instance limits and category bounds
must remain valid. Its declared exclusive closure has seven nodes: adapter,
lease, original texture, Request, layout, input and label store.

OriginalCommandTextureReadbackSource retains that same canonical texture adapter
and the exact original named destination Buffer lease. Its frozen Request is
inherited from the adopted source; no clone or caller correlation substitute is
accepted. The destination ID must equal the descriptor's named destination, with
CopyDst usage and capacity for the complete declared byte range. This binder
contributes one exclusive node. Source table admission requires the exact texture
adapter and destination lease; immutable canonical source identities ensure the
seven-node source and two-node destination closure are counted once. The binder's
identity is the canonical Request in the explicit CPU-input key namespace and
publishes no fabricated GPU resource ID.

TextureReadback command operands are that original Texture and canonical Request.
The descriptor must retain the same original full input as the Request. Aggregate
mip, label, declared result-byte and owner limits are independent; command-variable
label must match Request label and each command consumes its potential result
capacity and extra snapshot label bytes. For one source/destination/binder used
twice, the closure is usage12/scalars14/input15, while two mips per command charge
four mappings. Destination wrapper ownership and prospective result-byte caps
prove no mapped result allocation or publication. CPU Result-domain intent remains
required when the prospective unpublished-result limit is nonzero; actual
completion/map/result owners are mandatory later execution prerequisites.

Null is ValidatorOnly; Vulkan, Metal and WebGPU remain DiagnosticFailure for
generic execution. Source generation/initialized contents/state/access and native
recording/submission/completion/map/publication remain independent. No Placeholder,
Dummy, CpuReplay or host-immediate completion path is introduced. This stage can
achieve Contract Complete / Prototype Complete CPU typed binding only.

Canonical original readback typed source/input binding verified 2026-10-04:
host/Null10/10, Wasm/browser6/6, inventory105/105; both reports require
genericOriginalTextureReadbackInputsPassed=true and all preceding graph markers
true, diagnostics/nativeErrors empty. Existing adopted source lease and exact
Request/layout/input/label remain original; no second source UniquePtr exists.
Exact Source/Graph relationships, source/destination table dependencies, CPU
Request key namespace and usage12/scalars14/input15 closure pass. Two commands
consume mip4/label16/prospective-result1536 bounds independently; full fields/
Texture+Request refs, forged/foreign/equal-owner/range/quota negatives and cold
rollback/caller-drop/hot allocation0 pass. Original allocation identity/quota/size
preserved. All seven CPU input families now have complete source/field binding
for supported original sources; unsupported source domains fail at signature.
Contract Complete / Prototype Complete CPU input scope only. Full original
logical device/registry/state/completion and native execution remain pending;
public state() values cannot substitute for revision/generation/range ownership.
Evidence generic-original-texture-readback-inputs-* YAML/log/XML. Null remains
ValidatorOnly and Vulkan/Metal/WebGPU DiagnosticFailure for generic execution;
no Backend Validated, Platform Validated or Production Ready promotion.

## Original logical registration and committed state observation /1

Every GPU source admitted to a complete generic recording belongs to one exact
live logical device domain. Original registration is issued only by the device's
resource factory and owns its device lifetime/epoch, exact resource object/ID,
resource lifetime generation, logical kind and registered state-control identity.
A caller ID, retained wrapper, equal descriptor or state()/subresourceState() value
cannot register a resource or manufacture that evidence. State revision and
resource lifetime generation are separate monotonic domains; neither may stand
in for the other. Device loss, destruction, unregister, generation exhaustion and
terminal state invalidation permanently invalidate the corresponding evidence.

Original committed observations retain canonical device/registration/control
owners and their Source/input graph. Buffer observations name the complete buffer
state domain. Texture observations name normalized aspect/mip/layer coverage and
its original committed revisions, not an assumed uniform whole texture. A view
observation resolves its canonical original parent and exact declared view range.
The original range ledger must reject missing, mixed or conflicting coverage;
no backend-default layout or arbitrary whole-resource state is inferred. Sampler
and pipeline state are explicitly state-free but still require original device
registration/lifetime. Descriptor/table, upload/readback, query, bundle/secondary,
bindless and indirect sources must bind their exact transitive registered closure;
classification alone cannot supply unimplemented domain ownership.

Snapshot/transaction/command acceptance remains externally serialized. A committed
snapshot rejects pending controls; pending evidence is bound to the exact live
MutationJournal and retained transaction, using existing VersionedMutationValue
and RecordedResourceStateControl semantics. Every write, rollback and terminal
invalidation must invalidate obsolete committed evidence. No new control may be
initialized from a caller's scalar enum/revision to impersonate an original
control. Existing RecordedResourceStateSequence resolves ordered transitions and
validates exact stage/access/discard contracts, independently of native barrier
lowering and queue completion. CPU state mutation is not GPU work or completion.

Independent explicit limits cover one device domain, resource registrations,
state observations, normalized texture ranges, retained owners and original CPU
metadata bytes. Counts never imply byte quotas. Cold reservation fails without
consuming ownership; observation/validation/transaction attachment in the hot path
must allocate zero, perform no file/network IO and make no blocking waits.
Source/Graph/Recording/Submission/Callback/Result quotas retain their previously
declared separate meanings; an observation cannot authorize completion owners.

OriginalStateAdmission/1 lowering is version-controlled absence of a complete
public L2 device-issued original registration/observation graph: Null provides
ValidatorOnly and no issuer; Vulkan, Metal and WebGPU return DiagnosticFailure
with stable backend-specific original-state issuer diagnostics. Null's scalar
validation, Vulkan/Metal private mutation snapshots and WebGPU's genuine private
versioned controls are reusable implementation primitives but do not implement
this missing device issuer/graph. Selected WebGPU readback native evidence cannot
open this contract. All originalIssuerAvailable/executionAvailable flags remain
false. Default budgets are unavailable; explicit Prototype opt-in changes no
issuer availability. Recovery requires a new valid device-issued cold registration
and exact original observations once an implementation exists; retrying a borrowed
scalar or switching a backend label never recovers missing authority.

Required tests cover all 15 source requirements, unknown-domain/backend failure,
independent version/opt-in/device/registration/observation/range/owner/CPU limits,
unchanged actual private/owned prototype regressions, all four closed issuer and
execution decisions, and exhausted hot classification/gate allocation0. This
phase can reach Contract Complete requirements/closed lowering only; device-owned
issuer implementation and backend/platform validation remain pending.

Original logical state contract verified 2026-10-04: host/Null10/10,
Wasm/browser6/6, inventory106/106. Both genericOriginalStateContractPassed and
preceding graph flags true, diagnostics/nativeErrors empty. All15 source
requirements and independent intent bounds explicit; scalar authority rejects,
four original issuer/execution gates remain closed, exhausted hot allocation0.
Contract Complete requirements/closed lowering only. Evidence generic-original-
state-admission-contract-* YAML/log/XML. Device-owned registration/control/range/
completion/native graph remains pending; no Backend Validated, Platform Validated
or Production Ready promotion.

## OriginalDeviceLifetime /1 prerequisite

The original L2 device owns exactly one opt-in logical domain. Only its sealed
cold preparation can construct that domain; callers cannot supply an epoch,
generation, device ID or shared alias as authority. Devices cannot be copied or
moved. The domain binds the exact device object and an immutable explicit budget
with one domain, two retained owners (domain and existing core LifetimeEpoch
state), and a matching CPU metadata quota. Both original allocations and their
control blocks charge that quota; count budgets do not imply byte limits.
Preparation failure leaves the device without a domain and permits cold retry.
Successful preparation is one-shot, including after terminal retirement.

Tickets contain weak canonical domain and epoch observations. Copies allocate
nothing and prolong neither the domain nor device lifetime. Their control blocks
remain allocator-accounted until the last weak ticket is destroyed. Validation
against another device rejects even with an equal budget. Explicit retirement
and destruction permanently invalidate issued tickets; no terminal epoch can be
reopened or used to issue fresh authority. Observation, cold preparation and
terminal mutation are externally serialized. Ticket issuance/copy/validation and
retirement perform zero allocations, no blocking waits and no IO.

Null, Vulkan, Metal and WebGPU all use the same L2 base for Equivalent logical
CPU lifetime semantics. Unknown backends fail deterministically. This mapping
asserts no native capability. Resource factories, canonical original controls,
normalized range ledgers, native loss callback integration and complete frozen
graph binding remain separate required work. Native loss callback integration
is absent; no nativeLossObserved or resourceIssuerAvailable claim is made.
Default budgets fail closed, explicit Prototype opt-in grants only
the logical prerequisite, and every original-state/generic execution gate stays
closed. Recovery from terminal retirement requires a new device/domain; native
loss recovery remains unimplemented. This phase can reach Contract Complete /
Prototype Complete logical CPU lifetime only.

Required tests use actual Null/WebGPU device factories for exact/foreign device,
default/malformed/wrong quota, cold exact/one-less/exhausted rollback, one-shot
retirement, caller-drop/weak destruction and exhausted hot allocation0. These
prove logical domain ownership only. Existing browser smoke remains mandatory;
no visual, backend native state or platform/production promotion follows.

OriginalDeviceLifetime/1 verified 2026-10-04: host/Null10/10, Wasm/browser6/6,
inventory107/107. Both originalDeviceLifetimePassed and prior graph flags true,
diagnostics/nativeErrors empty. Actual L2 devices own sealed exact-quota two-owner
core epoch domains. Weak exact/foreign/retired/destroyed tickets, mutable-quota
rejection, cold exact/one-less/exhausted rollback and one-shot preparation pass;
exhausted hot issuance/copy/validation/retirement allocation0. Metal compilation
passes; current host has no Vulkan target/SDK. An earlier Chrome temporary-profile
cleanup race occurred after successful reports; failure evidence retained and
final-code rerun6/6 passes. Evidence original-device-lifetime-* YAML/log/XML.
Contract Complete / Prototype Complete logical CPU lifetime only. Four explicit
Equivalent logical lifetime mappings grant no original resource/state/native-loss
or GPU authority; unknown backend fails. Factory registration, original controls,
range ledgers, native loss integration and complete graph binding remain pending.
No Backend Validated, Platform Validated or Production Ready promotion.

## Device factory resource lifetime registration /1

The next factory boundary is a logical lifetime prerequisite to original state
admission, not an original state issuer. Only the original L2 device can issue a
record for its newly created Buffer, Texture or TextureView. Each record binds
the exact base resource object address, actual opaque nonzero ID, typed kind,
separate monotonic lifetime generation and one canonical device registry. No
caller-created registration, external adoption or scalar state/revision is an
issuer. TextureView registration binds the same device's exact live Texture
registration, not an equal ID or descriptor. The existing frozen Source graph
still binds full immutable physical layouts and exact declared view ranges.

One explicit, one-shot Prototype policy defines independent Buffer/Texture/View
registration capacities, maximum unique retained owners and CPU metadata bytes.
The registry retains the original device domain/epoch; its metadata quota is
exact and independent from the domain quota and original resource allocation.
The unique owner closure is three fixed owners (device domain, epoch state and
registry), plus record and original wrapper for each live registration. Owner
capacity is enforced independently from all three kind capacities and CPU bytes.
Reuse core AtomicCapacityBudget/Lease and MonotonicIdSequence; no unbounded lookup
container is required. Both registry and records allocate through core under the
exact original registry quota. Resource allocations retain their original charges.

Weak resource tickets do not retain the resource or registration. Destroying the
resource expires its record; generation values never recycle within the exact
registry even after count release. Device retirement/destruction invalidates all
records via their original domain. Views fail if the exact original parent has
expired. Copying a resource wrapper cannot duplicate authority. Original factory
failure destroys only its unpublished candidate and returns null with a stable
diagnostic; no placeholder is returned. Successful registry preparation cannot be
replaced or reopened. Ticket issuance/validation is allocation-free, externally
serialized, nonblocking and performs no IO.

Null, Vulkan, Metal and WebGPU use the same L2 typed logical registry for
Equivalent CPU lifetime semantics. Ordinary and placed resource factory returns
must consume this policy when explicitly configured; WebGPU placed creation
remains DiagnosticFailure because no placed resource exists. A default device
has no registry, so its ordinary resources carry no original-registration proof.
Unconfigured factories keep their existing resource API, while original
admission rejects missing tickets. Unsupported source kinds and unknown backend
contracts fail explicitly. No original state control/range observation, pending
journal, native loss observer, completion or execution capability is inferred.
All full original-state/generic gates remain closed until those prerequisites
are implemented, including terminal native loss wiring. This stage can reach
Contract Complete / Prototype Complete typed logical lifetime only.

Tests must use actual Null/WebGPU factories to cover original object identity,
foreign/unregistered/equal-ID impostors, same-device parent closure, separate kind
and unique-owner limits, exact/one-less/exhausted cold metadata quota, unchanged
original allocation charges, monotonically distinct post-release generations,
resource/device destruction and retirement, and exhausted hot allocation0. Four
backend closed full-state conclusions and existing smoke remain mandatory.

OriginalResourceRegistration/1 verified 2026-10-04: host/Null10/10,
Wasm/browser6/6, inventory108/108. Both originalResourceRegistrationPassed and
prior graph flags true, diagnostics/nativeErrors empty. Actual Buffer/Texture/
TextureView factory returns, including supported placed Textures, consume one
sealed L2 registration policy across all four backend sources. Exact base object,
original ID, independent monotonic lifetime generation, canonical original device
and exact registered view parent bind; dead parents/resources and retired/destroyed
devices invalidate tickets. Independent kind/owner/exact quota limits, cold exact/
one-less/exhausted rollback, changed quota/foreign/unregistered/preexisting/equal-ID
negatives, unchanged original allocation charges and hot allocation0 pass. The
canonical Source table revalidates layouts/dependencies and all7 source variants'
GPU records; existing upload/readback fixtures also run on registered actual
devices. Metal compilation passes; Vulkan target/SDK absent on current host.
Evidence original-resource-registration-* YAML/log/XML. Contract Complete /
Prototype Complete typed logical lifetime/source binding only. Registry metadata
has an independent bounded owner closure, but complete state admission still must
count the union with the CPU input graph: Source fixture10 plus records4/fixed3
is17. Existing Source/input owner counts alone do not prove that combined bound.
Original controls/range revisions, pending journals, native loss observation,
completion and full execution remain pending/closed; no Backend Validated,
Platform Validated or Production Ready promotion follows this CPU prerequisite.

## Original committed Buffer control /1

A Buffer committed observation is issued only from a registered original resource
and its one bound original RecordedResourceStateControl. Null/Vulkan/Metal replace
scalar state plus MutationSnapshot at original creation/write boundaries with the
shared versioned control. WebGPU binds its existing native payload control; an
absent native payload's metadata control remains ineligible. No control is created
from state() or caller revision. The state enum moves to the shared L2 common
header without changing its values so resources and controls share one definition.

The bounded scalar utility supports original journal preparation without writing
or incrementing a revision. Preparation enlists that exact embedded snapshot and
rejects inactive/foreign-journal ownership. Committed observations reject pending
controls. A write consumes a monotonic revision; rollback consumes a new revision
even when restoring equal values. Commit of preparation with no write changes no
value/revision. Existing pending observation identity and terminal invalidation
remain unchanged. No journal preparation signals GPU order or completion.

The sealed weak committed Buffer observation binds original registration,
resource lifetime generation and control identity plus frozen state/revision.
It exposes only values and deterministic diagnostics, no native or mutable control
pointer. Equal scalar facts, a foreign buffer/device, pending control, stale
revision, dead resource/device or inactive original control never validate.
Observation requires a retained original wrapper at use; weak tickets do not
extend resource lifetime. Exact Source/input ownership must accompany any frozen
observation graph, including the union of canonical registry/record owners with
existing CPU graph owners. No pending graph is issued without the retained exact
original journal/transaction. Default unregistered resources fail closed.

Null provides logical ValidatorOnly buffer control; Vulkan/Metal provide equivalent
logical versioned controls and WebGPU observes the original native-backed logical
control. This is a control binding prerequisite, not a native barrier or complete
original-state issuer. Texture range observations, unsupported source closures,
native loss wiring, retained pending transaction, completion and native command
admission remain missing, so all complete original-state/generic gates stay closed.
Contract Complete / Prototype Complete committed Buffer scope is the maximum
status for this stage. Tests must prove actual registered resources' committed,
pending, rollback/commit, same-fact foreign, liveness and hot allocation0 semantics
and preserve the existing journal, four-backend gates and platform smoke.

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

## Original committed Buffer input graph /1

This prerequisite retains the exact quota-bound OriginalCommandInputs and canonical
Source table. It freezes one sealed original committed observation for every
canonical Buffer lease in Source order, including operands reached by BufferUpload
through its required original source/destination leases. Empty source sets and
Texture/View/TextureUpload/TextureReadback source families fail with a stable
unsupported-source diagnostic. No partial graph may claim complete state admission.
All source registrations must belong to the exact live original issuing device.
Input equality is exact shared pointer and control-block identity, never equal
fields or resource IDs. Observation values are read-only logical facts, with no
native control exposed. Validation rechecks original input layouts/quotas, device
lifetime and every frozen original Buffer observation. Pending preparation, any
revision change (including rollback ABA), terminal device retirement/shutdown or
resource/control invalidation rejects the entire graph. Cold rejection changes
neither state nor original ownership; caller drop preserves retained input owners.

Canonical registration owner accounting adds exactly one original record per
unique registered GPU wrapper and the declared fixed registry closure of three
owners to the existing CPU graph. Wrapper ownership is already counted by its
canonical lease and must not be counted again; upload/readback/view dependencies
must resolve through required canonical Sources. SharedIdentityProjection inspects
the canonical GPU inventory with no temporary table. Thus the existing four-resource
Usage fixture10 becomes17. A two-Buffer input graph9 becomes15 with its new graph
root1, records2 and registry fixed3. Independent state registration/observation/owner
limits and exact cold metadata quota are enforced; no inference from native budget
or Source count alone. The state quota must be a different control block from the
Source and input Graph quotas. Hot validate/comparison allocates zero, uses no IO,
blocking wait or native call; all operations require external serialization.

Null is ValidatorOnly. Vulkan/Metal/WebGPU have equivalent logical Buffer graph
mapping only when the original actual resource control and registration exist;
metadata/native-empty resources fail. This is not a barrier sequence, pending
transaction, native loss observer or completion issuer. All four full original-state
and generic execution decisions remain unchanged and closed. Tests cover exact and
foreign graph/device, forged owners, unique ownership and repeated dependencies,
cold exact/one-less/exhausted quota rollback, stale/pending and retained caller-drop,
and existing quick/Null/browser smoke. Maximum status is Contract Complete /
Prototype Complete committed Buffer graph scope, without Backend/Platform/Production
promotion or a new shader/rendering path.

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

## Original Texture state range geometry /1

Normalize committed Texture state coverage into explicit mip/layer/aspect cells
before original control migration. Row-major rectangular indexing is reusable core
logic, bounded by a caller-declared maximum cell count; zero dimensions, overflow,
unknown format and insufficient cell capacity reject. Texture state geometry uses
format metadata: color formats have one Color plane, D32Float one Depth plane,
Depth24Stencil8 separate Depth and Stencil planes. It does not merge aspects or
multiply cube faces already represented by arrayLayerCount. A 3D texture has one
array layer in this contract; depth texels do not create independent state cells.
Ranges are nonempty and must fit exact declared mip/layer bounds with an aspect
valid for the format. Source order is aspect, mip, layer. Range ordinal mapping is
mip then layer, so overlapping ranges resolve to the same canonical cells. A
uniform whole-resource slot may satisfy only an exact whole declared aspect range;
a partial request is never widened. Unknown or malformed geometry rejects before
allocation/state mutation. These value facts alone grant no original registration,
control, committed revision, transaction, native layout or execution authority.

All four backends share this pure geometry validator. Null remains ValidatorOnly;
Vulkan/Metal normalized original range issuers are DiagnosticFailure until actual
scalar/sparse snapshot migration. WebGPU's existing UniformWholeTexture control
may be reused for an exact whole range; partial original state execution remains
DiagnosticFailure with RHI-WEBGPU-BARRIER-SUBRESOURCE-STATE-UNPROVEN. Separate
Depth/Stencil writes cannot be represented by the existing uniform control and
must reject rather than alias the planes. New native backend mappings and gates
are not implemented by a successful geometry test. Creation metadata, native
acquired swapchain state, original writes, exact retained journal, revision and
lifetime remain required in the subsequent migration. Quick contract and Null/
both browser smoke cover rectangular overflow, cell capacity, overlapping ranges,
aspect and cube/3D mapping and exhausted hot allocation0. This stage can reach
Contract Complete geometry only, not Prototype/Backend/Platform/Production readiness.

Original Texture migration audit, next stage: actual Null/Metal creation state is
explicit descriptor initialState. Vulkan newly allocated images use the declared
native Undefined creation contract; only actual acquired swapchain payload may
supply its backend-private acquired state. WebGPU must reuse actual payload state,
not seed a second control from the wrapper state() result. Existing Null64 sparse
records and dynamically growing Vulkan/Metal exact-range vectors cannot prove
normalized coverage or bounded hot allocation. They must be replaced at original
creation and write boundaries, with exact descriptor-derived cell/CPU allocation
charges, original journal ownership and versioned rollback/commit. Existing core
RowMajorIndexGrid, MutationSnapshot and VersionedMutationValue are the reuse
boundaries; missing native or range controls reject. Whole scalar state() is not
authority for mixed coverage. Any migrated writer must diagnose nonuniform before
state rather than confuse it with a valid Undefined state. WebGPU uniform control
cannot acquire partial or independent stencil semantics through an adapter.
This audit supplies no new original issuer. The next stage must implement and test
those original controls before registered range observation/input graph binding.

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

## Original normalized Texture control /1

At actual resource creation, Null/Metal use the explicit descriptor creation state;
Vulkan uses its declared Undefined/native acquired state mapping. A fixed dense
cell array and equally sized rollback snapshot are allocated once through core,
with exact size from normalized descriptor geometry. No sparse insertion, guessed
capacity or hot growth is permitted. Generic bounded versioned mutation storage
belongs in core and reuses MutationSnapshot plus VersionedMutationValue. One
original texture-wide monotonic revision conservatively invalidates every range
observation after any write, including disjoint writes; no per-cell revision claim
is made. Range observations freeze the exact original range, uniform state and
whole-control revision. This conservative invalidation changes no recorded state
coverage and cannot revive a stale observation.

All writes validate complete range geometry, known before/after state, uniform
covered before-state and exact journal ownership before changing cells. Mixed
coverage is a distinct diagnostic, never a valid Undefined state. A journal
prepares the same embedded snapshot and revision token; preparation has no write,
no-write commit preserves revision and rollback consumes a new revision while
restoring all cells. Fixed snapshot capacity proves preparation/write/rollback hot
allocation0. Resource destruction detaches embedded records. Terminal invalidation
or revision exhaustion never restores authority. Scalar state() and subresourceState()
remain value-only legacy queries and cannot authorize writes or observations;
unsupported or nonuniform scalar results are interpreted only via deterministic
control diagnostics at admission.

WebGPU now binds the creation-bound native normalized control, with actual texture
and device presence, under NormalizedTextureStateCreation/1 below. Exact partial
ranges and independent Depth24Stencil8 planes use that same authoritative source;
legacy whole observations require all planes uniform. The earlier uniform-only
checkpoint is superseded. Native-empty/metadata controls cannot issue canonical
original observations, and native execution remains independently gated. Null
normalized control is ValidatorOnly; Vulkan/Metal equivalent logical normalized
controls grant no native barrier proof. Four complete state/generic gates remain
closed. The public sealed weak observation binds registration, resource generation,
exact original control kind/identity and frozen range/state/revision; dead/retired/
foreign/pending/stale/mixed/invalid ranges fail. Inputs retain the original resource
wrapper before any use; no native or mutable control is exposed. Texture-view
parent/input graph binding, retained pending transactions, native loss and complete
execution remain subsequent requirements. Tests must exercise actual factory
registration/control, overlapping range writes, mixed negatives, separate planes,
prepare/no-write commit, rollback ABA/commit, quotas/destruction/retirement and
existing unit/Null/both browser smoke. Maximum status is Contract Complete /
Prototype Complete logical normalized control; Backend/Platform/Production remains
unproven without target payload/platform and RenderDoc evidence.

Original normalized Texture control /1 verified 2026-10-04: host/Null10/10 plus
extra5/5, Wasm/browser6/6, inventory112/112; both originalTextureControlPassed and
required prior flags true, reports clean. Actual normalized Null/Vulkan/Metal
creation/write boundaries and, at that historical checkpoint, actual WebGPU uniform
native payload control (superseded below by NormalizedTextureStateCreation/1)
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

## Original committed resource input graph /1

Cold preparation retains the exact original Inputs root and original device ticket.
Each canonical Buffer source contributes one committed original observation. Each
active Texture/View operand contributes an ordered range observation identified by
command index, positional reference and source index. Copy inputs supply their
explicit declared source/destination ranges, including the exact original full-mip
snapshot owner; this stage does not prove copy geometry or native mapping. Texture
barriers supply their declared range. Range-less memory/alias/retain intents cover
the whole resource; Depth24Stencil8 emits separate Depth and Stencil observations.
Render-pass operands use the exact retained view's declared parent range and aspect,
with original registered parent identity checked. Upload/readback operands use the
exact retained original input's declared range. This is committed input evidence,
not proof of render-pass stencil access or a future ordered pending transaction.
Unknown Texture operand semantics fail with a stable diagnostic. Unused parent/
staging closure resources remain registered owners; they grant no active observation.

A shared allocation-free RHI semantic projector replays this ordered sequence in
preflight and validation; no inferred defaults, widened partial ranges, copied
resource controls or backend-name branching is permitted. Observation and Texture
range counts have independent Source and state budgets; original registrations
and retained CPU/registry/record ownership use the existing unique owner union.
State metadata quota is independent of Source and Inputs. The exact retained
inputs/device and every frozen control/generation/range/revision are revalidated
on use. All cold allocation failures release only newly owned state metadata;
validation, projection and pending/stale/rollback rejection allocate0.

Null is ValidatorOnly logical evidence. Vulkan/Metal are Equivalent logical range
observation providers after genuine normalized control binding; complete execution
remains DiagnosticFailure. WebGPU now reuses its actual creation-bound normalized
control for exact partial/aspect ranges; legacy whole consumers fail on nonuniform
state. This supersedes the earlier uniform-only checkpoint. No full issuer,
NativeSync, public/general recording or bindless
execution gate changes. Pending transactions/native loss/completion and complete
native payload remain mandatory. Maximum status Contract Complete / Prototype
Complete logical graph, with actual Null and both browser smoke; no Backend/
Platform/Production promotion or visual proof from CPU observations.

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

## Original L2 device native-loss subscription /1

Explicit Prototype opt-in prepares the original logical domain first, then a
native-loss subscription, then the resource registry. A subscription may not be
attached retroactively to an existing registry. The receiver is minted only by
the exact original GpuDevice, observes its sealed domain weakly and can deliver
only terminal NativeLost. Ordinary tickets cannot create receivers. Delivery
invalidates the same original epoch and all registered observations permanently;
retirement/shutdown remain terminal and cannot be overwritten or revived by a
late native event or journal rollback. This stage proves domain/observation
revocation, not terminal mutation of every backend-private unsubmitted journal;
retained transaction/control quarantine remains a subsequent obligation.

Reuse DeviceLossRegistrationBudget/validator and ExternalCallbackGate. One original
device has one subscription and one callback; both budgets must explicitly admit
one, and the subscription has an independent exact CPU quota and one retained
metadata owner. It retains no device or resource object. Backend-private native
source ownership is captured at actual device construction and released at actual
device destruction. Original resource factory returns must validate that their
actual native owners match that captured source when loss binding is opted in.
The public L2 interface exposes only stable diagnostics and bound status, never
native handles or registration state. A successful binding is one-shot. Cold
allocation/native binding failures publish no binding; the native bridge clears
captured userdata before compensation, allowing explicit retry before registry
creation. No native failure becomes a placeholder subscription.

The backend payload uses a preallocated shared subscription object, held by its
exact device plus a deliberate native callback lease on the same control block.
Native suppression precedes detachment and callback-lease release. Delivery clears
userdata, invalidates the weak domain, finishes the one-shot gate and releases the
lease without allocating. Device destruction invalidates Shutdown first and
requests native detach before releasing its payload. A failed native detach must
retain the callback lease until actual delivery; it may not free captured userdata.
Public detachment closes the original domain (Retired), and permits neither rearm
nor native work. Quotas, one-shot/count/owner limits and exact original receiver
must be revalidated; hot delivery/detach/invalid-observation paths allocate0.

Null is ValidatorOnly and rejects native binding. Vulkan native device-lost results
are not yet wired to this exact subscription; binding is DiagnosticFailure.
Metal command-buffer failures do not establish device-wide loss equivalence;
binding is DiagnosticFailure. WebGPU uses only its captured actual native device's
lost promise as Native Prototype; native-empty/foreign source or failed promise
binding is DiagnosticFailure. The global browser lost flag is not binding or
receiver authority. Existing original lifetime/registration/full-state capability
contracts and complete generic/bindless/NativeSync gates remain closed as before.
Required evidence includes actual cold exact-less-exhausted/retry/detach/destruction,
cleared late userdata and genuine browser GPUDevice.destroy/lost delivery reaching
original domain/Buffer/Texture/View/input observations, with no cross-device or
rollback revival. Maximum status for this phase is Contract Complete / Prototype
Complete subscription; Backend/Platform/Production requires full broader evidence.

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

## Original registered state-domain checkpoint /1

At successful original factory publication, each already-bound genuine Buffer or
normalized/uniform Texture control receives exactly one sealed weak original
lifetime ticket. Only GpuDevice may bind it; no mutable control or receiver is
exposed. A reused control, pending control or invalid domain must reject publication
without replacing the original ticket/control. Metadata-only wrappers may receive
a logical registration but cannot gain an original control from this checkpoint.
All ticket storage is embedded, allocator-free and already belongs to the original
wrapper/native payload; weak aliases do not add a retained owner or observer scan.

The checkpoint is checked before active/prepare/write/committed/pending observation
and matching. An original domain terminal event therefore closes every registered
control entry point without a resource inventory, hot allocation or implicit
journal commit/rollback. Raw scalar state and numeric revision queries remain
non-authoritative historical metadata. A pending snapshot may restore value bytes
on explicit rollback, but the immutable ticket remains terminal and no active,
write or observation authority can revive. This is Equivalent terminal admission
revocation, not mutation of the core revision token or proof of complete retained
journal ownership/quarantine. Complete native recording must still bind its exact
original transaction and completion envelope.

All four backends now use genuine normalized texture and scalar buffer controls;
WebGPU owns the normalized texture control in its native payload at creation.
All share the same sealed logical checkpoint;
Null validates terminal rules, Vulkan/Metal native loss observer remains explicit
DiagnosticFailure, WebGPU native original-domain loss comes from its proven
captured-device subscription. Required tests cover actual factory binding, domain
retire/destruction/loss with pending writes, prepare/write/observePending/matches
rejection after terminal state, rollback no revival, no-control/duplicate/pending
negative publication and exhausted hot allocation0 plus Null/both browser smoke.
This checkpoint cannot open the four full issuers or claim complete native
transaction, synchronization, device drain, Platform/Production readiness.


Original registered state-domain checkpoint /1 verified 2026-10-04: host15/15,
Wasm/browser6/6, inventory117/117; both originalStateDomainPassed and preceding
mandatory flags true, reports clean. Genuine publication binds one immutable
embedded weak original domain ticket to existing scalar/normalized/uniform
controls. Retirement/destruction/real captured WebGPU loss closes active,
prepare/write/committed/pending/matching gates; explicit rollback cannot revive
original authority. Reusable sealed array pending stamp and normalized range
observation allocate0; actual legacy barrier diagnostic strings are checked
outside exhausted quotas and have no allocation0 certification. Logical negative
publication fixtures certify no native payload. Owner counts24/22/15/21 and loss
graph20 unchanged; native six quotas retire to0. Evidence
`build/web-bindless-apple-debug/artifacts/bindless-continuation/original-state-domain-evidence.yaml`
and corresponding YAML/log/XML. Contract Complete / Prototype Complete checkpoint
only. Vulkan official-header syntax passes; Metal compiles/availability-gated;
four full execution issuers remain closed. No broad Backend Validated, Platform
Validated or Production Ready; RenderDoc tool unavailable. Continue complete
retained original transaction, quarantine and exact completion ownership.

## Original pending enlistment epoch /1

A journal address and unchanged value/revision cannot identify one pending
transaction. Preparing, committing without a write, and preparing the same
journal again preserves those facts. An observation from the first enlistment
must reject the second enlistment even when the original control, journal,
value, revision and byte storage remain identical. Reusing a journal object or
its storage must never recycle pending authority.

Each reusable VersionedMutationValue owns an embedded non-recycling monotonic
pending-enlistment sequence, separate from its value revision. The first
successful enlistment into a previously unowned snapshot acquires one epoch;
repeated prepare/write within that same enlistment retains the epoch. Sealed
PendingObservation includes that epoch alongside exact control/journal/value/
revision, and matching requires the current pending snapshot and the same epoch.
Commit/rollback/destruction clear snapshot ownership; the next enlistment must
acquire a new epoch. A no-write commit must preserve committed value/revision.
Epoch exhaustion refuses any new journal preparation/write before snapshot or
value mutation. Already-owned enlistments remain usable, and direct non-journal
writes keep their existing independent value-revision contract.

VersionedMutationArray reuses its scalar stamp's epoch and additionally requires
its original array snapshot ownership. Normalized Texture pending range evidence
reuses that exact stamp; no second session counter, registry, heap allocation or
backend-specific inferred identity is permitted. Null/Vulkan/Metal/WebGPU consume
the same Equivalent CPU identity rule; native execution/transaction/completion
issuers stay closed. This is a pending identity prerequisite, not a retained
original transaction or native completion proof. Required regression paths are
scalar/array prepare-no-write-commit/reprepare, repeated same-session prepare,
foreign journal/control, rewrite/rollback/commit, zero/last/exhausted epochs,
unchanged value/revision on refusal, hot allocation0 and Null/both browser smoke.


Original pending enlistment epoch /1 verified 2026-10-04: a red regression first
proved prepare/no-write-commit/reprepare could revive old pending observations.
Reusable core scalar state now seals a non-recycling enlistment epoch separate
from value revision. Arrays and normalized Texture ranges consume that exact
stamp. Same-session prepare remains stable, no-write committed revision stays
unchanged, same/recycled journal address rejects old evidence; zero/last/exhausted
new enlistments refuse before value/snapshot mutation, direct writes remain
independent. Stable resource/query enlistment-exhaustion diagnostics distinguish
revision exhaustion. Core quick and host15/15, Wasm/browser6/6, inventory119/119;
both pendingEnlistmentEpochPassed and prior mandatory original state/loss flags
true, reports clean. Evidence
`build/web-bindless-apple-debug/artifacts/bindless-continuation/original-pending-epoch-evidence.yaml`
and red/final logs, reports and XML. Contract Complete / Prototype Complete pending
identity prerequisite only; original retained transaction still pending. Its
module/ownership audit is `original-retained-state-transaction-audit.yaml`: private
WebGPU native transfer/readback States and borrowed caller journals cannot replace
the canonical registered L2 graph. Continue canonical transaction root, normalized
ordered range preflight, native admission, quarantine and exact completion retention.
All four full issuers/generic/bindless/NativeSync remain closed; Vulkan syntax,
Metal compilation and selected browser proofs do not promote broad Backend
Validated, Platform Validated or Production Ready. RenderDoc tool unavailable.

## Canonical original state transaction ownership /1

The canonical transaction owns the exact OriginalCommandResourceStates graph,
its embedded MutationJournal and its already factory-bound original Buffer and
normalized/uniform Texture controls. It exposes no mutable control, borrowed
journal or native object. Registered resources outlive pending observations and
the journal; journal rollback/destruction precedes graph/resource release.

Cold ownership preparation and initial snapshot enlistment are explicit CPU
prerequisites. They do not validate a planned sequence of state mutations, record
GPU work, commit a command admission or grant GPU visibility/completion. Full
ordered command/range preflight and final-state publication must subsequently
consume this same root before a full execution issuer can open. The existing
private WebGPU transaction cannot substitute for this canonical L2 root.

Reuse the frozen Source budget's maxStateTransactions as a live root-count bound
shared by all Inputs/state graph versions retaining that exact Source object.
The Source embeds AtomicCapacityBudget; the transaction embeds an
AtomicCapacityLease using an alias of the original Source owner. This adds no
second shared owner/allocation for the count domain. Independent native device
submission/callback/loss budgets remain separate. Recording CPU bytes must match
the frozen Source Recording domain and the explicit state admission budget;
Source, Inputs, committed-state and Recording quota control blocks must differ.
Pending observation/range and full retained-owner counts use explicit limits from
the existing state admission budget, additionally bounded by the frozen Source
observation/range limits. Every root adds exactly one retained owner to the
committed graph's existing union; its journal, count domain alias and observations
are embedded or reuse existing owners.

The cold publisher validates the exact original device, graph, controls, ranges
and quota/count bounds before publication. Preparation allocates only under the
Recording quota; allocation/count failures preserve resource state, snapshot
ownership and count leases. Initial enlistment preflights all original controls
before enlisting any; duplicates such as texture view-parent ranges share the
same control/journal and consume no second snapshot. Pending observations bind
exact control/journal/revision/enlistment epoch and original range/value. Foreign
writers must reject while the root owns the snapshots. CPU no-write commit,
explicit abandonment and destruction are allocation-free; abandonment restores
metadata and invalidates old revisions as required by the core rollback contract.
A terminal original domain revokes all root/control observations and cannot be
reopened by rollback. Root ownership count is released only when the root dies;
an abandoned but retained root still consumes its original live-root reservation.

Null, Vulkan, Metal and WebGPU share this Equivalent CPU ownership contract.
Null is a validator for GPU behavior; Vulkan/Metal/WebGPU full native recording,
submission, quarantine and completion issuers remain DiagnosticFailure. WebGPU's
actual captured-device loss subscription must additionally invalidate this exact
root, including after caller graph/resource ownership is dropped. This phase is
Contract Complete / Prototype Complete CPU ownership only when quick contract,
Null hidden-frame, cold exact/one-less/exhausted/count/foreign/forged/quota cases,
retained caller-drop/destruction, pending conflict/commit/rollback, terminal loss
and both actual browser modes pass. It never confers Backend Validated, Platform
Validated or Production Ready on the complete transaction or bindless plan.


Canonical original state transaction ownership /1 verified 2026-10-04: host15/15,
Wasm/browser6/6, inventory121/121; both originalTransactionOwnershipPassed and
prior mandatory original state/loss flags true, reports clean. Root retains exact
canonical graph and embedded journal; genuine factory controls bind mutable access
privately, native recordedState getters remain read-only. Frozen Source live-root
count uses original owner alias, no second count-domain allocation/owner. Mixed
control3/observations6/union25 cold peak900 Wasm, exact-less-exhausted/count1 shared
graph versions, independent Recording/range/registration/owner limits, foreign/
forged/quota tests pass. Initial pending snapshots, foreign writer rejection,
no-write CPU commit, abandonment/caller-drop/destruction and genuine shutdown
allocate0. Real captured WebGPU loss revokes canonical pending root control1/
union13 after caller graph drop, commit rejects, rollback cannot revive; unrelated
loss isolation and quotas7 zero. Previous committed unions24/22/15/21 and loss
graph20 unchanged. Evidence
`build/web-bindless-apple-debug/artifacts/bindless-continuation/original-transaction-ownership-evidence.yaml`
plus reports/logs/XML. Contract Complete / Prototype Complete CPU ownership only;
complete supported ordered scalar/normalized/uniform state/access preflight and
final publication, native admission/quarantine/exact completion remain open.
All four full execution issuers/generic/bindless/NativeSync stay closed. Vulkan
syntax, Metal compilation/availability-gating and selected browser proof do not
promote broad Backend Validated, Platform Validated or Production Ready. RenderDoc
unavailable. Continue exact normalized cell/range sequencing with explicit budgets.

## Canonical original state sequence preflight /1

The next CPU prerequisite uses `OriginalCommandStateSequence` on the exact
retained `OriginalCommandResourceStates`. `OriginalCommandStateTransaction`
exposes its dry preflight only while Prepared. This does not materialize a mutable
plan, enlist a journal, change a control/revision, admit native recording, or
publish completion. Full ordered transaction integration remains open.

`OriginalCommandStateSequenceBudget/1` explicitly requires Prototype opt-in,
command capacity, expanded original observation-cell capacity, per-cell step
capacity, and declared debug nesting capacity. Cell capacity counts every Buffer
observation and every mip/layer/aspect cell of every declared original Texture
observation; duplicate/overlapping ranges are counted separately. Steps count
these declared Texture cells and scalar Buffer operands at their actual command
positions, plus each readback destination write derived from its exact canonical
Source closure. The two limits are independent of retained-owner, observation-count,
range-count, Source and Recording-byte limits. Count admission precedes timeline
resolution and any future state-plan allocation/mutation. The separately completed
ownership root may already exist; the dry scanner itself allocates nothing.

The immutable resolver selects exact cells through `OriginalTextureStateRange`
and then reuses `RecordedResourceStateSequence`. Earlier Texture barriers affect
only cells in their explicit range and aspect. Depth/Stencil planes have disjoint
indices; reverse mapping uses the same `RowMajorIndexGrid`. Initial values come
only from original graph observations. Every transition before-state and
stage/access scope, Buffer/Texture memory scope, supported copy/upload state,
and supported single-pass attachment state is checked in command order. Final
results are per cell and may be nonuniform; they are never coerced to a scalar
whole-resource state. Retention observes state/lifetime but requests no access.

Readback reuses the exact original destination Buffer lease already bound in the
canonical Source closure and committed graph; it checks CopyDst before each
request and charges a separate write step. Source Texture cells require CopySrc.
No destination is reconstructed or added to a second ownership graph.

This prerequisite does not prove copy byte/pitch/extent lowering or complete pass
pipeline/stencil semantics. Alias barriers
lack a canonical original memory-binding closure and fail with
`RHI-ORIGINAL-SEQUENCE-ALIAS-BINDING-UNPROVEN`; no memory binding is inferred from
matching IDs or state. Subpass, read-only attachment, unobserved stencil attachment and all broader
unsupported recording families fail deterministically. Complete supported mutation/access
storage and journal application are the subsequent checkpoint, not completed by
this scanner.

| Backend | CPU prerequisite | Full original execution gate |
| --- | --- | --- |
| Null | ValidatorOnly, genuine scalar/normalized original metadata | Closed |
| Vulkan | Shared L2 dry scanner; native integration unimplemented | DiagnosticFailure |
| Metal | Shared L2 dry scanner; native integration unimplemented | DiagnosticFailure |
| WebGPU | Shared L2 dry scanner; creation-bound normalized original metadata | DiagnosticFailure |

Status: Contract Complete / Prototype Complete read-only CPU prerequisite. No
Backend Validated, Platform Validated or Production Ready promotion follows from
this checkpoint.


The following mutation materialization checkpoint must use this exact retained
root and Recording quota. It may reserve at most the declared expanded-cell and
step capacities; deduplication must preserve exact original control plus cell
identity, including view-parent aliases and separate aspect planes. Scalar
uniform Texture controls can admit only proven whole-aspect transitions. Each
final observation must seal the exact embedded journal, enlistment epoch, state,
range and revision. Original graph observations remain immutable initial
intent; planned pending/committed evidence must be validated separately after
writes, including nonuniform final ranges. No borrowed journal or replacement
state control is allowed. Retained count/owners, rollback-before-release and
terminal loss contracts continue unchanged. Native admission, quarantine and
completion remain gated until this complete plan is materialized and validated.


Canonical original state sequence /1 and canonical readback access prerequisite
verified 2026-10-04: host15/15, Wasm/browser6/6, inventory123/123; both
originalStateSequencePassed and originalStateSequenceReadbackPassed true, prior
mandatory original state/ownership/loss flags true; reports clean. The exact
original graph supplies initial values, explicit expanded-cell/step/command/debug
limits precede state-plan allocation/mutation, scalar sequence resolver filters
barriers by canonical mip/layer/aspect cells. Normalized overlap finals15/14 and
uniform21/20, scope/before/one-less/range failures, valid transfer-to-attachment
state intents, erroneous original ownership-only copies and attachment state
rejects pass. Reverse grid/aspect tests and exhausted-quota dry scans allocate0;
no original state/revision/control is replaced or mutated. Readback reuses existing
registered destination closure, checks source CopySrc/destination CopyDst, charges
hidden writes; cells5/steps6, one-less step and earlier destination transition
negative tests pass. Four full original/generic/bindless/NativeSync gates stay
closed; native proof friends exactly two. Evidence
`build/web-bindless-apple-debug/artifacts/bindless-continuation/original-state-sequence-evidence.yaml`
plus reports/logs/XML. Contract Complete / Prototype Complete CPU prerequisites
only. Complete mutable plan/final evidence/journal application, copy-layout and
full pass semantics, native admission/quarantine/completion remain open. Alias
without original memory binding and unsupported subpass/read-only/stencil paths
fail deterministically. Vulkan pinned-header syntax only; Metal compiles but
actual-device fixture remains availability-gated; RenderDoc unavailable. No broad
Backend Validated, Platform Validated or Production Ready promotion. Continue
same-root bounded immutable plan storage and exact planned-state observations.


PortableOriginalCommandAccess/1 checkpoint (2026-10-04): Contract Complete /
Prototype Complete CPU preflight. Host15/15, Wasm/browser7/7, inventory125/125;
both browser modes originalAccess/upload/readback flags true, reports clean.
The authored portable opt-in requires 4-byte Buffer copies and the existing
256-byte row-pitch contract, explicit complete ordered mip descriptors and full
padded footprints (not just last-row useful bytes). This deliberately refuses
some native-valid minimally padded buffers; no native equivalence is inferred.
Compressed 3D, layered/subpass/read-only/stencil passes, Present without an
original swapchain closure and alias without original memory binding reject.
Uniform WebGPU controls retain whole-range requirements; separate dual-mip copy
and single-mip pass fixtures cover access semantics without widening controls.
Shared upload/readback packing and exact frozen mip projection are reused.
All-controls write-count plus one rollback revision admission is read-only and
requires external serialization. Null validator and Vulkan/Metal/WebGPU share
CPU validation; all four full execution/issuer decisions remain closed. This
checkpoint grants no Backend Validated, Platform Validated or Production Ready
status and changes no shader or visual output; RenderDoc unavailable.
Evidence: build/web-bindless-apple-debug/artifacts/bindless-continuation/
original-access-evidence.yaml. Continue same-root immutable plan and final seals.


OriginalCpuStatePlan/1 checkpoint (2026-10-04): Contract Complete / Prototype
Complete CPU plan only. Host15/15, Wasm/browser7/7, inventory126/126 and both
originalPlanPassed=true; reports clean. The canonical transaction embeds bounded
immutable ordered access/transition intent and unique final mip/layer/aspect cells
under its existing Recording quota, with no new shared owner or state control.
Complete access scanning and per-control writes-plus-one-rollback admission precede
cold storage and application. Each barrier writes its exact range once; three
barriers consume three revisions, rollback one. Overlapping normalized final
ranges can be nonuniform; exact per-cell pending/committed seals validate the
embedded journal/enlistment epoch/revision rather than stale initial graph values.
Scalar Texture controls require the exact default range and reject combined
Depth24Stencil8 aspect planning. CPU apply/commit are explicitly Prototype
methods; commit proves no GPU order, visibility, submit or completion. Abandon
retains the Source count until root destruction. Actual WebGPU loss with an
applied original Buffer transition preserves pending ownership until abandon,
refuses commit and cannot revive the original domain; retire/shutdown agree.
Cold exact/one-less/exhausted and independent bounds, uploads/readbacks/four copy
families/pass/normalized overlap, caller-drop and exhausted-hot allocation0 pass.
Null remains ValidatorOnly; Vulkan/Metal/WebGPU full native execution/issuer remain
DiagnosticFailure. Existing private WebGPU native transactions are separate graphs
and cannot substitute for this canonical root. Next: version exact native closure
and recording/admission/quarantine/completion contract before backend integration.
No Backend Validated, Platform Validated or Production Ready promotion. Selected
affected targets build/pass; extra all-target build fails existing Unity importer
DirectNative CFG proof. No shader/visual changes; RenderDoc unavailable.
Evidence: build/web-bindless-apple-debug/artifacts/bindless-continuation/
original-plan-evidence.yaml and original-plan-native-boundary-audit.yaml.


CanonicalOriginalNativeClosure/1 checkpoint (2026-10-04): Contract Complete /
Prototype Complete resource closure only. Final host15/15, Wasm/browser7/7,
inventory127/127; both canonicalNativeClosurePassed/originalPlanPassed true,
prior mandatory markers retained, reports clean. Backend-private immutable closure
owns the exact canonical OriginalCommandStateTransaction and its original factory
registry observations, registration generations, native shared owners/objects/
devices/layouts/allocation tokens. Hot registry validation uses nonblocking existing
adapters. No reconstructed registry, second graph/control/journal or recorded proof.
Source-authored payload bound precedes allocation; root Recording quota funds cold
publication. Reused core identity/allocation projection counts shared texture/view
backing once: resources5, root owners30, closure owners42, bytes4224, Wasm cold1652.
Independent limits and exact/one-less/exhausted quota, forged control blocks,
foreign device, native size/object/device/token changes and fresh prepare from
committed root reject deterministically. Pending/committed validation, caller-drop,
genuine shutdown and actual original browser loss pass; original Buffer root
owner13/closure17 stays pending, refuses commit and cannot revive on rollback.
All hot paths allocate0. Null ValidatorOnly refuses absent native resources;
Vulkan/Metal DiagnosticFailure; WebGPU selected Prototype resource binding only.
No canonical native recording/submission/completion has executed, no broad Backend
Validated, Platform Validated or Production Ready promotion. Full generic original,
bindless and NativeSync gates remain closed; proof friends exactly Recording and
Submission. Existing private WebGPU transactions cannot replace the canonical root.
Next: seal CPU mutation authority before native recording and bind independently
budgeted scopes/admission/quarantine/exact completion to that same root. No shader
or visual changes; RenderDoc unavailable. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-native-closure-evidence.yaml`
and snapshotted reports/logs/XML, including preserved initial Wasm Null link failure
and corrected pure-contract-only Wasm fixture boundary.


CanonicalOriginalNativeLifecycle/1 contract checkpoint (2026-10-04): Contract
Complete; Prototype Complete read-only preparation inspection only. Host15/15,
Wasm/browser7/7, inventory128/128; both browser modes lifecycle/closure flags true,
clean reports. The complete transition inspection requires an exclusive one-shot
claim, successful actual recording scopes, an exact submission reservation, and
MaySubmit BEFORE native queue invocation. No pre-submit event commits the embedded
journal. Uncertain submit/cancel invalidates affected original controls and retains
owners; only exact queue AND callback drain or captured original loss with safe
callback teardown permits release. Submitted work never rolls back to reusable
CPU state. Receipt/drain alone is not success; terminal phases cannot revive.
Public event enums and transition inspection issue no lifecycle authority.
Source-authored submission/callback/registration limits and distinct original
Source/Graph/Recording/Submission/Callback quota ownership are inspected against
the genuine Prepared canonical plan. Core SharedOwnerProjection now supplies
reusable allocation-free owner independence validation, rejecting duplicate
objects/control blocks, missing and nonowning/forged aliases. Read-only admission
preserves root/count/journal and rejects Pending/Committed plans. Null ValidatorOnly;
Vulkan/Metal/WebGPU DiagnosticFailure until sealed native issuer payloads exist.
No broad Backend Validated, Platform Validated or Production Ready promotion.
Next: implement the exclusive CPU mutation interlock and pre-record-only ownership
seal, then consume it from real native recording/admission/completion without a
second state graph/journal. Generic original/bindless/NativeSync gates stay closed.
No visual output or shader changes; RenderDoc unavailable. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-native-lifecycle-evidence.yaml`.


CanonicalOriginalRecordingSeal/1 checkpoint (2026-10-04): Contract Complete /
Prototype Complete pre-record CPU ownership interlock only. Host15/15,
Wasm/browser7/7, inventory129/129, quick unit/Null3/3; both browser modes seal,
lifecycle, native closure and original plan flags true, reports clean. A cold
quota-bound seal retains the exact canonical root plus independent Submission and
Callback CPU quotas (root30 + three owners =33). Full source/domain/owner admission
and every shared allocation finish before the one-shot root claim is published.
Failed cold publication leaves the genuine root Prepared and unsealed. The exact
original graph/control/journal/count are preserved; no second state transaction.
Public CPU materialize/apply/begin/commit/abandon now return the stable
RHI-ORIGINAL-TRANSACTION-RECORDING-SEALED diagnostic while claimed. Abandon returns
a diagnostic instead of silently bypassing the interlock. The claim never restores
caller CPU mutation authority. This version exposes no native event API, encoder,
recorded proof, submit or callback, so seal destruction is strictly a pre-record
abandonment. Future native issuance requires private proven-event handling and
runtime ownership through callback drain/quarantine; this destructor is not a
submitted-work release path. Missing/Pending/Committed plan, owner32, exact/one-less/
exhausted quota, duplicates, forged control blocks, foreign original device and
quota aliases reject. Genuine shutdown, caller-drop retention, source count and
quota release, all exhausted-hot validation/refusal/destruction allocation0 pass.
A reusable one-shot quota fixture rebuilds genuine candidates before pressure.
The initial quota-zero assertion kept a test weak control block alive; the observer
is now released before checking allocator bytes, with no production budget change.
Null remains ValidatorOnly for native work; Vulkan/Metal/WebGPU native issuers and
execution remain DiagnosticFailure. CPU seal inspection is portable Prototype;
no broad Backend Validated, Platform Validated or Production Ready promotion.
Native resource closure validation and exact two proof issuer friends are retained.
Next: charge actual original Source submission/callback/registration capacity and
bind cold native admission to the exact closure/seal and original native loss
subscription. The existing private ordinary graph and CPU commit-before-submit
cannot substitute. Original facade shutdown alone never proves a submitted queue
is drained. No shader/visual changes; RenderDoc unavailable. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-recording-seal-evidence.yaml`
and `canonical-recording-native-admission-audit.yaml`, reports/logs/XML.


CanonicalOriginalLifecycleCapacity/1 checkpoint (2026-10-04): Contract Complete /
Prototype Complete cold Source capacity ownership only. Host16/16, Wasm/browser8/8,
inventory130/130, quick unit/Null3/3; both capacity/seal/lifecycle/native-closure/
original-plan browser flags true, clean reports. The original Source now embeds
three independently authored core atomic count budgets for pending submissions,
callback tickets and callback registrations. A quota-bound reservation retains the
exact original recording seal/root and aliases those Source-owned budgets; it never
reconstructs a pool, graph, resource control or journal. Metadata and shared owner
are charged to the independent original Submission CPU quota. Full retained
Submission owner capacity is34 (seal33 + reservation1); a live duplicate reservation
per seal refuses before allocation. Versioned explicit cold demand reserves one
single-root submission and declared nonzero callback/registration units, solely
as capacity intent. It cannot prove a native callback count or issue a native event.
Native lowering must independently derive and compare all actual requirements.
Core AtomicCapacityBudget.canAcquire reuses overflow-safe unsigned-range arithmetic
for non-reserving preflight; rejected valid capacity records its stable refusal.
All cold failures release partial leases and preserve the sealed Prepared root.
Genuine distinct roots sharing one original Source exercise1-4-2/2-3-2/2-4-1/2-4-2
budgets: each independent domain exhausts deterministically, used counts remain
unchanged on rejection, and release restores admission. Exact/one-less/exhausted
quota, owner33, zero/over-budget/version/opt-in requests, duplicate and forged
owners plus foreign genuine seal comparison reject. Caller-drop retains ownership;
original facade shutdown refuses validation but keeps units until pre-record
reservation destruction. Root/journal are unchanged, Source counts and quota bytes
release exactly, and all exhausted-hot validation/refusal/destruction allocate0.
Publication/destruction require external serialization per Source/seal; atomic
leases do not make wrapper interlocks concurrently callable. Null ValidatorOnly;
Vulkan/Metal/WebGPU native issuer/execution DiagnosticFailure. No native callback
registration or canonical GPU work is issued by this primitive. No broad Backend
Validated, Platform Validated or Production Ready promotion. Exact two native proof
issuer friends and generic/bindless/NativeSync gates are preserved. Next: bind the
exact original native loss subscription and backend-derived complete scope/callback/
command requirements, then retain the same closure/seal/reservation in native
runtime completion storage. No shader/visual changes; RenderDoc unavailable.
Evidence: `build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-lifecycle-capacity-evidence.yaml`.


OriginalNativeLossObservation/1 checkpoint (2026-10-04): Contract Complete /
Prototype Complete opaque exact subscription custody only. Host16/16,
Wasm/browser8/8, inventory131/131, quick unit/Null3/3; both browser modes
originalNativeLossObservationPassed and earlier canonical capacity/seal/closure
flags true, diagnostics/nativeErrors empty. Only the original GpuDevice issues
the read-only observation, retaining the exact domain and quota-bound actual
armed native loss binding without retaining the facade. Pointer plus shared-owner
identity and original facade identity refuse foreign observations even over the
same physical browser device. Default/unprepared observations, domain/subscription
quota mutations refuse. Copy/observe/validate/destruction allocate0 under exhausted
quotas; Retired and Shutdown remain distinct after facade drop. Actual original
browser device loss returns the original NativeLost diagnostic in retained
observation/device validation and refuses new observation; unrelated loss isolation,
pending canonical root/journal and rollback-no-revival tests remain intact. No
native handle, receiver, detach or GPU completion authority is exposed. Null
ValidatorOnly; Vulkan/Metal native lifecycle DiagnosticFailure; WebGPU genuine
subscription custody verified, canonical recording/submission execution remains
closed. No broad Backend Validated / Platform Validated / Production Ready
promotion. Next: genuine loss-before-registry plan recipe and backend-derived
complete cold requirements, then native runtime custody. No shader/visual changes;
RenderDoc unavailable. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/original-native-loss-observation-evidence.yaml`.


CanonicalLossBoundNativeClosure/1 checkpoint (2026-10-04): Contract Complete /
Prototype Complete exact native subscription resource custody only. Host16/16,
Wasm/browser8/8, inventory132/132, quick unit/Null3/3; both browser modes
canonicalLossBoundClosurePassed and earlier canonical flags true, reports clean.
The reusable genuine copy/pass plan recipe binds original native loss before the
registry and all resource factories. A selected closure validates the device-issued
observation against the exact original facade and registered canonical root before
cold allocation, then retains that subscription with the existing closure. Root
registry already retains its domain, so resource closure owner42 becomes43; native
resource bytes remain4224 and parent allocation dedup unchanged. Existing and bound
closure Wasm cold metadata now1708 (embedded opaque observation storage). Exact/
one-less/exhausted Recording quota, owner42, foreign facade over the same physical
native device, unissued/unprepared and mutated subscription quota refuse. Unbound
closures expose RHI-WEBGPU-CANONICAL-CLOSURE-LOSS-UNBOUND and remain inspection-only.
Caller-drop keeps exact root/subscription alive; facade shutdown retains its distinct
diagnostic until pre-record destruction. The actual original browser loss fixture
now uses the bound Buffer closure owner18, retaining pending journal, rejecting
commit and preventing rollback revival. Exhausted hot validation/release allocate0.
Null returns its declared ValidatorOnly loss refusal before registry order checks;
initial test diagnostic assumption was corrected without production changes and
failed log/XML preserved. Vulkan/Metal native lifecycle DiagnosticFailure; WebGPU
selected resource custody Prototype only. No encoder/queue/proof/completion is
issued here; generic/bindless/NativeSync and exact two proof issuer friends unchanged.
No broad Backend Validated / Platform Validated / Production Ready promotion.
Next: derive complete native command/scope/callback/registration requirements and
consume the exact Source reservations, then allocate native recording/completion
custody under independent quotas and implement private proven lifecycle events.
No shader/visual changes; RenderDoc unavailable. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-loss-bound-closure-evidence.yaml`.


CanonicalOriginalColdAdmission/1 checkpoint (2026-10-04): Contract Complete /
Prototype Complete immutable backend-derived cold admission only. Host16/16,
Wasm/browser8/8, inventory133/133, quick unit/Null3/3; both browser modes
canonicalColdAdmissionPassed and all prior mandatory flags true, clean reports.
Selected CopyPass/1 verifies exact original ten-command language and Prepared
canonical plan, exact loss-bound closure/root/seal/reservation and independent
CPU quotas. Recording encoder creation and commands share one native error-scope
batch; requirements are recording3 + submission3 + queue1 + runtime loss1 =8
allocated slots /8 new registrations, one pending submission. Existing original
device loss1 is a separately budgeted retained dependency and is never charged or
registered again. Actual native filter order/count and pure cold planner now share
WebGpuErrorScopeContract/1; shader validation consumes that count too. No ordinary
unused creation slots may be allocated or registered by this selected lowering.
Exact Source request1/8/8 is required; smaller or larger reserved demand refuses.
Retained owner union is closure43 + reservation34 - same root30 + admission1 =48;
Recording/Submission/Callback owner limits all checked. Metadata is charged to
exact original Recording quota. Forged requirements, overflow/inconsistent total,
unbound closure, forged owner, owner47, quota mutations, independent count limits,
exact/less/exhausted allocation and missing/foreign root provenance refuse.
Caller-drop preserves the same root and all units; facade shutdown retains units
until pre-record release. Hot validation/release allocate0 and counts/bytes release
exactly. Pure Wasm Null initially imported the modern native callback ABI; extracted
the pure scope contract instead, with failed build log preserved. Null ValidatorOnly;
Vulkan/Metal/WebGPU native lifecycle execution remains DiagnosticFailure. Requirements
are consumed by cold admission, but actual callback metadata/runtime loss observer
and native lifecycle issuers still need implementation. No broad Backend Validated /
Platform Validated / Production Ready promotion. Next: actual opaque device/queue
command shell custody and charged callback/runtime completion preparation. No shader/
visual output change; RenderDoc unavailable. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-cold-admission-evidence.yaml`.


CanonicalOriginalColdCommand/1 checkpoint (2026-10-04): Contract Complete /
Prototype Complete opaque cold native command custody. Selected browser WebGPU
device/queue connection is Backend Validated only for this cold payload. Final
host16/16, Wasm/browser8/8, inventory134/134, quick unit/Null3/3; both browser
modes canonicalColdCommandPassed and all prior mandatory flags true, clean reports.
A backend-private opaque wrapper and native command Impl retain the exact cold
admission/root/closure/seal/Source reservation. Real original device and actual
queue are connected without creating an encoder; immutable device/queue/recording
identity snapshots are revalidated. No native handle, mutable raw command or
recorded proof is exposed. All three additional objects (wrapper/shared command/
owned Impl) allocate under the exact root Recording quota; total owners51 and
Wasm cold bytes752. All three lifecycle owner limits and independent cold budgets
are checked. Every allocation and exact native connection completes before a
sticky claim is published on the original seal. Duplicate wrappers, peer admission
objects and replacement reservations on that same seal cannot issue another shell,
including after original shell destruction. Failed cold allocation leaves no claim.
Exact/less/exhausted allocations use rebuilt genuine one-shot roots; no budget
relaxation. Forged owner, owner50, single-payload/version/opt-in/CPU failures refuse.
Caller-drop retains root and Source1/8/8; facade shutdown fails validation without
releasing units. Actual original browser loss now also closes a genuine native
shell; all units remain until pre-record destruction, then counts and nine quota
bytes release exactly. Native device/queue references die before root/Source. Hot
validation/release allocate0. Generic native open explicitly rejects canonical
shells until private recording events are implemented. Null ValidatorOnly;
Vulkan/Metal DiagnosticFailure; WebGPU Prototype actual native cold custody with
recording/submit/completion unavailable. Exact two proof issuer friends and full
generic/bindless/NativeSync gates unchanged. Actual callback metadata/runtime loss
observer and completion storage remain pending; reserved slots do not prove issued
callbacks. No broad Backend Validated / Platform Validated / Production Ready
promotion. Next: allocate8 Callback quota slots and exact Submission completion
custody, bind charged runtime loss1 independently of facade loss, then proven private
lifecycle events. No shader/visual output change; RenderDoc unavailable. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-cold-command-evidence.yaml`.


### CanonicalOriginalCallbackCustody/1 verified checkpoint — 2026-10-04

Completed the cold eight-slot boundary: recording scopes3, submission scopes3,
queue1 and runtime loss1 allocate under the exact original Callback quota. The
opaque wrapper, runtime state, one-entry core retention pool and graph allocate
under independent original Submission. The retained graph owns the same original
cold shell/root/closure/seal/reservation and embedded journal. No old ordinary
private transaction is substituted. Admitted owner union62; measured Wasm cold
Submission348 and Callback160 bytes. The independently issued runtime device-loss
observer consumes the already reserved Source1/8/8 demand, without recharging the
existing original facade subscription. Seven scope/queue slots remain unarmed.

Failed CPU allocation or actual native binding publishes no custody claim and
unwinds all bytes. Native binding failure after actual promise registration
suppresses captured userdata before release. Successful cancellation/destruction
suppresses native userdata before releasing the graph. Failed suppression returns
a stable diagnostic and preserves bounded runtime retention through wrapper drop
until genuine physical loss. Facade Shutdown alone neither delivers runtime loss
nor proves queue completion. Actual browser loss independently releases live and
Shutdown custody; late callbacks from cancelled observers deliver0.

Validation: host16/16, Wasm/browser8/8, inventory135/135, quick unit/Null3/3,
pinned Vulkan header syntax. Both browser reports are clean and require
canonicalCallbackCustodyPassed. Contract Complete / Prototype Complete for this
cold boundary; Backend Validated only for selected browser WebGPU actual native
loss observation. Null ValidatorOnly; Vulkan/Metal DiagnosticFailure; WebGPU
explicit Prototype. No generic/original/bindless/NativeSync execution, encoder,
recorded proof, queue submit or broad Platform Validated / Production Ready claim.
No visual output change; RenderDoc unavailable. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-callback-evidence.yaml`.

Next: private sealed-root plan/pending/commit/invalidation operations and actual
Recording/Submission issuer callbacks against this same storage. Cold inspection
requires Prepared/no encoder; extend a separate proven live lifecycle boundary
without weakening that evidence. MaySubmit must precede the native queue call;
canonical journal commit must wait for exact queue and all submission scopes.

The final cancellation review also adds reusable core pool `canCancel` inspection
before native suppression. Exact invalid/stale/reused/submitted/quarantined/closed
tickets are tested without hot allocation; a submitted graph cannot lose its
observer through the pre-record cancellation entry. The next phase design audit
is `canonical-private-native-event-audit.yaml` in the same evidence directory.
It identifies sealed public rollback refusal, separate live validation, old
ordinary pre-queue commit and outstanding callback lifetime as required private
issuer integration work; it grants no executable native authority.


### CanonicalOriginalCopyPassLowering/1 verified checkpoint — 2026-10-04

Added exact cold native input projection under the same runtime custody. All copy
fields expand through shared `TransferCopyProjection`; explicit mip geometry,
pitch, offset, relative source/destination mip and layer values are preserved.
The selected browser path binds four single-mip operations to actual original
native payloads and maps complete color/resolve/depth attachment inputs through
shared validators. Whole CopyDst-to-ColorAttachment uses an explicit normative
WebGPU automatic-usage transition contract; empty/layered/subpass/dependency or
unsupported shape fails deterministically. No encoder or GPU work is issued.

One opaque wrapper and runtime-owned payload add two owners (union64); measured
Wasm Recording cold708 bytes. Exact bounded copy/color arrays allocate in original
Recording; inline custody pointer extends its cold Submission footprint to364
bytes, Callback remains160. Publication is sticky after all allocations/preflight;
failure unwinds and preserves retry. Cancellation/loss release borrowed native
descriptor storage before the original graph/root. Caller drop, Shutdown and real
physical browser loss preserve the original ownership chain and safe teardown.

Historical CopyPass checkpoint before NormalizedTextureStateCreation/1 (superseded
by the verified normalized creation checkpoint below): a genuine Null normalized
two-mip CopyPass graph/plan passes (cells6, steps15,
transition1). The actual WebGPU equivalent fails earlier at original graph creation:
its native texture source is uniform and the mip0 attachment view is partial,
producing `RHI-ORIGINAL-TEXTURE-STATE-PARTIAL-CONTROL-UNPROVEN`. This failure is
required and tested, with initial failure evidence retained. Pure CPU multi-mip
projection does not establish native multi-mip readiness. No view widening,
truncated mip data or fixture control substitutes for that missing native source.

Validation: host16/16, Wasm/browser8/8, inventory136/136, quick unit/Null3/3 and
pinned Vulkan header syntax. Both browser reports require canonicalCopyPassLoweringPassed
and have clean native diagnostics. Contract Complete / Prototype Complete for
cold input projection. Backend Validated only for selected browser single-mip
native bindings and actual multi-mip refusal. Null ValidatorOnly, Vulkan/Metal
DiagnosticFailure, WebGPU explicit Prototype. No native encoder/recorded proof/
queue completion, broad Platform Validated / Production Ready, generic/bindless/
NativeSync promotion or visible output change. RenderDoc unavailable. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-copy-pass-evidence.yaml`.

At that historical checkpoint, the next prerequisite was genuine creation-bound
normalized WebGPU texture state with
one authoritative control and authored bounded allocation, as audited in
`canonical-webgpu-normalized-state-audit.yaml`. Null/Vulkan/Metal source bindings
already use the shared normalized control; their native readiness is not inferred
from this audit. Then Recording/Submission issuers consume the exact same private
projection and existing callback slots against the sealed canonical journal.
# NormalizedTextureStateCreation/1

Creation owns one normalized control, bound exactly once to the original texture.
The exact descriptor fixes mip × array-layer × aspect cells; two fixed arrays
(current values and journal snapshot) require `2 × cells × sizeof(ResourceState)`
data bytes. Core allocator owner/header charges remain additional and use the
active creation quota. Invalid geometry/state, size overflow or allocation failure
rejects creation before publishing a native texture. No uniform placeholder,
lazy retrofit, second mutable tracker or recording-time allocation is allowed.

Whole-resource legacy consumers use an allocation-free borrowed view of that
same control. They require uniform values across every cell, including both
Depth24Stencil8 planes; nonuniform observations fail deterministically. Partial
canonical observations retain exact ranges and conservative whole-control
revisions. Whole writes update all cells atomically under the same journal;
rollback invalidates old stamps. Texture upload exclusivity remains independent
of state layout. Loss invalidates that single control before retained owners drain.

Null is a normalized validator; Vulkan and Metal bind existing normalized controls;
WebGPU must bind the native payload's normalized control at construction. These
CPU mappings grant no native recording/submission/completion or NativeSync proof.
Contract Complete describes these semantics only. Backend Validated requires
genuine native creation/state evidence; Platform Validated and Production Ready
remain unclaimed until their independent acceptance requirements pass.

NormalizedTextureStateCreation/1 is now Contract Complete / Prototype Complete for
selected creation-bound state and cold input projection (2026-10-04). WebGPU real
native payloads bind one common normalized control; borrowed whole consumers
share it without a shadow scalar. Exact descriptor cells fix current/snapshot
storage and core creation-quota charges. Nonuniform/missing whole observations
fail closed; allocation exceptions retain existing RESOURCE-ALLOCATION diagnostics
before native creation, and failed enabled-native creation cannot publish metadata.
Null's normalized validator, existing Vulkan/Metal mappings, actual browser
multi-mip/partial-view bindings, pending/rollback/stale/drop/loss, all-aspect whole
writes and hot allocation0 were assessed. Host17/17, unit Wasm6/6 plus final browser/
inventory3/3, inventory137/137, quick unit/Null3/3 and Vulkan syntax pass. Both
browser reports are clean; genuine mips2 graph cells6/steps15/transition1 projects
copies7 (Recording1020, owner64, Submission364/Callback160). Backend Validated is
limited to selected browser creation/state/bindings/loss. Canonical encoder,
recording, submission, completion, NativeSync, broad Platform Validated and
Production Ready remain unproven. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/normalized-texture-state-evidence.yaml`.
Next: private sealed-root plan mechanics and exact existing Recording/Submission
issuers. `RHI-RENDERDOC-TOOL-UNAVAILABLE`; no new visible output or GPU commands.

Canonical root application and commit now reuse private same-root mechanics;
public CPU methods retain `RHI-ORIGINAL-TRANSACTION-RECORDING-SEALED` checks.
Write/enlistment/final-validation failure directly rolls back the internal journal
and cannot be blocked by public sealed abandonment. The existing seal remains
cold Prepared-only and invokes neither helper; no proof friend, native callback,
encoder or queue execution was added. Contract Complete / Prototype Complete
shared CPU mechanics only: host17/17, unit/browser7/7 plus corrected inventory1/1
(138/138 source assertions), fast contract and Null hidden-frame paths pass. The
actual sealed-native caller and completion commit remain unimplemented, with no
new Backend Validated / Platform Validated / Production Ready promotion.
Evidence: `build/web-bindless-apple-debug/artifacts/bindless-continuation/root-mechanics-evidence.yaml`.
Continue private live ownership/validation and callback drain retention before
arming scopes or queue callbacks. `RHI-RENDERDOC-TOOL-UNAVAILABLE` remains explicit.


### Canonical terminal retention/drain prerequisite — 2026-10-04

TerminalRetention/1 closes original scalar/normalized controls before releasing
any graph. The root becomes Invalidated and discards saved journal snapshots
without restoring uncertain values or claiming successful completion. The root
retains its sole seal friendship; only custody's private terminal-loss helper
can reach this new seal operation. Public CPU mutation stays sealed.

Core CompletionRetentionPool now supports one-shot invalidateAndRetain and exact
releaseInvalidated tickets. The retained ticket refuses cancel/submit/resolve;
immediate invalidate cannot bypass its drain obligation. WebGPU native loss
keeps the exact lowering/shell/root until all gates are Unarmed, Delivered, or
Detached after native suppression. Armed/Delivering includes the current loss
callback. Scope/queue gates are never detached by the loss handler. Cancellation
refuses outstanding scope/queue userdata before detaching the loss observer.

Verified host8/8 and Wasm/browser8/8, inventory138/138, Null hidden-frame and
pinned Vulkan syntax. Core tests cover all128 issued-slot subsets before/after
submit, stale tickets, duplicate invalidation, refused cancellation/completion,
last-callback delivery and exhausted-quota hot allocation0. Genuine browser loss
permanently invalidates a caller-retained original root and releases unissued
custody with all nine quotas zero in both feature modes. The seven scope/queue
slots still remain unarmed: outstanding native scope/queue drain is not yet
Backend Validated and must be exercised by actual issuers in the next stage.

Contract Complete / Prototype Complete for this drain prerequisite; Backend
Validated only for selected browser loss invalidation/unissued release. Null
ValidatorOnly; Vulkan/Metal DiagnosticFailure; WebGPU explicit Prototype with
recording/submission/general/bindless/NativeSync gates closed. No broad Platform
Validated or Production Ready. RenderDoc: RHI-RENDERDOC-TOOL-UNAVAILABLE; no new
encoder, GPU command or visible output. Earlier immediate-loss-release evidence
applies only to unarmed cold storage and is superseded by this retained path.
Evidence: `build/web-bindless-apple-debug/artifacts/bindless-continuation/terminal-retention-evidence.yaml`.
Next: separate retained/live identity checks and actual Recording/Submission
issuers against the existing eight slots; MaySubmit-before-queue and completion
commit remain unimplemented.


### Retained ownership versus live native facts — implementation contract

Ownership inspection validates the exact original root, graph, registered source
lifetime, independent CPU domains, source budgets and retained owner bounds. It
may observe Prepared, owned Pending finals or Committed finals; that read-only
result grants no execution authority. Cold inspection additionally requires
Prepared, an empty journal and no encoder/finished/submitted command. The cold
seal, reservation and admission retain this restriction. Private native live
validation must use the ownership path and exact captured root/seal/reservation/
closure/command/device/queue/recording identity, require owned Pending finals,
and check encoder/finished/submitted payload consistency. Caller-supplied phase
enums or replacement ordinary completion reservations cannot stand in for facts.
Null remains ValidatorOnly and other backend execution gates remain closed until
actual issuer/callback evidence is validated. This split adds no native event API.


Retained/live inspection prerequisite verified — 2026-10-04: host8/8 and
Wasm/browser8/8, inventory139/139, fast contract and Null hidden-frame pass.
Shared ownership checks observe genuine Prepared/Pending/Committed CPU roots,
validate owned final seals and independent quotas, reject forged roots, stale
original descriptor bindings and Shutdown. Cold preparation/seal/reservation/
admission remain Prepared-only; the native cold shell additionally refuses
encoder/finished/submitted state. Private post-record live inspection checks
the exact captured admission/root/seal/reservation/closure, native command owner,
Recording quota, device/queue/recording identity, Pending journal and finished
payload. It uses no old ordinary completion reservation and emits no GPU work.

The same identity helper is exercised by real browser cold validation; the
private live post-record path has no actual issuer caller yet and is not Backend
Validated. Contract Complete / Prototype Complete applies only to shared
read-only ownership and cold identity reuse. No recording/queue evidence, native
completion authority, execution capability or Platform/Production promotion.
Both current browser reports have success=true and empty diagnostics/nativeErrors.
Proof friends remain exactly the two existing issuers; root friendship remains
solely the existing seal. No new allocations, shaders, encoder or native command
path. RHI-RENDERDOC-TOOL-UNAVAILABLE. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/retained-ownership-evidence.yaml`.
Continue cold allocation of actual Recording/Submission objects and evidence,
then scoped encoder, existing slots3/4, MaySubmit-before-queue and genuine
outstanding callback drain/loss. Parent canonical lifecycle and bindless plan
remain incomplete.


### CanonicalOriginalIssuerPreparation/1 — cold inventory contract

Prepare the existing concrete WebGpuOrdinaryRecording and WebGpuOrdinarySubmission
objects over the captured canonical native shell, with an opaque wrapper that
exposes no proof, issuer, native handle or event API. The exact additional owner
inventory is wrapper1 + Recording1 + Submission1 =3 (selected total67 after
lowering64). Inline recording scope3 evidence and existing submission queue+scope4
evidence add bytes only. No new command, pool, graph, state transaction, callback
slot or Source capacity is allocated; Source remains1/8/8 and seven slots Unarmed.

Recording metadata/share ownership charges original Recording. Submission and
wrapper metadata/share ownership charge original Submission. Both independently
charged peaks must be measured from core allocation headers, with exact/less/
exhausted admission tests. Budget/version/opt-in/owner/domain checks and all CPU
allocations precede sticky publication and encoder. Failed candidates unwind
without changing existing custody or publishing a claim; cold retry is allowed.
State retains both issuers, which hold weak State links. Cancellation/loss resets
Submission then Recording before borrowed lowering/graph release. Public ordinary
record/submit paths refuse canonical mode before dereferencing the old completion
reservation; preparation neither manufactures a proof nor satisfies native sync.

Null ValidatorOnly; Vulkan/Metal DiagnosticFailure; WebGPU explicit Prototype
issuer storage only. Native scoped recording/submission/completion, actual
outstanding callback drain and Platform/Production acceptance remain incomplete.
Required paths are fast contract/unit, Null hidden-frame and both genuine browser
feature modes, covering quota failures, repeat/forged preparation, caller drop,
Shutdown, cancellation, real loss and hot allocation0.


### CanonicalOriginalIssuerPreparation/1 verified checkpoint — 2026-10-04

Cold preparation now allocates the actual existing Recording and Submission
concrete classes over the captured command, with an opaque non-proof wrapper.
Additional owner inventory3 yields selected total67. Measured Wasm incremental
Recording316 and Submission280 bytes include actual core metadata/share owner
charges; Callback allocation0 and extra Source demand0. Existing Source1/8/8 and
slots3/3/1/1 are unchanged. Recording's inline scope evidence and Submission's
existing inline queue/scope evidence are bound to the slot counts at compile time.

Both issuer links to State are weak. Native loss/cancellation releases Submission
then Recording before borrowed lowering and retained graph. All fallible CPU
allocations precede sticky issuer publication; failed Recording/Submission
quota probes unwind to the original baselines, leave the root Prepared and
allow cold retry. No old ordinary completion reservation, replacement command
or state transaction is created. Ordinary record/submit deterministically refuse
canonical mode; status/diagnostic inspection never dereferences an absent old
completion owner. The wrapper exposes neither actual issuers nor recorded proof.

Verified host11/11 (including retained allocation, WebGPU memory/staging and Null
hidden-frame), Wasm/browser8/8 and inventory140/140. Both genuine browser modes
report canonicalIssuerPreparationPassed=true, clean diagnostics/nativeErrors,
exact/one-less/exhausted independent quotas, repeat/forged/owner/domain rejection,
cold retry, caller-drop/Shutdown and physical loss with all nine quotas zero.
All hot validation/closed-entry/drop/loss paths allocate0. The initial quota-test
baseline omitted the pressure lease; that fixture assertion was corrected and
its failed reports preserved without changing production budgets or paths.

Contract Complete / Prototype Complete for cold issuer storage only. Backend
Validated remains limited to original browser native bindings and loss teardown;
no actual canonical encoder/scopes/queue completion evidence exists yet. Null
ValidatorOnly, Vulkan/Metal DiagnosticFailure, WebGPU explicit Prototype cold
storage; recording/general/bindless/NativeSync execution gates remain closed.
No Platform Validated or Production Ready. RHI-RENDERDOC-TOOL-UNAVAILABLE; no new
shader, encoder, submitted GPU work or visible output. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/issuer-preparation-evidence.yaml`.
Continue exact sealed-root application and actual encoder/recording scopes using
these already allocated issuers and slots; MaySubmit and native completion
commit remain subsequent required work. Parent lifecycle and bindless plan stay
incomplete.


### CanonicalOriginalScopedRecording/1 checkpoint — 2026-10-04

A separate version1 Prototype opt-in now drives the existing real Recording
issuer over the original sealed root/native command and CopyPass/1 projection.
Private application/rollback passes only through the seal; root friendship and
public CPU mutation refusal remain unchanged. Three existing role/index/ticket
bound recording slots consume real scoped encoder creation, ordered mip copies,
empty render pass and native finish. The actual proof issuer reports Recorded
only after all scopes succeed and drain, userdata clears and private live
validation proves exact native identity and unchanged owned Pending finals.
Cold Prepared/no-encoder inspection remains strict. No replacement transaction,
command, completion reservation, callback allocation or Source demand is added.

Pre-submit error/cancel/drop/Shutdown retain through issued userdata then roll
back once. Real device loss invalidates first, retains while callbacks are Armed
or Delivering and releases the original issuer/lowering/graph only after drain.
Hot recording, callbacks, validation and teardown allocate0 under all nine
exhausted independent quotas. The native-error negative probe produces a genuine
GPUValidationError on the same encoder; no callback or proof is fabricated.

Null ValidatorOnly, Vulkan/Metal DiagnosticFailure, WebGPU explicit Prototype
recording only. Queue, MaySubmit, original completion commit, general/bindless
execution and NativeSync remain unavailable. Contract Complete / Prototype
Complete recording only; Backend Validated selected real browser recording
payload/scopes and loss drain. No full Platform Validated or Production Ready.
No shaders or visible output changed; newly recorded buffers are unsubmitted.
RenderDoc unavailable: RHI-RENDERDOC-TOOL-UNAVAILABLE, no visual acceptance claim.
Authoritative validation is in
`build/web-bindless-apple-debug/artifacts/bindless-continuation/scoped-recording-evidence.yaml`.
Continue c2b4 MaySubmit-before-queue and Submission4 evidence/commit; c2b/c2 and
full bindless readiness remain incomplete.

Verified recording checkpoint: host12/12, Wasm/browser8/8 and inventory142/142.
Both genuine feature modes report canonicalScopedRecordingPassed=true,
diagnostics=[] and nativeErrors=[]. Seven native probes cover success, real scope
error, cancellation, caller drop, Shutdown, changed native binding and reversed genuine scope promises. Each
physical loss observer sees3 Armed recording scopes, then invalidates the root
and drains to all-nine-quotas-zero. Source1/8/8 and owners67 stay unchanged;
current measured cold issuer increments remain Recording316/Submission280 bytes.
Queue invocation and original completion commit are still absent.

CanonicalOriginalQueueSubmission/1 now defines the next independent explicit
intent and MaySubmit-before-queue/commit-after-evidence contract. All four backend
submission/commit/NativeSync execution gates remain closed until real native
payloads are implemented and tested. Null is ValidatorOnly; Vulkan/Metal/WebGPU
are DiagnosticFailure for this submission contract. Existing ordinary submission
commits before queue invocation, so canonical integration must use a separate
private path over the original issuer, ticket, root and Submission4 evidence.
Post-MaySubmit uncertainty/cancel/Shutdown invalidates and retains; successful
queue receipt alone cannot authorize commit. This is Contract Complete planning
only, with no submission Prototype Complete, Backend Validated, Platform
Validated or Production Ready claim. Continue b4b actual native integration.

### CanonicalOriginalQueueSubmission/1 — verified selected private lifecycle

The previous closed-submission checkpoint is superseded for explicit selected
WebGPU Prototype submission. The original Submission issuer consumes the exact
Recorded proof/root/device/queue/non-recycled ticket and publishes pool submitted
retention and MaySubmit before native queue invocation. Only proven invocation,
successful queue plus three scopes, drained userdata, unchanged original Pending
finals and a live loss registration permit private seal commit. Root friendship
remains exclusive to the seal; public mutation and generic/native proof gates stay
closed. Original Source1/8/8 and core owners67 are unchanged. No old ordinary
reservation, replacement transaction, commit-before-queue or post-submit rollback
is used.

NativeCompletionReceipt/1 cold-prepares four foreign states, eight listeners and
four ownership handles in the existing slots. Native phases Prepared/Bound/
Delivered/Suppressed clear userdata before delivery or suppression. A partial then
registration cannot leave usable userdata. Suppressed queue listeners prove no
GPU termination: after registration refusal the exact root/native shell/lowering
stay quarantined until physical loss and complete userdata drain. Thrown native
scope pop closes subsequent canonical recording for that physical device until
replacement. Post-MaySubmit errors/cancel/drop invalidate and retain. Logical
Shutdown closes original lifetime tickets synchronously; the first private issuer
observation invalidates the pending journal while outstanding callbacks retain it.

Final host11/11, Wasm/browser8/8 and inventory146/146 pass, including fast
contract/unit and Null hidden-frame. Both genuine browser feature modes have
empty diagnostics/nativeErrors. Ten native submission probes verify successful
commit, thrown invocation before/after actual native submit, genuine scoped
GPUValidationError, reversed real promises, partial scope registration, cancel,
drop, Shutdown and changed binding. Native destination-buffer readback checks
seeded mip0/mip1 pixel rows and the original buffer-copy gap. Four physical loss
probes verify observer4 outstanding slots, queue-registration retention after scope
drain, genuine pop-then-throw with observer3 remaining slots, and deterministic
subsequent-recording refusal. Final all-nine-quotas-zero and core hot allocation0
are verified. Current Wasm issuer increments remain Recording316/Submission280;
custody measured cold peaks are Submission388/Callback384.

Null remains ValidatorOnly; Vulkan/Metal return deterministic DiagnosticFailure;
WebGPU selected version1 intent is Prototype. General/bindless execution and
NativeSync remain closed. Browser-managed promise/reaction/submit-array byte
budgets are unavailable: fixed foreign counts are documented, but no foreign-byte
or Production Ready claim is made. Contract Complete / Prototype Complete is
restricted to selected CopyPass private lifecycle. Backend Validated is restricted
to actual browser WebGPU queue/scopes/output/uncertainty/loss. Full Platform
Validated and Production Ready are not claimed. RenderDoc tools are absent on
this host (RHI-RENDERDOC-TOOL-UNAVAILABLE); transfer readback is data evidence,
not RenderDoc visual acceptance. No capture or visual acceptance is claimed.

Evidence: `build/web-bindless-apple-debug/artifacts/bindless-continuation/queue-submission-evidence.yaml`
and `canonical-private-native-lifecycle-completion-audit.yaml`, with final
reports/log/XML and a saved RenderDoc diagnostic. The initial full smoke failed
because a new pre-destroy wait allowed old mapping probes to complete; moving
that wait to early cold preparation preserves the original loss assertions and
all production semantics. The failed report remains available. Enclosing selected
cold/private lifecycle tasks are closed; broader original stream/native parent and
full bindless plan remain open for the next scope audit.


## Original stream compilation and selected projection traversal (2026-10-04)

`OriginalCommandStreamCompilation/1` compiles the complete supported immutable
`OriginalCommandInputs` profile into pointer-free ordered operations. Each operation
binds its original command, sparse descriptor, reference span, and family-specific
expansion ordinal. Scalar/dependency operations have no expansion; copy elements,
buffer uploads, texture upload/readback mips and attachment references use separate
ordinal spaces. Explicit descriptors and existing `TransferCopyProjection` and
upload/readback layout contracts determine counts. No extent, pitch, staging content,
native resource generation or completion identity is guessed.

Independent opted-in limits cover commands, descriptors, references, copy elements,
buffer uploads, upload/readback mips, render passes, attachment references, buffer
upload **declared** bytes, texture upload CPU pixel bytes, readback **declared** bytes,
CPU closure owners and metadata. Repeated commands charge repeated work/declared
bytes even when the same original snapshot is retained once. Source and Graph quota
aliases are rejected; materialized program metadata uses an explicitly bounded
Recording quota within the original Source's authored Recording ceiling. CPU owners
are the original Inputs closure plus the program, not native registration owners.

Existing input/order/provenance validation runs first. `PreflightReplay` validates
all operations/counts before cold allocation or the first CPU projection visitor.
The immutable program retains the exact original Inputs; it does not recreate the
root, state graph or journal. `OriginalCommandAccessValidation`, the original state
plan, native closure, issuer, scope/queue/loss bindings and independent Source,
Recording/Submission/Callback/Result/staging budgets remain separate prerequisites.
Zero disables an absent family. A budget failure publishes no partial program and
invokes no projection visitor. Visitor failure stops subsequent consumption and
invalidates the whole cold projection; partially built native work is never submitted.

`OriginalStreamSelectedProjectionTraversal/1` permits the existing private selected
CopyPass lowering to consume this allocation-free operation traversal and exact
sparse descriptor indices. Source-authored command/descriptor/reference limits,
selected admission's render-pass limit and independently authored native copy limit
form its traversal budget; upload/readback categories remain disabled. Every native
copy expansion must agree with the operation's first ordinal/count. This traversal
owns no additional retained program or callback and cannot widen the exact selected
language or either proof issuer. It creates no GPU work.

Four complete-program executor outcomes are explicit: Null is ValidatorOnly;
Vulkan, Metal and WebGPU report stable DiagnosticFailure (`*-EXECUTOR-MISSING`).
The existing private WebGPU CopyPass executor remains separately versioned and
selected. CPU compilation does not promote capability, NativeSync or generic/bindless
execution. Missing native initialized buffer staging, packed texture staging,
readback mapping/publication, draw/dispatch/query bindings and complete ordered
native executors remain unfinished.

Readiness: **Contract Complete** and **Prototype Complete** apply only to CPU stream
compilation. Genuine browser-resource smoke validates ownership/count traversal;
it does not establish complete-program **Backend Validated**, **Platform Validated**
or **Production Ready**. Selected queue/output/error/loss evidence is separately
required for native projection integration. No shader or visual output is changed
by compilation; unavailable RenderDoc capture remains an explicit acceptance gap.


Verified checkpoint: final host **11/11**, Wasm/browser **8/8**, source inventory
**148/148**. Both genuine feature-mode browser reports require
`originalStreamProgramPassed` and retain `canonicalSubmissionPassed`; diagnostics and
nativeErrors are empty. Selected native projection uses shared complete preflight
and sparse operation indices; expansion ordinals/counts match native projection.
No additional retained native owner or callback was introduced. Actual destination
mip/buffer readback and existing submit/error/cancel/drop/physical-loss probes pass.
Program quota object/control-block/non-owning aliases reject; all independent count
refusals run before allocation/visitor, and hot validation/traversal allocates zero.
Host program cold peaks: ten-command single/multi-mip 928 bytes, nine-command
ordered two-pass 872, five-command upload/readback 648; these are allocator
measurements, not native memory estimates. A six-byte copy can compile fields but
fails the separate portable access contract and remains execution-closed.

Evidence: `build/web-bindless-apple-debug/artifacts/bindless-continuation/stream-program-evidence.yaml`
and `stream-program-final-*` reports/logs/XML. `stream-program-renderdoc-diagnostic.yaml`
records the unavailable capture tool. No RenderDoc visual acceptance is claimed.
The next audit (`canonical-original-ordered-executor-next-audit.yaml`) identifies
that native admission still selects exact ten commands and the recorder still
collects copies before one empty pass. Ordered per-pass native payloads/count
requirements, initialized original upload staging and genuine original readback
completion/map/publication remain required. Enclosing/full bindless tasks stay open.


## OrderedOriginalCopyAttachment/1 and explicit cold/lowering /2

Cold admission /2 and copy/attachment lowering /2 select this profile with explicit
Prototype opt-in. /1 preserves its exact ten-command CopyPass vocabulary and rejects
/2-only budget fields. /2 permits only original transfer-pass boundaries, four copy
families, whole texture transitions, empty attachment-pass boundaries and Finish.
Existing original input/order/access/state validators remain mandatory. Unsupported
upload/readback/draw/dispatch/query/debug/alias/buffer-barrier families fail before
GPU work; no empty native payload stands in for them.

Original commands and render passes are derived from the complete immutable stream,
with independent authored command, expanded copy, pass and attachment-reference
bounds. Exactly one real encoder and command buffer serve the complete stream.
Recording scopes3, submission scopes3, queue completion1 and runtime loss1 consume
original capacity1/8/8; original device loss1 stays separately owned and charged.
Pass descriptors/colors/depth fields are backend-private, bounded and cold-allocated
under the exact root's Recording quota. Native per-pass descriptors must retain
stable pointers after their bounded storage is finalized. No new root/journal or
callback owner is introduced. Actual foreign native wrapper byte usage remains a
Prototype limitation; bounded creation counts do not claim quota-metered JS bytes.

Every native command consumes its original ordered operation. Copy native expansion
must match the original ordinal/count. Each BeginRenderPass selects its own immutable
original attachment descriptor; EndRenderPass closes that exact native encoder.
Finish occurs only after the validated complete sequence. Cold allocations finish
before recording; no per-draw pipeline/shader creation, file/network IO or blocking
GPU wait is added. Empty transfer boundaries are L2 grouping on the same native
encoder, not fake completion. Empty attachment passes perform real load/store/clear.

Whole texture transitions validate exact complete CopySrc/CopyDst/ColorAttachment/
DepthAttachment scopes (or explicit Undefined discard) and matching aspect/range.
WebGPU's normative automatic ordered GPU usage provides Equivalent mapping on these
states; the canonical original journal owns pending/final logical states. This
mapping is not a CPU synchronization primitive and cannot promote NativeSync.
Unsupported ranges/scopes/formats/native bindings reject deterministically.

Four profile outcomes: Null ValidatorOnly; Vulkan/Metal DiagnosticFailure with
stable payload-missing IDs; WebGPU explicit private Prototype using the existing
original issuers, scope/queue receipts, uncertainty quarantine and loss lifetime.
General full-program/bindless execution remains closed. Original /1 and /2 admission
and lowering versions must match. Successful metadata inspection cannot record,
submit, commit, publish results or replace missing original source/completion payload.

Acceptance must cover actual variable counts, multiple/interleaved passes/copies,
original native output/order, error/quarantine/loss and all independent quotas.
Contract Complete refers only to this defined profile; Backend Validated requires
actual native evidence. Platform Validated/Production Ready require broader backend,
platform and RenderDoc acceptance; unavailable capture is recorded explicitly.


2026-10-05 verification checkpoint: OrderedOriginalCopyAttachment/1 selected
WebGPU executor is Contract Complete / Prototype Complete / Backend Validated.
Host11/11, Wasm/browser8/8, inventory150/150 passed. Genuine original18 copy3
pass2 gives exact red-clear/copy-red then green-clear/copy-green texels; transfer
only7/copy7/pass0 gives actual queue completion. Native second-pass usage error,
post-submit cancel quarantine, caller-drop, and actual device loss with four
outstanding receipts pass. All nine quotas retire to zero; cold peak1684 passes
exact/one-less/exhausted plus retry and hot core allocation0. Prior /1 lifecycle
probes remain successful. Evidence: ordered-copy-evidence.yaml, final host/web
logs/XML/reports in build/web-bindless-apple-debug/artifacts/bindless-continuation.
RenderDoc unavailable (ordered-copy-renderdoc-diagnostic.yaml); complete Platform
Validated / Production Ready and full bindless execution remain unclaimed.

CanonicalOriginalUploadStaging/1 defines the next L2 prerequisite. It derives
required initialized buffer bytes from exact original BufferUpload declarations,
and packed texture bytes from the original immutable snapshot and destination
layout through the shared TextureUploadLayoutValidation Mapper. Packed row/mip
padding is normative; raw pixel bytes are a separate CPU retention count. Buffer,
packed texture and aggregate staging byte budgets are explicit independent limits.
The inspection allocates0 and publishes no partial requirement on any refusal;
its values provide no initialization/registration/recording/completion authority.
Four outcomes are Null ValidatorOnly; Vulkan/Metal/WebGPU DiagnosticFailure with
stable JOIN-MISSING diagnostics. Selected cold/lowering /2 still rejects uploads.

Native binding must retain exactly the original registered canonical object,
lease, allocation and generation. Backend-owned source initialization must seal
host/GPU writes, prove actual native byte layout and drain genuine initialization
queue/error receipts before source Ready. Buffer frame labels and texture caller
generation fields cannot issue native provenance. Texture binding must use the
exact snapshot/packed mapping/destination and genuine content-ledger expectation.
Every added native source/allocation/callback/CPU payload/resource owner requires
an authored count/byte domain; it cannot silently enter existing Source budgets.
Pending, foreign, missing, stale or aliased sources fail before record; there is
no automatic retry, fallback source, replacement canonical root or second journal.
Sources stay retained until actual canonical execution receipts drain or physical
loss invalidates them; post-submit uncertainty/cancel cannot release GPU owners.
The existing initialized staging/source/packing modules must be extended rather
than duplicated; audit canonical-original-upload-staging-next-audit.yaml identifies
the missing canonical registration and unique object adoption boundary.


Upload requirements verified 2026-10-05: host11/11 (fast contract and Null
hidden-frame included), Wasm/browser8/8, inventory151/151. Original buffer fixture
uploads2 requires16 native initialized bytes; texture fixture uploads2/mips4
retains40 CPU pixel bytes and requires1536 packed staging bytes. Exact/one-less/
invalid/missing/version/opt-in refusals publish no partial requirement; inspection
allocates0. Both real browser feature modes report requirements passed and prior
ordered profile/lifecycle probes remain clean. Evidence: upload-staging-requirements-
evidence.yaml and corresponding host/web logs/XML/reports. Contract Complete /
Prototype Complete read-only byte planning only; initialized canonical upload joins
still fail closed on all native backends. Upload Backend Validated, complete Platform
Validated and Production Ready remain unclaimed. Next: extend existing initialization
with actual canonical registration/lease/object/allocation binding, then texture
content-generation binding, then a new explicit ordered upload execution profile.


InitializedCanonicalBufferStaging/1 (2026-10-05 implementation contract): the
private factory adopts one actual GpuDevice-created, canonical registered buffer
under its original native-loss domain. It requires the exact complete CpuToGpu
CopySrc|CopyDst layout, committed initial CopySrc state, full payload size equal
to the genuine allocation footprint, and unsealed writes. Cold metadata, frozen
payload, original resource object, lease and callbacks have distinct explicit CPU
quotas; per-source native buffer1/retained bytes and owner bounds are independent
retention admission, while the genuine device allocator owns actual GPU capacity.
All inspection and cold allocations precede one-shot lease adoption/write sealing.
There is no replacement resource/registration, relabeled old source or size-only heap.

Initialization publishes MayWrite before a guarded real queue.writeBuffer call.
Three real error-scope receipts and one queue receipt must prove success before
Ready; one independent native loss subscription retains uncertain work. Existing
core ExternalCallbackGate/CompletionEvidenceBatch and native completion/loss
adapters own every foreign callback. Registration suppression is never queue
completion: unknown write/queue registration quarantines until true receipts or
physical loss establish lifetime. Cancel/caller drop before write detaches cold
callbacks; after write it retains owners until receipts drain or actual loss.
Native error never publishes Ready, and original logical shutdown/retirement/loss
closes validation even while exact owners remain retained. Frozen bytes and native
lease/payload/allocation/registration generation must still match at Ready joins.
Buffer frame serial is only an exact declared field comparison, never authority.

Four outcomes: Null ValidatorOnly; Vulkan/Metal payload-missing DiagnosticFailure;
WebGPU explicit private Prototype initialization payload. No public bindless or
NativeSync capability promotion, recording proof, root/journal, destination write,
texture generation or readback result authority follows from source Ready. Foreign
JS wrapper/queue internal bytes remain outside core CPU accounting and are an
explicit Prototype limitation. Backend Validated requires genuine initialized
bytes plus error/cancel/drop/quarantine/loss evidence, not this contract declaration.

CanonicalTextureUploadPackingSource/1 and CanonicalTextureUploadStaging/1
(2026-10-05 implementation contract): L2 binds the exact original texture upload
binder, snapshot owner, destination lease and immutable normative packing. Mip
count, tight pixel bytes, packed bytes and retained owners are independent bounds.
Packing is produced by the existing Mapper/copyRows path; no second layout or
padding algorithm exists. Wrapper, Source, Graph, packing metadata and packed-byte
CPU domains are independent. Every refused preflight allocates0; failed native
joins issue no mapping output.

The WebGPU private staging wrapper retains one genuine canonical initialized
buffer created through GpuDevice and the existing InitializedCanonicalBufferStaging/1
receipt owner. Its frozen bytes must equal every packed byte (including padding),
with exact size/frame equality. Ready requires actual queue plus three scopes,
original native loss, exact destination registration/lease/native allocation and
captured committed resource-state expectation. Foreign owners, altered bytes/frame,
pending/stale target state or changed native layout fail closed. Mapping reuses
TextureUploadPackedCopyMapping and compares the exact original packing-source owner.
No new callback slot, shader, native command, issuer or mutation journal is added.

This is staging provenance only. The destination state expectation's factory-issued
registration generation is a resource lifetime; UploadTextureDesc.resourceGeneration
and mapped contentGeneration remain authoring equality labels. They cannot prove
GPU target content. The old TextureUploadContentTransaction uses its own journal
and commits before the canonical completion boundary, so it cannot implement this
join. A genuine target content expectation integrated with the existing canonical
root/journal and consumed by the two existing native issuers remains required.
All content execution gates remain closed, including WebGPU's stable
RHI-CANONICAL-TEXTURE-STAGING-CONTENT-EXPECTATION-MISSING diagnostic.

Four outcomes: Null ValidatorOnly; Vulkan/Metal DiagnosticFailure payload-missing;
WebGPU explicit private Prototype staging join. Contract Complete/Prototype Complete
may refer only to this limited staging binding after tests. Backend Validated needs
real packed-byte readback and physical-loss evidence. Platform Validated/Production
Ready, generic/bindless/NativeSync and ordered texture upload execution remain
unpromoted. RenderDoc is unavailable on this host; native byte readback grants no
visual acceptance. Foreign browser allocation byte sizes remain explicitly unmetered
Prototype work, with no hot core allocation, IO or compilation.

CanonicalTextureUploadStaging/1 additional admission: wrapper metadata cannot alias
any of the five initialized-buffer CPU domains or any retained packing-source CPU
domain, including packed storage. One native target and its actual destination
texel footprint have an explicit independent limit; source packed bytes cannot pay
for target retention. These are retention bounds; original backend allocators still
own genuine allocation capacity. State expectations are not content expectations.

CanonicalTextureUploadStaging/1 verified 2026-10-05: host11/11 (quick contract and
Null hidden-frame included), Wasm/browser8/8, API inventory155/155. Real canonical
staging768-byte readback validates two mips/20 tight pixels and every zero-padding
byte against independent expected rows. Cold exact/one-less/exhausted/retry,
foreign/forged/quota/byte/count/pending/stale/layout refusal and real physical-loss
four-receipt drain/all14 quotas zero pass; hot core allocation0. Rollback invalidates
prior expectations even when state values restore. Evidence canonical-texture-
staging-evidence.yaml and final host/web logs/XML/reports in bindless-continuation.
Contract Complete / Prototype Complete / Backend Validated selected staging only.
Factory-issued target content ownership/journal integration and ordered uploads
remain incomplete; content execution fails with the stable expectation-missing
id. No broad Platform Validated/Production Ready or visual acceptance; RenderDoc
unavailable diagnostic saved. Continue canonical-texture-content-next-audit.yaml.


CanonicalTextureContentCreation/1 and CanonicalTextureContentPlan/1 (contract):
The explicit L2 factory accepts one new whole-texture content domain per creation
call, an authored texel footprint/generation ceiling and an independent CPU content
quota. Ordinary TextureDesc never opts in. WebGPU uses the genuine canonical device
resource factory, allocation, registration and immutable OriginalUploadExclusive
native access policy. It never relabels an older texture. Null is ValidatorOnly with
creation unavailable; Vulkan/Metal return payload-missing DiagnosticFailure;
WebGPU exclusive creation is explicit Prototype, native content execution remains
unavailable. Fresh Uninitialized/generation0 expectations describe domain ownership
only, never initialized GPU texels or successful asynchronous validation.

Full exact original uploads enlist that texture-owned control in the existing
OriginalCommandStateTransaction journal, with explicit cold plan control/upload
bounds and additional retained-owner capacity. Normative upload Mapper/extent
validation rejects partial regions/ranges. Control-issued monotonic generations are
independent of resource registration, state revisions and caller generation/frame
labels. Rollback restores the value while consuming a new revision and never
recycles issued generation IDs. Pending/stale/foreign/unissued expectations fail
closed; public CPU commit refuses content plans. Only the existing seal's private
native completion path can finalize Completed in the same state/content journal,
after genuine queue/error receipts. No additional proof issuer or independent
content transaction is introduced. Upload execution integration and genuine target
output remain open; do not mark b3b complete from creation/CPU proposals. No broad
Backend Validated, Platform Validated or Production Ready claim.

CanonicalTextureContentCreation/1 and Plan/1 verified 2026-10-05: host11/11,
Wasm/browser8/8, inventory156/156. Creation content216 and root plan2036 cold
peaks; exact/one-less/exhausted/retry, resource/registry and independent domain
failures publish no partial resource/plan. Genuine exclusive canonical native
creation, unissued ordinary/foreign/stale expectations, partial range refusal,
generations1/2 then rollback/drop3/4 and exhaustion pass; public commit refuses.
Actual physical loss invalidates observation; all five creation quotas zero, hot
allocation0. Contract Complete / Prototype Complete selected creation/CPU proposals;
Backend Validated selected WebGPU exclusive creation only. Native target texels,
queue/scopes content completion and ordered upload execution remain unproven/open.
Evidence canonical-content-creation-evidence.yaml and final host/web logs/XML/reports.
RenderDoc unavailable; no broad Platform Validated or Production Ready claim.


CanonicalTextureContentStaging/1 prerequisite: a backend-private composed wrapper
retains the exact existing initialized canonical texture staging and a fresh
factory-issued destination content expectation. Its separate metadata quota cannot
substitute any staging/packing/input/buffer domain or content control quota. A
bounded retained-owner closure pays for the new wrapper and exclusive content
control. Ordinary/unissued targets, pending/stale/foreign/forged sources, whole-range
violations and quota pressure fail before publication. Source packed bytes, target
state revision, factory registration generation and content generation remain
separate. The wrapper validates committed content ownership only; after same-root
proposals it refuses public ready observation. It issues no GPU command, completion,
new journal or proof issuer. Ordered native upload integration remains closed with
RHI-CANONICAL-TEXTURE-UPLOAD-ORDERED-EXECUTION-MISSING on a valid prerequisite.
Null is ValidatorOnly; Vulkan/Metal are payload-missing DiagnosticFailure; WebGPU
is explicit Prototype prerequisite only. No target texel/output readiness claim.

CanonicalTextureContentStaging/1 verified 2026-10-05: host11/11 (quick contract
and Null hidden-frame), Wasm/browser8/8, API inventory157/157. Metadata peak124,
exact/one-less/exhausted/retry; all sixteen CPU domains independently checked,
foreign-device/content/forged-source/not-ready/version/opt-in/quota/owner refusal.
Same-root proposals return Pending; rollback consumes revision and wrapper remains
stale. Real physical loss refuses observation; sixteen quotas zero, hot allocation0.
Contract Complete / Prototype Complete selected prerequisite; Backend Validated
selected WebGPU initialized source/exclusive target binding only. Ordered upload
execution and genuine target content queue/scopes completion remain closed/open.
Null ValidatorOnly; Vulkan/Metal payload-missing. Evidence canonical-content-
staging-evidence.yaml and final host/web logs/XML/reports; continue
canonical-ordered-upload-next-audit.yaml. RenderDoc unavailable; no broad Platform
Validated or Production Ready claim. Parent content-completion task remains open.


OrderedOriginalUpload/1 defines the next explicit canonical native profile. It
preserves the original complete positional stream (ordered copy/empty attachment
vocabulary plus BufferUpload, TextureUpload and exact transfer BufferBarrier).
Upload staging, native copy-command work, texture-upload command counts and
texture-destination write texel work have independent authored limits. Destination
write work is summed per upload; it is not physical/resident allocation or unique
target retention. Native source/target allocation ownership must be separately
bounded by the same-root native closure and initialized-source join.

Inspection reuses OriginalCommandStreamProgram, OriginalCommandUploadStagingContract,
whole-texture upload validation, normative destination footprint and budget ledger.
All counts remain local until the complete stream/profile validates. Caller frames,
resourceGeneration and a CPU access plan never issue initialization/content/native
recording authority. Null is ValidatorOnly; Vulkan/Metal are payload-missing;
WebGPU remains explicit Prototype with native upload joins/record/execute closed
until the full native payload is implemented and tested. Existing /1 and /2 native
profiles do not inherit uploads. No default opt-in, second journal or third issuer.

Implementation must bind each upload ordinal to the exact original registered
initialized buffer lease/payload/allocation and exclusive target expectation.
Recording scopes validate every actual target copy use before queue admission;
an unvalidated creation handle cannot authorize submit. Whole-texture content
proposals enlist the original root journal; only successful real record scopes,
submission scopes, invocation and queue completion publish Completed. Pre-submit
failure drains then rolls back with new revisions/non-recycled IDs; post-submit
uncertainty/cancel/loss invalidates and quarantines. Native output evidence must
cover all destination bytes/mips and genuine lifecycle paths under authored
budgets, with hot allocation0, fast contract and Null/browser smoke. Target output
completion and full b4 remain open until those proofs exist.

OriginalInitializedUploadJoin/1 stores bounded cold entries/native observations in
the original Recording quota, retaining the same root and already created upload
proofs. It must cover every original upload ordinal exactly once, match original
buffer binders or texture packing sources, and require initialized staging leases
as explicit entries of the original canonical Source inventory. Genuine source and
destination allocations have independent deduplicated native byte/object bounds;
Source maxNativePayloadObjects/maxNativeStagingBytes remain execution admission
requirements. Owner capacity uses an explicit conservative per-entry closure bound,
including repeated proof references, never an observed native memory estimate.
No native creation/write/encoder/queue/callback occurs in join publication.
Prepared validation closes on pending/stale/root mismatch, changed payload/layout,
foreign registration/device, forged proof owner and quota-domain replacement. This
prerequisite cannot enable existing CopyPass/1 or OrderedCopyAttachment/1 uploads.


2026-10-05 ordered upload prerequisite validation: OrderedOriginalUpload/1 and
OriginalInitializedUploadJoin/1 are Contract Complete / Prototype Complete for
selected cold metadata only. Actual WebGPU buffer and composed texture proofs
join the same Prepared root at original ordinals 1/2, require the exact initialized
staging lease in Source, retain one unique source/target allocation, and consume
independent Source native object/staging limits and original Recording quota.
Two 20-byte texture writes count 40 texel work bytes while unique staging768 and
target20 remain separately bounded. Join peak876 supports exact/one-less/exhausted
retry. Pending, rollback/stale, foreign device, forged owner, wrong variant/order/
count, native layout and quota-domain substitutions refuse; actual physical loss
refuses both joins, all ownership quotas drain, hot core allocation0. Host11/11,
Wasm/browser8/8, inventory159/159. Backend Validated selected WebGPU native binding
only; no target write or native content completion claim. Null ValidatorOnly,
Vulkan/Metal payload-missing; execution remains closed. Existing journals and two
proof issuers unchanged. Evidence: ordered-upload-join-evidence.yaml and final
host/web XML/logs plus normal/indirectDisabled reports under bindless-continuation.
RenderDoc unavailable diagnostic retained; no broad Platform Validated or
Production Ready promotion. Continue explicit cold/lowering /3 and private
same-root Pending consumption; parent b4/b3b/b3/b/d3/full plan remain open.


CanonicalOriginalColdAdmission/3 selects OrderedOriginalUpload/1 and requires
OriginalInitializedUploadJoin/1 over the same root. Per-command buffer/texture
upload/mip/native-copy counts, declared buffer bytes, packed texture work, raw
pixel bytes and target texel work are authored independently of unique staging/
target resource retention. Failed derivation publishes zero requirements.
/1 and /2 require every /3 upload field to be zero. /3 uses the existing actual
Source-authored pending submission/scope/queue/loss capacities, same seal and
independent Recording/Submission/Callback domains; initialized proof quotas
(including pointer/control-block aliases) cannot substitute these domains.
Cold admission may retain initialized/native ownership but cannot issue a
command: RHI-ORIGINAL-NATIVE-COLD-UPLOAD-ORDERED-LOWERING-MISSING closes the
canonical command factory before any payload allocation or sticky claim.
Null ValidatorOnly and Vulkan/Metal payload-missing; WebGPU selected Prototype
cold join only. Target write/content completion remains unproven.


2026-10-05 ColdAdmission/3 validation: rebuilt host11/11, Wasm/browser8/8 and
inventory161/161. Real initialized buffer/texture joins, native resource closure,
original seal and Source capacity1-8-8 are retained together; cold metadata peak364
passes exact/one-less/exhausted/retry. Missing/forged and valid foreign-root joins
refuse; independent count/byte/owner limits and initialized callback quota pointer
plus control-block aliases refuse. Core allocation quota alias query reuses
existing allocation headers without another domain inventory, is unit-tested and
allocation-free. Failed derivation publishes zero work requirements. /1 and /2
reject upload fields. /3 refuses canonical command creation before allocation/
sticky claim; target writes and content completion remain closed. All tested
retained quotas drain, hot core allocation0. Contract Complete / Prototype Complete
selected cold /3; Backend Validated selected WebGPU ownership joins only. Evidence:
ordered-upload-cold-evidence.yaml, final host/web XML/log/reports and RenderDoc
unavailable diagnostic under bindless-continuation. No broad Platform Validated
or Production Ready claim. Continue b4b2 positional native payload and private
same-root Pending authority via ordered-upload-pending-lowering-next-audit.yaml.

2026-10-05 CopyPassLowering/3 cold projection contract (validation pending):
The /3 descriptor independently bounds regular copies and upload-expanded copies,
buffer commands/declared bytes, texture mips/pixel bytes, staging work and texel
work. /1 and /2 reject these fields. Every upload uses the retained exact-root
initialized native join and its original command ordinal; texture expansion uses
the existing whole-mip packed-copy mapper. Immutable upload geometry occupies a
separate bounded collection and is reinspected before publication. Sealed staging
transitions/writes refuse. Ordinary exclusive texture access remains closed.
Cold /3 may allocate the existing command and callback custody after complete
owner-budget admission. Issuer preparation fails before allocation with
RHI-ORIGINAL-UPLOAD-LOWERING-PENDING-AUTHORITY-MISSING until private same-root
Pending authority is implemented. No encoder work, queue submission, proof or
second journal is granted by projection. Null is ValidatorOnly; Vulkan and Metal
are DiagnosticFailure under OrderedOriginalUpload/1; WebGPU is explicit Prototype
metadata projection only. No Backend/Platform/Production validation is claimed.

2026-10-05 CopyPassLowering/3 projection validation: host11/11, Wasm/browser8/8,
inventory162/162. Both actual native browser modes freeze original buffer uploads
into two copies (Recording metadata peak800), and texture uploads into four exact
whole-mip copies (peak1024). Exact/one-less/exhausted/retry, independent work and
owner/CPU bounds, zero hot allocation and complete quota retirement pass. The
Pending issuer diagnostic remains closed. Contract Complete / Prototype Complete
selected projection; Backend Validated native cold ownership only. Parent b4b2/b4
and full bindless remain open. RenderDoc unavailable; no visual, Platform Validated
or Production Ready claim. Evidence ordered-upload-projection-evidence.yaml and
ordered-upload-projection-{host,web}-results.xml in bindless-continuation.

2026-10-05 InitializedOriginalBufferUpload/1 contract (validation pending):
This explicit Prototype subset consumes CopyPassLowering/3 with initialized
buffer uploads and zero texture uploads. Texture recording deterministically
refuses with RHI-ORIGINAL-UPLOAD-LOWERING-PENDING-AUTHORITY-MISSING until its
content authority is implemented. Null is ValidatorOnly; Vulkan/Metal payloads
are DiagnosticFailure; WebGPU may use the existing Recording/Submission issuers.
The private seal compares a creation-issued staging observation to the exact
root graph object/control/registration/state/revision, requires initial/final
CopySrc and unchanged actual Pending revision, and validates matchesPending on
the root's existing journal. Source readiness, drained initialization receipts,
native layout, frozen bytes/frame and leases are independently revalidated.
Public source validation must still refuse Pending. Frozen upload copy ranges
are emitted at the original operation positions in the existing encoder/scopes.
Only existing queue+scope completion may commit the same root; pre-submit failure
rolls back and post-submit uncertainty invalidates. No second journal, standalone
upload queue, proof issuer, generic capability or NativeSync promotion is allowed.

2026-10-05 InitializedOriginalBufferUpload/1 validation: host11/11, Wasm/browser8/8,
inventory163/163. Both real browser modes prove original target bytes16 after
queue+three submission scopes, staging revision unchanged, private real-journal
Pending authority and public source refusal. Success, pre-submit cancel, actual
record validation error, post-submit caller drop, post-submit cancel and actual
destroyed target submit error all pass; hot allocations0 and all quotas retire.
Contract Complete / Prototype Complete selected buffer subset; Backend Validated
selected WebGPU target payload/lifecycle only. Texture and full /3 execution,
distinct/interleaved output and upload-specific physical loss/uncertainty remain
open. No broad Platform/Production claim; RenderDoc unavailable. Evidence
ordered-upload-buffer-pending-evidence.yaml and final host/web XML/log/reports.

2026-10-05 InitializedOriginalTextureUpload/1 contract (validation pending):
The /3 private texture execution subset extends the same initialized-buffer
root authority to creation-issued texture state and content expectations. It
checks exact registration/control/initial state/revision/range and the original
command's CopyDst access steps, plus the original content initial observation,
real Proposed Pending observation and monotonically minted per-command generation
in the same root journal. The final proposed generation must equal the last
original upload for that control. Authoring generation/frame labels grant no
completion authority. Public source/content validation still refuses Pending.
Only existing Recording may consume the immutable ordinal-bound whole-mip copies
through a private exclusive-content emitter. Ordinary texture copy/read access
remains denied. Existing genuine queue+scope/drain completion commits state and
content together through the sole seal. Null ValidatorOnly; Vulkan/Metal explicit
payload-missing DiagnosticFailure; WebGPU explicit Prototype only. No general
bindless/NativeSync/Platform/Production promotion. Full distinct/interleaved output
and upload-specific uncertainty/loss validation remain separate required tasks.

The completed /3 cold facade continues to grant metadata ownership only, with
RHI-ORIGINAL-NATIVE-COLD-UPLOAD-COLD-JOIN-ONLY and
RHI-ORIGINAL-UPLOAD-LOWERING-COLD-PROJECTION-ONLY. The separately opted-in private
InitializedOriginalBufferUpload/1 and InitializedOriginalTextureUpload/1 execution
decisions are consumed by existing issuers before allocation. Buffer-only work
rejects texture inputs with RHI-ORIGINAL-INITIALIZED-BUFFER-UPLOAD-TEXTURE-PROFILE;
texture work consumes its own approved decision. These labels replace historical
missing-payload reasons; metadata objects still prove no native completion.

2026-10-05 InitializedOriginalTextureUpload/1 validation: rebuilt host11/11,
Wasm/browser8/8 and inventory164/164. Private exact original state/content/control/
registration/initial revision and per-command minted generation checks consume
real Proposed Pending observations in the same sealed root journal. Initialized
staging retains genuine ready/native evidence and unchanged CopySrc revision.
Both buffer and texture paths use original ordinal ranges through the existing
Recording and Submission issuers; proof friends remain exactly those two. Public
source/content Pending validation and ordinary exclusive texture copy/read remain
refused. Actual target readback verifies buffer16 and both texture mips20; texture
content becomes Completed generation2 with revision equal to initial plus two
proposals and one completion write, never authoring generation29. Each path's six
probes cover success, pre-submit cancel rollback, actual recording validation
error, post-submit caller drop retention, post-submit cancel quarantine and actual
destroyed-target submit error/invalidation. Hot allocations0 and all source/
Recording/Submission/Callback/device quota domains retire. The shared typed test
fixture reuses the same cold and native lifecycle builders for both resource kinds.
Contract Complete / Prototype Complete selected cold /3 and private initialized
upload execution; Backend Validated selected WebGPU target payload/lifecycle only.
Null ValidatorOnly; Vulkan/Metal stable payload-missing DiagnosticFailure. No
broad Platform Validated or Production Ready claim; RenderDoc tool unavailable.
Parent b4b/b4b2 closes for this explicit selected execution contract. b4c/b3b/b4/
d3 and full plan remain open for distinct/interleaved writes, upload-specific
physical loss/uncertainty and original readback completion/map/result ownership.
Evidence ordered-upload-texture-pending-evidence.yaml, final status host/web XML/
logs/reports and RenderDoc diagnostic in bindless-continuation. Continue through
ordered-upload-target-lifecycle-next-audit.yaml.

2026-10-05 interleaved upload and invocation verification: host11/11 including
fast contract/core and Null hidden-frame; Wasm/browser8/8 in normal and
indirect-disabled modes; inventory164/164. Buffer A/B uploads occupy ordinals1/9
in three legal transfer passes separated by encoder barriers; actual checkpoint
is A and final buffer B. Texture upload ordinals1/3 surround independent copy
element0; both final mips contain B and content is Completed minted generation2.
Unique native bytes64/1588, staging32/1536, native copies3/5; independent work
and capacity/ordinal negative gates pass. Shared typed fixture now12 lifecycle
probes per kind, including native before/after invocation throw, reverse actual
receipts, partial scope registration, native layout mutation and logical shutdown.
Extracted native probe preserves genuine GPU objects/promises/results and is
reused by existing canonical submission. No second proof/journal, early
publication or hot allocation; all quotas retire. Contract Complete / Prototype
Complete selected acceptance; Backend Validated selected WebGPU paths only.
Null ValidatorOnly; Vulkan/Metal DiagnosticFailure. Physical loss and queue/
scope-stack uncertainty remain open. No Platform Validated / Production Ready
promotion; RenderDoc unavailable. Evidence ordered-upload-interleaved-and-
invocation-evidence.yaml and host/web results/reports in bindless-continuation.

2026-10-05 upload physical-loss verification: host11/11 (fast contract/core +
Null hidden-frame), Wasm/browser8/8, inventory164/164, plus two fresh browser
modes for buffer and texture native scope uncertainty. Five loss paths per kind:
cold, recording with three real scopes outstanding, submitted with four real
queue/scopes outstanding, suppressed queue registration retaining invalidated
shell after scope drain, and genuine native pop-then-throw retaining three
scopes until actual device loss. Scope uncertainty causes subsequent canonical
recording refusal; no stack reset or guessed recovery. Exact device.lost and
genuine native promises/results prove loss/drain; same root/seal/journal
Invalidated, never Completed state/content, all source/device/packing/content/
Recording/Submission/Callback quotas zero, hot allocations0. Existing original
submission now reuses the same native loss probe; selected upload mode replaces
its final scope probe only, leaving ordinary recorded root loss-tested.
Parent initialized upload/content/ordered-execution b3b/b3/b4/b acceptance closes
for the explicit selected /3 private path. Contract Complete / Prototype Complete
selected acceptance, Backend Validated selected WebGPU only; Null ValidatorOnly,
Vulkan/Metal stable DiagnosticFailure. No broad Platform Validated or Production
Ready/public execution promotion. RenderDoc unavailable. d3c original native
readback completion/map/result and enclosing bindless plan remain open. Evidence
ordered-upload-physical-loss-evidence.yaml, host/web reports/XML, separate scope
mode reports and RenderDoc diagnostic in bindless-continuation.

CanonicalOriginalReadbackResult/1 contract-first cold prerequisite (Contract
Complete): exact original Request/destination ownership distinguishes aggregate
command work from unique final snapshot capacity. Two writes of the same Request
to its same destination require four mip copies and declared bytes1536, but one
final padded snapshot768 under independent Result CPU65536. Distinct Requests
sharing a destination are refused as ambiguous concurrent map ownership.
Reuses OriginalCommandStreamProgram, shared identity/allocation projections and
original layout Mapper; IDs/frame/generation labels and CPU metadata grant no
completion/map/publication authority. Failed inspection publishes no partial
requirements. Null ValidatorOnly; Vulkan/Metal/WebGPU stable payload-missing
DiagnosticFailure. Existing Cold/CopyPass /1-/3 remain unchanged and refuse
TextureReadback. Backend-private ordered readback, seal-bound completion, map
receipts and independent Result storage/ownership remain future gates.

Verified 2026-10-05: Contract Complete cold prerequisite only; host11/11
(including quick contract/core and Null hidden-frame), Wasm/browser8/8
(normal and indirect-disabled), inventory164/164. Both native browser reports
require canonicalReadbackResultRequirementsPassed. Exact/one-less budgets,
distinct genuine Requests sharing a destination, no partial requirements and
hot allocation0 verified. No execution/map/publication gate was opened; no
Backend Validated, Platform Validated or Production Ready claim for readback.
Evidence canonical-readback-result-contract-evidence.yaml and matching logs,
XML and preserved reports in bindless-continuation. Continue d3c2 exact native
bindings/projection, then d3c3 completion/map/Result publication.

CanonicalOriginalReadbackJoin/1 contract-first cold binding (Contract Complete):
original ordinal/Request/layout/destination and native allocation identities are
retained under the original root Recording quota; source/staging native budgets
count unique allocation tokens separately from aggregate copy work and Result.
Destination requires exact CopyDst/GpuToCpu and actual native CopyDst|MapRead,
Unmapped bridge/browser map state; GpuOnly or other usage refuses deterministically.
Mip decisions reuse TextureReadbackCopyLowering and the original validated layout.
Only ordinary source content is admitted; exclusive content requires its own
future same-seal read contract. Null ValidatorOnly, Vulkan/Metal DiagnosticFailure,
WebGPU private Prototype cold join only. Existing Cold/CopyPass /1-/3 still refuse
readback; no recording, map, result allocation/publication or legacy proof bridge.
d3c2b will consume these joins in a versioned same-seal recording payload.

Verified 2026-10-05 d3c2a: Contract Complete / Prototype Complete cold joins;
selected WebGPU native binding Backend Validated only. Host11/11 with quick
core/contract and Null hidden-frame, host WebGPU stub rebuild, Wasm/browser8/8
normal/indirect-disabled, inventory166/166. Original ordinals1-2/Request1/mip
copies4 retain exact source20/staging768, aggregate work1536 and unique Result768
requirement independently. Wasm cold peak1604; exact/one-less/exhausted budgets,
forged/equal/foreign/order/count/native mutation, actual native mapped buffer,
Pending/stale, caller-drop/shutdown and genuine physical device.lost tests pass.
Hot allocation0 and all eight quota domains retire. No Result storage allocated,
no recording/completion/map/publication authority. Evidence canonical-readback-
join-evidence.yaml, logs/XML/reports and RenderDoc unavailable diagnostic.
Next d3c2b introduces an explicit /4 same-seal ColdAdmission/CallbackCustody/
ordered recording profile; /1-/3 stay closed to readback. Existing State/Graph
retention and Recording/Submission issuers remain sole native authorities.
d3c2/d3c3/enclosing bindless remain open; no Platform Validated/Production Ready.

OrderedOriginalReadback/1 with Cold/CopyPass /4 (verified 2026-10-05): explicit
Prototype native ordered mip copies of ordinary source content only. /4 requires
independent readback command/mip/declared/unique-Result/CPU budgets and rejects all
upload/copy/pass fields; prior /1-/3 reject all new readback fields and commands.
The exact published seal/root/journal remains the only state authority; cold joins
bind frozen layout decisions to original Closure native observations. Only the
existing Recording/Submission issuers may record/submit; error scopes, genuine
queue completion and actual loss retain the same State/Graph until drain.
Null ValidatorOnly; Vulkan/Metal deterministic payload-missing DiagnosticFailure;
WebGPU private Prototype native-copy path. Dependencies use normative full-range
TransferDependencyLowering/1; no NativeSync promotion, second journal, dummy or
legacy proof bridge. Mapping/publication remain closed pending d3c3 independent
Result ownership and genuine same-root map/completion evidence.

Ordered readback verification: host11/11, Wasm/browser8/8, inventory167/167.
Ten native execution probes and five actual device.lost probes pass, including
reversed genuine scope/queue receipt order, real validation errors, mapped target
refusal, invocation before/after uncertainty, metadata change, logical shutdown,
queue/scope registration uncertainty and exact same-root callback quarantine.
Successful mip pixels match authored bytes1..20; all ten CPU quotas retire to zero
per probe, hot allocation0. Contract Complete / Prototype Complete and selected
WebGPU ordered native-copy Backend Validated only. Canonical Result/map/publication
remain closed; no Platform Validated/Production Ready. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-readback-ordered-*`.
RenderDoc unavailable on macOS, deterministic transfer evidence retained.
Next: d3c3a independent bounded cold Result storage, then same-root genuine map
and immutable publication. No legacy transaction/proof bridge.

CanonicalOriginalReadbackResultStorage/1 (verified 2026-10-05): cold independent
Result CPU allocation only. Exact original Requests deduplicate repeated writes;
unequal Requests/aliased destinations remain rejected by CanonicalResult/1.
Snapshot metadata/storage are allocated under Result, while the bounded cold
collection uses the exact original Recording quota and retains the original /4
Admission. Result must be independent from Source, Graph input/state graph,
Recording, Submission, Callback, original native resource allocations and original
device/loss/registration CPU owners. Capacity failure is atomic, no partial result
escapes. Snapshot retains only immutable Request/layout; it cannot retain root,
source/destination GPU objects or grant completion. No mutable byte view, native
map, publication, synthetic receipt or legacy transaction/proof bridge exists.
All four backend cold-storage outcomes are ValidatorOnly, map/publication false;
WebGPU preparation additionally validates genuine original native /4 joins.
Mapping must separately version Source-authored callback counts and same-root
State/Graph retirement before any map issuance. No Backend/Platform/Production
promotion follows from cold CPU allocation.

CanonicalOriginalReadbackMap/1 (verified 2026-10-05): L2 count/lifetime contract
only; no native issuer or map/publication gate opens. Each exact Request consumes
one map callback and a versioned three-scope batch, so one repeated Request adds
four slots to original eight: Source must author twelve callback tickets and
twelve registrations before seal publication. Source Result/CPU budgets remain
exact. Overflow/one-less failure clears every output. This planning contract
cannot consume fabricated completion events: future runtime must bind exact
same-root Submission/queue/scope evidence before map, preserve State/Graph and
loss ownership across map, copy/unmap before publishing, join all real map/scope
receipts, and retain owners until genuine completion/loss plus foreign callback
drain. Listener suppression alone cannot prove mapping or release its lifetime.
Published/Retired are terminal and cannot revive. Null ValidatorOnly; Vulkan,
Metal and WebGPU deterministic map-payload-missing DiagnosticFailure. Contract
inspection grants no Backend Validated/Platform Validated/Production Ready.

Cold Result verification: host11/11, Wasm/browser8/8, inventory168/168, clean
reports. Request1/writes2 -> one padded768 snapshot; Wasm Recording peak140,
Result peak840, independent exact/one-less pressure. Forged/equal/foreign/aliased
Source/Graph/state graph/Recording/Submission/Callback/native allocation/device
owner domains refuse. Snapshot survives original root/facade destruction without
native retention; drop and actual device.lost retire all eleven quotas, hot
allocation0. Contract Complete / Prototype Complete cold CPU storage only;
map/publication gates remain closed, no new native Backend/Platform/Production
validation. Initial peak-measurement fixture failure preserved and corrected by
a fresh non-exhausted original candidate; production budgets unchanged.
Evidence `build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-result-storage-*`.

Map count/lifetime contract verification: host11/11, Wasm/browser8/8,
inventory169/169; all current browser flags true with no diagnostics/native errors.
Reused Result/1 requirement validation rejects missing copies/submission, old8
Source callback limits, Result/CPU mismatches, one-less and overflow; no partial
requirements. Early map/publication, suppression-only release, unknown phase/event
and terminal revival refuse; hot allocation0. Contract Complete planning only;
native map/publication remain closed on all four backends. No Backend/Platform/
Production promotion. Evidence `build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-readback-map-contract-*`.
Next runtime audit: /5 adds per-Request four cold callbacks to the exact original
State/Graph; foreign-drain must include them and preserve original loss ownership
before current Submission retirement. emdawn bridge state and actual native
GPUBuffer.mapState must agree; reuse genuine C map/read/unmap algorithms without
legacy transaction/proof authority or JS-only bridge compatibility.

Cold ReadbackMap profile contract (cold preparation verified 2026-10-05): NativeCold
and CopyPass /5 retain original readback-only /4 command/mip semantics and add
explicit CanonicalMap/1 requirements. Earlier profiles reject every map field.
Map counts derive from exact unique original Requests, using original recording/
submission/queue/loss counts plus four callbacks per Request; Source must author
the complete demand before seal/reservation. Cold /5 binds Result storage to the
exact Admission, with independent Result quota and immutable original Request
identity. CallbackCustody /2 allocates the derived bounded slot count; /1 stays
exactly eight. Owner demand includes all added slots and independent storage/
snapshot owners within original Recording/Submission/Callback lifecycle limits.
The original loss slot keeps index7; all foreign-drain/cancel loops must cover
additional slots. Null validates; Vulkan/Metal diagnose missing payload; selected
WebGPU permits only genuine cold native connection/projection/callback metadata.
Native map and publication stay unavailable: issuer creation must reject /5
through CanonicalMap's deterministic payload-missing diagnostic until a complete
same-root native map receipt/retirement path consumes this profile. No default
opt-in, legacy proof, second journal, placeholder or dummy success exists.

Cold map /5 validation: host11/11, Wasm/browser8/8, inventory170/170.
Request1 repeated writes2 yields map1/scopes3, original8 -> Source1/12/12,
independent Result snapshot768 and exact native copies4. Source8/11 capacity,
missing/forged/foreign genuine Result Admission, one-less owner/CPU limits reject.
Submission and Callback exact/one-less/exhausted allocation probes unwind and
retry without publication; genuine physical device.lost invalidates same root,
keeps Result unpublished and drains all eleven quotas, hot allocation0.
Contract Complete / Prototype Complete cold metadata only. All four native map/
publication gates remain closed; no map, publication, Backend Validated,
Platform Validated or Production Ready claim. Evidence canonical-map-cold-*.
Continue cold C-map receipts and same-State/Graph mapping retirement.

Canonical map receipt adapter contract /1 (selected adapter verified 2026-10-05):
L2 map intent remains exact original Request -> one MapRead over offset0 and its
padded byteCapacity; no map outcome may grant Submission or snapshot authority.
Null validates intent only; Vulkan/Metal diagnose absent receipt payload; selected
private WebGPU /5 may cold-bind C-map userdata and the existing three native
scope receipt adapters to the exact Result Request and original staging buffer.
Canonical execution/publication remain closed until same-State/Graph retirement
is implemented. Earlier /1-/4 profiles allocate no map receipts.

The backend-private native C adapter must consume actual wgpuBufferMapAsync,
wgpuBufferReadMappedRange and wgpuBufferUnmap algorithms, requiring emdawn and
GPUBuffer.mapState agreement at each access boundary. Map ranges use spec-defined
four-byte size alignment, offset0 and explicit native MapRead usage. All adapter
objects/userdata live in cold Callback-owned original slots; no hot core-owned
allocation or new RecordedCommand proof issuer is allowed. Native scope listeners
are the existing cold NativeCompletionReceipt/1 payloads. Caller/enum/boolean
cannot synthesize delivery. C-map callback reports only native map status.

Guarded native invocation must catch JS throws across a trivial C thunk. Unknown
map/read/unmap invocation fails with stable diagnostics, preserves the exact cold
slot/root/Graph, and never claims callback/lifetime completion. C callbacks may
arrive synchronously when wgpuBufferUnmap aborts a pending emdawn future; caller
must retain the original state through the entire call. Only actual callback plus
bridge/native unmap agreement, or a proven native loss teardown with all future
userdata drained, permits release. Listener suppression alone is insufficient.
Do not manually alter emdawn state/future tables. External emdawn event allocation
is opaque and hot in this library version: selected use remains explicit
Prototype; core hot-allocation0 is not an assertion about bridge/driver allocation,
and cannot promote this path to Production Ready.

Canonical ReadbackMapDrain/1 contract (pure contract verified 2026-10-05): native
map retirement is separate from independent Result publication. Facts describe
original /5 expected maps, actually invoked C maps, genuine map callbacks,
copied-and-unmapped maps, map scopes drained/successful, original foreign receipt
drain and genuine queue/loss evidence. These are validation inputs only; no public
facts/enum/bool may create native authority or retire the original graph.

Each issued C map must deliver its actual callback and prove bridge/native unmap
agreement before its raw userdata or original graph can retire. Every issued map
owns exactly three original scope receipt slots. Suppressed scope listeners may
count only toward foreign userdata drain, never scope success; suppressing map
listeners is not supported by the C adapter. Invocation uncertainty, cancel,
native map/scope errors and physical loss invalidate-and-retain until original
callbacks, every issued C map and safe unmap have drained. Queue suppression is
not GPU completion: MaySubmit requires genuine queue receipt or actual loss even
when all userdata is suppressed. Inconsistent/overflow facts fail closed.

Successful drain requires genuine same-root Submission proof, all expected maps
issued, exact copies/unmaps, all scopes successful and all foreign receipts
drained. It permits native graph retirement with Result still unpublished, never
snapshot publication. Null validates these rules; Vulkan/Metal/WebGPU canonical
execution remain deterministic DiagnosticFailure until their private native
runtime consumes this contract. Existing /4 success retirement must remain
unchanged; /5 must keep the existing State/Graph/loss/seal across mapping and use
Recording/Submission as the only RecordedCommand proof issuers.

Canonical map receipt / drain validation 2026-10-05: host11/11,
Wasm/browser8/8, inventory172/172; both canonicalNativeMapReceiptPassed and
canonicalMapDrainContractPassed true with clean reports. Native C-map adapter
passes eight real execution/error/uncertainty probes and genuine pending-map
physical loss. Native map/read/unmap validates emdawn and actual GPUBuffer state;
read768 bytes match the authored native seed. Throwing unmap clears emdawn's
onUnmap list before its native call; retry state mismatch fails deterministically,
and the negative fixture requires explicit native buffer teardown. No guessed
bridge repair exists. Cold scope receipt partial failure unwinds and retries;
original /5 Source1/12/12, independent Result and original loss slot remain.
ReadbackMapDrain/1 prohibits release before C callback/unmap/scope drain, rejects
suppression as success or queue lifetime, and allows only unpublished native
retirement. Contract Complete / Prototype Complete cold receipts; Backend Validated
selected standalone C-map adapter only. Drain is Contract Complete only. Canonical
map/publication four-backend gates still closed; no Platform Validated or
Production Ready claim. Core hot allocation0 excludes opaque/hot emdawn event
allocation, whose selected use remains explicit Prototype. Preserved failures and
final reports: canonical-map-receipt-* and canonical-map-drain-*. Continue
same-State/Graph native map lifetime before original Submission retirement;
pending Result/joins need explicit original-seal validation. RenderDoc unavailable
on macOS; no visible output changes or broader visual/platform validation claim.

Pending canonical map validation contract /1 (retained validation verified 2026-10-05):
ResultStorage retains its exact original Admission and seal when the root is
sealed Pending; cold creation still requires Prepared. A private retained
validator must preserve immutable Request/layout/Result quota binding and all
original CPU domains, source/native allocations, descriptors, capability and
lifetime checks. Result bytes stay unpublished and no validator issues commands,
commits journal, publishes bytes or synthesizes a proof.

Only the exact genuine NativeBufferMapReceipt bound cold to that Request's
original staging buffer may change the active Request's map-state validation.
Prepared expects Unmapped, Pending requires bridge/native Pending agreement,
delivered native success expects Mapped until proven unmap then Unmapped. Every
other Request still expects Unmapped. Unknown/failed/uncertain C-map receipt cannot
relax state; foreign original Request/buffer/byte range/seal/Admission or aliased
ownership fails deterministically. All repeated writes of the same Request use
the same active validation, never a backend handle lookup heuristic or wildcard.
Null validates semantics; Vulkan/Metal diagnose missing native map payload;
selected private WebGPU permits read-only exact retained binding validation only.
Canonical map/publication gates stay closed until same-State runtime consumes
actual Submission/receipt/drain evidence. Native smoke may exercise genuine
mapping under a retained original graph solely as a fixture; validation cannot
prove that original GPU work was submitted or authorize early map execution.

Pending retained map validation verified 2026-10-05: host11/11,
Wasm/browser8/8, inventory173/173; canonicalRetainedMapValidationPassed true on
both browsers. Same original sealed Recording root/journal/Admission remains
retained through genuine C receipt Prepared/Pending/Mapped/Unmapped validation;
exact staging768 and native read768 bytes match the authored fixture seed.
Repeated Request writes2 share active proof; other Requests must stay Unmapped.
Missing pair, foreign genuine seal/Request/buffer/receipt, forged owner, wrong
size, failed/uncertain native receipt and cancelled/stale root refuse. No
Original Submission or Result publication authority is inferred. Metadata
headers forward-declare the native receipt, preserving offline Wasm contract
ABI; no guessed legacy callback ABI compatibility. Core hot allocation0 and
all eleven quotas retire. Contract Complete / Prototype Complete retained
binding validation only. Canonical map/publication gates still closed, no full
map Backend/Platform/Production promotion. Evidence canonical-map-retained-*.
Continue original Submission -> map callback consumption, private unpublished
Result copy and same-State MapDrain retirement. No visible output changes;
RenderDoc remains unavailable on macOS.

Same-State native readback execution contract /1 (selected /5 runtime integrated):
selected /5 is an explicit Prototype opt-in with Source-authored callback and
ownership budget; public capabilities do not automatically enable mapping.
Genuine original Submission invocation plus queue/scopes success must be latched
inside the existing private State before any native map call. The journal stays
Pending, original loss remains armed, and the original Recording/Submission
issuers, seal, graph ticket and native allocations survive through mapping.

Cold map slots and three native scope adapters per exact unique Request are the
only accepted payloads. Process original Requests in frozen order, at most one
active map/scope batch at a time; this is a declared bounded scheduling policy,
not a compatibility fallback. Additional unissued Request buffers remain
Unmapped. Map callback validates exact retained binding, copies only into that
Request's already-allocated independent unpublished Result range, and proves
native/emdawn unmap before completing its foreign slot. Scope callbacks only
consume native evidence; they never create command or Submission proof.

Private byte writes must not expose any public writer, mutable snapshot, native
handle, native enum or premature publishedBytes. Repeated original writes retain
one final snapshot; no cloned Request/layout/buffer or temporary hot CPU storage
may replace it. Snapshot publication stays closed until its separate canonical
publication contract/runtime is complete.

Success consumes ReadbackMapDrain::RetireUnpublished then calls private original
seal commit through the existing Submission issuer; loss detach and native graph
retirement follow all actual map/scope/unmap evidence. Cancellation/error/throw/
registration uncertainty invalidates-and-retains; original queue/loss evidence
and every issued C future plus safe-unmap must drain before owner retirement.
Native unmap mismatch after a throw cannot be repaired by emdawn table mutation;
physical-loss teardown may destroy the exact quarantined native buffer only when
actual callbacks and bridge/native teardown prove its raw userdata safe.
Suppressed listener is never output/GPU completion. Existing /4 retirement stays
unchanged. All error paths require stable diagnostics and bounded original
retention; core hot allocation0 excludes opaque emdawn event allocation, so this
selected path remains explicit Prototype. Null is ValidatorOnly; Vulkan/Metal
have deterministic missing native payload diagnostics; WebGPU mapping availability
may be promoted only after the complete private executor and its genuine native
negative/loss/drop/cancel probes consume this contract. No capability inference
from backend/OS name or Dummy success is permitted.

Same-State map /1 verification (2026-10-05): Contract Complete / Prototype
Complete; Backend Validated selected WebGPU /5 unpublished map/copy/drain only.
Host11/11, Wasm/browser8/8 and inventory174/174 pass; both reports clean with
canonicalMapExecutionPassed and preserved /4 ordered readback. Real authored
texture20 traverses original writes2/copies4 and exact staging768 into independent
unpublished Result768. Seven success/error/order/cancel/drop probes and four
physical-loss/uncertainty probes prove same-State Pending journal/loss retention,
genuine C callbacks, safe unmap/destruction, MapDrain retirement and eleven quota
domains zero. Scope-stack poisoning stays fail-closed until actual physical loss;
no fixture resets it. Public capability/default and publication remain closed.
Null ValidatorOnly; Vulkan/Metal deterministic missing-payload diagnostics;
WebGPU selected Prototype/unpublished. Core allocation0 excludes opaque hot
emdawn event allocations. No Platform Validated or Production Ready claim.
RenderDoc unavailable on macOS; no visible rendering change. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-map-execution-*`.

CanonicalSnapshotPublication /1 (selected /5 runtime integrated):
Publication requires a separate explicit versioned Prototype intent, consumed
cold with the same /5 original Result/Admission. It adds no callback, native
allocation, temporary byte range or Source capacity: the existing Source-authored
Result and map/callback limits are the only budgets. Mapping-only /5 remains
unpublished. A public fact/enum/validator result cannot issue publication or proof.

The existing original Submission issuer must prevalidate the genuine same-State
MapDrain RetireUnpublished result, exact original Request/layout and independent
Result-quota binding, complete once-only unpublished snapshot coverage and absence
of terminal/cancel/loss/error/uncertainty. Original seal/root commit and empty
journal are mandatory before freezing all independent snapshots. Freeze is a
bounded allocation-free CPU operation with no native calls or callback reentry;
prevalidate the complete set before changing any publication flag. Any failed
precondition keeps every snapshot unpublished and reports a stable diagnostic.
Already-published snapshots cannot be written, mapped, republished or invalidated
by subsequent native resource loss. Independent snapshots retain only original
CPU Request/layout/Result quota and immutable bytes, never State, root, native
closure/allocation, Recording/Submission or loss subscription. Caller shell drop
while submitted still cancels/quarantines; it must not publish from future output.

Reuse the existing owned-snapshot public const-byte access shape; its Legacy
publication/CompletionSignal paths cannot supply canonical native authority.
Null remains ValidatorOnly; Vulkan/Metal publication payloads are deterministic
DiagnosticFailure with distinct stable ids; WebGPU selects explicit Prototype
only through separate cold publication opt-in. Broad Backend/Platform/Production
promotion remains prohibited. Contract-only and selected runtime verification
checkpoints below distinguish the validated scope and remaining platform limits.

CanonicalSnapshotPublication /1 contract verification (2026-10-05): Contract
Complete only. Host contract/inventory/Null hidden-frame3/3, Wasm offline
contract/inventory2/2, inventory175/175. Missing native drain/commit/bindings,
duplicate/partial coverage, terminal/cancel/loss/error/uncertainty, version and
opt-in reject. Publication gates remain closed; native runtime integration is
next. Evidence `artifacts/bindless-continuation/canonical-snapshot-publication-contract-*`.

CanonicalSnapshotPublication /1 runtime verification (2026-10-05): Contract
Complete / Prototype Complete; Backend Validated selected WebGPU /5 immutable
CPU snapshot publication only. An optional separate cold Intent/1 selects
publication; absent intent preserves mapping-only unpublished behavior. The
existing original Submission issuer consumes same-State MapDrain evidence and
commits the exact original root/seal before atomically freezing the prevalidated
snapshot set. Public callers cannot obtain a writer, native payload or proof.
Cold reuse/private writes reject frozen snapshots. No added Source slots,
callbacks, native allocations, temporary bytes or hot core allocations.

Host11/11, Wasm/browser8/8 and inventory176/176 pass; both reports clean with
canonicalSnapshotPublicationPassed, canonicalMapExecutionPassed and preserved
ordered /4 behavior. Seven success/order/cancel/error/drop probes and four
physical-loss/uncertainty probes verify publication suppression and native
retirement. A successful independent snapshot retains exact original CPU
Request/layout and Result bytes after all original graph/seal/issuer/native/
facade owners are gone, remains byte-identical through actual physical loss,
and drains all eleven quota domains when finally dropped. Original CPU Request
metadata is charged to the frozen input metadata domain, not inferred from a
Source/Graph role name. Core allocation0 excludes opaque emdawn map events.

The publication lowering now selects WebGPU Prototype only with separate explicit
opt-in; Null ValidatorOnly and Vulkan/Metal deterministic payload-missing
DiagnosticFailure remain. Map /1 itself still grants no publication. General
RHI/bindless capabilities remain closed. No Platform Validated / Production Ready
claim; RenderDoc unavailable on macOS and no visible rendering change introduced.
Evidence `build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-snapshot-publication-*`.
Continue complete ordered native executor coverage; enclosing bindless stays open.

CanonicalOriginalTypedSources/1 and CanonicalOriginalTypedInputs/1 now define
original Pipeline/Descriptor/Table/Query ownership, immutable artifact/element
relationships, positional command fields and independently bounded native demand.
See [the typed source contract](canonical-original-typed-sources-contract.md).
The declaration validators bind exact original registered resources/generation,
resource usage and explicit access/ranges; their successful CPU checks cannot
issue original typed adapters or native work. Null remains ValidatorOnly;
Vulkan/Metal/WebGPU each return a stable typed payload-missing DiagnosticFailure.
Status: Contract Complete declarations only; owned adapter/native typed execution,
Platform Validated and Production Ready remain open. Continue d3d2 on the same
canonical Source/Inputs/root/seal/journal, preserving the existing two proof issuers.

Original pipeline factory registration /2 (d3d2b) is **Contract Complete /
Prototype Complete** CPU lifetime/metadata custody only; see
[the versioned factory contract](original-pipeline-factory-registration-contract.md).
Explicit original layout/graphics/compute factories reuse the existing registry,
reserve before backend calls, retain the canonical core layout owner and copy
package metadata. Ordinary objects stay unregistered. Native-loss-bound typed
creation fails with `RHI-ORIGINAL-PIPELINE-NATIVE-LOSS-JOIN-MISSING`. Host12/12,
Wasm/browser8/8 and inventory179/179 pass, preserving Null hidden smoke and prior
map/publication checks. Descriptor/Query factory/content joins, full immutable
pipeline artifacts, typed Source/Inputs and native execution remain open; no
typed Backend Validated, Platform Validated or Production Ready promotion.
