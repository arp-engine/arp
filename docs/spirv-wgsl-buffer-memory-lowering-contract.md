# SPIR-V / WGSL buffer memory representation /1

Status: Contract Complete / Prototype Complete for this cold declaration
relation and its explicit plans. Not Backend Validated; Not Platform Validated;
Not Production Ready. This relation is declaration data, not an original
owner, target minimum issuer, shader execution equivalence or bound-length proof.
Reuse BufferDeclarationTypeLayouts and the two existing verified parsers.

The caller must select one RootForm before comparison; there is no try/unwrap
fallback. BlockStruct preserves the SPIR-V Block root and target struct.
UniformValue removes exactly one SPIR-V root Block member at offset0, only for
Uniform roots, and compares its complete child to the target value. StorageRuntimeArray
removes exactly one SPIR-V root Block member at offset0 whose type is a runtime
array, only for storage roots, and compares the whole runtime array to a target
runtime array. The existing binary verifier, not names, establishes Block roots.
No nested struct is removed, even if it has one member or an identical extent.
Slang's SPIR-V fixed-array value wrapper inside the real nested probe requires
an explicit ArrayValueWrapper rule identifying a source wrapper node and target
array node. The rule must establish one ordered offset0 member, a fixed-array
child, unknown SPIR-V padding, and wrapper occupied extent equal to the child.
The target must be a verified fixed array. This removes only that exact wrapper
at that exact pair; the array dimensions/stride/complete element graph are still
visited. Rule count is authored and bounded by the pair budget. Null rule spans,
invalid/duplicate pairs, changed wrapper shape and unused rules fail. No
name-based match, recursive scalar unwrap or automatic choice of rules exists.
Without the exact rule, a struct/array pair returns
SHADER-BUFFER-LOWERING-ARRAY-WRAPPER-PROOF. This is not an execution ABI proof.

Both descriptor roots must have cardinality1 and no descriptor-array nodes.
Arrays retain every rank, count and stride. Traversal is iterative and bounded
by an authored pair budget. Every pair compares kind, scalar identity/width,
vector components, fixed/runtime array stride/count, occupied bytes, struct
member count, ordered member offsets and child relations. WGSL alignment,
reserved sizes and SizeOf remain target facts; unknown SPIR-V padding is never
manufactured. Equal occupied bytes alone never succeeds. Only after complete
traversal may output contain target declaration SizeOf and source occupied size.

Any matrix on either side returns SHADER-BUFFER-LOWERING-MATRIX-PROOF; an equal
byte layout is insufficient to prove the row-major numerical contract. Atomic
nodes return SHADER-BUFFER-LOWERING-ATOMIC-PROOF; scalar declaration identity in
SPIR-V cannot prove atomic operations. Descriptor arrays return
SHADER-BUFFER-LOWERING-DESCRIPTOR-ARRAY. Changed structure/scalar/offset/count/
stride returns SHADER-BUFFER-LOWERING-MEMORY-MISMATCH. Unknown root forms, invalid
indices, missing target sizes, wrong source encoding, exhausted pair budget or
allocation fail deterministically and clear all output. Traversal failures use
the shared SHADER-BUFFER-TYPE-PAIR-BUDGET / -ALLOCATION / -COMPARE-FRAMING
diagnostics. Actual storage access
permissions and complete-program uses still require separate original joins.

Raw WebGpuBindingSpace/5 checks precede cache parsing in original artifact3/4
and graphics module1. Active group/binding/capacity/arrayElement are required
typed fields. Capacity equals logical cardinality and arrayElement is0; this
checks metadata only, never certifies native descriptor-array ABI. Active maps
join exact logical identities/name/type; every logical binding has exactly one
active map. Logical and physical duplicates fail. Inactive records carry only
identity/active facts and cannot carry a physical payload. Physical group extent
is exact, including separately mapped AtomicFullBlockPush uniforms; gaps do not
invent bindings. Push cannot collide with any active descriptor. Unavailable
maps are empty with group extent0 and null push mapping. Legacy entry1/2 keep
their prior metadata checks and cannot issue new facts. No publication schema or
ownership family is added.

Four backend outcomes for these declaration prerequisites: Null is validator
only; Vulkan and Metal are DiagnosticFailure for this target-specific relation;
WebGPU can consume declaration data only after the pending exact artifact,
physical target, push and every graphics stage custody joins. Request2 and
native slices stay DiagnosticFailure on all four backends. Matrices/atomics and
bound-region length need independent approved lowering proofs. No capability,
native payload, NativeSync or public recording gate is promoted.

Acceptance requires quick scalar/order/rank/stride/padding/alias/deep/budget
tests, real SPIR-V/WGSL pairs, rehashed raw metadata through both loaders,
hostile PMR/quota cleanup, Null hidden frame and both browser feature modes.
No shader sources or visible output change; RenderDoc remains unavailable on
this host (RHI-RENDERDOC-TOOL-UNAVAILABLE). Backend Validated, Platform Validated
and Production Ready remain unclaimed.

Verified 2026-10-05: host31/31 including Null hidden frame, Wasm and both actual
browser feature modes11/11, inventory204/204. Raw metadata cases cover original
entry2399 and combined module78, retaining legacy entry1/2 behavior. Wasm hostile
PMR caught an implicit conditional owned-string copy; the check now compares
views. Complete three-binding real Slang probe matches target sizes4/48/4 under
one explicitly consumed nested-array wrapper rule; absent, duplicate, unused,
invalid, oversized rules and changed binary wrapper offsets reject and clear
facts. Same-size signedness/order/rank/stride disagreements, distinct padding,
1500 nested arrays with1500 explicit rules, hostile PMR and quota cleanup pass.
Matrices, atomics and descriptor arrays remain explicit proof diagnostics.
Every previous true browser flag remains true; diagnostics/nativeErrors empty.
Both modes require bufferMemoryLoweringPassed and webGpuRawPhysicalMetadataPassed.
No shader source or visible output changed. Evidence:
build/web-bindless-apple-debug/artifacts/bindless-continuation/
buffer-memory-lowering-validation.yaml. Continue exact original physical/target
declaration custody, push accounting and both selected graphics stage joins.
