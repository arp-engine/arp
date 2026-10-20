# TextureReadbackBatchIsolation/1 and TextureReadbackExpectedManifest/1

These consumer-side L2 contracts extend TextureReadbackIdentity/1 without
changing the extension ABI, native payload layout, capability or queue policy.
They apply equally to every backend. A successful native readback is still
required for backend validation; a logical batch or Null result cannot replace it.

## Batch isolation

Every entry declared by TextureReadbackBatch obtains a nonzero request ID from
one exported RHI-domain sequence shared by all capacities and callers. IDs never
repeat within that loaded runtime and are not recycled on failure/destruction.
The sequence reuses core::MonotonicIdSequence; zero means exhaustion and causes
RHI-READBACK-REQUEST-ID-EXHAUSTED before scheduling. The batch owns its correlation
state and is neither copyable nor movable. Identical frame, generation, label
and source in two independent batches must still produce distinct identities.

Consumers treat requestId as opaque, not an array index. Lookup is bounded by
the declared batch capacity and does not allocate. An unknown request ID rejects
with RHI-READBACK-RESULT-UNEXPECTED; a known request with changed frame/generation,
source, label, format or extent rejects under the existing identity diagnostics.
First error remains latched, including after finish or an apparent complete batch.

requestIdentity(index) returns the declared identity, or an invalid zero identity
for an undeclared index. This supports explicit queue routing and duplicate-request
tests without assuming numeric values. Direct scheduleTracked callers still own
their correlation domain and must not deliberately reuse another pending request's
identity; they may use the same allocator. No adversarial authentication guarantee
is implied by correlation IDs.

## Complete expected manifest before source validation

The consumer declares all required labels with declareExpected before looking
up successful resource handles. Each successful declaration increases expected
exactly once and freezes the entry's identity. bindExpected then associates an
entry with a valid texture ID, format and extent. A source can be bound only once,
before seal; invalid indices, invalid IDs and rebinding fail without scheduling.
seal rejects any unresolved source with RHI-READBACK-EXPECTED-SOURCE-MISSING.

Missing resources must not reduce the already declared expected count. A failure
in entry 0, the middle entry or the last entry preserves the same complete required
denominator. No partial manifest can schedule work after that failure. Existing
expect(label, texture) remains a convenience for callers that already have a
complete known input; workloads that must detect missing required resources use
the separate declaration/binding phases. Capacity and label bounds stay unchanged.

WOCC freezes required labels from its requested feature configuration, declares
the full set, then resolves graph textures. It retains its domain diagnostic
WOCC-READBACK-REQUIRED-ATTACHMENT-MISSING and reports the full expected count.
It does not lower required to optional when allocation or graph construction fails.

## Four-backend lowering and evidence

| Backend | Consumer semantics | Execution boundary |
| --- | --- | --- |
| Null | Shared batch identity, manifest, negative and first-error validation | No readback extension; no GPU execution claim |
| Metal | Shared batch; arbitrary caller identities are echoed on completed/failed/abandoned payloads | Existing native tracked extension; same-frame/same-source two-request GPU test must reject cross-batch results |
| Vulkan | Shared batch and unchanged explicit unsupported gate | Tracked readback still DiagnosticFailure until bounded ownership implementation; never substitute legacy readback |
| WebGPU | Shared batch and unchanged explicit unsupported gate | Same explicit DiagnosticFailure; browser/native support is not inferred from this change |

Required tests: simultaneous batches with identical source/frame/generation/label,
different template capacities, duplicate and stale samples, declaration overflow,
unresolved first/middle/last source, rebinding and post-seal edits, first-error and
count preservation, per-position schedule rejection, and existing native completion/
abandonment. Request sequence overflow/concurrency is covered by the reusable core
sequence tests; no test resets or recycles the production sequence. Final WOCC
Debug/Release smoke verifies integration, not domain image correctness, fault
injection completeness, deadline or internal bounded shutdown.

## TextureReadbackCommandAbandonment/1

Unsubmitted recording ownership is distinct from backend storage retention.
Destroying an unfinished encoder, or destroying its finished but never submitted
command buffer, makes every recorded readback terminal Abandoned. A still-live
finished command buffer remains eligible for submission after its encoder dies.
Destroying submitted public wrappers never abandons in-flight GPU work; native
completion/failure remains the only execution terminal. Existing poison/submission
failure diagnostics take precedence over abandonment. Recording, destruction and
polling remain externally serialized under the existing command ownership contract.

Readback queue entries and diagnostic native observers are storage holders, not
recording/submission owners. Their shared_ptr reference count cannot determine
abandonment: multiple pending readbacks must not keep one another permanently pending.
Polling emits each exact request identity once, releases its staging charge once,
and permits budget reconfiguration after the queue empties. Retained native storage
is freed only after all actual owners retire; no fake completion or early GPU free.

Four-backend lowering: Null retains validation-only status and no readback extension;
Metal tracks explicit abandonment at encoder/command-buffer ownership boundaries;
Vulkan/WebGPU keep their explicit tracked DiagnosticFailure gate, with no legacy
substitution. No public ABI, capability, shader, or consumer native access changes.
Required native tests cover two requests sharing one unfinished/finished command,
encoder destruction with the finished buffer alive, buffer discard with the encoder
alive, extra native observer retention, and submitted public-wrapper destruction.
