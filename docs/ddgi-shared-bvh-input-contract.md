# DdgiSharedBvhInput/1

Status: specified; implementation and evidence are tracked independently.
Scope: flat geometry, split static BLAS, dynamic TLAS, their node/leaf/emissive
buffers and the shared instance table. No final ray/GI or production acceptance.

Every accepted DDGI input reader leases the existing submission group through
GpuInputSlotLease/1. Before changing shared BVH storage or its authoritative CPU
cache, poll all existing slot leases. Any pending reader yields
DDGI-SHARED-BVH-INPUT-NOT-READY with no writes, allocation, cache/telemetry change
or prepared-token invalidation. No-change reuse under the existing revision
contract may proceed. No new fences, waits, GPU storage or reader collections.

On an admitted mutation, invalidate the prepared input/history token and shared
BVH cache validity before modification. Publish validity only after every needed
allocation/upload succeeds. A partially written physical version is not usable;
preparation and direct recording reject until a successful BVH update recovers it.
Retry after failure, or switching flat/split representations that share physical
buffers, requires rebuilding and fully uploading all required domains. Returning
to an old revision cannot recover validity through a cache hit. Existing accepted
GPU work is never rolled back and never given a fabricated completion.

Allocation/upload failure returns success=false with an owned stable domain
diagnostic and preserves the underlying upload diagnostic when one is available;
zero geometry is not an error substitute. Callers must propagate the failure and
must not prepare or submit a frame from its default zero counts. Empty input is
an explicit valid state. Nonzero count with null source and excess instance count
are deterministic invalid input, not silent removal/truncation. Reuse requires
matching static and dynamic counts even when a count becomes zero: removing one
domain while the other survives must publish the new counts, not the old cache.

Four-backend mapping reuses unchanged lease/completion behavior: Null validator
fences; Metal native command completion; Vulkan native queue fences; WebGPU
asynchronous queue completion. Cache publication and mutation admission are common
renderer semantics, with no native handle/OS/backend branch upstairs. Capability
unavailability fails closed. Native evidence per backend remains separate.

Storage remains owned by DDGI/RHI; existing memory budgets govern allocations.
The new admission/validity state is fixed size and does not allocate or wait.
This contract does not close the separate total physical budget gate.

Successful upload telemetry includes instance-table bytes and ranges as well
as tree storage; transform/count-only updates cannot report zero transferred
bytes after writing the table. Busy rejection leaves prior telemetry unchanged.

Required evidence: three-path pending/no-change/mutation controls; actual Metal
old/new node and leaf bytes; static/dynamic/instance changes and removal; both
reader completion orders; partial upload plus same/old revision recovery;
allocation failure and retry; mode switches; stale token rejection; real ray
hit/material/distance cases and final retirement. Native tests must use actual
buffers and real completion. WOCC production GPU deferral and full four-backend
runtime validation remain separate gates. No native evidence is claimed yet.

## Shader consumption

The shared production traversal is also consumed by a native test entry point;
the test supplies rays and reads hit distance, material category, normal and
emissive data. It does not substitute a second traversal implementation or claim
material texture/lighting/visual acceptance. Shader inputs retain their existing
declarations, layouts and four-backend compilation. Matrix conventions do not
change in this work.

sceneBvhParams is (static leaf count, static node count, dynamic leaf count,
dynamic node count). Flat geometry is stored in the static buffers in world
space, with an identity instance table and zero dynamic counts. Its proxy AABB
count is telemetry, not a dynamic-tree count. Frame identity belongs in the
existing frame fields, never in the dynamic node count. Split counts describe
each physical tree separately, including removal to zero.

The instance table is a float4-addressed byte stream of DdgiSceneBvhInstance.
Its first stableId word contains a uint32 instance count and must be bitcast to
uint32 by the shader, not interpreted numerically as a float. CPU input count
zero requests the existing identity instance; explicit counts are bounded by
kMaximumSceneBvhInstances. Reuse includes the effective instance count. A switch
to flat mode must rewrite identity metadata/transforms before publication.

Production ray, shadow and combined-update entry points use the same geometry
predicate: either static or dynamic leaves are present. Static count zero does
not disable a populated dynamic tree. Empty trees remain valid misses.

For normalized world directions and finite nonsingular affine instance
transforms with the supplied inverse, local rays are normalized after applying
localFromWorld. The local distance interval is the world interval multiplied
by the length of that transformed direction; no empirical scale bound is valid.
Hit normals use the inverse transpose and are normalized in world space,
retaining the ray-facing orientation. Identity, translation, rotation, small
uniform scale and nonuniform scale must preserve world hit distance and normals.
This is the mathematical meaning of the existing row-array instance ABI, not
a matrix storage/layout migration. The same Slang logic lowers to Vulkan,
Metal and WebGPU; Null checks CPU parameters/ownership without claiming shader
execution. Each native backend still requires its own execution evidence.
