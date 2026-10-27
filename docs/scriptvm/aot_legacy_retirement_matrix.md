# ScriptVM AOT Legacy Retirement Matrix

Status: `Legacy Isolation: Contract Complete; Legacy Module Split: Contract Complete; Full Legacy-Retirement Gate: Contract Complete`

The former contract-v1 descriptor, generic executor, and Hybrid implementation
have been retired from the source and build graph. `AotRegistrationHostV5` is
the only production registration ABI. Generator, continuation, and deopt
boundaries now use the explicit Interpreter-only contract or the bounded
DirectNative operation-boundary contract; unsupported shapes are deterministic
`AotSwitchUnsupported` decisions, never silent legacy selection.

The v5 `DirectNative` runtime is the only production AOT path. Contract-v1 and
Hybrid are retired; unsupported production shapes remain explicit v5 rejection
or an isolated Interpreter-only editor path.

The detailed notes below are historical acceptance records. Any older sentence
that calls a migration row "open" describes the state at that date and is not
the current gate status above.

| Capability | Current owner/status | Required replacement | Evidence required before deletion | State |
| --- | --- | --- | --- | --- |
| Legacy module split | Retired; no `aot/legacy/` executor or Hybrid target remains | Split artifact, execution, generator, proof, registry, and codegen responsibilities without changing ABI or semantics | Debug and Release source/build-graph audit and AOT-only symbol/package smoke | Contract Complete: retired source, targets, tests and symbols are absent; no production/editor target links Legacy |
| Generator protocol | Interpreter owns the complete protocol; DirectNative owns only proven bounded lowering | Versioned Interpreter-only protocol for all general generator graphs; DirectNative bounded delegation contract with exact identity and no-replay | VM generator unit corpus, v5/Interpreter randomized differential, generated package and browser smoke, Windows/Web evidence, deterministic unsupported-shape tests | Contract Complete: `yield/await/send/throw/close/delegation` is covered by the Interpreter contract; bounded DirectNative delegation is differentially verified; unproven graphs are explicit Interpreter-only or `AotSwitchUnsupported` |
| Complex handler CFG | v5 data-driven exact-shape proof; no generic executor | v5 CFG proof/lowering with deterministic rejection for every unproved graph; editor uses the explicit Interpreter path | Typed-IR exception CFG validation, malformed-handler negative unit, generated package/scene smoke, AOT-only symbol audits, and nested-handler Interpreter hot-update smoke pass | Closed for production and editor targets; unproved graphs remain Interpreter-only or deterministic rejection |
| Hybrid adoption | Interpreter-only editor boundary plus bounded DirectNative canonical transfer | Versioned canonical continuation contract with module/version/artifact identity, owner transfer, pending-await and terminal cleanup | Continuation identity/no-replay unit tests, v5 cross-backend smoke, Windows/Web generated/browser smoke, wrong-owner and partial-adoption negative tests | Contract Complete: bounded adoption is explicit and ownership-safe; all non-canonical/general adoption requests fail closed or remain Interpreter-only; no Hybrid dispatcher remains |
| Optimized-PC deoptimization | No arbitrary native-PC contract; cooperative operation-boundary only | Canonical operation-boundary materialization with complete locals, stack, handlers, captures, caller chain and side-effect state | Continuation materialization unit tests, arbitrary-PC rejection tests, no-replay differential, Windows/Web platform smoke | Contract Complete: operation-boundary transfer is the only supported deopt contract; arbitrary native-PC requests are deterministic `AotSwitchUnsupported` |
| Editor hot update | Interpreter-only editor service with exact artifact/host freshness boundary | Interpreter-only editor service that binds exact source/dependency/host identities and rejects stale replacements | Freshness/version/owner negative tests and editor hidden-window smoke | Closed for editor hot-update migration; `InterpreterHotUpdateService` is isolated from retired Legacy, binds complete artifact/host identities, configures VM/global/memory ownership before publication, rejects incomplete/stale replacements, and passes Windows Debug/Release hidden-window smoke |
| v1 code generation | None | All production and editor hot-update consumers generate/use v5 or Interpreter artifacts | `arp_script_aotc` links only v5 tooling/compiler runtime; strict symbol audit, generated-package smoke, and Interpreter hot-update package-free smoke pass | Closed for production and editor hot update; no v1 generator remains |
| Generic executor | None | All supported production shapes use v5; unsupported editor shapes use the explicit Interpreter path | AOT-only and v5 target audits reject generic executor symbols | Closed for production and editor targets; unsupported shapes have no compatibility wrapper or silent fallback |

