# DdgiSharedInput/1

Status: specified; implementation and native evidence are tracked separately.
Initial scope is shared local-light records and per-probe influence indices.

These two physical buffers are shared by both prepared DDGI input slots. A free
slot is not evidence that shared storage can be overwritten. Every accepted
reader must be covered by the slot's actual submission-group completion under
GpuInputSlotLease/1. Before changing shared bytes, poll every existing slot lease;
all must be complete. There is no new fence, wait, allocation, unbounded reader
list, CPU completion substitution or last-fence-wins rule. Same-group duplicate
registration remains idempotent; the existing per-slot group bound still applies.

setLocalLights stages a validated, bounded CPU source revision. It does not
publish a GPU version. Preparation preflights whether shared data needs upload
before discarding its existing candidate or advancing telemetry. If an upload
would overlap an accepted reader, return DDGI-SHARED-LIGHT-INPUT-NOT-READY without
writing either buffer or changing the prepared receipt/history. A no-change
preparation on an independent free slot may proceed. With zero local lights,
origin/ring changes do not alter the all-invalid influence bytes and need no
shared upload. A changed light revision still requires upload, including removal.

The upload phase repeats validation on the same serialized owner. Before the
first write, invalidate both the prepared receipt and the previous shared cache
validity. A failing upload may have changed physical bytes; neither the new key
nor the previous key can authorize reuse after that failure. A later retry,
including one returning to the previous grid origin/ring, rewrites both inputs
before publishing cache validity and creating a new receipt. Busy preflight does
not invalidate either. All four backends use this same publication rule; failure
does not imply native write rollback.

WOCC's frame-admission callback conservatively requires all shared input readers
to finish before entering its mutable DDGI preparation. RenderSystem owns the
retained acquisition and nonblocking retry; the game never observes native
handles or waits on GPU work. This policy adds no GPU storage and may reduce
CPU/GPU overlap; performance remains a separate measured gate, not an inferred
equivalence to the previous unsafe scheduling.

Four-backend lowering reuses the unchanged completion contract: Null validator
fences for contract fixtures; Metal command-completion fences; Vulkan native queue
fences; WebGPU asynchronous queue completion. Each uses the same bounded lease
poll and busy diagnostic, with no backend or OS branch in renderer/game. Native
completion capability and execution gates remain authoritative; missing native
completion cannot be replaced by an immediately signaled fixture. Null, source
compilation and metadata evidence do not sign native Vulkan/WebGPU behavior.

Required evidence: preserve the real Metal FAIL/control; A pending while B changes
lights in another slot must leave A's exact bytes intact, reject before receipt
mutation, then allow B and read its new bytes after actual completion. Cover two
readers completing in both orders, no-change independent slots, removing lights,
upload failures/cache publication, invalid inputs and bounded storage retirement.
Full WOCC forced GPU deferral is a separate integration gate. Shared sky/CSM, BVH,
atlas/descriptor lifetime and meaningful final GI output remain separate scopes;
this contract does not silently certify their direct writer APIs.
