# OriginalDepthTableScalarExpansion/1

## Publication version boundary (2026-10-16)

Version1 is a frozen offline compiler-publication schema, not an exhaustive
enumeration of current RHI backends. Its exact backendDecisions keys remain
Null/Vulkan/Metal/WebGPU, each DiagnosticFailure with executionAvailable=false;
its entry target roles remain SPIR-V/MSL/WGSL/reflection. Adding WebGL or ES100
to this document without a new schema is invalid, including an added WebGL
entry that itself claims failure. Both the regenerating Python reader and the
runtime immutable reader must reject such substitutions, missing/replaced
backend keys, and any version change. Existing approved v1 publications remain
readable; there is no implicit migration, execution promotion, or new fallback.

The five-backend L2 evaluation includes WebGL explicitly: original typed-input
tables are unavailable, with
RHI-WEBGL1-ORIGINAL-TYPED-INPUT-TABLE-UNAVAILABLE. Null remains validator-only;
Vulkan/Metal/WebGPU retain their existing ordinary-table rejection and any
independently versioned original-program contracts. Unknown backend identities
retain the generic TABLE-PROFILE-UNAVAILABLE refusal. The v1 publication grants
none of these execution authorities, even after a successful immutable load.

The ordinary WebGL1 production ShaderPackage/ES100 ABI belongs to P1 of the
WebGL1 plan. It must use its own versioned package and target identity; it
cannot consume, relabel, extend in place, or infer compatibility from this
specialized depth-table publication. Future support for this particular
original-program transformation would require a separately approved version,
explicit target provenance, and genuine native validation. This audit closes
only the historical schema scope, not the complete P0-04/07 or P1 work.

Status (2026-10-06): Contract Complete / Prototype Complete for the selected
offline complete-program transformation and publication. Backend Validated for
WebGPU shader compilation and numeric selection only; Vulkan/Metal targets are
compiled and binary/reflection checked, with no native execution claim.
Not Platform Validated / Not Production Ready. Evidence is recorded in
`original-depth-table-scalar-expansion-validation.yaml`.
This is a complete-program offline lowering
prerequisite for f4c3b; it grants no accepted original Pipeline/Table actor,
positional authority, native receipt or base capability.

L2 intent is the original ordered table `Inputs`, set0/binding0, capacity3,
SampledTexture, depth=true, 2D, with Compute or Fragment visibility. The only
approved programs are the exact UTF-8 LF sources descriptor_depth_compute.slang
(SHA256 a5c13f383abb647fc2b64202a7f9ad96a0beed5961089a5445fc97d6495ceacd)
and descriptor_depth.slang
(SHA256 e7efff11f4e28fc1e6294a5ed3c1673837e9ee9756dae6628ff3d3baab0fa4e8).
These are versioned program identities, not caller-supplied semantic assertions.
Any source, schema, identity, capacity, operation, stage or budget ambiguity
fails with an ORIGINAL-TABLE diagnostic before publication. A changed approved
program requires a separately reviewed contract version.

The transformation replaces exactly the resource declaration and its sole
depth Load expression in Slang, and lowers the explicit ARP_SAMPLED_DEPTH macro
to the compiler's native DepthTexture2D type (float texel, shadow/depth flag1).
The pinned Slang2026.17 hlsl.meta.slang defines that alias explicitly; no target
type rewrite is permitted. Three scalar depth declarations occupy
set0/bindings0,1,2, in original element order. A bounded switch executes exactly
the selected Load, with the original int3(0,0,0) coordinate. Output at
set1/binding0, compute workgroup5x1x1, dispatch addressing thread.y*30+thread.x,
vertex position and fragment color remain verbatim. The original modulo3
expression remains at the call. For every uint input its remainder belongs to
{0,1,2}; therefore the compiler-required terminal return is unreachable in
these complete programs. It provides no general out-of-range Table semantics.
There is no sample operation, derivative, implicit LOD, metadata/fault buffer,
snapshot helper or BindlessCross identity. No other program may use this proof.

Compiler targets SPIR-V, MSL and WGSL consume the same generated Slang with
explicit -matrix-layout-row-major. Targets are never rewritten. Existing
reflection helpers prove each entry's active physical resources and locations.
The separate YAML publication binds original source, generated source, entry,
stage, ordered logical-to-physical element map, reflection and every target by
SHA256. The reader also derives SPIR-V scalar depth image declarations from
the existing binary parser and re-derives WGSL/MSL physical maps using existing
compiler helpers; rehashed depth/cardinality/location substitutions fail.
This is compilation provenance and interface validation, not authentication of
arbitrary rewritten target programs: future runtime input must retain the
genuine approved compilation identity, never accept author assertions alone.
It is not a SingleEntryShaderArtifact, GraphicsShaderModuleArtifact or
BindlessShaderArtifact and cannot be substituted into ordinary original
factories. There is no runtime reader/availability change in this phase.

The author explicitly budgets physical bindings (compute4, graphics3), switch
cases3, and cumulative shader payload bytes (original and expanded source plus
all target/reflection roles, excluding the final manifest). The generator checks these
before resource generation or final publication. Failed replacement removes
the old final manifest. No inferred quota or silent budget enlargement is
permitted. Runtime integration must separately author original owners,
element capacity, layout/group/native-call budgets and CPU retention quotas.

| Backend | Compiler representation | Current original execution decision |
| --- | --- | --- |
| Null | Exact original logical metadata; validator only | DiagnosticFailure, existing TABLE-PROFILE-UNAVAILABLE |
| Vulkan | Three scalar sampled-depth descriptors and bounded selection, Equivalent candidate | DiagnosticFailure until original actor/limits/lifetime are proved |
| Metal | Three scalar depth textures and bounded selection, Equivalent candidate | DiagnosticFailure until original actor/limits/lifetime are proved |
| WebGPU | Three scalar texture_depth_2d bindings and bounded selection, Equivalent candidate | DiagnosticFailure until original actor/limits/lifetime are proved |

Equivalent here describes the proved program transformation, not a successful
RHI execution gate. Native limits must be queried and checked before future
actor creation: three sampled textures in the consuming stage, two bind groups
for compute/one for graphics, physical binding extents3, and the existing output
storage binding's size/alignment/range contract. No optional feature is enabled.
Existing DescriptorArrayAbi/1 and TableLowering1 remain closed. The scalar
bridge carries one genuine resource per distinct binding; its existing header
and JavaScript provenance must be bound to future original native creation.

Tests must reject source edits (including load/index/depth/workgroup/Output),
unknown/extra schema fields, mismatched versions/hashes/profile/capacity, missing
and insufficient independent budgets, reordered/duplicate maps, modified target
or reflection bytes and stale final manifests after failure. They must verify
all three compiled entry stages and all target/reflection bytes. Genuine
WebGPU probes query limits and shader diagnostics, fill three independent
depth32float textures through depth clears, execute compute and fragment,
read back exact distinct values, then permute actual resource ordering and
replace one resource to observe the corresponding numeric changes. These
probes are backend shader evidence only, never accepted original actor proof.

Preserve the quick unit/contract and Null hidden-frame paths. RenderDoc capture
or RHI-RENDERDOC-TOOL-UNAVAILABLE must accompany visual status; numeric GPU
readback alone does not grant Platform Validated or Production Ready. After
this prerequisite, continue f4c3c with a separately versioned original memory
artifact/input and genuine table actor, before f4c3d positional/state/issuer work.
