# Original typed dependency projection and frozen candidate /1

This contract defines the original positional CPU dependency prerequisite for
f4eb2c2a2c2b. It does not approve native synchronization or an empty native
barrier payload. Status is **Contract Complete / Prototype Complete** for this
selected CPU prerequisite. The parent native operation/executor phase remains
incomplete.

## L2 semantics and provenance

`OriginalTypedDependencyProjectionContract::Request` is independently versioned
and requires explicit Prototype opt-in, including the existing StateBoundUsage1
request. `common::OriginalTypedDependencyProjection` consumes the authentic
same committed State2/Stream2 and all original usages. It owns no allocation,
GpuDevice, alternate resource inventory, state machine, journal or proof. It
reuses the guarded original usage walker, state timeline resolver, canonical
texture geometry, ResourceDependencyArguments and existing buffer compatibility
and writable-alias checks.

Each record captures original command/descriptor/reference/source positions,
kind, resource ID and registration generation, complete immutable dependency
fields, actual whole-buffer bytes or original texture layout/full-aspect range,
and exact before/after states. Every represented texture cell must resolve to
the same original state at the dependency position. Combined depth/stencil
planes, partial texture dependency/attachment ranges and unrepresented resources
fail deterministically. A transition retains its original complete range and
states; a memory dependency retains its original fields and current state.

The complete contribution list is validated for buffer usage compatibility and
shader writable aliasing before dependency emission. All render bindings remain
in their full pass scope, including shadowed or unused bindings. Duplicate
texture/aspect attachments in one render scope are refused in this selected
full-aspect profile. No byte-range heuristic makes incompatible whole-buffer
usages legal.

The existing `whole-slots-shared` actor binds the same buffer as StorageRead and
Storage in one dispatch. Its genuine metadata closure remains valid, but this
projection and candidate publication must return
`RHI-TYPED-USAGE-SCOPE-INCOMPATIBLE-BUFFER-USAGE`, emit zero records and clear all
counts before allocation. It is mandatory negative coverage alongside the eight
compatible original actors, rather than evidence of native execution legality.

`core::OrderedIntervalNeighborhood` selects the nearest preceding and following
complete scopes for the same resource. It admits unordered repeated identical
intervals, bounds observations and clears both sides/counts on failure. A
resource contribution whose scope contains the dependency is refused. Ambiguous
selected boundaries are refused. The core algorithm allocates nothing and uses
inclusive interval ends without a sentinel or end+1 overflow.

The projector then revisits the complete original contributions and unions every
matching contribution's stages/access in each selected scope; every endpoint
retains its scope, state and contribution count. Original dependency masks must
cover the complete union, with exact before/after state matching. No single
representative binding can stand in for that union.

| Clause | Original endpoint evidence | Meaning |
| --- | --- | --- |
| InitialBookkeeping | Following scope only | Original initial state bookkeeping; does not prove external initialization |
| BetweenScopes | Preceding and following scopes | Original positional candidate for native scope order/visibility mapping |
| FinalBookkeeping | Preceding scope only | Original terminal state bookkeeping; does not prove an external consumer or GPU completion |

A terminal memory barrier remains FinalBookkeeping even when its destination
stage/access descriptor is valid. The original two-pass attachment probe has no
buffer use in the second pass: command10 therefore has no following buffer
endpoint. Its command9 attachment memory dependency has both original pass
endpoints. The compute probe's dependencies8/9 lie between original dispatches6
and14; initial0/1 and final16/17 remain separate bookkeeping clauses.

## Budgets, publication and hot validation

The caller must explicitly bound dependency records, captured projection passes,
contribution visits, compatibility pair checks, total endpoint contributions
and texture cells. No runtime estimate enlarges those budgets. The complete original StateBoundUsage1 join and footprint validate once per
dependency traversal, before a private borrowed CapturedJoin is issued. Only the
CPU dependency projector can consume that scoped ticket. It is never stored or
used to authorize pending state or GPU work. Each subsequent projection pass
reuses the same original consumption/range mappers and pure footprint checks,
with the captured lifetime/State2 lowering guard before each pass. It does not
repeat complete transitive Source validation for every compatibility pair. The
public cold/state-bound visitors keep their complete original preflight; no
external entry accepts an unvalidated ticket. Counts describe
one complete dependency projection, rather than repeated outer verification.

Dependency preflight and replay share that one ephemeral complete join under the
existing immutability/serialization contract. Timeline queries also consume it
through private CPU usage-projector wrappers, checking the same lifetime/lowering
guard before calling the existing buffer/cell resolver. Public timeline queries
retain full Graph validation. The timeline grants exactly one CPU friend and no
backend authority. This avoids repeated recursive source checks per cell while
preserving the original resolution algorithm and refusal semantics.

