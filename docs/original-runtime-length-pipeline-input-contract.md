# Original available-target runtime length pipeline input

Status: Contract Complete / Prototype Complete for explicitly opted-in original
available-target input. Backend Validated for actual WebGPU creation and accepted
wrapper reuse only. Host57/57 with Null hidden-frame, Wasm/both-browser11/11 and
inventory204/204 passed; evidence runtime-length-pipeline-input-validation.yaml.
Not Platform Validated or Production Ready.
request2, native finite descriptor intervals, Source/Inputs/state, NativeSync and
command execution remain closed. No new owner/root/seal/journal/source family or
native proof issuer is permitted; Recording and Submission remain the two full
native execution issuers.

Implementation contract: both existing input Budgets append scalar
`availableTargetContractVersion` and `prototypeOptIn`. Budget/1 requires these
fields to remain zero/false. Budget/2 requires version1 and explicit opt-in; only
the diagnosed /5 entry or /2 module profile with complete frozen memory and
nonempty operations is accepted. No generic ShaderPackage field changes.
Shared interface validation separates source/auxiliary artifact paths and
logical entry/reflection checks from the complete-target MSL check, without a
skip boolean. Ordinary package validation retains the same complete checks.
Runtime-length interface population has original-shared-owner overloads and a
private policy; raw packages cannot select it. Hot `packageDiagnostic` validates
the exact original input and full PipelineDefinitionIdentity of a candidate.
An exact value copy paired with its genuine input may be compared as metadata;
it independently grants no owner/native authority and generic validation still
rejects it. Public original consumption retains its exact package-pointer gate;
accepted wrapper factories retain their complete creation/definition equality.
Selected-entry validation reuses one common implementation behind separate
ordinary and original-owner entry points. Only original WebGPU validators and
emitters consume these owner-aware entry points.

The pre-c3 audited blockers were explicit. OriginalSingleEntryPipelineInput/1 and
OriginalGraphicsPipelineInput/1 prepare complete cross-target packages. The
package validator rejects an empty MSL path. The unchanged Budget/1 path returns
SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED before publishing either input owner.
Creating a dummy path/file, editing generated shader bodies, populating MSL from
another artifact, or bypassing the ordinary validator is forbidden.

## L2 and original ownership

Define OriginalAvailableTargetRuntimeLengthInput/1 as an independently opted-in
cold policy in the existing input owners. Existing Budget/1 and default package
conversion/validation retain their rejection. Budget/2 explicitly
select this exact policy, author the unchanged CPU quota and retain the selected
contract as scalar owned state; no borrowed request pointers survive preparation.
The exact accepted artifact profile is SingleEntryShaderArtifact/5 or
GraphicsShaderModuleArtifact/2 with both /5 selected children. Preparation requires
successful original-owner/quota validation, nonempty frozen runtime operation
facts, complete target memory, exact unavailable-Metal diagnostic and available
SPIR-V/WGSL. Label/role/package copies or a version alone cannot select the policy.

The resulting package keeps the MSL path empty. Ordinary validateShaderPackage,
ordinary interface population and GraphicsShaderInterfaceMerger/1 still reject
that package. Add an independently versioned original role-aware conversion and
validation path to the existing interface utilities, sharing their logical
binding/entry/push/type validation rather than cloning algorithms. It consumes
exact original artifact/input owners or an already validated accepted creation
whose full PipelineDefinitionIdentity matches. A raw caller package or capability
copy cannot opt ordinary/native factories into this policy. No generic package
flag or filename manufactures original provenance.

Both selected graphics children are independently validated and deterministically
merged under the new policy; combined original reflection, memory and operation
scope must still match. Preserve all existing selected stages, stage masks,
source hashes, push/member layout, exact active physical bindings and quotas.
Missing/legacy/profile-mixed children close the complete input. Do not infer
whole-program equivalence or stage invocation counts from operation inventories.

