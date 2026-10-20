# Bindless implicit sampling reference /1

This numerical oracle compares the existing public OriginalExecution3/4 Root
sampleGrad output with an independent, statically bound Traditional shader.
It adds no execution profile or capability. Scene-wide implicit sampling,
material ABI, quality and platform readiness remain separate gates.

The ordinary Slang reference uses two textures and independent nearest/linear
samplers. Real implicit Sample operations execute before non-uniform material
selection and array-layer discard. The existing Root computes derivatives of
the same UV before table/sampler dispatch and discard. Both use the documented
RGBA8 upload input, two mips, and either one layer or two array layers. The
reference keeps its explicitly authored SampleLevel control separate from its
implicit mip-selection channel. Mip0 and mip1 must produce different results.

`compile_shaders.py --preserve-implicit-sampling` is an explicit ordinary
single-entry/graphics-module publication policy. It propagates to sealed child
entries and bypasses the existing general WGSL base-level rewrite. Combined and
selected Fragment WGSL must contain real textureSample; their SPIR-V must contain
OpImageSampleImplicitLod. All source/SPIR-V/MSL/WGSL/reflection roles and child
publications are sealed. Native WGSL validation remains enabled. A re-sealed
legacy fixed-LOD substitution must fail reference admission.

The browser oracle is test-only native WebGPU code. It creates a separate ordinary
pipeline, uploads the defined input, waits for genuine queue and map completion,
and compares all sixteen RGBA bytes against both clean public Root receipts in
the same run. Public Root execution, fault and retirement are established by the
existing C++ lifecycle cohort, independently of the oracle. Three native error
scopes must drain without errors; reference buffers/textures/device are destroyed.
The host validates the shader digest, dimension, receipt correspondence and
non-base mip control before accepting the report. No fabricated receipt or
oracle success can grant an RHI capability.

| Backend | Effect of this contract |
|---|---|
| Null | No numerical execution; existing execution refusal remains |
| Vulkan | Sealed SPIR-V preserves implicit derivatives; no new runtime evidence |
| Metal | Sealed MSL generated; runtime/capture deferred per Metal-last order |
| WebGPU | Ordinary preserved-derivative reference versus existing public Root; native validator rejects invalid uniformity |

The old general compiler policy and WOCC selected Traditional artifacts still
need their own migration and scene-quality verification. A finite RGBA8 fixture
does not establish full-world budget, all mip/filter modes, PBR/terrain semantic
parity, Release performance, or Production Ready status.
