# DdgiScheduleSubmission/1

Status: contract specified; scheduler candidate/commit, upload failure, exact
graph receipt binding and normal Metal/lifecycle paths have scoped verification.
Real rejected-frame retry fails in both Null and Metal because abandoned recording
leaves resource states changed; full backend validation is not claimed. Evidence:
WOCC build/evidence/macos/20260917-ddgi-scheduler-5jNKGt/result.md,
20260917-ddgi-receipt-TGR8Qu/result.md and 20260918-ddgi-retry-kwNjUm/result.md.

The DDGI scheduler's accepted state describes the probe history selected for
subsequent frames. Merely preparing a probe list, uploading it, or recording
commands must not mark any probe updated. A rejected frame must leave accepted
cell identities, pending-new-cell flags, update ages and lighting/geometry
revisions unchanged.

## Preparation and acceptance

Preparation computes a bounded candidate from the last accepted state. The
returned list has a nonzero, non-recycled generation belonging to that scheduler.
Preparing again replaces the unaccepted candidate, including its cell remapping;
the caller must not retain the borrowed list across another preparation.
The same input prepared twice without acceptance produces the same list and
statistics, even though the candidate generations differ.

Commit accepts only the currently prepared generation and succeeds once. Stale,
zero and already consumed generations are rejected without changing accepted
state. Discard also targets a generation; a stale discard cannot erase a newer
candidate. Identity exhaustion fails closed and never recycles zero or an old
generation. A scheduler is externally serialized, not a concurrent transaction
service. Tokens cannot be transferred between scheduler instances.

State, candidate-state, ranking and output-list capacity are allocated through
core/PMR during construction, bounded by the configured probe count. Preparing,
committing, discarding and changing an update budget within that count allocate
no memory. Commit swaps preallocated state storage; it does no logging, IO,
blocking, or exception-producing work.

## Renderer ownership

The renderer keeps the candidate generation with the prepared frame. Upload
failure does not make that generation eligible for commit. The complete update
chain that becomes the next persistent history commits its scheduler candidate
at the same declared queue-acceptance boundary as the history slot. The caller
must associate the exact prepared generation with that chain, rather than
inferring it from frame parity or from a later RendererResult.success.

No commit occurs on recording/finish/fence failure, rejected submission,
update-disabled work, or a culled update chain. Acceptance followed by telemetry
failure remains acceptance: in-flight storage cannot be treated as unsubmitted.
Completion, device loss and history invalidation remain separate facts; accepted
work is not proof of successful GPU completion. Scope here is scheduler state;
scrolling, light-origin metadata and other temporal histories require their own
consistent transaction boundaries.

CPU simulation and independently accepted upload/BVH work must not be rolled
back as a side effect of discarding a scheduler candidate. Recreating a renderer
invalidates its previous scheduling domain and any outstanding generation.

## Four-backend lowering and evidence

| Backend | Commit boundary | Evidence required |
| --- | --- | --- |
| Null | Explicit validator queue acceptance | State/identity/allocation tests; never native output evidence |
| Vulkan | Native queue acceptance preserved by QueueSubmitResult | Shared contract and explicit accepted-error behavior; native runs tracked separately |
| Metal | Native queue acceptance preserved by QueueSubmitResult | Nonempty work, rejected frame retry, pending/completion distinction and app smoke |
| WebGPU | Native queue acceptance preserved by QueueSubmitResult | Shared contract and explicit accepted-error behavior; native runs tracked separately |

The implementation must reuse FrameGraphSubmissionObserver/2 where the graph
owns submission. No backend branches or native handles enter the renderer or
WOCC. Unsupported execution remains a deterministic failure, not a fabricated
scheduler commit or successful completion.

The renderer exposes a value-only prepared-history token. Validating and
accepting history require that exact nonzero owner/generation; no no-argument
"accept latest" entry point is permitted. Each graph registration copies its
token, validates it before recording and before submission, and presents it at
acceptance. Replacing a candidate makes earlier graphs stale without rebinding
them. Cached graph rebinding is explicit and happens only between executions.

Minimum tests: prepare twice without submission; discard/retry; commit exactly
once; stale/zero generation; budget zero and bounded changes; cell scroll and
lighting/geometry revision rejected then retried; no hot-path allocations;
existing fairness/starvation cases with explicit acceptance; real Metal history
recorded then rejected, followed by a successful frame consuming correct data.
