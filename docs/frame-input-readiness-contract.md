# FrameInputReadiness/1

Status: specified; implementation and validation tracked separately.

## L2 semantics and layer ownership

AcquiredFrame may report NotReady with success=false, recoverable=true and no
swapchainRebuildRequired/surfaceLost. This is temporary input unavailability,
not a presented/submitted frame, fatal error, or evidence of GPU completion.
RenderSystem owns readiness and an acquired-but-not-yet-admitted frame. Renderer
reports whether its declared mutable inputs for that logical frame slot may be
prepared. A pipeline with no such requirements reports ready. Queries are
nonmutating, nonblocking, and must precede input writes or packet replay effects.

Native acquisition may identify a slot before its renderer input is writable.
RenderSystem retains that exact acquired frame while NotReady. Subsequent polls
reuse its identity without acquiring another drawable or changing frame serial.
Once ready, ownership moves to the normal current-frame path exactly once.
Clients cannot render or present a NotReady result. Repeated acquire while an
admitted current frame exists is rejected. Cancellation/resize/pipeline change
discards a pending acquired frame through its owning Swapchain before replacing
the dependency. Destruction also discards pending acquisition, retaining native
resource ownership below RHI. Discard failure is diagnosed, never presented.

Retained acquisitions are revalidated without image wrapping before readiness,
following AcquiredFrameValidity/1. External lifecycle invalidation returns one
explicit NotReady result and forgets the obsolete CPU reference; the following
attempt performs normal acquisition. Already-invalidated cancel is distinguished
from a successful native discard and from GPU retirement.

This contract does not infer that native drawable acquisition retires arbitrary
renderer input, nor does it infer GPU completion from CPU packet completion.
Input owners use their actual submission completion tracking. In-flight input
versions, multiple consumers, shared resources and bounded storage need their
own explicit ownership contract. This gate alone does not protect writes that
occur before acquire or outside declared inputs.

## Four-backend lowering

| Backend | Acquired identity retained by platform | Readiness |
| --- | --- | --- |
| Null | Validator slot and serial | Explicit pipeline requirement; test semantics only |
| Vulkan | Existing native swapchain acquisition result | Actual input completion, not image acquisition alone |
| Metal | Existing drawable/slot acquisition result | Actual input completion, not present or logical slot clear |
| WebGPU | Existing surface acquisition result | Actual asynchronous input completion, never host fake signal |

No native API default or capability is changed. NotReady is platform admission,
so all four backends consume the same logic and retain their existing native
acquire/discard paths. A native acquire failure is propagated unchanged. Missing
required completion support remains a deterministic capability/diagnostic error;
it cannot become permanently pending or ready-by-default.

## Required verification

Verify pending identity is stable, repeated polls do not acquire more slots,
ready hands off once, another available slot can proceed after handoff, pending
cancel releases the exact slot, native acquisition failure passes through,
and NotReady never requires swapchain recreation. Platform teardown, resize and
pipeline replacement must cancel pending ownership. Retained input's real Metal
completion and WOCC deferred-then-ready packet behavior require separate native
integration; Null state-machine tests do not prove native readiness.
