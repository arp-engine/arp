# Original finite descriptor Source composition

Status: Contract Complete / Prototype Complete for f4a relative-access,
f4b1 committed-content diagnostics and f4b2 original Source/3/assembly/3 CPU
custody with FullBoundView/1. Host59/59 (including quick/Null4/4),
Wasm/browsers11/11 and inventory207/207 pass; evidence
`build/web-bindless-apple-debug/artifacts/bindless-continuation/original-finite-source-custody-validation.yaml`.
Backend Validated only for real WebGPU source-adopted groups and numeric probes.
No new native execution, Platform Validated or Production Ready claim.
Source/1 and Source/2 continue to reject committed
finite content. Source/3 enables explicit CPU custody only, without Inputs, state emission,
ordinary commands, general bindless or NativeSync. f3b2's scoped write acceptance
is a prerequisite; successful write observations alone are not command authority.

The extension must reuse OriginalCommandDescriptorSource, its unique wrapper
custody, canonical OriginalCommandResourceSources and the same Source/Usage/
Scalars/Inputs root. It must not create a new Source family, ownership inventory,
root, seal, journal or native issuer. Recording and Submission remain the only
full native proof issuers.

Implementation order:

1. Define a versioned, explicit opt-in diagnostic for mapping authored
   shader-relative byte access into a validated finite binding. The result is
   metadata, has no owner and never authorizes a write, state transition or
   command. Reuse DescriptorBufferBindingRangeContract::resolve and
   core::containsUnsignedRange; never form unchecked endpoint sums.
2. Extend the existing Source contract only after an exact original pipeline/
   descriptor/arena/buffer identity join. Source/3 freezes the
   successful authored range, actual finite-write pipeline, logical set,
   dynamic displacement in logical binding order and physical lowering in the
   backend. Its pipeline must match an original pipeline Source in the same
   canonical inventory, including shared-owner identity and registered generation.
3. Compose positional Inputs and the existing original state transaction with
   that exact frozen Source, immutable revision/epoch and dynamic offsets. Only
   then may backend-private Recording consume the committed native group.
   Complete stream preflight/replay must remain hot allocation0/query0.

A binding interval and access coverage are separate facts. The binding contains
an authored static base and unchanged finite size; dynamic displacement shifts
its effective base once. A shader-relative access is mapped against that
specific effective interval. Existing bufferAccessOffset/Bytes represent
physical coverage and cannot select or manufacture a binding slice. Coverage
must lie inside the original buffer and contain the explicitly mapped access.
An access inside the underlying buffer but outside the finite binding rejects.
Nonzero displacements must also match the frozen successful group and exact
positional Inputs; alignment of a sum cannot repair separately unaligned fields.

Caller-authored relative access is diagnostic input only. Source/3 admission
independently establishes its selected shader footprint. For the diagnosed
runtime-length publication profile, the first admissible conservative policy is
an explicitly versioned FullBoundView profile: the complete bound view is
covered for the declared stages/read/write access, regardless of a shorter
caller-declared footprint. Shader/target bounds semantics must come from the
original publication and backend contract; no guessed indexing behavior or
inferred footprint is permitted. Uniform-only profiles, tables/arrays, mixed
compact sets and other shader ABI profiles require separate contracts and tests.

The existing canonical owner projection must account for the retained genuine
pipeline and buffer dependencies without inspecting or caching native handles
or allocations. The finite native group and its CPU state remain backend-owned,
under the three independently captured f3b2 quotas and retained-slot budget.
The canonical Source quota is one domain shared by original buffer lease
metadata, pipeline Source adapters, the existing inventory and descriptor Source
adapters. It must not overlap device/registry ownership, accepted pipeline
creation/wrapper/artifact domains or any of the three finite-write private
quotas. Equal byte limits never establish domain identity or independence.
This preserves the existing canonical inventory/lease quota contract; buffer
metadata is not a separate domain from Source metadata.
Dropping callers after successful unique Source adoption must preserve the same
original owners. Pending, failed, canceled, reset, retired, sealed-write, lost or
foreign descriptor content cannot be frozen or replayed.

