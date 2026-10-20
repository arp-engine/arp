# Original graphics artifact memory input /1

Status: Contract Complete / Prototype Complete for graphics memory input /1.
Verified 2026-10-05: host19/19 (including real Metal original provider/push and
Null hidden-frame), Wasm/browser8/8 and inventory189/189. Original accepted
compute factory /5 is Contract Complete / Prototype Complete / Backend Validated
for its selected WebGPU wrapper. Selected WebGPU byte consumption /1 is now
validated separately in
original-graphics-artifact-consumption-contract.md. Graphics native
creation acceptance and original graphics L2 join remain DiagnosticFailure.
Canonical Pipeline Source, positional descriptors, shader footprint/state and
typed execution remain closed.

The validated GraphicsShaderModuleArtifact is the sole common provenance owner.
It owns combined module targets and two distinct genuine selected-entry children
on the exact common artifact quota. Combined targets and selected-entry targets
have different authored identities; a path/digest match cannot substitute one
for another. Preserve that distinction in typed target views, byte accounting
and consumption receipts. An immutable graphics input retains this original
module/control block, builds exactly two selected L2 entries, and owns its
canonical merged package under an independent authored metadata quota. No IO,
YAML reparse, target byte copy, hydration cache, native handle or speculative
resource compatibility enters this boundary.

Reuse populateSingleEntryShaderInterface for each original child and extract the
existing selected vertex/fragment interface merge from Metal's original graphics
provider into rhi/shader_package_reflection. Keep Metal using that shared helper;
do not duplicate a renderer/backend-local merge. Match source identity, exact
Vertex/Fragment stages and selected entries before merging. Logical bindings are
keyed by explicit set/binding; require identical type, array count, depth flag
and subpass attachment identity before unioning declared visibility. Missing,
duplicate, unsupported or conflicting semantics fail with stable diagnostics.

Shared push constants must match byte size, version and every member's name,
scalar type/width, vector/matrix dimensions/stride/layout, array dimensions/stride,
offset/size/decorations and all backend mapping fields. Parse and union explicit
stage visibility deterministically using the existing normative parser. Never
choose Metal's buffer index as the only ABI proof or silently overwrite another
stage's layout. All preserved fields come from approved authored reflection.
Refactor existing member validation into the appropriate shared RHI/shader utility
where necessary, preserving caller diagnostics at backend boundaries.

Define target views against the publication's explicit roles/statuses before
native consumption. Vulkan requires the exact original selected SPIR-V child
objects per selected stage. Metal and WebGPU can consume the combined original
MSL/WGSL module only under an explicit versioned combined-module profile that
validates both entries and merged bindings. Absent or unavailable authored targets
reject their stable diagnostics. Null is ValidatorOnly; none of these views
creates native acceptance or execution permission.

Cold preparation validates original module/child owners, immutable ceilings,
independent quotas, versions, exact selected semantics and bounded metadata.
Failure releases all new charges and preserves caller owners. Published input
survives caller module release and authored-file removal. Hot original validation
is allocation0 and checks unchanged identities/budgets. Reuse ShaderPackageStateCopy,
ShaderReflectionStateCopy and core PMR/factories for all nested owners.

The subsequent graphics consumer freezes the complete RenderPipelineDesc before
native calls, validates exact reflected vertex input via VertexInputConsumptionContract,
and reuses approved backend native emitters. Real asynchronous scopes must prove
creation separately; a digest, returned handle or compute receipt cannot issue
graphics acceptance. Original graphics L2 registration then needs its own exact
accepted receipt/parent/full-definition join; no ordinary relabeling or new
canonical root/graph/journal/State/proof issuer is allowed.

Retain quick contract/unit and Null hidden-frame paths, plus both WebGPU browser
modes with prior mandatory flags. Tests must cover actual generated two-entry
module, all backend target outcomes, genuine pointer/control-block custody,
semantic conflicts and complete push/member equality, exact/one-less/exhausted
metadata and retry, caller release, complete authored directory absent, hostile
ambient PMR, hot allocation0 and final quotas0. No shader source or visual output
change in this input stage. RenderDoc remains RHI-RENDERDOC-TOOL-UNAVAILABLE on
this host; no Backend/Platform/Production promotion from input assembly.

Validation evidence: graphics-input-validation.yaml in the bindless-continuation
artifact directory, full host/web logs and saved browser reports. Native/Wasm
cold input peaks2400/2072, merge2428/1624, memory lowering252/188 bytes. Exact,
one-less, exhausted, retry, hostile PMR, hot allocation0 and final quotas0 pass.
Both browser modes remove the complete authored folder after dropping the caller
module, then lower retained original memory. Mandatory flags pass; diagnostics
and nativeErrors empty. Initial file equivalence failure arose from incomplete
test path bindings and was corrected using the existing explicit fixture paths;
no loader fallback was added. Input/target facts alone do not establish graphics
Backend Validated or Platform Validated. Existing Metal provider/push regression
validation remains scoped to its real payloads.
