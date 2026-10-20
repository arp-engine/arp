# Original typed loss observer — next contract

Status: bounded CPU registry and independent observed native binding preparation
are validated. Authentic cold typed custody is implemented and verified; see
`original-typed-callback-custody-contract.md`. Private Pending
validation and typed live Recording/Submission remain DefinedOnly and closed.
The earlier independent binding tests continue to issue zero receivers in their
own profiles; the new custody tests use separately authored observed profiles.

## Reuse evidence and authority gap

The existing original subscription has one native `device.lost.then` handler and
one sealed `OriginalDeviceLifetime::NativeLossReceiver`. Delivery moves its native
self-lease, invalidates that exact original domain and releases the foreign handle.
The legacy canonical custody independently registers another handler for its loss
slot. It cannot consume typed demand8 unchanged while claiming subscription reuse.
`OriginalDeviceNativeLossObservation` is read-only and exposes no receiver, detach
or native handle. Its public API must remain that way.

Loss v1's registration/callback count and owner/CPU ceilings do not authorize an
observer table. A table needs a separate explicit versioned opt-in request with
bounded observer slots, CPU ownership and delivery/teardown semantics. Existing
v1 objects and genuine metadata actors must preserve their authority and evidence.
No mutable Source expansion, registry retrofit or uncounted second subscription
is permitted. Native loss, logical retirement and shutdown are distinct events;
none is successful queue completion.

## Required implementation order

Define L2 observer intent/invariants and four backend lowering first. An observed
subscription profile must be authored before original resource registration,
retain the authentic already-armed native binding/domain, and issue a private
observer registration only for an authentic typed command/custody graph. Public
observations cannot create terminal authority. A v1 binding without observer
support fails with a stable diagnostic; it cannot infer capacity from quota bytes.

Reuse `ExternalCallbackGate` for one-shot delivery/suppression, core quota-bound
ownership, bounded stable storage and existing monotonic identity helpers. The
observer indexing/delivery algorithm belongs in core/runtime. It must reject
capacity exhaustion, foreign/stale tickets and generation exhaustion; allocate
only during preparation and define reentrancy restrictions. This registry owns
observer lifetime only, not a second callback completion pool or GPU state ledger.
Existing `CompletionRetentionPool<Graph>` remains the sole custody completion pool.

Observer publication follows all fallible C++ allocations. The exact original
binding delivers the terminal event once; close observer admission before calling
receivers. Hold each original owner through its callback. Suppress future delivery
before releasing userdata; failed suppression retains ownership and fails closed.
Native subscription detachment while live observer graphs exist must reject or
perform the separately defined terminal shutdown-and-drain operation. Physical
loss invokes original domain invalidation and existing custody quarantine/drain,
without claiming queue completion or rollback success for possible submissions.

Counts must distinguish original shared native subscription/callback from local
observer registration/delivery, scoped callbacks and queue completion. Validate
actual Source/reservation demand against that versioned classification before any
issuance. Existing demand8 is metadata evidence only; do not promote it until the
observer's actual participants are proved. If different demand is required, create
a new typed contract version and separately authored actor before execution.

Preserve legacy allocation sizes/messages by keeping the old concrete subscription
layout and old request intact. An independent observed implementation/profile can
reuse factored subscription mechanics while owning explicitly admitted observer
storage. Adding fields to v1 or widening its defaults is not an authorized design.

## Four backend and validation boundary

Null is ValidatorOnly with deterministic observer execution rejection. Vulkan and
Metal reject until their original native loss and observer payloads exist. WebGPU
prepares only the explicitly admitted native subscription/storage profile. It
still rejects observer issuance until an authentic typed receiver and its live
terminal/drain policy are implemented and validated. No Native/Equivalent synchronization, capability, Platform Validated or
Production Ready claim follows from a core table or metadata contract.

Required quick tests and Null hidden-frame: intent/count/CPU/owner/overflow,
four gates, stale/foreign/capacity, one-shot delivery, reentrancy, teardown and
suppression failure. Genuine WebGPU tests must show one original native promise
handler, exact bound device/domain, real physical loss, cold cancellation, caller
drop, callback drain and possible-submit quarantine. Keep all prior browser
flags/message multiplicities and numerical probes. RenderDoc capture or stable
unavailable diagnostic is still required for any visual acceptance.

After observer integration, factor private retained validation behind the authentic
claimed command and same seal/reservation. Reuse root `validatePlannedFinals` and
its embedded journal. Split immutable native binding/ordinal checks from public
Prepared Stream2 validation and committed-state usage projection. Add no caller
bypass flag, stored CapturedJoin or new proof issuer. Only existing Recording and
Submission consume the completed typed payload and issue genuine receipts.


## Remaining terminal authority and one-shot boundary

The original binding preparation stage issued zero receivers. The new cold custody
stage retains authentic local receivers on separately authored profiles. Cold
Shutdown/Retired tests cannot prove possible-submit behavior.
Before adding the private bridge, define phase-aware terminal lowering against the
existing custody state's `maySubmit`, real queue receipt and foreign-callback drain
facts. A local Shutdown/Retired event must never mark the loss slot physically
Delivered or satisfy `releaseTerminalIfDrained`'s native-loss condition. A one-shot
terminal table that releases a possible-submit receiver on Shutdown also removes
its later physical-loss path. Do not silently enable that combination: either
prove queue/foreign drain before terminal release, or retain the original native
borrow and exact receiver until actual physical loss/drain under an explicit
versioned policy. Keep this decision inside the existing custody state and core
observer mechanics; do not create a second completion ledger.

`TypedNativeCold1`'s value schema has `runtimeLossCallbacks/Registrations=1` and
`existingDeviceLossCallbacks/Registrations=1`, but does not separately describe
local observer issuance. Preserve its metadata actors and immutable Source
reservations. Define the executable participant classification before deriving
new demand; if semantics change, author a separate version/profile and genuine
actor before Source/registry construction. No uncounted local callback or duplicate
native subscription can be justified by the coincidentally equal number eight.

The typed cold command has no custody claim field; preserve its measured layout
and existing messages. A sticky claim must be tied to the authentic seal or an
explicitly admitted backend-owned claim mechanism. Failed allocation/attachment
must publish no claim; wrapper drop or replacement reservation must not reset a
published claim. The existing canonical Graph should hold an explicit typed
alternative, using the same `CompletionRetentionPool<Graph>`, private issuers and
drain state. Public observations still expose no receiver or handle.
