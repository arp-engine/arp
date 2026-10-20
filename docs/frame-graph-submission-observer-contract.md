# FrameGraphSubmissionObserver/3

Status: contract specified; implementation and validation tracked separately.

Version 3 adds a non-null, shared, read-only `GpuFence` completion reference to
each acceptance notification. It is the exact fence prepared before this queue
submission, not a new signal, a frame index, presentation state or a later graph's
fence. All notifications for one accepted submission receive the same identity.
Observers may copy its existing ownership without allocating. The graph never
resets or host-signals an exposed fence. Observers can retain and poll it after
graph reset/destruction, but cannot signal, reset or wait through this const API.
The device and its backend completion infrastructure must outlive retained work.

The reference does not itself retain every input resource and does not imply
successful GPU output. Input owners must retain their storage until the relevant
work retires; several unordered consumers require all their completions, not
blind replacement by the latest fence. Resource leases and platform readiness
are separate consumers of this primitive. No readiness or backend capability is
added by this change.

Lowering uses the existing fence of the same submission: Null validator
completion (test-only semantic evidence), Metal native command completion,
Vulkan native submission fence, and WebGPU native queue-completion registration.
Rejected work sends no notification. Accepted-error sends its original fence
even when completion registration subsequently fails; an unsignaled/unknown
fence must never be interpreted as complete. Native failure/recovery remains a
separate contract and validation gate. Native handles remain backend-private.

## L2 boundary

Queue acceptance is distinct from GPU completion and presentation. A successful
QueueSubmitResult implies acceptance. If an error is discovered after acceptance,
the result retains that fact in `workAccepted`; `hasAcceptedWork()` is true for
either case. Rejected work has neither success nor workAccepted. Completion
failure must not retroactively describe accepted work as unsubmitted.

## Four-backend lowering

| Backend | Accepted work | Failure boundary |
| --- | --- | --- |
| Null | Validator queue accepts recorded commands; no native execution claim | Fixture rejection has no accepted work; fixtures may model accepted failure explicitly |
| Metal | Finished native command payload is committed to its command queue | Pre-commit validation rejects; asynchronous GPU status belongs to completion |
| Vulkan | Native queue submit succeeds | Later fence binding failure retains acceptance and diagnostics; no emulated success |
| WebGPU | Native queue submit accepts the command payload | Later completion registration failure retains acceptance; absent native payload rejects |

No backend capability is enabled by this contract. It consumes the existing
queue contract and leaves native handles and completion ownership backend-private.
Native Vulkan/WebGPU runtime validation is a separate gate, not implied by Metal.

## Renderer consumption

Version 2 adds a copied `PassSubmissionToken { ownerId, generation }` to each
registration. It contains only CPU domain identity, never a native handle or
borrowed schedule list. Empty tokens are explicitly for stateless notifications;
the default validator rejects nonempty tokens unless the observer implements
their domain validation. Both validation and acceptance receive the registered
value, not a mutable "latest token" obtained from the observer.

Before allocating/recording graph work, and again immediately before queue
submission, the graph validates every executable registration. A non-null stable
diagnostic rejects the attempt without queue acceptance or notifications. The
second check catches a candidate invalidated during recording. Validators are
noexcept, allocation-free, nonmutating and return static diagnostic strings.
Execution and domain preparation are externally serialized; no concurrent
mutation, graph rebinding or reentry is allowed between validation and acceptance.

Cached topology can be reused by explicitly rebinding the terminal PassBuilder
between executions. Registration copies the new token into that graph only;
another old graph remains bound to its old token and must fail preflight.
Once-only domain tokens reject duplicate execution; stateless empty-token
observers may still observe each repeated execution. A domain registers its
one-shot token on one terminal pass only, never multiple passes of the same graph.
Duplicate executable registrations of the same nonempty observer/token reject
with `RG-SUBMISSION-TOKEN-DUPLICATE` before recording. Validation uses bounded
pass-list scans and no additional per-execution allocation.

This validation is shared across Null/Vulkan/Metal/WebGPU, before each backend's
native queue acceptance described above. It adds no capability or native default.
An accepted-error notification still receives its original token and retains
in-flight ownership. Culled registrations are neither validated nor notified.

A pass may reference one non-owning PassSubmissionObserver. It is called once
for that pass per accepted graph execution, in executable pass order, after all
recording/finish/fence creation and queue acceptance. Culled passes are not called;
registration does not prevent culling. Failed record/finish/fence creation or
queue rejection triggers no observer. A subsequent accepted execution triggers
one new notification, including an explicitly repeated compiled graph.

Observers must outlive all executions referencing them. onSubmitted is noexcept,
bounded, non-blocking, allocation-free, and may only commit already prepared CPU
domain state. It must not mutate/reset/reenter the graph, destroy registered
observers, submit GPU work, or infer GPU completion. Reset/destruction never
notifies. Distinct registrations for the same observer remain distinct effects;
consumers needing one commit register it on only the terminal pass of their chain.

The graph prepares fence shared ownership through the core allocator BEFORE
submission. On acceptance, it retains that fence and publishes acceptance before
any optional reporting or throwing diagnostics. Pass notifications precede the
accepted-error throw. Completion/retirement remains fence-gated; acceptance never
signals a fence. An accepted error must fail the caller and invalidate affected
domain histories before recovery, while retaining in-flight resource ownership.

lastExecutionSubmitted describes the latest execute attempt, clears at the start
of execute (including its compilation) and reset, and remains true if reporting subsequently fails. It does
not replace resource lifetime fencing or identify GPU completion.

## Required validation

Record failure, native-encoder finish failure, fence creation failure, queue
rejection, accepted error, normal acceptance, culling, reset and repeated execution
need independent observations. Notifications must see accepted work and unchanged
history throughout recording. At least one real Metal nonempty submission must
verify output as well as notification; Null counts alone prove only the contract.
WOCC integration additionally verifies DDGI/TAA copy-then-fail and next-frame
content; other temporal histories, device loss, presentation and bounded shutdown
remain separate required cases.

Version 2 additionally requires stale/foreign/zero/consumed domain tokens,
two graphs sharing an observer, invalidation during recording, explicit cached
graph rebinding and culling. Stale preflight must record no pass and submit no
work. Acceptance must consume exactly the identity registered by that graph.

Version 3 additionally requires non-null exact completion delivery on success
and accepted-error, no delivery on rejection, distinct retained fences for
distinct submissions, and retained completion after graph reset/destruction.
A real Metal nonempty graph held behind a native timeline wait must notify while
the delivered fence is pending, then become complete only after release; verify
the graph's readback as well. Null immediate completion cannot prove this case.
