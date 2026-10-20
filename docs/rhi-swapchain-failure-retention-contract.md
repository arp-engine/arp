# SwapchainFailureRetention/1

Implementation and validation status is recorded in rhi-webgl1-validation.md.
This definition alone grants no Backend/Platform/Production readiness.

An acquired image, submitted command and attempted present have distinct lifetimes.
Neither cancellation, a surface-availability notification nor a recoverable native
result proves completion. An acquire that succeeds natively must publish backend
ownership before a subsequent host wait can fail. Failed present or acquire-wait
closes further frame admission until the original swapchain is replaced.
Retained backend ownership is not public drawable admission: changing a failed
AcquiredFrame's success field cannot authorize isFrameAcquired/acquiredImage.

| Backend | Required decision |
| --- | --- |
| Null | Validator-only synchronous release; never native completion evidence |
| Vulkan | Unsubmitted discard/present rejects before queue invocation; surface changes preserve live owners; all native present failures retain their slot; failed acquire waits retain the acquired image. Final native destruction requires original acquire-fence completion and original device idle, plus release of retained image payloads |
| Metal | Existing native drawable/command completion graph remains authority; host diagnostic fixtures grant no native completion |
| WebGPU | Existing original completion-resource graph and loss invalidation remain authority; host diagnostic fixtures grant no browser completion |
| WebGL | Production swapchain unavailable until P2/P3; no fence or immediate GPU completion is synthesized |

Vulkan's backend-owned swapchain lifetime is reserved at cold creation. Its finite
process domain uses the existing kVulkanMaxAsyncSubmittedCommands capacity, one
reservation per live or quarantined native swapchain. The domain holds only weak
identities; a lifetime holds itself while native destruction remains unproven.
This intentional quarantine is bounded and is not successful completion. Exhaustion
rejects creation before native allocation. No destructor allocates a rescue slot.

Image payloads retain this exact lifetime. A facade's destruction requests retirement;
remaining image/command owners prevent native destruction. The original acquire
fences and successful original vkDeviceWaitIdle authorize destruction only after
external payload owners release. Missing entrypoints, timeout, native errors or
device loss retain the complete native owner and all handles. The cold retirement
pump retries the same issuer; it never substitutes another queue/runtime or a global
serial. An active failed acquire/present is counted as unproven even before facade
destruction, so the bounded shutdown report cannot publish Completed for it.
Failed retirement is observable by a stable diagnostic and pending count.
Process exit is not reported as a successful drain.

Acquisition, recording and presentation neither allocate quarantine storage nor wait
for quarantine cleanup. Creation/destruction and explicit shutdown retirement are
cold boundaries. Releasing an image payload never invokes the blocking pump;
the original device destructor or an explicit cold caller retries its own runtime.
The bounded shutdown report refuses completion while retired native lifetimes remain.
This does not certify pre-existing Vulkan legacy present polling
as a nonblocking hot path.

Resize must drain submitted commands and presentation, reject retained external
image owners, and preserve the old object on failure. A swapchain closed by an
unproven acquire/present result requires complete replacement; resize cannot turn
its binary semaphore state into a fresh generation. Surface notification fails
without clearing in-flight tokens. Synthetic validator behavior remains explicit.

Required negatives call original factories with real WSI/device/handles: unsubmitted
discard/present makes no native present call; surface notification does not revoke
the acquired frame; failed post-acquire wait closes admission; present error does
not free a slot; failed original idle destroys no semaphore/fence/swapchain and
retains the original native owner; successful retry releases exactly once. Null
hidden-frame and original Vulkan clear/alias/WSI RenderDoc regression remain required.
Metal/browser/mobile validation is not implied by these Vulkan tests.

## SurfaceMutationAdmission/1

Metal and WebGPU surface availability/resize/reconfigure first collect only original proven
completion, then reject RHI-{METAL,WEBGPU}-SURFACE-MUTATION-IN-FLIGHT if any slot remains
acquired or presented pending retirement. Rejection precedes native surface/layer mutation,
slot clearing, completion reset and descriptor update. It reports acquire/present disallowed,
no surface resource release, and rebuild required. Retry follows exact completion or explicit
unsubmitted discard; no frame age, public signal or forced cleanup may authorize the mutation.
Null remains a validator; Vulkan retains its stricter original-runtime WSI lifetime rules.
WebGL production remains unavailable. Host WebGPU metadata fixtures and Metal source contracts
are contract evidence only; Apple/browser native lifecycle validation remains a platform gate.