Before Source/3 custody, f4b1 adds an independently versioned committed-content
diagnostic to the existing final GpuDevice/DescriptorSet boundary and the existing
backend-private group. Request/1 requires explicit Prototype opt-in, a nonzero
expected content revision and the captured canonical Source quota byte limit.
The input is a borrowed, complete declaration sequence in ascending logical
binding order. It must match the committed group's genuine accepted shared
pipeline, logical set, every successful write, original buffer lease owner and
dynamic displacement exactly. Missing, reordered, foreign or forged inputs
reject. Every lease metadata quota must be that same canonical Source quota;
the pipeline and three private write domains must be independent of it.
The actual committed state revalidates its original device ticket, registration,
arena epoch, limits, declarations, revision and completed native error scopes.
A pending replacement rejects even if older committed content remains valid.
No native handle/allocation, private quota object, detached observation or new
owner is returned. Success is only a metadata diagnostic; it cannot adopt a
Source, emit state/commands or serve as native authority. Diagnostic observation
is allocation0/query0/IO0, including under full Source quota pressure.

Four-backend diagnostic outcomes: Null is validator-only DiagnosticFailure;
Vulkan is DiagnosticFailure because its original finite issuer is missing;
Metal is SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED; WebGPU is Equivalent only
after the existing group's committed native state passes the complete join.
Source/3, assembly/3, FullBoundView admission, positional Inputs and state/native
composition remain independent subsequent stages. Diagnostic/1 does not open
any of them.

f4b2 implemented custody contract (execution remains closed):

- Source budget/3 must explicitly name the canonical pipeline Source index,
  logical set, FullBoundView footprint version/1 and opt-in, and a bounded
  logical dynamic-offset capacity. Source/1-/2 must reject finite content;
  Source/3 must reject tables and ordinary/non-runtime publication profiles.
  New fields must be rejected by older versions when populated, rather than
  silently ignored. Unsupported footprint versions must fail deterministically.
- The genuine pipeline Source must already occupy that index in the same
  existing canonical inventory. Retain that original adapter and revalidate
  its exact object/control block, generation, kind and accepted publication.
  Canonical inventory ownership remains weak to avoid an inventory/Source cycle.
  A pipeline value, observation or matching layout cannot replace this adapter.
- Freeze all original successful writes and logical dynamic displacements in
  ascending binding order. Reuse bounded core storage under the captured Source
  quota; any contiguous declaration projection needed by diagnostic/1 is built
  cold and retained immutably. No hot reconstruction, allocation or native query.
- FullBoundView/1 conservatively covers every byte of each unchanged finite
  view at the shifted physical base. Reuse f4a with relative offset zero and
  access size equal to that view's size; physical coverage must contain it.
  Coverage cannot change the binding. Declared stages must cover the complete
  accepted binding stage mask, not only stages containing GetDimensions. Readonly
  storage requires ShaderRead; RW storage conservatively requires both ShaderRead
  and ShaderWrite. Reuse existing usage/stage/access/state validators and exact
  accepted target metadata; uniform/array/table/mixed-group profiles stay closed.
- Existing assembly/3 counts pipeline slots as assembly/2 does. It admits
  original finite Source/3 only when the same inventory and pipeline index are
  retained and current. Assembly/1-/2 cannot adopt Source/3. Preserve ordinary
  Source/2 handling without treating it as finite authority. Local inventory
  validation must check the same version rules as cold append.
- Retained pipeline and buffer owners must appear in the existing canonical
  projection once per genuine identity, including pipeline/layout/arena
  registrations. Reuse SharedIdentityProjection and RowMajorIndexGrid; do not
  add an owner inventory. Unique descriptor adoption remains the final operation
  after complete declaration/content/original checks and shared custody allocation.
