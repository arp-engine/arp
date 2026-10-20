# Canonical original typed sources and inputs /1

Status: **Contract Complete** for the L2 declarations, ownership obligations,
field/reference relationships, budgets and four closed lowering outcomes below.
CPU validators do not establish Prototype Complete owned adapters, Backend
Validated typed native execution, Platform Validated or Production Ready.

Implementation entry points are `OriginalCommandTypedSourceContract` and
`OriginalCommandTypedInputContract`. They reuse the existing original Source,
resource registration, command scalar/variable fields, descriptor layout,
resource/access and Query declaration validators. Declaration validation is
cold; callers charge any diagnostic allocation to their original Source quota.
No validator issues a ticket, accepts a native handle, creates GPU work or
publishes completion. In particular `generatedReflectionHydrated`, hashes,
stage enums, nonzero generation labels and successful QuerySet creation are
not original factory/artifact/execution authority.

## Ownership and immutable definition boundary

| Source domain | Original owner | Required retained immutable relationships |
| --- | --- | --- |
| Pipeline | `SharedOriginal` | Exact factory wrapper and control block; logical device lifetime and nonrecycled factory generation; graphics/compute kind; original layout; complete selected pipeline descriptor; generated shader artifact owners and selected entry reflection |
| Descriptor | `UniqueObject` | Adopted original DescriptorSet; issuing original arena/device and arena allocation generation; allocation shape; complete layout; content revision; current written elements; exact referenced Buffer/View/Sampler Source owners |
| DescriptorTable | `UniqueObject` | Adopted original DescriptorTable; same arena/device/generation obligations; full table descriptor; content revision; each written logical index and exact resource Source owner |
| Query | `PreparedOriginal` | Exact original Query input snapshot and factory-created query owner; type/count/statistics mask/timing scope IDs/label; original device and generation; selected command-point and resolve/result lifetime contract |

All four require genuine factory provenance. A caller subclass, borrowed native
observation, aliased/forged shared pointer, original-looking ID or equal metadata
cannot substitute. `core::ColdOwnedObjectSlot`, `core::SharedObjectIdentity`,
quota-bound identity and existing registration are reuse candidates for adapters.
An adapter must retain original allocation ownership instead of reconstructing
the wrapper. There is no second root, graph, journal or ordinary transaction.

Pipeline definitions freeze the full authored graphics state including vertex
layouts, raster/front-face, color/blend/write-mask, depth/stencil, MSAA,
viewport/scissor policy, render-pass/subpass compatibility and tessellation ABI,
or the complete compute descriptor and reflected workgroup dimensions. Existing
pipeline-state validators remain mandatory. Raw descriptor pointers cannot be
retained without their exact original owners. Debug labels are copied under the
same bounded metadata ownership, never used as binding identity.

Artifact identity must come from the existing validated artifact loader/issued
factory record. Retain the exact source/target/reflection publication bytes and
selected entry metadata; compare source SHA, entry/stage, physical mapping,
matrix layout contract, push members and source/target identity at the unified
boundary. `stableShaderPackageHash` is a cache key, not an exact identity proof.
Paths and a public hydration flag cannot establish that the issued pipeline
consumes those bytes. Multi-entry packages require selected-entry reflection;
module-wide reflection alone must not prove a selected entry footprint.

## Descriptor resource and access closure

Each written set element freezes the complete `DescriptorBindingWrite`, its
logical binding/array element, canonical resource Source index, exact original
wrapper/owner and issued registration generation. Table elements freeze the
complete `DescriptorTableWrite` and the same resource relationships. Validate
one write per logical element, indices/types/array capacities, complete authored
binding shape, resource kind/ID, usage, view-parent ownership, dimensions,
depth/aspect and storage format. A resource Source index is neither an ID nor
a backend binding index. Sampler is a required dependency domain; until its
original adapter exists, sampler references fail deterministically.

The `/1` Element carries explicit shader stage/access declarations. They must
fit both the binding visibility and declared resource state; UniformBuffer,
ReadOnlyStorageBuffer and sampled textures are reads. Storage buffer access is
explicit and storage texture access must fit its authored access mode. These
declarations never prove actual shader accesses. Before native recording, join
the exact retained artifact's validated footprint to this element, its buffer
range or view subresources, all dynamic offsets and the state **before that
original command index** using the existing original plan/journal. Unknown or
unbounded shader footprint rejects; it cannot be estimated from descriptor size.
Sealed GPU writes, alias hazards, memory ownership, capabilities and native
size/alignment limits remain mandatory independent gates.

