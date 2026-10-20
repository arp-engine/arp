# Original typed cold issuer preparation

Status: Contract Complete / Prototype Complete for cold preparation only.
Backend Validated for genuine WebGPU cold receipt preparation/refusal/cleanup
and physical loss only. Not Platform Validated; Not Production Ready.

TypedIssuerPreparation1 is an independent explicit Prototype opt-in for cold
preparation. It admits the existing WebGpuOrdinaryRecording and
WebGpuOrdinarySubmission objects and seven genuine native completion-receipt
states: three recording scopes, three submission scopes and one queue receipt.
Prepared listeners are Unarmed and unbound. They prove no native operation.
Recording and submission remain closed.

The request independently specifies one Recording, one Submission, seven native
slots, owner ceiling and exact Recording/Submission/Callback CPU ceilings. The
owner increment is three: the public opaque wrapper and the two existing issuers.
Preparation authenticates the original custody graph/pool ticket, shell,
seal/root/reservation, Source and lifecycle ceilings, local loss observer and
independent quota owners. It reuses existing State slots and the sole completion
pool. It adds no original device.lost subscription, callback slot or loss receiver.

All C++ allocations use core quota-bound allocation. All seven native states must
be Prepared and match the original device, queue, kind, State-owned userdata and
exact existing issuer callback function before any handle or issuer claim is
published. A partial failure suppresses its genuine native states and clears
userdata/functions, returns failure without publication, restores live quotas and
permits retry on that same original candidate. Successful publication is sticky;
dropping the wrapper never permits another preparation.

Inspection first checks exact retained State/issuer owners and native receipt
bindings, then consumes the complete public original cold contract before success.
A changed native receipt therefore fails before expensive committed-state
projection replay. This changes refusal ordering only; no successful validation
omits the original custody, shell, seal/root/reservation, Source or quota checks.
The closed-inspection API itself performs this full validation before checking
ordinary-path refusal, so fixtures need not immediately repeat identical scans.

Quota tests measure genuine retained allocations once, cancel that candidate and
reuse one fresh unclaimed candidate for each independent exhausted/one-less
failure and retry. Both independent exact ceilings and a full Callback quota are
then imposed simultaneously for genuine success. That exact original candidate
continues into retirement/deferred native suppression; both post-cancel live
quotas must equal their measured cancellation baseline.

Each existing issuer stores a weak original State identity and a private immutable
typed tag. The matching lock selects the exact State specialization; the other
lock returns empty before casting. This preserves legacy issuer layout and
allocation observations. It introduces no typed-to-legacy authority adapter.
Ordinary prepare/record/submit APIs remain closed for these canonical objects.
The only recorded-command proof friends remain Recording and Submission.

Cold native callbacks cannot produce proof: unexpected delivery quarantines the
original State. Cancel/drop, logical retirement/shutdown and actual physical loss
reuse the existing retention/drain/reclamation algorithms. Logical terminal events
retain the exact physical-loss receiver; they cannot fabricate completion. Actual
loss suppresses all seven prepared native states per custody before releasing the
retained graph and quotas.

Null validates intent and rejects native preparation/execution. Vulkan and Metal
return stable missing-payload diagnostics. WebGPU supports this explicit Prototype
cold preparation only, with recording/submission diagnostic failures. This
boundary provides no NativeSync or public/general bindless promotion.

Final verification and status are recorded in
`build/web-bindless-apple-debug/artifacts/bindless-continuation/typed-issuer-preparation-validation.yaml`.
Backend evidence is limited to genuine cold native preparation, binding refusals,
failure cleanup and physical-loss reclamation. Pending recording and actual queue
completion remain separate requirements. No Platform Validated or Production
Ready claim is permitted; RenderDoc is unavailable in this environment with
`RHI-RENDERDOC-TOOL-UNAVAILABLE`.

Final quick/Null5/5, Wasm10/10, inventory237/237 and sequential browsers2/2 pass.
Recording316/Submission288 bytes; flags385/386 preserve all382/383 and exact1101
prior stable messages/counts including legacy allocations. Native7/listeners14,
partial failure3 cleanup/retry, independent quota pressures, binding mutation5,
hot0, sticky drop and prepared loss states21 all pass. A43/B36 and pixels3,16,3,12
remain unchanged. Final runs179.20/179.02 seconds use unchanged180 deadline;
earlier assertion/build failures and timeouts remain retained and excluded from
passing evidence. The remaining deadline margin is small.