- Backend outcomes remain Null validator-only failure, Vulkan issuer-missing
  failure, Metal runtime-length-unsupported failure and WebGPU Equivalent custody
  only after diagnostic/1 and FullBoundView/1 pass. No positional/state/native
  proof or command authority is introduced. Existing native issuers remain two.

Verified f4b2 evidence includes genuine compute/graphics inventory closure, exact/
one-less/exhausted Source quotas and owner budgets, caller-drop retention,
pipeline-position/kind/owner/generation failures, wrong logical set/displacement,
full-view exact/one-less coverage and stage/access failures, changed revision,
arena reset/retirement/device destruction/actual physical loss, and hot0/query0.
The existing20 compute/5 graphics numeric probes and every previous324/323 browser
flag remain valid; new custody flag gives325/324. Four compute variants and one
graphics pixel probe consume the exact Source/3-owned group. Thirteen genuine
custody cases, five cold quota probes (minimum1076 bytes), hot allocation0/query0
and actual loss/destruction pass. Shared control-block quota charges are checked
after weak observers release. The original two-slot monotonic arena is explicitly
retired/reset between completed numeric and independent caller-drop fixtures.
Source/3 custody is established by actual unique adoption and original inventory
proofs, independent of f4b1 diagnostics. Full state/positional/native composition
is pending f4c and later stages.

Four-backend conclusions for this stage:

| Backend | Access metadata validation | Source/native composition |
| --- | --- | --- |
| Null | Pure validator only | DiagnosticFailure; no native finite payload |
| Vulkan | Pure validator only | DiagnosticFailure; original finite issuer missing |
| Metal | Pure validator only | SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED |
| WebGPU | Pure validator; private queried interval prerequisite | Equivalent CPU Source3/assembly3 custody; positional/state/native composition closed |

The diagnostic must clear all output on version/opt-in/schema/bounds/coverage
failures, allocate nothing, perform no native queries and grant no capability.
Tests must cover exact/one-less coverage, nonzero base plus displacement,
unchanged binding size, accesses outside the finite view, zero/overflow ranges,
and all four execution gates. Source/1-/2 finite negatives and the Null hidden
frame remain required. Source/3 custody has genuine original ownership, quota
pressure, caller drop, state/positional gate failures and actual both-browser
payload/output evidence. Positional/state execution requires separate proofs.
RenderDoc is unavailable on this host
(RHI-RENDERDOC-TOOL-UNAVAILABLE); no Platform Validated or Production Ready claim.


Continuation 2026-10-06: selected original Inputs3 Source2/3 descriptor positional
joins are now Contract Complete / Prototype Complete, with genuine WebGPU CPU
join/group regression evidence in descriptor-positional-validation.yaml.
Source3 itself still grants no position/state/native authority. TableSource2
metadata diagnosis is verified DiagnosticFailure only; accepted table actor is
open. Sparse texture projection now resolves exact command-index records across
typed Scalar-family binds (typed-texture-projection-validation.yaml). Full typed
shader consumption/state/transaction/native remains closed. Next contract:
original-descriptor-consumption-state-contract.md and audit
original-descriptor-state-next-audit.yaml. Full bindless is not complete.

Pending native execution amendment 2026-10-06: FullBoundView/1 physical
coverage now equals the resolved finite interval exactly, including its single
dynamic displacement. This tightens only Source3 full-view validation; the
general finite access diagnostic retains conservative containment semantics.
The same pure frozen-footprint helper is consumed by cold Source validation and
the authenticated private Pending closure, without committed-state projection
or a borrowed logical device. Enlarged coverage uses the stable diagnostic
RHI-ORIGINAL-FINITE-SOURCE-FOOTPRINT-COVERAGE. Public cold validators remain
Prepared-only and native proof issuers remain exactly Recording/Submission.
Acceptance is recorded separately in original-typed-native-finite-acceptance-
contract.md; this amendment does not open the ordinary non-runtime profile.