DescriptorSet freezing includes *written* elements, preserving optional unused
slots. Each draw/dispatch must independently prove every reflected element it
uses is written and still joins the frozen descriptor revision. Any subsequent
successful write, arena reset, lifetime change, native payload replacement,
changed generation or foreign resource invalidates the join, even if contents
appear identical. A revision exhaustion failure cannot publish new contents.

L2 `DescriptorTableWrite` contains no state/access field. `/1` only defines
sampled read/sampler tables; storage tables fail
`RHI-ORIGINAL-TYPED-TABLE-ACCESS-UNDECLARED`. Expanding this needs an explicit
versioned L2 access declaration. InputAttachment/physical-lowering writes fail
`RHI-ORIGINAL-TYPED-DESCRIPTOR-LOWERING-UNBOUND` until an exact original shader,
logical attachment and approved backend lowering join exists. No hash-only ticket.

## Positional command contract

| Commands | Frozen fields and references | Consumption joins |
| --- | --- | --- |
| Bind graphics/compute pipeline | Existing None scalar plus exactly one Pipeline source | Exact original definition, matching graphics/compute kind; pass compatibility and selected shader ABI |
| Bind render/compute descriptor | Logical set scalar, ordered dynamic-offset array, exactly one Descriptor source | Exact layout slot, full authored layout and revision/element closure; offset count, order, queried alignment and range |
| Bind render/compute table | Logical set scalar, exactly one DescriptorTable source | Declared table slot/type/shape/visibility/capacity and frozen revision/element closure |
| Set viewport/scissor | Existing complete scalar fields, no source reference | Bound graphics definition, explicit static/dynamic policy and validated target extent |
| Bind vertex/index/tessellation buffer | Slot/offset or index format/offset or offset/stride, one Buffer source | Exact original buffer usage/range, graphics ABI and state/access at command index |
| Draw / DrawIndexed | All existing scalar arguments, no new direct references | Current pipeline, pass/subpass/attachments, required descriptor elements, push block, vertex/index/tessellation ranges; signed vertex offset remains exact |
| Indirect draw/count / dispatch | Complete existing scalar arguments and existing Buffer signatures | Exact indirect ABI, original buffer ranges/alignment, limits and state; GPU-authored values need their existing producer/lifetime contract |
| Dispatch | All three workgroup counts, no direct references | Exact compute definition, compiled threadgroup dimensions, queried native limits and proven descriptor footprints |
| Push constants | Stages/offset/size plus immutable bytes and Upload signature | Exact original pipeline full-block push ABI, reflection members, shared matrix contract and approved native lowering |
| Timestamp / statistics | Query index plus exactly one Query source | Same original query input/owner/type; original pass/pipeline command point; queried operation capability, slot lifetime, resolve/result contract |

Use `CommandUsageContract::validReferences`, `CommandUsageSequenceContract`,
`CommandRecordingOrder`, `CommandVariableArguments` and `core::PreflightReplay`.
Do not implement another parser, traversal or command-order machine. The
original positional Input binds the scalar, variable fields, typed definition
and exact reference span to the same original command index. Missing/extra/
reordered references or same-valued foreign definitions reject before allocation
or native consumption. Changing `Scalar` classification is not admission.

Query validation reuses `QueryPoolOriginalInputArguments` and original creation/
source lowering. Timestamp feature/QuerySet creation is insufficient for an
arbitrary timestamp command point; actual encoder/pass-write and resolve/map
API queries still control operation availability. WebGPU pipeline statistics
remain explicitly Degraded under the existing original Query source contract.
Canonical typed Query execution remains closed even when all API facts hold.

## Independent budgets and native lowering

The typed Budget is versioned and opt-in. Bound pipeline definitions, descriptor
elements, Query slots, retained artifact bytes and access declarations. Match
Source/Graph/Recording/Callback CPU limits to the exact original Source budget
and use distinct quota objects **and** distinct owning control blocks. Equal
limits do not prove independence. None of these quotas substitutes for resource,
staging, result, device or submission quota obligations.

`OriginalCommandAdmissionBudget::maxTypedNativeCommandCalls` is an explicit
Source-authored ceiling, default zero. Match it, native payload-object limits,
callback-ticket and registration limits exactly when deriving typed lowering.
Native calls never inherit the L2 scalar count. A backend-private immutable
profile must enumerate its actual bind/draw/dispatch/query/pass/encoder/finish,
error-scope and completion call expansion and pending userdata from approved
operation contracts; all requirements must fit the original ceilings before
native work. The Requirements validator only proves capacity, not correct
lowering or native issuance. Nonzero native demand requires bounded Recording
and Callback storage. Queue/loss/map obligations keep their existing contracts.

