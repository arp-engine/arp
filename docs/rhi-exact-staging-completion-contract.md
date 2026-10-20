# ExactStagingCompletion/1

Status: versioned contract and implementation. Executed evidence and readiness are
recorded in `rhi-webgl1-validation.md`; Apple-native execution is a separate open gate.

L2 intent is immutable uploaded bytes retained through the issuing GPU command's lifetime.
Recording order, frame number, public fence signal, readiness epoch, unrelated queue completion,
and device facade destruction are never completion authority. No public L2 ABI changes.

An unaccepted recording cancels only its exact reservations. Before native acceptance, its
owned journal transfers to the original native command; before commit, that journal transfers
to pre-created submission retention. A native terminal callback resolves only that journal.
Terminal native error permits lifetime reclamation after the native API proves no remaining
access; it never publishes a successful upload, readback or shader output. A terminal
public fence is lifetime completion only; its wait reports the native failure.
Missing callback/unknown status keeps reservations and exact page/native device owners alive.

Metal callback threads only resolve pre-existing atomic reservation tokens. The serialized
allocator owner collects those tokens and invokes existing core exact slice cancellation or
retirement. The token callback does not mutate the page allocator, wait for an owner lock or
allocate. Native page release after terminal completion continues through the existing
completion retention closure. The journal owns original page payload and completion metadata even if
pool/device wrappers are destroyed. Allocation exhaustion returns the existing stable capacity
diagnostic before a copy is recorded; no rescue page beyond the caller's page count/budget.
Reservation identity uses the existing core MonotonicIdSequence across allocator instances, with a non-reused allocation generation, not merely an offset/frame.
Published epoch is retry telemetry and cannot authorize any slice. Legacy serial retirement is
rejected with RHI-METAL-STAGING-SERIAL-COMPLETION-UNAUTHENTICATED. Serial allocation exhaustion
fails closed; it cannot wrap and reuse a live identity.

| Backend | Lowering / conclusion |
| --- | --- |
| Null | Validator only; explicit host reservations do not prove GPU completion. |
| Vulkan | Existing original-fence exact command journal; no new serial-prefix authority. |
| Metal | Exact atomic reservation tokens and original native terminal callback; owner-thread collection. Apple-native validation remains required. |
| WebGPU | Original persistent copy entry refuses RHI-WEBGPU-STAGING-COPY-PATH-UNAVAILABLE. Reachable queue-write CPU source consumption ends after the API has copied the source; it is not GPU completion. Original queue/error-scope joined publication remains required. |
| WebGL | Production factory/record/submit still fail closed; P2/P3 must implement its original context ownership and completion contract. |

Validation must cover two recorded commands completed in reverse order, held unsubmitted
recordings, unrelated epochs/serials, stale token ABA, wrong slice identity, exact cancellation,
wrapper destruction before journal resolution, callback/owner interleaving and failed capacity.
Quick core/Metal-host tests and Null hidden frame establish contract behavior only; Apple
native submit/readback tests are a separate platform gate. No shader or visual changes here.
