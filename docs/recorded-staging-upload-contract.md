# RecordedStagingUpload/1

Status: Contract Complete for recorded staging ownership and queue-acceptance
semantics. Metal Backend Validated for partial recording abandonment, exact slice
rollback, open-pass abandonment and recovery submission on macOS. WebGPU common
and pool lowering compile/tests pass; native WebGPU validation remains open.
Vulkan lowering and the equivalent maintained pool test are implemented, but the
current Apple build graph has no Vulkan target, so Vulkan compile/native status is
NOT-RUN. No Platform Validated or Production Ready claim follows from this scope.

Evidence: WOCC
`build/evidence/macos/20260918-metal-recording-abandon/p3-p4-result.md`.

## L2 semantic boundary

A backend-owned staging slice allocated while recording a command is provisional
until the native queue accepts the finished command buffer. The allocation is
not a completed upload, an accepted command, or an in-flight GPU resource merely
because CPU bytes were copied or a native copy command was encoded.

The ownership sequence is:

| Event | Owner and required action |
| --- | --- |
| staging allocation succeeds | The command encoder owns one exact provisional slice. |
| native record or flush fails | Cancel the slice for that failed command immediately and preserve the first native diagnostic. |
| encoder is abandoned or poisoned | Cancel all remaining provisional slices in reverse allocation order. |
| `finish()` succeeds | Transfer the journal once from encoder to command buffer; encoder destruction cannot cancel it. |
| submit is rejected before native acceptance | Keep command-buffer ownership so a legal retry remains possible; destruction still cancels it. |
| queue accepts the command buffer | Commit the journal exactly once at the same boundary as recorded resource state. Later reporting, fence binding or callback registration failure is an accepted error and cannot roll staging back. |
| native completion is observed | The existing backend completion serial retires committed slices. Completion is not synthesized by journal commit. |

An exact slice identity is `(pageIndex, offsetBytes, allocationSizeBytes,
retirementSerial)`. Logical payload bytes may differ from allocation bytes because
of backend alignment. The journal always records allocation bytes. It carries no
native handle, mapped pointer, resource label or backend enum.

Exact cancellation may remove an older provisional slice while newer slices on
the page remain live. The resulting hole is not reusable. Only the contiguous
tail after the highest remaining live slice is reclaimable. This preserves the
bounded linear allocator and prevents overlap with independently recorded or
accepted work.

Rollback and commit run without allocation, blocking, logging or IO. The journal
uses the core CPU allocator for bounded recording-time storage. No wait, capacity
increase, dummy resource, CPU replay or silent fallback is part of this contract.

## Relationship to recorded resource state

`RecordedResourceState/1` and `RecordedStagingUpload/1` share one queue-acceptance
boundary. A finished command buffer owns both journals. Acceptance commits both;
abandonment rolls both back. A backend must not publish resource state while
canceling staging, or commit staging while restoring resource state.

The two journals remain separate implementation objects because resource-state
rollback is intrusive metadata restoration while staging rollback cancels exact
allocator reservations. Neither mechanism is exposed through public L2 command
descriptors.

## Four-backend lowering

| Backend | Lowering conclusion |
| --- | --- |
| Null | Equivalent validator. It exercises the same record/finish/reject/accept state machine and recorded resource-state ownership, but has no physical staging slice and makes no native allocation claim. |
| Vulkan | Native. Persistent mapped staging allocations enter the exact-slice journal. Record/flush failure cancels the latest slice; queue acceptance commits it; Vulkan completion serial retires it. |
| Metal | Native. Persistent shared/managed staging allocations use the exact-slice journal. In addition, render/compute/blit encoder factories establish a local autorelease boundary and retain backend ownership before that pool drains, so abandon releases native command ownership deterministically. |
| WebGPU persistent staging | Native/Equivalent according to the existing runtime profile. Aligned allocation bytes enter the exact-slice journal; accepted queue completion serial retires them. |
| WebGPU queue-write | Native immediate path. The bounded temporary reservation is released after the queue API returns and never enters the command journal because no persistent GPU staging slice survives the call. |

Unsupported or foreign payloads fail with their existing deterministic diagnostic.
No backend infers support from its name, commits on command-buffer creation, or
retires an entire submission serial merely because an encoder is destroyed.

## Required evidence

Fast tests must cover exact cancellation of an older slice with a newer live
slice, non-reuse of the hole, reverse rollback, `rollbackLast()`, commit retaining
live slices, finish transfer, unaccepted command-buffer destruction, rejected
submit followed by accepted retry, accepted-error retention, Null's no-physical-
slice conclusion, and the WebGPU queue-write/persistent split.

Backend tests must cover Metal's real partial transfer recording, deterministic
staging exhaustion, more than 64 abandon cycles, full logical command capacity,
zero provisional live slices, and successful recovery submission. Existing
submit/wait, staging pool, command contract and RenderGraph submission tests must
remain green in Debug and Release. Native backend results are reported separately;
Metal success does not imply native Vulkan or WebGPU validation.
