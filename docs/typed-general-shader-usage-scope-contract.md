# TypedGeneralShaderUsageScope/1

This prerequisite extends `TypedShaderUsageScopeContract` with its explicit
version5 value profile. It reuses the original positional command sequence,
resource-access validation and bounded pair traversal. Versions1-4 retain their
resource allowlists. No new resource inventory, state owner, journal, native
payload, recording adapter or completion receipt is created.

The authority for usage compatibility is WebGPU CRD2026-09-15 sections3.4.3 and
3.4.4: https://www.w3.org/TR/2026/CRD-webgpu-20260915/#internal-usage . A whole
buffer is a subresource; a texture subresource is selected by aspect, mip and
array-layer ranges. Compute dispatches have independent scopes. Every render
binding contributes to the whole pass, including shadowed bindings. Writable
alias validation at an individual draw/dispatch is independent from whole-pass
compatibility. These are L2 validation rules, not native feature inference.

Version5 adds UniformRead buffers and StorageRead/Storage textures. UniformRead
is constant usage and may coexist with readonly storage and vertex/index inputs.
Its access must be ShaderRead in the correct shader stage. StorageRead texture
usage is derived from a readonly storage binding; Storage derives from a writable
binding, even when a shader operation only reads. Both require color aspect and
validated shader-only access. Sampled and readonly storage texture usages may
coexist for the same subresource and state. Storage and any readonly/attachment
usage cannot coexist in one scope. Multiple Storage bindings may coexist across
draws or shadowed binds, but overlapping writable texture subresources at one
draw/dispatch must fail with `RHI-TYPED-USAGE-SCOPE-TEXTURE-WRITABLE-ALIAS`.
Different mips/layers/aspects are checked independently; different resource
generations are never treated as non-aliasing. Buffer compatibility likewise
ignores byte disjointness; per-consumption writable-buffer range checks remain.

Every bind and consumption must validate its access, contribution position and
scope. Ordinary descriptor-set and table render bindings share these rules.
Wrong shader stage, writes through readonly bindings, legacy profile use, invalid
range/identity, incompatible whole-resource usages, overlapping writable aliases
and one-less authored traversal budgets reject. Failed inspections clear counts.
Sampler payload ownership/type/comparison validation belongs to the original
descriptor projection; a sampler has no memory usage-scope contribution.

| Backend | Value validation | Native execution decision for this increment |
|---|---|---|
| Null | Version5 positive/negative validator | ValidatorOnly; no GPU execution |
| Vulkan | Shared semantics available | DiagnosticFailure; original general projection/payload missing |
| Metal | Shared semantics available | DiagnosticFailure; original general projection/Apple payload missing |
| WebGPU | Shared semantics available | DiagnosticFailure; original general projection/public adapters missing |

The existing `lowering(backend)` remains closed for all four backends. Pure
success is not complete original contribution coverage, synchronization,
shader-operation equivalence, recording, submission or completion authority.
Current original usage projection versions1-4 must continue rejecting uniform
and storage-texture inputs until their source, binding, reflection and range
projection is explicitly extended and tested. The following increment must wire
this profile into that original graph, retaining conservative full-table access
sets, before native/public adapters or Emulated capability can open.

Validation must include assertion-enabled Host contract and Null hidden-frame
smoke, existing legacy usage/geometry/readback checks, source inventory and a
browser contract entry. The 2026-10-10 value prerequisite is Contract Complete /
Prototype Complete: rebuilt assertion-enabled MinGW Host/Null5/5 (2.20s), source
inventory319/319 (9.110s), Release browser values plus original table/geometry
numeric completion2/2 (23.33s), and independent ordinary numeric geometry pass.
The ordinary report has numericalRenderedReadback=true, public execution=false
and empty diagnostics/nativeErrors. Python/generated JS syntax checks pass.
The first Host fixture failure omitted BindIndexBuffer before DrawIndexed;
the corrected authored sequence supplies the real positional binding without
relaxing production validation. Its selected failure evidence is preserved.
These value checks claim no Backend Validated, Platform Validated or Production
Ready. The preceding17/17 lifecycle run predates the shared-header increment.
