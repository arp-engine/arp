# CompletionConsumerAudit/1

2026-10-17. Scope: P0-10 existing reachable consumers and explicit refusal boundaries.
This is a source/contract audit. Executed tests and readiness are recorded separately in
`rhi-webgl1-validation.md`; this document grants no Apple/browser production readiness.
Public signatures, vtables and descriptor layouts remain unchanged.

## Authority and consumers

| Consumer | Original authority / current execution gate | WebGL1 boundary |
| --- | --- | --- |
| GpuQueue/public fence | Vulkan `vulkan_device.cpp` binds the original native command fence; host signal, pending reset and rebinding reject. Metal `metal_device.cpp` binds only the original prepared completion state, resolved by the native terminal callback; the same host mutations reject. WebGPU generic submit lacks the original resource graph and refuses, public fence creation refuses `RHI-WEBGPU-PUBLIC-FENCE-RESOURCE-GRAPH-UNAVAILABLE`, nativeSync=false. Null signal is validator-only. | Production factory/submit and NativeSync remain refused. Ordered issue is not completion. |
| Command retention | `command_retention_storage.h` requires completion proof. Vulkan original submitted-command custody retains original runtime and owners through original fence or matching successful device idle; failed idle/loss does not authorize release. Metal pre-created SubmissionRetention is captured by the actual command callback. WebGPU ordinary submission joins original queue-done, three error scopes and exact resource graph; rejected generic submit cannot create an equivalent receipt. | P2/P3 must adopt original context objects before issuing work. CPU snapshots and owner existence are not execution evidence. |
| Frame slots / WSI | Vulkan `vulkan_backend_swapchain.cpp` uses exact acquired/presented ownership; failed present/idle and no-maintenance paths preserve unproven owners. Metal slot reuse joins original graphics completion and drawable-present completion. WebGPU uses the original native presentation signal/ticket. Metal/WebGPU surface loss/resize/reconfigure first collect exact completion, then reject any in-flight acquired/presented slot before native mutation or owner clearing. | P3 owns zero extent, visibility, pause/resume and context generation; no fabricated 1x1 or frame-age completion. |
| Upload | Vulkan `vulkan_staging_upload_pool.cpp` enlists exact owned reservations in original command custody. Metal `CompletionPageSliceAllocator` resolves exact immutable generation tokens; native callback owns journal plus original page, serialized allocator owner collects. Public serial/epoch writes do not retire Metal storage. WebGPU persistent staging copy refuses `RHI-WEBGPU-STAGING-COPY-PATH-UNAVAILABLE`; its reachable immediate queue-write releases CPU input only after the actual API source-copy call. Original GPU publication still joins original queue/error scopes. | GL bridge CPU source consumption and GPU lifetime are separate. P2/P3 must implement and charge both explicitly. |
| Resource pool / ring / cache | `frame_graph_resource_pool.cpp::recycleCompleted` rejects null/pending fence for texture, buffer and alias group, then requires backend normalization. RenderGraph creation refuses a missing completion fence. `frame_graph_uniform_ring_buffer.cpp::onFrameComplete` rejects unauthenticated caller completion. Pipeline/descriptor cache onFrameComplete only advances cache age; eviction removes facade ownership, not original command custody. Backend descriptor liveness/retention still guards native mutation. Scene2D requires real timeline capability and observes its submitted values before slot overwrite/atlas release. | Refused alias/ring opt-ins are not extra P0 implementation prerequisites. P4 must supply authenticated ordered reuse separately, with no readback/alias/completed-publication authority. |
| Readback / publication | Metal `metal_native_commands.mm::readbackCompletionStatus` checks the original command and submit-wait status; `metal_texture_readback.cpp::poll` reads bytes only for Completed, emits Failed/Abandoned otherwise. Vulkan uses original command fence then invalidate/readback. WebGPU `webgpu_readback_results.h` rechecks exact original submission, waits for drained execution scopes, issues native map, verifies mapped range/unmap and map scopes, then consumes the destination publication ticket. Borrowed readback ABI remains refused. | P0 ordered-reuse eligibility cannot publish bytes or signal Completed. Actual WebGL readback needs its own explicit completion contract. |
| Shutdown | Vulkan original runtime drain keeps pending closures on failed/expired/unproven wait. Metal `drainMetalRhiNativeQueueForShutdown` observes the real queue marker and deadline. WebGPU synchronous GPU drain refuses `ARP-RHI-WEBGPU-SHUTDOWN-DRAIN-DEADLINE-UNSUPPORTED`; async accepted custody retains/quarantines its owners. Null shutdown is validator-only. | No synchronous deadline GPU drain is promised; P3-11 implements deterministic cancel/CPU cleanup with the declared refusal. |

`FrameResourceRecycler` (`frontend/frame_resources.*`) stores accounting records, not native
resources. Its only non-test consumer in src is `null_hidden_frame_smoke.cpp`. Its serial
input is validator metadata, not a production retirement issuer. Likewise
`completedVulkanRhiSubmittedFrameSerial` has no remaining src consumer beyond its declaration
and definition. WebGPU's old staging counter is not connected to an accepted persistent copy;
its existence does not make that refused path executable.

Terminal lifetime completion is distinct from successful content. A failed Metal command
may release its exact upload source once native terminal status proves no further access,
but public wait reports failure and readback/publication remain failed. Writable telemetry,
unrelated device/queue, stale generation, frame age and CPU submit return grant no authority.

## Verification and remaining platform gates

The batch covers quick core/exact-journal tests, host-only Metal pool negative tests, actual
Vulkan public-fence pending/reset/rebinding/reuse, original Vulkan resource/staging/WSI
regressions, token-aware Metal/WebGPU source contracts, explicitly host-only WebGPU acquired
surface-mutation negatives, Null hidden frame and original Vulkan hidden/RenderDoc regression.

Apple native `rhi_metal_recording_abandon_tests.mm` retains actual accepted-upload coverage
and adds reverse native submission to prove exact retirement. It is not executed on Windows.
Metal surface/drawable lifecycle, browser WebGPU asynchronous scopes/map/surface lifecycle,
Android/iOS pause/resume, actual target-host loss and native stability remain platform gates.
Their absence cannot be reported as Backend/Platform Validated or Production Ready. They also
do not turn shader ABI work into a dependency on implementing refused alias/ring features.

After P0 contract/audit acceptance the next delivery is all P1-01..11: production shader
target/package/reflection, uniform snapshots/layout, coordinates/precision and real browser
compile/link/numerical/capture evidence. P2/P3/P4 retain their existing implementation gates.
