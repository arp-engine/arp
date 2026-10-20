# Original single-entry pipeline artifact input /1

Status: Contract Complete / Prototype Complete for the cold memory boundary below;
verified 2026-10-05 host17/17, Wasm/browser8/8 and inventory185/185. Consumption receipts and canonical Pipeline admission
remain DiagnosticFailure.

The existing validated SingleEntryShaderArtifact is the sole byte and selected
reflection owner. A quota-bound OriginalSingleEntryPipelineInput retains that
exact original object/control block and builds its immutable L2 package with
populateSingleEntryShaderInterface. It has an explicit version and independent
nonzero CPU metadata quota ceiling. Reject missing/forged owners, shared input
and artifact quota domains, changed ceilings, unknown versions and allocation
pressure. Prepare before a backend call; failure releases all input quota charges
without altering caller ownership. There is no second byte copy, path hydration,
cache substitution, native handle or retrospective artifact attachment.

All source/target/reflection/publication bytes and their authored filenames
remain owned by the artifact; filenames are labels, never memory-input IO
instructions. The generated package has the original selected entry/stage,
workgroup and exact reflection, and is const after publication. Memory-only
inspection remains valid if authored files are removed or changed. Validation
is allocation-free and verifies exact owners and the unchanged authored budgets.
Target selection is backend-specific and checked before consumption: Null is
ValidatorOnly; Vulkan selects Spirv, Metal selects Metal, WebGPU selects Wgsl.
Missing targets return their authored stable diagnostic; unknown backends fail
closed. Target bytes do not imply native capability or authorize execution.

| Backend | Cold input | Consumption receipt / typed execution |
| --- | --- | --- |
| Null | Equivalent immutable input; ValidatorOnly | DiagnosticFailure |
| Vulkan | Equivalent immutable input; SPIR-V target | DiagnosticFailure |
| Metal | Equivalent immutable input; MSL target | DiagnosticFailure |
| WebGPU | Equivalent immutable input; WGSL target | DiagnosticFailure |

WebGPU factors its existing reflection-to-binding-space lowering validation into
one reusable backend-private body. The existing ordinary file loader validates
its loaded identity and calls that body. The new original input path validates
the genuine input, selects its retained target and calls the same body with the
artifact-owned reflection and package; it performs no filesystem IO or YAML
parse. Every lowering container and nested string uses core PMR and the caller's
authored cold quota. Successful lowering establishes only deterministic layout
preparation, never native module/pipeline consumption or shader access proof.

Keep fast contract and Null hidden-frame paths using real generated graphics
children, and both browser modes using a real compute artifact. Cover input
version/quota/owner/target failures, exact/one-less/exhausted cold quota and retry,
same-pointer forged aliases, caller release, target byte pointer equality,
changed ceilings, hostile ambient PMR, hot allocation0 and final quotas0.
Validate the shared lowering's package disagreement negative case and equivalent
memory/file results. The later factory must consume this exact memory input and
issue private facts from bytes actually passed to native APIs; it may not claim
consumption by correlating a path-loaded digest. Preserve canonical root, seal,
journal, State and exactly two command-proof issuers. No shader or visual changes,
no Backend/Platform/Production readiness promotion.
