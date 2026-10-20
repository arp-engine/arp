# ResourceMemoryBarrier/1

Status: Contract Complete. Metal Backend Validated for the buffer dependency
scope; it is not a macOS Platform Validated or Production Ready claim.

## L2 semantics

`ResourceMemoryBarrierDesc` expresses a dependency between earlier and later
accesses to one live resource without changing its logical `ResourceState`.
It is the only legal representation of an access-only dependency such as
compute-write -> compute-read/write between two command encoders. A
`ResourceBarrierDesc` still represents a state transition and continues to
reject `before == after`.

The descriptor must identify the resource and provide non-empty source and
destination stage/access masks. It has no implicit state, native handle,
queue serial, retry rule, or fallback. The command encoder rejects malformed
descriptors, foreign resources, recording after finish, or an active pass with
a stable diagnostic. Recording a memory barrier neither mutates resource-state
metadata nor enlists the resource in `RecordedResourceState/1`.

## Lowering

| Backend | Result |
| --- | --- |
| Null | Equivalent validator and command record; state remains unchanged. |
| Vulkan | Native `VkBufferMemoryBarrier` with the exact L2 stage/access masks; no image-layout transition. |
| Metal | Equivalent command-encoder boundary; Metal hazard tracking orders the resource without guessing a layout. |
| WebGPU | Equivalent encoder ordering/usage tracking; WebGPU exposes no explicit native barrier command. |

All four backends use the existing stage/access barrier capability decision.
The texture overload applies the dependency conservatively to the entire live
texture, including all mips/layers. It preserves every subresource state even
when those states differ. Null validates and records; Vulkan emits an access-only
`VkMemoryBarrier` (no image layout change); Metal and WebGPU use their tracked
encoder-boundary ordering. Native storage must remain retained through completion.
The texture overload does not reinterpret a same-state transition as a valid
`ResourceBarrierDesc`. Explicit image transitions must consume the supplied
stage/access masks unchanged; inferring them again from resource state is invalid.

No CPU replay, dummy resource, `waitIdle`, polling loop, or upper-layer
backend branch is permitted.

## Required proof

Fast tests must prove a valid buffer memory barrier keeps the resource state,
and malformed descriptors are rejected while ordinary no-op state transitions
remain rejected. Backend tests must prove the lowering uses the existing native
payload path. WOCC tests must exercise clear -> cull and cull -> emit without
Metal barrier diagnostics.

## Evidence

- Debug `arp_rhi_command_contract_tests` passed: valid access-only dependency
  preserves `ShaderWrite`; a malformed descriptor and a no-op state transition
  both fail deterministically.
- Debug `arp_rhi_metal_staging_upload_pool_tests` passed with a real Metal
  device/queue submission and a `ShaderWrite` buffer memory barrier.
- WOCC `arp_wocc_gpu_pending_tests` passed with `MTL_DEBUG_LAYER=1` and
  `MTL_SHADER_VALIDATION=1`: Release 8.41 s and Debug 69.31 s. Both logs show
  the expected `uploaded 96 vegetation templates (96 valid)` and no
  `RHI-METAL-BARRIER-NO-OP-TRANSITION` or
  `RHI-METAL-BARRIER-BEFORE-STATE-MISMATCH` diagnostic.
- The WebGPU backend implementation compiled in the macOS Debug graph. Vulkan
  native compilation/execution and WebGPU browser-native execution are
  `NOT-RUN` on this Apple host and remain explicit follow-up evidence.

Texture increment (Windows host): all four overloads now implement the above
contract. Null rejects foreign texture IDs, active passes and finished recording;
Vulkan retains real image storage and emits an access-only memory barrier.
The RenderGraph Vulkan hidden smoke uses Load after Clear on the same attachment
state. RenderDoc verifies four native access-only dependencies and unchanged
presented pixels, with no validation messages; see
`build/win-vulkan-debug/artifacts/bindless-graph-sync/capture-report.json`.
Metal host-stub and WebGPU Emscripten builds pass; this new texture scope has no
Metal-device or browser-native execution evidence yet.
