# WebGlCanvasReuse/1

L2 intent: a caller-budgeted sequence of logical drawing-buffer leases on the
same original context and single graphics queue. Explicit Prototype opt-in and
maxLeases >= 2 are required before the first acquire. One original image,
framebuffer and resource identity remain owned; no native image count is
invented. Generic DeviceCapabilities and production factories remain closed.

| Backend | Lowering |
|---|---|
| Null | Validator-only, no native execution authorization |
| Vulkan | DiagnosticFailure; original completion-based WSI unchanged |
| Metal | DiagnosticFailure; original drawable/completion contract unchanged |
| WebGPU | DiagnosticFailure; original surface/completion contract unchanged |
| WebGL | Explicit Prototype: sequential zero-FBO leases, ordinary GL ordering; no GPU completion |

Enable occurs once, only in Prepared. Recycle requires the exact last frame
serial in Presented or unsubmitted Discarded, no pending state journal, return
of the original unique image wrapper (unless it was never transferred), and
exclusive ownership of the original framebuffer by its canvas. The last check
uses the known shared framebuffer owner: use_count == 1. Any prepared encoder,
pass, finished command or external framebuffer reference prevents recycling.
This is CPU custody, never a driver/GPU reference count or completion proof.
Failure does not consume the returned image or advance state/serial/budget.

Recycle increments the original state revision, resets ColorAttachment and
issues a fresh global logical serial. Old AcquiredFrame values reject. Clear/
Store remains mandatory each frame; prior drawing-buffer pixels are undefined.
Presented recycles after browser handoff, not after GPU completion. Issued GL
reads precede subsequent writes in the same context; pending recorded readers
still prohibit buffer/texture updates through the original retainedReads gates.
All old command owners must be dropped before rearm. Partial issue/loss/domain
failure rejects reuse; it never synthesizes completion or revives a lost epoch.

Storage is constant: one image/framebuffer and scalar budget/serial fields,
all allocated at original cold creation. Acquire, return/recycle and updates
add no owned allocations. Command preparation remains an explicit bounded cold
operation under its original quota; record/finish add no allocations. No shader
or pipeline is recreated per frame. Logical/upload/CPU budgets retain their
original independent owners. Shutdown/cancel keeps existing unsupported drain
diagnostics; no finish, synchronous readback or blocking wait is introduced.

Acceptance: quick five-backend/request negatives and a real browser sequence
with alternating texture data, uniform/vertex updates, conflicting recorded
read rejection, exact image return, stale serial and budget exhaustion tests,
and stable retained CPU/resource counters. ReadPixels belongs only to the test
oracle. RenderDoc must inspect both update outcomes. This batch does not claim
context restoration, resize, mobile lifecycle, all P3 or Production Ready.

Update custody: the original buffer write synchronously copies the exact range
into its bounded CPU shadow and, for native buffers, into glBufferSubData before
return. Texture updates synchronously consume explicitly pitched RGBA8 rows via
glTexSubImage2D and update the original initialized-byte tracking. No source
pointer is retained. Extent/mip/usage/pitch/range checks, original logical storage
limits and independent metadata/upload quotas precede execution. Pending state
journals as well as retained recorded reads prohibit writes before native calls.
A real native upload error invalidates content/domain through the original error
path; rejected updates never change content revision. Reuse does not assert a
new memory allocation, GPU completion, exact physical driver bytes or visibility
to an external queue/context.

Accepted P3-02 plus bounded sequential lease implementation (2026-10-17): root
build input manifest has 7,819 stable named inputs, 12/12 host/Null/backend tests,
4/4 browser tests, 10/10 inventory tests and nine formatted C++ files. Four
RenderDoc captures contain 44 verified draws, including 32 canvas draws with
alternating magenta/cyan upload results; zero replay errors. AI inspected both
canvas PNGs. See build/webgl1-evidence/p3/reuse-native-final-1/checkpoint.yaml.
This is sequential lease validation in a single callback; cross-RAF scheduling,
context restoration and full P3 lifecycle acceptance are still pending.

Superseding cross-RAF acceptance 2026-10-17: P3-03 is now accepted with actual
RAF timestamps strictly increasing, eight original leases before recovery and
eight after rebuilding from the original Slang publication. Stable core retained
bytes/resource counters and allocation-free update/record/recycle gates remain.
Context loss invalidates exact original epochs; see WebGlContextRecovery/1 and
`build/webgl1-evidence/p3/recovery-native-final-3/checkpoint.yaml`. This still
makes no GPU completion, real GPU reset or full lifecycle/production claim.
