# Canonical original typed Source assembly /1

Status: Contract Complete / Prototype Complete for the CPU ownership and
publication contract below, verified 2026-10-05 host15/15, Wasm/browser8/8 and
inventory182/182. Canonical descriptor commands still require
original Pipeline definitions, exact positional Inputs, shader footprint/state
joins and backend payloads. No native execution capability changes.

`OriginalCommandResourceSources` remains the one canonical Source and lifecycle
capacity owner. A separately versioned, explicit Prototype cold assembly reserves
all resource and descriptor slots once, using the original Source quota and the
authored per-domain ceilings. Existing preparation returns a sealed resource-only
inventory. Cold typed preparation returns the same quota-bound mutable owner;
only registered `/2` DescriptorSet/Table sources may be appended. No inventory,
root, state capacity, seal or journal is cloned or replaced. No graph has been
published during this cold phase. Every existing resource index remains stable.

Descriptor Source `/1` remains the previous explicit Prototype snapshot custody
contract and cannot enter canonical typed assembly. Descriptor Source `/2` keeps
the exact original arena, adopted descriptor and complete successful-write
observations, but retains each referenced original Buffer/View lease directly.
These are dependencies on the existing canonical resource owners, not a second
source table. It weakly observes the exact canonical Source control block. Every
dependency joins its unchanged source index and exact original owner. The arena,
view-parent, registration, resource shape, generation, access and content gates
retain their existing semantics. Source expiry, foreign same-valued inventories,
ordinary/forged owners and changed content fail deterministically. No strong
ownership cycle can exist between the canonical Source and its typed children.

All append checks precede mutation. Append consumes one pre-reserved slot, adds
no allocation and preserves previous elements on failure. Domain and identity
checks, genuine facade validation and exact resource indices remain mandatory.
Typed wrappers have no GPU IDs. Their factory registration generation and exact
object/control block participate in a distinct identity namespace. Resource IDs,
CPU snapshot pointer keys and typed factory generations must never alias.

Successful publication of the existing `OriginalCommandUsage` seals the Source.
Allocation or preflight failure before publication leaves it cold. Sealing changes
only the Source publication flag; it does not signal completion or create a second
recording seal. Once sealed, appending fails even after the graph's caller drops.
Externally serialized Source assembly and inspection remain required.

CPU graph owner accounting counts the existing resource closures, each descriptor
Source/wrapper and each distinct exact arena once. Original registration accounting
counts resource records, typed wrapper records and each arena record once, then
adds the existing fixed registry/device closure. Use `SharedIdentityProjection`;
no ad-hoc sets, secondary registries, native handles or manufactured GPU IDs.
Direct per-element dependency references reuse the already counted resource
owners. Neither exclusive descriptor budget nor metadata bytes prove graph
owner capacity. Source/Graph quotas retain independent owner identities.

| Backend | Cold assembly/CPU reference identity | Typed command/state/native consumption |
| --- | --- | --- |
| Null | Equivalent; validator only | DiagnosticFailure until independent joins |
| Vulkan | Equivalent over genuine original objects | DiagnosticFailure until independent joins |
| Metal | Equivalent over genuine original objects | DiagnosticFailure until independent joins |
| WebGPU | Equivalent over genuine original objects | DiagnosticFailure until independent joins |

Existing resource-state and native profile consumers must explicitly reject
uncovered typed intents. Descriptor records are not resource state cells. Keep
the existing order validator: descriptor bind still requires an original bound
pipeline. Missing Pipeline sources cannot be replaced with a fixture or a dummy
bind to demonstrate a successful typed command graph. `OriginalCommandUsage`
can retain a typed Source alongside a genuine resource-only graph, but that
does not prove descriptor command admission or native payload availability.

Tests must prove same Source address/control block and unchanged resource indices,
slot/domain/owner capacity, exact/one-less/exhausted cold preparation, append under
full quotas with allocation0, failed publication remaining cold, successful
publication sealing permanently, duplicate/foreign/forged/changed/expired inputs,
arena and record deduplication, original resource retention after caller drop,
and no Source/child cycle on final release. Preserve quick unit/contract, Null
hidden-frame and both browser smokes with prior real map/publication checks.
Native proof issuer friends remain exactly Recording and Submission. No visible
rendering or shader change; no Backend/Platform/Production readiness promotion.

Both browser modes require one Null and one WebGPU assembly marker plus the
existing mandatory map/publication checks; diagnostics/nativeErrors are empty.
Host root/set/table peaks are 648/836/488 bytes; Wasm Null 520/656/340 and
WebGPU scalar 520/692/340. Exact/one-less/exhausted cold quotas, append and
validation allocation0, owners14/records6/total23, failed publication remaining
cold, successful publication sealing and final cycle-free release pass. Vulkan
common source syntax is checked; CPU evidence cannot prove typed native payloads.
Evidence: `build/web-bindless-apple-debug/artifacts/bindless-continuation/typed-source-assembly-validation.yaml`.
