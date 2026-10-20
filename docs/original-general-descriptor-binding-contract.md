# OriginalGeneralDescriptorBinding/1

This independently opted-in borrowed-entry prerequisite covers six scalar resource
classes: whole UniformBuffer, ReadOnlyStorageBuffer and StorageBuffer; SampledTexture
and StorageTexture views with their original parent; and Sampler. It reuses the
original Sources registry, per-resource canonical dependencies, full-stage conservative
footprint validation, accepted pipeline contract and original target buffer layout.
It does not retain the mutable Sources inventory or introduce a second owner graph.

| Backend | Lowering | Authority |
|---|---|---|
| Null | DiagnosticFailure, validator only | No native preparation or execution |
| Vulkan | DiagnosticFailure, missing original adapter | Existing bounded BindlessCross Native domain is unchanged |
| Metal | DiagnosticFailure, missing original adapter | Existing bounded Compute domain is unchanged |
| WebGPU | Equivalent private borrowed entry | No group publication, recording, submission or completion |

Request version1 and explicit prototype opt-in are required. Arrays, table-backed
bindings, dynamic bindings, finite buffer ranges, input attachments, push mappings,
unaccepted pipelines and physical type substitution fail with stable diagnostics.
Buffer binding consumes the original shader target group/binding/minimum, complete
buffer coverage and captured pipeline limits. Texture format/access/dimension/aspect
and sampler comparison/stages/nonmemory semantics consume existing L2 validators.

Native entry generation queries no limits, allocates no owned objects and invokes no
resource creation or queue API. Authentic wrapper-owned nonblocking registry observations
reject missing, foreign or mutated native handles/device/backing. Sampler observe/validate
overloads reuse WebGpuOrdinaryResourceObservation and the existing registry identity.
Outputs are cleared before any check and published only after complete validation.

A scoped transaction must retain exact original dependency owners and native
backing, consume independent bounded quotas and genuine error scopes, atomically publish
the complete group/revision, and preserve pending candidates through callback drain.
This prerequisite is not a substitute for that transaction, native closure/freeze/payload,
recording, real queue completion or public Emulated authorization.

The private OriginalGeneralDescriptorWrite/1 transaction now supplies this
creation/publication join through the existing candidate slot and genuine scopes;
see original-general-descriptor-write-contract.md for its selected evidence.
The later command/native closure and GPU completion boundaries remain closed.

Validation (2026-10-10): rebuilt assertion-enabled MinGW Host four-backend contracts/Null
smoke4/4 pass5.91s, source326/326, package/root8/8 and Wasm Release artifact2/2 pass.
Actual Chrome scalar-entry fixture1/1 passes2.36s for UniformBuffer, StorageTexture,
SampledTexture and Sampler. It validates original source/generation, accepted pipeline,
target memory, missing/mutated nonnull native handles, view-parent identity, full fields,
cleared failure outputs and hot allocation/query0. ReadOnlyStorageBuffer/StorageBuffer
have six-class schema coverage here; this adds no GPU execution proof for any type.
Affected shader/lifecycle/table-geometry browser regression5/5 passes45.01s; independent
ordinary numeric geometry/readback/completion/retirement passes on these artifacts.
The first failed browser report preserves unrequested target-memory admission; the
fixture now requests the existing ABI proof and separately asserts that missing opt-in
refuses. The second failure caught WHOLE_SIZE in unused native fields, now explicitly
zeroed. Both failed reports/logs remain. State owner ceiling48/actual44 and every existing
production quota/deadline remain unchanged. Selected current sources/artifacts/reports
are archived in `build/web-webgpu-release/artifacts/bindless-continuation/general-binding-release-validated/manifest.json`;
`transitiveBuildInputsComplete=false`. Public WebGPU
remains DiagnosticFailure/executablePayload=false. P3-P6 and all four full objectives remain
incomplete; no broad Backend/Platform Validated or Production Ready promotion.
