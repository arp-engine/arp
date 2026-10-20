# AcquiredFrameValidity/1

Status: specified; implementation and validation tracked separately.

## L2 intent and invariants

`Swapchain::isFrameAcquired(frame) const noexcept` queries whether a successful
acquisition issued by this same, still-live Swapchain remains acquired for its
exact slot and serial. Callers retain the issuing owner and serialize this query
with acquire/present/discard/lifecycle operations. It performs no allocation,
native acquire, image wrapping, wait, retirement, or mutation. It is not a GPU
completion query, device-health probe, or permission for concurrent calls.
Cross-owner or forged frames are outside the query's ownership precondition.

Successful present/discard or lifecycle invalidation removes acquisition authority.
A presented-but-not-GPU-retired slot is no longer acquired. Reusing a slot with a
different serial never revives its previous frame. Rejected descriptor/extent
validation without acquisition-state mutation leaves existing frames acquired.
Backend lifecycle error paths report actual remaining slot authority; native
failure reporting and storage retirement remain their own obligations.

This is a required Swapchain operation, with no permissive default. It exposes
only L2 identity state, not native handles or backend selection.

## Four-backend lowering

| Backend | Exact acquisition authority | Conclusion |
| --- | --- | --- |
| Null | success and fixed slot in-flight/serial match | Validator semantics only |
| Metal | success and drawable slot in-flight/serial match | Native-backed bookkeeping; no new native call |
| Vulkan | success, in-flight/serial match, not already presented | Native-backed or explicitly synthetic bookkeeping; evidence separate |
| WebGPU | success and canvas slot in-flight/serial match | Native-backed or metadata bookkeeping; evidence separate |

All mappings use existing backend-owned acquisition state. No capability is
promoted; a metadata swapchain remains metadata-only. Query results never prove
native work executed or retired. Native lifecycle events continue to obey their
declared resource ownership, failure and retirement contracts.

## Platform admission

FrameInputAdmission revalidates its retained acquisition before consulting input
readiness on every attempt. When invalidated, it clears only its obsolete CPU
reference and returns NotReady with the stable diagnostic
`PLATFORM-FRAME-INPUT-ACQUISITION-INVALIDATED`, without claiming submission,
presentation or GPU completion. It does not acquire a new frame in that attempt.
The next attempt follows normal acquire and reports its actual status. NotReady
does not request a redundant swapchain rebuild after completed recovery.

Cancellation of a reference already invalidated by its issuing Swapchain returns
success with the distinct FrameDiscardStatus::AlreadyInvalidated. This reports
that no acquisition remains to cancel, not a successful native discard or GPU
retirement. Real discard failure propagates unchanged; retain the reference until
a subsequent query proves invalidation or discard succeeds. No-pending cancel
remains idempotent.

## Verification

Keep the original Null failure: surface loss/recovery while pending delivered a
successful old serial although acquiredImage rejected it. Cover loss, resize,
reconfigure, cancellation, rejected extent, slot reuse and repeated queries.
Real discard failure cannot be rewritten as AlreadyInvalidated success. Shared
fixtures run on available backends, with Metal native window evidence and
Vulkan/WebGPU synthetic/native coverage explicitly separated. Readiness callbacks
must not run for invalidated frames. Full WOCC owner-thread coordination and
forced deferral remain separate gates.
