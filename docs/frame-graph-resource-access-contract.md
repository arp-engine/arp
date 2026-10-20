# FrameGraphResourceAccess/1

Status: Contract Complete for the scope below. Null validation and the Vulkan
graph barrier/clear fixture are Backend Validated. Metal host stub and WebGPU
Emscripten compile; their new texture dependency execution is not platform
validated. No bindless capability is enabled by this contract.

Pass declarations identify concrete textures/buffers, state, pipeline stages,
access masks and a bounded mip/layer or byte range. Indexed access declares the
conservative union of all potentially addressed resources. Declaring a table
does not declare its resources. Buffer indirect reads use DrawIndirect and
IndirectCommandRead. Shader accesses explicitly include VertexShader when used.

Exact ranges survive compilation. This version synchronizes and schedules whole
resources conservatively: disjoint ranges may be over-synchronized but must not
lose any read/write dependency. Multiple uses in one pass must have the same
state and combine stage/access masks. Read-write uses participate as both readers
and writers. Invalid ranges, access/state/stage/usage combinations and conflicting
same-pass states fail before command recording with stable diagnostics.

Access scopes accumulate conservatively across the compiled resource lifetime. Same-state RAW,
WAR and WAW dependencies are ResourceMemoryBarrier operations, never elided as
redundant state transitions. Different-state dependencies use ResourceBarrierDesc
with complete source/destination scopes. Both graph execution paths must consume
the same compiled scopes. State-only legacy declarations use a documented
conservative scope including all three shader stages, with ShaderWrite declaring
read/write (including attachment load/read access). They cannot stand in for
explicit indirect intent. The first access retains a boundary dependency even
when the initial state matches, so repeated execution can consume the previous
frame's final state and scope. Native subpass groups retain their separate
validated internal attachment transitions; their outer scope unions stages and
uses the declared attachment boundary state.

| Backend | Lowering |
| --- | --- |
| Null | L2 scope/range and command validation; no GPU proof |
| Vulkan | Whole-buffer/image transitions and access-only native barriers |
| Metal | Existing tracked resource encoder boundaries; reject unsupported contracts |
| WebGPU | Existing tracked pass ordering/usage scopes; reject unsupported contracts |

Subresource state splitting, aliasing between ranges and multiple queues are not
introduced here. Explicit declarations cannot authorize bindless execution or
replace immutable snapshot/package matching and GPU completion ownership.

## Evidence

`arp_render_graph_rhi_execution_tests` covers explicit vertex/indirect reads,
same-state storage/attachment hazards, range preservation, conservative unions,
read-first graph replay, invalid ranges/stages/access, and same-pass conflicts.
`arp_rhi_command_contract_tests` covers the texture memory-barrier overload,
foreign-resource and active/finished-encoder rejection. Existing graph, alias,
borrowed-resource, pool, topology, Null and Vulkan hidden tests remain passing.

RenderDoc replay at `build/win-vulkan-debug/artifacts/bindless-graph-sync/` checks
four explicit vertex image barriers, four same-state attachment dependencies,
both presented images against the clear reference (UNORM tolerance 1/255), and
an empty validation-message list. Reproduce with the resource-management hidden
smoke under RenderDoc, followed by `tests/tools/analyze_graph_sync_renderdoc.py`
with `ARP_GRAPH_SYNC_CAPTURE_OUTPUT` pointing at that directory. These are native
barrier/clear results, not bindless shader or WOCC workload validation.

## Persistent buffer import

GraphBufferImport/1 adds shared ownership of an existing L2 buffer. Repeated
imports of the same object share one graph identity and hazard chain; equal
descriptors on different objects do not alias. Null references reject. Imports
are not transient, are never returned to the graph resource pool, and remain
registered across reset just like imported textures. Graph destruction releases
its CPU reference; backend command payload retention remains responsible for
actual GPU completion. This API does not turn CPU graph lifetime into GPU proof.

Reads may consume externally initialized contents, and read-before-write keeps
WAR ordering. Writes to imported buffers are externally observable and cannot
be culled merely because no later graph pass reads them. All declared ranges,
usage checks, replay and source/destination scopes use the same existing L2
buffer contract. Foreign resources must fail the backend command ownership gate.
The Null/Vulkan/Metal/WebGPU mappings are those in the table above; no native
handle, new memory intent or buffer aliasing is introduced.

Import evidence: `arp_render_graph_import_texture_ref_tests` now covers buffer
identity, owner release, retained registration after reset, graph destruction,
external read-before-write ordering, culling, indirect range declarations and
replay. Undefined imported contents without a graph producer reject. Eight graph
regressions pass, including the Null and Vulkan resource-management smokes.
Emscripten graph builds. New capture at
D:/arp/build/win-vulkan-debug/artifacts/bindless-graph-import/graph_capture.rdc
also verifies four native fragment-write to indirect-read buffer dependencies.
It preserves the vertex image/attachment dependencies and expected clear pixels,
with zero validation messages. This proves buffer import/synchronization only;
the smoke does not execute indexed materials or indirect draws from that buffer.

## GraphSnapshotBinding/1

Status: Null contract and command validation complete. GPU execution remains
DiagnosticFailure on Vulkan, Metal and WebGPU until backend issuance is proven.

A graph callback binds a snapshot against the current compiled pass resource
uses. It cannot substitute an independently authored access array through this
adapter. An L2 declaration view resolves each concrete ID/range/stage/access on
demand; validation consumes it synchronously and never retains the view or
allocates a per-record list. Raw L2 callers retain the array adapter and remain
responsible for their own complete access contract. Both feed one validator.

The graph explicitly configures CommandEncoderDesc retention budgets before
compilation; the compiled graph freezes those values. Zero remains disabled.
Metadata import uses aliasing shared ownership of the sealed snapshot, retaining
its accounting and immutable publication; it is read-only and never pooled.
The import is separate from pass declaration, so omission still rejects binding.
A standalone RenderContext, missing pass declaration, insufficient range/stage,
write omission or stale generation rejects. Native multi-subpass groups reject
indexed binding until per-subpass access proof is supplied; a group union cannot
authorize an individual subpass. Separate-pass lowering follows ordinary passes.
The sealed pipeline's active shader stages filter required descriptor accesses:
compute-only storage writes are not attributed to a graphics pass. A declared
resource still needs the exact stages used by that pipeline. This stage mask is
derived from authorized entry points, not supplied by the graph callback.
The graph compares the pass extension's opaque recording identity to the
current command encoder and rejects a pass from another encoder.
Null consumes the declaration view through its real command validator. Vulkan,
Metal and WebGPU retain closed public indexed entry points pending their full
execution gates; the graph adapter cannot enable them. Direct RHI frame-plan
execution has no indexed payload contract yet and remains unable to issue
indexed commands. This does not replace barriers or grant GPU readiness.

Compiler/runtime package tests exercise both compute and graphics passes with
complete declarations, missing metadata/texture, short buffer range, missing
stage/write access, stale generation, zero indexed capacity, standalone context,
and a foreign pass. Compiled encoder retention budgets remain fixed when the
graph's authoring value changes after compile. Thirty-two repeated binds in
each successful callback create no new core allocations. Metadata imports keep
the snapshot's leases alive after the original owner is released; application
writes and reads under a writable state reject. This is Null command evidence,
not a GPU bindless execution claim.
