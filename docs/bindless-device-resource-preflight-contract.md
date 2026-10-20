# Original device resource preflight /1

Status: implementation in progress; no new platform or production claim.

This increment connects ResourceDemand/1 from the original sealed inventory to
cached facts of its exact registered device, before public execution prepares
any native payload, recording seal or reservation. It creates no resource table,
owner graph, state/content journal, reservation or execution authority. Cold Root
closure preparation remains legal before the usage graph seals its inventory.

L2 sizes include the complete buffer allocation, uniform/storage binding slices,
2D/cube face dimensions, 3D dimensions and physical array layers. The shared 2D
ceiling must cover both kinds: Vulkan publishes min(maxImageDimension2D,
maxImageDimensionCube), never infers the cube limit from the 2D limit. A required zero
fact is unavailable; exact limits pass and one-less limits fail. Size-only
validation is independent of alignment. Existing full validation continues to
require queried global alignments. Selected Metal pipeline argument reflection
remains the authority for each original binding; a zero global alignment is not
filled with a guessed constant. Formats, operations, memory residency and
independent in-flight quotas remain separate gates.

The inspection visit budget derives only from already admitted immutable Source
budgets: sum of per-domain source ceilings; sum of frozen-element ceilings on
Root/set/table sources; two retained resource visits per Source from the closed
source+destination algebra. Checked addition/multiplication must refuse overflow.
This bounds traversal and does not authorize new resources or reservations.
An invalid/unsealed/foreign/retired inventory refuses before device validation.
Failure clears the entire output; validation does not allocate, wait or perform
IO. Actual execution authenticates the device's original BindlessCross issuer;
authored scalar facts cannot substitute for that issuer.

## Versioned four-backend lowering

| Backend | Resource facts | Alignment | Decision |
| --- | --- | --- | --- |
| Null | No native facts published | Validator declarations only | Explicit device-preflight refusal; no execution |
| Vulkan | Physical device limits; whole buffer from queried maintenance4 maxBufferSize when core1.3 or KHR support is established | Queried minUniform/StorageBufferOffsetAlignment | Native size/shape preflight; missing required query refuses |
| Metal | Actual maxBufferLength; queried Metal3 family supplies conservative guaranteed 16384/2048/2048 shape ceilings | Original selected pipeline MTLArgument.bufferAlignment | Native size/shape preflight; no fabricated global alignment |
| WebGPU | Created device.limits, including safe-integer maxBufferSize; no adapter/bootstrap substitution | Created device minUniform/StorageBufferOffsetAlignment | Equivalent bounded preflight; whole-scene execution gate unchanged |

WebGL and unknown backends refuse. Metal shape guarantees are the Apple7 minima
for all Metal3 devices in Apple's [feature tables](https://developer.apple.com/metal/capabilities/)
(2026-05-21, footnote1). Layer ceiling is conservative physical faces; cube arrays
do not multiply it by six. Vulkan maxBufferSize is the buffer creation ceiling
in [maintenance4](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceMaintenance4Properties.html),
not maxStorageBufferRange. Unknown family/SDK/property support publishes zero.

Required checks: size and alignment independence; every exact/one-less/unknown
size fact; four-backend decisions; original identity/lifetime and unsealed
refusals; hot0; positive actual Vulkan/Metal/Chrome public PBR/Terrain27 execution,
fault and retirement. Keep failures and source epochs. Do not infer the common
engine gate, renderer/WOCC readiness or complete in-flight quotas from success.
