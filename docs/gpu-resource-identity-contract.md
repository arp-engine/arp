# ResourceIdentity/1

L2 identity contract: every engine-created GpuResourceId is nonzero and unique
across devices, resource kinds, heaps, and acquired images in one linked RHI
runtime. IDs are opaque, are not serialized asset/network identifiers, and are
never recycled when wrappers, devices, or swapchains are destroyed. Resource
creation order and contiguous values are not public semantics. Exhaustion must
raise RHI-RESOURCE-ID-EXHAUSTED without wrapping or creating an object with ID 0.
The 64-bit public layout remains unchanged.

A descriptor resolves an ID only in its owning device registry. A foreign ID
must fail validation; a rejected update preserves the previous valid binding.
Uniqueness does not extend resource liveness: retaining native storage for a
recorded command is separate from permission to bind a destroyed wrapper ID.
Parent/view liveness and record-time revalidation remain separate contracts.

## Lowering

| Backend | Identity lowering | Descriptor gate | Validation scope |
| --- | --- | --- | --- |
| Null | Shared RHI monotonic allocator, including validator heaps and images | Local registry only | Validator; no native claim |
| Vulkan | Same allocator for resources, views, samplers, heaps and swapchain images | Local Vulkan registry | Native execution requires Vulkan runtime evidence |
| Metal | Same allocator for resources, views, samplers, heaps and drawable wrappers | Local Metal registry | Native resources and set/table writes on macOS |
| WebGPU | Same allocator for resources, views, samplers and canvas wrappers; unsupported heaps remain rejected | Local WebGPU registry | Metadata tests do not establish browser validation |

The allocator has one out-of-line exported definition in arp_rhi. Backends and
plugins must link that runtime, not embed private ID counters or independent
copies of an RHI runtime and exchange objects between them. No native handle,
backend tag, allocator ownership, or device-selection branch enters consumers.
Core owns the reusable saturating sequence. Concurrent identity allocation is
safe; this does not make device APIs generally thread-safe.

Tests cover two devices with matching creation order, foreign set/table writes,
rejected overwrite preservation, cross-backend IDs, concurrent allocation, and
sequence exhaustion. Swapchain/heap and device recreation must not reuse IDs.
Native lifetime, stale-parent and depth table semantics are not inferred from
these identity tests. Status remains scoped to the collected evidence.

## DescriptorResourceLiveness/1

Writes and subsequent validation of descriptor sets/elements/tables resolve
the resource's live public wrapper in the arena's owning registry. A destroyed
wrapper ID fails even if a descriptor, view or previously recorded command
retains the native allocation. Identical-write caches cannot bypass this check.
Recreating a same-shaped resource does not resurrect a stale binding; only an
explicit successful write of the new ID updates it. Arena reset invalidates both
set and table validation. Null already provides this validation model; Metal and
WebGPU must re-resolve instead of just checking their written flags; Vulkan must
unregister public wrapper lifetimes independently of weak native payload life
and check registry membership before both synthetic and native writes.

This contract does not allow concurrent wrapper destruction during recording,
redefine parent/view liveness, or imply new native descriptor-table support.
Existing recorded commands keep their native storage ownership and budgets.
Native Vulkan table execution remains a separate acceptance gate; metadata
table tests cannot establish it. Required command-consumption validation and
parent-view rules require separate tests before the whole T8 gate closes.

## DescriptorParentLiveness/1

A texture view's public permission depends on its original parent texture ID
remaining registered in the same device. SampledTexture, StorageTexture and
supported InputAttachment writes, cached rewrites, binding validation and
draw/dispatch consumption reject an orphan with
`RHI-DESCRIPTOR-PARENT-NOT-LIVE`. A failed write preserves content, revision and
allocation accounting; a failed bind/consume preserves prior legal state,
command counts and pending push constants. Same-shaped replacement textures
have new IDs and cannot revive the old view. View destruction independently
remains `RHI-DESCRIPTOR-RESOURCE-NOT-LIVE`.

This enforces the existing layered-design parent rule. It does not revoke storage
already retained by legal recording: recorded/submitted work keeps its original
native resources through retirement, with one charge per retained allocation.
Placed resources retain the existing heap ownership contract. Public validation
must not filter generic native storage lookups or introduce native objects above
the backend. Recording and public destruction remain externally serialized.

| Backend | Lowering and execution gate |
| --- | --- |
| Null | Resolve view and parent Texture kind in the owning registry for all supported texture descriptor types; no native claim |
| Metal | Store original parent ID with view metadata; resolve its registered texture payload under the registry lock, then reject orphan descriptors before publication or recording; retain recorded native resources independently |
| Vulkan | Store original parent ID in the view registry entry; validate that parent remains a registered image before cached/native writes and set/table validation; keep recorded storage ownership unchanged; native table limitations remain explicit |
| WebGPU | Store original parent ID and explicit texture-object registration metadata; revalidate the parent before writes/consumption, including explicit sampled lowering of InputAttachment; metadata fixtures do not prove browser execution |

