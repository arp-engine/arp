# Original descriptor dependency wrapper retirement after loss /1

Status: Contract Complete / Prototype Complete / Backend Validated for the
selected WebGPU wrapper cleanup scope. Required c2b4e prerequisite; not Platform
Validated or Production Ready. Original native table execution remains closed.

L2 original descriptor and resource custody retains native objects until all
callers, recording/submission/callback and quarantine owners drain. Device loss
rejects work; it does not authorize early destruction of this custody. Once the
last owner drains, host API references must retire regardless of physical loss.
CPU quota release alone cannot certify native wrapper retirement. Reuse the
Texture/View /1 release contract; add no owner graph, root, journal or proof.

Scope: WebGpuNativeBufferPayload, WebGpuNativeSamplerPayload,
WebGpuNativeDescriptorGroupPayload (group plus layout),
WebGpuNativeRenderBundlePayload (encoder, finished bundle and retained native
descriptor groups), and command-local group/layout/bundle release helpers.
Temporary group-layout references in native pipeline and bundle lowering follow
the same host Release rule, including partial native layout creation failure.
Each non-null host handle receives exactly its owned Release after loss. Buffer
Destroy retains the healthy-device gate; this contract never submits GPU work,
forces a wait, changes loss observations or clears outstanding quarantine owners.
Other command/queue/query/device wrapper release paths require a separate audit
and are not certified by this scope. No broad production readiness is claimed.

| Backend | Final dependency custody retirement |
| --- | --- |
| Null | Equivalent CPU validator ownership; no native handles |
| Vulkan | Existing native owner cleanup, typed execution remains DiagnosticFailure |
| Metal | Existing retained-object cleanup, typed execution remains DiagnosticFailure |
| WebGPU | Native host-reference Release after final custody drain, work stays rejected |

Quick source contract tests must prohibit loss-conditioned Releases for all named
classes/helpers, retain BufferDestroy's loss gate, and preserve callback/custody
mechanisms. Null hidden-frame remains required. Native browser probes must use
actual valid buffer/sampler/group/layout/bundle encoder/bundle wrappers, observe
them live before physical loss, keep them live while another payload custodian
retains them, then observe absence after final custodian release and quota0.
Verify both browser modes and all prior lifecycle flags, with captured validation
scopes and no uncaptured errors. These backend-private fixture objects certify
cleanup only; they do not simulate a successful original table Source or execution.
Record RenderDoc unavailable diagnostics; no shader or visual change is needed.

Four-backend code evidence: Vulkan NativeDescriptorSetPayload destroys its owned
pool/layout and NativeSamplerPayload destroys its sampler with no physical-loss
condition (vulkan_native_resources.cpp); VmaBufferPayload retains VmaState until
vmaDestroyBuffer (vulkan_allocator.cpp). Metal retainNativeObject drops its retained
Objective-C reference with no loss condition (metal_native_resources.mm). These
are unchanged cleanup paths, not new native-loss validation claims. WebGPU real
wrapper probes are required for the modified scope; other command/queue/query/
device wrapper paths remain outside this certification.

Validation 2026-10-05: host20/20 (quick artifact/descriptor/retention and Null
hidden-frame included), Wasm/browser8/8, inventory203/203. The initial actual
browser failure reproduces native wrapper leakage after quota0. Final probes
retain six real native wrappers through physical loss and caller drop, then
verify all six absent after the last payload custodian drains, quota0 and hot
allocation0 under exhausted quota/hostile PMR. Both browser modes preserve every
previous true flag and have empty uncaptured diagnostics/nativeErrors. Existing
callback/quarantine ownership remains intact. A stale host source assertion for
bindingArraySize was updated to require pre-native ABI rejection; its initial
failure is preserved. Evidence descriptor-wrapper-retirement-validation.yaml and
final reports. RenderDoc unavailable; no shader/visual output change.
