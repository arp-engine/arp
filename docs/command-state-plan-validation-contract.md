# Original state plan validation scope /1

Status: Contract Complete; implemented. Scoped Windows/Vulkan and WebGPU
verification passes; current Metal verification and production gates remain open.

The original transaction's private `populatePlan` operation consumes one immutable,
serialized original graph. It must validate the complete original graph before
reading any inventory, buffer observation, texture cell or timeline. Failure
returns the original diagnostic and publishes no plan. The operation contains
only bounded plan writes and internal synchronous visitors; it must not invoke
caller callbacks, publish state/content mutations, suspend, or retain a validation
ticket across operations.

After that guard, cell resolution may reuse the existing private state-sequence
buffer/cell resolvers for this graph during this operation. Revalidating the
entire graph for each cell is redundant and makes material/texture demand multiply
whole-graph validation cost. Canonical identity, exact mip/layer/aspect selection,
sequence ordering, initial/final state and plan capacities remain mandatory.
Public buffer/cell queries retain complete per-call original graph validation.
Only the existing scoped CPU projector and original transaction may access these
private resolvers. There is no new public unchecked reader or execution authority.

| Backend | Mapping and refusal |
| --- | --- |
| Null | Same original graph validation and bounded CPU plan; no GPU execution. |
| Vulkan | Same common plan and existing native lowering; invalid graph/sequence/capacity refuses before recording. |
| Metal | Same common plan and existing native lowering; invalid graph/sequence/capacity refuses before recording. |
| WebGPU | Same common plan and existing Emulated lowering; invalid graph/sequence/capacity refuses before recording. |

No capability, budget, owner, generation, journal, completion or retirement rule
changes. There is no persistent cache or new allocation. The change is confined
to validation placement within an already serialized private CPU operation.

Verification: source guards enforce the private boundary, complete entry guard
and guarded public queries. Existing original state/transaction negative tests
must continue rejecting stale/mutated sources and invalid capacities. Actual
Vulkan and Chrome PBR clean/fault/lifecycle workloads must pass their unchanged
registered deadlines; actual Mac verification remains required. Earlier Vulkan
238-second standalone success and Chrome 480-second timeout are retained as
failure/cost evidence, not passing registered acceptance or scene performance.

## Scoped original Root access projection /1

The private graph preflight, Stream mapper and transaction plan builders already
validate their original Inputs/graph before running synchronous internal visitors.
They may reuse the original Root's private frozen declaration readers during
that operation. The same existing walker must retain all command order, selected
stage, canonical source/view identity, fault-copy, push and capacity checks.
Only complete entry validation is shared; no independent access graph is stored.

The public access visitor and public Root readers keep their current per-call
validation. An external callback must never enter the scoped path. The private
path is restricted to the original graph, Stream and transaction classes and
must not survive suspension, external callbacks or state/content publication.
It returns the same declaration/source indices and budgets as the guarded path.
Private read helpers grant no resource, command or execution authority.

The four-backend mappings above are unchanged: this is the common CPU projection
consumed by Null, Vulkan, Metal and WebGPU, with the same refusal diagnostics,
quotas and real completion/retirement obligations. A failed original validation
still returns before projection or plan publication. Source boundary guards and
existing stale-owner/range/capacity tests precede affected GPU/platform reruns.

## Scoped sequence and ordinary range validation /1

The original state sequence may use the same private Root access walker only
after its complete graph entry guard. Its internal per-command validators have
no external callbacks and cannot publish or retain an admission token. Public
sequence inspection and cell queries retain their complete entry validation.

Ordinary Graphics range admission validates the original Root once, then walks
every active selected-stage buffer using private range/member readers. Those
readers must preserve all original binary/target/driver evidence, resource/range,
alignment, runtime stride/capacity and member checks. The shared private Root
walker may use this admission only inside its already validated synchronous
scope. Public range/member queries retain version checks, cleared refusal
outputs and full original Root validation. No buffer proof, lifecycle or budget
check is removed; repeated whole-Root validation is not a distinct requirement.

Null, Vulkan, Metal and WebGPU consume the same common admission and retain the
mapping/refusal table above. This adds no capability, executor, public unchecked
query, retained cache, allocation or native-specific shortcut. Source boundary
guards and stale-owner/range/version refusal tests must precede affected platform
execution under the existing deadlines.

## Current scoped verification (2026-10-17)

Source guards421/421, style6/6 and pinned formatter pass. Rebuilt Windows
cold/Root/range/Null regression14/14 and registered Vulkan2/2 pass. PBR now
finishes in7.99s under its unchanged60s deadline (Instance64 1.49s). Fresh
independent RenderDoc replay verifies both clean/fault draws,128 lanes, exact
ordinary bytes,10 authored mip chains, color/fault and validation0.

Current Wasm/Node4/4 passes. Actual Chrome PBR completes22/22 lifecycle flows
and4/4 numeric128 receipts in85.50s under the unchanged480s workload deadline.
Current matrix/Instance64/skinning regression adds66 lifecycle flows and12
numeric receipts; all four browser reports have empty diagnostics/nativeErrors.
Failed earlier epochs remain preserved, including third8/22 and fourth9/22
browser timeouts; web3 includes a correction to its report-freshness metadata.

Evidence: `build/bindless-production-pbr-material-1/plan-scope3-report.json`.
This closes the observed Windows/WebGPU validation-cost regression only. Actual
Mac source transfer/execution remains pending while the peer is offline. The
PBR implicit-reference/discard/filter semantics and common production engine
gate remain open; WOCC remains Traditional.
