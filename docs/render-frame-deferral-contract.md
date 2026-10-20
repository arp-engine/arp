# RenderFrameDeferral/1

Status: contract specified; implementation and evidence tracked separately.

## Domain and ownership

A render packet has three consumption outcomes: Completed, Deferred, Stop.
Completed means the CPU consumer finished processing the packet, not successful
GPU completion or presentation. Stop preserves the existing fatal-consumption
boundary: release this packet, publish its consumption serial, request stop.
Deferred means no irreversible packet consumption occurred; retain the same
Reading slot and immutable packet, do not publish its completion serial, and
retry it before later packets. A consumer must return Deferred before packet
uploads or other replay side effects. Retrying must not advance simulation,
replace input with a later tick, or count a submitted/presented frame.

The reusable core render-thread scheduler owns rescheduling and packet lifetime.
It has no RHI dependency and cannot infer readiness from backend or frame parity.
The consumer queries the platform's nonblocking readiness on each attempt.
RenderSystem owns acquired-frame readiness/lifecycle; renderer describes input
requirements. Native completion stays below RHI. Consumers must not wait on GPU
fences or spin until completion inside consumeFrame.

## Rescheduling and stop

Between deferred attempts the scheduler performs an interruptible condition-
variable timed park, not a GPU wait. The default scheduler retry cadence is
1 millisecond, a declared scheduling policy rather than proof of readiness;
callers may supply a positive cadence at construction. Work is always retried
against actual readiness, never assumed complete after elapsed time. This adds
no packet allocation or unbounded queue. Existing two-slot back-pressure remains.

requestStop wakes the park. If the current packet is still Deferred, release its
CPU slot and exit without signaling that packet as consumed. Existing producer
waits wake through stopRequested, not fake completion. A stop arriving during a
consumer invocation is observed at its next Deferred result. Already Completed
packets retain normal serial publication; normal ready work retains the existing
stop/drain behavior. This does not guarantee bounded shutdown of a consumer that
never returns, GPU drain, or driver loss; those need separate deadline contracts.

## Backend mapping

The scheduler behavior is identical for Null, Vulkan, Metal and WebGPU consumers:
no native API, CPU signal, fallback or capability is introduced. Readiness comes
from the platform's declared input-completion contract, never CPU packet serial.
Null can test deferred/ready state transitions only. Native proof requires a
real consumer held pending and then resumed. Browser single-thread consumers
must retain a deferred packet across event-loop invocations instead of parking
the browser main thread; that adapter is a separate required integration gate.

## Required validation

Preserve existing lifecycle, FIFO, completed-serial, fatal and stop/drain tests.
Add deferred-then-completed with the same packet address/value, no premature
completion or slot release, later ready packet not overtaking, stop while
permanently deferred, cancellation without a fabricated serial, and invalid
retry cadence. Release builds must execute assertions. GPU/application tests
must separately prove input stability, no duplicate replay and forward progress;
core scheduler tests alone do not validate platform integration.
