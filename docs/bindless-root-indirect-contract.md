# Original Bindless Root indirect join

The current SnapshotScope/1 increment resolves every fault clear from its actual
Root in Vulkan, Metal and WebGPU, and authenticates each WebGPU fault copy against
that binding's original leases and exact four-byte range. Readback sequences
retain one fault per represented snapshot and cover all consumed Roots. This
preserves the original journal and completion path; produced execution capability
remains closed. GPU-driven renderer migration follows WOCC scene integration and
does not expand the pre-integration direct-scene acceptance scope.

RootState6 and Inputs8 extend the existing original command inventory, inputs,
state graph, stream and journal. They create no replacement resource identities,
state owners, completion signals or execution receipts.

Inputs8 admits prepared compute and graphics bindings in their respective scopes,
index/vertex bindings and finite indexed indirect reads. Ordinary pipeline and
descriptor commands remain forbidden. Every indirect draw consumes the currently
bound graphics Root, its authenticated per-entry declarations, the current index
binding and every declared vertex stream. Pass boundaries and every pipeline
bind invalidate prior bindings.

An indexed indirect operand is the real canonical buffer lease. Its usage,
four-byte alignment, stride, record count and complete byte extent are validated
before any output. Calls and expanded native records have independent explicit
ceilings. The read contributes ShaderRead/DrawIndirect/IndirectCommandRead to the
same state sequence; shader and geometry accesses contribute their original
scopes at the draw command. A ShaderWrite-to-ShaderRead transition with the
original DrawIndirect destination scope is mandatory after storage production.

Address validation does not prove the values written by a GPU producer. Native
admission additionally requires the authenticated producer algorithm and its
immutable input custody, output capacity, fault suppression, index/vertex fetch
bounds, selected firstInstance ABI, and the original native completion join.
Neither CPU expected values nor a claimed write access can satisfy that proof.
GPU-counted draws and indirect dispatch remain closed in this version.

Four-backend lowering:

| Backend | CPU projection | Native execution |
| --- | --- | --- |
| Null | Original inventory/state/stream validator | Refused; validator only |
| Vulkan | Same projection | Refused until producer/content/native custody joins |
| Metal | Same projection | Refused until producer/content/native custody joins |
| WebGPU | Same projection | Refused until producer/content/native custody joins |
| WebGL | Refused | Refused |

Backend command admission continues to reject this new profile until its complete
payload is implemented. Existing profiles retain their exact previous schema and
refusals. This document records the L2 prerequisite; it is not a GPU validation
or a completed engine gate.


RootSequence/1 independently checks every prepared-binding occurrence against
that same original stream. Five repeated graphics Root binds now have a positive
CPU fixture, followed by vertex/index rebinding in the final pipeline epoch.
Unconsumed occurrences, stale generations, forged owners and insufficient visits
fail with an empty report. This does not authorize arbitrary native sequences:
native custody now retains its closures in quota-owned bounded vector storage,
while the public Source adapter forwards a bounded sequence with an exact historical
first/next/final prefix. Existing workload execution profiles remain unchanged.

Fetch/3 expands the actual indirect operand's offset/stride/record count and
checks each closed-producer maximum against the real pipeline epoch, initialized
index bytes and initialized vertex bindings. It shares the direct-fetch walker,
performs complete dry validation before visitor output and charges independent
command/draw/stream/binding-scan/index-analysis ceilings. Root6 stream construction
accepts its already-declared geometry join instead of the former Root5-only test.

The original Vulkan, Metal and WebGPU recorders now contain finite indexed
indirect payload lowering. Vulkan emits one native indirect command per record,
avoiding an extra multiDrawIndirect feature requirement. Metal reuses its existing
indexed-indirect encoder; WebGPU freezes the canonical argument handle and fields
and emits the original bounded record sequence. These private branches remain
behind the unchanged public profile gates. Their existence proves neither
producer bounds nor content initialization or completion.

All three original recorders select Graphics/Compute bindings by canonical Root
Source. WebGPU freezes each selected closure, pipeline and bind group per binding,
rechecks those same fields in Pending and emits that closure's ordinary groups.
No first-Root default supplies foreign pipeline or resource custody.

Root6 ordinary buffer copies now remain in the explicit transfer/state domain;
copies involving snapshot internals keep the exact existing fault-copy checks.
Binding/5 authenticates a storage-buffer readback range against the selected
Root's actual declaration and copy operands. This includes a genuine nonzero
destination-range CPU fixture and rejects legacy binding profiles, undeclared
ranges, duplicate writes and insufficient visits. A non-fault binding cannot
acquire snapshot fault custody.