Tests must distinguish fresh/cached writes, set/table, parent-only/view-only
destruction, consumption after bind, recovery with a different live view, and
recorded retention. Native capability or visual acceptance is not inferred from
registry metadata checks. Each result records the actual backend/test scope.

## DescriptorArrayElements/1

The pair (logical binding, arrayElement) identifies one descriptor element.
Elements occupy independent storage; writes validate `arrayElement < arrayCount`
before modifying public/native state. Scalars require element zero. Unknown
bindings and out-of-range elements reject without changing revision, content or
budget. Written-element validation checks each written resource independently;
complete-layout validation additionally requires every declared element. An
unused binding does not become mandatory just because another binding is an array.

`hasWrittenBinding` means at least one element is written. `validateBinding`
validates every written element and fails if none is written. Element queries
report only the selected element. Legacy `stats.writtenBindings` counts written
elements (as Metal/WebGPU already do); `bindingCount` remains logical bindings.
Array resource destruction, parent invalidity and cached rewrites follow the
existing liveness/revision/recorded-storage contracts for each selected element.

Arena descriptor capacity is the checked sum of arrayCount, not binding count.
Unrepresentable sums, remaining-capacity exhaustion and table count overflow
reject before accounting or allocation. Fixed per-element vectors derive from
validated capacity; this does not permit unbounded native snapshots or queues.

| Backend | Lowering |
| --- | --- |
| Null | Separate bounded written resource records per element; validate each resource, shape and parent; validator only |
| Vulkan | Native dstBinding plus dstArrayElement; one retained selected resource and auxiliary view per element; recorded-storage rewrite refusal remains unchanged |
| Metal | Existing logical (binding, arrayElement) mapping and actual reflected native slots/argument-buffer indices; corrected arena element accounting; no assumed native support from metadata alone |
| WebGPU | Existing explicit array/bind-group lowering and per-element metadata; corrected capacity accounting; native arrays require supported lowering, otherwise the declared deterministic failure remains |

All backends assess one contract. Depth table expression and dynamic-index native
coverage remain separate D3 gates; this contract does not silently add table or
native descriptor-array capabilities to a backend that cannot execute them.

## DescriptorTableTextureShape/1

DescriptorTableDesc and its PipelineLayout table slot explicitly declare
`depthTexture`, `textureViewDimensionConstrained` and `textureViewDimension`,
using the existing DescriptorBindingDesc meanings and defaults. These are L2
requirements, never inferred from a table name or the first written resource.
Depth requires a sampled depth-format/depth-aspect view (or the separately
supported InputAttachment identity); dimension constraints require a texture
descriptor type. Existing unconstrained color tables retain their defaults.
This does not introduce storage-format, filtering or comparison-sampler table
capabilities; those still follow their existing explicit lowering requirements.

Allocation validates shape before consuming capacity. Each fresh/cached write
and subsequent validation rechecks resource kind, usage, parent/view identity
and declared shape. Failure preserves content, revision, native payload and
accounting. Shader reflection depth identity must exactly match the pipeline
table slot. Bound table/slot depth identity and dimension constraint must match;
capacity/visibility retain their existing coverage rules. Pipeline identity
hashes include shape so caches cannot merge incompatible declarations.

| Backend | Lowering / execution gate |
| --- | --- |
| Null | Existing registry depth/aspect/dimension/usage validator, now consumed by table writes/queries and shared table-slot binding validation; no native claim |
| Metal | Reuse registered view shape validation; native depth textures remain backend-owned argument-buffer entries, and table/slot shape is checked before recording; InputAttachment tables remain explicitly unsupported |
| Vulkan | Table metadata uses the same registered view shape/usage/depth checks; native table recording remains deterministic unsupported, never a synthetic success claimed as execution |
| WebGPU | Propagate table depth/dimension to the physical bind-group layout as well as its shader mapping; validate table/slot before native bind; unavailable native array lowering remains an explicit failure |

Tests cover color-as-depth, wrong aspect/dimension/usage, foreign/expired parent
or view, first/last elements, cached writes, failed-update preservation, shape
mismatch at reflection/bind and hash separation. Native acceptance requires a
generated Slang shader dynamically reading known depth values through the actual
table; metadata or a sampled set cannot prove table execution. Recorded storage
and budget contracts remain unchanged. Native comparison sampling and other
binding/command combinations require their own coverage.

## DescriptorArenaLifetime/1

