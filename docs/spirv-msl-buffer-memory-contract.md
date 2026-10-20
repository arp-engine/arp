# SPIR-V / MSL buffer member memory /1 and /2

Status: Contract Complete; Platform Validated for the independent Metal member
collector and the declaration cases below. No new execution authority.

`SpirvMslBufferMemoryLowering` compares two bounded typed subtrees using the
existing `BufferDeclarationTypeLayouts::visitPairs`. It does not parse source
names, equate total buffer sizes, infer matrix transposes, or issue original
artifact receipts. Source nodes must be binary declarations. Target nodes must
come from an independent collector of the selected compiled Metal function.

The relation compares scalar kind/width, vector shape, matrix dimensions and
column stride, fixed-array count/stride, every nested member offset and occupied
extent. SPIR-V ColMajor to MSL column-vector memory requires an explicit pair
for every matrix type. Array value wrappers on either side require explicit
single-member, zero-offset, equal-extent fixed-array rules. Duplicate, missing,
unused, malformed or over-budget rules refuse and clear all output facts.
Runtime arrays, atomic and descriptor arrays remain explicitly unsupported by /1.
Numerical matrix operations still require separate genuine GPU evidence.

The native collector must retain actual member offsets and array stride/length.
Metal scalar/vector/matrix sizes follow the specified MSL type ABI, including
three-lane vector storage padding; no size may be inferred from the next member
or from a buffer's total size. A queried root minimum/alignment remains a
separate range constraint. Collection and comparison are cold, bounded and use
the core allocator; failure publishes no partial graph. Original selected-entry,
physical slot/stage, module bytes, access and resource ownership must be joined
before these declaration facts can support a public execution capability.

| Backend | Mapping and boundary |
| --- | --- |
| Null | Validates declaration relations; supplies no native target evidence or GPU execution. |
| Vulkan | Existing authenticated SPIR-V layout path remains Native. An MSL relation is not Vulkan evidence. |
| Metal | Independently queried selected-function member graph is the target input. The original selected pipeline/artifact join is validated for authored Graphics; public Native Root proof remains unavailable pending its canonical resource/range join. |
| WebGPU | Existing explicit SPIR-V/WGSL relation remains required. MSL nodes and receipts grant no Emulated execution. |

No capability bit, package version, range-only driver result or synthetic graph
can grant this proof. Existing Native target-memory refusals remain mandatory.

Verified checkpoint: `build/bindless-native-member-memory1/current-report.json`.
The actual Mac driver graphs match the selected WOCC SPIR-V declarations for
shadow indexed256 skinning16432, PBR temporal skinning32864 and previous camera128
bytes. Three missing-matrix plans, a same-size signedness mutation and a changed
array count refuse. Exact node/member ceilings pass; smaller/zero ceilings and
core quota exhaustion clear partial output. The existing authored320-byte matrix
declaration is also a registered native smoke. Windows3/3, Mac2/2, Wasm/Node,
source418/418, style6/6 and formatter pass against the recorded8615-input epoch.

The newer `build/bindless-native-pipeline-memory1/current-report.json` closes
original source/rule custody and selected Metal pipeline/physical stage-slot
comparison for the authored320+16-byte workload. Windows5/5, actual Mac6/6,
Wasm/Node1/1, source418/418 and style6/6 pass;8619 source inputs and native
artifacts independently match. Fresh GPU readback/capture preserves32 exact
lanes. Remaining: the typed public Root query must join the existing canonical
buffer/range/generation. A caller-supplied tree or the
standalone inspection executable cannot issue a public original-memory receipt.
Indexed skinning arithmetic and production scene execution remain separate gates.

## Explicit value and runtime element relation /2

Plan2 adds explicit root forms to the same bounded member comparison. `Block`
preserves /1. `Value` consumes a declared single-member, zero-offset source
wrapper and compares that member to the actual selected pointee. `RuntimeElement`
requires a source storage block with exactly one zero-offset runtime array,
zero static extent, nonzero stride and a complete value element. The target is
an independently collected selected-function pointee value graph. Its actual
queried dataSize is the MSL pointer element stride (including storage padding),
and its queried alignment must be nonzero, a power of two, and divide that stride.
The source and target strides must agree, and every element member/type/offset
must pass the original relation. No fixed count1 array or source-shaped target
may stand in for a driver element. Nonzero-prefix/multiple/nested runtime tails,
atomic/descriptor arrays and unknown forms refuse. Query1 remains static only.

Native request2 freezes the source graph and root form in the original package;
request1 cannot carry these forms. The original Metal pipeline consumes actual
bufferPointerType facts at the mapped stage/slot during cold creation. Hot Root
queries use these retained facts and the existing Range2 canonical resource,
generation and capacity. No new owners or execution authority are introduced.
Null has validator evidence only; Vulkan keeps exact SPIR-V identity; WebGPU
keeps its independent SPIR-V/WGSL proof. Their proofs cannot substitute for MSL.
Missing opt-in, pointee type/stride, member proof, source/target budgets or any
failed relation must clear output and refuse before record/publication.


Validated /2 checkpoint: `build/bindless-metal-runtime-member2-1/current-report.json`.
The existing shared traversal now proves explicit Value and RuntimeElement source
forms against independent selected Metal pointer values. Fast tests cover scalar4
and four-float4 record64, signedness/member offsets despite equal extent, missing
or wrong stride/alignment, old/unknown forms, prefix/nested runtime refusal,
source custody and exhausted core quotas. The record64 test is declaration-only;
production instance/material driver layouts and arithmetic remain separate work.
Actual public GraphicsDomains runs20 lifecycle workflows,16 genuine submissions,
exact output875/876/875/876, clean/fault, cancel/drop/device retirement, hot0 and
retirement0. Selected driver graph budget and wrong explicit form refuse without
publishing a pipeline; insufficient source pair budget refuses during load.
Actual Mac7/7 includes static matrix and temporal skinning256 regression. A new
submitted-only Metal capture contains10 files; no independent replay is claimed.
Native request2 custody also passes Windows and Wasm/Node; query1 remains static.
