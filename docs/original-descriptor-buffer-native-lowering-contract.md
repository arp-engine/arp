# Original finite buffer native lowering

Current closure audit (2026-10-06): f3 is complete for the selected explicitly
opted-in original finite profiles, with Contract Complete / Prototype Complete
and actual WebGPU Backend Validated evidence. Original offset/unchanged-size/
dynamic lineage, ordinary consumer refusals, scoped write transactions and
selected typed execution are mapped in `original-finite-subrange-closure-audit.yaml`.
Null/Vulkan/Metal selected native paths fail deterministically. Generic
OriginalRequest2 and uniform/array/table/ordinary range promotion remain open;
no visual, Platform, Production or NativeSync promotion. The following f3a/f3b
sections preserve their historical implementation checkpoints.

Status: Contract Complete / Prototype Complete for f3a borrowed native entry
construction; Backend Validated for real WebGPU diagnostic mappings only.
f3 production admission and f4 remain incomplete. Quick/Null3/3,
Wasm/both-browser11/11 and inventory204/204 passed; prior host59/59 retained.
Evidence: `build/web-bindless-apple-debug/artifacts/bindless-continuation/runtime-length-entry-lowering-validation.yaml`.
c4 is Contract Complete / Prototype Complete and Backend Validated for numeric
finite intervals.
Production writes/commands, request2, Source/Inputs/state and NativeSync stay
closed. Not Platform Validated / Not Production Ready.

## Approved first implementation boundary: f3a

Extract the two fixture-only native entry mappings into one reusable backend
private stateless lowering. It consumes the exact original GpuDevice, accepted
shared pipeline, original buffer lease, logical set/binding, authored finite range
and frozen dynamic displacement. It must invoke the c4 joint original diagnostic
itself; caller observations, native buffers or copied layouts never authorize it.
Reuse diagnostic request1 with explicit Prototype opt-in. Request2 is not opened.

Only after the complete original diagnostic passes may the lowering obtain the
accepted native pipeline and original buffer payloads. The pipeline's issued
native device and the buffer's native owner device must be identical, and the
native buffer size must equal the original frozen lease size. Missing payloads,
wrong physical device/extent or unknown request reject with stable diagnostics.
No native API call, limit query, allocation, wait or IO belongs in this lowering.

The output is a borrowed native bind entry used immediately by a backend/test
transaction retaining the same original arguments. It is not a receipt, Source,
prepared owner or detached execution permission. Output contains no owned object
or new authority. On failure the entry, group and dynamic displacement clear.
On success physical binding/group come from original target facts, offset is the
authored static base, size is the unchanged authored finite size, and dynamic
displacement is a separate setBindGroup argument. Adding displacement to the
entry base would apply it twice and violates this contract. The effective sum is
used only by the original diagnostic's bounds validation. A non-dynamic binding
permits only displacement0. Exact dynamic count/order still belongs to the
complete group/positional contract and cannot be inferred from a single entry.

| Backend | f3a native entry preparation | Production execution gate |
| --- | --- | --- |
| Null | DiagnosticFailure, validator-only metadata | Closed; no native payload |
| Vulkan | DiagnosticFailure: original interval payload missing | Closed; no accepted original lowering issuer |
| Metal | SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED | Closed; direct buffer offset cannot prove finite length ABI |
| WebGPU | Equivalent mapping under diagnostic1 Prototype opt-in | Closed; Source/Inputs/state and version2 writes missing |

The existing c4 decision is the sole f3a backend eligibility source. Do not add
a capability based on backend name, a native issuer or a second decision table.
The helper must consume that decision and must keep executionAvailable=false.
Original logical and physical lifetime/loss checks remain in their existing
registry/accepted pipeline validators. Native results cannot be retained beyond
those original owners, and uncertain/lost execution cannot become success.

