# PublicFenceCompletion/1

Status: versioned contract and implementation. Executed evidence and readiness are
recorded in `rhi-webgl1-validation.md`; source/host checks do not certify Apple/browser.

A queue completion fence observes one original accepted native submission. Public host signal
cannot establish GPU completion and must throw a stable diagnostic for production fences.
An unbound fence is unsignaled. Reset while its original command is pending must reject;
reset after terminal completion detaches that one binding. Reuse without reset must reject
before any native submit, command mutation, retention transfer or submit-count increment.
Backend-private original completion states remain inaccessible through the public fence.
Terminal completion permits lifetime reclamation, independently of successful output.
Metal wait reports a failed native outcome; Vulkan wait propagates native wait failure.
Readback/publication must still validate its original successful execution evidence.
No public signature, vtable or descriptor layout changes. CPU-signaled timeline/semaphore
intent remains separate: it authorizes explicit ordering values, not GPU completion receipts.

| Backend | Conclusion |
| --- | --- |
| Null | Explicit validator/Dummy signal remains validator-only. |
| Vulkan | Original payload's submitted fence is the sole completion source; host signal rejects and pending reset/rebinding reject. |
| Metal | Fence binds to the original command's pre-created completion state, published by its terminal callback. Host signal and pending reset/rebinding reject. |
| WebGPU | Generic submission remains refused. Public createFence and any submission with a supplied fence reject RHI-WEBGPU-PUBLIC-FENCE-RESOURCE-GRAPH-UNAVAILABLE; nativeSync remains false. Private ordinary signal tickets cannot manufacture a public completion fence. |
| WebGL | Production factory and NativeSync remain refused; no dummy fence is introduced. |

Quick tests must exercise actual factory-produced fences, unbound signal rejection and exact
pending/terminal reset/reuse. Real Vulkan timeline blocking proves pending semantics; Apple
native execution is a separate platform gate. WebGPU source/host tests do not claim browser
completion validation. Existing readback/publication paths consume original native tickets,
not this public signal method; their joined queue/map/error-scope evidence stays required.
