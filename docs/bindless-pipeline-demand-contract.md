# Bindless PipelineInterface demand /1

Status: Shared demand query Contract Complete (Windows3/3 and Wasm/Node1/1).
Device-request projection is Backend/Platform Validated for the authored layout
and sealed Terrain27 package on actual Chrome: default-device16 refuses the
36-texture layout, queried-adapter48 permits an exact36 request, and the actual
package requests/receives27 textures. The registered browser/Node demand and
Terrain declaration/data/oracle/codegen cohort passes6/6. Evidence:
`build/bindless-production-terrain27-1/package-plan-web-tests2.log` and
`device-plan-browser2/`. This is
layout capacity planning, never a capability, package authentication, execution
ticket, complete memory budget or renderer integration.

`inspectBindlessPipelineInterfaceDemand` derives allocation-free requirements
from the same complete compiler-owned `BindlessShaderArtifact` that feeds
`inspectBindlessPipelineInterface`. One canonical traversal validates all
indexed/ordinary domains, resource classes, physical slots, stage visibility,
texture/sampler/storage shapes and collisions. The existing queried-device
inspector preserves its diagnostics, refusal rules and zero-on-failure output.

The separate typed demand contains all per-stage descriptor totals, total
descriptor count, nonempty domain count, required group slots (highest set+1),
and required bindings per group. For Emulated the last includes highest scalar
binding+1 as well as count; Native binding numbers are not descriptor counts.
Read-only/writable storage buffers share their native stage bucket. All counts
must fit their native integer domains; overflow fails deterministically and
clears the entire output. Legacy reservation fields cannot coexist with /1.

Demand does not require an existing GPUDevice. A caller may compare it to a
queried adapter and explicitly request sufficient limits before device creation.
Adapter success never promotes device capability: the resulting actual device
must independently pass the existing runtime query and interface preflight.
No request is silently clamped, packed, split, or retried at lower quality.
Every active/reserved binding, snapshot metadata, fault and push resource must
already be present in the complete artifact; omitted resources cannot be
inferred or supplied by this query. Selected-entry custody remains independent.

| Backend | Mapping |
| --- | --- |
| Null | Shared typed layout demand; validator only, no native work. |
| Vulkan | Same demand against queried descriptor/set/stage limits; no new feature enablement. |
| Metal | Same demand against queried argument-buffer/stage limits; no residency or completion grant. |
| WebGPU | Scalar physical demand may plan explicit requiredLimits; actual device limits remain authoritative. Insufficient adapter/device limits refuse. |

Fast tests cover complete terrain-shaped material+reserved layouts, the
default-device16 versus queried-adapter48 texture boundary, exact/one-less
stage limits, sparse set/binding slots, shared storage accounting, duplicate
domains/bindings, overflow, zero-on-failure, no-allocation and no-execution.
The authored layout tests do not certify production terrain arithmetic. Null
hidden-frame smoke and existing planner tests guard the common path; actual
WebGPU limit query evidence is retained separately. Terrain27 weight arithmetic,
three-backend execution, complete live/in-flight budgets and platform lifecycle
remain required before the common production engine gate.

## WebGPU device-request projection /1

The backend-private `WebGpuBindlessDeviceLimitPlanner` consumes an Emulated
compiler artifact and explicitly queried adapter capacity. The adapter wrapper
has a separate type from runtime device limits/capabilities. It never becomes
`BindlessCapabilities`. The planner reuses the canonical demand query, rejects
insufficient adapter capacity before `requestDevice`, and clears all output on
failure. No lower-quality retry, packing or splitting is permitted.

WebGPU's per-stage limit is requested as the maximum of the three declared
stage counts. The seven projected keys are bind-group slots, bindings per group,
sampled textures, samplers, storage textures, uniform buffers and storage
buffers. Only nonzero demands are sent. Buffer byte sizes/alignment, texture
extent/layers, features and live/in-flight memory remain separate requirements;
this projection must not be described as a complete device descriptor.

Native Vulkan/Metal retain the shared complete-layout query and queried-device
preflight; this WebGPU-only dictionary does not apply to either. Null remains a
validator. Querying or successfully creating a WebGPU device grants no pipeline,
operation, residency, execution, completion or application-selection capability.
The created device must independently pass the existing actual-device query and
whole-interface preflight. Tests cover exact and one-less ceilings, stage maxima,
sparse slots, malformed/Native artifacts, overflow and allocation-free failure.
The browser smoke must request the derived values on a real adapter/device and
observe its returned limits; an adapter-only receipt is insufficient.