An arena owns permission to allocate and to use its current allocation generation.
Reset invalidates all earlier set/table allocations; destruction invalidates all
allocations. Surviving descriptor wrappers remain safe to inspect and destroy,
but isCurrentAllocation and written queries return false, written statistics are
zero, and writes/validation reject with RHI-DESCRIPTOR-ALLOCATION-STALE. Immutable
allocation/layout metadata remains inspectable and does not authorize execution.
Exhaustion permanently rejects allocation with RHI-DESCRIPTOR-GENERATION-EXHAUSTED;
generation values cannot wrap and revive an older allocation.

Arena operations, descriptor operations and recording are externally serialized.
No new permission to reset/rewrite resources still consumed by pending GPU work
is introduced. A successfully recorded native command retains the payload it
already owns independently of this allocation ticket, through completion or
abandonment. Destruction cannot release native pools/storage still owned by that
command. Subsequent binding/recording must validate the allocation again. This
change does not redefine which unreferenced layout slots require content, nor
close the separate audit of all command-consumption gates.

| Backend | Allocation lifetime lowering | Native storage rule |
| --- | --- | --- |
| Null | Shared core epoch owner; weak immutable ticket; reset/destruction/exhaustion reject | Validator only, no GPU completion claim |
| Vulkan | Same ticket replaces the raw pointer into the arena generation | Descriptor payload/allocator ownership remains independent; no pool reset or dummy payload added |
| Metal | Same ticket replaces strong generation retention | Recorded descriptor-group/table payload ownership remains independent |
| WebGPU | Same ticket replaces strong generation retention | Existing bind-group payload retention remains independent; metadata is not native evidence |

The reusable epoch utility belongs to core and uses core allocation once per
owner, no allocation per reset. Tickets never prolong the owner lifetime. Tests
cover stale sets and tables after reset/destruction, new-generation allocation,
non-resurrection after owner recreation, and saturation at the integer boundary.
Backend/Platform/Production maturity requires its own native evidence; this
contract alone does not promote any of those statuses.

## DescriptorBindingLiveness/1

Every executable descriptor-set/table bind revalidates its allocation and all
written resource identities before retaining payloads, updating bound state or
recording a native command. A cached native group/set is not permission to use
a destroyed wrapper. A rejected bind does not increment successful command count
or replace the preceding binding. Existing diagnostics propagate unchanged.

This is a minimum binding gate, distinct from shader-reflection completeness.
It does not require previously optional/unreferenced slots to become populated.
Null and Vulkan validate WrittenElements here; Null retains its reflection-driven
draw/dispatch completeness gate. Metal retains its existing CompleteLayout gate;
WebGPU sets validate WrittenElements before their existing executable bind-group
requirements, and tables retain CompleteLayout. No backend silently skips a
written invalid resource. Array-element behavior remains subject to the existing
backend array contract and capability; this gate adds no array lowering support.

| Entry | Null | Vulkan | Metal | WebGPU |
| --- | --- | --- | --- | --- |
| Render/compute set | WrittenElements | WrittenElements before native recording | CompleteLayout | WrittenElements before native recording |
| Render/compute table | WrittenElements | Existing deterministic unsupported lowering | CompleteLayout | CompleteLayout |
| Render-bundle set | Existing unsupported default | Existing unsupported default | Existing unsupported default | WrittenElements before native bundle recording |
| Secondary descriptor state | No separate descriptor bind API | Same | Same; consuming primary pass owns inherited bindings | Same |

The common validator is RHI-domain logic with bounded traversal and no owned
allocation on success. It checks allocation currency even for empty layouts.
Allocation/resource failure occurs before native payload extraction; unsupported
entry points keep their explicit rejection instead of simulated success.

This batch does not redefine bind-to-draw mutation semantics or command payload
snapshots. Callers must preserve the existing externally serialized recording
and in-flight mutation rules. Null still detects resources destroyed after bind
at draw/dispatch. The cross-backend audit of that later boundary, descriptor
wrapper destruction during an open pass, arrays and parent/view rules remains
open; bind-entry tests alone cannot close all command-consumption acceptance.

## DescriptorConsumptionLiveness/1

Recording and wrapper destruction are externally serialized. Between a successful
bind and its draw/dispatch, the bound descriptor object, allocation generation
and written resources must remain live. A dead wrapper must be rejected without
dereferencing it (RHI-DESCRIPTOR-OBJECT-NOT-LIVE); arena/resource failures preserve
their existing diagnostics. Rejection does not record work or consume pending
push constants. A successful rebind replaces that slot's observation.

Descriptor wrappers expose a core weak lifetime ticket, independent of arena
generation and native payload ownership. The common RHI recorder reuses Null's
existing bounded 32-set/32-table slot tracking, with capacity checked before
native binding. Numeric slot indices are not restricted to [0,32). Successful
recording captures the required native storage under the existing backend
contract; later destruction does not invalidate already recorded native work.

