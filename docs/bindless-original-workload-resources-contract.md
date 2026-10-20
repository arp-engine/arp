# Original workload resource inspection /1

Status: Contract Complete; scoped original-inventory Platform Validated on
Null and public Vulkan/Metal/WebGPU workloads. Actual device ceiling enforcement
and complete in-flight budgets remain open.

This is a cold, allocation-free projection of the existing canonical inventory.
It retains no second owner graph and grants no execution, state, content,
completion, resident-memory or in-flight reservation authority. ResourceDemand/1
remains the common buffer-range and texture-shape algorithm.

Input is the original quota-bound shared inventory and exact original device.
The inventory must already be sealed by its existing usage graph. Inspection
validates original ownership, canonical dependencies and registered lifetime.
Independent budgets bound source visits, all frozen descriptor elements and
retained buffer/texture visits before the
projection begins; unknown versions, missing opt-in and one-less capacities
refuse without partially publishing output. Counts describe canonical inventory
records, never native allocations or physical residency.

Every canonical buffer and texture lease contributes its complete frozen
allocation descriptor, including non-descriptor indirect/readback buffers and
snapshot metadata/fault resources. Every Root physical element and every
ordinary descriptor set/table frozen element contributes its actual immutable
buffer binding range. Inactive physical slots remain included. Repeated
references combine maxima and offset gcd; they do not become new allocations.
Table writes use their existing whole-buffer range contract. Dynamic offset
declarations are refused by /1; no guessed zero or maximum is substituted.

Descriptor sources already prove their exact retained resource owner, source
index, original registration generation and immutable observation. The projection
consumes that typed owner and declaration; raw resource IDs or equal descriptors
cannot supply ownership. Root ordinary domains are also present as canonical
descriptor sources, so no second traversal through a Root's ordinary domains is
needed. Root physical elements remain distinct from those ordinary sources.

Four-backend decisions: Null is validator-only; Vulkan, Metal and WebGPU allow
cold original inspection when original registration/lifetime checks pass.
WebGL and unknown backends deterministically refuse. A successful scalar report
does not prove format-operation availability or whole-workload completeness
outside this inventory. The engine still needs actual device ceiling checks,
the full pass/resource closure and independent in-flight owner/quota enforcement.
Metal per-binding alignment continues to come from original driver reflection;
it must not be replaced by an invented global alignment.

Required validation: actual Null and three native/public workload inventories;
exact/one-less source and descriptor ceilings; unsealed/null/foreign shared
owner; unknown version/opt-in; zero output on failure; no inspection allocation;
all underlying resource and GPU completion/retirement regressions retained.

## Validation at the recorded source epoch (2026-10-19)

Windows9 selected unique tests pass across recorded attempts; Mac13/13 and
Wasm CTest9/9 (Node5/Chrome4) pass. Source guards422/style7/layout4 and pinned
formatting5 pass. PBR and Terrain27 inventories expose32/65 source records,
6 buffer visits,22/56 texture visits and17/33 frozen descriptor elements;
visits are not unique resource allocations. Both real Chrome workloads produce
22 inspection receipts and retain genuine numeric/fault/retirement acceptance.
Native ordinary Traditional references and clean/fault readbacks remain green.

The first build's layout/descriptor type mismatch is preserved. The correction
uses the existing frozen layout.write() helpers, including original readback
texture layouts. A Windows Terrain27 60s timeout during concurrent Wasm work is
also retained; an isolated unchanged-binary run passes under the same60s limit.
Its cause is not proven. Apple9267 source inputs and178 observed artifacts match
their recorded epoch; all5 affected native inputs match the staged source.
This is no whole-tree/dependency-closure seal or new GPU capture.

Evidence: `D:/arp/build/bindless-original-workload-resources1/current-report.json`.
Next: actual queried platform ceiling publication and enforcement by the
original execution preflight, complete workload correspondence and independent
in-flight owner/quota budgets. This report grants none of those authorities.