## Legacy-Retirement Gate

The full-retirement criteria are `Contract Complete`: `src/scriptvm/aot/legacy/`
and its migration targets are absent, no public header exposes v1 descriptors,
no production/editor target links a legacy runtime or Hybrid dispatcher, and
source/symbol/package inventories have negative assertions. The remaining
unsupported behavior is intentionally represented by the versioned
Interpreter-only contract or deterministic `AotSwitchUnsupported`; there is no
compatibility wrapper or silent fallback.

Acceptance evidence on 2026-08-25 covers the current bounded v5 lane:
Windows Debug and Release pass the build-graph/source-boundary contracts, v5
unit contract, AOT-only link smoke, generated-package smoke, scene behavior
smoke, and dynamic-package smoke. The explicit `arp_scriptvm_editor_runtime`
also passes its hidden-window freshness/owner negative smoke in both Windows
configurations. Web Debug and Release pass the corresponding Wasm
build-graph/source-boundary, v5 unit, generated-package, scene, plugin
reflection/catalog, source-inventory, and browser smokes in complete 14/14
CTest selections.
The dynamic-package smoke covers truncated-DLL repair, completion-marker
recovery, lexical relocation, manifest-budget identity variation, and both
thread and separate-process concurrent generation; Debug and Release both
passed. These are bounded
`Platform Validated` results only. The open generator protocol, Hybrid
adoption, arbitrary-PC deoptimization, and mobile platform rows keep the
overall state at `Implementation In Progress`, never `Production Ready`.

The 2026-08-26 quota audit replaced the former inline-byte plus allocator-byte
sum, which double charged generated frames. `CpuMemoryQuota` is now the single
budget fact source; frame outer allocations use lease/precharge or atomic
replacement transfer, while frame count remains independent and
`continuationFrameBytes` is telemetry. Retained external Value graphs are
cycle-safe and identity-deduplicated. This strengthens the v5 memory contract
but does not promote the overall system to `Production Ready`.

The Web 14/14 reruns first caught stale reflection JSON and static plugin
catalogs in reused build directories. Rebuilding all five static plugin targets
regenerated the current manifest header/service contract; the focused plugin
contract passed 1/1 and the complete Debug and Release selections passed
14/14. The freshness correction is part of the acceptance evidence and does
not change the open migration rows.

The same audit now covers canonical Interpreter continuation identity on both
generator and ordinary suspended-await adoption. The semantic runtime unit
constructs a generator through the public call path, resumes it after a
snapshot without replay, and rejects mismatched receiving module identity;
ordinary suspended-frame adoption rejects both module/version and complete
artifact-identity mismatches. These are bounded adoption guards only and do
not close the full Hybrid adoption, cross-backend generator, or arbitrary-PC
deoptimization rows.

The bounded cross-backend direction is now explicit as well: the optional
fixed-capacity composite `GeneratorBridgeV2` provider registry lets the
DirectNative runtime register an optional provider and lets an
Interpreter `yield from` consumer resume an AOT child, forward `send()` and
handler-caught `throw()`, and forward `close()` while preserving child
completion and no-replay behavior.
The semantic runtime unit covers this path on Windows Debug/Release and Web
Debug/Release. The bounded reverse direction now also admits a statically
proven DirectNative child from a declared module dependency; Interpreter
children, delegated await/throw, and the full protocol remain open and keep
the generator and Hybrid rows open.

The versioned `GeneratorBridgeV3` contract is now present without changing the
V2 ABI. Interpreter and DirectNative providers expose a complete
module/version/artifact identity only when their owner metadata is explicit;
unit tests cover both positive queries and missing-token rejection. Generated
DirectNative yield-from code consumes the V3 provider only after an exact
identity match and otherwise returns `AotSwitchUnsupported`. This closes the
identity-query contract slice. Source-level reverse admission is now covered
for the bounded DirectNative dependency shape; delegated await/throw and the
full generator protocol remain open.

The final Windows Release acceptance rerun on 2026-08-25 completed the full
`arp_scriptvm_*` selection at 25/25 after rebuilding previously absent plugin
and stress targets. Dynamic package generation also passed in Release after
exporting the optional `GeneratorBridgeV2` provider symbol from the host DLL.
This is additional bounded platform evidence; it does not close the reverse
delegation beyond the bounded DirectNative dependency shape, full generator protocol, arbitrary-PC
deoptimization, mobile-platform, or eight-hour endurance requirements.

