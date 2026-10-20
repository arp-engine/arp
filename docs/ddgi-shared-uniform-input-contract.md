# DdgiSharedUniformInput/1

Scope: the existing shared sky-environment and directional CSM constant buffers
in DdgiRenderer. This contract does not change shader matrix layout, sky/CSM
formulas, shadow-map binding ownership, descriptor/atlas versions or frame
lifecycle. Implementation and native evidence are tracked separately.

## Common input semantics

Successful renderer creation initializes each complete GPU block and its fixed
CPU cache. Setters reject an uncreated, failed-create or destroyed renderer.
Validate the public descriptor before touching any state. Invalid descriptor
input preserves the previous valid block, prepared token and committed history.

Compare only fully initialized packed bytes with proven layout (sky 96 bytes,
CSM 272 bytes, all float fields and no padding). Bit-identical valid input is a
no-write success, including while any accepted reader is pending. A cache match
never restores an invalid block. No tolerance or matrix-layout guess is allowed.

Before any changed or recovery upload, query all existing GpuInputSlotLease/1
readers. Any pending reader returns success=false with the domain diagnostic
DDGI-SHARED-SKY-INPUT-NOT-READY or DDGI-SHARED-CSM-INPUT-NOT-READY. It causes no
upload, allocation, cache/telemetry change, prepared-token invalidation or history
advance. Readiness is bounded nonblocking polling, not a host/GPU wait.

After admission, invalidate the prepared input/schedule and that domain's cache
validity before writing. Publish validity and bytes only after the full upload
succeeds. Upload failure retains the underlying diagnostic under the existing
domain upload prefix. Both failure-before-write and failure-after-partial-write
leave the domain invalid: prepare rejects DDGI-SKY-INPUT-INVALID or
DDGI-CSM-INPUT-INVALID, and all direct record entries fail before any command.
The other domain and BVH validity remain independent. A complete successful
upload, including a retry with the old bytes, recovers validity. A subsequent
prepare supplies a fresh token; the old prepared token never becomes acceptable.

These operations do not advance or roll back committed scheduler/history state.
Previously accepted work retains its real completion. Callers serialize prepare,
mutation, record and acceptance as required by the existing DDGI contract. A
mutation after prepare requires re-preparation before acceptance. Default black
sky/disabled CSM initialization remains the existing declared input behavior,
not fallback after an upload error.

## Four-backend lowering

| Backend | Storage / accepted-reader completion | Unavailable or failed path |
| --- | --- | --- |
| Null | Validator buffers and explicit validator fence completion; common admission/cache rules | Validation failure is observable; Null never certifies native consumption |
| Metal | Existing native uniform buffers and accepted submission completion fence; shared writes only after all readers retire | Creation/upload/submission failure preserves failure; no placeholder or fake completion |
| Vulkan | Existing native uniform buffers and queue fence completion with the same common admission | Capability/creation/upload/submission failure rejects; no CPU replay |
| WebGPU | Existing uniform buffers and asynchronous queue completion represented by the existing fence contract | Unavailable capability or failed upload/submission rejects; no immediate CPU signal substitute |

No RHI descriptor, capability, native state or completion mapping changes are
introduced. Backend names/handles remain absent from renderer/game decisions.
Each native backend still requires separate native evidence; a specified row or
Null test is not Backend/Platform Validated.

## Storage, consumers and required evidence

Reuse the two fixed-size caches and existing bounded lease array; no extra GPU
buffers, fences, version lists or runtime waits. CPU storage added is bounded by
the two block sizes and validity flags. Existing DDGI/RHI resource budgets and
ownership apply; total physical-peak validation remains a separate requirement.

WOCC consumes setter failures without recording/submitting the invalid candidate.
Its RenderSystem readiness owner remains unchanged. CSM uniform bytes and future
shadow-map descriptor identities have separate ownership; this contract cannot
claim that a game has connected CSM or validated its projected lighting.

Required tests: lifecycle/invalid input; default/no-change reuse; both domains,
both slots and two-reader completion orders; zero writes/allocations/state change
while busy; actual old/new GPU contents of the complete blocks; upload failure
before/after real write and same/old bytes recovery; stale token rejection; all
nine direct recording guards; successful recreation and final retirement.
Each batch also retains a fast Null test, native smoke, Debug/Release assertions,
affected consumer/app tests and input/artifact hashes. Native driver/device loss,
full production pending/recovery and visual acceptance are independent gates.