The existing Result contract joins independent fault/resource requests against
the same program, graph, snapshot/authorization and result quota. Native custody
uses `OriginalBindlessRootReadbackSequence`, allocated cold through the original
quota. Exactly one snapshot fault binding owns the domain. Vulkan/Metal gather
all private bytes before the original commit and publish only after it succeeds;
WebGPU keeps one bounded native storage/map record per original binding. Existing
public inputs now forward an authenticated result set into all three cold native
factories. Existing workload profiles retain their previous execution scope;
multi-result Root6 admission and authentic lifecycle validation remain unfinished.

Native sequence custody now accepts an explicit bounded input. Cold factories
authenticate the whole original stream/inventory/generation before retaining it;
historical first/next/final entries must match the exact prefix. The command owns
its copied collection, and Pending never borrows the caller's array. Extra Root
and Result owners are preflighted against the existing lifecycle ceilings with
overflow-safe arithmetic. This storage support does not broaden a program gate.

Produced Geometry2 uses independent Lifecycle21 (fence), Lifecycle22 (callback)
and Completion15 contracts over Content9/Root6/Inputs8. A historical Geometry1
cannot obtain these seals, and a produced geometry root cannot borrow a legacy
lifecycle. Program7 describes compute, indexed-indirect and transfer in one
original stream; its Geometry2 extension is mandatory. Direct draws, counted
indirect and indirect dispatch stay refused. Native admission/issuers remain
closed until the complete authenticated producer/content/fetch join is wired.
Callback completion now checks the exact original BufferCopy count against map
requirements, rather than asserting one map. Genuine callback drain and the
original private commit remain mandatory; caller counts grant no completion.

Public Callback/Fence sequence inputs optionally declare one closure plan per
distinct authentic Root. The table must exactly cover its sequence, rejects
duplicates/foreign owners/invalid budgets, and has a separate explicit cold scan
ceiling. Vulkan, Metal and WebGPU select each Root's budget before creating its
closure; native custody copies budgets into the original authentic closures. The
borrowed plan table is never used in Pending. Sequence preparation stages its
owner collection privately and swaps it into the output only after all factories
succeed. Neither these plans nor the sequence adapter broaden execution gates.

Root completion now selects its submission version from the immutable seal's
lifecycle through OriginalBindlessRootCompletionProfile. Both fence backends
consume the same private seal method; Metal no longer infers storage/mixed
completion from the number of next/final closures. Callback commit consumes the
same profile only after genuine original drain inspection. Lifecycle21 and22
map to Completion15 with separate fence/callback evidence requirements. Unknown,
cold-only and upload lifecycles cannot use this Root completion selector. These
changes complete profile wiring only; they do not open produced native admission.


2026-10-20 development batch: the pure closed visibility ABI and cold inspectors
now live in `rhi/frontend/visibility`, with source-compatible `pcg::` aliases.
Original cold inputs carry a finite produced-geometry correspondence; the three
factories authenticate their real initialized actors before creating the original
seal. No caller certificate, additional inventory, journal or completion issuer
is introduced. This join does not open Root6 execution: native attachment,
workload and lifecycle admission is still unfinished.

WebGPU original buffer mapping now freezes allocation/offset/size and preserves
the actual readback request's finite interval in both native map and read calls.
Retained validation checks all three values; concurrent mappings of one physical
destination are refused. The historical whole-buffer entry remains explicit.
These changes have syntax/contract evidence only until the integrated batch is
frozen and subjected to genuine three-backend validation.


The current development batch adds GraphicsPass/2 over the same produced stream.
The selected command ordinal and every graphics binding inside that scope must
name the authentic retained Root. Vulkan/Metal cold factories build the complete
pass collection before preparing the seal; failure destroys the temporary
collection and returns its capacity charges. The original command retains its
own pass vector through Pending/completion. Native recording only consumes
these prepared pass objects, with no per-pass creation. Metal geometry binding
now resolves its initialized actor from the currently bound graphics closure.

WebGPU Admission7 now checks the four compute producers separately from the
indirect graphics consumers, requires Fetch3 and an exact Lifecycle22 profile,
and accounts for initialized actors in all historical prefix closures. Original
record preparation resolves attachments through the complete retained closure
set and associates pushes with the selected graphics Root. Lifecycle22 uses the
same callback publication domain, which still requires genuine map/scope/queue
drain. These branches do not open Program7/Lifecycle21/22 lowering or publish a
new executable capability. Native workload admission and production shader
families remain unfinished; integrated GPU/platform validation is deferred until
the complete engine code batch has been frozen.