The 2026-08-25 reverse-delegation fixture now covers a statically proven
cross-module DirectNative child. Admission requires an explicit
`generatorBridge` ScriptFunction binding, fixed-arity agreement, and an exact
module/version/artifact identity from the package dependency table. The parent
forwards resume/send/throw through the child without replay, and generated and
dynamic-package Windows Debug smoke plus Web Debug generated smoke pass. This
closes only the bounded DirectNative dependency slice; Interpreter children,
dynamic or cyclic graphs, delegated await/throw, and the complete generator
protocol remain open.

The canonical continuation slice now materializes operation-boundary locals,
operand stack, captures, handler metadata, source line/column, and complete
module/function/artifact identity before snapshot. DirectNative adoption checks
the same identity and exact generated frame shape, rejecting foreign captures,
handler PCs/depths, caller chains, and missing source metadata deterministically.
Generated production payloads use the stable `aot:<function-id>:pc:<pc>` source
label rather than embedding host `.arp` paths; this preserves diagnostic
location data while satisfying the Web source-inventory contract. This remains
an editor/differential bounded slice and does not close full Hybrid adoption or
arbitrary-PC deoptimization.

The reverse bridge contract now has a real Interpreter child unit fixture in
`arp_scriptvm_aot_v5_tests`: a DirectNative parent obtains a VM-owned
`ScriptGenerator` through the explicit module-export callback, negotiates the
same module/version/artifact identity through `GeneratorBridgeV3`, and forwards
resume, `send()`, `throw()`, and `close()` without replay. This is bounded
runtime evidence only; generated production payloads, delegated await/throw,
dynamic or cyclic graphs, and the complete generator protocol remain open.

The bounded reverse pending-await slice is now explicit: an admitted
DirectNative parent may forward `send()`/`throw()` to a real Interpreter child
whose suspended `ScriptAwaitable` exposes the versioned `inject` callback. The
injected value is consumed once at the original await boundary; adoption,
close, and child-completion paths clear the pending injection state. The unit
fixture asserts that await polling remains at one call and that the child
completion result is not replayed. This closes only the pending-await injection
slice; general reverse adoption and the full generator protocol remain open.

The reverse pending-await slice now has production-shaped generated-package
evidence. `arp_scriptvm_aot_v5_cross_backend_smoke` runs the generated `basic`
package, hands a VM-owned Interpreter child through the explicit module-export
callback, and verifies pending `send()`/`throw()`, missing-injection rejection,
poll no-replay, and `close()`. Windows Debug/Release and Web Debug/Release all
pass this smoke. The complete ScriptVM CTest selections also pass in all four
presets (27/27, 26/26, 13/13, 13/13). This remains a bounded contract slice;
dynamic/cyclic graphs, delegated await/throw, general Hybrid adoption, the full
generator protocol, arbitrary-PC deoptimization, and mobile evidence remain
open, so the overall status is still `Implementation In Progress`.

The stable V3 reverse bridge ABI remains at version 1. After an audit found
that a generic `Suspended` child result was being treated as pending-await
injection support, providers now expose an independent, versioned
`GeneratorBridgeV3Capability` extension derived from the actual suspended
await state; generated parents consume that extension and fail closed when it
is absent. Windows Debug/Release and Web Debug/Release AOT unit plus generated
cross-backend smoke tests pass. This closes the capability truth-source slice,
not delegated await/throw or the complete generator/Hybrid migration rows.

The Windows dynamic-package staging cleanup was also hardened after the full
Release selection exposed a transient sharing violation during concurrent
generation. Cleanup now retries for a bounded ten-second window and still
fails deterministically after the window; partial staging/DLL output is never a
cache hit. Debug and Release dynamic-package smokes pass, including three
consecutive Release reruns, and the complete Release ScriptVM selection is
26/26.

The continuation audit on 2026-08-25 now rejects generator snapshots unless
the generator is started and currently resumable. Completed or closed
generators cannot be exported and replayed as a canonical continuation, and a
complete program artifact identity must match the requested module identity
instead of being overridden by caller-supplied parameters. Windows Debug and
Release semantic-runtime, editor, AOT-unit, and generated cross-backend tests,
plus the Web Debug semantic-runtime test, pass with these guards. This closes a
terminal/initial-state no-replay contract gap only; delegated await/throw,
general Hybrid adoption, arbitrary-PC deoptimization, and the full generator
protocol remain open.

