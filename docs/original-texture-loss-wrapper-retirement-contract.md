# Original attachment wrapper retirement after device loss /1

Status: Contract Complete / Prototype Complete / Backend Validated for selected
WebGPU Texture/View wrapper retirement. Not Platform Validated or Production
Ready. Verified 2026-10-05 host19/19, Wasm/browser8/8, inventory197/197.
Selected prerequisite c2b4b1 for Pipeline positional Inputs graphics loss evidence.

L2 original Source/Usage/Inputs ownership remains retained until the last caller,
recording/submission and callback custodian drops it. Device loss rejects further
GPU work and invalidates the existing state/lifetime domains. Once this custody
drains, native reference wrappers must retire even if the physical device is lost.
CPU quota zero cannot substitute for actual native-wrapper retirement. No native
getter, new root, completion primitive or execution fallback is introduced.

For WebGPU Texture and TextureView, Release drops the host API reference. It does
not record or submit GPU work and remains legal after device loss. Their final
payload destructors must call wgpuTextureRelease / wgpuTextureViewRelease for
every non-null handle. Explicit TextureDestroy retains the existing healthy-
device and non-browser-surface policy; surface destruction is not authorized by
this contract. No native allocation, blocking wait, file IO or query is added.

| Backend | Final original texture/view custody retirement |
| --- | --- |
| Null | Equivalent CPU validator custody; no native wrappers |
| Vulkan | Native owner cleanup unchanged; accepted typed execution remains DiagnosticFailure |
| Metal | Native retained-object cleanup unchanged; accepted typed execution remains DiagnosticFailure |
| WebGPU | Native host-reference Release after loss; GPU execution stays rejected |

Required verification: quick contract/Null hidden-frame, inventory guard against
loss-conditioned Release, both actual WebGPU browser modes, real texture/view
wrappers observed live before loss and absent after original Inputs/Source/factory
custody drains, all related quotas zero, earlier lifecycle/retention flags intact
and diagnostics/nativeErrors empty. Capture RenderDoc availability diagnostics;
no visual-output or shader changes. Other native wrapper classes are a separate
audit: this selected fix does not certify their post-loss retirement.

Code assessment: Vulkan PlacedImagePayload destructor in vulkan_device.cpp drops
vkDestroyImage on its non-null owned device/image; Metal retainNativeObject in
metal_native_resources.mm drops Objective-C retained references without a loss
condition. Their lowering/execution gates are unchanged, with no new native-loss
validation claim. The Emscripten API reference-release manager deletes host
references independently of GPU work; actual WebGPU browser evidence confirms
both original texture/view wrappers retire after physical loss. Initial failing
reports prove CPU quotas alone missed these leaked references. Both final modes
retain earlier lifecycle flags and empty diagnostics/nativeErrors. Evidence in
pipeline-positional-validation.yaml and the final reports. RenderDoc unavailable
(RHI-RENDERDOC-TOOL-UNAVAILABLE); no shader or visible-output changes.