## Versioned lowering and execution boundaries

| Backend | Input target selection | Original consumption/creation conclusion |
| --- | --- | --- |
| Null | ValidatorOnly, no target payload | Existing Null validator contract only; no native length/execution proof |
| Vulkan | Exact selected SPIR-V bytes | Existing original payload contract is DiagnosticFailure; do not infer success from byte availability |
| Metal | DiagnosticFailure SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED | No target/placeholder pipeline; native length ABI remains unavailable |
| WebGPU | Exact available original WGSL bytes | Existing device query, physical lowering, native shader/pipeline error scopes and original creation/accepted-wrapper gates must all consume the new role-aware policy before any Native creation result |

Native byte consumption and successful asynchronous creation are separate from
original command execution and native descriptor interval semantics. A real
WebGPU compute/graphics pipeline can validate this phase's creation payload;
it cannot open subrange writes, request2 or canonical commands. The existing
creation actors, issued consumption custody and accepted wrappers are reused;
none becomes a third native execution issuer. Hot validation reads frozen policy
and original bytes/facts only, without IO, allocation, compilation or waits.

## Required downstream audit and tests

The following existing validators currently assume complete packages and require
explicit audited extension before backend changes:

- shader_package_reflection.cpp: selected entry population, graphics merge and
  combined interface validation.
- PipelineShaderDeclarationContract: package, layout, selected entry/stage and
  compute workgroup contract; share its checks without an unchecked boolean skip.
- webgpu_backend_objects.cpp: original compute/render validation and both accepted
  wrapper factories, including full definition equality and independent quotas.
- webgpu_native_resources.cpp: original factories' exact byte selection, selected
  declaration validation and backend-private pipeline emission.
- gpu_device.h: shared original registration selected-declaration admission uses
  original input only when paired with an accepted original creation; ordinary
  factories retain complete-target validation.
- Existing original creation actors and issued pipeline contracts: exact input,
  device/layout lineage, target hash/size and genuine asynchronous scope results.

Tests must retain Budget/1/generic package rejection; reject absent opt-in, wrong
versions/profiles, copied packages, forged owners, mutated quotas and missing/
wrong children. Verify all four target selections, exact/one-less/exhausted
allocation cleanup, deletion/caller drop and hot0. Require a quick contract,
Null hidden-frame and both browser modes with every old success flag preserved.
WebGPU creation needs real payload and native error-scope evidence; host mocks
cannot validate it. RenderDoc remains RHI-RENDERDOC-TOOL-UNAVAILABLE on this host,
so no Platform Validated or Production Ready promotion is permitted.

After this phase, separately join accepted original pipeline/layout/device,
frozen queried alignment/maxima, exact original buffer extents, the authored
finite descriptor offset/unchanged size and dynamic displacement to actual native
binding payloads. Reuse DescriptorBufferBindingRangeContract.resolve and
WgslBufferDeclarationLayout.runtimeArrayLength; access coverage, stride or copied
facts cannot supply a descriptor slice. Only that completed contract can extend
OriginalDescriptorBufferBinding request2 and precede f3/f4 execution.

Selected c3 verified 2026-10-06: all four compute fixtures and both-stage graphics
create real WebGPU shader/pipeline payloads with completed clean error scopes.
Issued original input, target hashes/sizes and device/layout custody match.
Accepted wrapper publication, reuse and quota pressure create no further native
shader/pipeline payload. Caller owners dropped and authored folders absent during
creation; all artifact/input/context quotas return to zero. Eight input contract
tests preserve default/generic missing-MSL rejection, wrong policy/profile/owner,
full-definition mutation and deleted-file hot0. Prior browser true flags314/313
remain true and new originalRuntimeLengthPipelineInputPassed is required.
Initial include/digest, registration-admission, inventory-prefix and incomplete
input-type failures are retained in evidence. Continue original native interval
lineage; original request2 and all descriptor/typed execution gates stay closed.
