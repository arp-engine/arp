# Original typed cold command / 1

This independent Prototype stage binds a runtime-owned opaque command shell to
TypedNativeCold1. It connects the actual original WebGPU device and queue, but
creates no encoder, callback, command buffer, proof or submitted GPU work. It
never adapts typed admission to legacy NativeCold1–5.

## L2 intent and invariants

The version1 request requires explicit Prototype opt-in, exactly one command
payload, an independent total retained-owner ceiling and the exact Recording
quota ceiling. Shell custody adds three owners: opaque wrapper, shared native
command payload and its owned implementation. Total owners must fit this request,
the original Source graph ceiling and all three original lifecycle owner ceilings.
The earlier admission request budgets admission metadata; this separate request
budgets the shell increment. No immutable Source or admission budget is expanded.

Publication requires authentic quota-bound admission/reservation/seal/root with
Prepared state and an empty embedded journal. All shell objects allocate through
core under the original Recording quota. Connection uses the existing runtime
allocator/connector. Fallible allocations and exact native identity checks precede
publication of the original seal's sticky one-command claim. Failure frees partial
objects without claiming; success cannot be reused after wrapper drop, peer
admission or replacement reservation on that same seal.

The shared command implementation retains an opaque shared admission owner solely
for lifetime, exact owner comparison and generic-recording refusal. It has no cast
or virtual execution/authority interface. Typed and legacy wrappers retain their
own distinct, statically typed authentic admission and budgets. There is no new
state machine or callback pool. Shared native identity validation checks command
shared ownership, nonzero original recording identity, original device/queue,
existing completion-receipt inspection and exclusion of ordinary command modes.
Public inspection remains cold. No Pending validator is enabled by this stage.

## Versioned lowering

| Backend | Cold shell / connection | Recording and submission |
| --- | --- | --- |
| Null | ValidatorOnly, deterministic diagnostic | Closed |
| Vulkan | DiagnosticFailure, typed shell missing | Closed |
| Metal | DiagnosticFailure, typed shell missing | Closed |
| WebGPU | Explicit Prototype, actual original device/queue connection | Closed, typed callback/live executor missing |

SDK absence, native loss/shutdown, budget/identity/mutation or connection failure
reject deterministically. No dummy native payload or guessed native defaults are
permitted. Destruction releases references before admission/root. This stage
cannot have issued userdata; the original seal performs pre-record cancellation
and reservation counts release when their genuine owners drop. Generic open and
record paths remain closed. Only existing Recording and Submission may eventually
issue proof, after separate private live and callback contracts are implemented.

## Validation plan and state

Contract Complete / Prototype Complete for this selected cold shell only.
Backend Validated covers actual WebGPU device/queue connection and cold
identity/budget/claim/refusal/reclamation tests; no typed GPU execution.
Not Platform Validated; Not Production Ready. Quick contract and
Null hidden-frame cover intent, exact/one-less owner budget, four backend outcomes
and closed execution. Browser tests use a separately authored genuine Source,
original frozen payload and same-root seal/reservation. They cover actual queue
connection, forged owner, native mutation, sticky claim, peer/replacement
reservation, exact/one-less/exhausted CPU quotas, allocation-free inspection and
pre-record reclamation. Prior browser messages and flags must be preserved.

No visual output is changed; retain existing numerical prerequisite probes and
RenderDoc capture or stable tool-unavailable diagnostic. No typed native recording,
synchronization, Platform Validated or Production Ready claim is authorized here.
Continue private Pending validation and original-loss callback deduplication.

Final quick/Null5/5, Wasm10/10, inventory233/233 and sequential browsers2/2 pass.
Flags375/376 preserve all373/374 and exact prior1092 message strings/counts.
Actual shell metadata requires752 bytes, measured under its authentic Recording
quota; exact, one-less and exhausted cases retain the same assertions. Failed
allocation recovers without a claim. Peer admission, drop and replacement genuine
reservation never reset the original seal's sticky claim. Native pipeline/group
mutation and forged owners fail under full quota pressure with hot0/query0.
Existing A43/B36 and pixels3,16,3,12 probes remain unchanged prerequisites.

The first passed runs approached the unchanged180-second deadline because a
monotone search rebuilt the full genuine chain repeatedly. Reusing the existing
single measurement plus three single-use pressure cases reduced final browser
progress to160.75/161.47 seconds. Initial passed reports/XML/logs are retained;
no assertion or production gate was relaxed and no timeout was increased.

Evidence: `build/web-bindless-apple-debug/artifacts/bindless-continuation/typed-cold-command-validation.yaml`.
RenderDoc: `RHI-RENDERDOC-TOOL-UNAVAILABLE`. Continue observer/custody design through
`typed-cold-command-live-next-audit.yaml` and
`docs/original-typed-loss-observer-next-contract.md`, both DefinedOnly. Typed
callback/live recording/submission and the full bindless goal remain incomplete.