Complete original validation and every dependency mapping are preflighted before
the first visitor or frozen-record allocation. A validation refusal clears all
aggregate counts and emits zero records. Replay must derive identical counts;
a visitor refusal is terminal for that ephemeral projection. Original owners
must remain immutable and externally serialized throughout preflight/replay.

`FreezeRequest` independently admits retained owners and CPU metadata bytes.
`WebGpuOriginalTypedDependencyDecision` retains the genuine loss-bound Closure2,
which already binds the same original State2/Stream2/root, native resource,
pipeline/group payloads, registration generations and native loss observation.
It requires the existing Recording quota identity/limit and Source-authored
Recording ceiling. Its added object owner must fit both the request and original
Source owner ceiling. The original quota owner is already in the closure and is
not counted a second time. All owned objects/storage use core allocation and
BoundedSequence. A failed preparation publishes nothing and releases metadata.

The allocation boundary fixture measures the actual live quota delta of one
successful factory while its result remains retained. It asserts that no allocator
deallocation occurs inside that factory, so the measured retained charge is its
exact incremental peak. Destruction must restore the baseline. Exact, one-less
and exhausted probes still execute independently; no guessed object/header size,
historical high-water inference or extra quota is used. Browser progress records
include measured elapsed time for diagnosing cold validation cost.

Frozen records retain the complete original clause plus one explicit CPU
candidate operation: InitialStateLedger, InterScopeCommandListBoundary or
FinalStateLedger. Validation replays every original record, candidate, count and
owner binding. A genuine different Closure2 built from the same root is refused
by owner identity. Native payload mutation, registration/state/source staleness,
quota limit changes and captured loss invalidate before any native operation.

Mutation coverage holds the actual frozen candidate in the same retained State
that undergoes original quota, pending-write, committed revision and arena epoch
changes. Independent finite and whole loss actors retain candidates before
pending ownership and the real browser device-loss event. The finite loss actor
explicitly authors Registration7/native source ceilings; ordinary CPU fixtures
keep their zero native ceilings. After actual loss, both the captured original
loss observation and candidate validation must return the exact
`RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST` diagnostic. Candidate validation,
republication and projection all refuse, allocate nothing and clear all counts.
Original callback/transaction cleanup still owns abandonment and reclamation.

There is no recording/submission API or receipt here. Candidate operations
cannot execute and return
`RHI-WEBGPU-TYPED-DEPENDENCY-EXECUTOR-PAYLOAD-MISSING`. All typed native cold,
recording, submission and NativeSync gates stay closed. Actual operation payload
admission and existing executor consumption are mandatory next steps; an enum,
count or frozen CPU record does not substitute for them.

## Four-backend lowering and status boundaries

| Backend | Selected CPU clause projection | Typed native execution |
| --- | --- | --- |
| Null | ValidatorOnly / DiagnosticFailure | Closed, stable validator diagnostic |
| Vulkan | DiagnosticFailure, original typed join missing | Closed |
| Metal | DiagnosticFailure, original typed join missing | Closed |
| WebGPU | Equivalent CPU projection under explicit Prototype intent, genuine State2/Stream2 and loss-bound Closure2 | Closed pending actual native operation/executor payload |

No DeviceCapabilities flag, extension default, fallback plan, shader artifact,
GPU payload factory or native proof friend changes. Recording and Submission
remain the only recorded-command proof friends. Numerical compute and attachment
prerequisites remain separate evidence; they are not executed by these candidates.

Contract Complete and Prototype Complete describe only this selected CPU
projection/frozen candidate. Backend Validated describes
actual WebGPU original actor equivalence/refusal, with no new native GPU payload.
Platform Validated and Production Ready are not established. RenderDoc remains
unavailable (`RHI-RENDERDOC-TOOL-UNAVAILABLE`). The parent dependency synchronization
phase and full bindless goal remain incomplete.

## Verified evidence

Quick contract/Null hidden-frame5/5, Wasm10/10 and inventory230/230 pass. Sequential
disabled/enabled browser paths pass2/2, with367/368 true flags. Every previous
361/362 true flag and the exact prior1054 BINDLESS message strings/multiplicities
remain preserved. Eight compatible actors freeze; one incompatible shared-buffer
actor refuses; two retained candidates reach real physical loss. Twelve genuine
refusals cover quota, pending, revision, arena epoch and finite/whole physical loss,
with exact native-loss diagnostics, allocation0 and query0.

The existing A43/B36 compute experiment and four attachment pixels3,16,3,12 remain
unchanged. Full reports, comparisons, unit results and status boundaries are in
`build/web-bindless-apple-debug/artifacts/bindless-continuation/typed-dependency-decision-validation.yaml`.
The next native payload/executor binding audit is
`typed-dependency-native-operation-next-audit.yaml` in the same evidence directory.
