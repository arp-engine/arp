# GpuInputSlotLease/1

Status: specified; implementation and validation tracked separately.

A mutable input slot may be overwritten only before any consumer has been
accepted or after all accepted consumers of its current version have completed.
This version supports one submission group per slot: every consumer in that
group is covered by the same completion fence. Repeated registration of that
same fence is idempotent. A different pending group cannot replace its lease;
the caller must defer before recording/submitting work for that slot. Independent
slots remain usable. Multiple unordered submissions sharing one slot require
an explicit joined completion or a future bounded multi-reader contract, not
last-fence-wins. This does not enable multi-queue capability or synthesize joins.

GpuInputSlotLease retains only a shared read-only GpuFence. Retain is bounded and
does not allocate; readiness only polls. Null completion is rejected. Unknown or
unsignaled completion stays pending, including accepted-error cases. The fence
must be the actual completion of the accepted submission group and must never be
reset/host-signaled by the input consumer. Queue acceptance and successful GPU
output remain distinct. This helper does not own input resources: backend native
recorded-resource ownership must preserve storage after wrapper destruction.
Discarding a lease is permitted only with retirement or permanent invalidation
of the corresponding storage; never discard it to overwrite that storage.

Null maps to validator fences for tests; Metal to actual command completion;
Vulkan to native queue fence completion; WebGPU to native asynchronous queue
completion. All share this lease algorithm. No Dummy completion, CPU packet
serial, drawable acquisition or presentation can substitute. Missing native
completion support is a deterministic unsupported execution gate. Native runs
for other backends remain distinct from metadata/contract evidence.

## DDGI consumer

The existing two DDGI update-list/sampling-constant slots each hold one lease;
there is no extra GPU allocation, dynamic lease list or copy-on-write. Preparation
checks the target slot before discarding the previous candidate or writing any
input in that preparation. Busy returns DDGI-INPUT-SLOT-NOT-READY without side
effects. Platform admission queries the same slot first, so a production caller
defers and retries the original packet instead of treating expected busy as fatal.

Prepared input identity copies the scheduler owner/generation and slot, but input
acceptance is independent of history advancement. Pure sampling still leases
the input while update-disabled/culling does not commit scheduler history.
Exact graph tokens are preflight validated. The accepted group may report the
same completion through both input and history observers without double leasing;
later/stale graph execution rejects. History acceptance requires non-null
completion, commits its exact schedule once, and registers that same input lease.

The WOCC graph registers its prepared input lease independently of the update
observer, covering every sampling consumer in that single graph submission. A
conservative external lifetime pass may retain a prepared input even if no draw
ultimately samples it; this does not advance history. Renderer/WOCC never read
native handles. FrameGraphSubmissionObserver/3 carries actual completion.

Scope: update-list and sampling UBO reuse. Shared local-light/BVH storage, atlas
read/write order, wind/skinning/camera inputs, GPU errors/loss and final GI output
remain separately tracked ownership/correctness obligations. Slot safety must
not be described as proving those resources safe.

## Required tests

Keep the original Metal FAIL/control evidence. A real pending consumer must read
A's original origin (-1), while same-slot B preparation reports busy without
changing candidate identity/telemetry; after completion B prepares and reads +1.
Other slots can prepare. Verify null/different pending fence rejection, same-fence
idempotence, exact token validation, pure sampling without history advancement,
and rejection/accepted-error propagation. Destruction and recreation may not
reuse an old storage/fence identity. Production deferred-then-ready and two-build
app tests are separate gates from this minimal native input test.
