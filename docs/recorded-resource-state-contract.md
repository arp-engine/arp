# RecordedResourceState/1

Status: implemented for serialized primary recording and ordered queue segments.
Backend Validated: Metal, scoped resource abandonment/acceptance and named DDGI
retry cases. Null validation, Vulkan compiled synthetic/ASan/UBSan checks and
WebGPU metadata contracts passed. Native Vulkan/WebGPU verification remains open;
no full Platform Validated or Production Ready claim.

Evidence: WOCC `build/evidence/macos/20260918-ddgi-retry-kwNjUm/closeout-VnaRk5/`;
Debug/Release each 36/36 selected tests and eight app cases, API Validation enabled.
This does not cover in-flight mutable input reuse, access-only dependencies,
complete physical budgeting, spatial/visual correctness or device-loss recovery.

Resource-state transitions recorded into an unaccepted primary command buffer
are provisional. On the serialized recording thread, state()/subresourceState()
continue to expose the current recording's state for subsequent barriers and
validation. Abandoning the encoder, or the finished but unaccepted command buffer,
restores its enlisted resources to their states before that recording. This is
CPU metadata restoration, never native GPU execution or a compensating barrier.
Resource owners must serialize uses of a resource until acceptance or abandonment;
this contract does not authorize access-only work to depend on an unsubmitted
recording. Conflicting state mutation by another recorder fails deterministically
with RHI-RESOURCE-STATE-RECORDING-CONFLICT before encoding its barrier.

Finish transfers provisional-state ownership from encoder to command buffer.
The encoder's destruction after finish cannot roll it back. Queue acceptance
commits the state exactly at the existing acceptance boundary, before optional
reporting/fence binding/completion registration. Accepted errors remain accepted.
Command-buffer destruction, present failure, pending completion or later GPU error
never restores an older CPU state over accepted work. GPU failure/loss invalidation
and in-flight resource retirement remain separate contracts.

Rejected submit retains unaccepted command-buffer ownership until it is abandoned
or validly retried. Unrelated, independently accepted uploads/BVH work are not part
of this transaction. Destroying a resource detaches its metadata record safely;
abandonment cannot dereference a destroyed CPU wrapper. This does not relax native
resource lifetime or descriptor validation.

## Storage and implementation boundary

Reuse a core intrusive mutation journal: records live in the objects whose values
they protect. There is no growing per-command undo vector. One core-owned journal
is created with an encoder and moved to its command buffer. Scalar/array snapshots
are fixed-size. Texture range metadata keeps one reusable backup bounded by the
existing metadata's high-water size, using core/PMR; full T4 physical accounting
must include that extra retained capacity. Snapshot preparation precedes native
barrier emission; commit/abandon perform no allocation, blocking, logging or IO.

An object enlists once per journal; subsequent changes retain the original
snapshot. Conflicting ownership cannot be stolen. Rollback is reverse enlistment
order; acceptance discards snapshots without modifying the current values. The
journal and snapshot addresses are stable and noncopyable. Restore callbacks may
only restore their own saved value, never reenter or alter journal membership.

All machinery stays in core and backend-private/common RHI implementation. No
native handle or backend branch enters renderer/game. Strict state, subresource,
no-op and resource liveness validators remain unchanged.

## Four-backend lowering

| Backend | Record/abandon | Commit |
| --- | --- | --- |
| Null | Snapshot validator scalar and subresource records; abandon restores them | Real Null queue acceptance; no native execution claim |
| Vulkan | Snapshot logical state before native barrier recording; abandoned VkCommandBuffer has executed no layout change | Successful native queue submission, before fence binding/reporting |
| Metal | Snapshot before native barrier emission; abandon discards metadata and native recording | Successful native command-buffer commit |
| WebGPU | Snapshot before native barrier recording; abandon restores logical metadata | Accepted queue submission, before completion callback registration |

Shared behavior is implemented in all four paths; native Vulkan/WebGPU runs remain
separate required evidence. Unsupported/foreign payloads reject; no dummy state
commit, CPU signal or relaxed validator substitutes for queue acceptance.

Required tests: buffer/texture/subrange abandon; multiple changes restore the
first state; finished-unsubmitted ownership; accepted state survives destruction;
record/native/fence/submit failure; accepted-report error; conflicting recorders;
resource destruction before journal; no allocation during commit/rollback; real
DDGI A accepted -> B recorded/rejected -> same-input C accepted, with native
all-channel content compared against a clean sequence. Existing four-backend
contract and WOCC Debug/Release smoke must pass before a scoped closure.

## Ordered queue segments

The active RHI frame-plan executor records, finishes and submits each ordered
queue segment before recording its dependent successor. Timeline capability and
the semaphore, segment storage and encoder objects are prepared before the first
submission. Queue acceptance releases provisional metadata ownership; the real
timeline signal/wait establishes GPU visibility for the successor. This requires
no CPU completion wait and does not authorize independent concurrent recordings
to observe provisional state. Four-backend lowering uses the same L2 sequence:
Null validates it, native queues execute their supported timeline lowering, and
an unavailable queue/timeline follows the existing explicit capability decision.

A multi-submit frame is not atomic. Later record/finish/submit failure retains
the accepted prefix and abandons only the unaccepted suffix. Reports count
accepted commands even when acceptance is followed by a reporting error, stop
before recording another segment, and never label the partial frame successful.
Whole-frame retry must replan from accepted states; replaying the original initial
barrier plan after partial acceptance is invalid. This does not change the single
command-buffer FrameGraph submission observer contract.

Tests cover C-G, G-C-G and longer handoffs, as well as a rejected or accepted-error
graphics successor: counts and resource states must match the accepted prefix.
Recording every dependent segment under independent unaccepted journals is not
valid; conflict validation must not be weakened to support that old workflow.

Native submit adapters must preserve acceptance for the current attempt if
post-submit retirement/reporting fails. A second submission of an already
submitted payload is a rejected attempt, not a new accepted effect.