| Backend | Typed source/recording/execution `/1` outcome |
| --- | --- |
| Null | ValidatorOnly; explicit DiagnosticFailure gate, no native payload or completion |
| Vulkan | DiagnosticFailure `RHI-ORIGINAL-TYPED-SOURCE-VULKAN-PAYLOAD-MISSING` |
| Metal | DiagnosticFailure `RHI-ORIGINAL-TYPED-SOURCE-METAL-PAYLOAD-MISSING` |
| WebGPU | DiagnosticFailure `RHI-ORIGINAL-TYPED-SOURCE-WEBGPU-PAYLOAD-MISSING`; existing ordinary handles and selected readback `/5` do not grant typed authority |

Future native profile opt-in must consume genuine adapters through the existing
canonical Recording/Submission State, root, seal, journal and quotas. Keep
`WebGpuRecordedCommandProof`'s two existing issuer friends. Scope failure before
submit drains and rolls back; possible submit/cancel/error/unknown callback
registration quarantines until actual queue/loss and userdata drain. No CpuReplay,
Dummy or placeholder; no silent downgrade or default capability promotion.

## Verification

Quick contract/unit fixture: `OriginalCommandTypedSourceContractFixture`, in
`arp_rhi_command_retention_contract_tests`; original registered resource joins
also run alongside the two-frame Null hidden-window smoke. The WebGPU browser
suite executes CPU contract checks without claiming native typed execution.
Negative checks cover versions/opt-in, independent domains, Source/native demand
mismatch, exact/one-less capacities, missing/forged/foreign original sources,
changed registration generation, array duplicate/bounds, resource usage/access,
entry/reflection/workgroup mismatch, stale descriptor arena and Query index.
No shader or visible rendering behavior changes in this contract stage; native
typed output and RenderDoc acceptance belong to the subsequent execution stage.

The first d3d2 ownership prerequisite extends the existing `pipeline_state_copy.h`
with `PipelineLayoutStateCopy`, `ShaderReflectionStateCopy` and
`ShaderPackageStateCopy`. Each cold copy owns all nested layout names/bindings,
set/table names, selected entries and reflection member names/array dimensions
under core PMR allocation. `OriginalPipelineMetadataCopyFixture` disables the
ambient std PMR default, verifies deep independence after caller mutation and
measures actual metadata quota peaks for exact/one-less/exhausted checks. This
is **Prototype Complete** CPU metadata copying only. It does not load artifact
bytes, preserve an unowned layout wrapper, issue a factory receipt, or create
an original Pipeline Source adapter. Typed native lowering remains closed.

The next prerequisite is [Original pipeline factory registration /2](original-pipeline-factory-registration-contract.md).
Explicit GpuDevice original factories issue logical layout/graphics/compute
receipts through the same registry and reservation machinery as /1 resources.
They retain the exact core shared layout and independently copied shader package
metadata; ordinary pipeline factories remain unregistered. Native-loss-bound
typed creation rejects before the backend call. This is **Prototype Complete**
CPU factory lifetime and metadata custody only. Host12/12, Wasm/browser8/8 and
inventory179/179 pass; genuine WebGPU logical layout/foreign-facade and loss-gate
probes are required by both browser modes. No immutable artifact bytes, complete
frozen pipeline definition, original Descriptor/Query Source or typed execution
is inferred. d3d2 now continues with original arena identity and precise complete
write observations at the existing successful final write boundary.

The descriptor prerequisites are now **Prototype Complete** for CPU original
factory/epoch/content and adopted frozen owner closure only. See
[factory/content /3](original-descriptor-factory-content-contract.md) and
[Source freeze /1](original-descriptor-source-freeze-contract.md). Host15/15,
Wasm/browser8/8 and inventory181/181 pass. Both browser modes require genuine
Null/WebGPU freeze checks and actual required-array payload rejection. Full
arrays are validated on Null; WebGPU explicitly authors two scalar bindings.
Canonical source/positional integration, shader artifact footprint/state and
Query ownership remain open. The same original State/root/seal/journal and two
native proof issuers are unchanged; public/general/bindless/NativeSync remain
closed. No typed Backend/Platform/Production promotion.