The same continuation review now validates frame provenance, not just serialized
identity: `snapshotGeneratorFrame` requires the active frame `Function*` to be
owned by the selected `Program`. A foreign same-process canonical frame is
rejected deterministically with `AotSwitchUnsupported`. The negative semantic
runtime test passes on Windows Debug/Release and Web Debug, while the v5 unit
and generated-package smoke remain green across Windows/Web. Android/iOS
contract targets pass, but no device or emulator evidence is claimed.

The final focused audit after this guard passed the four-lane source boundary,
Windows Debug/Release build-graph and AOT-only link checks, Windows dynamic
package smoke, Web Debug/Release v5 contract checks, and deterministic package
inventory. The historical full selections remain 27/27, 26/26, 13/13, and
13/13. This closes the current structural and bounded contract work only; the
generator, general Hybrid, arbitrary-PC, mobile-device, and waived endurance
rows remain open, so the overall status stays `Implementation In Progress`.

The closing allocator pass replaced default-string path suffix construction in
dynamic-package and plugin process logs with filesystem-path composition. The
Windows Debug plugin unit and dynamic-package smoke passed after recompilation;
the change preserves the existing cache identity, staging, and diagnostic
contracts and does not alter the open migration rows.

The Interpreter-side delegated-await continuation now records and validates an
exact parent Yield PC when a child Await parks at RangeNext. send() and
throw() inject into the child once, deliver the resulting value directly to
that parent Yield, and clear the pending metadata; malformed lowering shapes
fail with AotSwitchUnsupported. The VM unit asserts the PC and no-replay
poll count, and the Windows Debug generated/cross-backend smokes remain green.
This is a bounded Interpreter protocol slice only; dynamic/cyclic graphs,
general delegated await/throw, Hybrid adoption, and the full generator row
remain open.

Static DirectNative delegation cycles are now admitted when the package has a
non-zero recursion budget. Each generated entry still passes through the
runtime recursion gate, so recursive graphs terminate with the stable budget
diagnostic instead of building an unbounded native call chain. Dynamic source
typing, delegated await/throw breadth, and the complete protocol remain open.

The generated-package recursion slice is now exercised end to end as well.
`basic` exports a recursive `yield from` entry: a shallow call yields and
terminates without replay, while a depth-100 call exceeds the declared
recursion budget and deterministically clears its native continuation frame.
Windows Debug generated and dynamic-package smokes pass with the package's
continuation-frame budget explicitly larger than its recursion budget, so the
observed failure is `RecursionLimitExceeded` rather than an incidental frame
quota failure. This is bounded runtime evidence only; dynamic source typing,
general delegated await/throw/close, and the complete Python generator
protocol remain open. The same dynamic assertion passes on Windows Release;
Web Debug and Release generated/Wasm smokes pass as well.

Interpreter delegated-await traversal now derives its depth budget from
`Vm::maxContinuationFrames()` instead of a fixed depth-two constant. The V2/V3
bridge callbacks use the caller-provided remaining budget and return a
deterministic overflow value when it is exhausted; depth arithmetic saturates
instead of wrapping. A VM unit covers a four-edge delegated graph (accepted
with budget 8 and rejected with budget 2), including a real pending-await
send() no-replay path, while the generated DirectNative recursion smoke remains
green. This expands only the bounded Interpreter protocol slice; delegated
cross-backend graphs, general Hybrid adoption, arbitrary-PC deoptimization,
mobile evidence, and the full generator protocol remain open.

The DirectNative V2/V3 provider now follows the same caller-budget overflow
contract instead of returning a historical sentinel. Its admitted depth remains
one for a native generator state; nested generated-child graphs are still
constrained by the existing static lowering and recursion gates.

Interpreter generator adoption now rejects arbitrary in-range PCs as well:
ordinary continuations must resume immediately after a declared `Yield`, and a
delegated child-await continuation must resume at its declared `RangeNext`
retry boundary. The semantic-runtime unit covers the negative arbitrary-PC
case. This is a fail-closed boundary guard, not arbitrary-PC deoptimization or
general Hybrid adoption.

The DirectNative delegated-child canonical audit now carries the bounded native
ownership contract. A `yield from` parent records its source local, exact
`RangeNext` PC, pending parent `Yield` PC, and child-awaiting state after
rolling back the iterator index. A parent `Yield` publishes a post-yield
canonical frame that retains the child `Value`; same-backend adoption restores
that child and resumes it without replay. DirectNative/V3 identity and
pending-await capability are validated before execution. Incomplete metadata,
foreign identity, and non-resumable children remain deterministic
`TypedIrInvalid`/`AotSwitchUnsupported`.