Quick tests keep four-backend decisions, request2/version2 write negative gates
and Null hidden-frame. Genuine WebGPU compute and graphics fixtures must consume
this helper, assert static base/unchanged size/group/binding/separate displacement,
and compare every numeric runtime result already required by c4. Extend their
forged/missing/foreign/quota/retirement/loss negatives to assert cleared native
outputs, and run hot allocation0/query0. Preserve all old browser flags and both
actual modes. This validates entry preparation only, not production emission.

## Remaining f3 and f4 obligations

First consumer prerequisite (f3b1): the ordinary WebGPU native group builder,
compact entry builder and dynamic/bundle buffer validator must consume the
existing shared range write gate before interpreting a buffer's native size or
querying limits. These legacy whole-buffer consumers cannot accept version2
metadata even when passed a hand-constructed backend payload that bypasses the
public DescriptorSet transaction. This is a deterministic rejection, not a new
range implementation or fallback. Native group rejection may perform bounded
cold layout metadata work, but must not create native layout/group objects or
query limits. Compact rejection precedes resource-array mutation. Dynamic
validation returns the shared stable schema/range diagnostic before native-buffer
or limit consumption. Request1 diagnostic entry lowering is a separate approved
path and cannot open these ordinary consumers.

Quick negatives pass backend-only version2 metadata with no native buffer to
the dynamic validator and require the range diagnostic. Genuine original browser
buffers then prove ordinary native group creation rejects version2 before any
native group/layout calls or limit getter reads, and dynamic validation rejects
the same metadata. Existing version1 native groups/dynamic0 and both original
finite numeric probes retain their behavior. Null/Vulkan/Metal final writes and
all four production execution gates remain unchanged and closed for version2.

The final DescriptorSet write gate remains non-overridable and rejects version2
on all backends. Removing it requires complete production admission, explicit
four-backend status, and the full resource/declaration/limits/usage/lifetime joins.
General Vulkan offsets/ranges and Metal finite-size length ABI cannot be inferred
from WebGPU evidence. Ordinary, bundle and compact-group consumers currently use
whole-buffer semantics and must reject unsupported new ranges before mutation.

Before successful production native binding, f4 must join each authored write to
the same original DescriptorSource elements, successful write receipt, arena
epoch/revision and retained original buffer dependency, exact pipeline scope and
positional set/dynamic count/order, declared access coverage and original state
plan. No detachable entry or copied metadata substitutes for that composition.
Uniform/non-runtime declarations, arrays/tables and all legacy/general consumers
require independent complete contracts or stable capability-false rejection.
Recording/Submission remain exactly two full native execution issuers.

RenderDoc: RHI-RENDERDOC-TOOL-UNAVAILABLE. Numeric diagnostic probes are no visual,
Platform Validated or Production Ready promotion.


f3b1 verified 2026-10-06: host59/59 with Null hidden-frame,
Wasm/both-browser11/11 and inventory204/204. Existing true flags320/319 preserved;
new legacy-consumer flag required. Genuine original buffers in both binding
positions reject group/layout creation with calls0 and limits getter reads0;
dynamic validation emits the shared range diagnostic. Compact guard is verified
from its source position before resource-array mutation; no direct compact native
negative observation is claimed. Contract Complete / Prototype Complete for
rejection; Backend Validated WebGPU group/dynamic rejection only. All c4/f3a
numeric probes remain valid and independent. No production gates promoted.
Evidence: `build/web-bindless-apple-debug/artifacts/bindless-continuation/runtime-length-legacy-consumer-validation.yaml`.

Next-stage audit: `build/web-bindless-apple-debug/artifacts/bindless-continuation/original-finite-descriptor-write-next-audit.yaml`.
The current synchronous final write transaction and backend candidate commit do
not themselves prove async native group acceptance under exact original pipeline
scope. Original finite admission must retain those existing owner/receipt/revision
boundaries and establish genuine bounded scope completion before success. Source
bufferAccessOffset/Bytes currently prove whole-resource access coverage only;
f4 must separately establish shader-relative finite footprint/state composition.
Uniform/non-runtime declarations and arrays/tables are still separate obligations.
