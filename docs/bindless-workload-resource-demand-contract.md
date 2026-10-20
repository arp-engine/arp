# Bindless workload resource demand /1 and WebGPU device request /2

Status: Contract Complete; authored planner/resource-binding smoke Platform
Validated as recorded below. This is cold planning, never execution authority.

The existing PipelineInterface/1 inspection remains the single source for all
indexed, ordinary, reserved, metadata and fault binding counts. ResourceDemand/1
adds borrowed BufferDesc and TextureDesc allocation declarations, plus finite or
whole buffer binding declarations referring to those buffers by checked index.
Buffers without descriptors (vertex, indirect, upload, readback) still count
towards maxBufferSize. A buffer's full allocation and its binding slice have
different limits. Slice containment uses DescriptorBufferBindingRangeContract;
texture mip geometry and logical footprint use the existing texture helpers.
Offsets retain their authored values. No padding, clamping, splitting, resource
repacking or quality change is permitted. Nonzero offset divisors are the gcd of
the actual authored offsets, not an assumed device alignment.

The allocation-free inspector resets its entire output on failure. It derives
maximum buffer allocation, uniform/storage binding size, 2D/3D dimension and
array layer requirements. All supplied allocations are inspected even if they
have no shader binding. Buffer usage/type, ranges, format, mip count, cube and
3D shape are validated. This does not authenticate completeness against a live
Root, validate format operations or infer native resident bytes. Callers must
supply the full workload including frame/GI, snapshot/fault and helper resources.
Missing original inventory correspondence must continue to refuse execution.

Four-backend lowering:

| Backend | ResourceDemand/1 |
| --- | --- |
| Null | Validator only; authored scalars cannot grant execution |
| Vulkan | Cold planning only; queried limits and original allocation/state/lifetime gates remain mandatory |
| Metal | Cold planning only; queried limits, argument-buffer/residency and original completion gates remain mandatory |
| WebGPU | Cold planning only; device request /2 consumes this demand and the existing full layout demand |
| WebGL / unknown | Deterministic diagnostic failure |

WebGPU DeviceLimitPlan/1 keeps its descriptor-only ABI. DeviceLimitPlan/2 is an
explicit additional entry point: it derives a single requiredLimits dictionary
from the existing /1 binding plan and ResourceDemand/1. Larger-is-better limits
are checked against queried adapter maxima. Alignment limits use the opposite
direction: request the queried adapter minimum only when the authored offsets
need it, and require it to divide every offset. Unknown relevant limits fail
closed; zero unused demand emits no artificial request. Values sent through JS
must be exactly representable integers. The created device must be rechecked
before use; adapter feasibility does not prove device capacity.

CPU quotas, resource allocation/native observation, upload/readback staging,
snapshot generation, binding retention and pending callback/retirement capacity
remain independent enforced owner/lease domains. They must not be summed into
a fictitious resident total, or replaced by this pure device-limit plan. Full
in-flight budget and live original-inventory correspondence remain separate
engine exit requirements.

Required tests: exact/one-less maxima; allocation versus binding range; invalid
usage/index/range/mip/cube/3D; offset gcd and alignment direction; overflow and
zero output on failure; all backend decisions; no hot allocation. A real browser
must execute the C++ planner, request its dictionary, revalidate actual device
limits and create declared resources and finite buffer bindings under native
error scopes. This smoke grants neither bindless execution nor the engine gate.

## Current validation (2026-10-19)

Current scoped increment (2026-10-19): ResourceDemand/1 and explicit WebGPU
DeviceLimitPlan/2 now add resource size/shape/alignment to the existing complete
PipelineInterface/1 binding planner. The inspector reuses original finite-range
and mip/footprint helpers, handles non-descriptor buffers, distinguishes full
allocation from binding slices and clears all output on failure. Exact/one-less,
range/usage/shape/overflow, gcd alignment direction and hot0 checks pass.
Windows4/4, Wasm CTest5/5 (Node3 and browser2, including the existing /1 modes)
and Mac6/6 pass. A final real Chrome smoke creates4 buffers/32 textures/1 sampler
and one complete36-binding group, using the C++ generated15-limit request and
rechecking actual device limits, with empty native errors. This is an authored
planning/resource-binding workload, not production Root inventory or execution
acceptance. Mac's native5 affected inputs match its staged epoch; the final
browser-only complete-group observer follows that native epoch. Source style7
and package layout4 pass. Evidence:
`D:/arp/build/bindless-workload-resources1/current-report.json`.

Next: project live original inventory and full workload correspondence into
device/resource preflight; finish independent in-flight owner/quota budgets,
production pass semantics, cross-pass atomic/indirect and remaining lifecycle.
Then pass the same integrated three-backend engine gate before renderer/WOCC.
Common gate=false; WOCC=Traditional; renderer migration has not started.

