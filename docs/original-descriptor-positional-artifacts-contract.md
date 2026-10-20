# Original descriptor positional shader artifacts /1

Status: Contract Complete / Prototype Complete for immutable publication and
original memory-input prerequisites. Backend Validated only for existing WebGPU
regression paths; no native table validation, Platform Validated or Production
Ready claim. Original Inputs /3, typed State and native commands stay
closed. No new Slang source or authored matrix data is introduced.

Use the existing descriptor_depth_compute.slang and descriptor_depth.slang as
immutable artifact sources. Compile through the existing deterministic shader
publication pipeline with explicit matrix-layout-row-major for every target.
Publish SPIR-V, MSL, WGSL and reflection plus Source; publish a computeMain
SingleEntryShaderArtifact/3 for compute and GraphicsShaderModuleArtifact/1 with
sealed vertexMain/fragmentMain children for graphics. All metadata remains YAML.
Publication3 adds original binary-verified declaration facts under
original-buffer-declaration-custody-contract.md. Legacy publication1 retains
whole-buffer behavior without declaration-layout query authority. Neither version
proves a native table ABI or target minimum binding size.
CMake must declare generated artifacts and preload the existing shader fixture
publication paths for the actual WebGPU browser fixture. No fallback placeholder
or relabelling of a scalar binding as a descriptor table is permitted.

Required shader semantics: the Inputs array has explicit sampled-depth semantics,
logical set0/binding0, cardinality3. Compute uses it at Compute visibility and
writes Output storage buffer at set1/binding0/cardinality1. Its workgroup is
5x1x1. Graphics uses Inputs only in fragmentMain, so the authoritative descriptor
visibility is Fragment; vertexMain has no active descriptors. The combined
module must preserve that stage union. Determine visibility from per-entry
SPIR-V reflection, never from names or source declaration frequency. The explicit
ARP_SAMPLED_DEPTH annotation is the reviewed texture semantic metadata consumed
by the existing compiler's depth lowering; do not infer depth from float alone.

Quick artifact tests must load through original quota-bound immutable loaders,
verify Source/SPIR-V/MSL/WGSL/reflection bytes and original owner seals, and assert
actual binding type/set/binding/arrayCount/depth/stage and compute workgroup.
Graphics children and combined module must agree on authoritative bindings and
module identity. Missing/inconsistent generated facts fail diagnostic validation;
no native actor should be constructed until these facts are proven. Artifact
success certifies immutable metadata only, never native table binding or output.
Null hidden-frame and both actual WebGPU browser regression paths remain required.

Four backend decisions for this stage: common L2 immutable publication/validation
is Equivalent; Null is ValidatorOnly for GPU work, Vulkan/Metal accepted-original
actors remain DiagnosticFailure, and WebGPU native original actor/table Source
consumption requires the subsequent c2b4d2 fixture. Native command proof issuers
remain exactly Recording and Submission. Existing Source/root/seal/journal/counters
and cold/native quotas remain separate; shader bytes cannot bypass them.

Validation 2026-10-05: host20/20, Wasm/browser8/8, inventory201/201. All earlier
true browser flags preserved; captured diagnostics/nativeErrors empty. Evidence:
build/web-bindless-apple-debug/artifacts/bindless-continuation/descriptor-depth-validation.yaml.
RenderDoc unavailable (RHI-RENDERDOC-TOOL-UNAVAILABLE); no visible output change.

Next prerequisite /1: actual native array diagnostic probe. Feed the immutable
generated compute, vertex and fragment WGSL bytes to the exact original browser
device. Observe getCompilationInfo and captured validation error scopes; record
enabled device features verbatim without interpreting guessed feature names.
Expected unsupported WGSL rejection is diagnostic evidence, not a native table
success. Layout support alone cannot provide missing bind-group resource arrays.
The pinned WGPUBindGroupEntry resource representation and production array-count
gate must be checked separately. Null is ValidatorOnly; Vulkan/Metal original
actors remain DiagnosticFailure; WebGPU table execution remains DiagnosticFailure
until shader acceptance, exact native array entries and capability provenance
all have a proved contract. No new execution proof issuer or fallback is allowed.

WebGpuDescriptorArrayAbi/1 lowering boundary (defined before implementation):
L2 descriptor cardinality is the exact number of independent resources in one
logical binding. An array must never become a single resource or multiple
unrecorded bindings. For the pinned emdawnwebgpu browser bridge, resource entries
are scalar (buffer/sampler/textureView); its makeEntry function does not forward
bindingArraySize and asserts cardinality <=1. Thus this version has no native
array payload or equivalent lowering. Cardinality0 is invalid; cardinality1
retains the existing scalar path; cardinality>1 fails before native layout or
shader creation with RHI-WEBGPU-DESCRIPTOR-ARRAY-ABI-UNSUPPORTED. This conclusion
comes from the version-controlled bridge contract, regardless of browser name,
OS, guessed features, layout metadata or compilation acceptance. An enabled
browser extension could not supply missing C ABI fields in this version.

Reuse one allocation-free count/reflection diagnostic helper for compute and
graphics original factories and native layout entry construction. Factory array
rejection grants no native payload, no Source or capability promotion; native
layout entry failure must propagate as failure through existing callers. Scalar
paths remain separately validated. Four outcomes: Null validates L2 cardinality
only; Vulkan/Metal original artifact actors retain DiagnosticFailure; WebGPU
scalar actors retain their existing acceptance contract, while arrays are
DiagnosticFailure. Quick tests cover zero/scalar/array/maximum cardinalities,
real artifact factory rejection before native calls, hot allocation0 under
hostile PMR, Null hidden-frame, both browser modes and source inventory. Browser
WGSL diagnostics remain distinct from this ABI execution gate. Recovery requires
a new proved ABI/lowering version with exact resource entries and capability
provenance, plus original actor/table tests; no silent retry or emulation.

Verified 2026-10-05: quick/Null5/5, Wasm/browser8/8, inventory202/202. Real
compute/fragment diagnostics name sized_binding_array as a disallowed language
feature; vertex compilation succeeds with no descriptor resources. Neither
observation promotes arrays. Both original factories reject before native calls
(0) and allocations (0), including exhausted quota and hostile PMR. Earlier true
flags preserved and uncaptured errors empty. This is Contract Complete /
Prototype Complete / Backend Validated for the selected rejection path only.
Evidence descriptor-array-abi-validation.yaml. Full accepted actor/table Source
integration remains open; independent wrapper-retirement work continues.