| Backend | Consumption check | Native lowering boundary |
| --- | --- | --- |
| Null | Safe observations replace raw set/table pointers; reflection completeness remains | Validator, no native execution claim |
| Vulkan | Render/compute direct and indirect consumers revalidate tracked sets | Existing native commands retain their payload; table/bundle unsupported paths stay rejected |
| Metal | Render/compute direct and indirect consumers and inherited secondary/bundle use revalidate tracked bindings | Native command payload ownership remains unchanged |
| WebGPU | Render/compute consumers and bundle recording revalidate tracked bindings | Completed native bundles own their recorded state; execution does not resurrect wrapper requirements |

No new per-draw allocation or resource ownership enters upper layers. A wrapper
allocates its bounded lifetime state through core on creation; observation and
validation do not allocate on success. Resetting the tracker releases its weak
references. This is an object/resource liveness gate, not a revision/snapshot
contract for descriptor rewrites. Bind-to-consume mutation and in-flight native
descriptor replacement still need explicit closure; this batch does not declare
those operations safe or authorize previously forbidden in-flight writes.

## DescriptorContentRevision/1

Every accepted set/table write is a new logical content revision, including an
identical write optimized to avoid a native update. A failed write preserves
the previous revision and content. A successful bind observes that revision;
draw/dispatch using an older observation rejects with
`RHI-DESCRIPTOR-CONTENT-CHANGED` before recording/counting work or consuming push
constants. The caller must rebind after a successful write. This is independent
of wrapper lifetime, arena generation and resource identity validation.

Revision advancement is externally serialized with writes/binds/recording. It
uses a bounded, non-recycling core sequence, preflights exhaustion before any
backend mutation, and advances only after successful backend commit. Exhaustion
returns `RHI-DESCRIPTOR-REVISION-EXHAUSTED` and preserves the final valid revision.
Tracking/validation adds no allocation to successful bind/consume paths.

| Backend | Logical revision / consumption | Write failure boundary |
| --- | --- | --- |
| Null | Shared public write gate and bounded recording observation | Validate before metadata commit |
| Vulkan | Same gate including cached identical writes; render/compute rebind required | Validate before native update; recorded/in-flight mutation still needs separate closure |
| Metal | Same gate; direct/table render/compute and inherited recording observation | Build replacement payload before publishing metadata/payload |
| WebGPU | Same gate; render/compute and bundle recording observation | Build required replacement group before publishing metadata/payload; unavailable native runtime rejects required payload |

This contract closes bind-to-consume revision ambiguity, not native in-flight
snapshot/retirement. It does not grant permission to update a native descriptor
referenced by recorded work. Native recorded/in-flight preservation, snapshot
budget and retirement remain C1b-2 work, with separate native evidence required.

## DescriptorRecordedStorage/1

Successfully recorded descriptor use retains all native descriptor storage,
selected buffer/view/sampler storage and auxiliary native views until the command
is discarded safely or its GPU work has completed and retention is released.
Public wrapper/arena destruction only revokes new recording permission. Resource
budget release follows the final native storage owner, not the public wrapper.

A successful later write must not change an earlier recorded operation's native
bindings. Backend lowering must either publish independent immutable binding
storage or reject an attempted in-place mutation before updating native/metadata
state with `RHI-DESCRIPTOR-RECORDED-STORAGE-IN-USE`. Identical writes may reuse the
physical payload after normal liveness validation; the public logical revision
still advances and requires rebind. Rejection preserves the old revision/content.

| Backend | Recorded storage | Mutation lowering |
| --- | --- | --- |
| Null | Validator only; no native storage claim | Logical revisions invalidate old bind observations |
| Vulkan | One backend-owned storage object retains the native set, selected resources and created views; command retention owns that object | Changed in-place writes reject while storage has a recording/retention owner; no hidden snapshot allocation. After retirement/discard releases owners, rewrite is allowed |
| Metal | Native commands retain resources and independently encoded argument buffers | Replacement descriptor metadata cannot alter prior native bindings; A/B GPU output must demonstrate this |
| WebGPU | Immutable group/resource payloads and recorded native groups own their bindings | Replacement group publication is independent; native browser evidence remains required |

Vulkan view ownership uses one current view per supported binding; safe rewrites
destroy the replaced view immediately after the native update, rather than
growing an unbounded retired-view list. The storage holds its runtime alive until
native destruction completes. Vulkan arrays/table execution and immutable-copy
snapshot capacity are separate contracts, not implied by this scalar storage
fix. Bounded command-retention queues and complete frame-peak budgets remain T4.
