# Original observed native loss binding / 1

Status: Contract Complete / Prototype Complete for the separate request, bounded
CPU registry and original observed native binding preparation. Backend Validated
is limited to real WebGPU subscription/storage, device/domain, suppression and
physical-loss/reclamation evidence. Its original preparation fixtures issue zero
receivers. Later authentic cold custody uses independently authored profiles; see
`original-typed-callback-custody-contract.md`.
Observer/custody integration remains DefinedOnly. Not Platform Validated or
Production Ready; no GPU completion, recording or submission authority.

The L2 caller explicitly authors `OriginalDeviceNativeLossObserverRequest/1`
before resource registration. It declares opt-in, observer slots, binding-owned
objects and exact subscription CPU quota bytes. The original native loss budget
still declares exactly one native registration and one callback. Binding-owned
objects count the one binding plus one separately allocated stable slot per
observer; slot backing storage and shared control are charged to the same quota.
All allocations and validation precede native userdata publication. The old v1
budget, concrete binding fields, allocation sizes and defaults remain unchanged.

An observed WebGPU binding shares the original subscription mechanics and sealed
device-issued receiver. It registers one `device.lost.then`, never a second
observer-specific promise handler. The new derived owner embeds the bounded
registry; original read-only observations retain that exact owner/domain. No
public native handle, terminal receiver or arbitrary observer registration is
added. The later custody bridge must prove the authentic claimed typed shell,
same seal/reservation/root and exact original observation before private attach.
Request1 alone keeps generic observer issuance and typed execution closed. The
separate Custody1 contract may prepare an authentic cold local receiver only.

Native delivery first invalidates the original logical domain, then closes local
observer admission and delivers `NativeLost` once while the native self-lease
retains the exact binding. Logical retirement delivers `Retired` after domain
invalidation; it leaves the native borrow retained until suppression or physical
loss. Shutdown invalidates the domain and attempts suppression of the native
borrow before local `Shutdown` delivery. Successful suppression permits native
handle/self-lease release only after local delivery returns. Failed suppression
retains the exact native state and self-lease for physical loss, closes local
admission and records a stable suppression diagnostic. Neither logical event is
a physical loss receipt or successful GPU drain. Repeated terminal operations
cannot replace the first local terminal or deliver twice.

Explicit subscription detach rejects pending observers. With no pending observer,
native suppression must succeed before releasing userdata; the local table closes
with `Retired`. A native suppression failure retains all needed owners and is
observable as a deterministic diagnostic, with retry limited to subsequent
suppression or physical native loss. No dummy state or CPU signal substitutes
for native loss or queue completion.

Four backend lowering: Null is ValidatorOnly and rejects native binding. Vulkan
and Metal reject with stable missing-payload diagnostics. WebGPU may enable only
independently opted-in native binding preparation after real browser tests; private
generic observer issuance and typed execution remain unavailable until their payloads and
gates are validated. No base capability or native synchronization is promoted.

Verification retains quick contract/unit and Null hidden-frame, plus real WebGPU
subscription count, partial-allocation unwind/retry, native publication failure,
exact device/domain/metadata ownership, logical terminal distinction, physical
loss, caller drop and bounded reclamation. Core receiver tests remain CPU lifetime
tests. They cannot establish authentic custody or native observer execution.
Preserve old browser flags/messages/numerical probes. RenderDoc capture or an
explicit unavailable diagnostic is required; no Platform Validated or Production
Ready claim follows from this prototype.

## Verified preparation scope

Quick contract/Null hidden-frame5/5, Wasm10/10, inventory235/235 and sequential real
browsers2/2 pass. Flags379/380 preserve all prior375/376; all1094 prior distinct
message strings and multiplicities remain exact. Observed binding328 bytes under
exact/one-less/exhausted allocation; slots3/binding-owned objects4 are separately
authored. Every measured captured profile has one real native promise handler.
Native publication failure suppresses userdata and recovers without a sticky
claim. Captured original device identity survives global foreign-device
substitution; foreign observation rejects. Three native callbacks deliver and
two explicitly suppressed subscriptions deliver none; every foreign userdata and
callback clears. Shutdown suppression failure retains the exact bounded native
self-lease until actual physical loss, including after caller/facade drop. All
quota domains reclaim; core hot allocations0. Prior A43/B36 and attachment pixels
3,16,3,12 remain verified. Final browser progress162.49/161.81 seconds under the
unchanged180-second deadline. RenderDoc diagnostic:
`RHI-RENDERDOC-TOOL-UNAVAILABLE`.

Evidence: `build/web-bindless-apple-debug/artifacts/bindless-continuation/typed-loss-observer-binding-validation.yaml`.
The binding preparation tests inspect zero issued observers in their own profiles;
they cannot prove possible-submit custody. Cold authentic custody has a separate
contract and validation record.
Continue the phase-aware receiver/drain and exact participant contract in
`original-typed-loss-observer-next-contract.md` and
`typed-loss-observer-binding-next-audit.yaml` before attaching authentic typed
graphs. Typed observer, Recording, Submission, GeneralBindless and NativeSync
gates remain closed. There is still exactly one existing completion pool and two
proof issuers.
