# QueueSubmitStartOrder/1

This records the existing `GpuQueue::submit` L2 requirement: a timeline wait
blocks execution start of all submitted work, not merely its completion signal.
No new public synchronization capability or native handle is introduced.

An unsatisfied wait must prevent transfer, graphics, compute and observable
event side effects of the protected submission. A satisfied wait permits work;
signals/fence completion follow that work. Missing/foreign native objects and
duplicate submission reject before new GPU work. Host polling/fake completion
cannot implement a queue-side wait. Recording and submission remain externally
serialized as required by existing command ownership; no concurrent queue API
guarantee is introduced by this contract.

| Backend | Lowering / current evidence boundary |
| --- | --- |
| Null | Logical validator only; never evidence of native execution ordering |
| Vulkan | Native timeline wait in VkSubmitInfo, ALL_COMMANDS destination stage; native runtime proof still required |
| Metal | One backend-private wait-only MTLCommandBuffer is committed before the recorded payload on the same serial MTLCommandQueue. No CPU wait, native handle exposure or upper-layer workaround. Signal/fence remain on the protected payload; both native statuses participate in success |
| WebGPU | Core has no timeline queue-side wait/signal; reject both before native submission, never emulate with a completion callback |

Native verification must distinguish start from completion. A backend-private
fixture records a real transfer followed by an observable GPU event, submits
the nonempty payload against an unsatisfied gate, and checks whether that event
fires before releasing the gate. The observer is an explicit test instrument,
not a game/renderer native access path. Event synchronization avoids unsynchronized
CPU/GPU buffer reads. Every test releases its gate before checking failures and
uses a bounded completion deadline. A pending fence alone is insufficient.

Any corrective lowering must additionally document fixed auxiliary payload
count, ownership, creation failure, pre-commit rollback, ordering and retirement.
Keep native and synthetic evidence separate. Required work may not be dropped
or marked successful by disabling NativeSync. General queue/physical budgets
remain subject to the existing bounded allocation contract and T4 validation.

Metal allocates at most one auxiliary command buffer per submitted payload with
a wait, none without a wait. The native queue is the recorded command buffer's
own queue; Apple's MTLCommandQueue header defines it as a serial queue of command
buffers. Both buffers are validated/encoded before either is committed. Missing
auxiliary allocation fails with `RHI-METAL-SUBMIT-WAIT-COMMAND-BUFFER-CREATE-FAILED`;
there is no native commit or successful signal on that path. The payload and its
completion closure retain the auxiliary buffer until completion/discard, with
no growing list of retired wait objects. Native buffer errors are observable as
failed readback/query completion. Overall queue capacity and physical accounting
are separate T4 gates; the constant auxiliary count does not prove those gates.

## CommandCapacityReservation/1

L2 command creation/submission must reject exhausted native capacity before a
native call that can block for a free command-buffer slot. Reservation covers
all payloads required by lowering; releasing a public wrapper cannot free an
in-flight reservation. Failure before commit and discard return reservations;
successful submission returns them after native completion, even if wrappers
remain alive. This count budget is separate from physical/CPU memory bytes.

Metal's default queue has a documented capacity of 64 non-completed command
buffers (`MTLDevice.h`, `newCommandQueue`). The backend makes that existing
capacity explicit using `newCommandQueueWithMaxCommandBufferCount:64`, rather
than increasing it to hide exhaustion. A primary recording reserves two slots
before native allocation (work plus a possible wait prologue); present and
teardown drain reserve one. All users of the shared queue participate. No wait
means no native prologue allocation, but the reserved second slot is retained
until completion/discard. At most 32 simultaneous primary recordings fit with
no other reservations; this is a documented lowering capacity, not a physical
memory measurement. Exhaustion emits `RHI-METAL-COMMAND-CAPACITY-EXHAUSTED` and
does not enter the blocking native allocator. Uncommitted buffers must release
native ownership before returning their slots, including autorelease ownership.

| Backend | Capacity obligation for this change |
| --- | --- |
| Null | Logical validation has no native queue slots; cannot prove native boundedness |
| Vulkan | Timeline waits remain part of the one existing submit payload, no new auxiliary command buffer; existing command-pool/queue budgets still require their T4 evidence |
| Metal | Shared-queue count lease covers work/prologue/present/drain; no native allocation without reservation |
| WebGPU | Timeline submission remains rejected before native work; no auxiliary allocation or CPU callback wait |

Reservation arithmetic must live in core and reuse the CPU quota algorithm;
count units must not be reported as CPU or GPU bytes. The quota's established
zero-as-unlimited byte semantics must remain unchanged, while zero slot capacity
rejects. Tests cover exact/insufficient capacity, overflow, concurrent acquire,
idempotent lease release, native exhaustion without external kill, and recovery
after discard/completion. Overall native physical bytes and application peak
budgets remain separate T4 work.
