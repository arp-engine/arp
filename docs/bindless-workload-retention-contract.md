# Original workload retention /1

Implementation in progress in the engine closure batch. No integrated backend,
platform or Production Ready claim is made.

The original typed inventory may acquire one caller-authored shared capacity
pool after all original sources are assembled and before a CommandUsage graph
is published. Successful attachment freezes that same inventory. Existing
graphs cannot gain uncounted owners through a late attachment. Recording,
state, submission and callback ownership remain in the original mechanisms.

Seven independent reservations bound workload count, unique snapshot count,
snapshot envelope bytes, actual reported fault-buffer bytes, canonical buffer
and texture allocation count/bytes, and the canonical retained-owner union.
Snapshot and fault bytes overlap resource bytes; these domains are never summed
into physical GPU memory. Resource bytes are retained per workload: a resource
shared between two inventories is conservatively reserved in both. Actual device
memory remains enforced and reported by the original backend allocation ledger.
Pipeline, descriptor, staging, result and command allocations keep their own
existing independent owners/quotas; these seven counters do not replace them.

Snapshot identity is deduplicated by its original shared control block. Buffer
and texture sources are already unique in the canonical validated inventory;
views/uploads/readbacks must reference those original sources. Fault allocation
sizes come from their original GpuBuffer allocation observations. Unknown sizes,
unavailable original registrations, overflow, missing Root, incorrect snapshot
charges or an exhausted graph-owner cap refuse before reservation publication.

Acquisition is transactional across all seven original counters. A late refusal
releases only the current attempt, preserving concurrent reservations. Metadata
allocation occurs under the original Source quota. Allocation failure publishes
neither a reservation nor a sealed inventory. Repeated preparation can only use
the exact original pool. No caller can release a published reservation early.
The inventory owns it until its last original command/callback owner drains.

Null validates and reserves without executing. Vulkan, Metal and WebGPU use the
same shared capacity semantics; no native handle or backend-dependent capacity
is introduced. WebGL/unknown/version/opt-in failures are explicit. Common public
Compute callback/fence and Graphics preflight can require this reservation;
legacy scoped profiles remain distinguishable by their explicit request flag.

Acceptance includes exact and one-less ceilings in every domain, late rollback,
foreign pool/control block, stale device, allocation failure, same-pool reuse,
allocation-free hot inspection, full retention after caller drop and zero usage
after genuine completion/callback drainage. Consolidated platform verification
is deferred until the complete engine implementation batch is ready.
