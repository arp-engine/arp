# BindlessCross/1

## Metal public prepared Graphics buffer scope (2026-10-15)

The existing PreparedGraphicsBinding/1 and GraphicsPassFaultScope/1 contracts
also apply to an explicitly opted-in Metal device. `enableBindlessGraphics`
defaults to false and is independent of Compute and ordinary argument buffers.
Both Graphics versions may become 1 only with native runtime, Tier2 argument
buffers, native completion, a nonzero issuing device and queried buffer limits.
Disabling argument buffers closes both versions. Null retains Validator scope;
Vulkan and WebGPU keep their independently gated existing adapters. None of
these choices changes OriginalGraphicsExecution or Root/2 execution capability.

The admitted Metal scope is the sealed single-set Vertex or Vertex+Fragment
buffer ABI already defined below: UBO load, read-only storage load/query, and
Fragment writable-storage query/atomic only. Metadata/fault buffers count toward
the same per-stage limits. Images, extra sets, indirect and mixed Compute modules
remain rejected. Capabilities describe exactly these stages and operations.

Authorization validates exact package/snapshot/issuer/registry identity and all
selected module stages. The authorized factory retains the authentic package
owner, never an alias with the authorization's control block. Preparation uses
the existing immutable typed owners and bounded preparation pool. Public primary
preparation clears the fault latch before the render pass; Render binding consumes
the same owner and ordered accesses. Existing queue completion retains typed and
native owners through the real callback, publishes fault/readback, and releases
pool accounting only at completion or unsubmitted cancellation. No new graph,
journal, completion simulation or native handle is introduced to the upper layer.

Acceptance requires public factory/preparation/record/submit and exact image,
atomic and fault readback on Metal, caller-drop while a real GPU timeline wait is
pending, cancellation, rejected ownership/budget/scope cases, zero hot bind/draw
allocations and native capture. This scope is not full scene or Root/2 acceptance.

Scoped Platform Validated: Apple M4 public color/Vertex-only depth, exact2x2
RGBA/D32, counters102/202 or unchanged100/200, fault0/1 in both color and
Vertex-only depth (no Fragment forwarding), typed/native completion
retention, cancellation, budget rollback and hot allocation0 pass. Public color
and depth native captures/readback are retained; no independent replay claim.
Mac9/9 and Windows3/3 contracts/Null smoke, source417/417 and style5/5 pass.
Current scope/source/runner/package/evidence index:
`build/bindless-metal-public-graphics1/current-report.json`.

## Historical main-engine checkpoint

Current main-engine checkpoint (2026-10-13): Graphics8/GeneralSubmission3/Output3
runtime integration is applied to `D:/arp` (18 runtime files). Assertion-enabled
MinGW contracts/Null smoke pass4/4; structural guards pass3/3, including354
inventory cases. The main-source Wasm build uses no production overlay or forced
candidate header. Real Chrome passes six numerical and two caller-drop cases8/8
on one unchanged payload; exact output, final native-scope retention and retirement
are checked. Evidence: `build/bindless-graphics-integration1/validation.json`.
This is scoped private Graphics Backend Validated evidence. Complete Graphics
lifecycle, public Emulated, required Native coverage, renderer/WOCC P4-P6 and
Platform Validated/Production Ready remain open. All31 frozen worktree inputs
are unchanged; historical checkpoints below do not supersede this boundary.

Graphics8 copied record-only native-link checkpoint (2026-10-13 03:39 Asia/Shanghai):
all9 affected library TUs plus the assertion-enabled entry compile from selected
copied headers. The configured emdawn link preserves114 exact members across six
copied archives and produces JS/Wasm/data.3844 inputs are preserved before compile;
reader2 independently verifies10 objects,114 members, exact four-backend record-only
decisions/original recorder reuse and seven copied module origins from D:/wocc.
Copied tests5+5+3 pass;4146 selected artifacts are preserved. See
build/gfx-execution8-recording-link1/native-link-evidence.json and
graphics-execution8-recording-link-independent-replay2.json. This closes selected
copied compilation/link evidence only; no application/registration/native preparation/
recording/GPU/public/full SDK/platform/production authority follows. Callback24/frozen6,
original libraries/payload, quotas/memory/deadlines unchanged. Compute198 original
PID23628 live192/0,complete=false at03:39:47; six numerical cases and complete ordered
Compute/cohort/final/Callback96/runtime/integration plus WOCC P4-P6 remain open.

Copied Graphics Execution8 record-only checkpoint (2026-10-13 03:18 Asia/Shanghai):
the independent successor reuses exact Issuer8 and original recorder/emitter.
WebGPU private explicit Prototype recording is selected only in the copy;
submission remains false with RHI-ORIGINAL-GRAPHICS-EXECUTION-WEBGPU-SUBMISSION-MISSING.
Null validator/Vulkan/Metal refusal and versions1-7 stay unchanged. Full legacy
fixture,21840-line differential,source364/364,package/root4/4 each and registered
Graphics Null1/1 pass. Actual Host/native backend TUs compile; Host record rejects
missing original owner and submit rejects before ownership.3998 selected artifacts/
1370 dependencies independently replay from D:/wocc with seven archived modules.
See original-graphics-execution8-recording-contract.md and
build/gfx-execution8-recording-evidence1/validation.json. Application/registration/
native preparation/native recording/GPU/public/full SDK/platform/production remain
false. Callback24/frozen6, original library/payload, budgets/deadlines remain
unchanged. Compute198 original PID23628 is live:175/0,complete=false at03:18:04.
Complete Compute/cohort/final/Callback96/runtime/integration and WOCC P4-P6 stay open.

Copied Graphics Execution8 L2/refusal checkpoint (2026-10-13 02:57 Asia/Shanghai):
exact Issuer8/opt-in/synchronization and rejection of legacy completion/submission
profiles now pass assertion-enabled MinGW plus the complete legacy execution
fixture. Legacy differential21840 lines is unchanged; copied source364/364,
package/root4/4 each and registered Graphics Null1/1 pass. Actual copied Host
backend record/submit entries refuse8 before ownership/work; original libraries
remain unchanged. Selected3286 artifacts/646 dependencies independently replay
from D:/wocc with seven copied reader origins. See original-graphics-execution8-contract.md;
evidence: build/gfx-execution8-evidence1/validation.json. This copied candidate
is not applied/registered and all native preparation/GPU/public/full SDK/platform/
production gates remain false. Callback24/frozen6, budgets/deadlines and existing
payloads are unchanged. Compute198 originalPID23628 remains live:159 accepted/0
failures,complete=false at02:57:14. Complete Compute/cohorts/final204+6,Callback96,
actual Graphics preparation/record/draw/submit/completion/output, necessary Native,
D:/arp integration and renderer/WOCC P4-P6 remain required; full goal incomplete.

Copied Graphics Issuer8 native-link checkpoint (2026-10-13 02:32 Asia/Shanghai):
the finite configured graph is115 objects/114 VALID library members plus one
unbuilt entry. All nine affected library TUs and assertion-enabled Graphics entry
are now compiled together, and copied archives/native emdawn link emit JS/Wasm/
data with Node syntax pass. Copied reader3 independently verifies10 objects,
114 archive members,3843 prebuild files and seven import origins from D:/wocc;
tooling5/5,metadata3/3,source363/363,package/root4/4 each and existing Graphics
Null1/1 pass. First unbuilt-entry inspection and MAX_PATH copy failures remain
saved. Evidence: build/gfx-issuer8-link2/native-link-candidate.json and
graphics-issuer8-link-independent-replay3.json. This closes copied native linking,
not native preparation, GPU execution or SDK closure. No live application/CMake
registration or public/platform/production promotion. Callback24/frozen6 and
original libraries/payloads/native budgets/deadlines remain unchanged. Compute198
originalPID23628 is live:140 accepted/0 failures,complete=false at02:32:00.
Complete Compute/cohort/final,Callback96,runtime issuer/Graphics/public,required
Native,actual engine integration and renderer/WOCC P4-P6 remain open. See
original-graphics-issuer8-link-contract.md for exact scope and evidence.

Copied Graphics Issuer8 report checkpoint (2026-10-13 02:00 Asia/Shanghai):
explicit issuer selector uses a dedicated preparation runner and validator;
two genuine upstream preparation chains and one retention/retirement must match
exact families and ordering, without folding duplicates into Callback8 success.
Nine report/CLI/Node tests, legacy13/13, source363/363, package/root4/4 each and
unchanged registered Graphics Null1/1 pass. Selected2591 artifacts independently
replay from D:/wocc with9+13 report tests rerun. First malformed-family and late
second-chain regressions remain saved. Evidence: isolated
graphics-issuer8-report-evidence3/validation.json and
graphics-issuer8-report-independent-replay1.json. Parent native workload input
bytes are unchanged; this is copied report conformance, not new native compile,
successful native preparation, record/submit/output or capability evidence.
Callback24/frozen tools6 remain sealed; application/registration, SDK closure,
GPU/public/platform/production gates stay false. Compute198 originalPID23628
is live with115 accepted/0 failures,complete=false at01:59:58. Complete Compute/
cohort/final,Callback96,issuer/Graphics/public,required Native,actual engine
integration and renderer/WOCC P4-P6 remain required. See
original-graphics-issuer8-report-contract.md; the original plan is incomplete.

Copied Graphics issuer workload checkpoint (2026-10-13 01:33 Asia/Shanghai):
candidate2 explicitly declares additional3 before Source and reuses existing
Graphics initialization/root/closure/freeze/payload/cold/callback and issuer
conformance helpers. Genuine recapture drops old owners, rebuilds State/Stream/
root from original Inputs and reuses exact original Submission/Callback quotas;
unchanged cold recapture and stale-after-rollback recapture share one factory.
Actual assertion-enabled Emscripten native Graphics entry TU compiles with1175
dependencies; source363/363, package/root4/4 each and existing Graphics Null1/1
pass. Selected3745 artifacts independently replay through archived readers.
See original-graphics-issuer8-workload-contract.md and isolated
graphics-issuer8-workload-evidence2/validation.json plus
graphics-issuer8-workload-independent-replay1.json. Shared Callback24/frozen tools6,
CMake/payload and budgets stay unchanged; first guard and patch-EOL failures are
saved. Compilation is not actual preparation/rollback/caller-drop/retirement
validation. Application/registration, SDK provenance and GPU/public/platform/
production remain false. Compute198 original Python23628 remains live with96
accepted/0 failures,complete=false at01:33:41. Complete198/cohorts/final204+6,
Callback96,real issuer/Graphics/public execution,required Native,actual engine
integration and renderer/WOCC P4-P6 are still required.

Copied Issuer8 preparation checkpoint (2026-10-13 01:14 Asia/Shanghai): a separate
candidate2 now passes assertion-enabled four-backend/legacy contracts, actual
MinGW Host and Emscripten emdawn native-branch backend TU compilation, copied
Host archive linkage and backend rejection/legacy tests. Source inventory362/362,
package/root4/4 each and existing Graphics Null1/1 pass. Selected3921 artifacts/
1326 distinct dependencies independently replay from D:/wocc through archived
readers. See original-graphics-issuer8-preparation-contract.md and isolated
build/bindless-continuation/graphics-issuer8-preparation-evidence1/validation.json
plus graphics-issuer8-preparation-independent-replay1.json. Initial stale8 refusal
and SDK Python missing-yaml failures remain preserved. This changes copied
WebGPU8 preparation lowering only, keeping record/submit/NativeExecution8 closed.
Original Callback24/frozen tools6 and libraries are verified unchanged. Actual
sources/CMake/payload are not applied or registered; real native preparation,
complete SDK provenance and all GPU/public/platform/production gates remain
false. The separately authored full issuer workload and real preparation/rollback/
sticky claim/caller-drop/retirement tests remain pending after Callback96.
Compute198 original Python23628 remains live:81 accepted/0 failures,complete=false
at01:14:24. Complete198/cohorts/final204+6,Callback96,Graphics execution,required
Native,actual D:/arp integration and renderer/WOCC P4-P6 are still required.

Selected dependency checkpoint (2026-10-13 00:40 Asia/Shanghai): actual final
probe's three Ninja dependency inventories now have separately preserved bytes:
1353 unique dependencies/1374 selected artifacts independently replay from
D:/wocc. Eight reader import origins are verified inside the11-file verifier
copy. Final copied6/6 negatives, existing archive13/13/build6/6, package/root4/4
each and existing Graphics Null1/1 pass. Evidence: compute-probe-dependency-bytes1/
validation.json and compute-probe-dependency-independent-replay3.json. See
bindless-webgpu-build-input-provenance-contract.md. This after-build snapshot
does not prove original compiler inputs or complete transitive SDK provenance.
Callback24/frozen tools6, production sources/payloads, budgets and all public
gates remain unchanged. Compute198 native2 Python23628 remains live:56 accepted/
0 failures,complete=false at00:40:53. Full198/cohorts/final204+6,Callback96,
backend issuer/Graphics,public Emulated,required Native,integration and WOCC P4-P6
remain open.

Issuer8 copied-value checkpoint (2026-10-13 00:17 Asia/Shanghai): assertion-enabled
MinGW candidate3 compile/run and full legacy fixture pass; copied inventory362/362,
package/root4/4 each and existing Graphics Null1/1 pass. Selected3125 artifacts
including530 compiler dependencies independently replay from D:/wocc through
five archive-only readers; see graphics-issuer8-value-evidence2/validation.json
and graphics-issuer8-value-independent-replay2.json. First Windows dependency
parser failure is preserved. Shared Callback24/frozen tools6 are unchanged;
Issuer8 is not applied/registered, native preparation/record/submit stay closed.
Compute198 native2 Python23628 remains live:38 accepted/0 failures,complete=false
at00:17:48. Complete Compute/cohort/final204+6,Callback96,backend issuer/execution,
public Emulated,required Native,actual D:/arp integration and WOCC P4-P6 remain open.

Selected CPU increment preserved at23:39:48 files independently hash-replayed,
separate probe build archive manifest unchanged. Archive is
build/bindless-continuation/compute-final-probe-history-selected1/manifest.json;
all public/runtime/platform/production gates false. Compute198 native2 Python23628
is confirmed live,8 accepted/0 failures,complete=false. No whole-plan promotion.


Current checkpoint (2026-10-12 23:35 Asia/Shanghai): actual final probe build3
and selected build evidence3 pass. Replay initially refused incremental Ninja
history, then the equality candidate refused real edge/deps timestamp differences;
both failures are retained. Latest history/source-label contracts pass13/13,
build6/6, package/root4/4 each and existing Graphics Null1/1. A copied verifier
and all9 imported files replay the actual26 build artifacts from D:/wocc:
compute-final-probe-archive-relocated-replay1.json. Exact recipes, symbols,
deps1034/1349/1038 and tested payload hashes match; JS syntax passes. SDK
transitive provenance, final GPU matrix and all public capabilities stay false.
The former Compute198 attempt failed146/1/51 at Chrome profile cleanup WinError32;
one unchanged-driver/payload diagnostic retry passes, without matrix promotion.
A new independent full198 attempt runs asPython23628/session35305,5 accepted,
0 recorded failures,complete=false at23:35. Its source preflight proves1593/1602
inputs and198 registrations; current Callback8 compiled inputs24/24 unchanged.
No frozen canonical tool, CMake, Graphics source/payload or native budget changes.


Probe archive replay history contract (2026-10-12): Ninja v7 logs retain prior
incremental builds. Each selected output must have a latest successful record;
its successive recorded mtimes must strictly increase. Exact duplicated or
regressed rows, missing outputs, malformed/unsupported schemas reject. Replayed
object dependency mtimes must not predate the latest selected object record;
the final link record must not predate the selected dependency mtimes. These
two Ninja inventories capture distinct timestamps (observed on real v7 output),
so equality is not assumed. Keep the complete archived history,
exact verbose execution commands, object symbols and payload hashes. Do not
compact/rewrite the archive or use an older matching dependency row to bypass a
newer mismatch. This CPU evidence contract does not establish transitive SDK
provenance, runtime completion or public bindless availability.

Archive-only source ownership is derived from the copied CMake cache's single
absolute CMAKE_HOME_DIRECTORY and checked against the archived CMakeLists,
selected sources and exact compiler commands. A relocated verifier must not
infer the original source label from its own installation directory. Missing,
duplicate, relative or substituted source roots reject; no live original source
or build directory supplies missing evidence. The logical label supplies only
identity, never native authority. Copied-tool replay remains CPU evidence.

<!-- WINDOWS_CONTINUATION_20261012_START -->
Current Windows checkpoint (2026-10-12): current-source Release/UNDEBUG Wasm
and MinGW contracts were rebuilt; source inventory354/354, package4/4,
general/content/Null smoke4/4 and fresh readonly/Graphics artifacts2/2 pass.
Fixture-specific LF checkout rules preserve exact authored shader bytes. Initial
failures and the previous browser payload remain independently preserved.

Real Chrome Claim30/30 now passes on the frozen ordinary backend payload.
Independent strict stage replay and digest audit accepted30 reports and archived
1612 artifacts/49036285 bytes, including unchanged selected live inputs:
build/bindless-goal-20261012/claim-native1-audit/validation.json.
The original runner did not freeze driver-only shader publication metadata;
shaderMetadataFrozen=false records this limit. This is selected Claim evidence,
not affected-regression, aggregate Lifecycle4 or complete package provenance.

The shared native evidence checker validates all204 terminal and6 numerical
schemas against original preparation/content/receipt/retirement markers, exact
Boolean fields and the existing bounded progress transport. Synthetic hostile
tests grant no GPU proof. Matrix acceptance now consumes this checker, correcting
reverse-scopes numerical submission and physical-loss report identity handling.
The browser driver consumes the same checker and now routes the independent
PostSuppression selector with all six lifecycle choices mutually exclusive.
All12 type/range/failure routing combinations pass intercepted-launch tests;
the native probe remains default OFF and no PostSuppression GPU acceptance follows.

Registered tooling/source/package/artifact checks pass7/7; Null hidden-frame
smoke1/1 and the final matrix-runner tests3/3 pass. Ordinary browser five-file
payload hashes remain identical to accepted Claim. Fresh Compute-default198
(192 terminals plus6 numerical) is running sequentially on this uninstrumented
payload at build/bindless-goal-20261012/compute-default-native1/. Its successor
runner also freezes all generated fixture files, CMakeCache and used untracked
evidence modules. Do not mutate its frozen inputs or served payload while live.

Independent successor tooling was prepared without modifying those1593 frozen
inputs. Read-only actual Ninja commands/queries, object dependencies and LLVM
symbols verify the ordinary backend has no probe definitions and its smoke has
no linked probe object. Ordinary/main dependency counts are1033/1346. The archived
inspection remains incomplete because an executed verbose fresh-build log was
not available; configured recipes alone cannot establish executed link provenance.
The probe compiler/link/symbol rejection tests pass6/6. A subsequent probe build
must capture its real verbose commands, original uninstrumented backend object,
single explicitly instrumented wrapper and exact tested browser payload.

Copied-stage predecessor replay verifies every selected digest, reconstructs the
original acceptance from archived files and revalidates all native profiles.
Shader publication binding uses the authoritative reader on copied publications
and payloads, then checks every driver's four package identities. Its8 hostile
tests pass, including rehashed report/payload substitution, missing archived
payload with a live original present, copied archive replay after original removal,
duplicate artifacts and forged semantic profiles. Claim's copied1612 artifacts
replay successfully but remain ineligible as a complete aggregate predecessor
because shader publication inputs were not frozen. This is not filled from host
files. Fresh running Compute's first20 completed reports bind all four packages;
all1593 frozen input hashes remain unchanged. Partial observations are diagnostic
only: no predecessor/aggregate acceptance or execution capability is published.

Exact predecessor cohort archive tooling is now prepared independently of the
frozen matrix. It copies and replays the complete audited stage, original native
reports and archived shader publications/payloads before projecting Execution7(96),
Recording4(24), Submission4(48), Claim4(30) or PostSuppression4(12). The projection
is neither an additional GPU run nor aggregate authorization. Removed originals,
rehashed semantic-profile substitution, missing/duplicate artifacts, changed
package identities and final-run self-predecessors are checked. Nine focused
hostile/replay tests and Null hidden-frame smoke1/1 pass. The real earlier Claim
archive correctly refuses FINITE-COHORT-PACKAGE-PROVENANCE without creating an
acceptance, since its shader metadata was not frozen. Independent final-cohort
comparison still leaves affected-regression, actual-probe-build, final-rerun and
aggregate gates false; those remain mandatory separate evidence.
Latest partial observation binds33 completed reports to all four frozen packages
and verifies1593 live input hashes unchanged. Evidence is under
build/bindless-goal-20261012/: cohort-archive-tests2.log, cohort-null-smoke1.log,
claim-cohort-refused1.log and compute-default-partial-package-evidence2.json.
The new cohort tool/test are unregistered until the running matrix terminates.

The independent Release/static probe build at
build/web-webgpu-release-post-suppression-20261012 completed successfully.
Actual verbose compiler/link execution, ordinary/wrapper object and library
symbols, Ninja recipes and dependency inventories were archived and independently
replayed. The ordinary library has no probe definition; exactly one instrumented
wrapper is linked. Dependency counts are1033/1347/1037;26 artifacts are copied.
Fresh readonly/Graphics artifact tests2/2 and four package identity comparisons
pass. Probe archives: probe-build-evidence1/validation.json and the later explicit
Chrome-configured probe-build-evidence2/validation.json. Copied-build replay
probe-build-archive-replay2.json succeeds but preserves
liveDependencyFreshnessRechecked=false and all runtime/public/aggregate gates.

CTest now registers exactly12 PostSuppression cases. A diagnostic-only backtrace
node-ID change between full/filtered inventories initially blocked selection;
the runner now ignores only that field while retaining exact commands, properties
and case coverage. Initial missing-browser, invalid-regex and selection failures
are preserved. The actual fourth attempt finished: real Chrome commit6/6 pass,
publication6/6 fail; CTest exit8 after909.27s. Failure is not native validation
success. Original map/copy drain, invalidation and final retirement markers exist,
but the required MAP4_UPSTREAM_RETIRE marker is absent in every publication case.
The native fixture classifies finiteMap only by general-map-terminal and omits
the post-suppression publication selector. It emits the generic EXECUTION7 marker
instead; neither marker substitution nor report-success coercion is accepted.
All12 reports/browser logs/JUnit outputs are copied inside
post-suppression-native4/cases, with nonauthorizing case-files.json and
post-suppression-native4-outcome1.json. No stage validation.json was created.

A hash-bound repair candidate is prepared at map-selector-candidate1/: it adds
only publication selection to that existing map route, without changing any
runtime mechanism, budget or evidence requirement. Actual Node evaluation
reproduces the old mismatch and passes8 candidate route checks; git apply --check
passes. The candidate is not applied, compiled or GPU validated while the same
ordinary matrix still freezes this fixture. Its1593 original inputs are unchanged.

The independent dedicated native-attempt archive auditor replays copied probe
build evidence, full/filtered inventory, exact invocation/CTest/JUnit, report
markers, optional nonauthorizing case captures and all four archived shader
publications. Eight hostile/replay tests pass. The cohort tool now consumes this
distinct PostSuppression audit without pretending it is a complete ordinary or
final matrix; other cohorts reject, copied originals may disappear, and separate
final profiles are compared. The refreshed eleven-case cohort suite passes,
including the nonauthorizing case captures. Runner
selection/failure-capture tests8/8 and Null hidden-frame smoke1/1 pass. These are
tooling results, not actual PostSuppression stage or public execution acceptance.
At this checkpoint ordinary198 has103 accepted reports,0 rejected and no final
validation.json; its original Python process16672 is confirmed live. Its source/
CMake/build-cache/payload must stay unchanged until that same attempt terminates.

Complete ordinary198/predecessor/affected audits, then apply the prepared fixture
repair, rebuild/archive the independent probe and rerun all PostSuppression12.
The opted-in final same-payload204+6 must be independent of its predecessors.
Affected-regression/predecessor audits, accepted PostSuppression12 and final opted-in
Compute204+6 remain pending. Public WebGPU stays DiagnosticFailure with
executablePayload=false. ContentPlan6 mixed root/native Graphics, generic public
coverage, Metal public Graphics, renderer/WOCC P4-P6 and platform/visual/Release-
performance/server/production remain incomplete. Newer historical archives cited
below are missing on this host; their native evidence has not been freshly reissued.
<!-- WINDOWS_CONTINUATION_20261012_END -->

<!-- FINITE_GRAPHICS_INITIALIZED_STREAM_CURRENT_START -->
Selected private initialized Graphics Inputs6/State5/Sequence5/Stream5/Usage6/
Dependency6 capture is Backend Validated (2026-10-12). Authenticated completed
two-mip sampled and independent exclusive Uninitialized RGBA16F attachment owners
retain two explicit pre-pass transitions. The actual fourteen-command stream has
scope2..8, references/descriptors9, observations11/ranges10, represented cells15,
steps17 and implicit8. Usage has7 contributions/7 texture-cell visits. Dependency
proves2 initial plus4 final records,24 passes/168 visits,21 pairs,14 endpoint
contributions and7 dependency-cell visits. The independent ten-command stream
proves precisely initial2/final0,14 passes/98 visits,21 pairs,5 endpoints/cells3;
state transition records are dependencies, including each sampled mip neighborhood.

Four real browser cases cover both buffer types and all six static/dynamic/whole
combinations without native errors. Exact/one-less/exhausted admission, hostile
transition/frozen-record mutations, descriptor-reset refusal, preflight-no-output,
original source/state/stream/upload/content quota retirement and hot allocation/
query0 pass. Sampled content stays Completed with native state CopyDst; attachment
stays Uninitialized/generation0 with state Undefined. Dry capture creates no mixed
ContentPlan6 root, clear, native Graphics draw, completion or output authority.

Immutable build/bindless-graphics-initialized-stream1/validation.json and audit.json
verify281 artifacts/162512874 bytes and7 predecessor manifests. Archived MinGW
contract replay and4 exact real report replays pass;124 missing-evidence and8
native-evidence mutations reject. Eight authentic earlier initialized-source/dry
usage reports cannot satisfy the new runner. Full current browser plus explicit
Compute/Graphics compositions pass O3/UNDEBUG C++17 syntax; registered MinGW
contracts/Null two-frame smoke3/3, source inventory354 and package4 pass. First
explicit-array compile failure and real initial-dependency runtime failure retain
sources, logs, payload and reports independently; no production validator changed.

Next: one authenticated atomic ContentPlan6 transaction deriving its profile from
the same Sources/Inputs/State/Stream/Usage/Dependency identities, independent
completed-control and attachment-cell plans, publication/apply/rollback and exact
journal/retirement evidence. Graphics native closure/freeze/payload, genuine draw/
queue/completion, separate storage/attachment output profiles, shader-buffer
initialization authority, six numerical cases and Graphics lifecycle remain open.
Claim30/30 passes and affected204 remains live on the unchanged frozen payload;
its archive, PostSuppression12/affected234 and final Compute204 plus6 numerical
acceptance remain pending. Generic/public/NativeSync/Metal/WOCC P4-P6/platform/
visual/Release-performance/server/production gates stay open. No full execution
capability is promoted by this checkpoint.
<!-- FINITE_GRAPHICS_INITIALIZED_STREAM_CURRENT_END -->

<!-- FINITE_GRAPHICS_CONTENT_SOURCES_CURRENT_START -->
Selected private finite Graphics initialized Source5 is Backend Validated
(2026-10-12): authentic original upload completes the two-mip sampled input;
an independent ContentCreation2 RGBA16F attachment remains exclusive,
Uninitialized and generation0. Four actual browser reports cover both buffer
types and all six static/dynamic/whole combinations with no native errors.
Original upload/issuer/content custody, stale-stamp and forged-owner refusal,
independent quotas, exact/one-less/exhausted admission, descriptor-reset refusal,
retirement and hot allocation/query0 pass. No mixed root, clear, draw or output
authority is created.

Immutable evidence: build/bindless-graphics-content-sources1/validation.json.
Independent build/bindless-general-finite-graphics-content-sources-audit.json
verifies146 artifacts/60381719 bytes, replays4 real reports, rejects40 missing
and8 native-evidence mutations, and rejects preceding Usage/Dependency reports.
Current full browser and explicit Compute/Graphics/content-source instantiations
pass O3/UNDEBUG C++17 syntax. Registered MinGW contract/Null smoke2/2, inventory354
and package4 pass. Initial stamp-assignment failure and C++20-capture-warning
build are preserved; the accepted payload uses named C++17 tuple references.

Next: capture sampled CopyDst->ShaderRead and attachment Undefined->
ColorAttachment pre-pass transitions with the real two-mip resources, then join
Inputs/State/Stream/Usage/Dependency into one atomic ContentPlan6 root. Older
eight/twelve-command dry reports do not validate these initialized resources.
Claim30/30 passes; affected204 is still running on its unchanged frozen payload.
Claim archive, PostSuppression12/affected234 and final same-payload Compute204
plus6 numerical cases remain pending. Graphics native/output/lifecycle, generic
coverage, complete public/NativeSync, Metal Graphics, WOCC P4-P6 and platform/
visual/Release-performance/server/production remain open. Historical blocks
below preserve their original checkpoint scope; no execution gate is promoted.
<!-- FINITE_GRAPHICS_CONTENT_SOURCES_CURRENT_END -->

<!-- FINITE_GRAPHICS_CONTENT_SCHEMA_CURRENT_START -->
ContentPlan6 mixed Graphics content value schema is Contract Complete
(2026-10-12); authenticated original mixed root remains unimplemented. Independent
completed-control/attachment-control/cell/step/visit ceilings cover Fragment
sampled RenderBinding and draw consumption plus original ContentCreation2
Clear/Store attachment intent. Exact prerequisite versions, whole sampled views,
resource separation, incomplete/overflow counts, per-control read/cell/Clear-Store coverage, hostile attachment inputs and
active/uninitialized cell-timeline refusal are tested. Existing completed-read4/5,
attachment2/3, Pending2 and finite ContentPlan5 transaction gates are unchanged.

Assertion-enabled standalone MinGW contract, Null two-frame hidden smoke, Wasm
O3/UNDEBUG syntax and registered CMake/CTest2/2 pass. Source inventory354 and
package-layout4 pass. Archived native replay and independent digest audit verify
41 artifacts/419274953 bytes including exact native library inputs and preserved
selected sources. Evidence: build/bindless-graphics-content6-schema3/validation.json,
audit.json (including registered CMake/CTest evidence). The first standalone native link failure
is preserved separately in build/bindless-graphics-content6-schema1; its missing
shader/transitive library inputs were corrected without changing runtime policy.

Claim targeted5/5 plus remaining25/25 now pass on the unchanged frozen payload;
affected204 acceptance is running. Claim archive, PostSuppression12/affected234
and final same-payload finite Compute204 terminals plus6 numerical cases remain
pending. Submission and all preceding Graphics archives remain unchanged.

Next: real completed sampled initialization and independent exclusive attachment
custody on the same actual Graphics sources, then atomic mixed-root state/content
planning and rollback. Native closure/freeze/payload, pending/lifecycle/completion
and separate storage/attachment output authorizations require independent joins;
/6 values grant none of these. Four-backend schema decisions keep every root/native
execution gate closed. Native Graphics, generic coverage, public/NativeSync,
Metal, WOCC P4-P6 and platform/visual/Release-performance/server/production remain
open. This checkpoint completes a value contract only.
<!-- FINITE_GRAPHICS_CONTENT_SCHEMA_CURRENT_END -->

<!-- FINITE_GRAPHICS_USAGE_CURRENT_START -->
Selected private finite Graphics Usage6/Dependency6 is Backend Validated for
original resource-backed dry projection (2026-10-12). Four actual browser cases
cover Uniform/ReadOnlyStorageBuffer with static, dynamic and whole intervals
(six combinations). The same original Graphics Sources and independent RGBA16F
attachment retain a twelve-command input: the original render pass plus four
post-pass memory dependencies and Finish. Source budgets are authored before
publication: references/descriptors7, observations9/ranges8, sequence cells9/
steps11 and implicit8. Independent exact State/Stream bounds still accept the
genuine eight-command program and require its empty dependency rejection.

Usage6 emits seven exact memory contributions: bind3, draw3 and attachment1,
all within original render scope0..6. Two sampler references remain nonmemory.
Dependency6 proves final4, endpoints7, pairs21, passes16/visits112 and texture
cells3. Exact Fragment and ColorAttachmentOutput stage/access coverage, frozen
buffer intervals and native original parent/view provenance are preserved.
One-less budgets, hostile frozen record mutations, preflight-no-output,
descriptor-reset invalidation, original quota retirement and hot allocation/
query0 pass without changing any production profile or validator.

Immutable evidence: build/bindless-graphics-usage1/validation.json. Independent
audit verifies 132 artifacts/44801299 bytes and preceding shader/input, Source5,
Inputs6/render and State/Stream snapshots. Four archived real reports replay
exactly;84 missing-evidence and8 native-evidence mutations reject. Four authentic
State/Stream reports cannot satisfy the Usage/Dependency runner. Current full
browser plus explicit Compute/Graphics/source/render/state/usage instantiations
pass O3/UNDEBUG syntax with source freshness. MinGW standalone and registered
CMake/CTest contracts pass and the archived executable replays successfully;
source inventory354 and package4 pass. Initial enum compile failure is preserved;
the fixture now uses the existing ColorAttachmentOutput vocabulary.

Claim quick6/6 and targeted5/5 pass; remaining25 then affected204 acceptance
is running on the unchanged packaged payload, with16 preserved authored inputs.
The first package-boundary failure and repaired ordinary/explicit probe wrapper
syntax evidence remain preserved. Submission138/affected66 is archived;
PostSuppression12 and full finite Compute Lifecycle204 plus six numerical cases
remain pending.

Content/root integration remains next. Current completed-content planning
rejects RenderBinding origin with RHI-COMPLETED-TEXTURE-READ-PLAN-UNSUPPORTED-USE.
The Graphics content composition must explicitly admit its original sampled
bind/draw content and independent attachment lifecycle before any native encoder.
Native draw/completion, separate storage/attachment output authorization, six
numerical draws, Graphics lifecycle, generic geometry/table/stage/operation/ABI,
public execution/NativeSync, Metal, WOCC P4-P6 and platform/visual/Release-
performance/server/production acceptance remain open. No execution gate is promoted.
<!-- FINITE_GRAPHICS_USAGE_CURRENT_END -->

<!-- FINITE_GRAPHICS_STATE_CURRENT_START -->
Selected private finite Graphics State5/Sequence5/Stream5 is Backend Validated
for original resource-backed dry capture/projection (2026-10-12). The explicit
Graphics entry composes the existing eight-command Inputs6 program and actual
independent RGBA16F attachment without changing a production profile. State5
retains six observations/five texture ranges, exact attachment and Fragment
bind/draw origins, twelve original registrations and the original owner union.
Sequence5 proves six represented cells/seven steps; Stream5 preserves separate
four-access bind and draw footprints, one pass/attachment and eight implicit
accesses. Samplers have no memory step. All bounds are authored before Source
publication. One-less/exhausted admission, independent quota domains, forged
ownership, preflight-no-output, hot allocation/query0, descriptor-reset terminal
refusal and original State/Stream retirement are verified.

Four actual browser cases cover Uniform/ReadOnlyStorageBuffer with static,
dynamic and whole intervals (six combinations), with empty native errors.
Immutable evidence: build/bindless-graphics-state1/validation.json. Independent
audit verifies 121 artifacts/40209173 bytes and exact preceding shader/input,
Source5 and Inputs6/render archives. Four archived reports replay exactly;
52 missing-evidence and8 native-evidence mutations reject. Four authentic
Graphics input-only reports cannot satisfy the State/Stream runner. MinGW
assertion-enabled standalone and registered CMake/CTest contracts pass; current
full browser and explicit Compute/Graphics/source/render/state instantiations
pass O3/UNDEBUG syntax with selected-source freshness.

Claim browser build/packaging and preserved16 authored inputs are complete.
Its first quick attempt failed package import resolution because the ordinary
backend included a test probe header. That disabled include moved to the
explicit test wrapper; the frozen non-probe payload is unchanged. Ordinary and
opted-in wrapper syntax, package4 and restored quick6/6 pass. Initial failure
and repair evidence are retained; Claim30/affected204 acceptance is running.
Submission138/affected66 remains immutably archived. PostSuppression12 and
full finite Compute Lifecycle204 plus six numerical cases remain pending.

Usage/dependency/content joins, native render encoder/draw/completion, separate
storage/attachment output authorization, six numerical draws, Graphics lifecycle,
generic geometry/table/stage/operation/ABI, public execution/NativeSync, Metal,
WOCC P4-P6 and final platform/visual/Release-performance/server/production
acceptance remain open. No execution or production gate is promoted.
<!-- FINITE_GRAPHICS_STATE_CURRENT_END -->

<!-- FINITE_GRAPHICS_RENDER_CURRENT_START -->
Finite Graphics Inputs6/RenderUsage4 checkpoint (2026-10-12; real resource-backed fixture passed4/4).
The shared scalar Source5 fixture can admit an explicitly counted independent
RGBA16F attachment parent/view before freezing its canonical Sources. The actual
Graphics pipeline and descriptor retain their original source positions; the
shared Source5 helper now accepts that authored pipeline position rather than
assuming the Compute-only six-resource prefix. The separate Graphics entry
captures BeginRenderPass, BindGraphicsPipeline, SetViewport, SetScissor,
BindRenderDescriptor, Draw, EndRenderPass and Finish with original Clear/Store
attachment arguments. Existing Inputs6 and RenderUsage4/Consumption4 contracts
are reused; no production profile version or capability is broadened.

The fixture requires nonzero render-binding and draw-consumption projections:
one binding, one draw intent, four exact Fragment accesses, two exact texture
view/parent subresources and one sampler reference. Selected buffer intervals,
frozen dynamic offsets, pipeline/set/revision/scope positions, missing-evidence
rejection, independent quotas, hot allocation/query0 and descriptor-reset
invalidation are checked. An empty render visitor cannot satisfy this entry.
The preserved first compile failure rejected default construction of sealed
scalar arguments; each command now uses the existing explicit capture API.
The preserved native failures exposed foreign source-quota attachment leases
and a missing text reservation. Attachments now use the original canonical
source domain; pass text and graph text bounds derive exactly from the authored
label. The existing ownership and text-budget contracts remain unchanged.

Immutable resource-backed input/projection evidence is
build/bindless-graphics-render1/validation.json. Independent audit verifies
92 artifacts/32829861 bytes and exact preceding Source5/input
snapshots. Archived runner replay accepts4 authentic reports and rejects
32 missing-evidence and8 native-evidence mutations. Four authentic source-only
Graphics reports cannot satisfy this render-projection runner. Full current
browser plus explicit Compute/Graphics/render instantiations pass O3/UNDEBUG
syntax with selected-source freshness; source inventory354 passes.

This is command-input/dry projection work. No native render encoder, draw,
submission, initialized content, attachment readback or published pixels are
authorized. The preceding immutable Source5 custody and shader/input archives
remain valid for their exact archived payloads. State5/Stream5, usage/dependency/
content joins, output authorization, six numerical draws, Graphics lifecycle,
generic coverage, public/NativeSync, Metal, WOCC P4-P6 and production remain open.
Submission138/affected66 acceptance is immutably archived. Claim has been
activated after its preceding archive audit and is building; its30 terminal
cases/204 regressions and PostSuppression remain unvalidated.
<!-- FINITE_GRAPHICS_RENDER_CURRENT_END -->

<!-- FINITE_GRAPHICS_COMPOSITION_CURRENT_START -->
Selected private finite Graphics Source5 custody is Backend Validated
(2026-10-12): four real browser cases cover Uniform/ReadOnlyStorageBuffer with
static, dynamic and whole ranges (six combinations). The shared fixture derives its pipeline
source kind, exclusive owner bound, actual Shader target-memory artifact and
declared stage from the accepted RenderPipeline or ComputePipeline type. Graphics
reads its original Fragment target layout and declares Fragment resource usage;
the Source5 helper retains the actual pipeline type. The accepted Graphics
context supports explicitly counted additional scalar texture/view and sampler
registrations, retaining the original registry and allocator domains. A new
Graphics source fixture loads original module/child target-memory evidence,
creates the accepted native pipeline through its existing factory, and composes
Uniform/ReadOnlyStorageBuffer with static/dynamic or whole ranges and scalar
storage/sampled/sampler sources. It invokes the existing real GeneralWrite2 and
Source5 rejection/freeze/terminal/retirement machinery. It remains a source-only
entry: Compute Inputs/Consumption/State/Stream are explicitly refused for this
Graphics entry; no empty render visitor is accepted as Graphics traversal.

The full current browser plus explicit Compute and Graphics source template
instantiations pass current assertion-enabled O3 Wasm syntax. Real reports prove
accepted native RenderPipeline, Fragment4 bindings, genuine GeneralWrite2 scopes,
frozen Source5, descriptor-reset rejection and original source/input quota retirement.
Native errors are empty. The first preparation failure is retained; the diagnostic
rerun exposed a missing explicit set0-to-layout slot. The fixture now declares
that slot and validates real module reflection before native preparation; no
production contract was relaxed. Both full failure payloads/reports are retained.
Immutable source-only archive: build/bindless-graphics-source1/validation.json.
Independent SHA/byte audit verifies86 artifacts/32048027 bytes and the exact
preceding shader/input archive. Archived runner replay accepts4 real reports and
rejects22 missing-evidence and8 missing-group/native-error mutations. Two real
Compute Source reports cannot satisfy the Graphics runner. This independent entry
does not replace the fixed Submission payload. Native draw/readback, nonempty RenderUsage4,
attachment/state/dependency/content joins, numerical output, Graphics lifecycle,
public/NativeSync, Metal/WOCC P4-P6 and production/platform acceptance remain open.
The preceding immutable shader/CPU input archive stays unchanged. Original Submission138/138 and affected66/66 are independently validated
and archived; Claim/PostSuppression and full finite lifecycle remain pending.
<!-- FINITE_GRAPHICS_COMPOSITION_CURRENT_END -->

<!-- FINITE_GRAPHICS_INPUT_CURRENT_START -->
Finite Graphics shader/CPU input checkpoint (2026-10-12): two actual Slang
modules and four selected Vertex/Fragment entries pass artifact/ABI/role custody
checks. Fragment binds four scalar resources; the resource-free Vertex entry
has no storage write. SPIR-V proves the storage image and location-zero Fragment
attachment receive the same SSA value. This is executable-artifact evidence,
not a GPU draw or rendered result. The compiler uses selected SPIR-V descriptor
visibility to skip inactive storage declarations; four actual active-entry
missing-WGSL/missing-authored-declaration refusals and two inactive Vertex cases
pass. Both original Compute packages were recompiled independently: eight
Slang/SPIR-V/MSL/WGSL files remain byte-identical to the live Submission inputs.
MinGW assertion-enabled original Graphics module/input tests pass2/2, covering
module/child ownership, quotas, allocator, four backend target selection and
hot allocation0. No borrowed static library or live Submission image was rebuilt.

Immutable shader/input-only evidence is build/bindless-graphics-input1/validation.json.
Independent recursive SHA/byte audit verifies222 artifacts/667017034 bytes;
archived shader checks and both archived CPU input tests replay successfully.
Native draw/readback, real render binding/usage, full finite Graphics lifecycle,
generic geometry/table coverage, public execution, NativeSync, platform and
production gates remain closed. Next join the accepted original RenderPipeline
to Source5, nonempty RenderUsage4, attachment/state/dependency/content custody,
then genuine draw/completion and independently authorized attachment readback.
Original Submission native acceptance remains live: available authentic reports
125/138 pass independent marker/scope/numerical checks; remaining cases,
66 regressions and the immutable acceptance archive are still pending.
<!-- FINITE_GRAPHICS_INPUT_CURRENT_END -->

<!-- FINITE_GRAPHICS_SOURCE_CURRENT_START -->
Finite Graphics source-reuse checkpoint (2026-10-12; native Graphics acceptance
pending): an explicit RenderPipeline instantiation exposed a hard-coded Compute
alias-owner negative in the shared Source5 helper. The original compiler failure
and header are preserved. The helper now keeps the actual Pipeline type and
changes the declared Shader stage for both valid Compute and Graphics inputs.
Explicit ComputePipeline and RenderPipeline instantiations pass current Wasm
syntax; source inventory354 passes. This fixes reusable test machinery only:
no actual finite render binding, native draw or rendered numerical result is
validated. The Graphics composition contract records real render projection,
attachment/state/dependency/content joins, original output authorization, six
numerical combinations and Graphics-specific lifecycle requirements; generic
geometry/table/stage/operation/ABI coverage remains open. Upcoming Claim/Post
selected source preservation includes this helper, for16/22 authored inputs.

Independent finite Compute lifecycle aggregation is prepared: exact204 terminal
identities plus six successful numerical cases, recursive artifact SHA/bytes,
exact predecessor snapshots and final-payload scenario/Boolean/terminal/numerical
regression checks are required. Actual available archives verify291 artifacts/
1936370726 bytes, covering Recording24 and numerical6. Nine authentic-evidence
rejection checks pass. Submission, Claim and PostSuppression archives are still
missing; available-only auditing creates no acceptance archive and grants no
full lifecycle, Graphics, public execution, NativeSync, platform or production
authority. Submission packaging now directly binds archived Wasm bytes/SHA;
stage activation rechecks the preceding immutable evidence before replacing
its Python driver. The original live Submission payload/driver stays unchanged.
<!-- FINITE_GRAPHICS_SOURCE_CURRENT_END -->

<!-- POSTSUPPRESSION4_CURRENT_START -->
Post-suppression negative probe implementation/syntax checkpoint (2026-10-12;
native acceptance pending):12 authored cases cover finite Uniform/ReadOnlyStorage
x static/dynamic/whole x commit/publication. The same original command-preparation
implementation is compiled into only the explicitly opted-in browser smoke;
the ordinary backend library remains uninstrumented. The option is default-off
and requires Emscripten plus static backend linkage. It cannot deliver a callback,
construct a GPU receipt, publish a result or grant any execution capability.
After genuine suppression, Commit deliberately passes the incompatible frozen
Submission1 choice to the actual finite commit; Publication passes a local
non-opted-in intent to the actual publication validator after real copy/map and
commit. Held/terminal/rejection/retirement evidence must prove suppression1,
detached-revisit1, genuine queue/scopes/map drain, cleared original loss userdata,
Invalidated root, unpublished result, no retry and all original quota/count
retirement. Publication requires exact recorded Output2; no other profile can arm.
Ordinary/probe backend and full browser-template syntax, instantiated current
helper, Python/JS, source354, prior real report traces75/75 with264 missing-marker
refusals, CLI conflicts25/25 and actual CMake opt-in guard7/7 pass. The initial
compiler launcher selected Python3.9 and failed; this diagnostic is retained,
and explicitly selecting the EMSDK Python3.13 resolves it. These checks grant
no native post-suppression acceptance. The later Python gate and activation are
staged; activation/configuration is guarded by archived Claim30/affected204.
Submission/loss/map continues on its original fixed payload and Python driver;
Claim must still complete first. No browser/native payload was rebuilt here.
Full Lifecycle4, finite Graphics, generic coverage, public execution/NativeSync,
Metal Graphics, WOCC P4-P6 and production/platform acceptance remain open.

Post-suppression acceptance tooling is prepared: strict12-case/234-regression
validator,22 selected authored sources and the actual linked instrumented object,
separate main/probe compile freshness, explicit probe-only macro and one linked
probe object while the ordinary backend compile remains uninstrumented. Real
trace audits require255 reports,2814 missing-marker refusals and120 unexpected
execution/output refusals. The report independently requires the original held
submission/map evidence and original content/invalidation policy. Validator
preflight rejects a genuine successful Submission report as post-suppression
evidence and creates no acceptance archive. Current Python/JS syntax, prior
traces75/75 plus264 refusals and CLI25/25 pass. These are tooling checks only;
the12 native probe cases and234 affected regressions remain unrun.
<!-- POSTSUPPRESSION4_CURRENT_END -->

<!-- CLAIM4_CURRENT_START -->
Finite Lifecycle4 claim/pre-record terminal integration is implemented, native
acceptance pending (2026-10-12). Thirty cases cover Uniform/ReadOnlyStorageBuffer
x static/dynamic/whole x cancel, caller-drop, retirement, shutdown and genuine
physical loss. They reuse the original claimed Issuer7/root, frozen Submission2
and Snapshot2 preparation. Snapshot2 refuses completion and is released before
caller-drop so genuine issuer/callback facades can expire. Existing native cold
call and queue invocation observers must prove encoder0/groups0/submit0. The
exclusive CPU claim remains sealed after terminal delivery. Cancellation/drop
and logical retirement/shutdown end Abandoned; captured GPUDevice.destroy ends
Invalidated. Live content/state revisions remain unchanged when the device is
live; logical terminal refuses observations without pretending physical loss.
Original prepared receipts7, local loss registration, reservations, initializer
custody and quotas must retire. No replacement root, GPU object, executor or
receipt is introduced. Full browser template syntax, Python/JS syntax, source354,
prior real report traces75/75, missing-evidence rejection264/264 and CLI conflict
rejection18/18 pass; no native claim result is available. The new Python gate is
staged, not active. Submission/loss/map
still runs its existing Python driver and fixed compiled payload; fourteen
authored sources and compile freshness/runner equality were preserved before
claim edits at build/bindless-general-submission4-authored-inputs/provenance.json.
Do not rebuild browser/MinGW images or activate claim until that acceptance is
archived. Post-suppression failure remains required for full Lifecycle4; Graphics,
generic coverage, public/NativeSync, Metal/WOCC/P4-P6 and production remain open.

Claim acceptance tooling checkpoint: strict30-case/204-regression validator,
16-source compile provenance, immutable archive and recursive SHA audit are
prepared. Fixed build flags, exact held/terminal/content/retirement markers,
all prior Boolean and numerical profiles,243 real-trace report checks,
2604 missing-marker refusals and90 unexpected-execution refusals are required.
Tool preflight passes syntax and rejects a genuine successful Submission report
as Claim evidence. This is tooling verification only; native Claim remains
unvalidated and post-suppression failure remains open.
<!-- CLAIM4_CURRENT_END -->

<!-- SUBMISSION4_CURRENT_START -->
Selected private finite Compute Submission4/loss/map terminals are Backend
Validated (2026-10-12): all138 exact buffer/range/terminal combinations and66
affected regressions pass independent acceptance. The original regression process
failed after15 passes because Windows temporary Edge-profile cleanup raised
PermissionError; its actual successful GPU report, failed log/terminal and
payload are retained. The unchanged original driver and fixed Wasm recovered
the failed case plus50 unrun cases:51/51 passed, with no overlap and exact66
coverage. This is recovered acceptance, not a rewritten single-run success log.

The strict full validator archives all138 cases,66 regressions, original failure
and recovery evidence, selected authored inputs, native images and fixed browser
payload. Independent recursive audit verifies257 artifacts/436717622 bytes,
exact terminal identities and the packaged22894688-byte Wasm SHA256
0e5ecb53fe39291a0ebe85d1e874c835d5429e86b91cbf95622e8b1f669a1402.
Runner traces pass213/213 and reject2214 missing-evidence mutations. Selected
compile provenance, MinGW5/5, Wasm quick/Null6/6, source354 and Slang artifact
checks remain bound to this immutable stage archive:
build/bindless-general-submission4-webgpu-validated-payload/validation.json.

Submission failures invalidate the original root, remain unpublished and cannot
retry; actual device loss and original native drain are proven. Map failures
preserve completed upstream content and invalidate unpublished downstream output.
Reverse/published-loss cases preserve selected numerical bytes. Recording4 and
Execution7 archives remain immutable. The next dependency is Claim30/affected204,
then PostSuppression12/affected234 and final-payload204 terminals plus6 numerical
regressions. Full Lifecycle4, Graphics, generic coverage, public/NativeSync,
Metal/WOCC P4-P6, platform/visual/performance/server and production remain open.
<!-- SUBMISSION4_CURRENT_END -->

<!-- RECORDING4_CURRENT_START -->
Selected private finite Compute recording terminals are Backend Validated
(2026-10-12): Uniform/ReadOnlyStorageBuffer x static/dynamic/whole x cancel,
caller-drop, native-error and reverse-scopes pass24/24 actual native cases.
Nineteen affected browsers pass19/19 (844.66s); both439-Boolean profiles and
original numerical output remain unchanged. MinGW5/5, Wasm quick/Null6/6,
source354, Slang artifact1/1 and CLI conflict rejection8/8 pass. Report traces
pass75/75 and reject264 missing-evidence mutations. Drop releases caller facade
and diagnostic ownership; genuine native callback state retains initializer
custody until scope drain. All cases prove Abandoned rollback, stable live
content, queue.submit invocation0, unpublished/no retry, and original quota/
reservation retirement. Fixed O3/UNDEBUG/Asyncify16384/64MiB/no-growth/timeout180
remain unchanged. Immutable snapshot:
build/bindless-general-recording4-webgpu-validated-payload/validation.json.
Recursive audit verifies96 artifacts/432711208 bytes. Selected authored inputs
are preserved with SHA256 and compile freshness; this is not full SDK provenance.
Execution7 remains immutable. Full Lifecycle4 claim/pre-record, submission,
physical loss, map/publication, Graphics/public/NativeSync/Metal/WOCC/P4-P6 and
production readiness remain incomplete.
<!-- RECORDING4_CURRENT_END -->

<!-- EXECUTION7_CURRENT_START -->
Selected private finite Compute Execution7/Submission2/Output2 is scoped Backend
Validated (2026-10-12). After the completion-profile and JS report-scope repairs,
actual Uniform/ReadOnlyStorageBuffer static/dynamic/whole executions pass6/6
(651.50s), affected browser regressions72/72 (1295.50s), MinGW contracts5/5
(11.01s), Wasm quick/Null6/6 (22.26s), source354 and Slang artifact1/1 pass.
Runner report traces pass51/51, with actual corrected-branch browser probes3/3
(11.84s); the archived prior runner fails33/51 with ReferenceError. Both original
439-Boolean profiles and original RGBA16F/owners311 are unchanged. O3/UNDEBUG,
Asyncify16384,64MiB,no-growth and timeout180 are unchanged; main1336 dependencies,
22776724-byte Wasm and all generated Asyncify/export mappings are validated.
Submission2 is frozen before recording, consumes original scopes and queue
completion, and commits the same finite ContentPlan5 root. Snapshot2/Output2
authenticate that original root for genuine downstream copy/map/unmap/result
publication. Actual static/whole RGBA16F is
1.02734375/2.03125/3.03515625/4.0390625; dynamic is +4 per channel. Float versus uint
input, selected ranges, full backing charge/custody, held-Pending public refusal,
forged/cross-profile rejection, caller release and original quota/count retirement
pass. Recording-only7 still refuses a later Submission2 opt-in and rolls back;
Output1/whole General and Output2/finite General remain isolated.
Immutable acceptance evidence:
build/bindless-general-execution7-webgpu-validated-payload/validation.json.
The recursive SHA256 audit verifies195 artifacts/1503659518 bytes. It binds named
payloads and selected authored sources; it is not complete SDK transitive provenance.
Initial compiler/inventory/submission failures and the stopped regression with
three out-of-scope JS report branches remain retained. Null validates only;
Vulkan/Metal deterministically reject this private finite profile; WebGPU alone
consumes its explicit private adapter. Public execution and NativeSync remain false.
Full finite Lifecycle4, finite Graphics, generic resource/stage/operation/ABI and
four-backend coverage, Metal public Graphics, renderer/WOCC P4-P6 and all platform/
visual/Release-performance/server/production acceptance remain open. The next
step is the finite native lifecycle acceptance matrix in the finite composition
contract; its terminal cases remain unvalidated. The full bindless goal is active.
<!-- EXECUTION7_CURRENT_END -->

Historical implementation checkpoints follow; current acceptance is above.

First actual finite Execution7 browser submission failed before publication
(113.25s, no native validation errors): the original root still unconditionally
refused ContentPlan5 native completion. The private native completion boundary
now carries the recording-frozen GeneralSubmission version through the original
Seal, with exact Submission1/whole/ContentPlan4 and Submission2/finite/ContentPlan5
pairing; ordinary profiles remain Submission0. CPU mutation remains closed.
Terminal drain also recognizes an already authenticated, suppressed loss
registration after a later commit/publication failure, without synthesizing
queue or callback evidence. New completion profile matrices, current source354,
Wasm/MinGW and full instantiated/Python/JS syntax pass; runtime revalidation is
pending. Immutable first-failure payload/report/log/source is retained at
build/bindless-general-execution7-first-browser-failure. No numerical acceptance
or readiness promotion is claimed from these repairs.

General finite Execution7 / Submission2 / Output2 implementation in progress
(2026-10-11; not Backend Validated): production profile validators now pair
Output1 with Issuer6/whole General/ContentPlan4 and Output2 with Issuer7/finite
General/ContentPlan5. Join4 intent matches the retained snapshot version and
Cold5 intent matches Join4; downstream Source2/correspondence1/State1/Inputs1/
ContentPlan0 schemas are unchanged. Execution7 without explicit recording-frozen
Submission2 remains recording/cancellation only. Null is validator-only; Vulkan
and Metal reject this private profile; WebGPU private submission/output adapter
is implemented but awaits actual runtime validation. Public/NativeSync stay false.
Six independent Uniform/ReadOnlyStorageBuffer static/dynamic/whole scenarios now
compose real recording/submission scopes, queue completion and the original
copy/map/result path. Float versus uint selected bytes and distinct aligned
dynamic candidates derive a finite RGBA16F oracle from the original uploaded
texels. This is not numerical or lifecycle acceptance evidence yet. First syntax
and source-inventory failures are preserved in build/bindless-general-execution7-
first-syntax-inventory-failure. The immutable native7 checkpoint below remains
the last actual recording/cancellation validation. Full lifecycle, Graphics,
public execution, Metal Graphics, WOCC P4-P6 and Production Ready remain open.

Selected private WebGPU native7 recording/cancellation validated checkpoint
(2026-10-11): Uniform/ReadOnlyStorageBuffer static/dynamic/whole actual browsers6/6
pass (622.50s), quick/Null6/6 (23.12s), affected browsers66/66 (653.96s), source354
and Slang artifact1/1 pass. Wasm/MinGW/full instantiated/Python/JS syntax and fixed
Release O3 packaging pass; main1335 dependencies and all four minified Asyncify
bindings are validated. Actual MinGW General/retention/Null/WebGPU/Metal tests5/5
pass (5.72s), including the previously unstarted retention executable. Original
Debug artifacts and entry/code/data-identical runnable images are retained; the
historical startup failures remain evidence, with no cause attributed.
Cold7/Command7/Callback7/Issuer7 consume exact Payload7/Lifecycle4/ContentPlan5.
Real three-scope recording authenticates buffer Pending3 and texture Pending2,
keeps public Ready validation strict, rejects alias/native identity mutation,
retains caller-dropped input custody, and proves recorded cancellation after
all three scopes drain. Original
journal/state rollback, receipt suppression and all owner/count/quota retirement
pass. Cold223/Command226/Issuer240 owners, authored ceiling259 and Command752/
Submission492/Callback640-byte exact-less-exhausted admission pass; Sources CPU
ceiling stays49. Both439-Boolean profiles and original RGBA16F/owners311 numerical
output are unchanged. Immutable engine evidence:
build/bindless-general-native7-webgpu-validated-payload/validation.json;
SHA audit verifies161 artifacts/1656985295 bytes. The first missing-include/raw
inventory failures, deliberately stopped builds and first actual receipt-retirement
fixture failure are preserved. Scoped Backend Validated covers private finite
recording and cancellation only. GeneralSubmission2 stays closed; Output2,
finite numerical execution, full Lifecycle4 loss/shutdown/completion, Graphics,
public/NativeSync, Metal public Graphics, renderer/WOCC P4-P6 and Production Ready
remain incomplete. Next: pair real GeneralSubmission2 with exact GeneralOutput2
and typed selected-byte numerical readback on this same original owner graph.
The Lifecycle4 CPU and earlier checkpoints below are historical evidence.

Selected WebGPU Lifecycle4 CPU seal/reservation validated checkpoint (2026-10-11):
finite Uniform/ReadOnlyStorageBuffer static/dynamic/whole browsers6/6 (120.16s),
quick/Null6/6 (22.92s), affected60/60 (513.09s), source354 and artifact1/1 pass.
Wasm/MinGW/full instantiated/Python/JS syntax and fixed O3 packaging with main1334
dependencies pass. Original seal140/reservation132-byte exact-less-exhausted
admission, owners49/50, sticky claims, forged owner refusal, caller-drop retention,
hot0/query0 and all original transaction/reservation counts and quotas retire.
Both439 Boolean profiles and original RGBA16F/owners311 are unchanged. Engine
immutable evidence: build/bindless-general-lifecycle4-webgpu-validated-payload/validation.json;
SHA audit verifies135 artifacts/1403536299 bytes. MinGW4/5 pass (10.97s); retention
startup is BAD_COMMAND / operation not permitted, then the executable is absent.
Full Windows acceptance stays false; no security-product cause is established.
Scoped Backend Validated covers CPU seal/capacity admission only. Pending3 and
TexturePending2 are exact finite-root/ContentPlan5 contracts; native held-Pending
acceptance, Lifecycle4 native events, native7 recording/submission/output, Graphics,
public/NativeSync, Metal public Graphics, WOCC P4-P6 and Production Ready remain open.
The following Freeze7/Payload7 and older checkpoints are historical.


Selected WebGPU Freeze7/Payload7 private CPU preparation validated checkpoint
(2026-10-11): Uniform/ReadOnlyStorageBuffer static/dynamic/whole browsers6/6
(79.18s), quick/Null6/6 (23.33s), affected60/60 (443.12s), source354 and shader
artifact1/1 pass. Fixed Release O3 packaging and full instantiated syntax pass.
Root4332/closure3932/freeze1788/payload2468-byte admission, owners216/217/218,
caller closure/initializer drop, exact-less-exhausted, hot0/query0 and original
ContentPlan5 rollback/retirement pass. Both439 Boolean profiles and original
RGBA16F/owners311 are unchanged. Immutable engine evidence:
build/bindless-general-frozen7-webgpu-validated-payload/validation.json;
SHA audit verifies160 artifacts/1527202972 bytes. MinGW4/5 pass; retention did
not start (CTest BAD_COMMAND / operation not permitted). Full Windows phase
acceptance remains false, without an established cause. Scoped Backend Validated
covers private CPU candidate preparation and immutable native input custody.
Pending3/Lifecycle4/native7, finite numerical output, real Graphics/public/
NativeSync, Metal public Graphics, WOCC P4-P6 and Production Ready remain open.
The Closure9 and older checkpoints below are historical.


Selected WebGPU Closure9/custody2 custody validated checkpoint (2026-10-11):
six Uniform/ReadOnlyStorageBuffer static/dynamic/whole browsers pass6/6 (55.97s),
quick/Null6/6 (21.08s), affected54/54 (408.40s) and source354. Root4332/closure3932
bytes, owners216, complete backing charges, original initializer ownership after
caller release, exact-less-exhausted failures and hot0/query0 pass. Both439-Boolean
profiles and RGBA16F/owners311 are unchanged. Generated JS-to-Wasm Asyncify export
mapping and authored runner packaging pass after correcting the raw-name guard.
Immutable evidence: build/bindless-general-closure9-webgpu-validated-payload/validation.json;
recursive SHA audit verifies200 artifacts/1900768943 bytes. MinGW4/5 pass; canonical
unmodified retention startup returns WinError5 and its file becomes absent.
Full phase acceptance is explicitly false; no security-product cause is claimed.
Scoped Backend Validated is private WebGPU custody only. Freeze7/Payload7,
Pending3/Lifecycle4/native7, actual output, Graphics/public/NativeSync, Metal public
Graphics, renderer/WOCC and full P3-P6 remain incomplete. The earlier pending
checkpoint below retains its observed build/startup failures.

General finite Closure9/custody2 implementation checkpoint (2026-10-11): private
custody pairs the existing State5/Stream5/Inputs6/Source5/ContentPlan5 root with
GeneralResources2, GeneralWrite2/NativeIdentity4 and complete initialized backing
ownership. Selected WebGPU custody acceptance is documented above. Corrected MinGW General,
Null hidden-frame, Metal and WebGPU contracts pass4/5; retention startup is
refused and its executable subsequently becomes absent. Initial browser tests
stop on missing Asyncify exports, and a forced build later refuses an absent
main object after its compile step returned0. Failures and diagnostic artifacts
are retained; no cause is assigned to a security product. Freeze7/Payload7,
Pending3/Lifecycle4/native7 and public/NativeSync remain closed. The earlier
ContentPlan5 CPU checkpoint is retained below; the later selected Closure9
custody acceptance supersedes these initial browser/build failures.

General finite ContentPlan5 validated CPU checkpoint (2026-10-11): the original
StateTransaction factory now admits the explicitly paired StateSequence5/
ContentPlan5 root atomically before publication. The genuine ten-command
Source5/Inputs6/State5/Stream5 fixture retains its completed two-mip upload root,
issuer and content receipt. Uniform/ReadOnlyStorageBuffer static/dynamic/whole
actual Release browser cases pass6/6 (37.88s), quick/Null6/6 (31.62s), MinGW5/5
(6.13s) and affected browsers48/48 (374.06s). Root4332-byte exact/one-less/
exhausted admission, timeline9/final4 cells, hot0/query0 apply/rollback, unchanged
completed-content generation/revision and full quota retirement pass. Both439
Boolean profiles and RGBA16F/owners311 remain unchanged. Immutable evidence:
build/bindless-general-content-plan5-validated-payload/validation.json; recursive
SHA audit verifies142 artifacts/1509301520 bytes. First failures are preserved.
Scoped Backend Validated is CPU atomic admission/read-only plan/apply/rollback
only. Next: retained initializer custody2/Closure9 and independently paired
Pending3/Lifecycle4/native7. Graphics, public/NativeSync, Metal public Graphics,
renderer/WOCC and full P3-P6/platform/visual/production remain incomplete.

General finite selected ReadOnlyStorageBuffer Compute Usage6 validated checkpoint (2026-10-11):
the original shared fixture checks actual Source5 descriptor type against
StorageRead/UniformRead and retains the exact physical range/dependency checks.
Four readonly finite/whole/retired/loss browser scenarios reuse the existing
readonly Slang artifact, original initializer and nine-command State5/Stream5.
The runner composes original Ready-content and Source/Inputs/State/Usage evidence
and refuses initialization terminal combinations. Full Wasm/Python/JS syntax,
rebuilt quick/Null6/6 (21.36s), MinGW contracts5/5 (5.09s) and readonly artifact
guard1/1 (0.18s) pass. Fixed O3 browser build is in progress; actual browser4/
affected44 and final evidence remain pending. No new native/public execution authority.
First actual browsers4/4 (13.19s) refused the old fixture mode guard excluding
Inputs plus initialized content, with empty nativeErrors. Sources/payload/reports/
logs are retained under build/bindless-general-readonly-usage6-first-browser-failure.
The guard now admits only complete Usage6 composition and refuses initialization
terminal variants; corrected full Wasm/Python/JS syntax passes. The corrected
fixed O3 build succeeds. Corrected actual browsers4/4 (25.66s), affected44/44
(348.55s), final quick/Null6/6 (20.74s) and final SHA-bound source/payload/report/
log/native identity validation pass. Both439 Boolean profiles and original
RGBA16F/owners311 remain unchanged. Ready association, exact StorageRead ranges,
State948-byte/Stream1156-byte admission, hot0/query0 and complete quota drain pass.
Premature aggregation refused the unfinished final quick log without publishing
a manifest; its observed diagnostic is preserved, then terminal aggregation passes.
Immutable evidence: build/bindless-general-readonly-usage6-validated-payload/validation.json.
Scoped Backend Validated covers selected readonly Compute CPU usage/dependency
projection and existing Ready association; ContentPlan5/root, actual Graphics,
frozen7/Closure9/native7/public/NativeSync and full P3-P6 remain open.
Preceding selected Uniform Usage6 evidence is immutable below.

General finite selected Uniform Compute Usage6/dry request6 validated checkpoint (2026-10-11): exact
State5/Stream5/Inputs6/GeneralResources2/footprint4 and independent Scope5 mapping
are implemented, including dependency Record6 validation. Genuine nine-command
fixtures reuse original Inputs/State/Stream and add three authored buffer/texture
memory barriers; selected usage has contributions3/textures2/sampler1 nonmemory,
dependency final3/passes10/visits30/pairs3/endpoints3/cells2. Quota/count/profile/
record mutation, zero-output preflight, hot0/query0 and terminal fixtures exist.
Wasm/MinGW quick and full instantiated Wasm/Python/JS syntax pass; rebuilt
quick/Null6/6 (21.57s), source354 and actual MinGW contracts5/5 (11.85s) pass with
verified debugging-only removal and retained originals. First old source/fixture
maximum5/future6 failures are preserved; new dry6 and future7 assertions retain
the old frozen/native6 gates. Fixed O3 browser build succeeded; first actual
browsers4/4 refused the six-command RenderUsage fixture budget on the genuine
nine-command program, with empty nativeErrors. Source/payload/logs/reports are
preserved under build/bindless-general-usage6-first-browser-failure. The fixture
now explicitly selects nine commands and keeps an exact old-six-budget refusal;
full Wasm/Python/JS syntax, rebuilt quick/Null6/6 (22.34s) and MinGW contracts5/5
(5.19s) pass. Corrected actual browsers4/4 (25.37s) pass with State948-byte/
Stream1156-byte admission, finite256/512 or whole0 selected16-byte ranges and
empty nativeErrors. Affected40/40 (323.24s) and final SHA-bound source/payload/
report/log/native identity validation pass; both439 Boolean profiles and original
RGBA16F/owners311 remain unchanged. Immutable evidence:
build/bindless-general-usage6-validated-payload/validation.json. Scoped Backend
Validated covers selected Uniform Compute CPU usage/dependency projection only;
ReadOnlyStorageBuffer usage remains unvalidated.
Six-command dependency EMPTY remains an explicit refusal.
Frozen7/Closure9/content/native/public/NativeSync, real Graphics and full P3-P6
remain unavailable or unvalidated. Preceding State5 evidence is immutable below.

General finite State5/Sequence5/Stream5 validated checkpoint (2026-10-11):
original capture and timeline/stream storage now accept only the exact Inputs6/
GeneralResources2/footprint4 pair. Sampler references retain nonmemory semantics;
tables, geometry and retained-readback state remain excluded. A genuine mixed
Compute fixture checks cells3/steps3/implicit4, independent storage/count admission,
forged owners, preflight-no-output, hot0/query0 and terminal retirement/loss.
Wasm/MinGW quick and full instantiated Wasm/Python/JS syntax and source354 pass.
Rebuilt quick/Null6/6 passes (21.08s), actual MinGW contracts5/5 passes (5.77s)
after verified debugging-only removal with original code/data retained. Fixed
Release O3 build succeeds; first actual browsers4/4 fail original owner admission,
with empty nativeErrors. Source/payload/reports are retained under
build/bindless-general-state5-first-browser-failure/validation.json. The fixture
now authors the omitted fixed registry closure and ten canonical/associated
registrations before Sources publication, preserving exact State owner/registration
budgets and one-less refusals. Saturated historical quota peaks use the existing
history-independent threshold helper; stopped build/source evidence is retained.
Corrected actual browsers4/4 (21.84s) and affected36/36 (303.15s) pass, preserving
both439 Boolean profiles and RGBA16F/owners311. CPU31/registrations10/State44/
ceiling48 and exact State708-byte/Stream988-byte admission, one-less/exhausted,
hot0/query0 and complete retirement pass. SHA-bound immutable evidence:
build/bindless-general-state5-validated-payload/validation.json. Scoped Backend
Validated covers selected Compute CPU capture/timeline/stream only. Actual
Graphics and UsageProjection6/dry request6 remain open. ContentPlan5
and the new transaction root are unimplemented; State5 ownership refuses with
RHI-ORIGINAL-FINITE-TRANSACTION-CONTENT-PROFILE-UNIMPLEMENTED. The exact old
General root predicate and every legacy native gate remain unchanged.

General finite Consumption4 validated checkpoint (2026-10-11): exact
GeneralResources2/Inputs6 pairing now reuses original per-element frozen Source5
projection and inspect-then-visit preflight. Separate finite/whole permissions,
texture capacities and table/geometry refusals preserve original legacy gates.
Wasm/MinGW quick and full instantiated Wasm/Python/JS syntax pass; quick/Null6/6
(24.81s) and actual MinGW contracts5/5 (11.24s) pass with byte-verified DWARF
removal. The initial old maximum3 inventory assertion is preserved and updated
to maximum4 and only the exact Inputs6 pairing, keeping legacy table/geometry
checks. Fixed O3/Asyncify16384/64MiB/no-growth build succeeds; actual Release
Edge finite/whole/retired/loss passes4/4 (18.46s), affected browsers32/32
(308.36s). Both complete439-Boolean profiles and original RGBA16F/owners311
remain unchanged. SHA-bound sources/payload/reports/logs and native code identity
validate; immutable evidence is
build/bindless-general-consumption4-validated-payload/validation.json. The
selected Compute program validates RenderUsage's shared profile join only, not
real Graphics binding traversal. State5/Stream5, initializer custody and native
execution remain unavailable. The preceding Inputs6 source/payload/reports are
SHA-verified under build/bindless-general-inputs6-validated-payload/validation.json.

General finite Inputs6 validated checkpoint (2026-10-11): the original
positional validator now explicitly pairs Inputs6 with DescriptorSource5 and
consumes ordered dynamic offsets from its existing mixed frozenElements. Legacy
Inputs3/4/5 reject Source5; Inputs6 rejects old Sources, tables and geometry.
New independent browser scenarios cover static/nonzero dynamic/all-whole binding,
incoming identity/offset refusal, metadata admission and original graph retention
through retirement/loss. Full instantiated Wasm/Python/JS syntax passes after
correcting explicit scalar capture; the first syntax failure is preserved.
Fixed Release O3 Edge finite/whole/retired/loss passes4/4 (17.86s), rebuilt quick
contracts/retention/Null6/6 (24.25s) and affected browsers28/28 (277.77s) preserve
both complete439 Boolean profiles and original RGBA16F/owners311. MinGW syntax
and actual General/retention/Null/Metal/WebGPU contracts5/5 pass (5.37s). Four
large Debug payloads initially refused startup; original binaries are retained,
and DWARF removal preserves the entry point and every non-debug code/data
section before actual execution. The WebGPU source fixture now enforces its
authored LF fingerprint. Static whole malformed-offset Scalars requires an
explicit pre-published capacity1; its accepted Inputs6 still consumes zero
offsets. All earlier syntax/fixture/stale-run/launch failures are preserved.
SHA-bound Inputs6 snapshot: build/bindless-general-inputs6-validated-payload/validation.json. No
Consumption4/State5/Stream5, initializer custody or finite execution is admitted.
The preceding readonly validated source/payload/reports are now archived with
verified SHA256 under build/bindless-general-readonly1-validated-payload/validation.json.

General finite ReadOnlyStorageBuffer checkpoint (2026-10-11): fixed Release O3
Edge selected Ready/terminal/callback-only cases pass10/10 (41.37s), UniformBuffer
regression10/10 (39.87s), and affected browsers8/8 (196.40s). Original Source5/
Staging4 and the authored mixed StructuredBuffer<uint>/storage texture/sampled
texture/sampler artifact retain complete backing charges, actual storage
alignment/minimum4, selected16-byte ranges, hot0/query0 and full quota retirement.
Both439 Boolean profiles and RGBA16F/owners311 are unchanged. Artifact guard1/1,
quick6/6 (21.60s), Python/JS/full Wasm syntax and fixed O3/16384 Asyncify/64MiB/
no-growth build pass. SHA-bound current evidence is
build/bindless-general-readonly1-native-validation.json; prior initializer2
snapshot is archived under build/bindless-general-initialization2-validated-payload.
The first missing shader fixture catalog failure remains preserved. This closes
selected both-type initialization acceptance only; downstream initializer custody,
finite recording/numerical execution, public/NativeSync and P3-P6 remain open.
The original composition contract now records the remaining schema audit and
exact planned profile pairing; those new profiles are not implemented yet.

General finite initializer callback-only checkpoint (2026-10-11): actual Release
O3 Edge six terminal cases pass6/6 (24.81s), now including cancel-drop-pending and
pending-drop-loss. The caller's last initializer wrapper expires; its expired
weak witness's charged control block is released before measuring retirement.
The same original State/payload/callback allocations remain charged with held
native promises, including after partial release, and genuine full drain retires
all three quota domains. The finite cancellation case covers static/nonzero
dynamic Source5; actual loss uses the independent whole GeneralWrite2 layout.
Ready regression4/4 (16.16s), quick6/6 (22.48s) and affected browsers8/8 (195.27s)
pass, preserving both439 flags and RGBA16F/owners311. Current SHA-bound evidence:
build/bindless-general-initialization2-native-validation.json; previous terminal
source/payload/reports are archived under bindless-general-initialization1-
validated-payload/validation.json in build. The explicitly stopped first build
and corrected weak-control accounting are recorded. Scoped Backend Validated
covers selected UniformBuffer initializer terminal/callback-only custody only.
This supersedes the callback-only pending gate below; ReadOnlyStorageBuffer and
downstream initializer custody, finite execution/public/NativeSync, Metal
Graphics, renderer/WOCC P4-P6 and platform/visual/production remain incomplete.

General finite initialization terminal checkpoint (2026-10-11): actual fixed
Release O3 Edge cancel-before/cancel-pending/native-error/pending-loss4/4 passes
in16.78s. Real original Staging4 writes and scopes, held promises3/queue1, real
misaligned write validation, outstanding-at-physical-loss3, partial release,
Ready/retry refusal, zeroed failed intervals and complete quota retirement pass.
Pending loss uses an independent all-whole GeneralWrite2/Source5 layout without
post-loss native creation. Terminal reports require Ready/execution false and
have empty diagnostics/nativeErrors. Ready regression4/4 (16.26s), quick6/6
(22.08s) and affected browsers8/8 (196.15s) pass; both439 Boolean profiles and
RGBA16F/owners311 are unchanged. SHA-bound evidence:
build/bindless-general-initialization1-native-validation.json. Prior Ready
payload/reports are archived under bindless-general-finite-content1-validated-
payload/validation.json in build. Scoped Backend Validated covers selected
UniformBuffer terminal refusal/drain only, superseding the pending terminal
matrix below. Callback-only custody after caller initializer release, native
ReadOnlyStorageBuffer and downstream retained initializer custody remain open.
General finite complete execution, public WebGPU/NativeSync, Metal Graphics,
renderer/WOCC P4-P6 and platform/visual/production remain incomplete.

General finite initialized Ready checkpoint (2026-10-11): actual Release Edge
passes selected UniformBuffer finite/whole/retired/native-loss associations4/4
in16.61s. Complete backing528 with selected16 at static256/dynamic512, or separate
whole16, remains authenticated by the same original Staging4/Source5. Genuine
scopes3/queue1, pending refusal, complete-byte/sentinel/frame/owner/native/budget
mutation refusal, hot0/query0 and all quota retirement pass. Affected browsers8/8
in195.08s retain Source5, finite creation, both439 Boolean profiles and unchanged
RGBA16F/owners311. Updated General/retention/Null3/3, inventory/package/root3/3,
MinGW/native Wasm/full browser/Python/JS syntax and fixed O3/16384/64MiB/no-growth
build pass. Evidence: build/bindless-general-finite-content1-native-validation.json.
Scoped Backend Validated covers the selected Ready association only. Initialization
failure/cancellation/drain, other shader-buffer native types, downstream retained
custody and Pending/full finite execution remain open. Planned full composition:
original-general-finite-execution-composition-contract.md. Public/NativeSync,
Metal Graphics, renderer/WOCC, platform/visual/production stay incomplete.

General finite Source/5 validated checkpoint (2026-10-11): corrected Release Edge
passes selected mixed static/dynamic, all-whole GeneralWrite2, retirement and real
device-loss scenarios4/4 in17.27s. Existing frozen storage, genuine pipeline/set/
revision/offset/owner/receipt identity,1484-byte exact/one-less/exhausted admission,
pending/failed replacement refusal, hot0/query0 and final quota retirement pass.
Affected browsers4/4 in182.17s preserve both439 Boolean profiles and original
General RGBA16F/owners311. Corrected fixed O3/16384 Asyncify/64MiB/no-growth build,
MinGW/Wasm syntax, quick/source354 and rebuilt General/retention/Null3/3 pass.
First fixture-measurement failures are preserved. Evidence:
build/bindless-general-source5-final-validation.json. This supersedes Source/5's
pending acceptance note below at scoped Backend Validated for CPU custody only.
Finite initialized-content declaration/quick Node tests now pass. The original
initializer's borrowed Source5 association and four independent browser cases
are implemented; updated General/retention/Null3/3 and inventory/package/root3/3
plus native Wasm/full browser/MinGW syntax pass. Current O3 native validation is
pending, with no readiness promotion for the new association. Real association,
Consumption/Inputs/State/Stream/closure/native execution, public execution,
NativeSync, Metal Graphics, renderer/WOCC and platform/visual/production stay open.

General finite Source/5 implementation checkpoint (2026-10-11): the original
descriptor Source now borrows its existing frozen sequence for GeneralWrite/2
committed-content validation, retains the original pipeline adapter and keeps
finite-only declaration storage empty. Assembly/5 pairing and independent
four-backend outcomes are explicit; Inputs/Consumption/State/Stream/native
execution remain closed. Dedicated contract and independent browser Source
fixtures are being compiled and validated. This checkpoint grants no Backend,
Platform or Production readiness. The finite-creation evidence below remains
scoped to its earlier compiled payload. See original-general-finite-source-contract.md.

General finite creation checkpoint (2026-10-11): corrected Release Edge validates
the selected GeneralBinding2/GeneralWrite2 finite uniform16 + storage texture/
sampled texture/sampler group at static and nonzero dynamic displacements. Genuine
creation scopes, exact native entries, NativeIdentity4 profile isolation, range/
offset mutation refusal, failed replacement preservation, whole allocation charges,
hot0/query0 and full quota retirement pass in4.60s. Affected browsers3/3 (184.25s)
preserve both439-Boolean profiles and original RGBA16F/owners311 output. Dedicated
General contracts, retention/Null2/2, descriptor Node, quick3/3 and source354 pass.
Null Wasm artifacts and the Slang LF source hash now match the existing authored
package; initial hook/packaging/build/test failures are retained. Evidence:
build/bindless-general-finite2-final-validation.json. Scoped Backend Validated is
creation only. Source/5 is planned in original-general-finite-source-contract.md;
finite content/State/Stream/native execution, public adoption, NativeSync, Metal
Graphics, renderer/WOCC and full platform/visual/performance/production remain open.

Final General output map checkpoint (2026-10-11): corrected Release Edge
regression passes21/21 in671.35s, including all eleven map terminals, recording4,
shader values, geometry lifecycle3 and both complete feature profiles. Each
profile preserves439 Boolean flags with empty diagnostics/nativeErrors. Corrected
Wasm build, instantiated syntax and Node1/1 (0.39s) pass. The independent normal
General numerical regression retains the same RGBA16F/owners311/hot0-query0 and
zero final quotas. Real read/unmap refusals are READ-NATIVE-ERROR and
UNMAP-UNPROVEN; first failure reports are preserved. Evidence is continuation/
general-map-<kind>/ and general-map-terminals-numerical-regression/, plus
build/bindless-general-map-final-validation.json. Together with submission8 and
physical-loss4, the named private General lifecycle matrix is scoped Backend
Validated. Generic resource/stage/operation composition, bounded public execution,
NativeSync, Windows runtime, Metal public Graphics, renderer/WOCC and all
platform/visual/performance/production gates remain incomplete. This supersedes
pending acceptance notes below for the selected matrix only.

General output map matrix implementation (2026-10-11): eleven independent
terminal selections reuse original command/result/State and the existing native
map/loss probes. First Release Edge matrix passes9/11, including reverse numeric
completion and published bytes surviving real loss. Read-error's debug-console
assumption and unmap-throw's assumed uncertainty diagnostic are corrected to
actual Release bridge behavior: exact intercepted read range/READ-NATIVE-ERROR,
and one throwing native unmap with mapped state/UNMAP-UNPROVEN before loss.
Instantiated corrected Wasm syntax passes; final corrected build/native matrix
and shared-map regressions remain pending. All first reports are retained.
This adds no public capability or broader readiness claim.

General submission/physical-loss checkpoint (2026-10-11, Windows Edge): the
corrected final Release payload passes all eight submission cases and four
actual physical-loss cases12/12 in257.06s. Exact original root/journal/content
outcomes, native queue/scope/loss drain, no failed publication/retry and zero
retired quotas pass with empty diagnostics/nativeErrors. Reverse submission
again completes the same General numerical output. Final Wasm build and MinGW
C++17 syntax pass. Reports are continuation/general-submission-{cancel,retired,
shutdown,throw-before,throw-after,native-error,reverse-scopes,partial-scope}/ and
general-physical-{queue-uncertain,recording-shutdown,partial-push,recording-exception}/
under Release artifacts. This supersedes the pending acceptance notes below for
these candidates only. Output map terminals, public WebGPU/NativeSync, Windows
runtime, Metal public Graphics and renderer/WOCC/platform/production remain open.

General recording terminal checkpoint (2026-10-11, Windows Edge): four independent
cancel/drop/native-error/reverse-scopes scenarios reuse the original General
graph, held genuine scopes and existing native recording probe. Actual Release
Edge4/4 passes in83.48s with empty diagnostics/nativeErrors. Pending survives
held cancellation/caller drop; real scope drain rolls back the same journal,
native validation failure refuses retry, and reversed scope deliveries publish
recording only after full drain before cancellation. Exact authored reservations
remain charged while the diagnostic cold-admission observer is retained, then
zero counts and all original quotas/receipts retire. First premature-reservation
assertion failures remain preserved. Instantiated Wasm/MinGW C++17 syntax,
quick3/3 (21.45s), final Wasm build and Node pass. Scoped Backend Validated covers
these recording terminals only. Fresh numerical regression passes with unchanged
RGBA16F/owners311/hot0 (36.97s), and affected Node/browser5/5 passes in35.66s;
General submission/map/physical-loss/shutdown and public/platform/production
gates remain open. Evidence: continuation/general-recording-{cancel,drop,
native-error,reverse-scopes}/report.json under the Release browser artifacts.

Submitted cancel/Retired/Shutdown candidates now reuse fresh General graphs and
the recorded GeneralSubmission1 opt-in. Instantiated Wasm C++17 syntax passes;
independent CLI/report/CTest entries are implemented. Actual queue/scope drain,
native owner retention, Invalidated/empty journal, no output publication/retry,
no physical-loss substitution and original quota retirement require final native
build/acceptance. First actual Retired/Shutdown cases pass; cancel exposed a
fixture assumption inconsistent with original post-MaySubmit content-control
invalidation. The corrected fixture requires the existing INVALIDATED diagnostic
and no observation; first reports are preserved. Five additional native
throw-before/after, double-submit error, reverse and partial-registration probes
have instantiated syntax and strict CLI/report/CTest entries; final combined
build passes. Corrected actual cancel/Retired/Shutdown and native-error/reverse
cases pass with empty diagnostics/nativeErrors; reverse completes the original
numerical readback. Three early-quarantine cases exposed the same fixture content
assumption before drain; they now require exact immediate invalidation. First
reports are preserved and corrected regression remains pending. Four actual
physical-loss candidates reuse fresh General graphs, exact native device.destroy,
existing genuine scope/queue probes and the original loss receiver; instantiated
syntax and strict CLI/report/CTest entries exist, but native acceptance is pending.
Public capability, NativeSync and readiness remain unchanged.

General output numerical checkpoint (2026-10-11, Windows Edge): explicit
GeneralOutput1 now executes the original canonical copy/map/unmap/drain and
same-root commit/publication over the genuine GeneralSubmission1 output. The
original seal authenticates the one output Pending cell with revision R+1,
retaining complete upstream metadata/count/content and unrelated final checks.
Current authored dependency input and its frozen barrier are both revalidated;
public upstream/snapshot validation remains strict and stale after consumption.
Real RGBA16F is(1.02734375,2.03125,3.03515625,4.0390625), matching the reference
from frozen uniform/upload bytes. Native scopes delay publication; forged owners,
malformed profiles, exact/one-less budgets, scope mutation, hot0/query0 and final
quota/reservation retirement pass. Edge succeeds in37.08s with the numerical
flag true and empty diagnostics/nativeErrors. Quick3/3, current Node, MinGW C++17
syntax, final Wasm build and affected Node/browser5/5 (35.19s) pass. Earlier
JS/Wasm-pair, legacy Join1 validator and authored-scope failures remain preserved.
Contract Complete / Prototype Complete and scoped Backend Validated cover only
this selected private numerical chain. Public WebGPU, NativeSync, full General
loss/lifecycle, Windows runtime, Metal public Graphics and renderer/WOCC/P4-P6
remain open. Both full enabled/disabled profiles pass with no diagnostics/native
errors; all439 Boolean results match the prior asset-restored baseline. RenderDoc
1.14 connected four owned browser targets but observed no native API, no runner
progress and no .rdc; injected startup reached the240-second deadline. This is
RHI-RENDERDOC-BROWSER-NO-CAPTURE, not visual acceptance. The capture-control.json
and failed report under general-output-execution1-renderdoc/ preserve that outcome.
Evidence: build/web-webgpu-release/artifacts/bindless-continuation/
general-output-execution1-authored-scope/report.json.

General output execution pairing checkpoint (2026-10-11): shared explicit
GeneralOutput/1 now pairs Command/Callback/CopyPassLowering/Issuer/Recording/
Submission with Cold5/Join4, freezing the recorded profile in the existing State.
Final rebuilt Release Edge validates General pairing and legacy profile-injection
refusal; diagnostics/nativeErrors are empty, numerical/public remain false.
Source354, quick3/3, MinGW/Wasm builds, Node plus affected browser5/5 (35.76s)
and both full enabled/disabled profiles pass. Missing reflection-generated asset
preloads and CRLF source identity failures are preserved and repaired through
local tool configuration and explicit shader LF attributes. Authenticated Pending
handoff, actual copy/map/numerical output and the full objective remain open.
Evidence: build/web-webgpu-release/artifacts/bindless-continuation/
general-output-pairing1-final/ and general-output-pairing1-full-*-assets/.

General output Cold1 checkpoint (2026-10-11): independent Cold5 GeneralOutput1
admits exact Join4/State1/Inputs1/Content0/Source2/correspondence1. The old factory
and command consumers refuse; WebGPU private lowering grants cold ownership only,
Null validates and Vulkan/Metal refuse. Authored endpoints/encoder/command,
submission and twelve callback participants precede Source publication. Shared
read-only provenance checks upstream CPU/native/content/initializer and custody
quota domains, including completed texture staging after callback graph retirement.
Actual Edge final passes in42.79s with owners285, metadata572, one map/twelve
callbacks, exact/one-less/exhausted/forged/legacy refusal, hot0 and genuine
reservation/quota retirement. Source354, quick3/3, final MinGW/Wasm build and
rebuilt Node plus affected-browser5/5 (38.31s) pass. No diagnostics/nativeErrors;
numerical/public stay false. Scoped Backend Validated covers cold admission only.
No copy/map/result execution or lifecycle/public/production claim follows.
Evidence: build/web-webgpu-release/artifacts/bindless-continuation/general-output-cold1-final/.

General output native Join4 checkpoint (2026-10-11): actual Edge passes authentic
completed snapshot/output/Source2 association, revision1 and exact authored
ShaderWrite-to-CopySrc dependency. The existing bounded join retains273 owners,
8 source bytes,256 staging bytes and measured1248-byte metadata. Missing/forged/
scope/native mutation and one-less/exhausted refusal, hot0/query0 and final
retirement pass. Source353, quick3/3, rebuilt Node, MinGW/Wasm compilation and
actual affected-browser regression4/4 (33.75s) pass. The report has
originalGeneralOutputNativeJoinPassed=true, empty diagnostics/nativeErrors and
numerical/public execution false. Scoped Backend Validated is join-only; cold
pairing/copy/map/results/full loss and public execution remain open. Evidence:
build/web-webgpu-release/artifacts/bindless-continuation/general-output-native-join4-first/report.json.

General output readback input checkpoint (2026-10-11, Windows): standalone
Join4/GeneralOutput1 now explicitly selects retained Source2/correspondence1 and
State1/Inputs1/Stream1/Result1/Content0. Join1/2/3 reject the new intent. Whole
RGBA16F Storage|CopySrc target and CopyDst/GpuToCpu destination validation reuse
the existing output/readback contracts. The independent dependency validates
ComputeShader/ShaderWrite to Transfer/TransferRead, with explicit four-backend
lowering; old transfer-only mapping remains strict. Native join/copy/map/result
publication and public execution stay closed. Actual Edge consumes the genuine
completed General output revision1 and prepares its original transfer root and
one padded256-byte readback. Exact/one-less/forged root, hot allocation0,
upstream completion preservation and full cold quota retirement pass in36.10s
with originalGeneralOutputReadbackInputsPassed=true, empty diagnostics/nativeErrors
and nativeExecutionValidated/publicBindlessExecutionAvailable=false. Source352,
quick3/3, rebuilt Node and Wasm/MinGW compilation pass. The first run's cell
capacity failure is preserved; corrected authoring charges three observations
rather than unique-resource count. CPU48/native242/independent65536 quotas stay
unchanged. Device buffer registration3 is authored before publication; embedded
intent adds8 charged bytes to the prepared submission receipt (288 to296).
Contract Complete / Prototype Complete cover CPU preparation only. Next:
authenticated native Join4/state revision, explicit cold pairing and original
copy/map/result publication, then numerical and loss/lifecycle evidence.
Evidence: build/web-webgpu-release/artifacts/bindless-continuation/
general-output-readback4-inputs-corrected/.

GeneralOutputSnapshot1 checkpoint (2026-10-11, Windows): actual Edge WebGPU
validates an explicit queue-ordered association over the exact original General
issuer/root/Dispatch storage output. Typed issuer weak-root authentication and
the existing state-bound usage projector avoid a replacement graph or a new
State access facade. Storage|CopySrc is authored before admission. Prepared and
held recording/submission Pending refuse completion; genuine queue receipt,
recorded GeneralSubmission1, callback drain, same-root commit and strict original
final state observations are required. Exact/one-less/exhausted, forged identity,
wrong output, caller drop and full quota retirement pass. Snapshot owners241/242,
visits13, source8 bytes and measured metadata436 bytes fit original authored
quotas; CPU46/48 and independent65536 domains remain unchanged. Source351/351,
quick3/3 (32.02s), final rebuilt Node (0.38s), Wasm and MinGW builds pass; Edge
finishes in37.19s with originalGeneralOutputSnapshotPassed=true and empty
diagnostics/nativeErrors; affected browser regression4/4 passes in34.82s.
Scoped Backend Validated covers association only;
copy/map/numerical/public execution, General loss, Windows runtime and all
renderer/WOCC/platform/production gates remain open. Evidence:
build/web-webgpu-release/artifacts/bindless-continuation/general-output-snapshot1-first/.

GeneralSubmission1 checkpoint (2026-10-11, Windows): actual Edge WebGPU validates
the independently opted-in Execution6/Issuer6 submission through the original
State, three recording scopes, three submission scopes and queue receipt.
The request's submission choice is frozen at record; cancel-only upgrade,
downgrade, unknown/legacy/missing opt-in/mixed-readback, forged and duplicate
submissions refuse. Held native scopes keep the original root Pending and its
completion unpublished; native drain commits the same root/journal. Caller drop,
unchanged sampled content, loss disarm, original receipt/reservation retirement
and hot allocation0/query0 pass. Source350/350, quick3/3 (24.29s), rebuilt
Wasm/Node (0.43s), MinGW compile and affected browser4/4 (33.95s) pass. Edge
finishes in37.70s with success/originalGeneralSubmissionPassed=true and empty
diagnostics/nativeErrors. Measured original submission State charge is492 bytes
(prior484); native owners240/242, CPU46/48 and authored quota domains65536 stay
bounded. Scoped Backend Validated now includes private queue completion.
nativeExecutionValidated/publicBindlessExecutionAvailable remain false; output
association/numerical readback, General physical loss, Windows native runtime,
Metal Graphics, renderer/WOCC and platform/production gates remain open.
Next General output snapshot admission is DefinedOnly in
docs/original-general-native-cold-next-contract.md. Evidence:
build/web-webgpu-release/artifacts/bindless-continuation/general-submission1-first/.

General Pending/Recording6 cancel checkpoint (2026-10-11, Windows): actual Edge
WebGPU passes immutable uniform2 and completed whole sampled read1 through the
same original seal/root/journal. Three real recording scopes, held Pending state,
strict public refusal, authentic retained admission, forged/native mutation,
caller drop, cancellation rollback, unchanged content stamps, old-graph refusal
and full quota retirement pass with hot allocation0/query0. The first fixture
reused a stale graph after rollback; it now verifies the original cancellation
and revision advance without changing production rules. Source349/349, quick3/3
(20.72s), rebuilt Node contract and MinGW compile pass; corrected Edge smoke
finishes in20.34s with diagnostics/nativeErrors empty and affected browser4/4
passes in34.14s. Windows native runtime remains unverified. Scoped Backend
Validated covers recording/cancel only; General submission, completed output
provenance/numerical readback, physical loss, public WebGPU, Metal Graphics,
renderer/WOCC and platform/production acceptance remain open. Evidence:
build/web-webgpu-release/artifacts/bindless-continuation/general-pending-recording6-rollback-validated/.

General Issuer6 preparation checkpoint (2026-10-10, Windows): actual Edge WebGPU
passes the original Callback6/Command6/Cold6/Lifecycle3 join with240 owners within
an independently authored native ceiling242 and CPU root46/48. Seven prepared
receipts/fourteen listeners, exact316-byte recording/288-byte submission overhead,
one-less/exhausted recovery, partial-failure3 cleanup/retry, sticky claim, forged/
native mutation, caller drop and cancel/final receipt/quota retirement pass with
empty diagnostics/nativeErrors. The first browser run exposed a premature test
retirement check; moving it after final issuer release preserved production
lifetime. Source348/348, quick/package/root3/3, rebuilt Wasm/Node contract,
MinGW compile and affected browser regression4/4 (40.11s) pass. Windows native
runtime remains unverified. Contract Complete / Prototype Complete and scoped
Backend Validated includes issuer preparation only; Pending inputs, actual general
execution/numerical completion and physical-loss delivery remain open. Public,
Metal Graphics, renderer/WOCC, platform and production gates remain open.
Evidence: build/web-webgpu-release/artifacts/bindless-continuation/general-issuer6-retirement-validated/.

General native Cold6/Command6/Callback6 checkpoint (2026-10-10, Windows): exact
Payload6/Freeze6/Closure8 plus Lifecycle3/ContentPlan4 now reaches the existing
device/queue connection and sticky original seal claim. Actual Edge WebGPU passes
owners223 cold /226 command within immutable native ceiling239, CPU root46/48,
exact752-byte command quota, one-less/exhausted recovery, forged/native-mutation
refusal, caller drop and final retirement without hot allocation/query. Source346,
package/root3/3 and rebuilt Wasm/Node contract pass; MinGW builds, native Windows
launch is unverified (CreateProcess WinError5 and disappearing executable; no
reported quarantine record, cause undetermined). Callback6 pre-record custody also
passes actual Edge: owners237/ceiling239, submission exact484/callback exact640
bytes, one-less/exhausted recovery, sticky/forged/native mutation, caller drop and
hot cancel/final retirement. Source347/347, quick/package/root3/3, rebuilt Node
contract and MinGW compile pass. Affected shader values, recording-cancel,
submission-loss and map-loss browser regression4/4 passes in36.05s.
This is Contract Complete / Prototype Complete and scoped Backend Validated for
cold admission/connection and pre-record callback prepare/cancel only.
General physical-loss delivery remains unvalidated. No encoder, general shader execution,
public execution or platform/production claim. Metal public Graphics, renderer
material/instance, WOCC color/depth/shadow and workload/visual/performance/P6 gates
remain open. Next: issuer/pending-content/record/submit/completion and physical loss.
See original-general-native-cold-next-contract.md and actual evidence under
build/web-webgpu-release/artifacts/bindless-continuation/general-command6-first/
and general-callback6-first/.

Completed whole sampled read plan4 validated checkpoint (2026-10-10): the existing
state plan observes unchanged Completed whole content under explicit bounded intent.
Genuine Chrome initialized texture custody reaches Closure8/Freeze6/Payload6;
Pending/rollback preserves generation/revision and restores CopyDst. Source344/344,
rebuilt assertion-enabled Host4/4, package/root2/2, Release Wasm/artifact2/2, selected
general/texture-input/ordinary-numeric Chrome scenarios, full feature profiles2/2
and affected browser4/4 pass. CPU root/plan46 stays inside Sources48; native closure216,
Freeze217 and Payload218 have independent ceilings and retain initializer storage.
Legacy closures and lifecycle1/2 refuse plan4; native cold/record/submit/general
shader completion remain closed. This is Contract Complete / Prototype Complete
and scoped Backend Validated for read planning/completed-input cold custody only.
Public WebGPU, Metal public Graphics and renderer/WOCC P4-P6 remain incomplete.
Next cold composition is DefinedOnly; see original-completed-texture-read-plan-contract.md
and original-general-native-cold-next-contract.md. Selected evidence archive is
completed-read-plan4-release-validated/, transitiveBuildInputsComplete=false.

Completed sampled-view input validated checkpoint (2026-10-10):
The existing OriginalCompletedShaderTextureInput/1 now validates actual whole
sampled 2D views against the same original completed upload parent/root, minted
content expectation and explicit shader state/stage/read footprint. Missing
expectation, CopyDst intent, missing stages, write access, forged root/view owner,
short owner budget, missing opt-in and a valid native subview all refuse. Hot
validation allocates nothing and committed state stays CopyDst: no transition
or shader execution is granted by input validation.
The native fixture follows the existing same-quota view/parent source contract.
View source owners expire after caller release; native registry weak control
blocks remain charged to the original bounded source domain until device
retirement. The outer completed-upload fixture then requires every original
quota to be zero, after actual GPU completion and callback drain. No production
capacity, wait, deadline, owner graph or completion mechanism was enlarged.
Source342/342, assertion-enabled MinGW Host/Null/Vulkan synthetic/Metal stub4/4,
package/root2/2, Wasm Release build, Compute/Graphics artifact2/2, dedicated
Chrome view-input1/1, fresh general-content1/1 and ordinary numerical geometry/
readback/completion/retirement1/1 pass. Both full browser feature profiles2/2
pass; affected browser regression and shader-usage checks are preserved too.
The three first browser failures (source quota, premature quota-zero assertion,
duplicate hot-pressure domain) are preserved. Immutable optional inputs now
use construction/emplace; source inventory follows the reused allocation helper
and checks retained initializer staging accounting rather than its old location.
Contract Complete / Prototype Complete / scoped Backend Validated applies to
completed native view-input association only. ShaderTextureContentCustody/1 has
contract/code coverage, but positive initialized-texture generic Closure8 and
its explicit CopyDst-to-ShaderRead State/Stream transition still need validation.
General cold/command/callback/issuer/shader execution/readback/completion and
public Emulated remain closed. Metal public Graphics, renderer material/instance,
WOCC color/depth/shadow, named browser GPU-driven workload and P6 remain open.
Evidence: build/web-webgpu-release/artifacts/bindless-continuation/
shader-texture-view-release-validated/manifest.json (selected inputs only;
transitiveBuildInputsComplete=false). All four full objectives remain active.

OriginalCompletedShaderTextureInput/1 validated checkpoint (2026-10-10):
The original canonical issuer validates genuine completed upload/callback drain,
committed same root/journal, canonical source/target owners and exact minted
content generation/revision. Whole sampled 2D RGBA8/RGBA16 inputs have independent
upload/mip/texel-byte/join-visit ceilings; no replacement graph or receipt exists.
Fresh assertion-enabled MinGW Host4/4, source341/341, package/root2/2, Wasm Release
build and Compute/Graphics artifact2/2 pass. Genuine Chrome texture-input1/1
verifies20 bytes across2 mips, pending/stale/forged/budget/native-mutation refusals,
caller-drop retention and final quota retirement. Fresh general-content1/1 and
independent ordinary numeric geometry/readback/completion/retirement1/1 pass.
The affected five browser tests pass across4/4 plus the separate shader-usage1/1.
Both complete browser feature profiles pass2/2 in127.68s with empty diagnostics
and nativeErrors. Optional emdawn stderr is no longer a success prerequisite:
four negative read paths verify actual native/intercepted invocation, exact RHI
refusal, unpublished/unchanged output, callback/unmap drain and zero quotas.
The first range-log failure, subsequent three optional-log profile failures,
initial JS report timeout and private-method compile failure remain preserved.
No production capacity/wait/deadline or continuation ceiling was enlarged.
Contract Complete / Prototype Complete / scoped Backend Validated covers the
completed texture input prerequisite only. Shader view/range/state association,
Closure8 texture custody and general cold/command/callback/issuer/shader execution
remain required; native shader execution and public Emulated are false. Metal
public Graphics, renderer material/instance services, WOCC color/depth/shadow,
named browser GPU-driven workload and P6 acceptance remain incomplete.
Evidence: build/web-webgpu-release/artifacts/bindless-continuation/
shader-texture-input-release-validated/manifest.json (selected hashes only;
transitiveBuildInputsComplete=false). All four full objectives remain active.
See original-general-shader-content-contract.md.

OriginalGeneralShaderContent/1 validated checkpoint (2026-10-10):
Staging4 independently initializes immutable whole UniformBuffer/ReadOnlyStorageBuffer
using the existing canonical native queue/scopes/loss/completion/callback chain.
Closure8 ShaderBufferContent1 and Payload6 consume the same original buffer lease,
exact descriptor class/whole range, frozen bytes and bounded source custody.
Rebuilt assertion-enabled Host/Null/Vulkan synthetic/Metal stub4/4 passes5.24s;
corrected source336/336 passes9.869s, package/root2/2 and fresh Wasm Release/artifact2/2
pass. Genuine Chrome general-content1/1 passes with its new content flag true,
native write/scopes3/queue1 completion, caller-drop retention and final quota
retirement; affected browser5/5 passes44.20s, and independent ordinary numeric
geometry/readback/completion/retirement passes. Exact native closure78/94,
Freeze79/95 and Payload80 owners independently include canonical initializer
custody; State44/48 and CPU65536 quotas remain unchanged. Hot allocation/query0.
First compiler failures remain preserved; no production quota/wait/deadline or
continuation ceiling is enlarged. The native fixture covers UniformInput16 only;
ReadOnlyStorageBuffer has contract/code coverage, not native execution proof here.
Contract Complete / Prototype Complete and scoped Backend Validated apply to
initialized uniform content custody only. General native shader execution and
public Emulated remain false. SampledInput initialization and general cold/command/
callback/issuer/execution/readback/completion joins remain required. Metal public
Graphics, renderer/WOCC P4-P6 and all four full objectives remain incomplete.
Reports: build/web-webgpu-release/artifacts/bindless-continuation/general-content-final/report.json
and general-content-ordinary-numeric/report.json; selected evidence archive:
general-content-release-validated/ (not full transitive build provenance).
See original-general-shader-content-contract.md.

OriginalTypedNativePayload6 (2026-10-10; selected native inputs validated) consumes
exact Freeze6/Closure8 in the existing immutable records/pass builder with
independent opt-in and work/owner/CPU ceilings. Native retained owners compose
the authored Freeze6 ceiling and one payload owner; old profiles keep Sources'
CPU graph bound. Tables, geometry/readback and legacy tuples remain refused.
Null validator, Vulkan/Metal missing selected payload and WebGPU private
Prototype native-input preparation are explicit. Rebuilt Host/Null/Vulkan
synthetic/Metal stub4/4, source333/333, package/root2/2 and fresh Wasm artifact2/2
pass. Actual Chrome Payload6 passes1/1: nine records, seven projected native calls,
group0 exact original pipeline/set, owner66, metadata2268 and final quota retirement;
one-less/exhausted/forged/native mutation checks and hot allocation/query0 pass.
Affected browser regression5/5 and independent ordinary numerical geometry pass.
General shader nativeExecutionValidated=false and public executionfalse remain
explicit. Contract Complete / Prototype Complete is scoped to native-input preparation.
Cold1-5, initialized contents, actual record/submit/
completion and public Emulated remain separate closed gates. This does not
advance Metal Graphics, renderer/WOCC P4-P6 or platform/production readiness.
See original-general-native-closure-contract.md.

OriginalGeneralNativeClosure/1 + Freeze6 (2026-10-10; selected custody validated)
adds independent Closure8 custody over the committed GeneralWrite1 group and
canonical scalar resources, including nonmemory sampler observations. Existing
Registry7, Inputs3/State3/Stream3 and materialized original root remain
authoritative; old profiles refuse. No public recording/completion authority is
issued. Freeze6 consumes the same general Projection5/Closure8 with its independently
authored native custody ceiling; old profiles remain closed. The first Chrome
closure run found a fixture weak-control-block quota retention, now corrected;
combined rebuilt Host4/4, source330/330, package/root2/2, Wasm artifact2/2 and real
Chrome closure/freeze1/1 pass. Affected browser5/5 plus independent ordinary numeric
completion/retirement pass. Exact native owners64/80 and frozen65/81 retain the
unchanged State44/48/CPU65536 ceilings; hot allocation/query0. Both new flags are
true with empty errors but general nativeExecutionValidated/publicfalse. The old
unknown-Freeze6 negative failure is preserved and now tests Freeze7. Selected hashes
are archived in general-closure-freeze-release-validated/; not transitive build
provenance. Native payload/initialized content/record/submit/real completion remain
required. See original-general-native-closure-contract.md. All four full objectives
remain incomplete.

OriginalGeneralDescriptorWrite/1 (2026-10-10, selected transaction validated) extends
the existing scoped descriptor transaction with complete scalar resource groups.
Frozen canonical dependencies, native backing/allocation observations, exact
physical coverage and independent quotas precede genuine scopes and atomic L2
publication. Identity3/GeneralWrite1 proves creation metadata only. Ordinary
command bypass, legacy identity profiles and public Emulated remain closed.
Rebuilt assertion-enabled Host4/4, source327/327, package/root8/8, Wasm artifact2/2
and genuine Chrome transaction1/1 pass; affected browser regression5/5 and
independent ordinary numerical readback/completion also pass. Authentic Compute
creation covers uniform, storage/sampled views and sampler with exact/less/
exhausted quotas, real scopes3, atomic revision, native identity mutation and
failed-replacement preservation. No new RO/RW-buffer or Graphics GPU proof.
See original-general-descriptor-write-contract.md. Native closure/freeze/payload
and actual command/completion joins, Metal Graphics, renderer/WOCC and full
acceptance are still required.

OriginalGeneralDescriptorBinding/1 (2026-10-10; selected borrowed entries validated) adds the
six-class scalar borrowed-entry prerequisite to the existing original-source chain.
Independent opt-in and conservative whole/general footprints precede accepted native
pipeline mapping. Sampler observations reuse the registered original handle/device.
Preparation creates no group or receipt and authorizes no execution. Rebuilt Host4/4,
source326/326, package/root8/8, Wasm artifact2/2 and Chrome entries1/1 pass; affected
browser regression5/5 and independent ordinary numeric readback also pass. The native
fixture covers uniform, storage/sampled views and sampler, including frozen nonnull
identity changes, complete outputs and hot allocation/query0. The unrequested ABI and
unused native size initialization failures are preserved. Selected current evidence is
archived under general-binding-release-validated/, not complete transitive provenance. The existing
scoped descriptor transaction and generic closure/completion remain required before
public WebGPU Emulated. Metal Graphics, renderer/WOCC and P4-P6 remain incomplete.

OriginalSamplerNativeLossJoin/1 (2026-10-10; selected source join validated) reuses the exact
registry7 TypedNativeLossJoin1 domain/subscription and the independently opted-in
sampler registration. WebGPU validates authentic sampler handle/device against
the captured native-loss source; legacy/table profiles, foreign/missing payloads
refuse. Null is validator-only and Vulkan/Metal source joins refuse explicitly.
This grants no recording, submission, completion or public Emulated authority.
The existing general original8 projection now uses registry7 with its existing
bounded observed-loss request. Rebuilt Host/Null/Vulkan synthetic/Metal stub4/4,
source325/325, package/root8/8, Wasm artifact2/2, genuine Chrome loss/projection1/1
and affected browser regression5/5 pass; independent ordinary numeric geometry
also passes. Actual independent browser device loss, caller drops and original
quota retirement are verified, with public/nativefalse and empty errors. See
original-sampler-custody-contract.md. All four full objectives remain open.

General original resource/dependency projection validated (2026-10-10):
Consumption2/3 and RenderUsage2/3 now carry independent general-resource intent;
Usage5 projects uniform buffers and ordinary sampled/storage texture views from
the same original source/parent/stage/range graph. Legacy profiles refuse these
fields and native Freeze/Payload/Cold/public authorization stays closed. New
L2 input-profile matching admits opted-in ordinary Inputs3 while preserving
table Inputs4 and geometry Inputs5. Rebuilt assertion-enabled MinGW Host/Null/
artifact5/5 (4.93s), source322/322 (9.372s), genuine Release Chrome projection1/1
(2.20s), affected value/table/geometry-lifecycle5/5 (45.04s), and independent
ordinary numeric geometry pass. Diagnostics/native errors are empty and public
execution remains false. The first successful C++ projection timed out because
the runner omitted its terminal marker; that failed attempt is preserved and the
report hook is now connected without changing success gates or deadlines. This
is Contract Complete / Prototype Complete for CPU Usage5/Dependency5 only.
Dependency5 projects three authentic uniform/storage/sampled memory dependencies,
exact endpoints and frozen positional replay, with one-less refusal and hot0-query0.
Freeze5 still rejects this general profile. Native descriptor write/identity and
closure custody for ordinary textures/samplers, initialization/payload/recording/
completion and public authorization remain required. See
`original-general-shader-resource-projection-contract.md`.

The preceding general usage-scope5 value prerequisite passes assertion-enabled
Host/Null5/5 (2.20s), source319/319 (9.110s), selected Release browser2/2 (23.33s)
and independent ordinary numeric geometry. Public execution remains false;
diagnostics/nativeErrors are empty. Selected380 hashes verify in
`general-shader-usage5-values-validated`; not full transitive provenance.
The earlier17/17 lifecycle result predates this shared-header value increment.

Original31 callback-registration uncertainty scoped validation closed (2026-10-10):
current rebuilt assertion-enabled Release Chrome passes the two authentic listener
registration faults2/2 (31.63s), then the full affected matrix17/17 (319.52s).
Queue/scope registration attaches a real listener then throws. Both terminal cases
keep original owners/results, reject retry/publication/completed submission and
retire only after actual-device-loss and foreign callback drain, with balanced
147/147 scopes, encoder1/copy1/queue1/map0/read0/unmap0 and empty errors.
Independent ordinary numeric geometry passes with publicfalse and no native errors.
MinGW Host/Null5/5 (3.98s), source318/318 (9.623s), Python/generated JS syntax pass.
Selected evidence is frozen at table-geometry-registration-release-validated;
not complete transitive provenance. No quotas/waits/deadlines/memory/continuation
ceilings change. Null validates, Vulkan/Metal reject absent composed payload,
WebGPU grants private Prototype/scoped Backend Validated only. Public Emulated
remains DiagnosticFailure/executablePayload=false. Next require generic resource/
stage/operation and public recording/completion adapters; Metal public Graphics,
renderer material/instance services, WOCC P4-P6 and the full goal remain incomplete.

Current composed uncertainty validated (2026-10-10): rebuilt Release Chrome
passes15/15 (284.22s) in one invocation, including original31 normal/cancel/loss,
authentic encoder/submit/map after-invocation throws and recording/submission
partial scope-push faults, procedural table and ordinary lifecycle regressions.
Independent current ordinary numeric geometry also passes. Rebuilt Host/Null5/5
passes4.48s and source317/317 passes9.144s; Python/generated artifact/runner JS
syntax pass. Unknown push effects require no guessed pops, no native work after
the failed push, no retry/publication and retention until real loss. Initial
fixture custody-shell assertion failure is separately archived with341 verified
hashes; the corrected Complete path observes original retained Result2. Current
selected provenance is archived in table-geometry-invocation-release-validated;
neither archive is full transitive build provenance. Public WebGPU remains
DiagnosticFailure/executablePayload=false; generic resource/stage/operation,
callback-registration coverage, Metal Graphics and renderer/WOCC P4-P6 remain
independent incomplete gates. No production ceilings or waits change.

Affected-path regression complete for this private increment (2026-10-10):
current ordinary original19 numeric geometry/readback scenario passes, and its
three cancellation/loss CTests pass3/3 (17.46s), independently of original31
table/geometry4/4 and procedural table3/3. Public execution stays false, with
clean native errors/diagnostics and unchanged budgets/waits/continuation/fixed
memory. Current Host/Null5/5 and source316/316 pass. Generic resource/stage/
operation and native-invocation uncertainty coverage still block public Emulated.
This does not complete P3, Metal Graphics, renderer/WOCC or platform acceptance.

Legacy procedural table lifecycle regression passes3/3 (2026-10-10, Release,
99.07s): numerical completion and both submitted/map-pending physical loss.
Together with same-original31 geometry/table4/4 and current Host/Null5/5,
this validates the scoped owner-projection/lifecycle increment. Ordinary
original19 geometry regression is being rebuilt because its shared headers
changed. No public Emulated, Metal Graphics or renderer/WOCC gate is promoted.

Original31 table/geometry lifecycle validation (2026-10-10): current
assertion-enabled Release Chrome passes4/4 (73.49s): numeric completion,
recording-cancel, submitted physical loss and map-pending physical loss. Success
requires genuine completion/publication; all terminal cases retain unpublished
output until authentic callbacks drain and all original quota domains retire.
Map-loss reads0/unmaps1 and never reports completed rendering. Host/Null5/5
(3.15s), source316/316 (9.427s) and generated JS/runner syntax pass. Failed
fixture retention assertions remain separately archived. Legacy table lifecycle
regression is running against unchanged artifacts. No production waits, budgets,
continuation ceilings or public capabilities change. Generic operation/stage
and NativeInvocationUncertain coverage, public Emulated authorization, Metal
Graphics, renderer/WOCC and all P4-P6 acceptance remain incomplete.

Table+indexed geometry+readback private execution validated (2026-10-10): Release
Chrome passes1/1 (18.70s), preserving original31 and the compiler-issued capacity3
depth-table/vertex-instance ABI. Exact half-float numeric output, genuine
queue/map/read/unmap, same-root commit/publication and final quota retirement pass.
The original pipeline owner projection now follows the explicit ordinary versus
expansion input; the previous null ordinary-artifact dereference is repaired.
Legacy creation/procedural-table completion pass2/2 (47.47s), current rebuilt
Host/Null5/5 (4.73s) and source316/316 (9.717s) pass. Selected278 hashes verify
in table-geometry-completion-release-validated; not full transitive provenance.
Same-original31 cancellation/loss coverage is being built and must pass separately.
No waits/budgets/continuation ceilings are increased. This is scoped private
Backend Validated only; public WebGPU, Metal Graphics, renderer/WOCC and P4-P6
remain incomplete. The preceding Debug Pending failure remains historical evidence.

Current table/geometry/readback boundary (2026-10-10): actual original31 recording,
submit and copy occur, and native mapAsync is issued. The Debug browser test
terminates with RHI-WEBGPU-TYPED-SUBMISSION-PENDING before mapped read/unmap;
numeric completion and retirement are not validated. Current rebuilt MinGW
retention/three artifact variants/Null smoke pass5/5 (3.31s), and source inventory
passes316/316 (9.986s). Release validation is pending; no completion wait, native
quota or continuation ceiling is enlarged to obtain success. Public WebGPU
remains DiagnosticFailure/executablePayload=false. Metal public Graphics,
renderer material/instance services, WOCC P4-P6 and the full goal remain open.
See `original-geometry-readback-composition-contract.md` for the exact composition
and failed browser report. Historical public WebGPU execution evidence below
does not override the current closed authorization gate.

GraphicsGeometry compiler/preflight/creation prerequisite validated (2026-10-10):
The existing table expansion compiler now issues a separate complete-program
identity with vertex location0 Float2 and instance locations1/2 Float2/Float4.
Two explicit stream budgets and actual created-device vertex limits gate immutable
pipeline creation. Codegen8/8, source316/316, assertion-enabled Host/Null/artifact8/8,
MinGW/Wasm and syntax pass. Real Chrome final legacy+geometry creation2/2 passes9.51s;
22 geometry layouts have exact strides8/24 and vertex/instance steps. Encoder,
submit, copy and map counts remain0. This is compiler/preflight Contract Complete /
Prototype Complete with scoped native creation evidence only. Same-root indexed
geometry/table/readback execution and public Emulated authorization remain open;
Metal Graphics, renderer/WOCC and platform/performance acceptance are incomplete.

Geometry/readback lifecycle full regression (2026-10-10): unchanged current
Execution5 artifacts pass both full Chrome profiles2/2,317.71s/323.99s,641.71s total.
Numeric geometry execution and21-domain retirement remain private, with clean
errors and publicfalse.117 archive hashes verify over97 selected compiled inputs
plus evidence; served JS/Wasm/HTML match. This is not full transitive provenance.
GraphicsGeometry depth-table shader/preflight implementation is now in progress;
it reuses the existing expansion compiler and Closure7 table domain, with an exact
vertex/instance ABI and actual created-device limits. New builds/native tests are
required. Public Emulated, Metal Graphics and renderer/WOCC acceptance stay open.

Geometry/readback Execution5 same-artifact lifecycle validation (2026-10-10):
Current MinGW/Wasm builds, source316/316 (9.423s), assertion-enabled Host/Null/
package/root/geometry6/6 (2.34s) and Node syntax pass. Named real Chrome lifecycle
cases3/3 pass:recording-cancel38.31s,submission-loss37.43s,map-loss36.43s,112.19s
total. Actual loss invalidates the same original19 root before drain; held native
scope/map promises keep owners alive through partial drain. Final real drain clears
the same abandoned/invalidated journal and all21 quota domains retire. All failure
cases keep immutable output unpublished and completion/render validation/public
execution false; diagnostics/nativeErrors empty, transport complete. The same
current compiled artifacts pass the normal selected numeric test1/1 (markers37.328s)
with four blue RGBA8 pixels, genuine queue/map/scopes, same-root commit and21-domain
retirement. Exact Command5 shell/ancestor ceiling preservation and weak-control-
block retirement fixes are included. The initial owner-ceiling failure and EM_ASM
macro compile failure are retained; neither is counted as success. The lifecycle
checkpoint verifies112 hashes over97 selected compiled inputs plus evidence, not
full transitive provenance. Current unchanged artifacts now run full enabled/
disabled regression; results pending. Earlier positive owner-checkpoint full2/2
(316.94s/334.54s) remains confined to its own96 archived inputs/108 hashes.
Contract Complete / Prototype Complete / scoped Backend Validated applies only to
this private geometry/readback execution and cancellation/loss closure. Public
WebGPU remains DiagnosticFailure/executablePayload=false. Table+geometry+readback,
generic resource/stage/operation and native invocation uncertainty coverage remain
required before public authorization, along with Metal public Graphics, WOCC
shader coverage, renderer material/instance/color/depth/shadow, named GPU-driven
browser workload and P6 visual/Release/performance/lifecycle/platform/server gates.
All four full objectives remain incomplete; no full Platform Validated or
Production Ready promotion.

Geometry/readback Execution5 full regression closed (2026-10-10):
Both complete Chrome feature configurations pass2/2:316.94s enabled and334.54s
without indirect-first-instance,651.49s total. Reports prove the private composed
indexed geometry/readback marker and21-domain retirement, clean diagnostics/native
errors and complete transport; public execution remains false. Both reports/logs
are preserved with96 selected compiled inputs under the owner checkpoint;108
hashes verify and served JS/Wasm/HTML match its archived inputs. These are selected
provenance, not a complete transitive build archive. Current lifecycle source316/316
and assertion-enabled Host6/6 pass; new recording-cancel/submission-loss/map-loss
Wasm/native tests are pending. The frozen positive result does not authorize the
new lifecycle sources, public Emulated, Metal Graphics or renderer/WOCC. All four
full objectives and P4-P6 remain incomplete.

Geometry/readback Execution5 composed lifecycle continuation (2026-10-10):
The selected positive Execution5 browser result remains frozen in its owner
checkpoint; its full enabled/disabled regression is still running those unchanged
served artifacts. Current source now extends the same builder/Execution5 profile
with recording-cancel, submission-loss and map-loss named cases. Recording scopes,
submission scopes and genuine map promise/scopes are retained and partially drained;
real device destruction must invalidate the same original root before authentic
full drain. No output publication or successful completion is allowed, and all21
quota domains must retire after caller command/result drops. Existing native map
probe and error-scope fixtures are reused; no production quota/deadline/Asyncify
changes. Each case has an independent browser CTest/report under the existing
bounded deadline and reports renderedValidationSucceeded=false/public=false.
Current source316/316 passes9.748s and source HTML JavaScript syntax passes. The
rebuilt assertion-enabled MinGW Host/Null/package/root/geometry6/6 passes2.34s.
Lifecycle Wasm/native validation remains pending until the frozen positive full
regression finishes; these sources do not inherit its positive binary result. Public Emulated, Metal public Graphics,
renderer/WOCC P4-P6 and all four full objectives remain incomplete.

Geometry/readback Execution5 selected native validation (2026-10-10):
Repaired MinGW/Wasm builds and Node syntax pass; current source315/315 passes9.029s
and rebuilt assertion-enabled Host/Null/package/root/geometry6/6 passes3.86s.
Real selected Chrome1/1 passes with same-original19 indexedDraw1, vertex2, index1,
instances2/firstInstance1, copy1, real queue1/map1 and scopes3-3-3. The same Pending
journal commits only after genuine completion and immutable Result2 publishes all
four blue RGBA8 pixels at stride256. Caller command/result owners drop before
submission; all21 quota domains retire. Hot record/submit allocation0/query0 pass,
and diagnostics/nativeErrors are empty with complete progress transport. Selected
markers occur at38.672s. The successful archive has103 verified hashes over96
selected compiled inputs plus evidence (not full transitive provenance); the first
owner-ceiling failure remains separately preserved. The current unchanged artifacts
are running full enabled/disabled Chrome regression. This is scoped private
Prototype Complete / Backend Validated composition only. Public WebGPU remains
DiagnosticFailure/executablePayload=false. Composed cancellation/physical loss,
table+geometry+readback and generic operations remain required, alongside Metal
public Graphics, renderer/WOCC and P4-P6; all four full objectives remain incomplete.

Geometry/readback Execution5 first browser result and ceiling repair (2026-10-10):
The first real selected Chrome run rejects RHI-WEBGPU-TYPED-CALLBACK-OWNER-CAPACITY
before composed recording; nativeErrors is empty and transport complete. Existing
14-command geometry execution/retirement and19-command frozen payload pass. The
Command5 fixture declared only the shell minimum even though Cold5/Sources/seal
already declare the complete callback/result/issuer owner inventory. It now carries
that existing complete ceiling without increasing any production quota; the shell
still charges exactly its own minimum. One-less shell and callback minima reject
before allocation. The failed report/build and96 selected compiled inputs are
preserved under geometry-readback-execution5-checkpoint/ with100 verified hashes
(not full transitive provenance). Repaired builds/native validation pending. Public
WebGPU remains DiagnosticFailure/executablePayload=false; all four objectives,
Metal public Graphics, renderer/WOCC P4-P6 and full acceptance remain incomplete.

Geometry/readback Execution5 implementation (2026-10-10, validation pending):
Exact Cold5-to-Join3 mapping is now an L2 value rule; Cold3 still maps only Join2
and legacy/unknown profiles return0. Result2, retained mapper and publication
consume that rule and authentic same-root seal/Pending/request/native receipt.
Callback5 and Issuer5 reuse existing bounded scopes, map receipts, native loss
observer, Recording and Submission. Execution5 requires explicit completion1;
Null validates and Vulkan/Metal reject absent composed payloads. Sources declares
four additional map slots plus Result storage/quota/snapshot before publication;
the original Lifecycle2 seal authors command/callback/result/issuer owner demand.
No default quota, timeout or Asyncify capacity changes. The fixture holds scopes,
checks Pending/unpublished output, rejects legacy execution/missing map/opt-in and
mutated destination, records indexed geometry and copy in one encoder, waits for
real queue/map/scopes and verifies four blue RGBA8 pixels at row stride256 before
same-root commit and21-domain retirement. Current source315/315 passes9.038s;
assertion-enabled MinGW Host/Null/package/root/geometry6/6 passes5.78s. The current
Host rebuild is complete; Wasm build and composed native validation are pending.
Earlier Cold5/Command5 evidence is confined to archived inputs. Command5 full
enabled/disabled browser2/2 passes315.34s/318.05s,633.40s total. Its108 archive
hashes verify over96 selected compiled inputs plus evidence (not full transitive
provenance), and served JS/Wasm/HTML matched those archived bytes before rebuilding. Public Emulated stays DiagnosticFailure/executablePayload
false. Loss/cancellation, table/geometry/readback and generic resource/stage coverage
remain independently required, alongside Metal public Graphics, renderer/WOCC and
all P4-P6 gates. All four full objectives remain incomplete.

Geometry/readback Command5 selected validation (2026-10-10):
Rebuilt MinGW/Wasm and Node syntax pass; source314/314 passes9.556s and rebuilt
assertion-enabled Host/Null/package/root6/6 passes2.39s. Real selected Chrome1/1
passes, with Command5 same-original19/Cold5/Lifecycle2 connection and20-quota
retirement markers at28.187s; diagnostics/nativeErrors empty, transport complete.
Legacy1-4, missing opt-in, one-less owner/metadata budgets, forged owners, exhausted
allocation recovery, duplicate claims and actual native pipeline/descriptor mutation
reject. Successful retained validation allocates0/queries0. Dropping the caller's
admission retains it through the genuine command; all20 quota observers drain.
Selected evidence and96 frozen selected compiled inputs are archived under
geometry-readback-command5-checkpoint/ with103 verified hashes; not complete
transitive provenance. Contract Complete / Prototype Complete applies only to
private connection custody. No composed encoder/copy/map/numerical output or
public execution is proven. Full enabled/disabled browser regression is running
with these unchanged artifacts and the existing360s deadline; full results pending.
Next: same-original Result2 storage/Map1 callbacks, Callback5/Issuer5/Execution5,
genuine copy/map/queue completion, numerical geometry output, loss/retirement and
table/geometry/readback/generic operation coverage. Public Emulated remains
DiagnosticFailure/executablePayload=false. Metal public Graphics, WOCC shader
ABI/stage/operation coverage, renderer material/instance services and WOCC
color/depth/shadow, named real GPU-driven browser workload and P6 visual/Release
performance/lifecycle/platform acceptance remain required. All four full objectives
remain active; no Platform Validated or Production Ready claim.

Geometry/readback Cold5 full regression closed (2026-10-10):
Selected Chrome1/1 and both full enabled/disabled profiles2/2 pass:306.74s and
313.92s,620.67s total. Reports have complete transport, empty diagnostics/nativeErrors,
Cold5/20-quota retirement and corrected nine-case payload validation. Public
execution and composed numerical output remain false. The Cold5 archive verifies
107 hashes over96 selected compiled inputs plus evidence (not transitive provenance).
Command5 source314/314 passes9.556s; rebuilt assertion-enabled Host/Null/package/
root6/6 passes2.39s. Its Wasm/native tests are pending, and the Cold5 browser
results apply only to the prior frozen sources. Callback5/Issuer5/Execution5,
public Emulated, Metal Graphics, renderer/WOCC and all full acceptance remain open.

Geometry/readback Cold5 selected validation and Command5 continuation (2026-10-10):
Cold5 rebuilt MinGW/Wasm and JavaScript syntax pass. Assertion-enabled Host/Null/
package/root6/6 passes5.11s; source313/313 passes9.641s. Real selected Chrome1/1
passes with original19 Cold5/Lifecycle2/Result2/Map1 callbacks12 and final20-domain
retirement markers at28.047s, empty diagnostics/nativeErrors and complete transport.
The selected archive preserves96 compiled inputs plus evidence with103 verified
hashes, not transitive provenance. Public and numerical output flags stay false.
Full enabled/disabled regression is running against unchanged Cold5 artifacts.
Command5 sources now open only exact Cold5 private connection custody through the
existing native device/queue shell and one-shot seal claim. No encoder, callback,
map, record, submit or public authority is granted. Null validator and Vulkan/
Metal missing-payload outcomes remain explicit. New negatives cover legacy1-4,
missing opt-in, one-less owners, forged ownership, exhausted metadata followed by
recovery, duplicate claim and allocation/query-free validation. The original
Lifecycle2 seal reserves the shell's three owners before issuance; no Source or
production default is expanded after publication. Command5 native validation is
pending and does not inherit Cold5 results. Callback5/Issuer5/Execution5, Metal
public Graphics, renderer/WOCC and all four full objectives remain incomplete.

Geometry/readback Cold5 continuation (2026-10-10, native validation pending):
Payload5 full Chrome regression is terminal0/2 (612.92s): both configurations
have empty diagnostics/nativeErrors but typedNativePayloadFrozen=false. The
runner still required exactly eight old payload observations; the composed
geometry-readback helper legitimately adds the ninth. The repair checks all
nine observations by exact authored scenario multiplicity, including all eight
legacy cases, rather than weakening their checks. Failed full reports/logs and
96 unchanged selected compiled inputs are retained in the Payload5 archive;
108 hashes verify (selected provenance only, not complete transitive provenance).
Cold5 now consumes exact Payload5/Freeze5/Closure7 and Lifecycle2, with mandatory
Result2/Map1. Sources declares one map's12 callback participants before publication;
production quotas/defaults are unchanged. The fixture exercises preclaim failure,
wrong Lifecycle1, one-less callback budgets, exact/one-less/exhausted metadata and
allocation/query-free revalidation. Independent Submission/Callback observers add
two domains to the prior18; final20-domain retirement is required. Source313/313
passes9.641s. Host/Wasm/native validation pending. Command5/Callback5/Issuer5/
Execution5, composed GPU copy/map/numerical output and public Emulated stay closed.
Metal public Graphics, renderer material/instance services, WOCC color/depth/shadow,
named real GPU-driven browser workload and P6 acceptance all remain required.
Earlier checkpoints below apply only to their own archived source/artifact versions.

Geometry/readback Payload5 selected validation (2026-10-10):
Rebuilt MinGW/Wasm pass; Host/Null/package/root6/6 passes2.32s, source312/312
passes10.051s, HTML/generated JavaScript syntax and diff checks pass. Selected
real Chrome passes1/1 with all three custody/Freeze5/Payload5 markers and final
18-quota retirement at21.907s; diagnostics/nativeErrors empty and transport
complete. Same original19 program retains two vertex bindings, one index bind,
one indexed draw, one transfer pass/readback/copy, exact original Source2 and
destination; native input projection has13 calls and five dependencies. Shared
fixture exact/one-less/exhausted/forged/native-mutation probes and allocation-free
query-free revalidation pass. Evidence plus96 unchanged selected compiled inputs
is archived at geometry-readback-payload5-selected-validated/ with104 verified
hashes; not complete transitive provenance. This is Contract Complete / Prototype
Complete for the selected immutable payload preparation, not actual composed GPU
execution/readback. Both full ordinary browser configurations are running against
these artifacts; full results pending. Cold5/Command5/Callback5/Issuer5/Execution5
stay closed. Next: same-original lifecycle2/reservation, explicit Result2/Map1 and
real scope/queue/map completion, numeric geometry output and loss/retirement;
table/geometry/readback and generic operations remain independently required.
Public Emulated, Metal public Graphics, renderer/WOCC P4-P6 and all four full
objectives remain incomplete. No Platform Validated or Production Ready claim.

Geometry/readback Freeze5 regression closed (2026-10-10):
Selected Chrome1/1 and full enabled/disabled configurations2/2 pass:285.81s and
302.78s,588.60s total. Exact19-command Closure7/Freeze5 projection, prior geometry
execution and18-quota retirement pass; diagnostics/nativeErrors empty, transport
complete, public execution false. The freeze archive now has110 verified hashes
and96 unchanged selected compiled inputs; not complete transitive provenance.
This is Contract Complete / Prototype Complete for the selected frozen CPU
candidate, not GPU execution/readback proof. All compiled inputs were frozen
before subsequent Payload5 edits, so that version does not inherit these results.
Payload5 MinGW rebuild and Host/Null/package/root6/6 pass2.32s, source312/312
passes10.051s and HTML JavaScript syntax passes. Its Wasm build/native validation
are pending. Cold5/Command5/Callback5/Issuer5/Execution5, public Emulated, Metal
public Graphics, renderer/WOCC and all four full objectives remain incomplete.

Geometry/readback Payload5 implementation (2026-10-10, validation pending):
Freeze5 selected Chrome passes1/1 with exact dependencies5/contributions11/
passes22/visits242/pairs55/endpoints11, hot allocation0 and complete18-quota
retirement; diagnostics/nativeErrors empty, transport complete, numeric/public
flags false. Rebuilt Host/Null/package/root6/6 passes3.70s, source312/3129.314s,
MinGW/Wasm and Node syntax pass. Selected evidence plus96 compiled inputs is
archived under geometry-readback-freeze5-selected-validated/ with104 verified
hashes, not complete transitive provenance. Freeze5 full profiles are running;
these artifacts were frozen before the following Payload5 source changes.
Payload5 now reuses the original record walker with L2 geometry/readback profile
predicates and exact Freeze5/Closure7/Join3 correspondence; older3/4 tuples stay
strict. The reused native fixture helper counts authored transfer/copy work and
checks original geometry/readback endpoints, exact/one-less/exhausted budgets and
native mutations. The runner requires an independent Payload5 marker. Source
312/312 passes10.051s. Payload builds/native validation pending; projected19
commands/13 calls are not yet verified. Cold5/Command5/Callback5/Issuer5/Execution5
and public Emulated remain closed. Metal public Graphics, renderer/WOCC P4-P6
and all four full objectives remain incomplete.

Geometry/readback ordinary custody regression closed (2026-10-10):
Selected real Chrome1/1 and both full ordinary configurations2/2 pass:283.55s
enabled,295.25s without indirect-first-instance,578.82s total. All reports have
complete transport and empty diagnostics/nativeErrors; original19 Join3/Closure7
custody and18-quota retirement pass alongside prior14-command geometry execution.
Public execution and numericalRenderedReadback remain false. MinGW/Wasm builds,
Host/Null/package/root6/6 (2.75s) and source312/312 (9.318s) pass. Evidence is
geometry-readback-custody-domain-validated/ with96 frozen selected inputs and110
verified archive hashes; not complete transitive provenance. Scoped private
custody/retirement is validated only for this ordinary descriptor fixture;
table/geometry/readback, composed execution/readback and loss remain open.
These compiled inputs were frozen before subsequent Freeze5 source edits:
Freeze5 builds/native validation are still pending and do not inherit this full
regression result. Payload5 and all later execution/public gates remain closed.
Metal public Graphics, renderer/WOCC and all four full objectives remain incomplete.

Geometry/readback Freeze5 implementation (2026-10-10, validation pending):
Existing shared frozen Dependency4 machinery now admits explicit Freeze5 over
exact Closure7, retaining the original root/program and quota. L2/four-backend
matrix keeps Null validator-only, Vulkan/Metal unavailable, and grants WebGPU
CPU projection only; no native execution/synchronization or public authority.
The19-command fixture declares eleven contributions, five dependencies,22 passes,
242 visits,55 pairs and11 endpoint contributions; exact native counts are still
pending rebuilt validation. Wrong Freeze4, missing opt-in/readback, one-less owner,
dependency/pair/visit budgets and allocation-free revalidation are tested. The
runner requires a separate Freeze5 marker alongside custody and18-quota retirement.
Payload5/Cold5/Command5/Callback5/Issuer5/Execution5 remain closed. Current sources
are not the binary under the still-running custody full-browser regression;
its enabled profile passes283.55s, disabled profile pending. Prior selected custody
sources/artifacts are preserved with104 verified hashes and96 selected inputs at
geometry-readback-custody-domain-validated/; not complete transitive provenance.
All four full objectives remain incomplete; new builds/native validation pending.

Geometry/readback ordinary-descriptor custody selected validation (2026-10-10):
MinGW/Wasm rebuilds pass; source312/312 passes9.318s and rebuilt assertion-enabled
Host/Null/package/root6/6 passes2.75s. Selected real Chrome passes1/1; both new
original19 Join3/Closure7 custody and18-quota retirement markers arrive13.641s,
alongside the existing14-command stream execution/retirement. Diagnostics and
nativeErrors are empty and progress transport is complete. This validates only
private cold custody/retirement in the ordinary-descriptor domain, not table
composition, frozen payload, composed recording/copy/map or numerical rendered
output. PublicBindlessExecutionAvailable and numericalRenderedReadback remain
false. Both full ordinary browser configurations are running against the same96
frozen selected inputs with unchanged360s per-profile deadline. No full-regression
success is claimed yet. Next: Freeze5/Payload5 and the original completion/loss
consumers. Metal public Graphics, renderer/WOCC P4-P6 and all four full objectives
remain incomplete.

Geometry/readback pipeline-domain correction (2026-10-10, validation pending):
The registration repair's selected Chrome report is terminal failure with
RHI-ORIGINAL-TABLE-EMISSION-SOURCE-VERSION. Existing14-command stream execution
and retirement pass; new Closure7 custody and numeric output do not. The report,
logs and96 unchanged selected Wasm/source inputs are archived at
geometry-readback-custody-pipeline-profile-failure/ with102 verified hashes;
not complete transitive provenance. L2 pipelineSourceVersion now preserves the
declared ordinary /1 or table-expansion /2 pipeline domain, rejects mixed/empty
domains and retains older profile refusals. Shared backend discovery consumes
that rule for both loss registration and immutable pipeline validation, instead
of treating all Closure7 programs as tables. Source312/312 passes9.318s;
MinGW/Wasm/native revalidation pending. Freeze5/Payload5/composed execution,
public Emulated, Metal public Graphics and renderer/WOCC remain incomplete.

Geometry/readback registration correction (2026-10-10, validation pending):
Host6/6 and source312/312 pass; Wasm builds. Selected Chrome validates existing
stream execution/retirement then refuses new ordinary-descriptor custody because
it requested table-only Registry8. Failure plus96 selected Wasm inputs is archived
with104 verified hashes at geometry-readback-custody-registration-profile-failure/;
not complete transitive provenance. L2 lossJoinVersion now chooses existing1 for
ordinary descriptors,2 for tables, rejects mixed/empty and preserves Registry7/8
isolation. Repair builds/native validation pending. No new custody/execution claim.

Geometry/readback cold custody implementation (2026-10-10, in validation):
WebGPU Join3/Closure7 now have explicit private Prototype preparation using the
original readback/native closure path. Exact profiles, original owners, declared
CopySrc/content intent and initialized geometry remain mandatory. Legacy record/
map consumers reject; all composed frozen recording/submission/completion and
public grants remain closed. Source312/312 passes; builds/native smoke pending.
The earlier enabled startup rerun passes with complete transport and no errors;
startup-diagnosis/ holds98 frozen compiled inputs/104 verified hashes, not complete
transitive provenance. First blank timeout cause remains unresolved.

Geometry/readback L2 expanded validation checkpoint (2026-10-10):
Rebuilt assertion-enabled Host/Null/package/root6/6 and source311/311 pass.
Selected Chrome validates the contract alongside existing stream execution and
retirement, without numerical rendered output or public authorization. Full
ordinary regression is terminal1/2: enabled profile times out360.89s with no
progress; disabled-indirect-first-instance passes297.14s with complete transport
and empty errors. Both outcomes and98 selected unchanged inputs are archived at
geometry-readback-composition-l2-first-timeout/ with112 verified hashes, not
complete transitive provenance. Startup location diagnostics and rerun are pending;
all composed native gates and public WebGPU authorization remain closed.

OriginalGeometryReadbackComposition/1 L2 increment (2026-10-10):
The explicit Join3/Closure7/Freeze5/Payload5/Cold5 composition now defines the
same-root State4/Inputs5/Stream4 geometry and ContentPlan3/Result2/Map1 chain.
Finite work, exact legacy refusal and mandatory completion/map custody are
implemented; four-backend composed native gates remain closed pending payloads.
See [composition contract](original-geometry-readback-composition-contract.md).
Expanded Host/Null/browser validation is pending; no composed native output,
public Emulated authorization, Metal Graphics or WOCC completion is claimed.

Original immutable vertex/instance Execution4 evidence (2026-10-10):
Selected Chrome1/1 and both full configurations2/2 pass (586.89s full suite).
The new14-command private program uses actual initialized vertex/instance/index
buffers, original frozen bindings and two-instance DrawIndexed/firstInstance1.
Same-root recording/submission/scopes3-3/queue1 completion, journal commit,
caller-drop retention and final17-quota release are validated, alongside the old
20-command and ordinary device-loss regression. Errors are empty and progress
transport complete; Host/Null/package/root6/6, source310/310, MinGW/Wasm and Node
syntax pass. Evidence: stream-geometry-execution4-validated/;91 unchanged selected
inputs/108 verified hashes, not complete transitive provenance. This remains a
private scoped Backend Validated subset: numerical geometry output, same-root
geometry/table/readback, geometry loss and complete generic coverage are not proven.
Public WebGPU stays DiagnosticFailure/executablePayload=false. Metal Graphics,
renderer/WOCC and all four full objectives remain incomplete.

OriginalInitializedGeometryPendingRead/1 validated subset (2026-10-10):
The diagnostic-only Chrome run failed1/1 in250.48 seconds with
RHI-CANONICAL-UPLOAD-SOURCE-STATE-STALE, pending0 and terminal root3. It reported
no native scope error or Asyncify overflow. The failed68-selected-input checkpoint
and report/log are archived at geometry-execution4-state-stale-diagnosis/ with72
verified hashes; this is not a complete transitive inventory.
Initialized actors retain their genuine initialization completion and committed
state capture. Public ready/content validation must continue refusing during
Pending. Only the existing original Recording seal may authenticate immutable
geometry bytes against its exact root journal: same original lease, registration,
resource/control/generation and captured revision; State4/Inputs5 plus opted-in
GeometryDrawConsumption1; captured, initial, final and current Pending state all
ShaderRead; pending snapshot must match that sole journal. Storage/transfer writes,
wrong root/seal, absent journal, expired capture and state/revision mismatch reject.
Reuse the existing initialized CopySrc pending validator via a typed use selector;
its CopySrc behavior and diagnostics stay strict. No recapture, cached boolean,
new owner graph, native handler, budget increase or public authorization is allowed.
Four-backend lowering is versioned: Null validator-only; Vulkan/Metal original
payload missing; WebGPU private Prototype pending geometry read validation only.
L2 tests and shared pending validator must precede backend readiness integration.
Implementation now routes private Closure6 geometry reads through the retained
original seal, after genuine initialized-source completion facts. The shared pending
validator reads its actual State4 budget and requires Inputs5/GeometryDrawConsumption1.
The same browser fixture now asserts public ready/index-content APIs refuse while
Pending before native scopes drain. Source310/310 passes10.058 seconds; MinGW/Wasm
builds and assertion-enabled Host4/4 pass3.13 seconds. Both Chrome profiles then
fail0/2 in506.86 seconds after authentic recording/submission/queue completion,
root commit and caller command/issuer release: the fixture prematurely requires
all submission bytes to be zero while the caller admission retains its original
submission-charged lifecycle reservation. Both failed reports and68 unchanged
selected inputs are archived in geometry-execution4-pending-read-reservation-baseline-failure/
with78 verified hashes; this is not a complete transitive inventory.
The fixture now requires command/callback cleanup to restore the exact original
admission baseline; callback baseline is zero and reservation charge is nonzero.
The existing outer wrapper must still release admission before requiring every
quota and all original reservation counts to reach zero. No ownership, budget,
completion or capability gate changes. The rebuilt baseline repair passes
Source310/310, MinGW/Wasm and assertion-enabled Host4/4 (2.98 seconds), then
Chrome0/2 fails514.11 seconds with submission160/baseline132 and callback0/baseline0.
Both reports and68 unchanged selected inputs are archived at
geometry-execution4-weak-observer-control-block-failure/ with78 verified hashes.
The original issuer wrapper is allocated under submission quota; fixture weak
observers retain its shared control block after object expiration. The fixture
now verifies expired command/issuer observers, releases those observers, then
checks the exact retained baseline. The outer closure observer likewise verifies
expiration and resets before final all-quota-zero checks. No allocator accounting
is ignored and no production lifetime semantics change. The final rebuilt
assertion-enabled Host4/4 passes3.53 seconds; Source310/310 passes9.218 seconds;
MinGW/Wasm builds, package/root8, Node syntax and diff checks pass. Both real
Chrome profiles pass2/2 in567.56 seconds (277.57 enabled /289.97 disabled).
Both success and originalGeometryNativeExecutionPassed are true with empty
diagnostics/nativeErrors; publicGenericExecutionStatus remains DiagnosticFailure
and publicBindlessExecutionAvailable remains false. The same20-command program
records indexBind2/indexedDraw3, authentic native scopes3-3 and queue1 completion,
original Pending-journal commit, caller command/issuer drop retention, exact
admission quota baseline and final all-quota/reservation-count release. Public
source ready/content APIs still refuse Pending; frozen binding mutation and legacy
profiles still reject, and hot recording/submission add allocation0/query0.
Validated evidence is archived at geometry-execution4-pending-read-validated/;
68 selected compiled inputs match the frozen checkpoint;86 archive hashes are
verified. This is not a complete transitive inventory. Contract Complete / Prototype Complete and scoped Backend
Validated apply only to this private immutable geometry/completion subset. The
fixture still uses procedural vertices: actual accepted vertex/instance fetch,
useful numerical rendered readback, combined geometry/table/readback profile and
geometry-specific loss/cancellation/retirement remain required before complete
generic execution and any public Emulated authorization. Failed suites above
remain failures and are not included in the final passing result.
Public Emulated, Metal Graphics, renderer/WOCC and all four full objectives remain incomplete.

OriginalTypedGeometryExecution/4 recording observation (2026-10-10):
After synchronous preparation returns, the rebuilt Chrome suite finishes0/2 in
512.48 seconds. Neither profile reports the prior Asyncify continuation overflow;
both pass Cold4 admission and observe three held native scope promises, then fail
the unchanged five-second hasOriginalRecordedCommand wait. This proves movement
past the earlier continuation failure, not successful native recording/submission.
Both reports and68 unchanged selected inputs are archived at
build/web-webgpu-debug/artifacts/bindless-continuation/geometry-execution4-recording-observation-failure/
with73 verified hashes; this is not a complete transitive input inventory.
The exact recording diagnostic was hidden by the wait assertion. The fixture now
prints that diagnostic, pending-record count and original root status on failure;
its existing held error scopes optionally print genuine native validation errors
for geometry only. This does not install a new native handler, alter delivery or
acceptance, enlarge budgets/timeouts, or change production capability/lowering.
Source309/309 passes9.072 seconds; rebuilt MinGW/Wasm pass; assertion-enabled
Host4/4 passes3.20 seconds; generated JS syntax and diff checks pass. The main
Chrome diagnosis is running against68 frozen selected inputs (75 checkpoint
hashes); actual recording/submission/completion is still unproved.
Public Emulated, accepted vertex-instance fetch/output readback, geometry-table
combination, loss/retirement, Metal Graphics and renderer/WOCC P4-P6 remain open.
All four full objectives remain incomplete.

OriginalTypedGeometryExecution/4 continuation repair (2026-10-10):
The first real Chrome Execution4 suite is terminal and failed0/2 in504.61 seconds.
Both profiles passed original Cold4 admission, then reported
ASYNCIFY_CONTINUATION_BYTES18892/CAPACITY16384 before the geometry execution marker.
Native errors were empty; this does not prove native recording or completion.
The unchanged65 selected compiled inputs and both reports/log are preserved in
build/web-webgpu-debug/artifacts/bindless-continuation/geometry-execution4-continuation-failure/
with70 verified evidence hashes; this is not a complete transitive inventory.
The fixture now returns its same immutable admission and original quota observers
from a synchronous preparation helper before entering callback waits. Authored
command/argument/descriptor arrays and negative-probe temporaries leave the call
stack first. Original ownership, work, callbacks, production/continuation budgets,
timeouts, execution profiles and four-backend lowering remain unchanged. The
execution wrapper still requires actual committed completion, expired original
closure ownership and all four preparation quotas plus submission/callback quotas
and reservations to drain. Source309/309 passes9.530 seconds; rebuilt MinGW/Wasm
pass; assertion-enabled Host4/4 passes3.12 seconds; package/root8, runner Node
syntax and diff checks pass. Both Chrome profiles are running against68 frozen
selected compiled inputs (78 checkpoint hashes); actual browser validation is
pending. Public Emulated stays DiagnosticFailure/executablePayload=false.
Real vertex-instance fetch, useful output readback, geometry/table combination,
native loss/retirement, Metal Graphics and renderer/WOCC P4-P6 remain incomplete.
All four full objectives remain open.

OriginalTypedGeometryExecution/4 implementation (2026-10-10):
L2 Command4, Callback4, Issuer4 and Execution4 pair exactly with Cold4. Legacy
profiles and mixed geometry/transfer/readback intent reject; Callback4 requires
complete geometry counts and reuses the existing seven native scope/queue slots
plus one local loss observer. Four-backend lowering keeps Null validator-only,
Vulkan/Metal payload-missing and WebGPU explicitly private Prototype. Public
BindlessCross remains DiagnosticFailure/executablePayload=false.
The existing native emitter now consumes the immutable original buffer ranges
for vertex/index binding and all five indexed draw fields. Physical vertex slots
are frozen from the exact accepted pipeline layout order, matching native pipeline
creation; logical slot numbers are not assumed to be native vector positions.
No alternate command graph, native resource query, new submission path or hot
allocation is added. The shared fixture declares the17 extra command/callback/
issuer ownership nodes before original Sources publication and prepares the
existing observed-loss receiver explicitly before registry creation.
The original20-command positive now records two index binds and three indexed
draws through the same seal, root, journal, prepared callbacks, native submission
and genuine queue completion. Held scopes require Pending until actual drain;
legacy profiles and changed frozen buffer range refuse. Dropping caller command
and issuer wrappers must preserve callback-owned work until completion. The
custody wrapper remains held for successful completion; dropping it is the
existing explicit cancellation policy, not a successful-drop claim.
Source309/309 passes9.810 seconds; MinGW/Wasm builds and assertion-enabled
Host4/4 pass3.27 seconds. Package/root8, Node syntax and diff checks pass. Both
real Chrome profiles are running against a frozen65-selected-input checkpoint;
actual browser validation is pending. This fixture still uses procedural vertices; actual vertex/instance
fetch, geometry/table combination, output readback, geometry-specific native-loss
and full WOCC acceptance remain unproved. No Backend/Platform readiness claim is
made for this unvalidated implementation. All four full objectives remain open.


OriginalTypedNativeCold/4 repaired validation (2026-10-10):
Both real Chrome profiles now pass2/2 in560.06 seconds (280.20/279.84), after
correcting authored pipeline-before-setter order and eliminating the native
Source object9-to7 overwrite. Both success and originalGeometryColdAdmissionPassed
are true; diagnostics/nativeErrors are empty and public execution remains
DiagnosticFailure/publicBindlessExecutionAvailable=false. Assertion-enabled
rebuilt MinGW Host4/4 passes4.06 seconds; Source308/308 passes9.183 seconds;
MinGW/Wasm builds, package/root8, Node runner syntax and diff checks pass.
The same fixture command arrays and final Source budget are tested in Host and
native contexts, including rejection of the former setter order and resource-only7
or one-less8 native-object authority. Both profiles consumed hash-identical57
selected compiled inputs. Evidence is archived at geometry-cold4-repair-validated/
with70 verified hashes; this is not a complete transitive input inventory.
Contract Complete / Prototype Complete applies only to Cold4 admission over
Payload4/Freeze4/Closure6/Lifecycle1, including genuine seal/reservation, quota
pressure, forged/cross-profile rejection, caller-drop retention and release.
No native geometry command shell, recording, submission, actual vertex/instance
fetch or completed geometry readback is proven. Next join the existing typed
command/callback/issuer chain, native geometry emission and genuine completion
using the same root. Public Emulated, Metal Graphics and WOCC P4-P6 remain open.


OriginalTypedNativeCold/4 failed browser and repair (2026-10-10):
Both Chrome profiles finished with failure (0/2,460.10 seconds), before Cold4
admission at Usage::prepareOriginal in the complete20-command fixture. The
setters preceded BindGraphicsPipeline, violating the unchanged L2
RHI-COMMAND-ORDER-PIPELINE requirement. Reports/logs and the selected56-input
checkpoint are preserved at geometry-cold4-browser-usage-failure/; the selected
inventory is not a full transitive input inventory. Separately, the authored
native-object ceiling9 was overwritten by7 before Sources publication.
The repaired fixture shares its command arrays and final Source budget between
Host and native execution. Pipeline now precedes viewport/scissor in each pass;
references and the original16-to20 ordinal map match that sequence. Host tests
also reject the previous order and the resource-only7 / one-less8 native-object
budgets. No production budget, L2 order rule or execution gate changed.
Repaired source308/308 passes9.183 seconds; MinGW/Wasm builds pass,
assertion-enabled Host4/4 passes4.06 seconds, package/root8 and runner Node
syntax pass. Both Chrome profiles are running against a frozen57-selected-input
checkpoint; actual browser verification is pending. Public
Emulated remains DiagnosticFailure/executablePayload=false; native geometry
recording/submission/completion, Metal Graphics and WOCC P4-P6 remain open.


OriginalTypedNativeCold/4 implementation (2026-10-10):
Cold4 consumes only Payload4 geometry and Lifecycle1 ownership. Legacy Cold1/2/3
retain geometry refusal; mixed readback/schema and cross-version profiles reject.
The shared geometry-count inspector is reused by Payload and Cold validation.
Four-backend lowering keeps Null validator-only, Vulkan/Metal missing and WebGPU
cold admission only; recording/execution remain false. A separately authored
20-command program over the same original Sources adds explicit viewport/scissor
per pass while preserving the original16-command INCOMPLETE negative. Its finite
Source request declares20 commands,19 native calls, seven resource payloads plus
encoder/command buffer, eight existing callback participants and Cold admission
owners before Sources publication. Production limits are unchanged. New tests
require exact-one-less work/owner/callback capacities, genuine seal/reservation,
forged and short-demand rejection, measured quota pressure, caller-drop and final
quota release. Source308/308 passes8.894 seconds. Rebuilt Host/Wasm and actual
Chrome evidence are pending. Native commands, recording/submission/completion,
real accepted vertex/instance fetch and geometry-table combination remain open;
public Emulated, Metal Graphics and WOCC P4-P6 are incomplete. All four full
objectives remain active.

OriginalTypedNativePayload/4 validation (2026-10-10):
Rebuilt assertion-enabled MinGW Host4/4 passes3.57 seconds; source307/307 passes
8.332 seconds. MinGW Vulkan/Metal/WebGPU libraries and Wasm builds, package/root8,
runner Node syntax and diff checks pass. The first BufferDesc field spelling
failure and Wasm dependent-template spelling failure are preserved in build logs.
Both actual Chrome profiles pass2/2 in513.11 seconds (252.46 /260.64). Both reports
independently require originalGeometryNativePayloadPassed and have success=true,
Freeze4/Closure6 custody true, diagnostics/nativeErrors empty and public execution
DiagnosticFailure/publicBindlessExecutionAvailable=false. The selected53 compiled
inputs remained hash-identical throughout the browser suites; reports and logs
are archived under geometry-payload4-validated/ with70 verified hashes. This is
not a complete transitive inventory. The genuine original16-command fixture
freezes2 native index binding records,3 indexed draws,15 native calls and exact
per-pass draw ceilings. Native handles/binding sizes/borrowed original records,
exact-one-less/exhausted quota tests, mutation rejection, caller-drop and hot0/query0
pass. Contract Complete / Prototype Complete applies to private immutable native
input preparation only; no native geometry recording, submission or execution
claim. The selected pipeline still has procedural vertices. Legacy Cold1/2/3
reject the geometry payload; the barrier-authored16-command program also lacks
explicit per-pass viewport/scissor for native admission. Next author a complete
finite native workload before original Sources publication, reuse real accepted
Slang vertex/instance inputs, and join existing cold admission/root/seal/recording/
submission/completion/readback/loss-retirement. Closure6 geometry and Closure3/4
table expansion cannot be silently combined; their approved combined profile
remains required for WOCC. Public Emulated, Metal Graphics, renderer/WOCC P4-P6
and full named-browser/visual/Release/platform acceptance remain incomplete.

OriginalTypedNativePayload/4 implementation (2026-10-09):
Payload4 pairs exactly Freeze4/Closure6 and borrows the existing original
geometry binding, initialized actor, native buffer and pipeline entries. Work
counts independently bound vertex bindings, index bindings and indexed draws;
shared overflow-safe arithmetic and work comparison include all three. Legacy
Payload1/2/3 and Cold1/2/3 reject geometry demand. Four-backend lowering keeps
Null validator-only, Vulkan/Metal missing and WebGPU immutable input preparation
only; recording/execution remain false. No native API, new owner graph, command,
callback or completion authority is introduced by preparation. The same original
16-command fixture now freezes two index bindings and three indexed draws with
15 native calls, explicit bound-tail sizes and exact pass draw ceilings. A finite
Payload owner is declared before original Sources publication. Tests require
exact/one-less work-owner-metadata budgets, mutation rejection, caller-drop and
hot allocation0/query0; the runner independently requires the geometry payload
marker. Source307/307 passes8.832 seconds. Rebuilt Host/Wasm and Chrome evidence
are pending. This adds immutable native inputs, not native geometry execution;
real accepted vertex/instance fetch, cold admission/recording/submission/real
completion-readback-loss-retirement, public Emulated, Metal Graphics and WOCC
P4-P6 remain open. All four full objectives remain incomplete.

GeometryDependencyFreeze/4 validation (2026-10-09):
The explicit pre-publication native owner inventory repair passes rebuilt
assertion-enabled MinGW Host4/4 in3.11 seconds and both Chrome feature profiles
2/2 in502.89 seconds (246.76 /256.12). Source306/306 passes10.195 seconds;
MinGW backend libraries, Wasm, package/root8, runner Node syntax and diff checks pass.
Both reports require originalGeometryDependencyFreezePassed independently and
have success=true, Closure6 custody true, diagnostics/nativeErrors empty,
publicGenericExecutionStatus DiagnosticFailure and publicBindlessExecutionAvailable=false.
The same original Sources feeds16 authored commands,3 indexed draws,2 explicit
initial/inter-pass clauses and16 complete index/Fragment-read endpoints.
The original graph-only owner budget and one-less owner/endpoint/metadata quotas
reject; measured cold allocation, caller-drop retention/final quota release and
hot allocation0/query0 pass. Old barrier-free17-command EMPTY and Closure5 gates
remain mandatory. The selected51 compiled inputs, both reports and logs are
archived under geometry-freeze4-validated/ with62 verified hashes; not a complete
transitive inventory. Contract Complete / Prototype Complete applies to this
private CPU dependency custody only; native geometry execution is not proved.
Next required work is Payload4 geometry vocabulary and finite native work/owner
admission, then the same-root recording/submission/real completion/readback and
loss/retirement joins. Reuse existing accepted Slang vertex-stream fixtures for
actual vertex/instance fetch; procedural vertices do not supply that evidence.
Public Emulated, Metal Graphics, WOCC material/instance/color/depth/shadow and
named browser GPU-driven/visual/Release/platform acceptance all remain incomplete.

Geometry Freeze4 parent admission repair (2026-10-09):
The corrected Host4/4 passes0.86 seconds, but both real Chrome profiles reject
RHI-WEBGPU-TYPED-DEPENDENCY-OWNER-CAPACITY before the endpoint-capacity probe.
The original Source parent admitted only the earlier geometry graph; it had not
requested the newly retained Freeze4/native custody inventory. Both failed reports,
Host logs and selected51-input checkpoint are archived under
geometry-freeze4-parent-owner-admission-failure/ with56 verified hashes.
The readable workload now declares finite State/Stream/Transaction, Closure/loss,
initialized actor and native entry/quota slots plus Freeze ownership before immutable
Sources publication. This extends the explicit authored workload request, not a
production limit, post-admission growth or runtime fallback. The original graph-only
owner capacity now has a no-allocation rejection; measured exact/one-less Freeze
quotas remain. Source306/306 passes10.195 seconds. Rebuilt Host/Wasm/browser
validation is pending; public execution stays closed and all four full objectives remain open.

OriginalGeometryContentCustody/1 corrected validation (2026-10-09):
The stale negative classified supported Closure6 as unavailable. Unsupported7
and missing GeometryContentCustody1 intent on6 now reject independently. The
failed50-input checkpoint, both failed reports and build/test logs are preserved
under geometry-content-custody1-version-negative-failure/ with56 verified hashes.
Rebuilt assertion-enabled MinGW Host4/4 passes4.18 seconds; Wasm compiles;
Source305/305 passes8.366 seconds, package/root8 and runner Node syntax pass.
Both fresh Chrome profiles pass2/2 in503.04 seconds (252.49 /250.54).
Both success, originalGeometryContentCustodyPassed and immutableOriginalVertexContentPassed
are true; diagnostics/nativeErrors are empty, publicGenericExecutionStatus remains
DiagnosticFailure and publicBindlessExecutionAvailable=false. The corrected selected50
compiled inputs and reports/logs are archived under geometry-content-custody1-validated/
with59 verified hashes; not a complete transitive inventory. Scoped private original
geometry-content custody is validated; the shader remains procedural-vertex and
native DrawIndexed/vertex-instance fetch, public Emulated and WOCC P4-P6 stay open.
Freeze4 edits are newer than this compiled checkpoint and require independent validation.

GeometryDependencyFreeze/4 implementation (2026-10-09):
Freeze4 explicitly pairs Projection4 with Closure6; Closure5 and all cross-version
pairs reject. Legacy Freeze1/2/3 pairs remain exact. The existing dependency object
retains original CPU clauses and the same loss-bound closure through core quota
ownership; no replacement owner graph or execution authority is introduced.
A separately authored16-command program reuses the same accepted pipeline,
descriptor and initialized-index Sources, with explicit initial/inter-pass buffer
memory barriers covering Fragment ShaderRead and VertexInput IndexRead. Three
indexed draws require complete geometry and shader endpoints. The preceding
barrier-free17-command program still refuses EMPTY. Exact/one-less quotas,
endpoint coverage, caller-drop and hot allocation0/query0 probes are required.
Null is validator-only; Vulkan/Metal retain missing original joins; WebGPU freezes
CPU candidates only. Payload1/2/3 remain unable to consume Freeze4/Closure6;
native geometry recording/submission/completion, public Emulated authorization,
Metal Graphics and WOCC P4-P6 remain incomplete. Source306/306 passes; rebuilt
Host/Wasm and required new browser marker are pending. No Backend Validated claim.

OriginalGeometryContentCustody/1 implementation (2026-10-09):
Closure6 uses the existing resource entries and initialized source actor slot for
exact original State4/Stream4 draw-content custody. Explicit shared geometry and
indexed-fetch budgets validate the same pipeline, binding ordinals, immutable
index bytes and supported initialized vertex/instance ranges. Old Closure5 retains
its missing draw-join refusal. The existing fetch walker gains an initialized
vertex-content inspector after its shared range check; no ownership graph is added.
Source305/305 and package/root8 checks pass. Initial MinGW build succeeds; initial
Wasm fixture compilation fails at an extra closing brace in the new test branch.
The fixture is corrected; rebuilt Host/Wasm/native browser evidence is pending.
Public Emulated, native recording/submission/completion, actual accepted vertex/
instance fetch, Metal public Graphics and renderer/WOCC P4-P6 remain incomplete.

ImmutableOriginalVertexContent/1 implementation (2026-10-09):
InitializedCanonicalBufferStaging profile3 reuses the same original source actor,
lease, write seals, five CPU quota domains and error/queue/loss completion owners.
Vertex and instance content require explicit little-endian stride/rate/count and
an exact whole-buffer byte product; mixed index intent rejects. The fetch validator
joins the original initialized source/lease and matching stream shape, then reuses
VertexInputConsumptionContract for bounded range inspection. This is content/range
compatibility only; accepted pipeline custody and native draw authority stay separate.
Source304/304 passes8.739 seconds; assertion-enabled Host4/4 passes3.95 seconds;
MinGW Vulkan/Metal/WebGPU libraries and Wasm builds succeed. Package/root8,
runner Node syntax and diff checks pass. Both fresh Chrome feature profiles pass
2/2 in489.18 seconds (239.35 /249.82). Both success and the independently required
immutableOriginalVertexContentPassed are true, diagnostics/nativeErrors empty,
publicGenericExecutionStatus DiagnosticFailure and publicBindlessExecutionAvailable
false. Vertex/instance admission, measured exact/one-less/exhausted quotas,
frozen-byte native readback, seven lifecycle probes, pending/suppressed queue
physical-loss handling and hot0/query0 are verified using the same original actor.
The selected50 compiled inputs, both reports and source/Host/package/browser logs
are archived under vertex-content1-validated/ with56 verified hashes; this is not
a complete transitive inventory. Contract Complete / Prototype Complete and scoped
Backend Validated apply to private initialized vertex/instance content only.
Null validator-only; Vulkan/Metal original payload missing; native geometry
recording, public Emulated, Metal Graphics and WOCC P4-P6 remain incomplete.

Latest geometry entry validation and draw-access implementation (2026-10-09):
geometry entry isolation passes both Chrome feature profiles2/2 in491.74 seconds;
both fresh reports have success=true, correct-owner indexed custody/fetch/State4-
Stream4 markers true, diagnostics/nativeErrors empty and public execution false.
The preceding frozen50-input checkpoint, both reports and log are archived under
indexed-consumption1-geometry-entry-validated/ with53 verified hashes. It is a
selected compiled-input snapshot, not a complete transitive inventory. Host4/4
passes4.62 seconds; source303/303 passes8.255 seconds; Host/Wasm builds succeeded.
This closes that fixture-frame regression only, not public generic execution.

GeometryDrawConsumption/1 is CPU Contract Complete / Prototype Complete as an independent intent in
the existing geometry budget. Inputs5 projects conservative bound-tail reads at
actual Draw/DrawIndexed positions and retains the original binding ordinal;
shared latest-binding lookup has an independent total scan ceiling. State4
validates those reads at draw time; Sequence4 counts additional implicit steps;
Stream4 merges geometry and shader ranges at the same work ordinal. Usage4 /
StateBound4 / Dependency4 distinguish GeometryConsumption and require complete
count correspondence. The original transaction plan populates the same captured
cells/journal without another ownership graph. Old Closure5 explicitly refuses
GEOMETRY-CONSUMPTION-JOIN-MISSING. New synchronous genuine-input probes cover
three draw reads, scan/draw/access/pair one-less refusal, plan14 / implicit12 /
usage14, no-output refusal, hot0/query0 and old native closure rejection.
These edits are newer than the passing browser checkpoint. Source303/303 passes
12.847 seconds; rebuilt assertion-enabled Host4/4 passes4.50 seconds. C++17
MinGW backend libraries and Wasm builds succeed; package/root8, runner JavaScript
syntax and diff checks pass. The initial unsupported C++20 requires spelling
failed both builds and was replaced with C++17 type dispatch; failed logs remain.
The selected50-input checkpoint is frozen under geometry-draw-consumption1-before-browser-validation/.
Both new Chrome profiles pass2/2 in487.45 seconds (237.83 /249.61); both success
and the independently required GEOMETRY_DRAW_CONSUMPTION_OK marker are true,
with diagnostics/nativeErrors empty and public execution false. The selected50
compiled inputs, both reports and log are archived under geometry-draw-consumption1-validated/
with53 verified hashes. This proves selected CPU draw-position projection on
genuine browser inputs; it grants no native draw or generic submission authority. The selected shader
has procedural vertices; real vertex/instance content and index fetch validation
remain independent requirements, not proved by conservative hazard spans.

Four-backend decisions: Null remains validator-only; Vulkan and Metal retain
their missing original generic joins; WebGPU receives only private CPU projection,
with old native closure and public Emulated authorization closed. Metal public
Graphics, renderer/WOCC P4-P6 and all four full objectives remain incomplete.

Latest geometry fixture entry isolation (2026-10-09): the preceding50-input
Chrome regression is terminal0/2 in431.12 seconds. Both fresh reports refuse at
Asyncify continuation16908 >16384; correct-owner indexed custody was not reached.
The selected50-input checkpoint, both reports and log are archived under
indexed-consumption1-fourteenth-browser-isolated-frame-failure/ with53 verified
hashes. Descriptor-helper extraction alone did not solve continuation pressure.
The unchanged original geometry context now runs from verifyGeometryActual,
called directly by the same browser harness outside the unrelated factory
negative-case frame. Both readable and barrier-required cases, original owners,
registry inventory, actual native waits and retirement remain mandatory. No
production budget, Asyncify ceiling, deadline, validator or capability changed.
Source303/303 passes8.255 seconds; rebuilt Host and Wasm checks are pending.
Public WebGPU Emulated remains DiagnosticFailure/executablePayload=false;
draw-position geometry consumption, generic native recording/submission/completion,
Metal public Graphics, renderer/WOCC P4-P6 and all four full objectives remain open.

Latest shared geometry lookup and descriptor-frame isolation (2026-10-09):
the shared-arena Chrome regression is terminal0/2 in425.55 seconds at the
existing Asyncify continuation ceiling16648 >16384. It did not validate correct
pipeline custody. Its selected49-input compiled checkpoint, both reports and
log are archived under indexed-consumption1-thirteenth-browser-descriptor-frame-failure/
with52 verified hashes. The registration repair retains one arena/two sets,
exact named parent inventory and extra-arena/set no-allocation negatives.
Descriptor factory/write/freeze locals are now in prepareIndexedDescriptors;
its fixed returned fields are the same original arena/Source2/quota owners.
The original geometry function keeps the same root, initialized actor and
journal. Asyncify capacity, production budgets and capability gates are unchanged.

The required geometry draw-access prerequisite now extracts the existing
bounded latest-binding scan into OriginalCommandInputs::latestGeometryBinding.
It reuses core::BoundedSequence::findLastIf with an early capacity stop and
returns a borrowed original record only. Indexed fetch consumes the same lookup,
retains its shared total scan count and existing SCAN-CAPACITY diagnostic.
Genuine Inputs5 fixture probes cover original record identity, exact/one-less,
latest pass/pipeline window, absent vertex slot, invalid kind/ordinal and hot0/query0.
This is lookup reuse, not draw-position state/hazard projection or native authority.
Null/Vulkan/Metal/WebGPU existing lowering decisions remain unchanged. The
preceding lookup-only Host regressions passed4/4 in3.83 seconds; direct lookup
browser probes are not covered by the preceding49-input browser run.
Source303/303 passes8.770 seconds. Current isolated-helper Host4/4 passes3.47 seconds; MinGW backend libraries
and Wasm builds succeed; package/root8 and diff checks pass. The selected50-input
compiled checkpoint is frozen under
indexed-consumption1-lookup-isolated-descriptors-before-browser-validation/;
it is not a complete transitive inventory. Both Chrome profiles are running;
lookup and descriptor-frame browser behavior remain unvalidated. Public Emulated remains
DiagnosticFailure/executablePayload=false. Geometry consumption at draw ordinals,
native recording/submission/completion, Metal Graphics, renderer/WOCC migration
and full browser/visual/Release/lifecycle acceptance remain open. All four full
objectives are incomplete.

Latest exact descriptor registration inventory repair (2026-10-09): the
pipeline-owner Chrome regression is terminal0/2 in439.12 seconds. The fixture
requested a second arena before its parent Context's one-arena/one-set registry
could admit it; no correct-owner custody success was reached. The selected49-input
compiled checkpoint, both failed reports and log are archived under
indexed-consumption1-twelfth-browser-registration-inventory-failure/ with52
verified hashes. Both original pipelines now use two genuine descriptors from
one shared arena. The same public indexedDescriptorCount2 feeds the parent
registration request, arena set/element capacity and retained fixture arrays.
The parent retains its one-arena ceiling. A second arena and third set must
refuse REGISTRATION-KIND-CAPACITY before allocation, with unchanged quotas and
arena allocation counters. Stable registration diagnostics accompany any
unexpected factory refusal. Pipeline owner mismatch remains a separate negative;
three pipeline epochs/draws and the same original initialized index lease,
State4/Stream4/journal are preserved. This corrects the finite authored fixture
inventory only; production registry budgets and validators are unchanged.
Source303/303 passes8.025 seconds; rebuilt assertion-enabled Host4/4 passes
3.88 seconds; MinGW backend libraries and Wasm rebuilds succeed. Chrome0/2 failed
at Asyncify16648 >16384 before correct-owner custody; see latest isolation above.
Public WebGPU Emulated remains DiagnosticFailure/executablePayload=false;
Metal Graphics, renderer/WOCC migration and full browser/visual/Release/lifecycle
acceptance remain open. All four full objectives remain incomplete.

Latest original pipeline/descriptor custody repair (2026-10-09): both Chrome
feature profiles are terminal0/2 in424.63 seconds. State4 plan materialization
now succeeds; the next Closure5 check refuses
RHI-WEBGPU-CANONICAL-CLOSURE-DESCRIPTOR-PIPELINE. The joined fixture used the
first original pipeline's committed WholeWrite1 descriptor at the second
pipeline epoch. Native closure correctly requires the exact original pipeline
owner, even when their layouts match. The selected47-input compiled checkpoint,
both reports and run log are archived under
indexed-consumption1-eleventh-browser-descriptor-pipeline-failure/ with50
verified hashes. The fixture now commits two genuine Source2 descriptors on
the same initialized index lease, one per original pipeline, and joins source5
or6 at the matching bind. The old cross-owner binding is an explicit
no-allocation closure negative; pipeline switching, three draws,17 commands,
11 records/references,9 implicit accesses and11 plan steps are preserved.
The finite authored source inventory reserves Pipeline2-Descriptor2, with
one-less assembly refusal; per-source native/owner ceilings cover that actual
inventory. Production validators, budgets, shaders and public capability gates
are unchanged. Source303/303 passes8.599 seconds; rebuilt assertion-enabled Host4/4 passes
4.31 seconds; MinGW backend libraries and Wasm builds succeed; package/root8
and diff checks pass. The selected49-input compiled checkpoint is frozen under
indexed-consumption1-pipeline-owner-before-browser-validation/; it is not a
complete transitive inventory. Both Chrome profiles failed at the parent
registration inventory gate; correct-owner custody remains unvalidated.
Null remains validator-only; Vulkan/Metal original joins remain unavailable;
WebGPU remains private Prototype and public DiagnosticFailure/executablePayload=false.
Metal Graphics, renderer/WOCC migration and full browser/visual/Release/lifecycle
acceptance remain open; all four full objectives are incomplete.

Latest State4 render-binding plan correction (2026-10-09): both Chrome
profiles failed0/2 in448.93 seconds at materializeOriginalPrototypePlan.
The previous exact47-input checkpoint and both failed reports/log are
archived under indexed-consumption1-tenth-browser-state4-plan-failure/
with50 verified hashes. Sequence4 already counts three render-binding
buffer accesses, but populatePlan emitted them only for State3, leaving
the State4 joined plan short of its eleven steps. The production correction
consumes the same render footprint for explicit State3/4 versions through
the existing addImplicitBuffer, cells, journal and quota. It adds no owner
graph or native authority. The fixture now names materialization diagnostics,
proves one-less step refusal before allocation and plan publication, and
checks authored five/eleven-step correspondence. The new source regression
failed before the fix (302/303) and passes after it (303/303,9.025 seconds).
Null remains validator-only, Vulkan/Metal preserve their missing original
joins, and WebGPU remains private CPU Prototype. Host tests4/4 pass4.60
seconds; Vulkan/Metal/WebGPU libraries and Wasm rebuilds succeed. Package/root
eight checks and diff checks pass. The selected47-input compiled checkpoint
is frozen under indexed-consumption1-state4-plan-before-browser-validation/.
Both Chrome feature profiles are running; browser behavior remains unvalidated. Public
Emulated, native indexed geometry, Metal Graphics, renderer/WOCC and all four
objectives remain open.

Latest same-input custody repair (2026-10-09): the geometry-capacity
Chrome regression is terminal0/2 in445.68 seconds. Both profiles passed
indexed shader consumption, State4/Stream4 and initialized-index fetch
checks, then correctly refused DESCRIPTOR-UNBOUND in Closure5. The old
binding-only sequence shares the original Sources containing the genuine
descriptor but does not bind it. Both failed reports and selected47-input
compiled checkpoint are archived under
indexed-consumption1-ninth-browser-unbound-custody-failure/ with50 verified
hashes. That same-root unbound sequence is now an explicit no-allocation
negative. Positive index custody consumes the exact joined17-command
Inputs5/State4/Stream4 with three genuine descriptor binds and indexed draws.
Caller-drop validation precedes custody; the same initialized source then
remains closure-owned until release. No replacement graph, production
validator, budget, shader or gate changed. Source302/302 passes9.054 seconds;
Host/Wasm builds succeed; rebuilt assertion-enabled MinGW4/4 passes4.43
seconds. The selected47-input compiled checkpoint is frozen under
indexed-consumption1-joined-custody-before-browser-validation/. Both Chrome
profiles failed0/2 in448.93 seconds at plan materialization on the joined
sequence. The binding-only unbound-custody negative passed. The full same-input
custody repair remains unvalidated on browser. Native indexed
geometry, public Emulated, Metal Graphics, renderer/WOCC and all four
objectives remain open.

Latest original geometry capacity repair (2026-10-09): the isolated-memory
Chrome regression is terminal0/2 in433.47 seconds. The fixture parent
maxInputDescriptors was extended from8 to11 for the genuine descriptor/draw
join, but its old maxIndexBindings9 negative is now valid. The unchanged
validator correctly admitted it. Both failed reports and the selected47-input
compiled checkpoint are archived under
indexed-consumption1-eighth-browser-stale-geometry-capacity-failure/ with50
verified hashes. The fixture now consumes the same original parent capacity,
proves exact-capacity acceptance and quota restoration for index and vertex
ceilings, and one-over refusal before allocation. Unexpected negative
acceptance names its expected diagnostic. No production validator, capacity,
shader or capability gate changed. Source302/302 passes8.921 seconds;
Host/Wasm builds succeed; rebuilt assertion-enabled MinGW4/4 passes7.15
seconds. The selected47-input compiled checkpoint is frozen under
indexed-consumption1-geometry-capacity-before-browser-validation/. Both
Chrome profiles failed0/2 in445.68 seconds at the later unbound-custody
gate. Both reports have indexed shader-consumption, State4/Stream4 and
fetch markers true, nativeErrors empty and public execution false. The
full joined increment remains unvalidated. Public Emulated,
Metal Graphics, renderer/WOCC and all four objectives remain open.

Latest isolated target-memory preparation (2026-10-09): both Chrome profiles
failed0/2 in72.72 seconds at the existing Asyncify continuation limit:
16428 bytes exceeded16384. They did not validate the new graphics join. The
added per-stage request/graph locals were in the large asynchronous harness
frame. Their startup-only synchronous preparation is now in the reusable
OriginalTargetMemoryFixture::loadModuleWithTargetMemory helper. It preserves
absent-request cleared-output refusal, existing graph/pair budgets, the same
sealed artifacts and complete type/stage/extent facts. Host module tests also
compare isolated-load identity with the existing complete target-memory load
and prove quota restoration while the previous artifact remains retained.
No Asyncify capacity, memory limit, timeout, production budget or gate is
changed. The failed47-input checkpoint, reports and log are archived under
indexed-consumption1-seventh-browser-asyncify-frame-failure/ with51 verified
hashes. Source302/302 passes8.340 seconds; the isolated-helper Host and Wasm builds
both succeeded. Fresh assertion-enabled command-retention/vertex/index/Null
tests4/4 pass3.75 seconds and target-memory tests7/7 pass5.75 seconds.
The selected47-input compiled checkpoint is frozen under
indexed-consumption1-isolated-memory-before-browser-validation/. Both
Chrome profiles failed0/2 in433.47 seconds at a stale fixture graph-capacity
negative; neither reports another Asyncify failure. They reached the
genuine initialized-index/WholeWrite preparation before the later assertion.
No complete indexed State4/Stream4 or public authorization is granted.
Public Emulated, Metal Graphics, renderer/WOCC and all four objectives remain open.

Latest target-memory request repair (2026-10-09): the logical binding repair
Chrome regression is terminal, both profiles failed (0/2,385.61 seconds) at
WholeWrite preparation with SHADER-MODULE-TARGET-MEMORY-NOT-REQUESTED. Logical
slot3 and wrong-slot refusal were accepted. The compiled child artifacts already
contain declaration metadata; the original module was loaded without the explicit
GraphicsTargetMemoryLoadRequest. The repair reuses TargetMemoryRequestFixture for
both stages, proves absent-request refusal with cleared output, releases the bare
load, and reloads the same sealed Slang artifact with the existing graph/pair
budgets and requested target-memory proof. Logical set0/binding3 must then prove
a uint32 minimum extent. Source302/302 passes8.140 seconds; Wasm and focused MinGW builds pass.
Target-memory host tests7/7 pass9.91 seconds; prior same-header quick tests4/4
passed2.94 seconds. The expanded47-input checkpoint is frozen under
indexed-consumption1-target-memory-before-browser-validation/. Chrome profiles
failed at Asyncify capacity; see isolated preparation repair above. Production loader defaults, lowering,
shader outputs, budgets and public capability gates remain unchanged. Failed
evidence is indexed-consumption1-sixth-browser-target-memory-request-failure/
with49 verified checkpoint/report/log hashes. Public Emulated, Metal Graphics,
renderer/WOCC and complete visual/Release/lifecycle acceptance remain open.

Latest logical-binding repair (2026-10-09): the three-slot assembly repair
Chrome regression failed both profiles (0/2,443.96 seconds), now at descriptor
layout assertion. Source assembly was accepted. The genuine Slang fixture
ordinary_graphics_storage_read declares logical set0/binding3, while the joined
fixture incorrectly asserted/wrote binding0. The existing layout/reflection and
backend physical lowering are authoritative. Failed reports and exact45-input
checkpoint are archived in indexed-consumption1-fifth-browser-logical-binding-failure/
with49 verified hashes. The fixture now consumes binding3 from its original
pipeline layout and checks that a wrong logical binding0 write returns
RHI-ORIGINAL-WHOLE-WRITE-BINDING without changing descriptor revision, payload or
live quota bytes. The shared commit helper reports preparation diagnostics on
assertion failure. Production rules, shader semantics, budgets and execution
gates remain unchanged. Source302/302 passes8.190 seconds; rebuilt MinGW4/4 passes;
host/Wasm builds succeed. Exact45-input checkpoint is frozen under
indexed-consumption1-logical-before-browser-validation/. Both Chrome profiles
failed at missing load request; see latest repair above. Public Emulated and all four full objectives remain open.

Latest indexed-consumption validation (2026-10-09): the readonly repair
Chrome regression is terminal, both profiles failed (0/2,442.78 seconds) at
original Sources preparation. The fixture declared two Pipeline typed owners
and one Descriptor typed owner but reserved only two assembly slots. The
existing exact assembly validator therefore returns
RHI-ORIGINAL-SOURCES-ASSEMBLY-CAPACITY before allocation. Failed evidence is
indexed-consumption1-fourth-browser-assembly-failure/ with49 verified hashes,
including the exact45-input compiled checkpoint and both reports. The fixture
now derives the three slots from its declared per-domain inventory and proves
one-less refusal with unchanged quota bytes. Production limits and gates remain
unchanged. Source302/302 passes8.335 seconds; rebuilt assertion-enabled MinGW4/4
passes3.33 seconds; package/root8 and diff checks pass; corrected host/Wasm builds
succeed. The exact45-input checkpoint is frozen under
indexed-consumption1-assembly-before-browser-validation/. Chrome failed at logical binding; see latest repair above. An initial patch matched the earlier pipeline-only
fixture; both builds were interrupted and the patch moved to the indexed context
before restarting. Interrupted builds are not validation evidence. Public WebGPU
remains DiagnosticFailure/executablePayload=false; all four objectives remain open.

OriginalIndexedDrawConsumption/1 remains in progress. The corrected-scope
Chrome regression failed both profiles (0/2,428.19 seconds) at
RHI-ORIGINAL-CONSUMPTION-DESCRIPTOR-MISSING: the geometry-only fixture
correctly lacked the shader-required set. Shader-consumption markers passed;
State4/Stream4 did not. This failure is archived with47 verified hashes,
including the exact44-file compiled checkpoint, under
indexed-consumption1-second-browser-descriptor-failure/.
The repair adds one genuine committed WholeWrite1/Source2 descriptor on the
same initialized index buffer and original Sources, plus three explicit
bindings after the three pipeline epochs. The joined Inputs5 now has17
commands,11 records/references,3 descriptor binds,3 draws,6 analyzed indices
and14 binding scans. Stream4 charges9 implicit accesses (3 geometry bindings,
3 descriptor binding footprints,3 draw shader accesses); Sequence4 charges11
steps. Missing descriptors remain an explicit no-output negative. Only the
authored fixture inventory grows; production budgets, validators and gates
are unchanged. Corrected MinGW4/4 passes2.73 seconds; Source301/301 passes8.512
seconds; package/root8 and runner Node syntax pass. Host/Wasm builds succeed.
The exact45-input compiled checkpoint is frozen under
indexed-consumption1-descriptor-before-browser-validation/. That Chrome run
failed0/2 in432.38 seconds during initialized-index preparation. The old exact
Index/Copy usage contract correctly disallowed the fixture's StorageBuffer
usage. Both failed reports and the exact45-input checkpoint are archived under
indexed-consumption1-third-browser-index-usage-failure/ with48 verified hashes.
ImmutableOriginalIndexShaderRead/1 now independently versions/opts into readonly
Storage usage in the same IndexContentIntent and owner. Default /2 usage remains
unchanged; /1 and invalid/missing opt-in reject. Native Storage mapping consumes
the same intent, and host/shader/transfer write seals remain mandatory. Four
backend decisions reuse the existing private initialization/refusal matrix;
public execution remains closed. Pure intent/usage/seal tests and a genuine
absent-opt-in no-seal negative precede the joined fixture. Rebuilt assertion-enabled
MinGW4/4 passes4.59 seconds; Source302/302 passes11.388 seconds; package/root8,
runner Node syntax and diff checks pass. Three backend libraries and Wasm build.
The exact45-input compiled checkpoint is frozen under
indexed-consumption1-readonly-before-browser-validation/. Genuine enabled/disabled
Chrome regression failed at source assembly; the corrected assembly is pending validation.
Contract: original-index-shader-read-contract.md.
The first repair compile failed on two incorrect names (ResourceUsage and
consumption namespace); these are corrected. Source301/301 passes again.
Procedural vertices do not prove vertex/instance content fetch.
Contract: D:/arp/docs/original-indexed-draw-consumption-contract.md.
Public WebGPU remains DiagnosticFailure/executablePayload=false. Draw-position
geometry hazards, Freeze/native recording/indirect/same-root completion, Metal
Graphics, renderer/WOCC migration and full named-browser/visual/Release/lifecycle
acceptance remain open. All four full objectives remain active.

OriginalIndexedDrawFetch/1 has validated selected CPU projection evidence.
It consumes genuine Inputs5, original DrawIndexed Scalars, exact pass/pipeline
epochs, latest geometry bindings and initialized-index source/lease analysis.
Independent command/draw/stream/scan/index ceilings and complete dry validation
precede caller output. Signed baseVertex/firstIndex/binding offsets/instances,
late invalid ranges, stale bindings, forged analyzer ranges and hot0/query0 pass.
The selected procedural-vertex shader has zero vertex streams; native execution
and actual vertex/instance content fetch remain unproved.
Corrected assertion-enabled MinGW4/4 passes4.23 seconds; source300/300 and
package/root8 pass; backend libraries and corrected Wasm compile. Final Chrome
enabled/disabled-first-instance2/2 passes472.10 seconds (232.28/239.82). Both
reports success=true, indexed-fetch/index-custody markers true, diagnostics and
nativeErrors empty, public execution=false. The first Chrome0/2 failure442.28
seconds remains separately archived: the tail-negative fixture correctly hit
total-analysis capacity6 before range refusal; the correction isolates range
failure without relaxing the budget or changing production code.
Final evidence is indexed-fetch1-validated/ with46 verified hashes, including
the exact40-file corrected compiled checkpoint. Subsequent consumption edits
are separate and are not covered by this baseline browser result.
Contract Complete / Prototype Complete applies to selected CPU fetch projection
only; no native or public authorization and no WOCC P4-P6 completion is granted.

OriginalIndexContentCustody/1 now has selected genuine WebGPU custody evidence.
Closure5 consumes exact State4/Stream4 with independent initialized-index opt-in,
retains the same canonical source actors in existing original buffer entries,
and checks every original index source/lease/format/registration and binding
position before allocation. Explicit source, initialized-byte and transitive-owner
ceilings preserve the original root, journal, quota and allocation projection.
No replacement graph, completion issuer, source-to-lease cycle or execution gate.
State4 transaction admission matches both render and geometry footprints.

Rebuilt assertion-enabled MinGW4/4 passes3.07 seconds; three backend libraries and
Wasm compile. Source299/299, package/root8 and diff checks pass. Final Chrome
first-instance enabled/disabled profiles2/2 pass467.42 seconds (227.69/239.72).
Both reports have geometry index-custody/state/usage markers true, success=true,
diagnostics/nativeErrors empty and public execution=false. Three original index
binding positions share the exact initialized source; caller-drop retains it,
closure destruction releases it and quotas return to zero. Exact/one-less CPU,
owner/allocation/initialized-byte budgets and duplicate/forged source refusals
pass; selected hot checks have allocation0/device-limit-query0. Closure metadata
peak1900 bytes is an observed fixture measurement, not a production limit.

The first Chrome0/2 failure438.07 seconds used Registration6 without native loss.
The fixture preserves Registration6 positional/missing-observation negatives and
uses existing Registration7 for initialized geometry. A second Chrome0/2 failure
423.82 seconds exposed numeric version ordering selecting table-expansion ABI.
Closure3/4 now explicitly consume table expansion; Closure5 uses original pipeline
ABI, with identical cold mapping/recapture/hot validation decisions. Combined
geometry/table and geometry/readback execution remain closed. Both failed binary,
report and source checkpoints are preserved; no gate, capacity or deadline relaxed.
Thirty-seven final compiled checkpoint inputs remain unchanged. Final evidence:
index-custody5-validated/. Contract Complete / Prototype Complete and scoped private
WebGPU Backend Validated apply only to this retained-index custody increment.
DrawIndexed ranges, vertex-instance fetches, Freeze4, generic native geometry,
indirect and same-root completion remain open; public Emulated remains closed.
Metal public Graphics, renderer/WOCC color-depth-shadow migration and named browser,
visual A/B, Release performance/lifecycle acceptance remain incomplete. All four
full objectives remain active. Next consume original DrawIndexed scalar/binding
state and these exact initialized bytes, then validate all vertex/instance fetches.

ImmutableOriginalIndexContent/1 has verified private WebGPU initialization evidence.
InitializedBufferStaging/2 extends the existing canonical source with independent
index-content opt-in, exact Index|CopySrc|CopyDst / CpuToGpu / ShaderRead layout,
explicit Uint16/Uint32 little-endian bytes and bounded analysis ceiling. It retains
the same frozen full payload, original lease, write seals, native queue/error-scope
receipts and physical-loss observer; no replacement graph, journal or completion.
Analysis checks genuine source/lease, exact format and actual Ready facts before
using the shared core-backed index extrema helper. Legacy staging refuses this
profile. Null remains validator-only; Vulkan/Metal refuse missing index payloads;
WebGPU grants private initialization only. The three profiles reuse cold budgets,
readback, seven lifecycle cases and two actual physical-loss cases per profile.

Rebuilt assertion-enabled MinGW4/4 passes4.02 seconds; Vulkan/Metal/WebGPU libraries
and Wasm compile. Source297/297, package/root8 and runner JS syntax pass. Genuine
Chrome enabled/disabled-first-instance profiles2/2 pass483.49 seconds (241.06/242.42).
Both reports include index16/index32 analysis/readback/loss markers, success=true,
diagnostics/nativeErrors=[] and public execution=false. Frozen bytes survive
caller mutation; exact original GPU buffer readback, signed-base/range/analysis
ceiling, forged/foreign ownership, cold exact/one-less/exhausted quotas, pending
and uncertain writes, cancel/drop/shutdown and genuine physical-loss drain pass.
Each index profile contributes16 required markers per report. Hot analysis proves
allocation0/device-limit-query0; original eight quotas release after actual drain.
Thirty-two compiled checkpoint inputs remain unchanged. Final evidence is archived
under immutable-index2-validated/. The first source guard failure was an outdated
/1-only missing-payload count; it was replaced by exact /1 and /2 diagnostics,
without changing production gates, resource budgets or deadlines.

This is Contract Complete / Prototype Complete and scoped Backend Validated for
private index initialization/readback/lifecycle only. Original geometry binding
retention, DrawIndexed/vertex-instance fetches, Freeze4, generic native recording,
indirect and same-root completion remain open; public Emulated stays closed.
Metal public Graphics, renderer material/instance services, WOCC color/depth/shadow,
named browser GPU-driven workload, visual A/B, Release performance and lifecycle
acceptance remain incomplete. All four full objectives remain active.

OriginalGeometryUsageDependency/1 has validated selected CPU evidence. Usage4 / StateBound4 /
Dependency4 consume exact State4/Stream4 and Consumption3/RenderUsage3 plus the
same independently opted-in GeometryInputAccess/1 footprint. GeometryPoint
keeps original buffer/pipeline registration generations and fixed-function
VertexInput/IndexRead or VertexAttributeRead provenance; descriptor and transfer
provenance remain separate. The original complete mapper, scope compatibility,
interval neighborhoods and captured buffer timeline are reused. No retained
access graph, native actor or ownership/journal replacement is added.

A genuine accepted-WebGPU-pipeline fixture authors a CopyDst-to-ShaderRead
transition, two index bindings in separate render passes and a between-pass
IndexRead memory barrier. Expected complete projection is four contributions;
initial/between clauses must cover three endpoint contributions. Budget and
footprint mismatches fail before caller output. Frozen binding validation is
CPU value comparison only; Freeze4 and all native geometry execution remain
closed. This increment is Contract Complete / Prototype Complete for the
selected CPU index-binding usage/dependency projection only. Final rebuilt
assertion-enabled MinGW4/4 passes2.83 seconds; Vulkan/Metal/WebGPU libraries and
Wasm compile; Source296/296 and package/root8 pass. Genuine Chrome enabled and
disabled-first-instance profiles2/2 pass467.93 seconds (229.15/238.77). Both
report usage/dependency, StateStream and refusal true; diagnostics/nativeErrors
are empty and public execution remains false. The positive fixture verifies four
contributions, three endpoints, six pair checks, nine projection passes and36
contribution visits. Capacity/footprint/cross-quota refusal emits no caller output;
hot allocation0/device-limit-query0 and caller-drop/quota release pass. Combined
Usage4 geometry/readback and actual vertex fetch execution are not yet evidenced.

Evidence is under geometry-projection4-validated/ with44 selected source, binary,
report, log and document hashes. The compiled selected25 inputs were checked
unchanged against the pre-browser checkpoint. Initial failed Chrome0/2 /384.58
seconds and ten verified failed-evidence hashes remain separate. The fixture now
builds a pass snapshot in its exact Inputs quota and explicitly tests wrong-quota
refusal; no production guard, capacity or timeout was relaxed. No native geometry,
public Emulated, Backend/Platform-wide or Production readiness is granted.

Next prove initialized immutable index content bound to the same adopted buffer
and write seals, then DrawIndexed ranges/baseVertex and vertex/instance fetches.
Native recording/indirect and same-root completion follow those proofs. Metal
public Graphics, renderer/WOCC services and migration, the named real browser
workload, visual A/B, Release performance and lifecycle acceptance remain open.
All four full objectives remain incomplete.


OriginalGeometryStateStream/1 is Contract Complete / Prototype Complete only
for selected CPU geometry state/stream integration. Consumption3 / RenderUsage3
consume exact Inputs5; State4 captures existing original buffer observations and
Sequence4 resolves bound-tail state at each binding ordinal. Stream4 consumes
the identical independently opted-in geometry budget and bounded bind/access
ordinals. Old profiles, short budgets and CopyDst geometry access refuse before
visitor output. State/stream caller-drop retention and hot allocation0/query0
checks pass on genuine WebGPU resources.

Final rebuilt assertion-enabled MinGW4/4 passes1.64 seconds; backend libraries
and Wasm compile. Source295/295 and package/root8 pass. Final Chrome enabled /
disabled-first-instance profiles pass2/2 in483.26 seconds (243.69/239.57); both
StateStream/refusal markers true, diagnostics/nativeErrors empty and public
execution false. Thirty-five selected source/binary/evidence hashes verify under
geometry-state4-validated/. Initial compile and Host3/4-browser0/2 stale-version
failures remain separate evidence; production guards/budgets/deadlines unchanged.

Next integrate geometry into the existing complete usage/dependency projector,
then prove initialized immutable index content, DrawIndexed base/ranges and
vertex/instance fetches before original native recording/indirect/completion.
No native geometry or public Emulated authorization is granted. All four full
objectives, Metal Graphics and renderer/WOCC P4-P6 remain incomplete.

GeometryInputAccess/1 continuation: L2 adds explicit VertexInput stage and
VertexAttributeRead/IndexRead access; exact buffer usage and read-state validation
reject shader-stage substitution and texture geometry access. Vulkan maps native
vertex-input masks; Null remains validator only. Metal/WebGPU barrier entries
explicitly refuse unproven geometry dependencies before mutation. Original
Inputs5 enumerates all bound tails with independent command/binding/pair ceilings
and complete dry validation before caller output; no extra ownership graph is
created. Null/Vulkan direct barrier entry points enforce exact geometry type,
usage and stage/access before mutation. Null real-entry tests prove a genuine
CopyDst-to-ShaderRead transition, cancellation rollback and invalid requests
leave resource state untouched. Pair counting reuses the core overflow-safe helper. TypedShaderUsageScope4 validates whole-pass vertex/index/readonly
compatibility and refuses writable-storage aliases even for disjoint ranges.
Final rebuilt assertion-enabled MinGW4/4 passes3.84 seconds; Vulkan-Metal-WebGPU
libraries and Wasm compile. Source294/294 and package/root8 pass. Final Chrome2/2 passes466.88 seconds
(enabled226.43/disabled240.44); both geometry position/access true, empty
diagnostics/nativeErrors and public execution false. Thirty-one checkpoint
hashes verify under geometry-access1-validated/. State/Stream, initialized content, draw
fetches and native DrawIndexed remain open; public Emulated and the full goal
remain incomplete.

## Original geometry Inputs5 and compiler refusal (2026-10-09)

WgslBufferArrayDiagnostic/2 now distinguishes successful Slang buffer-array
emission from actual E56105. Selected source declarations, authoritative SPIR-V
cardinalities and emitted array shapes must correspond before unsupported WGSL
bytes are deleted. The unavailable role has its own versioned diagnostic;
Vulkan/MSL targets remain independently sealed. MinGW compiler/artifact/fetch
CTest5/5 and matrix/retention/Null3/3 pass. This repairs the broader geometry
verification build; no native WebGPU array ABI or public Emulated gate opens.

Inputs5 extends the existing original positional descriptor sequence with
pointer-free vertex/index records, exact adopted buffer/pipeline Sources, render
scope, pipeline-bind ordinal and both registration generations. The immutable
pipeline definition supplies vertex stream shape through the existing common
validator. Old Inputs profiles refuse geometry; older State/Stream consumers
retain their exact input profiles. Native geometry, initialized index-content
and draw-range proof remain closed. Source293/293, package/root8 and rebuilt Host5/5 pass. Genuine Chrome enabled
and disabled profiles pass2/2 /497.94 seconds (251.36/246.57), geometry position
true, diagnostics/nativeErrors empty and public execution false;22 checkpoint
hashes verify under geometry-inputs5-validated/. Full shader
coverage, public WebGPU execution, Metal public Graphics and renderer/WOCC
P4-P6 remain incomplete.

## TypedReadbackMapCustody/1 (Chrome cold receipt custody validated)

Callback3 has a separate ReadbackMapIntent/1 opt-in. It accepts only Cold3's
original Result2/Map1 projection: one map and three scope participants per unique
result, plus the original eight recording/submission/loss participants. Exact
Source-authored totals, result identity and all lifecycle owner ceilings must
match before native receipt preparation. Empty intent preserves Callback1/2/3's
recording-only profile; missing opt-in, wrong profile/counts and overflow refuse
without participants or native mutation.

Four-backend lowering is Null validator-only, Vulkan/Metal missing payload,
WebGPU cold exact native map/scope receipt preparation. Mapping, completion and
publication are unavailable on every backend for this independent contract.
Both legacy and typed custody reuse the original RuntimeState map preparation
and exact buffer/request/scope validator. ResultStorage2 is retained in the same
state; cancellation detaches the real loss observer and reclaims receipts and
quota ownership. No map is issued and no GPU completion is asserted.

Status: Contract Complete / Prototype Complete for cold receipt custody only.
Final rebuilt assertion-enabled MinGW/Null6/6 passes22.91 seconds; source290/290
and package/root8 checks pass. Final Chrome regression3/3 passes655.13 seconds:
legacy comprehensive236.03, mixed recording231.12 and typed map custody187.96.
All selected Compute/Graphics reports have success=true/exit0/errors empty.
Graphics measures532 Submission bytes and960 Callback bytes; Result remains588
bytes for capacity512. Caller-drop retention, exact counts/owner shortage,
missing/forged Result, exhausted quotas, duplicate claim and cancellation release
pass. Hot core allocation0/device-limit-query0; Asyncify17660/32768,208 unwinds.
Encoder/copy/submit stay0. Legacy genuine map/retained validation/publication and
submission report gates remain true; public execution gates stay false.
The initial Host diagnostic-expectation failure is retained separately and the
fixture now checks absent map opt-in plus genuine two-result/sixteen-slot values.
All39 archived hashes verify under
build/web-webgpu-debug/artifacts/bindless-continuation/typed-map-custody1-validated/.
Public Emulated, ContentPlan3 completion, Metal Graphics and WOCC P4-P6 remain
incomplete. Next authenticate original Pending Result2/Join2 bindings and frozen
count replay, then genuine queue/map/unmap before same-journal commit/publication.

## Typed mixed readback demand and Result storage /2 (Chrome cold storage validated)

Cold3 gains an independent nested Result2/Map1 demand. Empty demand preserves
the existing recording-only eight-participant profile. Result counts derive
from the exact immutable original program and Inputs; Map1 derives one map and
three scope callbacks per unique Request from the original Source budget.
Both total callback tickets and registrations must fit the previously authored
Source manifest and the same Lifecycle2 seal/reservation; no late enlargement
or replacement graph/pool is allowed. A selected single-result workload authors
12 participants before Source admission. Missing opt-in, legacy profiles,
inconsistent copies/results, undersized Source/result/callback quotas and owner
ceilings reject before recording.

ResultStorage2 reuses the existing bounded snapshot factory and independent
Result quota, retaining the actual typed Cold3 admission, root, seal and Join2
instead of constructing a legacy admission. Join2 must retain the same program
and root. Snapshots remain unpublished. Null validates the value contract;
Vulkan/Metal diagnose missing typed storage payloads; WebGPU prepares owned CPU
Result storage only. Existing ResultStorage1 and recording-only profiles retain
their old gates. Typed callback custody explicitly refuses a map demand until
its same-root map receipts are integrated. Submission3, map publication,
ContentPlan3 completion and public Emulated remain closed. Final host/browser evidence below validates cold Result custody only.

Next consume these authored participants and ResultStorage2 in the existing
typed RuntimeState, then join genuine queue/scopes, map/unmap and numerical
readback before private commit and eventual public authorization.

Final rebuilt assertion-enabled MinGW/Null/artifacts pass6/6 in26.13 seconds;
source inventory289/289 and package/root8 checks pass. Genuine Chrome regression
passes3/3 in577.46 seconds: legacy comprehensive smoke179.38, recording/cancellation
235.93 and typed ResultStorage2 162.14. Compute/Graphics reports have success=true,
exit0 and errors empty. The selected storage path retains the same original
Cold3/root/seal/Join2, result capacity512, actual Result quota charge588 and cold
metadata148 Wasm bytes; authored callbacks12=base8+map1/scope3. Exact/short/exhausted
budgets, forged owner, independent quota and genuine caller-drop retention/release
pass. Hot allocation/query0; Asyncify17660/32768 bytes. Encoder/copy/submit0: this
is cold Result ownership validation, not map or GPU output proof. The archived
reports, binary identities and exact sources have42 verified SHA256 hashes under
build/web-webgpu-debug/artifacts/bindless-continuation/typed-readback-storage2-validated/.
The first build failure (deduced accessor type and temporary reference) is retained;
explicit return types fix it. The initial selected browser1/1 passed161.77 seconds;
the final regression consumes the corrected diagnostic and rebuilt binary.

Status: Contract Complete / Prototype Complete and scoped WebGPU cold
Result-storage/custody validation only. Submission3, mapping/publication, numeric
shader-selection readback, ContentPlan3 completion and public Emulated remain
closed. Metal public Graphics, renderer/WOCC P4-P6 and the full goal remain open.
Next integrate these already authored map participants and ResultStorage2 into
existing typed RuntimeState with genuine map receipts; then require same-root
queue/scopes/map/unmap and numeric result proof before journal commit/publication.
The recording-only eight-participant profile and all ordinary exclusive-content
guards must retain their old behavior.


## Mixed original readback recording / Execution3 (selected browser validated)

Execution3 independently opts into exactly Issuer3 / Callback3 / Command3 / Cold3
and Lifecycle2 over Payload3 / Freeze3 / Closure4 / Join2 / ContentPlan3. Null is
validator only; Vulkan and Metal reject missing typed payloads; WebGPU records
the original immutable shader/attachment/transfer stream into one encoder.
Transfer scopes create no native pass. Readback consumes the original Join2
per-mip copy decisions and canonical endpoints; physical geometry and offsets
are frozen, not recomputed. The ordinary exclusive-content guard stays intact.

Only a genuine sealed Pending original journal may consume the private frozen
Join2 projection. Public cold validators, legacy joins and map consumers retain
their Prepared/profile gates. Real recording error scopes all drain before
Recorded; partial drain and caller drop retain the exact command/resources.
Cancel abandons and rolls back the same journal and reclaims measured quotas.
Rollback restores values while advancing revisions: old State/Stream snapshots
refuse. The next candidate recaptures State/Stream/root through existing factories
over the identical Inputs/resources and authored budgets. Downstream publication,
native mutation, arena reset and physical-loss probes remain required and pass.

Rebuilt assertion-enabled MinGW/Null/artifacts6/6 passes23.06 seconds; source
inventory288/288 and package/root8 checks pass. The final affected browser group
passes3/3 in714.01 seconds: Execution2 /245.30, Issuer3 /231.01 and Recording3
/237.69 seconds. All six Compute/Graphics reports have success=true, exit0 and
empty errors. Recording3 Graphics observes encoder1/copyTextureToBuffer1/
queueSubmit0, real three-scope drain and exact cancellation quota recovery.
Hot allocation/query0; Asyncify25140/32768 bytes. Final evidence, binary identities
and modified fixture/runner sources are archived under
build/web-webgpu-debug/artifacts/bindless-continuation/readback-recording3-conditional-recapture-validated/;
29 final evidence hashes verify.

The first selected success /244.88 seconds is separately retained with12 hashes.
Two earlier selected failures and the initial legacy regression7/8 /864.40 seconds
are preserved; the latter's Execution2 Graphics deadline failure has42 verified
hashes. Preparation-only cancellation now preserves valid captured State/Stream;
recapture is limited to actual journal application/rollback. Redundant explicit
Inputs validation is removed from the recapture adapter; the existing factories
still validate the original Inputs. No quota-pressure case, production ceiling,
ordinary guard or deadline is relaxed. The seven unaffected legacy targets passed
the preceding binary; the three affected targets pass the final rebuilt binary.
Ten distinct browser targets pass across those runs, with the failed attempt
excluded from success. This is not a claim of one final-binary ten-target sweep.

This is Contract Complete / Prototype Complete and scoped WebGPU Backend Validated
for private mixed recording/cancellation only. Submission3 remains
DiagnosticFailure RHI-TYPED-NATIVE-EXECUTION-READBACK-COMPLETION-MISSING.
Map/result publication, numeric shader readback, same-Join2 submitted physical-loss
proof, ContentPlan3 completion and public WebGPU Emulated stay closed. Metal public
Graphics, renderer/WOCC P4-P6 and the full four-part goal remain open.

Next integrate the existing map/result machinery into this exact original chain.
Derive unique results and the additional four map/scope callback participants per
result before Source admission; retain the original reservation and independent
Result quota. Reuse canonical result storage and RuntimeState map callbacks rather
than another graph/pool. Submission requires genuine queue/scopes, map/unmap and
result proof before same-journal commit/publication. The original recording-only
profile keeps its eight-slot demand and submission refusal.


## Mixed callback and issuer preparation: Callback3 / Issuer3 (preceding validation)

Version3 preparation consumes the exact original mixed Cold3/Command3 and
Lifecycle2 seal/reservation over Payload3/Freeze3/Closure4/Join2. Callback3 adds
11 explicit owners and Issuer3 adds3, authored before Source admission. Reuse the
existing RuntimeState, local loss observer and seven native receipt preparations;
no additional physical loss registration, replacement graph, encoder or proof.
The independent native-issuers smoke consumes the same mixed original program
with encoder0/submit0; authentic Execution2 stays in native-execution smoke. It tests exact/short/exhausted CPU domains,
partial receipt rollback, device/queue/userdata mutation, sticky claims and
caller-drop/cancellation. Native-execution retains complete Cold3 pressure.
Execution1/2 reject Issuer3; selected Execution3 recording is validated above.
Submission3 and public BindlessCross stay DiagnosticFailure/version0. Source inventory287/287 and package/root8 checks
pass; rebuilt assertion-enabled MinGW/Null/artifact6/6 passes27.81 seconds.
Selected genuine Chrome1/1 passes232.92 seconds with errors empty, encoder0/
submit0 and required mixed issuer marker. Actual owners136; Wasm callback
Submission476 / Callback640 and issuer Recording316 / Submission288 bytes.
Exact/short/exhausted quotas, partial receipt failure3, native binding mutation5,
caller-drop, State expiry and final measured domain baselines pass. Hot
allocation0; Asyncify17624/32768. Eight-target rebuilt regression passes8/8 in855.07 seconds; archived under readback-issuers3-validated/regression/.
This is Contract Complete / Prototype Complete and scoped WebGPU Backend Validated
for private native preparation/cancellation only. Mixed copy/map/result,
same-Join2 physical loss, public Emulated, Metal Graphics and WOCC P4-P6 remain open.

## Mixed sealed cold admission: Lifecycle2 / Cold3 / Command3

Selected genuine Chrome validates the exact original
ContentPlan3/Join2/Closure4/Freeze3/Payload3 root and Lifecycle2 seal/reservation.
Legacy lifecycle1 refuses ContentPlan3; Cold1/2 and Callback1/2 keep exact old
profiles. Independent Submission/Callback quotas, same-root sticky command
claim, exact/one-less/exhausted pressure, forged owner/native mutation,
caller-drop retention and terminal release pass. No native encoder, copy,
submission, mapping, result or completion authority is added by this cold chain.

Assertion-enabled MinGW/Null/artifacts6/6 passes24.29s, source286/286 and
package/root8 checks pass; genuine Chrome selected1/1 passes245.12s with errors
empty. Admission3/Command3 is476/752 Wasm metadata bytes and122 owners; hot
allocation/query0. Source explicitly authors16 native calls before admission;
the previous15-call ceiling refuses. Initial240-second browser timeout is saved;
publication smoke retains complete legacy Cold2 pressure while selected execution
retains its authentic positive chain and complete mixed Cold3 pressure, under
unchanged deadlines.198 evidence hashes verify. Remaining seven-target browser
regression is running; public Emulated, Metal Graphics and WOCC P4-P6 stay open.

## Mixed immutable native inputs: Payload3 (selected browser validated)

Payload3 has an independent opt-in and exact Freeze3 / Closure4 / Join2 tuple.
Original transfer scopes and readback commands stay in the same immutable stream;
native source/destination and per-mip copy decisions are borrowed from authentic
Join2, with complete ordered coverage and finite command/copy/CPU/owner ceilings.
The reusable copy validator now exposes texture-to-buffer layout inspection,
parallel to buffer-to-texture inspection. Ordinary copy retains its exclusive
content rejection; this payload derives attachment authority only from the same
Join2/root/content plan. No copy emission or recording/map/completion authority
is granted. Null validates values; Vulkan/Metal diagnose missing inputs; WebGPU
provides Prototype preparation only. Cold1/2 and public Emulated stay closed.
Source284/284 and package/root8 checks pass; Rebuilt assertion-enabled MinGW6/6 passes25.54s; selected genuine Chrome1/1
passes201.07s with empty errors and the required Payload3 marker. Payload3
metadata5524 Wasm bytes; current legacy Payload2 is4788 (historical4404),
with quotas unchanged. Full rebuilt pre-Cold3 browser regression passes8/8 in838.65s; enabled244.13 /disabled246.68 /publication124.71 /selected197.71. Reports are separately archived and172 evidence hashes verify.

## Usage3 / StateBoundUsage3 / Dependency3 mixed readback contract

The independently opted-in version3 CPU projection joins shader/attachment
usage and retained TextureReadback in the same original Stream3/Inputs4/State3.
It requires TypedReadbackState1; Consumption2 and RenderBinding2 remain unchanged.
Each TextureReadback contributes its exact canonical parent texture range with
CopySrc / Transfer / TransferRead and its original destination buffer with
CopyDst / Transfer / TransferWrite. No descriptor provenance is invented.
The transfer scope is the single original readback command, inside the authored
transfer pass. Other transfer operations and old Usage1/2 profiles remain refused.

Readback command, mip and declared-byte totals have independent caller-authored
ceilings, including repeated copies. The buffer envelope is [0,sizeBytes):
sizeBytes denotes absolute capacity and the existing immutable readback layout
proves all offsets/footprints fit within it, including padding. Source identity
uses the original shared owner, registration generation and canonical inventory;
IDs or equal layouts cannot substitute for those owners. No extra graph is built.
Rejected visits emit no partial contributions. Existing core interval neighborhoods
join attachment writes to transfer reads with exact state/stage/access coverage.

Four-backend decisions: Null value validator only; Vulkan/Metal diagnose missing
original joins; WebGPU Equivalent CPU projection with execution false. Freeze1/2,
native closure/payload/recording/map/completion and public Emulated remain closed
to this new profile. Rebuilt assertion-enabled MinGW6/6 passes24.19s, source280/280
and package/root8 guards pass. Genuine selected Chrome1/1 passes219.09s with the
new Usage3 marker, errors empty and hot allocation0. The fixture projects12
contributions /11 texture cells /8 dependencies /29 passes /348 visits /66 pairs /
15 endpoint contributions. Original source lookup reuses originalLeaseIndex;
two readback operands must match the canonical texture and retained Source2.
The initial host diagnostic-expectation and browser single-reference failures
are preserved. Shared schema adds24 Wasm Freeze2 bytes (2620); quotas unchanged.
Final seven browser regressions pass7/7 in640.79s; all8 targets pass with selected1/1.
Both feature profiles have empty diagnostics/nativeErrors and public execution
false. Cold publication remains encoder0/submit0;123 saved evidence hashes verify.
Selected Asyncify peak17604 /309 unwinds fits the unchanged32768-byte ceiling.
These are selected CPU proofs;
no new copy/map/result, numerical shader output or platform/production authority.

Current selected cold-browser validation: CanonicalOriginalReadbackJoin/2
reuses the original native observation, resource/owner/allocation projections
and copy lowering for exact Prepared Stream3/State3/Inputs4/ContentPlan3 custody.
It retains Result2 and the authored Source2/destination, rejects an equal-Inputs
foreign state graph, and preserves Join1 ordinary-exclusive guards. The table
readback fixture authors destination512 and4x2 output before root admission;
no native copy/map/result is granted. Old recording/map consumers reject Join2.
Four backend decisions remain Null validator, Vulkan/Metal missing payload and
WebGPU private Prototype cold join. Public Emulated, Metal Graphics and WOCC
P4-P6 remain open.

TypedReadbackState/1 now requires an independent State3 version/opt-in before
capturing the same retained Source2 alongside the original table. Sources/Inputs
correspondence, source counts, hidden owners, committed observations and existing
range/state validation are reused. Ordinary State3 and State1/2 remain closed to
this intent; transaction admission must consume exactly the captured profile.
Null validates values; Vulkan/Metal diagnose missing joins; WebGPU permits CPU
capture only. Initial Join2 label-budget and State3 source-profile browser
failures are preserved. Rebuilt assertion-enabled host6/6, source278/278 and
package/root8 checks pass; selected actual Chrome1/1 passes223.53s. Cold Join2
metadata is1048 Wasm bytes with91 owners, source64/staging512/copy1 and hot
allocation0. Final seven browser regression passes7/7 in645.79s; enabled and
disabled profiles have empty diagnostics/nativeErrors and public execution
false; cold publication remains encoder0/submit0. All8 browser targets pass and
107 evidence hashes verify. This validates cold
custody only; it does not prove numerical shader output or mixed native execution.

## OriginalCommandReadbackResult/2 (mixed-program implementation boundary)

Latest required prerequisite: **OriginalTextureContentPlan/3** adds retained
attachment readback to the existing cell timeline. The new ReadbackRead action
is read-only: repeated copies charge commands/steps but create no content write
or generation. The exact Source2 canonical parent, frozen range, CopySrc usage
and ordered state must match; every copied cell must already be stored outside
an active attachment. Older profiles, missing declarations, capacity overflow,
DontCare/Discard, untouched neighbors and wrong state refuse without partial
counts. Original plan population, proposal and rollback use the same journal.

Four-backend decisions are explicit: Null sequence validator only; Vulkan/Metal
missing joins; WebGPU Prototype CPU plan only. Existing native closure, lifecycle
and completion consumers reject ContentPlan3. This prerequisite does not grant
copy/map/result publication or restore public Emulated. Final verification and
saved failures are recorded in the publication validation manifest.

The result inspector now consumes an independently opted-in version2 budget,
the existing immutable Stream1/2/3 and its exact Inputs owner. Approved shader,
attachment and barrier operations can precede readback; only readback operations
contribute result counts. The shared identity/allocation projections preserve
request deduplication, destination-alias refusal, original Result CPU and
unpublished-byte ceilings. Forged/foreign program or Inputs, malformed profiles,
missing readback and exceeded capacities publish no partial requirements. This
is CPU metadata only; four-backend lowering denies recording/map/publication.
Version1 native consumers still reject version2. Rebuilt assertion-enabled
host/Null/artifacts pass6/6 (23.16s), source guards273/273 and package/root8 checks.
Selected real Chrome execution passes1/1 (201.38s), retaining original Graphics
encoder1/submit1 and genuine completion. Mixed-program metadata measures1096
MinGW bytes /916 Wasm bytes; hot projection allocates0 and final program quota
returns zero. The initial fixture declared only512 aggregate bytes for two
readbacks and was correctly refused; explicit pre-admission1024 fixes the
fixture while unique result storage remains512. Its failure log is preserved.
Complete browser regression passes7/7 (638.09s), including enabled244.46s,
disabled246.79s and cold publication124.93s. Public execution remains false,
diagnostics/nativeErrors are empty and cold encoder/submit counts remain0.
All82 saved evidence hashes verify.
The original table destination/readback and budgets must next be authored before
its inventory/root admission, followed by exact root/seal native copy/map/result.
Public Emulated, Metal Graphics and WOCC P4-P6 remain pending.

## OriginalRetainedReadbackCorrespondence/1 (implementation boundary)

An independently opted-in bounded value request joins SourceLease2/readback
Source2 to the exact already-adopted OriginalCommandTextureLease in the existing
canonical Sources inventory. Inputs must consume the identical version, opt-in,
readback-count and nested-owner ceilings. Default-empty requests preserve the
previous wrapper-based correspondence. Resource IDs or equal layouts never
substitute for the original shared owner; attachment/view parent ownership is
preserved and no second canonical texture entry is added.

Admission charges each readback's hidden TextureSource metadata independently,
excluding only the original texture lease and destination already required in
the canonical inventory. Missing opt-in/version, exhausted source/nested-owner
ceilings, foreign owners, duplicate identities and mismatched Inputs requests
fail before native observation. Creation and hot revalidation consume the same
immutable request and existing quotas. This CPU correspondence grants no content,
copy/map/result, recording, submission or completion authority. Null validates;
Vulkan/Metal/WebGPU have explicit CPU Prototype lowering with native execution
false. SourceLease2 remains rejected by old native consumers until their own
versioned execution contracts are implemented and validated.

Validation (2026-10-09): rebuilt assertion-enabled host/Null/artifacts6/6
(23.14s), source inventory271/271 and package/root8 checks. The selected real
Chrome test passes1/1 (197.11s), including authentic buffer/texture/view owners,
single/multiple request ceilings, complete caller-drop retention and zero hot
allocation. Source/Inputs metadata admission measures640/1320 MinGW bytes and
532/884 Wasm bytes. Existing selected Graphics execution still records encoder1 /
submit1 and authentic same-journal completion; this new correspondence remains
CPU-only. The first missing fixture buffer-registration budget and the second
obsolete runner buffer-count expectation are saved as failures, not successes.
Fixture capacity is authored before registration; the runner checks exactly one
additional destination buffer. Complete browser regression passes7/7 (636.64s);
both feature profiles have empty diagnostics/nativeErrors and public execution
false; cold publication retains encoder0/submit0.68 evidence hashes verify;
same-root/seal output copy/map/readback, public Emulated and WOCC remain open.

Retained output-readback ownership increment (2026-10-09): SourceLease2 and
OriginalReadbackTextureSource2/TextureReadbackSource2 retain the same adopted
OriginalCommandTextureLease and request under the original source quota.
Extra owner counts are consumed by local and command graph admission; forged,
foreign, mixed-profile, changed-layout/quota and exhausted requests refuse.
The assertion-enabled host/Null/artifact suite passes6/6 and source guards269/269.
This is Contract Complete / Prototype Complete CPU ownership only. Ordinary
leased WebGPU binding and CanonicalReadbackJoin1 refuse Version2 before native
observations. Output copy/map/readback and completion authorization are separate
pending gates; public Emulated, Metal public Graphics and renderer/WOCC remain
closed. The selected real completion-to-output-owner browser test passes1/1
(195.91s), retaining the same genuine adopted color owner and unchanged completed
cell. Its Wasm lease metadata charge is116 bytes (host184); this validates CPU
source composition after real completion, without native copy/map/result proof.
All8 browser targets now pass across selected execution1/1, initial regression6/7
and repaired cold publication1/1. The initial Graphics120-second timeout remains
saved; a separate240-second-profile retry measures119.95s, and the reconfigured
cold publication CTest passes122.11s. Its test-only deadline now shares existing
Execution2 runner240/CTest520; production budgets are unchanged. Both complete
feature reports have empty diagnostics/nativeErrors and public execution false;
cold publication has encoder0/submit0.55 evidence hashes are verified. The full
four-objective goal, public Emulated, Metal Graphics and WOCC remain incomplete.

Latest selected WebGPU increment (2026-10-09): private CallbackCustody2 /
IssuerPreparation2 / Execution2 now consumes the exact original Cold2/Command2,
Payload2/Freeze2/Closure3 and Inputs4/State3/Stream3/Transaction3/ContentPlan2 chain.
A separately opted-in real Chrome fixture completes three depth Clear/Store
passes and one scalar-table Draw with encoder1/submit1, genuine scope/queue
completion and same-journal state/content commit;24 of28 cells remain unchanged.
This is scoped Backend Validated selected Graphics execution. Shader-selection
readback, complete generic graph coverage and public Emulated remain closed.
The immutable report and remaining gates are recorded in
`original-depth-table-expansion-publication-validation.yaml`.

The current 2026-10-01 Metal result is scoped Backend Validated for the
explicitly opted-in `MetalComputeBuffers/1` and `MetalComputeImages/1` public
Compute subsets.
Earlier dated evidence sections describe their original validation state;
the current backend table and final acceptance section supersede their broad
Metal-unavailable statements. Graphics, indirect execution,
renderer/WOCC and Production Ready remain pending.


Current WebGPU status (2026-10-03): public BindlessCross execution is
DiagnosticFailure with executablePayload=false and execution versions0. The
complete generic original command graph/cold completion envelope is missing;
WebGenericQueueAdmission/1 now rejects both public and raw generic submit with
RHI-WEBGPU-ORDINARY-COMPLETION-RESOURCE-GRAPH-UNPROVEN before queue acceptance.
WebGenericRecordingAdmission/1 also rejects raw general recording, finish,
immediate uploads and automatic readback before native mutation with
RHI-WEBGPU-COMMAND-RECORDING-USAGE-UNPROVEN; public finish returns no buffer.
The historical public readback evidence below describes earlier runs and cannot
certify current availability. Explicit original private transfer and closed
ordinary Compute/Graphics reservations remain independently tested. Cold
WebGenericCommandProjection/1 ownership metadata is Contract Complete /
Prototype Complete only; it does not reopen general or public bindless execution.
CommandUsage/1 + WebGenericCommandUsage/1 now validate original ordered ownership
metadata with independent counts/CPU quotas and atomic consumption; even a fully
consumed plan grants no native recording or completion authority.
Full graph consumption, unsupported ownership domains, bounded automatic
readback, RenderDoc and public/backend/platform/production gates remain open.

## Closure3 / Freeze2 original table custody (2026-10-09)

The separately opted-in Closure3 consumes Prepared Transaction3 with materialized
ContentPlan2 and its exact State3/Stream3, original Registry8 loss subscription,
accepted Input3 pipeline and adopted TableSource2. It retains actual native
parent/view/output/pipeline/table payloads and all existing scoped publication
quotas under explicit source, payload, owner, backing-byte and Recording ceilings.
Table writes must match the original ordered view owners, immutable revision and
Inputs4 pipeline/slot binding. The existing native inventory owns this custody;
there is no replacement graph, registry, journal or completion mechanism.

Freeze2 consumes Projection2 with that exact Closure3 and retains its original
clauses under independent CPU/owner/visit/pair/cell limits. Freeze1/Closure2 remain
restricted to their original profiles. Null validates L2 values only;
Vulkan/Metal selected original custody payloads are unavailable; WebGPU admits
private cold custody/frozen CPU candidates only. Recording, initialized GPU
contents, submission and real completion remain independent gates. Public WebGPU
Emulated, public Metal Graphics and renderer/WOCC P4-P6 remain open. Validation
of this increment passed selected real browser custody/loss tests with13 native
payloads,96 retained owners,4380/2596 Wasm closure/freeze bytes and18 retired quota
domains. This is scoped cold custody validation, with encoders/submissions0.

TypedNativePayload2 extends the existing immutable record/pass projection for
Freeze2/Closure3 only. It consumes original table group mappings and exact depth
Clear/Store pass arguments on the same graph under independent work/color/owner/
CPU ceilings. Payload1 remains Freeze1/Closure2-only. Null validates values;
Vulkan/Metal lack this selected original join; WebGPU may prepare private native
inputs only. Cold admission1 and its executor must refuse Payload2. Native
recording, submission, initialization and completion remain separate gates;
Payload2 now passes selected real browser native-input/budget/mutation/loss
validation:21 commands,15 projected calls,4 exact render passes,3 depth clears,
1 table bind,7 dependencies and4404 measured Wasm metadata bytes. Actual native
encoders/submissions remain0; this does not enable public Emulated. Rebuilt
complete enabled/disabled Chrome regressions pass2/2 (246.98/245.78s), with empty
diagnostics/nativeErrors and all public execution flags false. Assertion-enabled
host/Null/expansion tests pass6/6, source inventory264/264 and package/root8 checks.
Evidence lives in the publication manifest.

## Cold2 / Command2 original table lifecycle admission (2026-10-09)

Cold2 now pairs exclusively with Payload2/Freeze2/Closure3; Command2 pairs with
that exact Cold2. L2 request/requirements/profile tests and four-backend cold
lowerings retain explicit opt-in and unknown-version refusal. Null validates
values, Vulkan/Metal diagnose missing selected joins, and WebGPU prepares private
Prototype cold admission/connection only. No live executor authority follows.
The same original Source authors resources13 + encoder1 + command buffer1,
calls15, pending1 and callback tickets/registrations8 before admission. Existing
seal/lifecycle1 reservation, command shell, original observed loss and independent
Submission/Callback quotas are reused without another graph, journal or pool.

The selected real browser test passes exact/one-less/exhausted CPU admission,
wrong versions, forged owners, sticky one-shot claim, caller-drop, native table/
pipeline mutation, arena reset and actual physical loss. One-shot measurement
candidates reuse identical original Inputs4/State3/Stream3 and registered resources;
cancelled roots abandon before preparing a new original transaction. Cold2 holds
106 command owners, with428/752 measured Wasm admission/command bytes. Loss retires
all20 quota domains. The first measurement-helper failure is retained separately;
the existing retained-allocation helper avoids history-dependent quota markers.
Production budgets are unchanged. Actual encoders/submissions remain0, and old
Callback1 rejects Cold2. Authenticated Pending scans, recording, initialized
content, genuine completion/readback, public Emulated, Metal Graphics and WOCC
P4-P6 remain open. Evidence lives in the publication manifest.

Rebuilt assertion-enabled MinGW host/Null/expansion tests pass6/6 (24.75s), source
inventory265/265 and package/root8/8. Complete Chrome enabled/disabled profiles
pass2/2 (244.25/246.87s), with empty diagnostics/nativeErrors and all public
execution flags false. Four prerequisite browser targets pass4/4; together with
the selected Cold2/Command2 test all7 current browser targets pass across these
runs. Reports are retained under table-cold2-validated/. No Platform Validated
or Production Ready promotion follows.

## L2 intent

BindlessCross is an explicitly enabled optional extension, not DescriptorTable
support and not a consequence of a platform being validated. A request names a
shader package, typed resource tables, shader stages, allowed operations,
non-uniform indexing requirements, and bounded snapshot/update/memory budgets.
Input attachments and GPU addresses are outside version 1.

Tables cover sampled textures, samplers, storage textures, uniform buffers,
read-only storage buffers and read/write storage buffers. Shape, sample type,
storage format/access, buffer layout/range/alignment and resource states remain
explicit. Logical table identity is independent of physical descriptor slots.
The compiler artifact authorizes the complete logical-to-physical mapping.

Buffer tables additionally name a compiler layout identity, element stride,
element alignment and minimum binding size. The workload must exactly match that
layout. Zero/missing layout or device binding-size limits reject. Version 1's
current descriptor payload binds the complete buffer at offset zero: preparation
checks the real buffer usage and size, integral element count, and the actual
uniform/storage binding-size ceiling. Shader element counts come from this
validated range and are immutable after publication. Nonzero buffer subranges
are not yet lowered and must not be emulated by changing graph metadata alone.
Null has explicit validator ceilings (64 KiB uniform, 128 MiB storage); Vulkan,
Metal and WebGPU must supply limits from their enabled device contract before
authorizing buffer tables. No default native limits are substituted.

Storage-texture access is an explicit descriptor shape: `WriteOnly`,
`ReadOnly`, or `ReadWrite`. The restricted Slang `RWTexture2D` bindless helper
publishes `ReadWrite` in its sealed physical binding metadata. Package loading,
plan matching, and WebGPU's native bind-group layout must agree with that
access; a `Load` on `WriteOnly` or `Store` on `ReadOnly` rejects. Null validates
the contract, Vulkan/Metal retain their native storage-image access semantics,
and WebGPU cannot claim executable support when its device rejects the
requested WGSL/storage-texture access mode.

Only a validated lowering plan can authorize execution. Its device identity,
contract version, package identity, operations, capacities and layout must match
the resources and executable pipeline. Optional extension discovery is not
opt-in; no base fallback plan may enable the extension.

`BindlessPackagePublication/1` publishes a small YAML envelope last. It names one
payload YAML and the SHA-256 of its exact bytes. That payload contains the typed
mapping, all module metadata and a SHA-256 for every source/target/reflection
file. Runtime loading is bounded by explicit file-count, per-file and total-byte
budgets, verifies each hash, checks reflected binding identities and complete
module coverage, and retains immutable file bytes. Paths inside the package are
flat relative filenames; traversal or ambiguous paths reject. A failed load
publishes no package. The full digest identifies content; an L2 uint64 identity
is deterministically derived from it, and is never sufficient by itself for
execution authorization. Pipeline creation must consume the retained target
bytes from the verified package, not reopen its filenames after verification.

WebGPU has a backend-private preparation path that selects retained Emulated
WGSL and its retained `WebGpuBindingSpace/5` reflection by entry and stage. It
validates scalar slot coverage and physical group metadata before creating a
native shader module. Public WebGPU execution is separately validated for
non-uniform Compute SampledTexture `load/query/sampleLevel` with independent
sampler indexing, ReadWrite StorageTexture `load/query/store`, UniformBuffer
`load`, ReadOnlyStorageBuffer `load/query`, and StorageBuffer
`load/query/store/atomicAdd`, and Fragment SampledTexture
`load/query/sampleGrad` with an independently indexed sampler, and Fragment
ReadOnlyStorageBuffer `load/query` with the explicit
`Slang.uint32.structured/1` ABI, through the public RHI snapshot, pipeline,
pass, queue-completion and fault-readback path. `sampleGrad` is supported when
explicit gradients are computed before index-dependent control flow. Fragment
implicit-gradient translation and comparison sampling remain unavailable. This
private module preparation does not authorize other resource classes,
operations, formats or stages in its broader fixture.

The private compute pipeline preparation additionally filters the sealed
physical layout to the named compute entry's active reflected bindings and
reuses L2 layout/reflection and WebGPU binding-space validators. A private
native compute pipeline factory consumes that layout and the retained WGSL.
An Emscripten/Chrome private fixture creates that native pipeline, binds real
resources, dispatches, waits for queue work, and reads back buffer and fault
results for the broader compiler operation matrix. A separate public RHI fixture
verifies the allowlist above; private results do not widen public capability.

This publication contract is backend-neutral: Null validates it; Vulkan consumes
verified SPIR-V, Metal verified MSL plus argument-buffer reflection, and WebGPU
verified scalar WGSL plus binding-space reflection. Missing target, mismatched
mode/ABI/mapping or expired device authorization rejects on all four backends.
Loading a valid package alone does not enable the extension or authorize a draw.

Null may validate a compiled Native descriptor-array ABI or an Emulated scalar
ABI. Plans therefore keep `bindingModel` separate from `execution`: Null always
returns Validator/non-executable, while its descriptor writes follow the
compiled binding model. Emulated opt-in and all mapping/budget requirements
still apply. Native backends must match the package execution mode exactly;
this validation route never creates Native/Emulated authorization on Null.

`BindlessAuthorization/1` is issued by the owning device extension only after a
sealed snapshot and retained package reproduce the same complete lowering plan.
The ticket retains the package and observes the issuing device lifetime epoch.
The base extension's issuance hook fails closed. Null explicitly accepts only
its non-executable Validator plan; each GPU backend must prove native storage,
fault completion/readback and lifecycle ownership in its own hook before it can
issue an executable ticket. Matching capability fields and package bytes alone
do not grant authorization.
Pipeline factories consume that ticket and a named reflected entry; ordinary
pipeline factories continue to reject tagged bindless packages. Null compute
pipelines validate the retained interface and retain Validator/non-executable
authorization, never a GPU execution claim.
Vulkan and WebGPU device-owned factories consume the ticket, retained target
bytes and identical layout only for their published stage/operation allowlists.
Vulkan's public Graphics path is validated for Vertex sampled-texture
`load/query` and Fragment sampled-texture `load/query/sampleLevel/sampleGrad`
with indexed sampler bindings and the completion-validated fault latch. Vertex
fault reporting requires the queried and enabled Vulkan vertex storage-side
effect feature. WebGPU public Graphics is validated for Fragment sampled-texture
`load/query/sampleLevel/sampleGrad` with a separately indexed sampler;
`sampleGrad` uses explicit gradients computed before index-dependent control
flow. Fragment ReadOnlyStorageBuffer `load/query` is validated for the
`Slang.uint32.structured/1` ABI, and Fragment UniformBuffer `load` for
`Slang.float4.uniform/1`. Fragment StorageBuffer `load/query/store/atomicAdd`
is validated for `Slang.uint32.structured/1` with exact read/write access
declarations and completion-gated counter readback. Implicit-gradient
translation, comparison sampling and other graphics resource classes/stages
remain diagnostic failures.
Metal Native execution is available only for the explicitly opted-in Compute
subsets defined by `MetalComputeBuffers/1` and `MetalComputeImages/1` below.

Graphics authorization consumes the same ticket, explicit vertex/optional
fragment entry names, and the existing L2 render-state descriptor. That
descriptor must not supply a layout, package or shader entries: those derive
exclusively from the retained publication. Both selected stages merge their
reflected binding visibility. Depth-only pipelines may omit the fragment stage;
color pipelines require it. The restricted publication has no tessellation ABI,
so patch/tessellation and nonzero subpass selection reject explicitly. Raster,
depth, blend, vertex-input and viewport/scissor state use the shared L2 validator.
Null validates direct draw/drawIndexed only. Vulkan's public direct Graphics
path is validated for the documented Vertex/Fragment sampled-texture access and
indexed samplers. WebGPU's public direct Graphics path is validated for
Fragment sampled-texture `load/query/sampleLevel/sampleGrad`, UniformBuffer
`load`, and ReadOnlyStorageBuffer `load/query` for their documented ABIs; Metal Graphics
remains DiagnosticFailure pending an Apple-native executable payload.
Indirect draws, secondary commands and bundles cannot consume this authorization
until their own access and retention contracts exist. Subpass advancement is
rejected for indexed pipelines; pass end releases local references while command
ownership remains.
The initial graphics validator accepts single-subpass, single-sample passes with
explicit attachment formats matching the selected pipeline. Name-only attachments,
resolve, layered and multi-subpass forms reject until resource-backed compatibility
validation is connected. Dynamic viewport/scissor must both be issued after each
pipeline selection before drawing. This restricted gate applies identically to
all future backend execution factories; it is not a Native capability claim.

Command encoders explicitly budget retained bindless pipeline and snapshot
objects. Pass extension binding checks the selected pipeline's authorization,
device, plan, snapshot generation and conservative resource access set. Ordinary
descriptor binding cannot satisfy an indexed pipeline; dispatch without the
validated snapshot rejects. Descriptor and indirect paths cannot bypass this
gate. The shared `validateBindlessIndirectDrawAccess` contract now checks
`DrawIndexedIndirectCommand` stride/count/offset against a live indirect-usage
buffer and requires one declared range covering every possible record with
`ShaderRead`, `DrawIndirect` and `IndirectCommandRead`. Invalid arguments,
malformed declarations and incomplete coverage return respectively
`RHI-BINDLESS-INDIRECT-ARGS-INVALID`, `RHI-BINDLESS-ACCESS-SET-INVALID` and
`RHI-BINDLESS-INDIRECT-ACCESS-INCOMPLETE`. A command encoder explicitly reserves
`maxBindlessIndirectAccesses`; the Null render pass copies matching declarations
from the transient graph view at snapshot bind and checks the retained value at
`drawIndexedIndirect`. Overflow fails with `RHI-BINDLESS-INDIRECT-ACCESS-BUDGET`.
Null still rejects execution after validation. Vulkan accepts one indexed
indirect record when its reported `drawIndirectFirstInstance` feature is enabled,
the encoder reserved the declaration, and the bound snapshot's retained access
set covers the entire record. The native command retains the args buffer through
completion; the public fixture verifies fence, Clean fault report and independent
pixel readback. Multi-draw and count variants reject. Metal still rejects
bindless indirect execution. WebGPU allows one indexed indirect record when the created device has enabled `indirect-first-instance`,
the pass retains a full indirect access declaration and the bound index buffer
is encoded before the draw. The Chrome public fixture requires completion,
Clean fault report and independent pixel readback; missing index binding
rejects with `RHI-WEBGPU-INDIRECT-INDEX-BUFFER-UNBOUND`. Count/multi-draw
remain diagnostic on both backends.
WebGPU separately reports `indirectFirstInstance` from the created device's
`features`, never from adapter availability or ordinary `IndirectDraw` support.
Vulkan reports the same independent fact only when the queried core
`drawIndirectFirstInstance` feature was included in logical-device creation.
Neither fact authorizes a bindless indirect draw by itself.
A false value requires the explicitly defined zero-`firstInstance` shader
instance-base lowering before any GPU-driven workload can execute.

`IndirectInstanceBase/1` is a value ABI for that lowering, not an execution
capability. Its physical indexed command remains the existing 20-byte ABI and
has `firstInstance=0`. A companion 16-byte record has four uint32 fields at
offsets 0/4/8/12: logical first instance, instance count, instance capacity and
version 1. Encoding checks nonzero capacity, base <= capacity and
count <= capacity - base before emitting the record. A rejected input emits
zero instances, zero physical first instance and an invalid (zero-version)
mapping; GPU encoding also latches the existing stable GPU index fault.
Zero-count culling records are valid but no local instance may resolve from
them. Resolution validates version and the complete range before checking
local instance < count and adding the base, with no uint32 overflow.

The shared CPU lowering is `semantics::IndirectInstanceBaseLowering`; the
compiler emits matching Slang encode/resolve helpers only with explicit YAML
`indirectInstanceBaseContractVersion: 1` and a fault latch. The version belongs
to the verified package, lowering metadata and plan digest. Vertex resolution
returns validity without writing a vertex storage buffer: a consuming shader
must suppress invalid instance resource accesses and forward the fault to an
authorized fragment/compute latch path. A GPU writer must use guarded encoding.
For all four backends this increment is value validation only: Null validates
CPU values, Vulkan/Metal retain their existing native first-instance paths,
and WebGPU keeps no-feature indexed recording closed until command generation,
mapping binding/access declarations, guards and completion readback are
consumed together by a public draw. Compiler metadata alone grants no draw.

`IndirectInstanceConsumer/1` declares a scalar read-only storage binding, a
fixed mapping record index and the exact nonzero logical instance capacity in
the offline package. Records use `IndirectInstanceBase/1` (16-byte stride).
The mapping binding is Vertex-only and distinct from tables, metadata and the
fault latch. Its full extent and Vertex ShaderRead access must be captured by
the immutable snapshot and declared by the pass. Snapshot preparation rejects
an insufficient or non-16-byte extent before recording. The generated reader
loads only the offline fixed record, then checks version, exact capacity, complete range and
local instance ID before computing a logical ID. Failure returns validity;
Vertex must forward it without performing instance-dependent resource access.
Fragment reports the failure to the authorized latch before any material
operation. A clipped primitive alone cannot prove fault reporting.

| Consumer lowering | Null | Vulkan | Metal | WebGPU |
|---|---|---|---|---|
| Mapping validation and plan identity | Validator | Shared L2 validation | Shared L2 validation | Shared L2 validation |
| Scalar Vertex mapping load, Fragment fault forwarding | No GPU execution | Diagnostic until this consumer subset is executed | Diagnostic: Compute-only issuance excludes this Graphics consumer | Equivalent scalar binding; direct graphics fixture must verify pixels, suppressed side effects and completion-gated fault readback |
| Zero-base indexed indirect authorization | Diagnostic | Existing native first-instance path only | Diagnostic | Diagnostic until a retained backend-owned producer proves physical args and mapping correspondence |

Consumer metadata does not certify arbitrary GPU-written indirect arguments.
`BindlessCapabilities::indirectInstanceConsumerContractVersion` independently
authorizes the exact reader subset; it is zero on unvalidated GPU backends.
The producer and consumer must share capacity, record identity, resource
generation and ordered accesses; a caller boolean is never a zero-base proof.

`ZeroBaseIndirectProducer/1` is a compiler-owned processing role, separate from
application shader entries. The closed kernel reads one logical 20-byte
indexed command from a uint storage buffer at an explicit word offset. Its
16-byte `Slang.uint4.uniform/1` parameter record carries that offset and the
exact consumer capacity as integers (never float conversion or NaN bitcasts). It writes a
20-byte physical command and 16-byte mapping through bounded typed helpers.
Every path writes physical `firstInstance=0`; invalid source range or capacity
writes zero instances and an invalid mapping, and reports the stable index
fault. A bounded empty record remains valid culling output.

The offline generator exclusively owns this entry and its fixed resource
schema. Application source, renamed entries, alternate operations and resource
layouts are rejected. Runtime package loading compares the complete source
and complete publication payload against build-generated canonical identities
for Native and Emulated. The latter seals the exact SPIR-V/MSL/WGSL,
reflection, lowering layout and compiler identities; rehashing a substituted
target cannot certify it. Builds without the host shader toolchain emit empty
executable identities and reject this role. Matching packages require the
same authoritative compiler/generator build. Adding the role to an application
package does not certify it. Compiler provenance is not an execution
authorization or GPU completion proof.

| Producer role | Null | Vulkan | Metal | WebGPU |
|---|---|---|---|---|
| Package/schema/source validation | Shared validator | Shared validator | Shared validator | Shared validator |
| Protected output allocation | Validator-only storage | Diagnostic | Diagnostic | Bounded registered native storage with initialization and write seals; no draw ticket |
| Zero-base draw | Diagnostic | Existing native first-instance path only | Diagnostic | Diagnostic until prepared kernel, protected packet, ordered producer/consumer commands and completion-gated readback are jointly verified |

`BindlessIndirectPacket/1` owns its physical arguments and mapping. Both are
initialized before publication and sealed against host writes, transfer
writes and ordinary writable descriptors. Only the approved backend processor
may write the native storage. Allocation is a bounded pool reservation, uses
reported native footprints and never grows or waits. Retained packet owners
must hold that reservation through real completion. Null packet allocation
is only storage/permission validation and cannot authorize a draw. Protected
storage capability is independent from trusted production and draw execution.
The storage capability is independently versioned as
`indirectPacketStorageContractVersion`. Null publishes version 1 solely for
storage validation. WebGPU publishes version 1 only for the tested registered
native storage and permission subset. Vulkan/Metal keep version zero. All
trusted producer execution and zero-base draw gates remain independent.
Candidate preparation
reproduces the consumer plan, requires fixed record zero and exact capacity,
owns and seals the integer parameter buffer too, and reserves explicit count
and per-packet byte ceilings before resource creation. Reported footprints
must fit those reservations; failures release both quotas. Pool destruction
does not release surviving packet owners. Device epoch expiry invalidates the
packet. These tests cannot prove queue completion or authorize any draw.
`BindlessIndirectPacketRecording/1` adds a single-use recording lease, distinct
from storage preparation and pipeline authorization. Acquisition must consume
a live sealed consumer snapshot, its exact plan and generation, a nonzero
encoder recording identity, and the packet's own scalar mapping binding.
That binding must name the packet-owned 16-byte mapping with Vertex read
visibility and the consumer's fixed record zero and exact logical capacity.
An equal plan alone does not certify a different mapping or snapshot. The lease
retains both packet and snapshot and excludes another writer while recording
or in flight. Abandonment releases an unsubmitted lease; a backend-owned
submitted lease may release only on real completion. Null can validate lease
ownership and rejection, but cannot provide GPU completion evidence.
The move-only lease is allocation-free on successful acquisition, validation
and cancellation. It validates the exact retained snapshot object again when
consumed; another sealed snapshot with the same plan and mapping does not
replace that object. Missing owners or zero recording identity reject with
`RHI-BINDLESS-INDIRECT-PACKET-RECORDING-INVALID`; changed generation or plan
uses `RHI-BINDLESS-INDIRECT-PACKET-PLAN-MISMATCH`, changed mapping uses
`RHI-BINDLESS-INDIRECT-PACKET-CONSUMER-MISMATCH`, competing ownership uses
`RHI-BINDLESS-INDIRECT-PACKET-IN-FLIGHT`, and a different object or recording
identity at consumption uses `RHI-BINDLESS-INDIRECT-PACKET-RECORDING-MISMATCH`.
An integer recording identity supplied to this ownership validator is not a
command execution ticket. The future native processor must consume its own
encoder identity and move the lease into completion-owned submission storage.

Producer record declarations must include the full logical source binding and
integer parameter reads in Compute, and the full protected destination ranges
as Transfer writes for the approved intermediate-to-packet copy. The source's
storage usage, uint32 stride and five-word logical record range are checked
before recording. The consumer independently declares mapping Vertex reads
and physical `DrawIndirect` reads. No declaration check grants privilege to
an ordinary transfer or writable descriptor; those seals remain enforced.
The shared buffer access validator requires one complete declaration covering
each range, state, shader/pipeline stage and access intent. Record-only source
declarations are insufficient because version 1 binds the entire source buffer
at offset zero. Invalid source usage, uint32 extent or five-word range uses
`RHI-BINDLESS-INDIRECT-PRODUCER-SOURCE-INVALID`; omitted or mismatched source,
parameter or output declarations use
`RHI-BINDLESS-INDIRECT-PRODUCER-ACCESS-INCOMPLETE`. The ordinary indirect
validator reuses the same coverage check and preserves its existing diagnostic
IDs. Snapshot preparation, record validation and package authorization reuse
full successful-plan comparison, including all budgets, descriptor count,
device/package identities and executable/binding mode; labels do not authorize
execution.
All four backends retain the zero-base draw diagnostic until the trusted
processor and same-command lease consumption are implemented and validated.
`PreparedZeroBaseIndirectProducer/1` is a backend-private preparation primitive:
only the compiler-owned producer package may create it. It validates the full
scalar resource shape and native buffer usage/extent/device, and creates its
pipeline and physical bind group before recording. Recording uses the retained
immutable group without layout/vector construction, consumes an explicit
per-command prepared-producer capacity, and excludes reuse of that prepared
producer until completion. Command cancellation releases unsubmitted ownership;
submission transfers a preallocated retention record to the native queue
completion callback. Its explicit device-owned completion owner reserves a
fixed number of submission slots during command preparation. An unknown queue
completion failure quarantines the slot, closes further preparation/recording/
submission on that owner and keeps producer storage and leases. A successful
callback releases only its exact generation ticket; unsubmitted cancellation
releases its reserved slot. An observer installed at owner preparation uses
the actual native device's `lost` promise to close the owner and release its
remaining storage. Late callbacks cannot match recycled slots. No CPU frame
signal or synthetic loss report can provide this proof. Null tests this
bounded ownership state machine only; Vulkan/Metal reject this
primitive until their owned payloads exist; WebGPU implements the scalar group.
This primitive does not issue protected packet storage, authorize a draw, or
publish the public trusted-producer capability. Its GPU fixture must compare
integer parameters, physical arguments, mapping and faults against the L2
reference, including culled records and illegal source/range inputs.
Recording holds immutable snapshots and pipeline/package objects through command destruction; real backends must
add GPU-completion ownership before accepting submission. Null revalidates its
retained tickets/snapshots at submit and makes no GPU-completion claim.

Native indexed retention reserves distinct pipeline and snapshot slots during
command creation from `CommandEncoderDesc`. Repeated ownership of the same object
does not consume another slot; capacity zero, excess unique objects, null payloads
or retention outside recording reject before indexed work. Abandoning an unsubmitted
command releases its references. Submitted references and their leases survive
until actual GPU completion even if the public command wrapper is destroyed;
timeouts do not release them. Null uses its existing command lifetime, Vulkan
uses its native fence, and WebGPU uses its submitted-work-done callback for the
published Emulated subset. Metal execution remains closed until its native
completion path is validated against the same ownership rule. This bounded
retention mechanism alone is not snapshot authorization or resource-byte accounting.
Vulkan render/compute `setPipeline` retains a live bindless authorization ticket
in the command's bounded pipeline domain before recording the native pipeline
bind; expired tickets and exhausted reservations reject before that bind.

Vulkan's existing shared retirement queue has an explicit 64-command ceiling.
Admission checks precede native acceptance; exhaustion reports
`RHI-VULKAN-SUBMISSION-RETIREMENT-CAPACITY` without waiting or silently expanding.
After proven completion frees slots, the unaccepted command may be retried. The
ceiling is an internal command-lifetime bound, not a promise of 64 live snapshots
per workload; workload snapshot and byte budgets are independently required.

The payload embeds the exact UTF-8 `loweringYaml` bytes whose digest is carried
by every entry's reflection. Parsed table/binding metadata must match that
lowering document; changing otherwise valid table IDs or sampler associations
cannot silently detach the plan from compiled helpers. Generated source is
persisted as UTF-8/LF on every host so reflection and file hashes consume the
same bytes. File-count budgets include the envelope and payload. Byte budgets
bound input bytes, not the third-party YAML parser's total resident allocation.

## Backend decisions

| Backend | Authorized target | Until execution is implemented and proven |
|---|---|---|
| Null | Validator only | No native GPU claim |
| Vulkan | Native descriptor arrays | Backend Validated for bounded Compute classes, Vertex sampled-texture load/query and Fragment sampled-texture load/query/sampleLevel/sampleGrad with sampler indexing; all other unsupported operations DiagnosticFailure |
| Metal | Native argument buffers | Backend Validated for opted-in Compute UniformBuffer load (`Slang.float4.uniform/1`), ReadOnlyStorageBuffer load/query and StorageBuffer load/query/store/atomicAdd (`Slang.uint32.structured/1`) through prepared bindings; independent MetalComputeImages/1 opt-in adds Texture2D Rgba8Unorm load/query/sampleLevel with ordinary sampler indexing and R32Float ReadWrite storage load/query/store; Graphics, indirect and all other image domains DiagnosticFailure |
| WebGPU | Historical Equivalent bounded scalar binding/operation lowering; original ownership migration pending | Current public execution DiagnosticFailure/version0 under WebGenericQueueAdmission/1; earlier public readback evidence is historical. Original private cold ownership/recording/submission prerequisites remain independently tested; no general bindless or Production promotion |

Native descriptor indexing requires queried AND enabled features for every
requested resource type/stage and an executable payload. WebGPU platform
validation does not establish native binding-array support. An unimplemented
path must remain unavailable even when its shader or descriptor facade exists.

`VulkanDescriptorIndexingEnablement/1` records the four fixed-array indexing
classes (sampled image/sampler, storage image, uniform buffer, storage buffer),
their dynamically uniform and non-uniform feature bits, and vertex/fragment
storage side-effect features. Device bootstrap copies only queried supported
bits into the actual Vulkan 1.2 device-create chain, then reports that exact
selection after successful device creation. Runtime re-queries physical support
and limits, rejects unsupported reported enablement, and keeps absent reports
unavailable. Synthetic runtimes never produce native query evidence. Neither
query success nor enabled bits publishes BindlessCross execution support; the
typed payload, package authorization, complete budgets and execution tests are
separate mandatory gates. No runtime arrays, partial binding, variable counts or
update-after-bind bits are enabled by this fixed-array bootstrap. Null remains
Validator; Metal/WebGPU retain their distinct feature and execution contracts.

## WebGPU bounded equivalent lowering

The compiler generates ordinary static resource bindings and a switch at each
operation from an explicit typed schema. It does not rewrite arbitrary WGSL
with a resource-name regex. Only the selected branch executes reads, writes and
atomics. Independent texture/sampler indices consume a bounded combination
budget. Unsupported operations fail at offline compilation.

Implicit texture derivatives may become explicit gradients only with a proven
uniform evaluation point. Otherwise reject. LOD=0, evaluate-all-and-select,
hidden resampling, auto-atlasing, resource substitution, and splitting dependent
dispatches are not equivalent lowerings. All targets use row-major Slang matrix
compilation. Shader package identities include the lowering version and variant.

Offline YAML variants declare finite capacities. Runtime selects the smallest
variant that contains the complete workload and fits actual device limits,
including reserved bindings and every active shader stage. Adapter limits can
authorize a device request, never execution. Missing device limits fail closed.
No runtime frame-loop compilation or automatic capacity growth is allowed.

The active prefix of each typed table is distinct from its offline capacity.
Every generated operation reads the immutable snapshot metadata, rejects counts
larger than the compiled capacity, and rejects indices outside the active prefix
before touching a resource. Sampling independently validates the sampler prefix.
The artifact declares `SnapshotActivePrefix/2` and `SnapshotMetadata/1`; without this guard ABI a workload
smaller than capacity is rejected. The runtime must publish counts with the same
material/instance/table generation and cannot derive them from physical slots.

Inactive physical slots still require live resources. Candidate preparation may
explicitly initialize them by aliasing a chosen, already initialized active entry
of the same typed table. This is caller-declared padding, never an automatic
substitute for a missing active resource. Padding consumes update budget and
retains normal descriptor liveness. CPU handles cannot address inactive entries;
guarded shader execution cannot select them. Null validates this contract,
Vulkan and Metal use fully populated arrays, and WebGPU uses fully populated
scalar bindings. Missing guard metadata or incomplete initialization rejects on
all backends. Native execution gates remain separate.

## Lifetime, synchronization and failures

Resource handles contain generations; shader indices are snapshot-local and
must not be serialized as asset IDs. Material/instance data and the table are
published atomically as one generation. Recorded snapshots are immutable.
Backend-owned native storage survives actual GPU completion; CPU render-thread
consumption does not authorize descriptor-slot reuse. Snapshot count, update
count, resident bytes and retirement are explicitly budgeted.

Submitted-work completion and device shutdown are separate scopes. A recorded
but unsubmitted command is not completed work. Polling an accepted submission
may release its retained objects only after its native completion primitive
reports success. An explicit wait outside the frame loop uses one absolute
monotonic deadline; timeout, missing fence or device loss retain ownership and
must not advance completion serials. Repeated waits do not restart that deadline.
Completion releases descriptor storage and its budget leases even when a CPU
command wrapper remains alive. Discarding unsubmitted commands may release them.

| Backend | Per-submission completion evidence | Explicit finite wait |
|---|---|---|
| Null | Validator consumption only, no GPU evidence | Validator only |
| Vulkan | Successful status/wait on the submission's VkFence | Remaining absolute-deadline duration passed to vkWaitForFences |
| Metal | Completed native command-buffer callback/status | Existing deadline contract; failed command is not successful execution |
| WebGPU | Original private queue reservation requires real WorkDone Success plus all three real Success/NoError submission scopes | Generic/public completion DiagnosticFailure until complete original graph/envelope; queue Success alone is not proof |

Waiting for submitted RHI commands alone does not establish completion of
presentation, external queue work or entire-device teardown. Vulkan's current
device shutdown gate stays closed until those scopes have their own proof.

RenderGraph declares the full possible resource-access set, subresources and
states before execution. Binding a table neither creates barriers nor permits
unreported storage/indirect accesses. All physical bindings are initialized to
declared live resources, never dummy substitutes. Invalid CPU handles fail before
record; GPU index guards prevent the access and latch a diagnostic. A faulted
validation frame is not successful rendering.

GraphSnapshotBinding/1 connects the actual compiled pass declaration set to
Null's indexed command validator. It filters required resources by the sealed
pipeline's active shader stages and rejects foreign command encoders. Snapshot
metadata is a read-only graph import that retains the snapshot and its budget
lease. The writable fault latch has a separate graph import with the same
ownership and resource identity rules; passes must declare its stage and
shader read-write access. A backend that publishes fault-readback contract
version 1 also gives the sealed snapshot an owned bounded CopyDst/GpuToCpu
buffer; the graph imports it separately and the consuming pass must declare
Transfer/TransferWrite over the complete four-byte range. The declaration does
not itself prove completion or host visibility: the backend retains the
snapshot until real GPU completion and reports only invalidated readback data.
Vulkan's bounded Native Compute and Vertex/Fragment Graphics allowlists and
WebGPU's bounded Emulated Compute allowlist and Fragment sampled-texture
`load/query/sampleLevel/sampleGrad`, UniformBuffer `load`, and ReadOnlyStorageBuffer
`load/query` subsets consume public indexed recording.
Metal and WebGPU graphics stages/operations outside those subsets remain
unavailable.

Mandatory fault collection is an execution gate beyond the current helper
`out bool` result. A package cannot claim GPU readiness merely because its
caller may choose to write a fault buffer. The compiler/backend contract must
guarantee fault latching for every permitted operation and read it after real
GPU completion before marking a validation frame successful. WebGPU vertex
shaders cannot use a writable storage buffer as a general fault sink; dynamic
vertex indices therefore require a separately proven valid-index producer and
bounded consumer contract, or that operation/workload must reject. A fragment
varying alone is insufficient when a bad vertex produces no fragment. This
constraint applies to the WOCC GPU-driven browser gate.

`BindlessFaultLatch/1` is a compiler-side increment. A finite YAML variant may
declare `faultLatchBinding`; generated compute/fragment helpers atomically OR 1
into a one-word storage buffer on every invalid table, element, texture or
metadata access before returning `fault=true`. The binding and active stages
are sealed in package reflection, consume physical and per-stage storage limits,
and are checked by the package loader and planner. Indexed vertex operations
cannot use this variant until bounded index provenance has its own contract.
The backend must initialize the word to zero before each submitted validation
workload and read it after GPU completion. A reused latch requires completed
prior GPU work and an explicit CopySrc-to-CopyDst transition before clearing;
clearing one in-flight snapshot is forbidden.
The Null snapshot validator now owns the reserved four-byte fault buffer, binds
it only during seal, rejects caller replacement, and requires a graph declaration
with shader read-write access. Its byte charge must fit in `snapshotBytes` in
addition to immutable metadata. This is validator ownership: Null performs no
GPU clear, and sharing one latch across overlapping GPU
submissions is forbidden. The planner requires the latch stage mask to match
the union of the indexed tables' stages. A sealed snapshot offers one
allocation-free `BindlessFaultSubmissionLease`; a second overlapping use fails
with `RHI-BINDLESS-FAULT-SUBMISSION-IN-FLIGHT`. A real backend must retain this
lease until GPU completion and readback, including when the caller discards its
command wrapper. This reservation alone does not make fault collection executable.

`BindlessFaultSubmission/1` defines ordering for an executable submission. The
backend acquires the snapshot lease at `bindSnapshot`, reserves a bounded fault
report slot, and schedules a zero clear before the first indexed operation in
the same ordered submission. A transfer clear cannot be emitted inside an open
render pass. Vulkan and Metal must therefore encode the clear in a native
pre-pass/preamble or defer opening their native render encoder until the clear
has been recorded; if the backend cannot preserve that order, it rejects the
bind. WebGPU queues the zero write before the command buffer on the same queue.
All three retain the snapshot, latch, report slot and lease through actual queue
completion and readback. Missing capacity, clear payload, ordering proof or
readback payload rejects recording/submission and cannot produce `Clean`.

Immutable snapshot metadata is authored before publication in host-writable
`CpuToGpu` storage and uploaded through `GpuDevice::writeBuffer` during seal.
The sealed descriptor references that same storage; post-seal
`GpuDevice::writeBuffer` is rejected on all four backends. Transfer writes and
writable aliases are rejected by the public RHI recording paths. The
fault latch remains separate GPU-owned storage, with per-submission GPU clear
and completion-gated readback required for executable backends.
Seal rechecks the owning extension's snapshot issuance decision and execution
mode; the L2 publication path does not infer permission from a backend name.
Typed ordinary buffer writes require the matching usage and a nonzero extent
within the queried device binding-size limit before entering the descriptor
set. Raw ordinary writes still require complete access declarations at record.
The compiler artifact sets `faultReportingContractVersion=1` only when the
generated helpers cover invalid accesses with the reserved latch. A package
without the latch reports zero. The loader checks this against the retained
lowering bytes and the latch binding. Separately, a real device must report
`faultReadbackContractVersion=1` only after it owns per-submission zeroing,
GPU-completion readback and validation-frame failure. Both versions and an
executable payload are required by the planner. Public GPU snapshot issuance
remains closed while backend readback capability is zero.

`BindlessFaultValidationStatus` now gives validation frames a fail-closed L2
result: Pending, Faulted, ReadbackFailed and Abandoned are failures; only Clean
passes. The backend-private Vulkan command probe records the shader-write to
transfer barrier and four-byte readback, retains both native buffers, and
resolves the result only after its submission fence succeeds. A timed-out wait
leaves it Pending. Discarding an unsubmitted command marks it Abandoned. This
probe rejects missing capacity, invalid buffers and buffers from another Vulkan
device. Vulkan's private command can clear the four-byte latch with a transfer
fill and a transfer-write to shader-read-write dependency; it rejects a missing
CopyDst usage, foreign device or invalid prior state before recording. This is
not yet connected to the public snapshot submission lease. The completion
regression also holds a fault readback Pending behind a
real timeline wait and resolves it only after the fence completes. Native
snapshot issuance, completion-gated readback enforcement and a proven
per-submission latch lifetime remain absent.

`BindlessCommandBufferExtension/1` exposes that result from a finished command
buffer by immutable snapshot generation. The report carries the generation,
status and fault bits; polling is non-blocking. A matching GPU submission starts
Pending and may become Clean or Faulted only after its real completion and
readback. Readback failure and abandoned recording stay failures. Unknown
generations return Unavailable, and Null returns NotExecuted for a retained
validator snapshot: Null validation must never synthesize GPU Clean. Backends
without an authorized execution/readback path do not expose this command-buffer
extension. This result surface does not itself enable snapshot issuance.

The current Vulkan private fixture first reads a nonzero fault, then clears and
reuses the same latch in a second submission after the first fence. Its second
fence reports `Clean` with zero bits. The clear's native resource retention has
a fixed command-creation capacity; overflow, wrong usage/state and foreign
device reject before recording. RenderDoc replay of
`build/win-vulkan-debug/artifacts/bindless-vulkan/clear_capture_capture.rdc`
matches shader hashes, color/depth output and an empty validation-message list.
This does not prove public snapshot lease consumption or Metal/WebGPU execution.

Failure returns stable RHI-BINDLESS-* diagnostics. Auto may select a validated,
same-quality traditional path BEFORE recording. Required rejects; Disabled uses
traditional bindings. Execution failure never silently changes the selected
mode. Recovery requires a new validated candidate at a lifecycle boundary.

## Verification

Each stage retains a fast assertion-enabled test and Null hidden-frame smoke.
Backend validation requires real graphics/compute resource read/write results;
Null and fixtures prove validation only. WebGPU acceptance includes a real WOCC
multi-material indirect scene, not a fallback success. Visual gates retain
RenderDoc Vulkan captures and Metal/browser native readback evidence. Windows
builds use MinGW. Contract Complete, Prototype Complete, Backend Validated,
Platform Validated and Production Ready are independent statuses.

## Current implementation evidence (2026-09-29)

The L2 planner validates explicit opt-in, device/package identities, whole-domain
per-stage limits, external-domain reservations, RO/RW shared storage budgets,
capacity/mapping collisions and snapshot/update/switch budgets. This is a contract
planner, not backend authorization. BindlessSnapshot currently accepts Null
Validator only, with immutable publication, generation checks and ordinary RHI
descriptor liveness. Null exposes a per-device extension identity; public
capability structs cannot authorize snapshot creation. Its versioned validator
ceilings are 4 groups, 4096 bindings and 4096 resources per stage/class (RO/RW
storage buffers share a ceiling). These are explicit Null validation ceilings,
not queried native GPU limits.

The snapshot pool bounds simultaneous candidates/live snapshots and declared
byte charges using core capacity leases; each successful rewrite also consumes
the update budget. Seal requires the owning device, rejects a foreign device,
and submits the complete metadata words through its buffer-write contract before
binding the reserved descriptor. The candidate also checks backend-reported
internal buffer allocation bytes against its declared snapshot charge. Declared
byte accounting in this Null prototype is not a measurement of native descriptor
pool allocations. Typed buffer/view writes capture real parent
IDs, extents, stages and conservative operation accesses. Record validation
rejects missing reads/writes, insufficient ranges, stale generations and different
plans/packages. Pass encoders consume these checks; RenderGraph forwards the
exact declared resource ranges/stages/accesses, while the checks do not themselves
emit barriers. At this contract checkpoint, native factories were closed pending
execution tickets and completion-budget integration; the later Vulkan and
WebGPU increments below supersede that status only for their tested allowlists.
Smaller workloads now
require `SnapshotActivePrefix/2` for compiled packages, and candidate snapshots can explicitly alias
inactive slots to a live active entry. Missing initialization rejects; inactive
slots do not obtain public handles. The active counts still need packaging with
runtime material/instance records before a native snapshot can be authorized.

Finite variant selection lives in `rhi::semantics`. Auto/Required/Disabled are
tested at the planner level, including deterministic smallest-capacity selection.
This is not yet a WOCC runtime setting.

The typed Slang generator currently covers float4 sampled Texture2D, ordinary
samplers, r32float read/write storage Texture2D, float4 uniform entries and uint
storage buffers. Buffer lengths are explicit snapshot range metadata inputs.
Implicit sampling and comparison sampling are rejected. Native source compiles
to SPIR-V/MSL with the existing Metal argument-buffer lowering; Emulated source
also compiles to WGSL. WGSL atomics reuse the existing validated atomic-storage
lowering for the closed generated schema. Arp's offline compiler accepts explicit
`--bindless-variant`, `--bindless-source`, repeated `--bindless-entry` and
`--bindless-mode Native|Emulated`. It verifies target stage visibility with
SPIRV-Reflect, emits per-entry reflection using the existing authoritative
helpers, and publishes a manifest only after all targets validate. Ordinary
bindings, full physical maps, operations/capacities, row-major compilation,
compiler/helper versions and artifact hashes participate in package identity.
Failed replacement invalidates the old manifest. Authored preprocessor directives,
push constants, direct references to generated physical tables and authored atomics
outside generated helpers are rejected in this initial closed ABI. Comment
stripping reuses the authoritative compiler helper; this rejection guard does not
claim general Slang AST or operation analysis.

Per-entry reflection carries BindlessCross identity into runtime reflection and
pipeline-cache hashing. Ordinary pipeline creation rejects these tagged packages
until device-owned execution authorization is implemented. The manifest is not
yet an executable shader-package ticket. Emulated never invokes the legacy
implicit-sampling rewrite. All browser device limits now come exclusively from
the actual created device, not adapter limits or `Module.webGpuLimits`.

The standalone real Chrome/WebGPU probe on an NVIDIA Ampere adapter passed
selected-branch reads/writes/atomic add, invalid table/element/texel fault output,
independent sampler/texture indices and 2x2 fragment pixel readback. Two mips and
distinct nearest/linear samplers exercise explicit LOD, trilinear interpolation
and explicit gradients against a CPU reference (2/255 channel tolerance); an
LOD=0 substitution changes the expected image. Evidence lives at
`build/artifacts/bindless-webgpu/report.json`. This probe bypasses the RHI and
does NOT establish RHI backend integration, WOCC scene readiness, derivative
equivalence for arbitrary shaders, or full format support. rgba32float read-write
storage was rejected by the actual device; the fixture uses explicit r32float.

Reproduce with `python tests/contract/bindless_webgpu_smoke.py --slangc <slangc.exe>
--browser <chrome.exe> --output build/artifacts/bindless-webgpu`.

The native Vulkan completion probe uses a real timeline semaphore to block a
submitted command. Deadline expiry keeps resources and core capacity leases
alive; successful fence waiting/polling releases them while the CPU wrapper is
still alive. This validates that completion primitive, not BindlessCross native
pipeline execution or bounded device shutdown. The same absolute deadline is
reused for retries. Test: `arp_rhi_vulkan_completion_tests` (MinGW, assertions on).

The WebGPU shader probe additionally verifies inactive resource indices and an
oversized active count. The compute readback is `[16,27,15,26,1,7,31]`; the last
entry is an expected fault mask and the storage resources remain unchanged by
the rejected writes/atomics. This remains standalone shader evidence.

`BindlessShaderPackage` now loads and retains compiler publications, validating
byte identity, table/physical coverage, closed buffer ABI, sampler combinations,
module metadata and per-entry stages within explicit input budgets. The real
Slang package test invokes `arp_rhi_bindless_package_tests` on Native and Emulated
outputs, exercises mutation/missing-file/budget failures and creates full/partial
six-class Null snapshots from both ABIs. Null results remain non-executable;
ordinary runtime pipeline authorization is still closed on every backend. Null
now has a separate device-authorized compute validator factory and pass binding
gate; no GPU backend is authorized by this validator implementation.
Physical target mapping authorization and parser resident-allocation accounting
remain required before production integration.

The Null compute command path retains authorized package/pipeline state and
immutable snapshots in explicit preparation-time capacities, rejects stale or
foreign tickets and validates resource liveness at submission. Contract tests
use both real compiled ABIs with complete/partial tables, verify pool occupancy
until command destruction, and check 64 successful set/bind/dispatch iterations
without new core allocations. This is not native GPU lifetime evidence. Graphics direct-draw authorization is now implemented as a restricted Null
validator (single sample/subpass, explicit attachments, color or depth-only).
Indirect authorization, native storage retention and graph integration remain
open. Package/authorization passkeys are non-aggregate in C++17, with compile-time
negative tests preventing external empty-brace construction.


Graphics/compute validator pipelines share retained package construction,
reflection composition and command retention capacities. The real compiled
package fixture includes vertex storage-buffer access, fragment sampled-resource
access and compute storage operations. Native and scalar packages pass full and
partial snapshot recording, depth-only/color state, cross-device/stale-reference
rejection and 64 allocation-free direct recording iterations. Unsupported
indirect, bundle, secondary and subpass commands reject before execution. This
is Null validation evidence only; attachment storage liveness, general vertex
input validation, full stage I/O reflection and real backend payloads remain
required for executable graphics authorization.


Vulkan prerequisite evidence: assertion-enabled tests now verify both real
query/device-enablement and real descriptor-owned buffer lifetime on RTX 3050.
The latter uses a two-element native descriptor array, delayed timeline submission,
GPU buffer copy and exact four-word readback after fence completion; it does not
execute a descriptor-indexed shader. Production BindlessCross stays disabled until
retained-package pipeline factories, feature/limit validation, bounded snapshots
and real compute/graphics execution tests are connected.

Native compute preparation consumes a sealed `BindlessShaderPackage`, a named
compute entry and retained SPIR-V bytes; it cannot reopen paths or accept an
Emulated package. Vulkan checks reported enabled indexing, full physical layout
per-stage/per-pipeline descriptor ceilings, buffer limits, compute workgroup
limits and storage-format support before allocating pipeline state. The initial
package ABI contains one complete binding domain; undeclared extra domains
reject. Native preparation alone does not issue a public execution authorization:
snapshot/command budgeting and the device-owned extension remain separate gates.
Null validates the same package; Metal/WebGPU retain DiagnosticFailure for this
native target and will consume their own MSL/scalar WGSL implementations.

Native graphics preparation uses the same retained-package entry composition as
Null. Authored state cannot replace reflection, layout or entry interfaces. The
initial Vulkan preparation supports explicit single-sample single-subpass color
draws with vertex/fragment entries and depth-only draws with one vertex entry.
It validates the full descriptor domain and stage features before creating modules
from retained bytes. A missing fragment on a color pipeline, unsupported state or
format rejects with a stable diagnostic. Depth attachment format support is
queried before creating a native pipeline. This backend-private
preparation does not authorize public recording; Null remains validation-only,
and Metal/WebGPU graphics execution remains closed until its own payload is tested.

RTX 3050 native compute/fragment evidence: the shared fixture now uses eight
threads per workgroup (non-uniform indices within a group), two independently
indexed textures/samplers and two mip levels. Real buffer/storage results are
`[16,27,15,26,1,7,31]` / `[1,5]`; four sampled RGBA8 pixels match the real browser
Emulated probe byte-for-byte and its CPU reference within 2/255. RenderDoc replay
of `build/win-vulkan-debug/artifacts/bindless-vulkan/native_capture.rdc` verified
one dispatch, one draw, the expected entries and exact pixels, with no captured
validation messages. `capture-report.json` and `indexed-sampling.png` accompany
the capture. This is Backend Validated evidence for the private fixture payload,
not public BindlessCross, WOCC, arbitrary formats or Platform/Production Ready.

The depth-only follow-up creates exactly one Vulkan shader module for the retained
vertex entry, records a 2x2 D32Float pass, and copies its depth aspect after a
real fence. Both full and partial fixture snapshots read back four zero-depth
pixels against an explicit clear of 1. RenderDoc replay of
`build/win-vulkan-debug/artifacts/bindless-vulkan/depth_capture_capture.rdc`
finds one compute dispatch, one color draw and one depth-only draw, matches the
retained SPIR-V hashes, reads the same depth pixels and reports no validation
messages. `capture-report.json` records the inspection. This extends only the
backend-private Vulkan fixture evidence; public snapshot and command authorization
remain closed.

RenderGraph prerequisite: `FrameGraphResourceAccess/1` now preserves explicit
resource IDs, byte/mip/layer ranges, stages and read/write masks through compilation.
Both execution paths consume complete masks, and same-state hazards emit memory
dependencies. Vulkan image transitions no longer discard explicit stages/access.
The graph uses conservative whole-resource synchronization and carries access
unions across frame replay. Scope/range tests and real Vulkan RenderDoc barrier/
clear evidence pass. Binding these declarations to indexed pass authorization,
persistent buffer imports and snapshot-driven resource enumeration remain open;
this does not promote the public extension or any WOCC workload.

Package identity is independent of temporary/output directories: generated target
inputs use a stable virtual Slang source name; sealed reflection uses relative
source/target names. Native and Emulated cross-directory rebuild tests compare
the entire package identity and revalidate retained bytes. MSL argument-buffer
structs are inserted immediately before the first consuming entry, after its
required pointee definitions. Compiler boundary tests cover uniforms whose types
appear between entries; this does not replace native Metal compiler validation.

Snapshot metadata increment: the sealed compiler package records a reserved RO
storage binding, stage union, word count and per-table offsets. The Null snapshot
builds immutable words from validated typed writes, initializes its reserved
binding itself, rejects application overrides, and requires the metadata resource
in record access declarations. Planner identity includes the complete ABI and
budgets its descriptor/stage/byte use. Old compiled /1 packages reject; handwritten
Null validator artifacts retain their explicitly non-executing compatibility.
The metadata does not yet prove matching GPU material/instance generation or
mandatory fault collection: public GPU issuance remains closed.

Published metadata is now GPU-write sealed in L2. All four backend RHI
recording paths reject transfer destinations and writable storage-buffer
aliases with `RHI-BUFFER-GPU-WRITES-SEALED`; the fault latch rejects generic
transfer writes but remains writable under its separate lease. Null negative
tests cover copy/upload rejection and
the writable control buffer. Metal/WebGPU compilation is not native execution
evidence, and this invariant alone does not enable the public GPU extension.

Snapshot creation now asks the device-owned BindlessCross extension for an
explicit factory authorization. The base extension fails closed; only Null's
non-executable validator currently permits creation. The fault latch is
`GpuOnly` and has no host-side zero initialization. An executable backend must
clear it under the per-snapshot submission lease before any indexed operation,
then collect the result after actual GPU completion. The private Vulkan clear
fixture does not issue this public factory authorization.

Actual Native/Emulated fixtures consume the new bounds. Full and partial tables
pass; the partial fixture leaves invalid-slot counters and storage textures
untouched. Browser negative generation-zero metadata rejects every access with
readable fault output. Slang SPIR-V/MSL/WGSL compilation, package mutations and
Null authorization tests pass. MSL is source output, not macOS execution.

The WebGPU private compute fixture now binds the retained Emulated package
through the backend's compact descriptor group and command recorder. A sealed
full-package descriptor payload may contain bindings from other entry stages;
the recorder selects only bindings in the current pipeline lowering and
rejects any missing active binding before native group creation. Browser
dispatch and completion readback pass. The public BindlessCross factory and
authorization hooks still reject executable WebGPU snapshots; this fixture
does not establish snapshot-owned fault collection or WOCC support.

Private WebGPU graphics preparation combines sealed vertex and fragment
reflection, checks shared scalar mappings, and selects only active bindings
for one native render pipeline. The current native fixture authorizes only an
explicit single-sample RGBA8 direct draw with no depth or vertex streams.
Chrome readback of fragment-indexed storage buffers yields four expected
pixels and a clean fault latch after queue completion. This is not a public
BindlessCross graphics capability or WOCC material/shadow evidence.

The backend-private Chrome graphics fixture also binds two sampled textures,
two independently indexed samplers, immutable metadata and a fault sink from
the same retained package. Both texture mip uploads use `queue.writeTexture`
with the source byte pitch; native buffer copies retain their separate
256-byte row-pitch constraint. Four rendered pixels and a clean fault latch
are read after queue completion with no WebGPU validation errors. This does
not issue a public GPU snapshot or validate a WOCC material workload.

WebGPU's private native command payload now registers a four-byte fault-latch
clear and records a copy into a backend-owned map-read buffer after the pass.
At submit it queues the zero write before the command buffer, so a snapshot may
be selected inside the pass while the clear still precedes indexed GPU work.
The per-command count is fixed at creation. A clear without a matching
readback rejects `finish`; readback before clear rejects; an unsubmitted command
marks its pending result `Abandoned`. The async map callback alone publishes
`Clean`, `Faulted`, or `ReadbackFailed` after a successful queue-completion
callback. Failed queue completion also yields `ReadbackFailed`; pending results
fail validation. Callback registration's future ID is not used to free state:
Emdawn can deliver a deferred callback with ID zero. Public use still needs
the snapshot lease to bound outstanding callback state. The
Chrome compute invalid-index probe returns `Faulted(1)`, while buffer and
sampled graphics return `Clean(0)`, with no native validation errors. This is
still a backend-private payload: the fixture does not hold
`BindlessFaultSubmissionLease` from a public snapshot and therefore cannot
authorize public WebGPU issuance.

The backend now has a snapshot-typed fault path alongside that private fixture:
it acquires `BindlessFaultSubmissionLease` before registering clear, derives the
native latch only from the sealed snapshot, and transfers the lease into the
queue/map callback state when scheduling readback. Mixed leased-clear/raw-readback
commands reject `finish`; abandonment releases the lease. This code compiles on
Emscripten and L2 lease lifetime tests pass, but no public WebGPU snapshot can
be issued yet, so the typed path has no positive GPU execution evidence and
does not change capability or authorization status.

Fault recording now checks the native buffer's queried size/usage and its
backend-owned device identity before registering clear or encoding readback. The browser
fixture rejects a mismatched owner at record time and accepts a fault buffer
created by the normal WebGPU native resource factory in a separate command.

Device discovery increment (2026-09-29): Vulkan, Metal and WebGPU now return a
device-owned `BindlessCross/1` diagnostic extension, as Null already does. Each
diagnostic instance has a distinct device identity, reports
`DiagnosticFailure`, and leaves queried limits, resource-operation support,
executable payload and fault readback unavailable. Planning without opt-in
rejects with `RHI-BINDLESS-OPT-IN-REQUIRED`; opt-in still rejects with
`RHI-BINDLESS-EXECUTION-UNAVAILABLE`. Device discovery never issues a snapshot
or authorizes a pipeline. Assertion-enabled device contract tests cover all
three GPU backends; the Null hidden-frame validator is unchanged. This is a
public diagnostic surface, not a Native or Emulated capability claim.

Vulkan planning-facts increment: on a real runtime with verified descriptor-
indexing feature enablement, the diagnostic extension publishes the queried
per-stage descriptor ceilings, bound-set count and uniform/storage buffer
ranges. Synthetic or unverifiable runtimes leave `limitsQueried=false`. The
extension still reports `DiagnosticFailure`, no operations, no executable
payload and no fault-readback contract. A real headless Vulkan assertion test
compares every published limit against the native query; the synthetic device
contract confirms no inferred limits. These facts do not authorize a plan or
pipeline until the native snapshot, command and completion path is connected.

Headless Vulkan shutdown increment (2026-09-29): tracked native submissions
can now drain through their real fences against one absolute deadline. Timeout
preserves the pending command in the submission owner; a later successful
drain retires it. A two-submission fixture verifies that a completed command
retires independently of a later timed-out one. A native headless RHI device
reports `Native/Completed` after
the drain, while synthetic and surface-backed devices fail closed. This does
not prove the last `vkQueuePresentKHR` has finished consuming its semaphore;
windowed WOCC shutdown and failed-drain teardown remain open.

Vulkan timeline capability correction (2026-09-29): the platform enables the
queried Vulkan 1.2 `timelineSemaphore` feature, reports that enablement to the
RHI runtime, and derives its fallback contract from the enabled device rather
than a fixed `true` value. The runtime checks both native support and the
enablement report; public timeline creation fails closed without them. The
real headless completion fixture supplies a queried per-device contract and
asserts that the same device reports TimelineSync before creating, signaling
and reading a native timeline semaphore. Assertion-enabled MinGW completion
and Vulkan device contract tests pass. This does not authorize BindlessCross.
The public timeline factory also requires that executable queried contract;
the same real runtime paired with the static Vulkan vocabulary reports false
and rejects timeline creation. A synthetic platform snapshot is no longer
marked as queried or filled with guessed Vulkan feature/limit support.

Windowed failed-drain evidence (2026-09-29): the Windows WOCC child-process
fixture submits a real timeline-blocked Vulkan command after a production
frame's present callback, then requests shutdown without releasing the gate.
Its assertion-enabled CTest passes: the child exits 70 with
`WOCC-SHUTDOWN-GPU-DRAIN-DEADLINE-EXPIRED`, `gpuAttempted=1 gpuCompleted=0`,
`resourcesReaped=0` and an observed retirement queue, without a Vulkan
validation error. This establishes a fail-closed pending-GPU teardown case;
forced `vkQueuePresentKHR` and swapchain-replacement failure tests remain open.

Vulkan fault-latch lifetime increment (2026-09-29): private native command
payloads now expose snapshot-typed clear/readback entry points. The clear path
acquires the one-in-flight `BindlessFaultSubmissionLease`; the command retains
the snapshot and lease through native fence completion and fault readback, and
`end()` rejects a typed clear that has no paired readback. A repeated submit of
the same snapshot is rejected by the shared L2 lease budget. The public
snapshot/pipeline/pass issuance gate is still diagnostic-only, so this typed
Vulkan route is compile-validated but has no positive native execution test and
does not promote capability status. The existing raw-buffer Vulkan fixture is
unchanged and remains test-only evidence.

WebGPU planning-limit increment (2026-09-29): the WebGPU `BindlessCross/1`
diagnostic extension now copies only non-zero limits queried from the already
created browser `GPUDevice`; adapter request limits and incomplete query results
do not set `limitsQueried`. The extension still publishes no supported resource
operations, executable payload or Emulated execution. The Emscripten Chrome
RHI fixture creates a device extension and compares all stage resource limits,
group/binding ceilings and buffer ranges to `GPUDevice.limits`; it also asserts
execution remains `DiagnosticFailure`. This improves planning facts only and
does not authorize snapshots or WOCC workloads.

WebGPU operation allowlist correction (2026-09-29): the queried device now
publishes the shader-side operation subset exercised by the bounded scalar
expansion fixtures: fragment sampled-texture load/query/sampleLevel/sampleGrad,
fragment read-only storage-buffer load, compute sampled-texture load/query,
storage-texture load/query/store, uniform-buffer load, read-only storage-buffer
load/query, and writable storage-buffer load/query/store/atomic. Vertex stage,
sample comparison, and untested resource/stage combinations remain unsupported.
The browser fixture compares this exact allowlist and non-uniform-indexing bits.
This supersedes the preceding statement that the operation table was empty;
`execution` remains `DiagnosticFailure`, with no public snapshot, authorization,
pipeline/pass binding or completion/fault-readback issuance. Operation
capability is not executable Emulated support.

RenderGraph access-bridge evidence (2026-09-29): an assertion-enabled MinGW
integration test verifies that `RenderContext::bindSnapshot` forwards exact
texture mip range, buffer byte range, shader stage, state and read/write access
from explicit `PassBuilder::access` declarations. This test exercises the Null
FrameGraph path only; GPU backend barriers, completion retention and WOCC
bindless execution remain unverified.

Vulkan fault-report surface increment (2026-09-30): finished Vulkan command
buffers now expose `BindlessCommandBufferExtension/1`. The backend keeps a
bounded generation-to-readback result entry for each snapshot whose typed fault
readback was recorded; the result remains `Pending` until the existing native
fence completion path resolves it, and unknown generations report
`Unavailable`. This is reporting plumbing only: Vulkan snapshot issuance,
pipeline/pass authorization and public GPU execution remain closed. An extra
empty RHI command encoder in the completion fixture exposed an existing
reusable-command-resource teardown crash at process exit, so that test shape
was removed; the normal completion, L2 contract and Null hidden-frame tests
pass, but the new generation mapping lacks an end-to-end public recording test.

Vulkan fault-clear preamble increment (2026-09-30): the backend-private clear
path now records a four-byte zero fill and explicit transfer-to-shader
visibility barriers in a bounded per-command preamble allocated from the
workload's command pool. Submission orders `[preamble, workload]` in one
`VkSubmitInfo`; the existing fence and asynchronous retirement therefore own
both command buffers and the snapshot/latch leases through completion. The
private real Vulkan compute fixture validates fault-bit readback and a cleared
latch readback, and the completion fixture validates that results remain
pending until its real fence completes. The completion fixture now drops the
caller command reference while a timeline wait is pending; asynchronous
retirement keeps the command and latch alive and only releases them after
completion/readback. This does not yet exercise an
authorized public `BindlessSnapshot`/pipeline/pass recording flow, does not
prove preamble pool reuse under device loss, and does not promote Vulkan
capability. Metal, WebGPU, renderer/WOCC integration and P0 teardown validation
remain open.

Pipeline authorization transport increment (2026-09-30): the L2 render and
compute pipeline interfaces now expose an opaque authorization ticket;
ordinary implementations default to empty, and the Null bindless pipeline
returns the exact issuer ticket used to create it. The package runtime verifier
asserts ticket identity through pipeline creation. This closes the cross-backend
metadata transport gap only; Vulkan compute now consumes the ticket through its
public pass and completion report. Metal and WebGPU factories and pass encoders
remain closed.

Vulkan public BindlessCross compute increment (2026-09-30): a real RTX 3050
MinGW fixture now consumes the selected Native plan through snapshot creation,
package authorization, retained compute-pipeline creation and the public
compute-pass binding extension. The submitted shader covers storage-texture
load/query/store, uniform-buffer load, read-only storage-buffer load/query,
writable storage-buffer load/query/store/atomic, and sampled-texture load/query.
After the real fence, the command-buffer extension reports the injected
out-of-range access as Faulted and the separate counter readback matches 16,27
(or 16 for the partial snapshot). The public package is generated from only
the Compute entry; the broader package remains for backend-private graphics
evidence. At this checkpoint Vulkan capability publication was restricted to
this tested Compute allowlist. The Vulkan Graphics increment below adds its
separately tested Fragment sampling subset; Metal, remaining WebGPU operations,
WOCC P4-P6, and platform visual/performance gates remain pending.

WebGPU public Emulated increment (2026-09-30): the Emscripten/Chrome fixture
loads a compute-only Emulated package and executes non-uniform storage-buffer
`load`, `query`, `store`, and `atomicAdd`, plus read-only storage-buffer
`load/query`, through device plan selection, immutable
snapshot creation, package authorization, public pipeline/pass recording, queue
submission and completion. The Clean command-buffer fault report accompanies
readback of final counters 11,21, loaded and atomic prior values 10,10 and 20,20,
and each one-element binding's query result 1. The fixture rejects missing
opt-in, disabled Emulated selection, ordinary descriptor bypass, plan tampering
and a missing fault-readback access declaration. Capability publication remains
limited to these six Compute storage-buffer operations. This is Backend
Validated only for the bounded operations and tested Chrome runtime. Metal,
other WebGPU operations, WOCC integration and platform/performance gates remain
pending.

WebGPU sampled-texture increment (2026-09-30): the public Emulated Compute
fixture now contains two real `Rgba8Unorm` textures uploaded through the RHI
transfer path and retained as typed entries in one immutable snapshot. The
pass declares both complete texture subresource ranges as Compute shader reads.
Non-uniform `SampledTexture.load/query` returns per-index red-channel values
`255,0` and width `2` for both views, while the same dispatch preserves all
storage-buffer results. The queue-completion report is Clean and Chrome reports
no WebGPU validation errors. MinGW assertion-enabled device/bindless contracts,
Slang codegen/package validation, and the Chrome RHI smoke pass. WebGPU's
published Compute allowlist now includes only sampled-texture `load/query`,
read-only storage-buffer `load/query`, and read-write storage-buffer
`load/query/store/atomicAdd`. At this checkpoint sampling/samplers were not yet
validated. That fixture also did not cover storage-texture operations beyond
those listed below, uniform buffers, Metal, WOCC, graphics stages, or platform
and production readiness.

WebGPU storage-texture increment (2026-09-30): the public Compute package
binds two typed `R32Float` storage-texture views and invokes non-uniform
`query/store`. The pass declares the full subresource ranges with conservative
shader read/write access; a following transfer copies both textures to a
separately mapped buffer after dispatch completion. Chrome readback confirms
`0.5` and `1.5`, while buffer and sampled-texture results remain correct and
the snapshot fault report stays Clean. The generated physical layout uses the
contract's ReadWrite storage-texture ABI. The following StorageTexture load
increment supersedes the original `query/store`-only allowlist. At that
checkpoint uniform buffers, sampler operations, Metal, WOCC and platform
acceptance remained unvalidated.

WebGPU storage-texture load increment (2026-09-30): the public Compute fixture
uploads `0.25` and `0.75` into two `R32Float` storage textures through the RHI
transfer path, then performs non-uniform `load/query/store` through one
immutable snapshot. Buffer readback confirms the loaded values and texture
readback confirms the subsequent stores (`0.5` and `1.5`). The declared
subresource access remains conservative shader read/write, queue completion
produces a Clean fault report, and Chrome reports no native validation errors.
The published WebGPU Emulated Compute StorageTexture allowlist now includes
`load/query/store` for this tested ReadWrite ABI. At this checkpoint sampler
operations were still unvalidated; the following independent-sampler increment
supersedes that portion. Other storage formats/stages, Metal, WOCC and platform
acceptance remain open.

WebGPU independent-sampler increment (2026-09-30): the public Compute package
declares two sampled textures and a separate two-entry sampler table. Dynamic
texture and sampler indices select different pairs in one `sampleLevel`
operation. Chrome readback verifies the linear-filtered red/blue midpoint and
the independently selected nearest-filtered green texel, alongside the prior
texture loads, storage-texture load/store, storage-buffer operations and Clean
fault report. The finite package/planner switch budget is 32 and accounts for
the 26 generated operation/index combinations; the physical layout reserves 13
bindings. The WebGPU Compute allowlist now includes `sampleLevel` and
non-uniform sampler-table indexing. Implicit-gradient and comparison sampling,
other formats/stages, Metal, WOCC and platform acceptance remain unvalidated.

WebGPU uniform-buffer increment (2026-09-30): the public Compute package binds
two 16-byte `UniformBuffer` entries with the `Slang.float4.uniform/1` ABI and
performs non-uniform `Globals_load`. Chrome readback confirms `3.25` and `7.5`
from the two entries in the same dispatch as sampled texture, independent
sampler, storage texture and storage-buffer operations. Queue completion
produces a Clean fault report with no native validation errors. The published
WebGPU Compute allowlist now includes `UniformBuffer.load` for this ABI. Other
uniform layouts/stages, Metal, WOCC and platform acceptance remain unvalidated.

Metal fail-closed contract increment (2026-09-30): assertion-enabled MinGW
tests confirm that enabling ordinary `MetalArgumentBuffer/1` does not publish
BindlessCross resource/stage operations, queried bindless limits, an executable
payload or fault-readback contract. Metal render and compute passes also keep
the BindlessCross pass extension unavailable. These checks exercise the Windows
diagnostic stub and shared contract gates only; they are not native Metal
execution evidence. Metal snapshot authorization, argument-buffer snapshot
payloads, pass binding/access validation, fault clear/readback and
completion-gated fault collection remain unimplemented. Metal command payloads
now reserve bounded pipeline/snapshot retention from `CommandEncoderDesc`,
deduplicate retained identities, transfer these references into the native
completion closure, and release them on unsubmitted abandonment. Compute
`setPipeline` validates a live Native authorization and retains its ticket
before native pipeline binding; pass-level snapshot consumption is wired behind
the device capability gate, while device snapshot issuance remains disabled.
An Apple-only lifecycle test covers
bounded retention, deduplication and abandonment, but has not been run here. A Metal authorized
Compute pipeline factory now selects the entry from `BindlessShaderPackage`,
uses the verified module reflection for argument-buffer lowering, compiles the
retained MSL bytes, stores the issuer ticket on the pipeline object, and owns
the generated pipeline layout for the pipeline lifetime. The device keeps this
factory unreachable while capability and snapshot execution are
diagnostic-only. MinGW compiles the factory against diagnostic stubs; it does
not establish native Metal or Apple-platform validation. The assertion-enabled
MinGW bindless and Metal device contract tests pass 2/2 after the layout
ownership fix.

Metal feature probing now reports `argumentBuffersTier2` separately from the
argument-encoder substrate probe. Tier 2 is required input for native indexed
array evaluation; Tier 1 must not be promoted from ordinary
`MetalArgumentBuffer/1`. This bit alone does not publish BindlessCross limits or
execution, which remain closed pending per-workload limit validation, Apple
execution of resource residency, snapshot authorization and completion tests.

Metal argument-buffer arrays now declare their indirect buffer and texture
resources with `useResource` on the active render/compute encoder, including
cached argument-buffer binds. Uniform and read-only storage buffers declare
read use, sampled textures read/sample use, and writable storage resources
read/write use. The same declaration covers ordinary descriptor tables. MinGW
contracts verify this type classification, but cannot compile or execute the
Objective-C++ calls; this is not Metal Native validation.

Metal fault-submission plumbing increment (2026-09-30): the native command
payload now has bounded generation-keyed fault outcomes, acquires a snapshot
fault lease before recording a blit clear, schedules the latch-to-readback copy
after encoder completion, and resolves the mapped readback only from the native
command-buffer completion handler. Successful readback advances the snapshot
latch state; failed submit/readback and unsubmitted abandonment remain
non-success statuses and release their leases. The finished Metal command
buffer exposes the report interface, which returns `Unavailable` when no
generation was scheduled. MinGW builds the command-buffer interface and
diagnostic stub; bindless, Metal device and Null hidden-frame contracts pass
3/3. Metal capability remains fail-closed and P3 remains incomplete. A later
adjacent regression run exposed an outdated non-Apple staging-report
expectation and two native-only command checks running against the Windows
diagnostic stub; the fixture has since been corrected to compare the report to
the runtime boundary and skip native command checks when Metal is unavailable.
The assertion-enabled staging-upload, Metal device, BindlessCross, RenderGraph
and Null hidden-frame tests now pass 5/5 on MinGW. This confirms the diagnostic
boundary and platform-independent bounded-pool contracts only; Apple Metal
execution remains unverified.

Metal Compute pass wiring increment (2026-09-30): the pass now implements
`BindlessPassExtension` behind a capability gate sourced from the device's
`BindlessCapabilities`, requiring Native execution, queried limits, executable
payload, `BindlessFaultReadback/1`, and encoder-owned retention capacity. With
that gate open, `setPipeline` retains a live Native authorization; `bindSnapshot`
checks the same authorized plan/package identity, snapshot generation, Compute
access declarations and descriptor layout, records the latch clear and binds
the snapshot descriptor set. Ordinary descriptor binding and dispatch without
the authorized snapshot are rejected, and pass end schedules completion-gated
fault readback. Current Metal capability values still keep the extension
unavailable. MinGW rebuild and assertion-enabled Metal device, bindless,
RenderGraph and Null hidden-frame tests pass 4/4; the device contract confirms
unscheduled generations do not appear Clean. This compiles only the Windows
diagnostic stub and shared pass code; native argument-buffer execution and the
Objective-C++ fault commands remain unverified on Apple.

Cross-backend regression checkpoint (2026-09-30): the assertion-enabled MinGW
matrix passes 9/9 for the L2 BindlessCross contract, Slang multi-target
codegen, real Vulkan public Compute execution, Metal direct-binding/device/
runtime-hash/staging diagnostic contracts, RenderGraph access forwarding and
Null hidden-frame smoke. The Emscripten matrix passes 3/3 for the L2 contract,
the public WebGPU Emulated RHI browser fixture and Slang codegen. These runs
refresh only the documented Vulkan/WebGPU Compute allowlists and the Windows
Metal diagnostic boundary. Metal Native remains unvalidated; no WOCC renderer
workload, full-scene visual parity, performance gate, or platform-wide
readiness is established.

Vulkan public Graphics increment (2026-09-30): the device extension now creates
an authorized render pipeline from a retained Fragment/Vertex package and the
original L2 render-state descriptor. The public smoke uploads two sampled
textures, binds an immutable typed snapshot through `BindlessPassExtension`,
records a real direct draw, waits for a Vulkan fence, and checks output pixels
and a Clean generation fault report. Negative assertions cover missing
snapshot, plan and package identity mismatch, missing Fragment resource
declarations, descriptor bypass, and indexed indirect draw rejection. The
assertion-enabled MinGW CTest `arp_rhi_vulkan_bindless_compute_tests` passes on
an NVIDIA GeForce RTX 3050. The fragment table is limited to sampled-texture
`load/query/sampleLevel/sampleGrad` plus its compiler-declared sampler table;
Fragment capability publication also requires enabled
`fragmentStoresAndAtomics` for the fault-latch write. This establishes Backend
Validated for that Vulkan subset only. Metal Native, WebGPU graphics beyond the
Fragment sampled-texture `load/query` subset, Vulkan
Vertex resource indexing, indirect bindless execution, WOCC integration and
platform/performance validation remain open. RenderDoc capture and replay of
the public RHI draw succeeded at
`build/win-vulkan-debug/artifacts/bindless-vulkan-renderdoc-final/` on this
Windows host. Event 87 matches the public Fragment SPIR-V hash, the 2x2 output
readback is non-clear, and RenderDoc reports no Vulkan validation messages.
No WOCC readiness status is promoted.

Shared multi-entry shader-package regression check (2026-09-30): after the
canonical Slang source-path fix, the existing Emscripten public WebGPU RHI
browser smoke, L2 contract and codegen suite pass 3/3 in
`build/web-webgpu-emscripten-contract-v2`. The separate `web-webgpu-debug`
Chrome run still exits with `RuntimeError: unreachable` after recording texture
uploads and reports no native validation errors; it is not counted as success
or as evidence against the passing contract build.

Vulkan Vertex indexing increment (2026-09-30): Native Vulkan capability
publication now reports only Vertex sampled-texture `load/query`, subject to
enabled descriptor-indexing bits. The shared
Graphics pass derives active shader stages from the sealed snapshot layout and
requires the declared access set to cover each stage/range. L2 and package
validation now permit Vertex fault-latch visibility for Native plans; WebGPU
Emulated Vertex operations retain the explicit fault-provenance rejection.
The public Vulkan Graphics fixture adds a distinct VertexImages table whose
load/query result changes vertex position, then validates GPU pixels and a
Clean completion-gated fault report. A negative record check omits Vertex
stage coverage and rejects with `RHI-BINDLESS-ACCESS-SET-INCOMPLETE`. The
assertion-enabled MinGW Vulkan smoke passes on an NVIDIA GeForce RTX 3050.
This validates only Vulkan Vertex sampled-texture load/query plus the already
documented Fragment and Compute subsets; WebGPU validation covers only Fragment
sampled-texture `load/query`. Metal, other WebGPU graphics operations, indirect
execution, WOCC integration and platform acceptance remain pending.

WebGPU Fragment Graphics increment (2026-09-30): an authorized public RHI
render pipeline consumes a retained Emulated package with a capacity-2
sampled-texture table. The Fragment shader performs non-uniform `load/query`
through generated scalar switch bindings. Debug and Release Chrome runs record the public render
pass, reports a Clean snapshot generation after queue completion, and reads
red/green output pixels. The fixture also validates the public pipeline report
and render-target compatibility contract. Evidence:
`D:/arp/build/web-webgpu-emscripten-contract-v2/artifacts/bindless-webgpu-rhi-public-graphics/report.json`.
This opens only Fragment sampled-texture `load/query`; no WOCC renderer path,
Vertex bindless access, sampling operation, indirect draw, Metal execution, or
platform-wide readiness is implied.

WebGPU Fragment explicit-gradient follow-up (2026-09-30): the public Emulated
RHI fixture verifies Fragment `sampleLevel/sampleGrad` with independently
indexed samplers. The Slang shader computes `ddx/ddy` before index-dependent
control flow, and the operation remains explicit-gradient only; implicit
derivative rewriting and comparison sampling stay rejected. Emscripten Debug
public browser/contracts/codegen passes 3/3, Release contract/codegen passes
2/2, and assertion-enabled MinGW BindlessCross, Vulkan execution, Metal and
WebGPU device contracts, RenderGraph access forwarding, codegen, and Null smoke
pass 7/7. The Release Chrome report shows both public Compute and Graphics
success, Clean completion-gated fault status, deterministic readback and no
native errors at
`D:/arp/build/web-webgpu-release/artifacts/bindless-webgpu-rhi-fragment-samplegrad/report.json`.
This extends only the documented WebGPU Fragment sampled-texture operation
subset; Metal Native, other WebGPU stages/operations, indirect execution,
WOCC renderer integration and platform acceptance remain open.

WebGPU Fragment material-buffer increment (2026-09-30): the public Emulated
fixture adds a two-entry Fragment ReadOnlyStorageBuffer table using
`Slang.uint32.structured/1`. UV-derived non-uniform indices load a material
value and query its element count; incorrect values select a magenta diagnostic
pixel. The public pass declares both exact buffer ranges as FragmentShader read
accesses, retains the snapshot through queue completion, validates a Clean fault
report and reads back the expected purple/yellow sample result. WebGPU publishes
only Fragment ReadOnlyStorageBuffer `load/query`; writes, atomics, other ABIs,
graphics stages, Metal Native, indirect execution, WOCC integration and
platform readiness remain unavailable. MinGW cross-backend tests pass 7/7,
Emscripten Debug browser/contracts/codegen pass 3/3, Release contracts/codegen
pass 2/2, and both Debug CTest and Release Chrome fixtures pass. Release evidence is at
`D:/arp/build/web-webgpu-release/artifacts/bindless-webgpu-rhi-fragment-material-buffer/report.json`.

WebGPU Fragment uniform-factor increment (2026-09-30): a two-entry
`Slang.float4.uniform/1` table is independently indexed by Fragment and
multiplies the sampled material color. The finite variant records 18 switch
cases and declares both uniform buffer ranges as FragmentShader reads. Debug
and Release Chrome public RHI fixtures read back `64,0,128` and `255,128,0`,
report Clean completion-gated faults, and contain no native WebGPU errors.
MinGW WebGPU/bindless/Null contracts pass 3/3, and Debug Slang codegen passes.
Release evidence is at
`D:/arp/build/web-webgpu-release/artifacts/bindless-webgpu-rhi-fragment-uniform/report.json`.
This validates only Fragment `UniformBuffer.load` for the generated ABI and
bounded fixture; other layouts/stages, Metal Native, WOCC and platform gates
remain open.

WebGPU Fragment storage-buffer increment (2026-09-30): the public Emulated
graphics fixture selects two `Slang.uint32.structured/1` storage buffers with
non-uniform Fragment indices. Each pixel loads and queries its selected buffer,
performs `atomicAdd(1)`, then stores the distinct value `old + 2`; the final
completion-gated readback is `12,22` from initial values `10,20`. This separates
the observable store effect from the atomic effect. The pass declares both exact
buffer ranges as FragmentShader read/write accesses; replacing one with a
read-only declaration rejects with `RHI-BINDLESS-ACCESS-SET-INCOMPLETE`.
The successful pass checks Clean fault
status. Debug CTest and Release Chrome public RHI smoke pass; the Release report
has no native errors at
`D:/arp/build/web-webgpu-release/artifacts/bindless-webgpu-rhi-fragment-storage/report.json`.
MinGW assertion-enabled BindlessCross, Metal/WebGPU device contracts and Null
hidden-frame smoke pass 4/4; Debug and Release codegen/contract suites pass
2/2 each and regenerate SPIR-V, MSL, WGSL and reflection. This validates only
the named Fragment storage-buffer operations and ABI in the finite variant;
Metal Native, other layouts/stages, WOCC workloads and platform acceptance
remain open.

WebGPU single indexed-indirect execution increment (2026-09-30): the public
Emulated graphics pass now exercises one `DrawIndexedIndirectCommand` with
`firstInstance=1` on a created browser device that enabled the optional feature.
The native index-buffer bind occurs at the logical bind command, so direct and
indirect draws consume the same pass state. The fixture rejects an undeclared
args buffer, multi-draw and missing index buffer before recording the valid
draw. It resets the fragment shader's writable counter inputs between the
direct and indirect validation frames. The Slang vertex shader forwards
`SV_InstanceID` to Fragment: direct base zero reads `64,0,128` / `255,128,0`,
while indirect `firstInstance=1` reads `32,0,64` / `128,64,0`. Distinct
readbacks verify the indirect instance base alongside a Clean completion-gated
fault report and no browser validation errors. Evidence: `build/web-webgpu-release/artifacts/bindless-webgpu-rhi-indirect-fixed/report.json`.
This validates only the named browser/device, finite shader variant and one
indexed-indirect operation; Metal, zero-base mapping on devices lacking the
feature, WOCC workload and platform acceptance remain open.

The same public browser fixture also disables the optional feature at device
creation and asserts that the authorized pass rejects the draw at recording
with `RHI-BINDLESS-INDIRECT-FIRST-INSTANCE-UNAVAILABLE`. The separate report
is `build/web-webgpu-release/artifacts/bindless-webgpu-rhi-indirect-feature-disabled/report.json`;
its `publicRhiIndirectFramePassed=false` is expected, not a successful GPU
indirect frame. Both feature states are explicit browser CTest cases.

IndirectInstanceBase/1 producer evidence (2026-09-30): the public WebGPU
Compute package emits two different logical-base mappings and 20-byte physical
records. Completion-gated readback compares every field and first/last guarded
logical ID against the shared CPU encoder. A separate entry tests uint32
overflow and zero capacity; both records have zero instances and invalid
mappings, and the command report is Faulted with bits=1. This expected negative
probe cannot qualify as a successful application validation frame.
MinGW assertion-enabled L2/Null tests and Slang/codegen/package tests pass;
Release Chrome's enabled/disabled-feature CTests both pass. The reports expose
`publicRhiIndirectBaseEncodingPassed` and `publicRhiIndirectBaseFaultProbePassed`
independently at `build/web-webgpu-release/artifacts/bindless-webgpu-rhi-pipeline/report.json`
and `bindless-webgpu-rhi-indirect-feature-disabled/report.json`.
This proves producer encoding only. Public no-feature draws still reject until
their mapping binding, Vertex guards and indirect access/lifetime contracts
consume this same versioned ABI.

IndirectInstanceConsumer/1 execution evidence (2026-09-30): the public WebGPU
graphics package reserves scalar Vertex binding 0, record 0 and logical
capacity 2. A Compute entry uses the generated encoder to populate that same
16-byte buffer; exact mapping readback, explicit write-to-read transitions and
subsequent direct graphics verify logical instances 0 and 1. The declaration
validator rejects missing Vertex visibility, and snapshot preparation rejects
an insufficient or non-16-byte buffer extent. Six direct graphics probes cover
stale version, smaller/larger claimed capacity, overflowed base, zero count and
out-of-range local ID. Each probe reports Faulted/bits=1 after GPU completion,
leaves material counter values at 10/20, and returns a derivative diagnostic
color before any material access. Positive draws update those counters to
12/22 and produce the CPU-reference color with RGBA8 tolerance of one unit.
This is Backend Validated for this exact WebGPU consumer subset only.

Both Chrome Release configurations pass, including a created device without
`indirect-first-instance`. Reports expose `publicRhiIndirectBaseConsumerPassed`
and `publicRhiGpuMappingConsumerPassed` independently at
`build/web-webgpu-release/artifacts/bindless-webgpu-rhi-pipeline/report.json`
and `bindless-webgpu-rhi-indirect-feature-disabled/report.json`, together with
package identity, exact consumer layout, per-probe pixels/counters/faults and
zero native errors. Assertion-enabled MinGW L2, Null and three GPU backend
device contracts pass; compiler tests regenerate and validate Native/Emulated
target interfaces and reject consumer metadata tampering. Vulkan/Metal
consumer execution remains closed. No-feature indexed draws still reject:
these direct draws do not prove correspondence or ownership of arbitrary GPU
indirect arguments. Renderer, WOCC and P6 readiness remain pending.

Closed producer/protected-storage evidence (2026-09-30): the Native/Emulated
compiler suite regenerates the fixed producer's row-major targets and rejects
schema/entry ownership changes, forged application roles and rehashed target
substitution. Builds without shader tools preserve empty executable identities.
Null's packet fixture rejects host/transfer/writable-descriptor mutations,
malformed capacities/versions/records, quota excess and stale device epochs;
readonly descriptor liveness and retention after pool destruction are tested.
Six assertion-enabled MinGW CTests pass, including 18 compiler tests; final
forged-role/packet-record probes also pass. Both Release Chrome configurations
pass the existing named execution subsets with no native errors. Their enabled
and disabled-feature reports remain at
`build/web-webgpu-release/artifacts/bindless-webgpu-rhi-pipeline/report.json`
and `bindless-webgpu-rhi-indirect-feature-disabled/report.json`. The disabled
configuration still reports no rendered indirect frame. This is compiler and
Null storage validation only; trusted producer execution, protected GPU packet
issuance/retention and no-feature indexed drawing remain closed.

Protected packet candidate payload gate (2026-09-30): after complete
initialization and sealing, preparation requires the issuing extension to
validate the actual payload. Null validates same-device L2 ownership only;
WebGPU may accept registered native buffers with exact extents and usage,
host/descriptor/transfer write seals and complete allocation accounting.
Vulkan and Metal reject this payload gate until their implementations exist.
Native WGPUDevice equality cannot replace membership in the issuing RHI
device's resource registry. A rejected candidate releases count/byte leases.
Storage acceptance grants neither trusted production nor zero-base drawing.
All ordinary host writes must also validate that same registry membership.

Prepared producer commands reserve at least one distinct fault slot for each
producer slot before allocating their encoder. Recording rejects a second
producer sharing a previously retained producer's fault latch. Neither this
budget gate nor a prepared kernel grants public packet execution capability.

Packet recording ownership evidence (2026-09-30): Null verifies a sealed
packet-owned scalar mapping, fixed record/capacity, exact retained snapshot,
plan/generation and recording identity. Negative cases include a same-sized
unrelated mapping, a second valid snapshot, concurrent reservation, forged plan
fields with an unchanged digest, expired device epoch, invalid source word
range and incomplete producer declarations. Lease move/cancellation and owner
destruction retain both packet and snapshot quotas until their last recording
owner releases; successful acquire/validate/cancel loops allocate nothing.
These are ownership tests, not evidence of native in-flight completion.
Six assertion-enabled MinGW CTests (including the 18-test compiler suite),
the real Vulkan public compute/graphics/indirect readback smoke and both Chrome
Release feature configurations pass after the shared validators changed.
Reports remain at `build/win-vulkan-debug/artifacts/bindless-vulkan/report.json`
and the two WebGPU paths above. The no-feature WebGPU report still records
`publicRhiIndirectFramePassed=false`. All GPU packet-storage capabilities and
all four trusted producer execution capabilities remain zero. Prepared native
processor payloads and same-command producer/consumer consumption are next;
Metal Native, renderer, WOCC and P6 acceptance are pending.

Prepared WebGPU producer and protected-storage evidence (2026-09-30):
the canonical Emulated package runs five native GPU probes with exact uint32
parameters, signed vertex offset bits, all physical argument/mapping words and
fault bits compared against the shared L2 reference. Reports now persist those
values, including capacity 16777217, a culled record, logical range overflow,
source offset UINT32_MAX and zero capacity. Negative outputs suppress instances
and return Faulted rather than a successful validation frame. Recording is
core-allocation-free; insufficient fault/producer/submission capacity, aliased
producer fault sinks, missing clear/readback, overlapping producer ownership
and missing completion owner reject. Cancellation reacquires its bounded slot.
After successful submission, destroying public command/producer owners retains
the producer until the actual queue callback, then releases the slot.

The new core completion retention pool has assertion-enabled tests for capacity,
generation reuse, cancellation, submitted ownership, unknown-result quarantine,
closed admission, terminal invalidation, late tickets and zero successful-path
allocations. Chrome separately injects an unproven ownership outcome, verifies
native preparation/submission remain closed and the slot stays retained, then
calls the real device destroy API. The actual native lost promise closes and
releases the owner, even after its public reference was dropped. This injection
does not claim to reproduce a real failing GPU queue, and no CPU or fixture
flag provides the device-loss proof.

WebGPU's public packet-storage fixture validates the sealed 52-byte payload's
exact initialized GPU readback, per-RHI-device ownership despite a shared
native device, opt-in, byte/count quotas, surviving pool destruction, actual
RHI-device epoch expiry and
rejected ordinary host/descriptor/transfer writes. Readonly mapping descriptors
remain legal. Callers retain packet/descriptor owners through readback completion;
this fixture does not yet prove automatic packet retention by a trusted command.
Null and WebGPU publish storage version 1 only; Vulkan/Metal keep zero, and all
four public trusted-producer execution versions remain zero.

Six assertion-enabled MinGW L2/Null/backend/codegen CTests pass (18 compiler
tests), together with Vulkan's public compute/graphics/indirect native smoke
and both Release Chrome feature states. The current reports are
`build/win-vulkan-debug/artifacts/bindless-vulkan/report.json`,
`build/web-webgpu-release/artifacts/bindless-webgpu-rhi-pipeline/report.json`
and `bindless-webgpu-rhi-indirect-feature-disabled/report.json`. Both browser
reports record protected storage, five producer readbacks and loss-observer
recovery independently, with no native errors. The disabled-feature report
still has `publicRhiIndirectFramePassed=false`. This is scoped Backend Validated
storage/private-producer evidence, not protected public processing, zero-base
drawing, Metal Native, WOCC or Production Ready evidence. Next is the public
processor contract, private intermediate storage, consumer-only immutable
mapping package, same-command packet lease consumption and completion-owned
producer/consumer fault reports before any no-feature draw capability opens.

### Indirect processor intent preparation (2026-09-30)

`BindlessIndirectProcessorIntent/1` is immutable L2 preparation, not a native
processor or draw authorization. The bounded pool requires explicit opt-in and
retains the verified closed producer package, the logical source buffer, the
protected packet, the exact consumer snapshot/generation and its device-issued
package authorization. Successful-plan comparison includes the complete budget
and physical plan, not only its digest. The first version requires a graphics-only
consumer package; Compute entries or bindings reject. Its scalar Vertex reader
must name the packet's protected mapping with the exact capacity and extent.

Count/byte leases survive destruction of the pool. Accounting conservatively
charges retained source allocation, both verified packages, packet allocation
and the complete consumer snapshot ceiling, including shared owners. Admission
never grows or waits. A moved recording lease retains the complete intent and
excludes competing writers using the packet lease. Source/parameter Compute
reads, protected destination Transfer writes and consumer Vertex mapping and
physical indirect reads are validated in separate producer/consumer declaration
views. Snapshot validation also requires the complete material, metadata and
fault declarations. Successful recording validation allocates no core storage.

| Backend | Preparation decision | GPU producer/draw decision |
|---|---|---|
| Null | Validator; source belongs to the issuing RHI registry | No GPU execution; producer execution version stays zero |
| Vulkan | `RHI-BINDLESS-INDIRECT-PROCESSOR-PREPARATION-UNAVAILABLE` | No protected processor payload; existing native first-instance path is independent |
| Metal | Same deterministic preparation diagnostic | No protected processor payload |
| WebGPU | Native source extent/usage plus exact RHI registry validation; independently tested before publication | Public producer execution remains zero; no-feature indirect recording remains closed |

Null compilation/package/lease tests cannot establish GPU completion retention.
WebGPU preparation cannot establish trusted scratch writes, protected copies,
command identity consumption or producer/consumer completion-owned fault reports.
Those native payloads and their positive/negative GPU fixtures remain required
before any public zero-base execution capability is enabled.

The assertion-enabled compiler/package suite now contains 19 tests, with Native
and Emulated graphics-only consumer compilation, mixed-Compute rejection,
ownership/plan/generation/source/access/quota failures, pool-owner retirement and
allocation-free successful lease loops. Both Release Chrome device-feature
configurations report `publicRhiProcessorIntentPreparationPassed=true`, retain
the new consumer package identity and report no native errors. The no-feature
report still has `publicRhiIndirectFramePassed=false`. This proves the documented
preparation subset only. A shared WebGPU descriptor check now reads live scalar
registry facts rather than copying resource descriptions with long labels; its
parent-liveness, type/format/usage and write-seal validation remains required.

### Private protected packet production (2026-09-30)

`PreparedIndirectProcessor/1` now implements the closed WebGPU lowering of the
immutable L2 intent. Preparation verifies the issuing RHI device, canonical
producer, exact graphics-only consumer snapshot and actual native source and
destination payloads. It builds SnapshotMetadata/1 with the same typed builder
as public snapshots, allocates and initializes four private buffers and
prepares the canonical native pipeline/group outside recording. All ordinary
host, writable-descriptor and transfer writes to those buffers are sealed.
Core count/byte leases reserve a fixed conservative ceiling, include retained
intent owners and reported owned-buffer bytes, and survive pool destruction.
Admission never grows or waits for GPU retirement.

The private record method obtains its own opaque encoder identity, validates
both access-declaration views, acquires packet exclusivity and stores the
move-only L2 lease in the existing bounded completion owner. One closed dispatch
and two exact copies produce the packet in the same native encoder. The method
exposes no arbitrary protected destination, kernel or descriptor substitution.
Private producer fault readback remains mandatory before submission. Processor,
intent, source, packages, packet, snapshot and scratch payload survive public
command/processor/pool destruction until real queue completion. Cancellation
releases unsubmitted ownership. Successful recording adds no core allocation;
existing fault-readback and submission callback allocations remain a separate
hot-path limitation.

Assertion-enabled L2/codegen/Null/backend tests and the real Vulkan smoke pass.
Both Chrome Release feature configurations report
`backendProtectedProcessorPassed=true`; structured readbacks compare every
physical argument and mapping word with the L2 reference. Valid input yields
Clean/bits=0. Invalid logical range suppresses instances, zeros the mapping and
yields Faulted/bits=1 with `validationFramePassed=false`. Native errors are
empty. Negative cases cover missing count/byte/command budgets, foreign RHI
devices, source/target extents, omitted declarations, overlapping recordings
and omitted fault reports.

This is Backend Validated private production evidence only. It does not execute
a consumer draw or prove its fault report and does not publish the public
producer execution capability. Null/Vulkan/Metal/WebGPU all keep that version
zero. The disabled-feature browser report still records
`publicRhiIndirectFramePassed=false`. Public processor API/command integration,
no-feature drawing, Metal Native, renderer/WOCC and P6 remain pending.

### Public protected packet execution (2026-09-30)

`BindlessIndirectProcessor` and its bounded pool are issued through the opted-in
device extension. Public encoder recording and pass packet drawing consume the
same immutable intent, exact snapshot, authorization and generation. A native
processor type check rejects application subclasses. Recording verifies logical
source and packet membership in the encoder's RHI registry; a peer RHI device
sharing the same native device remains foreign. The draw requires production
in that exact encoder, a retained native pipeline with the exact authorization,
the snapshot's bound native group, a complete indirect-read declaration and an
index buffer. Fixed offset zero/count one/stride twenty cannot be overridden.
Duplicate consumption rejects. Public finish requires the consumer draw and
both fault readbacks. Generation reports merge producer and consumer outcomes;
known producer faults never become successful zero-instance validation frames.

The prepared completion owner has a finite capacity derived from the explicit
runtime logical-frame budget. Processor budgets remain separate from snapshot,
pipeline, indirect-access and fault reservations. Reservations survive the
pool's destruction and are released on cancellation or real queue completion.
The public fixture drops processor, pool and encoder owners before completion,
verifies retained ownership, then observes release after the actual callback.
Existing private probes separately drop the native command owner as well.

Both Release Chrome feature configurations pass the public capacity-two
`BindlessIndirectConsumer` fixture. Distinct red/green textures are initialized
through public transfer commands. Logical `[3,1,0,0,1]` reads back physical
`[3,1,0,0,0]`, mapping `[1,1,2,1]`, a green RGBA8 pixel and Clean producer and
aggregate reports. Invalid logical first instance three reads back suppressed
`[3,0,0,0,0]`, zero mapping, the blue clear pixel and Faulted/bits=1; aggregate
validation is false. Negative probes cover forged payloads, foreign RHI-device
recording, another encoder's producer, missing production/declarations/budgets,
the wrong otherwise-valid authorization, omitted texture/indirect accesses,
an unbound index buffer and repeated consumption. A missing snapshot also
prevents pass end; incomplete public work cannot become a command buffer.

Reports expose `publicRhiProcessorDrawPassed=true` and structured
`publicRhiProcessorReadbacks`, including every physical/mapping word, pixel,
producer/aggregate fault bit and validation result. Native errors are empty:

- `D:/arp/build/web-webgpu-release/artifacts/bindless-webgpu-rhi-pipeline/report.json`
- `D:/arp/build/web-webgpu-release/artifacts/bindless-webgpu-rhi-indirect-feature-disabled/report.json`

WebGPU now publishes producer version one only with its real native device,
queried limits and prepared bounded completion owner. Null/Vulkan/Metal remain
at zero for this public producer. The ordinary first-instance feature gate is
unchanged; the disabled-feature report retains `publicRhiIndirectFramePassed=false`
while the separate protected processor fixture passes. Seven assertion-enabled
MinGW L2/codegen/Null/backend/Vulkan CTests pass (19 compiler/package cases);
focused L2/Null/WebGPU checks and both browsers pass after the final ownership
change. The browser runner is now a link dependency so its hard gates cannot
remain stale after edits.

This is Prototype Complete and scoped Backend Validated for the public packet
fixture, not complete P3, WOCC P4/P5, full Platform Validated or Production Ready.
Fault-readback preparation and submit callbacks still allocate. Metal native
execution, remaining shader stages/ABIs, renderer/WOCC, full visual/performance
and platform/server gates remain pending.

## PreparedComputeBinding/1

Preparation consumes an explicit opt-in, a retained device-issued Compute
pipeline, the exact immutable snapshot, complete lowering plan and snapshot
generation. A closed owner bundle retains the pipeline, authorization,
package and snapshot. Recording requires that exact pipeline object, live
owners and the complete Compute resource-access declarations. A matching
cache key or plan digest does not substitute for pipeline ownership.

The preparing pool reserves explicit binding count and resident byte ceilings.
Each binding conservatively charges `maxBindingBytes` until the final command
completion owner releases it, even after the pool is destroyed. The reserved
footprint covers snapshot accounting, retained source and the maximum native
argument storage. Separate argument-buffer count/byte ceilings constrain actual
backend preparation. Preparation never compiles a shader, grows a pool, waits
for completion or changes an existing snapshot. Failure cancels reservations.
Referenced scene resource allocations remain charged by their resource owners;
the prepared-binding quota is not a replacement for device memory accounting.

`preparedComputeBindingContractVersion=1` is independent from indexed execution
and from ordinary descriptor support. Null validates immutable ownership and
retains bindings with the explicit command `maxBindlessSnapshots` ceiling,
without GPU execution. Vulkan and WebGPU keep this additional preparation
interface closed and continue their previously validated binding paths. Metal
uses actual compiled function argument encoders, exact reflected array slots,
fully initialized buffers and explicit residency on every encoder bind.
Metal publishes this Compute interface only with explicit device opt-in and
the native prerequisites in `MetalComputeBuffers/1`; its public Apple-native
execution/completion fixture passes. Windows stubs never provide prepared payloads.

| PreparedComputeBinding/1 | Null | Vulkan | Metal | WebGPU |
|---|---|---|---|---|
| Public preparation | Validator, version 1 | DiagnosticFailure, version 0 | Native, version 1 only for opted-in MetalComputeBuffers/1; otherwise version 0 | DiagnosticFailure, version 0 |
| Recording | Exact typed binding, pipeline, snapshot and Compute access validation | Additional interface rejected | Prepared immutable native payload only; ordinary record-time allocation cannot substitute | Additional interface rejected |
| Lifetime | Retained through validator command consumption, NotExecuted | Existing native path unchanged | Retain argument storage, owners and quotas through actual completion | Existing emulated path unchanged |

Diagnostics distinguish missing opt-in, stale device epoch, missing owners,
foreign authorization, changed plan/generation, pool/count/byte exhaustion,
argument footprint excess, forged payload and wrong pipeline. A GPU-capable
backend may publish version 1 only after its public recording/completion fixture
passes. This contract does not authorize a graphics binding, indirect processor
or a new resource operation.

Previous increment evidence: seven assertion-enabled MinGW CTests pass, including the
19-case compiler/package suite, Null hidden-frame smoke and native Vulkan
readback. Emscripten Release L2 and both Chrome first-instance configurations
pass with empty native errors. New Null tests prove exact preparation/recording
correspondence, bounded finished-command retention and allocation-free successful
recording checks. Metal's pure mapping and unavailable Windows-stub checks pass.
That increment did not compile or execute Objective-C++ on Apple. Its zero
Metal capabilities are superseded by the scoped Apple result below; full P3,
platform-wide and production readiness remain unclaimed.

### MetalComputeBuffers/1 acceptance contract (2026-09-30)

The Apple-native increment is restricted to Compute UniformBuffer `load`
(`Slang.float4.uniform/1`), ReadOnlyStorageBuffer `load/query` and StorageBuffer
`load/query/store/atomicAdd` (`Slang.uint32.structured/1`). Arrays are fully
populated, fixed-capacity, and use compiler-reflected argument-buffer indices.
No graphics, texture, sampler, indirect consumer or protected producer is
authorized by this increment. Null remains Validator; Vulkan retains its
existing Native subset and WebGPU its existing Equivalent subset.

`MetalRhiDeviceDesc::enableBindlessCompute` defaults false and is separate from
ordinary argument-buffer enablement. Explicit device opt-in, native argument
buffer tier 2, native synchronization, queried maxBufferLength, and the
MetalComputeBuffers/1 bounded domain are all required. The domain contains
one logical group, with at most the Metal specification's 31 buffer argument
slots and 31 resources per buffer class (readonly/writable storage share the
ceiling). These are conservative versioned lowering ceilings, not invented
native maximum array lengths. Buffer-size ceilings come from maxBufferLength.
All unlisted class/stage support stays unavailable.

Snapshot issuance requires an executable Native plan for that owning device.
Authorization checks the retained Compute-only package, active-prefix,
metadata and fault contracts, exact readonly/readwrite buffer ABI, native
descriptor resources from the issuing RHI registry and native fault/readback
storage. Pipeline preparation consumes retained MSL and sealed reflection.
Only immutable prepared Compute bindings may satisfy pass binding. Missing
native payload, wrong device, stale owners, missing access declarations or
exhausted reservations must reject before dispatch. Finish requires scheduled
fault readback; abandoned and failed native work cannot report Clean.

Acceptance requires assertion-enabled fast L2/backend tests plus an offscreen
public RHI Compute smoke with independent buffer readbacks: non-uniform indices,
valid load/query/store/atomic results, rejected table and element indices with
unchanged neighboring buffers, Clean/Faulted completion reports, finite quotas,
allocation-free prepared binding/dispatch, and ownership retained until actual
completion after public owners are dropped. This is a Compute data test;
graphics visual acceptance and RenderDoc captures remain separate pending gates.

### Indexed indirect dispatch closure

BindlessCross/1 authorizes direct Compute dispatch only. A bound snapshot and
ordinary IndirectBuffer usage do not prove the indirect dispatch's full argument
range, access declaration, producer ordering or completion ownership. Until a
versioned contract proves all of these, Null rejects indexed dispatchIndirect
with `RHI-BINDLESS-INDIRECT-CONTRACT-UNAVAILABLE`; Vulkan, Metal and WebGPU reject
with `RHI-BINDLESS-INDIRECT-DISPATCH-UNSUPPORTED` before recording native work.
Ordinary non-indexed indirect dispatch retains its existing L2 contract. This
closure does not change the independently validated indexed indirect draw paths.

Metal direct dispatch consumes the immutable native lowering decision selected
at Compute pass creation (`MetalCommandBuffer` and `MetalComputeCommandEncoder`,
Native primary recording). The fixed direct workload does not rebuild a dynamic
semantic list per dispatch. Push constants, queries, transfers and indirect
operations retain their own distinct lowering checks. Null, Vulkan and WebGPU
retain their existing direct dispatch mappings. Allocation-free claims here
cover prepared native argument rebinding and successful direct dispatch only;
fault readback preparation and submission completion owners remain separately
allocated and do not establish an allocation-free frame.

### Apple evidence and remaining gates (2026-09-30)

Status: Contract Complete for `MetalComputeBuffers/1`; scoped Backend Validated
on Apple M4, macOS 26.6.2 (25G83), with `MTL_DEBUG_LAYER=1` and active assertions.
This offscreen data fixture does not establish platform-wide Platform Validated
or Production Ready, and it does not close P3/P4/P5/P6.

Eleven focused CTests pass, including the twenty-case compiler/package suite,
shader/matrix contracts, L2 bindless, Metal device/direct-binding, WebGPU device,
pipeline, Null hidden-frame, ordinary Metal Compute and in-flight regression,
and the new public Metal Compute test. Slang 2026.17 generates SPIR-V, MSL and
retained reflection for both entries. The publication payload SHA-256 is
`57d170f26facf0127bac530ebf9f32280315e53dcc8c51e71150978d640d515f`.

`cleanMain` and `faultMain` both read back `[114, 226]`; Clean has zero fault
bits and Faulted has bit 1. Each prepared binding owns three argument buffers
totalling 48 bytes on this device (observed, never a production limit).
Thirty-two native argument rebinds plus public direct dispatch add zero core
allocations. Timeline-gated work retains prepared owners after public objects
are dropped; native fence completion releases them. Cancellation releases
owners and reports Abandoned. Negative assertions cover device/pipeline
ownership, forged bindings, stale plan/generation, missing opt-in/accesses,
preparation/command budgets, unsupported ABI/domain and indexed indirect dispatch.

Evidence: `build/mac-metal-debug/artifacts/bindless-metal/results.xml`,
`tests.log` and `report.yaml`. Vulkan's indexed dispatchIndirect closure and
native fixture compile against Vulkan-Headers 1.4.350.0 on Apple, but its native
and MinGW regression remains pending. Null/package and WebGPU fast contracts
pass; their previously documented GPU evidence is not reissued by this run.

Next is a bounded prepared Graphics binding contract before enabling Metal
stage capabilities, followed by public pixel/fault/completion validation and
AI RenderDoc capture on a supported backend. RenderDoc is unavailable on this
Apple host; no visual result or capture is claimed. Metal texture/sampler
Compute, indirect producer/consumer, renderer/WOCC workloads, whole-frame
allocation and performance budgets, mobile lifecycle and server/platform gates
remain pending. Ordinary descriptor Graphics evidence cannot substitute for
bindless Graphics execution.

## PreparedGraphicsBinding/1

Graphics preparation requires explicit opt-in, a device-issued RenderPipeline,
the exact sealed snapshot, complete lowering plan and generation. The selected
Vertex/optional Fragment stages come from retained pipeline entry metadata;
caller-authored stage masks and descriptor visibility are not substitutes.
Unsupported/tessellation/unknown stages reject. Preparation owns the exact
pipeline, authorization, package, snapshot and count/byte reservations. Pipeline
identity is object identity; a matching cache key does not authorize reuse.

Compute and Graphics share the same preparation budget, reservation and owner
validation implementation. Count/resident/max-binding and native argument
count/byte budgets are supplied explicitly. Each binding reserves the complete
max-binding byte ceiling, including snapshot/source plus allowed native argument
storage; failed validation or backend preparation cancels both leases. Owners
outlive their pool until the last finished-command/completion reference releases
them. Graphics uses a distinct capability version and typed binding so Compute
capability or payload cannot opt in or satisfy a Graphics pass.

Recording validates an open Render pass, exact active pipeline, live device
epoch, complete snapshot/generation/plan and access declarations for all selected
stages. Attachment/raster/viewport/scissor compatibility still comes from the
existing L2 RenderPipeline/RenderPass contract. Binding does not issue a draw,
change dynamic state, compile shaders or allocate native argument storage.
Finished commands retain bindings under explicit `maxBindlessSnapshots` quotas
through real native completion, or validator consumption/cancellation on Null.
Indirect, secondary and bundle execution remain separately authorized contracts.

| PreparedGraphicsBinding/1 | Null | Vulkan | Metal | WebGPU |
|---|---|---|---|---|
| Preparation | Immutable typed Validator, version 1 | DiagnosticFailure, version 0; existing direct path retained | DiagnosticFailure, version 0 until prepared native Graphics execution/completion passes | DiagnosticFailure, version 0; existing direct path retained |
| Recording | Exact pipeline/stages/snapshot and bounded finished-command retention; NotExecuted | Additional interface rejected | Additional interface rejected until native immutable payload and fault ordering are validated | Additional interface rejected |
| Failure | Stable opt-in/epoch/owner/plan/stage/access/quota/payload diagnostics | Stable unavailable diagnostic | Stable unavailable diagnostic | Stable unavailable diagnostic |

Acceptance requires assertion-enabled positive/negative owner and budget tests,
allocation-free successful Null binding/draw checks and Null hidden-frame smoke.
These prove the L2 contract only. GPU publication requires native argument
storage, explicit per-stage residency, pre-pass fault clear, completed readback,
bounded command retention and independent pixel results. Visual acceptance also
requires AI RenderDoc capture evidence or deterministic capture diagnostics;
Null or ordinary descriptors cannot prove this gate. Current status: Contract
Complete on Null Validator. Twelve assertion-enabled focused CTests pass,
including Native/Emulated compiled packages, Vertex-only depth stages, mixed
Compute/Graphics command quotas, exact pipeline/owner validation, retained
finished commands and allocation-free Null binding/draw. Existing public Metal
Compute and Null hidden-frame smoke also pass. Evidence:
`build/mac-metal-debug/artifacts/bindless-prepared-graphics/report.yaml`,
`results.xml` and `tests.log`. No new native Graphics claim.

### MetalPreparedGraphicsStageLayout/1

Prepared Graphics must retain a separate physical layout for each selected
Vertex/Fragment entry. Each layout comes from that exact sealed module's target
reflection and compiled function. A merged stage mask or a shared physical slot
table cannot prove a logical binding's function-specific location. Same logical
bindings may occupy different physical slots in different stages; those layouts
remain separate. Inactivity must be explicit for every snapshot binding.

The shared prepared-binding validator enforces descriptor kind, array capacity,
complete unique elements, exact stage visibility, physical namespace/range,
unambiguous direct/argument mapping and slot ownership. Push constants reserve
their reflected buffer slot only in the consuming entry. Vertex streams reserve
the pipeline's exact lowered slots only in Vertex. Fragment resources do not
collide with Vertex streams; two resources in the same stage and namespace do.
Native argument buffers must use that stage's actual function encoder; count
and bytes include both stages even when their logical resources match.

This is a preparation prerequisite only: Null remains Validator; Vulkan and
WebGPU retain version-0 prepared Graphics DiagnosticFailure; Metal also remains
version 0 until function-specific native storage/residency, pre-render-pass fault
clear, completion retention, independent pixel/fault results and capture
diagnostics are proven. No backend capability is inferred from pure mapping
tests, compiler reflection or ordinary descriptor Graphics execution.

Native pipeline preparation consumes the two selected entries' retained MSL
byte views separately, creates each function from its corresponding library,
and retains both libraries/functions alongside the RenderPipelineState. It does
not concatenate modules, reopen artifact paths or substitute a different entry.
Creation/compile/reflection failure produces no executable payload. The existing
render state lowering still defines attachment formats, raster, depth/stencil,
blend, MSAA and vertex stream layout. This private construction prerequisite
does not authorize snapshot binding or publish Graphics capability.

M4c1 status: Contract Complete for stage mapping; scoped Backend Validated only
for two-entry native pipeline construction on Apple M4 with API Validation.
Fifteen focused CTests pass, including actual per-stage native reflection,
separate retained libraries/functions, deliberately missing artifact paths,
empty/wrong Fragment source rejection, ordinary Graphics/Compute in-flight
regressions and Null hidden-frame. Non-Apple verified-source construction is
closed; its stub source compiles on this host, while MinGW execution is pending.
Failure carries `RHI-METAL-PREPARED-GRAPHICS-PIPELINE-CREATION-FAILED` on Apple.
Control-block ownership for pipeline native objects uses core's memory resource.
Evidence remains `build/mac-metal-debug/artifacts/bindless-prepared-graphics/`.
No indexed Graphics draw, pixel result, fault ordering/completion or visual
acceptance is established. M4c2 argument-only evidence is recorded below.

### MetalPreparedGraphicsArguments/1

Preparation must consume the exact pipeline-owned Vertex and optional Fragment
functions, their independently validated stage layouts, the immutable snapshot
descriptor payload, device identity and explicit preparation budget. Descriptor
visibility alone never selects an encoder. Each selected stage must validate
complete unique descriptor elements, resource type, array capacity and native
device/resource ownership before any argument storage is accepted.

Compute and Graphics must share one native function-argument preparation
algorithm. Graphics retains separate results per function; identical logical
resources across stages do not permit sharing an argument buffer encoded by a
different function. Combined native argument-buffer count and actual allocated
bytes must fit the one binding's explicit ceilings. Encoded lengths and actual
allocation sizes must both be checked with overflow-safe remaining-budget
arithmetic. Native encoder/buffer creation failure or a later-stage budget
failure cancels all earlier preparation ownership; no partial binding is issued.

Every argument buffer is fully initialized before publication. The prepared
payload retains the exact compiled pipeline/functions, descriptor resources and
stage layouts. Recording may only bind this immutable storage and declare
stage-specific read/write residency; it may not allocate an encoder/buffer or
compile a shader. A Compute payload cannot satisfy Graphics, or vice versa.
Binding alone cannot initialize the fault latch inside an already-open render
encoder: pre-pass fault clear, attachment preservation, completion-owned
retention and readback remain independent gates for M4c execution.

Four-backend status during M4c2: Null has only Validator prepared Graphics;
Vulkan/WebGPU retain their approved direct paths and reject this additional
prepared interface; Metal private two-stage argument preparation is implemented,
while public prepared Graphics remains version 0 / DiagnosticFailure.
Acceptance must include fast type/stage/budget/device negative tests, real
native function encoders and storage inspection, ownership cancellation,
existing native Compute regression and Null hidden-frame smoke. These data
checks cannot establish indexed Graphics pixels or visual acceptance.

M4c2 status: Contract Complete; scoped Backend Validated for Vertex+Fragment
native argument preparation only on Apple M4 with API Validation. At M4c2,
optional Fragment omission remained a deterministic rejection in the native
constructor/preparation path; M4c3c1 below supersedes that limitation. Null's
Vertex-only Validator contract remains available.
Both stages preflight resources and encoders before native storage allocation.
The shared function preparation class also prepares all existing Compute
bindings. Count, encoded bytes and allocated bytes use core's overflow-safe
capacity budget. Prepared layouts are copied and retained; resources/functions
and the exact pipeline remain owned through the last prepared payload reference.

The native fixture creates three separate argument buffers with 48 encoded
bytes and 48 allocated bytes on this device. These observed values are not
production limits. Exact aggregate budgets pass; one-less count/byte ceilings
reject and restore resource reference counts and core live bytes. Wrong stage
or function entry, missing descriptors, foreign group device identity and
incompatible sampler reject before native array binding. Dropping external
resources retains their charged storage until the prepared payload releases.
Ordinary argument-buffer policy disabled yields an incompatible scalar-only
sampler. Native Compute readbacks remain `[114, 226]`, fault bits 0/1, and its
32 rebinds plus direct dispatch still add zero core allocations.

Fifteen focused CTests pass, including twenty compiler/package cases, both
Null compiled-package preparation modes, stage mapping, ordinary native
Graphics/Compute in-flight regressions and Null hidden-frame. Non-Apple command
and resource stubs syntax-check on this host; target MinGW execution is pending.
Evidence: `build/mac-metal-debug/artifacts/bindless-prepared-graphics/arguments-report.yaml`,
`arguments-results.xml` and `arguments-tests.log`. The fixture has no Graphics
fault latch and issues no indexed Graphics draw; it cannot prove the public
authorization, fault/completion, pixel or visual gates for M4c3.

`MetalSamplerArgumentCompatibility/1` is a backend-private creation decision,
not a new L2 sampler state or indexed-execution capability. The same L2 sampler
filter/address/mip/compare state is preserved on all four backends. Null validates
that state; Vulkan uses its sampler descriptor; WebGPU uses its sampler state;
Metal additionally sets `MTLSamplerDescriptor.supportArgumentBuffers` explicitly
from the device's approved, queried argument-buffer lowering and its ordinary
argument-buffer policy. When that lowering is unavailable the flag is false.
The payload records the exact creation decision only after native success.
Prepared array encoding rejects a false flag with a stable incompatible-sampler
diagnostic before calling the native encoder. Scalar direct sampler binding does
not require this flag. It never enables BindlessCross or widens an operation
allowlist. Non-Apple stubs retain no compatible native payload.

### PreparedGraphicsPassFaultScope/1 (M4c3, native Graphics consumption pending)

Graphics must declare its exact prepared binding and complete resource access
intent to the owning command encoder before the Render pass creates native
attachment work. Command preparation must validate the live issuer, exact
pipeline/snapshot/generation/plan, selected stages, capability/lowering decision,
native payload and explicit command retention budget before acquiring the
snapshot's exclusive fault submission lease. Clearing the latch is GPU command
work with real completion ownership; host writes or immediately-signalled
completion cannot substitute for it.

Fault clear occurs between encoders, before attachment load/clear operations.
An already-open Render pass must reject a new unprepared fault scope. It may
not end/restart the pass, change load/store actions, replay attachment clears
or reconstruct lost raster/descriptor/query state to make room for a blit.
The next Graphics pass must consume the exact command-owned preparation; a
different command, binding, pipeline or stale scope rejects before drawing.
Compute's existing buffer-only clear may end/resume its Compute encoder under
its existing contract, but that sequence cannot be reused for Graphics.

Bindings, snapshots, argument storage and fault leases share the command's
explicit bounded retention quotas through submission and actual completion.
End schedules fault readback only after the last consuming encoder ends.
Finish rejects unscheduled scopes; cancellation and failed native work never
report Clean. Failure after native mutation poisons recording and prevents
finish/submit. Successful repeated binding within an already-prepared scope
must not compile, allocate argument storage, block or issue another clear.

During implementation Null remains Validator/NotExecuted, Vulkan and WebGPU
must give explicit versioned lowering or DiagnosticFailure for this additional
scope, and Metal public Graphics remains DiagnosticFailure/version 0. Native
buffer-clear ordering, exact scope consumption, shared Compute/Graphics quotas,
completion/cancellation and independent clean/fault pixel/data results are
required before publication. Each increment retains a fast contract path and
Null hidden-frame smoke; visual acceptance still requires AI capture evidence
or deterministic capture diagnostics. This pending contract is not an execution
capability and does not change any backend's previously approved direct paths.

#### M4c3a: native fault-clear boundary and shared retention

The backend-private `recordBindlessFaultLatchClearBeforePass` primitive is the
BetweenEncoders lowering of this scope. It requires no open native encoder,
an unfinished/unsubmitted/unpoisoned command, an exact snapshot fault prior
state, real same-device latch storage and the existing exclusive submission
lease. It encodes only a native blit fill; it never constructs, ends or resumes
a Render encoder. Refusal before encoding leaves recording usable. Failure
after encoding starts poisons recording. Duplicate generation, exhausted
retention/lease budget, stale prior state and missing readback reject.

This primitive is not a prepared Graphics authorization or consumption API.
The command's Compute/Graphics prepared owner category uses the same
`core::BoundedRetainer<BindlessPreparedBinding>` reservation, with pointer
identity deduplication, no recording-time growth and transfer to actual native
completion. Only validated Compute owners enter it at this increment.

Four-backend conclusions: Null's existing Graphics Validator is unchanged;
Vulkan and WebGPU have no additional public fault-scope interface/lowering;
Metal alone implements this private Native blit primitive. All GPU backends
continue to reject this new public Graphics scope because no such execution
capability has been published. Non-Apple Metal gives deterministic
`RHI-METAL-BINDLESS-NATIVE-RUNTIME-UNAVAILABLE`. No visible-output acceptance or
Graphics Backend Validated claim follows from latch-only data evidence.

M4c3a status: Contract Complete; scoped Backend Validated for the native latch
primitive on Apple M4 with Metal API Validation. An authorized `faultMain`
dispatch writes fault bit 1; a separate between-encoder native blit fill and
completion readback return 0. Stale prior state, duplicate generation,
unscheduled finish, finished recording, zero quota, competing/in-flight lease
and open Render/Compute/Blit encoders reject. Cancellation before readback
scheduling releases the lease; cancellation after scheduling reports Abandoned.
Native wait-gated submission stays Pending and exclusively leased until actual
completion. Fifteen focused CTests, including Null hidden-frame, pass. Non-Apple
stub syntax-check passes; MinGW execution remains pending. Evidence:
`build/mac-metal-debug/artifacts/bindless-prepared-graphics/fault-scope-results.xml`
and `fault-scope-tests.log`. No indexed Graphics draw or pixel acceptance.

#### M4c3b: command-owned L2 intent

`CommandEncoder::prepareBindlessGraphicsPass` explicitly opts the next Render
pass into scope version 1. Preparation validates the actual backend-issued
binding, current issuer/pipeline/snapshot/generation/plan, complete access set
and command pipeline/snapshot/prepared-owner quotas before retaining anything.
`CommandEncoderDesc::maxBindlessGraphicsAccesses` bounds retained declarations;
zero disables this scope. Declarations are copied once into reserved storage.
Ordered field equality is the contract for later binding, including buffer
ranges and texture subresources; wider or reordered intent requires a separate
preparation, never an inferred equivalent scope.

Only one intent can await consumption. Failed preparation retains no intent;
failed next-pass validation preserves the pending intent for corrected input.
Compute/Transfer pass begin and finish reject while a Graphics intent awaits
consumption. The next valid Render pass validates compatibility before attachment
work and consumes it. Pipeline and prepared-binding pointer identity must
match; raw snapshot binding cannot substitute. Draw and end require successful
prepared binding with the retained access set. Rebinding with identical intent
is allocation-free. The command retains owners through completion/cancellation;
each subsequent Render pass needs its own explicit preparation.

This API preserves existing version-1 unscoped prepared binding paths. A new
native Graphics execution path must require scoped preparation before rendering.
Scoped
Graphics is an independent capability, never automatically enabled by ordinary
pipeline creation. Null advertises version 1 strictly as Validator/NotExecuted.
Vulkan, Metal and WebGPU advertise version 0 and use the default deterministic
`RHI-BINDLESS-GRAPHICS-PASS-SCOPE-UNAVAILABLE` execution gate. Metal's private
M4c3a blit does not change this conclusion. A native backend must implement the
lease/clear/readback contract and exact consumption before advertising version 1.

M4c3b status: Contract Complete for L2 intent and Null Validator only. The
shared `BindlessRetainedDeclarations` also implements the existing indirect
filter/capture algorithm; complete Graphics intent and indirect-only views have
independent explicit budgets/diagnostics, fail empty on unreadable/overflowing
input and never grow during recording. Field changes, reordered declarations,
wrong issuer/pipeline, a distinct valid binding with identical owners, forged
payload and raw snapshot bypass reject. Both Vertex+Fragment and Vertex-only
depth scopes pass. Pending scope rejects competing passes/finish; failed
next-pass input leaves it available for corrected input. Preparation and 32
rebinds/draws add zero core allocations; cancellation and finished-command
ownership return preparation pool charges to zero. Null reports NotExecuted.

Fifteen focused CTests, including 20 codegen/package cases, native Metal Compute,
ordinary Graphics/Compute/in-flight regression and Null hidden-frame, pass.
Vulkan command/device-contract and non-Apple Metal stub syntax-check pass with
Vulkan-Headers 1.4.350.0. MinGW runtime remains pending. Evidence:
`build/mac-metal-debug/artifacts/bindless-prepared-graphics/intent-report.yaml`,
`intent-results.xml` and `intent-tests.log`. Public Metal Graphics and this scope
on Vulkan/WebGPU remain version 0/DiagnosticFailure. No Graphics Backend
Validated, Platform Validated or Production Ready claim is made by M4c3b.

#### M4c3c: native selected-stage prerequisite

`MetalPreparedGraphicsStages/1` derives its selected stages from the complete
L2 descriptor and matching validated attachment formats. Vertex is required.
Fragment is required for color targets; omission is allowed only for an
explicit depth-only descriptor with a known depth format and an entirely absent
Fragment entry. A malformed/partial Fragment entry is an error, not omission.
Separate verified MSL input must likewise omit Fragment bytes for this case.
Native creation records the exact selection, builds only selected functions and
reflects only selected physical namespaces. The selection is never inferred
from a failed library/function lookup or a backend default.

Immutable argument preparation requires layouts and actual compiled functions
for precisely these selected stages. An absent Fragment must have no layout,
function, library or argument storage. Vertex and Fragment are preflighted
before allocating any selected-stage argument buffer; aggregate budgets count
only selected stages. Native draw/fault scope and public capability remain
independent gates. Null already validates Vertex-only depth intent; Vulkan and
WebGPU retain their approved direct depth behavior but this additional prepared
Graphics interface remains DiagnosticFailure/version 0. Metal public Graphics
also stays version 0 until native scope consumption and acceptance are proved.

M4c3c1 status: Contract Complete; scoped Backend Validated for selected-stage
pipeline and argument preparation only. The authoritative Slang/YAML
`MetalGraphicsBuffers` fixture produces Native and Emulated SPIR-V/MSL/reflection
(plus Emulated WGSL), with distinct clean/fault Fragment entries. Null scopes
remain NotExecuted for both. Seventeen focused CTests pass, including 21
codegen cases, matrix contracts, native Metal Compute and Null hidden-frame.
Evidence: `build/mac-metal-debug/artifacts/bindless-prepared-graphics/selected-stage-results.xml`.
No native Graphics draw, fault execution or pixel acceptance is established.

#### M4c3c2: native binding recording prerequisite

Private prepared Graphics recording consumes only factory-created immutable
storage for the exact bound pipeline and selected functions. Both selected
stages and physical direct/array bindings are preflighted before any encoder
mutation. Each native Render encoder declares resource residency explicitly
for the consuming Vertex/Fragment stage and descriptor access semantics.
Binding reuses prepared argument storage; it cannot create argument buffers,
compile functions, clear a fault latch or restart a Render encoder.

Private Graphics storage and authorized Compute owners use one backend-owned
bounded retention category, reserved by `maxBindlessSnapshots`. Identity
deduplication makes rebinding free of additional charges; cancellation and
actual native completion release storage. Native storage retention is type
erased internally only; it does not issue L2 authorization. This private
primitive is a recording/retention prerequisite and does not authorize indexed
draws, satisfy the pending fault scope or enable public Graphics execution.
Null retains its version-1 Validator contract; Vulkan/WebGPU and public Metal
remain version 0/DiagnosticFailure. Non-Apple Metal returns the stable native
runtime unavailable diagnostic.

M4c3c2 status: Contract Complete; scoped Backend Validated for binding/retention
only with Metal API Validation. Vertex-only and Vertex+Fragment binding,
32 allocation-free rebinds, immutable descriptor copies, exact-function and
zero/exhausted-budget rejection pass. Mixed Compute/Graphics budgets 1/2 in
both orders prove shared accounting. Owners survive a real native submit wait
and retire at actual completion; cancellation before/after finish releases
them. No indexed Graphics draw or fault scope acceptance follows.

#### M4c3c3 execution fixture prerequisite

Before public Graphics authorization, a private fixture may consume only sealed
`MetalGraphicsBuffers` sources/mappings and explicit real native buffers built
from reflected strides and the shared metadata builder. It records a native
Render encoder with the prepared bindings and verifies independent clean/fault
shader data and RGBA readback at actual completion. Fault storage starts at an
explicit GPU-copy-zero boundary before attachment operations. This test fixture
does not issue a public prepared owner, declare a Native L2 fault report or
replace the pending snapshot lease/intent contract. Its raw readback is scoped
native payload evidence. RenderDoc capture support is a separate visual gate;
unsupported capture must remain an explicit diagnostic and cannot imply visual
acceptance, Platform Validated or Production Ready.

M4c3c3a status: scoped Backend Validated for the raw native execution fixture
with Metal API Validation. Independent `cleanFragment`/`faultFragment` runs
produce exact 2x2 red/green RGBA, counters [102, 202] and latch bits 0/1.
Storage uses exactly one reflected element per buffer and the canonical
metadata builder. Readback pitch comes from `kTextureUploadRowPitchAlignment`
and the shared alignment utility. Real GPU-copy-zero occurs before attachment
load/clear, and readback happens after encoder end/actual completion. Binding
plus draw adds zero core allocations. Releasing caller-owned buffers, descriptor
group and prepared storage during a native submit wait preserves resources;
completion releases them. These are raw buffer results, not an L2 fault report.

RenderDoc probing found no `renderdoccmd`, `qrenderdoc` or
`/Applications/RenderDoc.app`. The diagnostic is
`RHI-CAPTURE-RENDERDOC-TOOL-UNAVAILABLE`; no capture or screenshot is claimed.
Visual acceptance, native L2 scope consumption, MinGW and broader platform
validation remain pending. Public Metal Graphics and Vulkan/WebGPU prepared
Graphics remain DiagnosticFailure/version 0; Null remains Validator/NotExecuted.

Final M4c3c1/c2/c3a verification: eighteen focused CTests pass, including 21
codegen cases, matrix contracts, ordinary native Graphics/Compute in-flight
regression, public Metal Compute and Null hidden-frame. Non-Apple Metal stub,
Vulkan command/device-contract syntax checks and all six new Graphics SPIR-V
modules (`spirv-val --target-env vulkan1.3`) pass. Evidence:
`build/mac-metal-debug/artifacts/bindless-prepared-graphics/recording-report.yaml`,
`recording-results.xml` and `recording-tests.log`. Next is M4c3c3b: command-owned
native Graphics intent, snapshot lease and exact completion fault report; it
must consume the existing binding/fault primitives and preserve all attachment
actions. Full M4c3/M4 and the bindless plan remain incomplete.

### BindlessFaultDomain/1 (M4c3c3b prerequisite)

A fault domain is an owned, validated L2 buffer pair with a nonzero generation,
one uint32 GPU atomic latch and one uint32 completion readback destination.
It is independent of shader authorization, descriptor preparation and public
Graphics capabilities. `BindlessSnapshot` is a domain and retains all existing
budget/descriptor ownership when leased. Standalone owned domains create actual
buffers through `GpuDevice`, require an explicit resident byte ceiling checked
against reported allocations, and seal external host/transfer writes to the
latch and external host writes to readback. A domain cannot publish a native
completion or shader capability by construction.

One exclusive move-only lease reserves the domain from GPU clear through real
completion/readback. Acquisition validates live storage before reserving the
existing core capacity budget; it never allocates or waits. Cancellation releases
the reservation without advancing prior state. Only backend completion after
the actual latch copy may mark prior state CopySrc. Leases retain the entire
domain owner; arbitrary CPU signaling and failed/abandoned work cannot report
Clean or advance completion state.

The shared Metal fault-clear/readback lowering consumes this interface and
validates real same-device native buffers. It keeps the existing between-encoder
and Compute-resume placement rules, bounded ownership/outcomes, duplicate
generation refusal, poison-on-partial-mutation and native completion handler.
This allows private Graphics native resource tests to use the same lease and
completion implementation as snapshots, without forging a snapshot or enabling
public Graphics preparation. The source scope contains no native handles.

Four-backend conclusions: Null validates storage/lease only and cannot infer
execution; Vulkan/WebGPU retain existing snapshot completion lowering and public
prepared-Graphics DiagnosticFailure; Metal shares its Native fault primitive
for actual owned domains but public prepared-Graphics remains version 0.
No generic buffer-pair helper establishes Backend Validated on a backend;
native clear/copy/completion evidence remains required. This is not the final
command-owned Graphics intent/pass-consumption contract or visual acceptance.

### MetalPreparedGraphicsNativeScope/1

The backend-private native scope consumes a factory-created prepared Graphics
payload plus an exact fault domain and a complete retained ordered L2 access
set. Scope version 1 is buffer-only, Vertex or Vertex+Fragment, one non-layered
Render subpass; other resource/scope forms deterministically reject. Declared
buffer IDs must resolve in the issuing logical device's registry to the exact
prepared buffers, with complete byte ranges, ShaderRead/ShaderWrite intent and
selected shader/pipeline stages. The domain's latch must be an actual writable
prepared descriptor and its readback must have an explicit TransferWrite
declaration. Type/range/stage/identity cannot be guessed or repaired.

Preparation validates exact storage, domain, declarations and shared pipeline,
domain/prepared-owner quotas before performing the existing native pre-pass
clear and acquiring the existing exclusive lease. Declarations are copied into
`maxBindlessGraphicsAccesses` reserved storage once; failed preparation retains
no pending scope. Only the next compatible Render pass may consume it, checked
before attachment work. Pending scope blocks Compute/Blit begin and finish.
Invalid next-pass input leaves the pending intent available for correction.

The consuming Render pass requires exact pipeline/prepared-binding identity
and ordered declarations. Unscoped prepared binding cannot bypass it. Draw/end
reject before the binding is consumed; indirect/subpass changes reject for
this explicit subset. Secondary command execution also rejects before native
mutation: its encoded bindings/accesses cannot consume this exact primary scope.
Rebinding does not clear the latch or allocate argument
storage. End first closes the consuming Render encoder and then schedules the
shared native fault copy; failures after mutation poison recording. Attachment
load/store/clear actions are consumed exactly once from the L2 descriptor.

Cancellation and actual native completion own the existing shared domain lease,
fault outcome and bounded retained pipeline/prepared storage. Native scopes
prove private lowering only. Public Metal Graphics remains version 0 until
device-issued Graphics authorization and L2 adapter acceptance are complete;
Null is Validator and Vulkan/WebGPU prepared scopes remain DiagnosticFailure.
These conclusions do not weaken capture/platform/Production Ready gates.

M4c3c3b1/b2 status: Contract Complete; scoped Backend Validated for the private
Metal native buffer scope with API Validation. Fault-domain storage and logical
device identity, bounded complete ordered intent, exact next-pass/pipeline/
binding consumption, automatic post-Render readback and real completion
retention pass. Missing/narrow/wrong-stage/state/access/resource declarations,
zero/exhausted command budgets, wrong domain, duplicate preparation and ordinary
set/table/secondary/indirect/subpass bypasses reject deterministically. Pending,
active-unbound and finished cancellation release leases and storage without
advancing prior state; abandoned readback reports Abandoned.

Independent clean/fault scopes return exact red/green RGBA and counters
[102, 202], with native completion reports Clean/0 and Faulted/1. Vertex-only
scoped depth execution writes exact D32 zero without a Fragment payload. A
scoped color Load/Store pass draws red in one column and preserves the seeded
blue in the untouched column; the different descriptor clear color is not
applied. Actual completion advances the fault-domain prior state to CopySrc.
Binding/rebinding/direct draw remain allocation-free; preparation, outcome and
submission ownership still allocate, so whole-frame allocation freedom is not
claimed. Eighteen focused CTests (including 21 codegen cases), shader matrix
contracts, Compute regressions and Null hidden-frame pass; Vulkan command/device
contract and non-Apple Metal stubs compile in syntax-only checks. Evidence:
`build/mac-metal-debug/artifacts/bindless-prepared-graphics/scope-report.yaml`,
`scope-results.xml` and `scope-tests.log`.

The unavailable RenderDoc diagnostic remains
`RHI-CAPTURE-RENDERDOC-TOOL-UNAVAILABLE`. Native pixel/depth data acceptance is
scoped payload evidence; capture/visual acceptance, public device-issued Graphics
authorization/L2 adapter, MinGW and full platform gates remain pending. All public
Metal Graphics versions stay zero; Null is Validator/NotExecuted and Vulkan/
WebGPU additional prepared Graphics scopes remain DiagnosticFailure. Next native
acceptance checks multiple real scope quotas and domain reuse after completion.

Multiple private scopes in one command use the same pipeline/domain/prepared
retainers as Compute. A distinct owner charges one capacity slot; completed
Render recording does not retire it before native submission completion.
Insufficient subsequent-scope capacity rejects before its GPU fault clear and
leaves the first scope's scheduled result intact. The same domain generation
cannot execute a second scope in one command. After actual completion releases
its lease, another command may reuse the domain from CopySrc; it must clear the
previous fault bits and derive a new result only from the new command's shader
work. Caller-owned declaration arrays may change after preparation; the command
continues to compare against its immutable captured intent.

M4c3c3b3 status: scoped Backend Validated for multi-scope private buffer
recording and real domain reuse. Two real pipelines/domains reject a second
scope with pipeline/domain capacities 1/2 and 2/1, preserving the first Pending
result and leaving the second domain unleased. Capacity 2/2 produces independent
Clean/0 and Faulted/1 reports; both prepared/domain owners survive a real native
wait and retire at completion. A same-command generation reuse rejects even
with available owner slots. Mutating the source declaration order after
preparation cannot alter the retained intent. Reusing one domain in separate
completed Faulted then Clean submissions clears from CopySrc and returns bits
1 then 0, with exact counters [102,202] then [104,204]. Quick contract, native
Graphics with API Validation and Null hidden-frame pass. Evidence:
`build/mac-metal-debug/artifacts/bindless-prepared-graphics/composition-results.xml`
and `composition-tests.log`. M4c3c3's private native execution gates are complete;
public authorization/adapter/capture/platform and full M4 remain open.

### MetalGraphicsBuffers/1 admission prerequisite

This admission validator is a preflight contract, not a capability or an
execution authorization. It consumes a sealed compiler package's typed artifact
and reuses the existing Metal buffer-domain rules: Native mode, set zero, no
other descriptor domains/reserved stage resources/indirect ABIs, immutable
SnapshotActivePrefix/2 plus SnapshotMetadata/1 and FaultReporting/1. All physical
bindings are nondynamic UniformBuffer/ReadOnlyStorageBuffer/StorageBuffer and
belong to declared tables or the exact metadata/latch bindings. The sealed
package loader remains the single validator for Slang.float4.uniform/1 and
Slang.uint32.structured/1 buffer identity/stride/alignment/minimum-size ABIs.

Graphics metadata/latch visibility is Vertex or Vertex+Fragment; Compute and
unknown stages reject. Selected pipeline stages still come from retained entry
metadata. The initial tested operation allowlist is UniformBuffer load and
ReadOnlyStorageBuffer load/query in Vertex and Fragment, plus StorageBuffer
query/atomicAdd only in Fragment. Vertex table writes and Fragment StorageBuffer
load/store are not admitted by extrapolating Compute evidence; the compiler's
reserved fault-latch write remains a separately validated primitive. Textures,
samplers, mixed Compute modules and indirect domains reject deterministically.
Compute retains its existing separate operation allowlist and capabilities.

Null continues its generic Validator contract; Vulkan/WebGPU retain their
existing independently tested direct operation allowlists and version-zero
additional prepared Graphics interfaces. Metal admission alone leaves both
public Graphics capability versions zero. Later public authorization must also
validate the issuing device/registry/native limits and exact snapshot/package;
its typed owner, public command adapter and capture acceptance require their
own tests. No caller-provided flag can promote this preflight to execution.

M4c4a status: Contract Complete for the admission prerequisite. The shared
`MetalRhiBindlessBufferDomainValidator` preserves Compute diagnostics/allowlist
and applies Graphics stage/domain/resource/operation restrictions independently.
Twenty-four deterministic negative cases cover unsupported ABI/version/mode,
Compute/unknown visibility, dynamic bindings, texture/sampler/storage textures,
unapproved writes/operations, extra domains/reserved resources and sampler
coupling. Vertex-only and Vertex+Fragment buffer domains pass. Real sealed
Graphics packages are admitted before the private native scope fixture executes;
this connects positive preflight to actual compiled ABI/payload evidence while
public Graphics remains closed, including on Compute-enabled capability input.
Five focused CTests pass (fast bindless/device contracts, native Compute/Graphics
and Null hidden-frame); non-Apple source/stub syntax checks pass. Evidence:
`build/mac-metal-debug/artifacts/bindless-prepared-graphics/admission-results.xml`
and `admission-tests.log`. No public Graphics adapter or authorization is claimed.

The next public adapter must consume a distinct explicit device opt-in contract,
queried tier-2 argument-buffer/native completion/buffer binding limits and exact
native resource registry. Package admission cannot substitute for any of these.
Device-issued authorization must retain package/snapshot identity and epoch;
preparation must use the existing typed Graphics owners, exact selected entries,
shared preparation budget and immutable native stage payload. Primary preparation
must validate its L2 scope before calling the proven native scope; Render binding
must consume both exact owner identity and ordered access intent. GPU completion
must retain the public typed owner as well as native argument/domain storage.
All public entry points continue to reject while capability versions are zero.
Capture acceptance and public end-to-end tests precede a version-1 publication;
no mutable acceptance flag or fixture-authored capability enables execution.

### MetalRetainedGraphicsPipeline/1 adapter prerequisite

The private pipeline builder consumes a retained sealed Graphics package, exact
selected Vertex/optional Fragment entries and state-only L2 RenderPipelineDesc.
It owns the generated selected-entry interface and full physical-domain layout;
no interface/layout pointer may borrow a factory stack object. Selected entries
are validated by appendBindlessShaderEntry and native function-specific lowering.
Each stage uses its own retained MSL/reflection bytes; pipeline construction must
not load an artifact path or infer omitted Fragment data. Shared L2 state/layout
validators and Metal vertex-stream/push/optional-feature lowerings run before
native pipeline creation. Unsupported metadata or missing native runtime fails
with a stable diagnostic, never an ordinary file-IO or Dummy fallback.

This builder creates a real private pipeline prerequisite, with no device-issued
BindlessAuthorization and therefore no public indexed preparation permission.
The later authorized wrapper consumes the same builder only after independently
checking both Graphics capability versions, Native executable payload, exact
issuer/current epoch/device/plan/package identity and the Graphics admission
contract. It retains authorization and uses pipeline-selected stage identity.
Version-zero callers reject before source preparation or native creation; native
Compute capability cannot enable this wrapper. Null retains its version-one
Validator interface and Vulkan/WebGPU their existing direct native/emulated
paths; their additional prepared Graphics interfaces remain version zero.
Real selected-stage pipeline/binding/draw/readback evidence and quick contract /
Null hidden-frame tests prove the private builder only. Public scope/authorization
publication and capture acceptance remain separate uncompleted gates.

M4c4b1 status: Contract Complete; scoped Backend Validated for the private
retained pipeline builder. Factory-owned selected interfaces/layouts/package
bytes replace the fixture-only construction in exact clean/fault draw/readback,
Load/Store, Vertex-only depth and Faulted-to-Clean reuse smokes. Vertex and
Fragment maps use their separate verified entry metadata; the merged selected
interface is used only for L2 reflection/layout validation. Sealed package
loading already rejects push constants; the selected pipeline view rejects them
again with a stable diagnostic rather than inferring an unproven ABI. A real
queried argument-buffer/tier-2 probe is required before native creation.

Factory tests reject missing/wrong-stage entries, caller-authored interface,
unsupported state, omitted Fragment outside depth-only, wrong backend and
missing declared argument capability. An independently loaded package remains
owned after its caller releases it; package/interface/layout ownership retires
with the concrete pipeline. Public and private authorized factories reject a
real Compute-issued authorization at version zero before source/native work.
With pre-reserved diagnostic storage this gate performs zero core allocations.
Public Graphics support remains entirely version zero; no authorized Graphics
owner, binding or L2 scope success has been manufactured. Eighteen focused
CTests and the latest five contract/gate/native/Null smoke tests pass. Vulkan
command/device contract and non-Apple pipeline/device source syntax checks pass.
Evidence: `build/mac-metal-debug/artifacts/bindless-prepared-graphics/factory-report.yaml`,
`factory-results.xml`, `factory-tests.log`, `factory-gates-results.xml` and
`factory-gates-tests.log`. Capture/public end-to-end acceptance, MinGW and
full platform/production gates remain pending.

The next typed binding adapter must retain real BindlessGraphicsBindingOwners,
reject Validator/foreign pipeline or descriptor payloads, derive selected layout
entries from the exact retained native functions, consume the canonical package
lowering and build immutable native storage through the existing Graphics
preparation algorithm. It reports combined argument counts/bytes to the shared
L2 preparation pool; it cannot create authorization or turn a private retained
pipeline into an indexed public pipeline. Public capability-zero gates remain
prior to owner allocation/preparation. Null/Vulkan/WebGPU conclusions are
unchanged; success tests for public Metal owners remain pending capture and
actual device-issued Graphics publication.

M4c4b2 status: Contract Complete for the adapter source/rejection boundary
prerequisite, with real public typed-owner/native success acceptance pending.
The concrete Metal binding retains immutable Graphics owners and canonical
native storage, validates exact native pipeline/set/stage/function/selected-entry
identity and forwards combined argument counts/bytes to the shared L2 pool.
Selected entry views borrow only from the owned concrete pipeline descriptor.
Public pipeline and preparation factories use one capability-fact predicate;
it consumes versions/Native payload/queried limits/completion facts and never
publishes Graphics support. Actual capabilities remain zero.

Real Null Validator owners reject before native preparation and with zero core
allocations when diagnostic capacity is reserved. Foreign/fake bindings cannot
extract a Metal Graphics payload; missing owners reject deterministically.
Native private retained pipelines return exact selected entries, including
explicit Fragment omission. These negative tests do not prove a public native
owner or completion path; no fake capability/authorization was created for
positive testing. Eighteen focused CTests and seven latest gate/native/Null
owner/smoke tests pass. Non-Apple adapter/pipeline/device/stub source syntax checks
pass. Evidence: `build/mac-metal-debug/artifacts/bindless-prepared-graphics/adapter-report.yaml`,
`adapter-results.xml`, `adapter-tests.log`, `adapter-gates-results.xml` and
`adapter-gates-tests.log`. Public authorization/scope/capture, MinGW and full
platform/production acceptance remain incomplete.

M4c4b3 must validate the typed owner through L2 primary preparation, then consume
both typed binding identity and native storage identity in the next compatible
Render pass. It must reject unsupported/raw/indirect/secondary/bundle paths
before native mutation, preserve complete ordered declarations, and retain the
public owner's count/byte leases through actual completion/cancellation using
backend-owned bounded storage. Retaining only native argument buffers would
prematurely retire public preparation charges and is invalid. Native end-of-pass
fault reporting remains the already-validated shared domain implementation.
Compute and Graphics share command quotas; publication remains closed until
real device-issued Graphics authorization and public positive execution/capture
acceptance are available. This source adapter phase cannot weaken those gates.

### MetalTypedGraphicsPassAdapter/1 lowering prerequisite

Primary preparation consumes the published Graphics predicate independently
from Compute, nonzero caller command quotas, a current exact issuing-device
authorization, the concrete immutable Metal binding and complete L2 declarations.
Only then may the native scope clear the fault latch and capture ordered intent.
The next compatible Render pass moves the typed intent; an invalid pass preserves
it, while Compute/Transfer/finish reject unconsumed intent. Render binding requires
the identical typed owner and selected pipeline, and native declaration equality.
Raw snapshots, ordinary descriptors, indirect draws, subpasses, secondary and
bundle replay cannot replace scoped intent. Cancellation releases it without
submission; end schedules the existing completion-gated fault copy.
Primary resource transitions, memory/alias barriers also reject unconsumed
intent so callers cannot mutate its admitted resource state before the pass.
Selected pipeline and direct draw gates revalidate live public owners; ended
passes cannot issue another direct draw. Render guards consume the native scope
fact even for private prerequisites, covering CPU replay and the single-subpass
no-op path before either can bypass the native scope gate.

One bounded prepared-binding slot retains an alias whose pointer identity is the
native storage and whose control block owns the typed binding. The binding owns
that storage, authorization, snapshot, pipeline and count/byte leases. This uses
no new quota or allocation during recording and creates no ownership cycle.
The alias must be inserted at scope admission before plain storage deduplication;
submission moves this same retainer to native completion ownership. Retaining a
plain storage pointer first cannot be used to prove typed-owner retention.

Null remains its tested Validator implementation; Vulkan and WebGPU keep their
prepared Graphics scope capability false and deterministic diagnostic failure.
Metal public versions stay zero: source/rejection and private native tests are
prerequisite evidence only. Real device-issued public Graphics owner success,
RenderDoc capture and platform acceptance remain pending.

M4c4b3a status: Contract Complete for the versioned lowering and source/rejection
boundary prerequisite only. Primary and Render-pass adapters are implemented,
with typed owner alias retention in the existing native completion retainer.
Pointer identity deduplicates one prepared slot; control-block identity rejects
an earlier plain-storage owner before scope mutation. The shared retainer's
unit fixture proves alias-owned capacity, deduplication, submission move and
final release without core allocation. This fixture is ownership mechanics,
not a public Graphics binding or native completion proof.

Real Null Validator owners and missing owners reject public/native preparation
without creating GPU encoders. A private real native scope consumed by an L2
Render pass rejects indirect/count draws, secondary/bundle objects and even the
single-subpass nextSubpass path; an ended pass rejects another direct draw.
Eighteen rebuilt CTests pass, including native Compute/Graphics under Metal API
Validation, exact clean/fault pixels and attachment preservation, shared quotas
and native completion, shader contracts, real Null prepared owners and Null
hidden-frame. Non-Apple adapter/pipeline/device/stub syntax checks pass.
Evidence: `build/mac-metal-debug/artifacts/bindless-prepared-graphics/pass-adapter-report.yaml`,
`pass-adapter-results.xml` and `pass-adapter-tests.log`.

M4c4b3 and M4c4b3b remain incomplete. No real public Metal Graphics authorization
or typed-owner native completion test has been enabled; publication/capture,
MinGW, Platform Validated and Production Ready gates remain open. Current
capture diagnostic remains `RHI-CAPTURE-RENDERDOC-TOOL-UNAVAILABLE`.

## MetalComputeImages/1

This independent Compute data increment extends the existing immutable prepared
binding contract. L2 intent is fixed-capacity, non-uniform Texture2D arrays:
SampledTexture load/query/sampleLevel with an independently indexed ordinary
sampler, and R32Float ReadWrite StorageTexture load/query/store. Existing closed
float4-uniform and uint-structured buffer ABIs remain available. Texture views
must be color, single-sampled, one layer, Texture2D; sampled views are Rgba8Unorm
and storage views R32Float. Comparison samplers, depth/array/MSAA views, implicit
derivatives, other formats and indirect execution fail deterministically.

`enableBindlessComputeImages` is a separate default-false opt-in and requires
`enableBindlessCompute` plus argument-buffer enablement. It grants no Graphics
capability. Runtime support derives from actual argument-buffer tier 2, native
synchronization, readWriteTextureSupport >= Tier1 (Metal's versioned R32Float
read/write guarantee), maxBufferLength and Metal's core conservative per-stage
ceilings of 128 textures and 16 samplers. Texture class limits share the native
texture namespace and must not authorize more than 128 combined sampled/storage
resources. Counts, preparation bytes and completion storage remain caller bounded.
L2 `BindlessCapabilities::combinedSampledStorageTextureLimits` is a per-stage optional extra
ceiling: absent means the backend declares no shared sampled/storage namespace
constraint; present zero denies all image resources. The planner includes table
elements and ordinary/reserved bindings before choosing executable lowering.
Metal supplies the versioned 128 ceiling; Null Validator, Vulkan's separately
queried classes and WebGPU's scalar binding classes leave this extra constraint
absent and retain their existing individual limits. This cannot be inferred
from a backend name by the planner.

| Backend | Lowering conclusion |
|---|---|
| Null | Existing Validator arrays, declared formats/shapes/access and NotExecuted completion; no GPU claim |
| Vulkan | Existing Native sampled load/query/sampleLevel with independent sampler and R32Float storage load/query/store, subject to its queried indexed features/limits |
| Metal | Native only after the independent opt-in, queried substrates, exact sealed Compute package and real prepared dispatch/readback acceptance; otherwise DiagnosticFailure for image intent |
| WebGPU | Existing Equivalent scalar expansion/switch with independent sampler combinations and explicit ReadWrite r32float layout; no new native-array claim |

Authorization consumes exact issuing registry/native resources, texture format,
view shape, storage access and sampler comparison/argument compatibility facts.
Preparation reuses the function-specific argument encoder, immutable storage,
per-resource residency and existing completion-owned typed binding. Native
creation/preparation failures return diagnostics; no buffer-only substitution or
Dummy is permitted. Upload/barrier/copy operations use existing L2 contracts and
backend-owned staging. No shaders compile or files load during bind/dispatch.

Acceptance requires generated Slang SPIR-V/MSL/reflection (and Emulated WGSL),
quick compiler/admission/opt-in negatives, Null hidden-frame, and actual public
Compute data readbacks. Distinct texels, samplers and storage values must prove
independent indexing, explicit filtering, load/query/write, invalid table/sampler
guards with unchanged resources, Clean/Faulted completion and bounded owner
retention/cancellation. Graphics capture/platform/Production Ready gates remain
separate and incomplete.

Status: Contract Complete for this closed image domain; scoped Backend Validated
on Apple M4/macOS 26.6.2 under Metal API Validation. Public device-issued
authorization, pipeline creation and prepared dispatch produce exact storage
values `[0.75,1.75]`. RGBA8 texel loads, width queries, distinct nearest/linear
samplers and explicit LOD=0/1 results match independently authored expectations.
Out-of-range table/sampler operations preserve the same results and the seventh
buffer sentinel; Clean/0 and Faulted/1 are observed only after real completion.
Actual comparison samplers and actual sampled R32Float views reject authorization.
Missing resource/write declarations and argument count/byte exhaustion reject
without charging a binding. Thirty-two rebinds plus dispatch add zero core
allocations; command-end fault reporting and submission still allocate and
whole-frame allocation freedom is not claimed. Dropping all caller wrappers
retains native image/sampler/storage and typed count/byte owners through a real
timeline wait, then retires them on completion or unsubmitted cancellation.

Twenty-one rebuilt CTests pass, including twenty-two compiler/package cases,
Native/Emulated image publication and WebGPU scalar interface validation, shader
matrix contracts, Null hidden-frame and existing Metal buffer/Graphics regressions.
Four generated image SPIR-V modules pass spirv-val for Vulkan 1.3. Non-Apple
Metal device/command/pipeline/stub and Vulkan command/device-test syntax checks
pass; these are compile checks, not target-backend execution evidence.
Evidence: `build/mac-metal-debug/artifacts/bindless-metal-images/report.yaml`,
`results.xml` and `tests.log`. No visible output/capture acceptance, MinGW native
execution, mobile lifecycle, Platform Validated or Production Ready claim.

## MetalPreparedFaultOutcomes/1

This backend-private performance lowering preserves BindlessFaultDomain/1 L2
meaning. The command descriptor's explicit maxBindlessSnapshots bounds result
owners: allocate exactly that many distinct Pending atomic result owners during
command preparation through core allocators. Scheduling consumes one reserved
owner only after domain/device/range/lease/duplicate/native encoder checks
succeed; no result owner may be allocated at scheduling time or recycled for a
second generation. Exhaustion fails RHI-METAL-BINDLESS-FAULT-READBACK-CAPACITY
before native work. Shared result owners outlive logical command destruction;
only real completion changes Pending to Clean/Faulted/ReadbackFailed and
unsubmitted cancellation changes it to Abandoned. Unused reservations have no
resource/domain references. Existing bounded pending/outcome vectors and actual
completion ownership remain authoritative; do not use CPU immediate success.

Null remains Validator/NotExecuted, Vulkan retains its existing Native completion
path, and WebGPU retains its Equivalent scalar completion path. Their result
ownership lowering is unchanged. Non-Apple Metal stays DiagnosticFailure and
does not allocate executable result owners. Native Metal may preallocate result
objects only for its actual command; this grants no Graphics capability. This
phase does not claim zero native encoder allocation, zero submission callback
allocation, whole-frame performance, Platform Validated or Production Ready.
Acceptance requires quick contract gates, Null hidden-frame, real Compute image
and buffer Clean/Faulted/cancellation plus private Graphics scope composition,
and evidence that scheduling never allocates a result object.

Status: Contract Complete for this lowering; scoped Backend Validated with
twenty-one rebuilt CTests. Telemetry records exactly the authored quota at
construction and successful consumption only. Zero quota rejects scheduling
before blit work; failed preflight and duplicate generations preserve remaining
owners. Real private Graphics scope composition yields independent Clean/0 and
Faulted/1 states, proving the atomic results were not recycled. Public Compute
buffers/images and unsubmitted cancellation retain their exact expected results.
Non-Apple Metal device/command/pipeline/stub syntax passes. Evidence:
`build/mac-metal-debug/artifacts/bindless-prepared-graphics/fault-outcomes-report.yaml`,
`fault-outcomes-results.xml` and `fault-outcomes-tests.log`. Scheduling still
creates a native blit encoder owner and may grow ordinary native resource
retainers; these allocations are not included in the outcome-object claim.

## MetalPreparedSubmissionOwners/1

This backend-private performance lowering preserves SubmittedResourceRetention/1
and BindlessFaultDomain/1. Prepare one core-allocated per-command shared owner
containing empty native-resource and pending-fault containers. After all current
native submit/query preflight succeeds, move the already-recorded vectors and
bounded typed retainers into this owner without allocating a new control block.
The native completed handler owns that same state, independently of logical
command destruction. Actual GPU completion resolves all pending outcomes and
leases, then explicitly clears native resources and typed owners before signaling
completion. Failed pre-commit submission and unsubmitted abandonment preserve
the current deterministic failure/cleanup contract. Never use a CPU completion
substitute or retain the whole command payload as a callback cycle.

Null stays Validator, Vulkan retains its current Native completion retention,
WebGPU retains its Equivalent scalar completion retention, and non-Apple Metal
stays DiagnosticFailure without executable owner allocation. This change grants
no capability and adds no quota: one fixed completion owner per existing bounded
native command, populated only from the existing caller/resource budgets.
Native submit-wait command ownership, query container copies and the optional
fence-free completion object are separate performance limits. Tests must observe
real typed/resource/fault retention before native timeline completion and release
after it, plus distinct multi-generation statuses and cancellation. A query-free,
explicit-fence submission may not allocate resource/fault control blocks; a native
timeline wait still allocates its wait-command owner.

Status: Contract Complete for this lowering; scoped Backend Validated. The
query-free explicit-fence public image path measures zero core allocations for
submission without a wait and exactly one for the native submit-wait command
owner when delayed by a timeline. Two private Graphics generations share the
preallocated completion owner yet resolve independently to Clean/0 and Faulted/1;
native image/sampler/storage and typed count/byte leases retire after actual
completion. Logical-command destruction and cancellation regressions pass.
Twenty-one rebuilt CTests and the real Metal staging pool pass; non-Apple
Metal device/command/pipeline/stub syntax passes. Evidence:
`build/mac-metal-debug/artifacts/bindless-prepared-graphics/submission-owners-report.yaml`,
`submission-owners-results.xml`, `submission-owners-tests.log` and
`submission-staging-results.xml`. Whole-frame/platform/capture gates remain open.

## MetalScopedFaultCopy/1

This backend-private lowering preserves the approved fault-copy ordering and
completion semantics. Fault copy is fully synchronous recording between native
encoders; its local Metal blit encoder must stay alive until endEncoding, then
release at that scope boundary. It does not need an independently allocated
shared encoder control block. All preflight and pending-Graphics intent rejection
must occur before native creation, and native creation failure remains a diagnostic.
Use a native autorelease scope to own the returned encoder through complete copy
and managed-resource synchronization recording. The already-acquired domain/lease
owns the latch; the pending record owns the exact native readback destination and
domain through actual GPU completion/cancellation. Do not add duplicate ordinary
resource-retainer entries for these already-owned resources. Consume a prepared
result only after native recording succeeds; failed managed synchronization or
end recording poisons the command as before.

Null Validator, Vulkan Native, WebGPU Equivalent and non-Apple Metal diagnostic
conclusions are unchanged; this is not a new capability or Graphics publication.
Acceptance requires zero core allocations for successful prepared Compute rebind,
dispatch and fault-copy pass-end, actual native clean/fault/cancel data and private
Graphics scope/attachment/lifetime regressions. This does not cover native driver
allocation, ordinary encoder/resource recording, queries, command preparation or
whole-frame performance.

Status: Contract Complete for this lowering; scoped Backend Validated with six
rebuilt focused CTests, including fast contracts, Null hidden-frame, real public
Compute buffers/images and private Graphics attachment/scope/lifetime tests.
Thirty-two prepared rebinds, direct dispatch and Compute pass-end add zero core
allocations, including fault copy/result recording. Exact image/buffer data,
Clean/Faulted, cancellation, exclusive fault lease and real typed retirement
remain validated. Evidence:
`build/mac-metal-debug/artifacts/bindless-prepared-graphics/fault-copy-report.yaml`,
`fault-copy-results.xml` and `fault-copy-tests.log`.

## MetalPreparedDefaultCompletion/1

This backend-private performance lowering preserves the L2 queue/fence completion
contract. Each native command's preallocated SubmissionRetention owner embeds a
real MetalRhiCompletionState. When the caller omits a fence, the queue passes an
alias-owned reference to this state rather than allocating a fresh state at
submission. An explicit fence continues to provide its own completion state.
The state stays Pending until the existing real native completed handler resolves
faults and releases owners, then signals; the current failed-precommit terminal
path remains deterministic failure and never authorizes successful fault data.
The state can outlive the logical command via the alias control block, without a
command/callback cycle. No CPU immediate completion or dummy state is permitted.

Null remains Validator, Vulkan Native and WebGPU Equivalent retain their current
completion mappings; non-Apple Metal returns no prepared executable completion
state and keeps DiagnosticFailure. No public capability, quota or synchronization
semantics change. Acceptance requires real query-free fence-present/fence-absent
submit, zero core allocations without native wait and one wait-command owner
allocation with delayed native wait, exact Clean/Faulted/resource retirement and
quick contracts plus Null smoke. Query copies, ordinary encoders, command
preparation, driver allocations and whole-frame performance remain separate.
Status: Contract Complete for this lowering; scoped Backend Validated. All four
query-free fence-present/fence-absent and native-wait/no-wait cases pass exact
image data and real typed/resource retirement. Submission adds zero core
allocations without a native wait and one wait-command owner with a native wait,
including when the caller omits a fence. The delayed fence-free state stays
Pending until the GPU callback; its alias survives logical command destruction
and does not keep native image/sampler owners alive after completion. The fast
device contract verifies stable state identity and allocation-free access;
non-Apple Metal returns no executable state. Twenty-two rebuilt CTests pass,
including compiler/package, shader, Null hidden-frame, staging and native
Compute/private Graphics regressions. Non-Apple Metal device/command/test/stub
syntax passes. Evidence:
`build/mac-metal-debug/artifacts/bindless-prepared-graphics/default-completion-report.yaml`,
`default-completion-results.xml` and `default-completion-tests.log`.

### Remaining retention-budget audit (M4e5)

The next phase remains incomplete. Metal's ordinary resource identity/payload
vectors still grow when recording previously unseen resources. Timestamp and
pipeline-statistics query recording also appends to dynamic vectors. Submission
now moves those exact arrays into the prepared completion owner and captures
only its alias; nonempty native query acceptance still needs a supporting host.
maxBindlessSnapshots
bounds prepared/fault ownership only; it is not a contract for ordinary resource
or query counts. Existing core BoundedRetainer, AtomicCapacityBudget and allocator
entry points are reuse candidates, but changing these paths requires an explicit
L2 count/byte/lifetime/failure/retry budget and all four backend conclusions first.
No hardcoded resource/query quota or silent overflow fallback is approved.
Query-native execution and exact callback retirement need their own target
evidence before closing this phase. Current successful prepared Compute
recording/submission measurements cannot certify those untested workloads or
whole-frame/platform/Production Ready behavior.

M4e5a prerequisite is Contract Complete: `docs/command-retention-contract.md`
defines independent owner/query counts, actual core metadata-byte accounting,
failure/retry/lifetime and four explicit unavailable lowering conclusions.
The optional descriptor and pure planner are implemented; explicit requests fail
before native command creation/staging mutation on all four factories. Current
capability versions remain zero. Twenty-four rebuilt regressions pass, including
atomic owner batches, bounded records, real CPU quota accounting/failure, fast
device negatives and Null hidden-frame. Vulkan source/test syntax is compile-only
evidence. M4e5 and actual backend recording/query integration remain incomplete.

M4e5b1 common storage is Contract Complete for preparation, cross-category
preflight/admission, immutable sealing and CPU ownership retirement only. Actual
metadata allocation headers retain the exact caller-owned byte quota until the
storage is destroyed. Successful admissions allocate no core objects; rejected
batches retain no partial owners. This helper grants no native completion or
query capability.

M4e5b2a removes query-vector copying from Metal submission using the existing
prepared completion control block. Exact nonempty CPU query metadata transfers
without allocation and releases pool/record references before handler destruction.
Native callback resolves first, then releases and publishes completion. Actual
Apple M4 probes draw=0, dispatch=0, stage=1: timestamp/statistics execution stays
unavailable and valid public query descriptors reject. Native nonempty query
execution and bounded ordinary recording remain pending; version zero is retained
on all four backends. This prerequisite does not establish visual acceptance.

M4e5b3a connects common preparation-owned retention to an explicitly opted-in
private Metal BufferCopy prototype. Exact registered buffer payloads are batch
preflighted before native copy; repeated copies use no core allocations. Actual
command/encoder/wait metadata is charged to the same immutable budget/quota,
sealing prevents later recording, and moved storage retires on the real native
callback before completion publication. Other recording paths fail explicitly;
ordinary texture plans and descriptor argument creation remain unbounded and
are the next integration targets. Twenty-eight rebuilt CTests pass, including
real copy bytes, quota/count/seal/state negatives, cancellation and delayed
completion with command destruction. This is Prototype Complete and scoped
Backend Validated for the private BufferCopy domain only. All four public
CommandRetentionBudget versions and Metal public Graphics remain zero.
Evidence: `build/mac-metal-debug/artifacts/bindless-prepared-graphics/retention-copy-report.yaml`.


M4e5b3c4e-c4g2 complete additional private ordinary prerequisites; these do not
advance the public Graphics or CommandRetentionBudget versions from0.
A reusable frozen owner projection now covers transitive native pipeline,
resource/device/allocation/registry and argument storage for Compute/Graphics;
canonical transfer graphs also retain original objects/tokens/placed heaps.
Private direct readonly Graphics storage uses actual per-stage ABI ranges and
frozen L2 binding metadata. Exact 4x4 A/B data, atomic owner35/48 admission,
real delayed lifetime/cancellation and accepted recording allocation0 pass.
Status: Contract Complete / Prototype Complete; scoped Backend Validated for
these actual private Metal payloads only. 29 rebuilt targets and 43 regressions,
16 Apple SDK syntax checks and quick/Null paths pass. RenderDoc is unavailable;
Platform Validated and Production Ready remain open, as do arrays/writable
storage, vertex/index, full queries, renderer/WOCC and performance phases.
The next audit finds authored vertex stride/rate/attributes are not retained by
the ordinary provider; c4h must first freeze this L2 fetch contract.
Evidence: `build/mac-metal-debug/artifacts/bindless-prepared-graphics/ordinary-storage-recording-report.yaml`;
full staged details are in `docs/command-retention-contract.md`.

M4e5b3c4h1/h2: Contract Complete / Prototype Complete; scoped Backend Validated
for private actual Metal nonindexed Vertex/Instance data/ownership/lifetime.
Authoritative selected SPIR-V input reflection (VertexInputInterface/1) replaces
source-name guessing for sealed single-entry providers, including InstanceInput.
Immutable L2 layout plus approved slots, canonical byte extents and full frozen
native owners govern preparation/atomic admission/draw validation. Owner unions
29/33/37/41, one-less capacity/quota, exact red/blue/green native readbacks,
firstVertex1/firstInstance1/2, hostile PMR/accepted allocation0, provider/pass
resets and four actual delayed completion cases pass.31 rebuilt targets,
46 regressions,4 focused tests,19 Apple SDK syntax checks,2 SPIR-V validations.
Evidence: ordinary-vertex-recording-report.yaml in prepared-graphics artifacts.
Null Validator; private Metal Native only in this proven domain; absent native
runtime and Vulkan/WebGPU bounded ordinary vertex execution DiagnosticFailure.
Public CommandRetentionBudget/MetalPreparedGraphicsBinding versions0. Indexed/
indirect need the next c4i immutable index producer/content/fetch contract;
arrays/writable storage remain closed. RenderDoc unavailable diagnostic keeps
visual acceptance false; Platform Validated and Production Ready remain open.

M4e5b3c4i1: Contract Complete / Prototype Complete for immutable typed index
content and cold selected-span bounds only. core unsigned span utility handles
unaligned Uint16/Uint32 and explicit source byte order; normalized owned bytes
feed exact selected extrema and safe signed baseVertex math. Four focused tests
including quick vertex/index contracts, Null smoke and actual Metal indexed
OPERATION-UNIMPLEMENTED gate pass; Apple SDK syntax passes. No GPU index upload
or native authority is granted. Null Validator; Metal/Vulkan/WebGPU bounded
indexed execution DiagnosticFailure, public versions0. Next c4i2 requires exact
backend-owned uploaded content/payload/seals and prepared draw admission/data/
lifetime. Evidence: ordinary-index-contract-report.yaml in prepared-graphics.
Visual/platform/Production acceptance remains open.

M4e5b3c4i2a/b/c: Contract Complete / Prototype Complete; scoped Backend Validated
for actual private Metal immutable prepared indexed upload/data/ownership/
lifetime. Canonical native transfer destinations honor write seals in both
retention modes. Dedicated IndexBuffer-only storage owns exact normalized
Uint16/Uint32 upload and all host/GPU seals. Only cold selected-span plans
mint indexed ranges; recording observes current Vertex/Instance bounds and
atomically admits complete owners37/43/49 before native draw. Actual bytes,
4x4 red/green pixels, signed bases/nonzero offsets/two instances, one-less owners/
full quota, writable usage/identity/range negatives, open/sealed cancellation
and eight actual delayed completion cases pass. Accepted record32 allocation0
under exhausted quota/hostile PMR.32 rebuilt targets,47 regressions,4 focused
tests,20 Apple SDK syntax checks pass. Null Validator; private Metal Native in
this proven domain; absent runtime and Vulkan/WebGPU bounded indexed execution
DiagnosticFailure. Public versions0; raw bounded index/indirect stay closed.
Next c4j needs actual selected-stage argument-buffer pointee facts for readonly
arrays. Writable storage, renderer/WOCC/performance/platform and visual/
Production gates remain open; RenderDoc unavailable diagnostic recorded.
Evidence: ordinary-index-recording-report.yaml in prepared-graphics artifacts.

M4e5b3c4j1/j2/j3: Contract Complete / Prototype Complete; scoped Backend
Validated for actual private Metal readonly storage and UniformBuffer arrays.
Native VS/FS pointee facts, exact root/index/count/stride and canonical element
identity govern immutable range captures; outer pointer-table layout cannot
prove pointee ranges. Storage alignment4 and Uniform alignment16 are separately
queried with32/16-byte spans. Logical readonly metadata adds const to exact MSL
device pointer arrays; writable pointers remain rejected. ConstantBuffer arrays
emit all target artifacts. Storage-array WGSL E56105 is a versioned deterministic
DiagnosticFailure, sealed SingleEntryShaderArtifact/2, with no placeholder;
v1 keeps its five-artifact contract. Three native tables48 encoded/allocated
bytes, owner44/66 unions, actual preparation6060 bytes and exact/one-less gates
pass. Exact4x4 A/B data, every-element/table native identity and freshness,
cancellation, eight total real delayed completions and accepted bind32/draw32
allocation0 pass.34 targets,51 regressions,6 focused tests,22 Apple SDK syntax
checks and4 SPIR-V validations. Null Validator; Metal private Native only in
the proven subset; absent runtime and bounded Vulkan/WebGPU execution
DiagnosticFailure. Public versions0. Next c4k must define writable Graphics
access/ranges and scope producer-consumer synchronization; readonly success
grants no writable/indirect authority. RenderDoc remains unavailable, visual/
platform/Production acceptance open. Evidence ordinary-uniform-array-report.yaml
in prepared-graphics artifacts; full contract details in command-retention docs.


C4k1/GraphicsStorageWrite/1 is Contract Complete for the initial bounded ordinary
Fragment uint32 element0 literal atomic-exchange domain and exact producer/
consumer scope only. C4k2a/FragmentAtomicWriteProof/1 is Prototype Complete for
cold allocation-free selected-binary proof, actual Slang SPIR-V/MSL/reflection
and explicit WGSL E36107 WgslGraphicsAtomicDiagnostic/1. Runtime loader recomputes
proof from sealed binary; intent/sizeof(T)/author assertions cannot grant writes.
C4k2b1 is scoped Backend Validated for actual Metal query only: direct Fragment
slot0/ReadWrite1/UInt33/alignment4/span4. Production buffer hazard query2 still
comes from default creation; separate explicit native tracked query2 is only
substrate evidence. Independent production tracking, bounded native barriers,
write data/visibility/lifetime and public versions remain unaccepted/zero.
Six focused proof/compiler/native-query/quick/Null tests pass. See
`docs/graphics-storage-write-contract.md` and prepared-graphics
ordinary-writable-proof/native-query evidence. No graphics bindless, indirect,
visual/RenderDoc/platform/Production acceptance follows.


C4k2b2 now supersedes the preceding default-hazard query gap for independent
ordinary buffers and prepared function argument storage only. One backend-owned
MetalIndependentTrackedBuffer/1 helper selects explicit tracked options, checks
actual maxBufferLength, storage/length and hazard query2, and fails with stable
diagnostics/no placeholder. Native resource observations freeze that fact;
readonly storage/Uniform array tests query every actual argument table. C4k2
is Contract Complete / Prototype Complete; scoped Backend Validated for actual
ABI/tracked creation/query and preserved readonly paths only. 37 rebuilt targets,
55 regressions including shader/quick/Null smoke,24 Apple SDK syntax checks and2
SPIR-V validations pass. Evidence: ordinary-writable-tracking-report.yaml and
logs/XML/capture diagnostic in prepared-graphics artifacts. Selected-binary proof
and explicit WGSL E36107 diagnostic remain as above. GPU bounded writable paths
and generic stat-only barriers remain DiagnosticFailure; c4k3 still needs actual
resource/dependency payload, existing recorded-state integration, write/consumer
visibility/guard/lifetime evidence. Public versions0 and indirect/RenderDoc/
visual/platform/Production gates remain unaccepted.

C4k3a now has owned ordinary writable preparation (no GPU execution authority).
HostBufferWriteSeal/1 blocks host uploads monotonically across all backends;
MetalOrdinaryGraphicsStorageWritePreparation/1 owns the actual L2 wrapper/seal,
selected sealed binary proof, exact immutable scope/dependency and native ABI/
tracked resource observations using existing quota/owner utilities. 38 rebuilt
targets,56 regressions,final7 focused checks and26 Apple SDK syntax checks pass,
including quick/Null smoke. Metadata/owner exact-one-less budgets, stale facts/
identities/seals/descriptors, cancellation and allocation0 observation pass.
Status: Contract Complete / Prototype Complete and scoped Backend Validated for
actual preparation observation only; writable execution remains four-backend
DiagnosticFailure/Null validator-only. Public versions remain0. Capture report
records unavailable RenderDoc; no writable visibility/completion, visual,
Platform Validated or Production Ready claim. Next c4k3b reuses the existing
recorded state journal and actual producer/dependency/consumer/native completion.

C4k3b1 completed the existing recorded-state prerequisite: non-mutating conflict
preflight reuses MetalRhiBuffer's embedded MutationSnapshot; same-journal enlist,
unique owner transfer, root-owned wrapper lifetime, rollback/commit and wrong
fresh producer state validate with allocation0 accepted operations. 40 build
targets,58 regressions,final5 focused quick/state/Null smoke checks and26 Apple
SDK syntax checks pass. Status: Contract Complete / Prototype Complete for
metadata only; current actual Metal preparation observation remains the scoped
Backend Validated evidence. Report ordinary-writable-state-journal-report.yaml
preserves all four execution gates/public versions closed/0 and RenderDoc
unavailable. K3b2 actual native producer/dependency/consumer/data/guards/
visibility/cancellation/delayed completion, Platform Validated and Production
Ready remain incomplete.

C4k3b2a completes the explicit private Metal atomic writer/transfer prerequisite.
One shared existing state journal commits at real native acceptance, with actual
wrappers retained through owner-thread commit. Real atomic value0x12345678,
three unchanged guards, exact offscreen pixels,32 draws/finish allocation0,
measured begin/combined owner union36/43 exact/one-less, cold quotas and native
shared-event delayed original object/control-block retirement pass. Cancellation,
poisoning, source/output identity/tracking/ABI/seals, descriptor freshness,
registry/quota/sequence/draw budget and journal conflicts fail closed. 41 targets,
59 regressions including quick/Null smoke and27 SDK syntax checks pass. Status
Contract Complete / Prototype Complete; scoped Backend Validated for this private
Metal transfer consumer only. See ordinary-writable-execution-report.yaml and
graphics-storage-write-contract.md. Public versions0 and public execution gates
remain closed. Required selected shader consumer visibility, RenderDoc visual,
Platform Validated, Production Ready and the full bindless plan remain incomplete;
continue c4k3b2b without treating transfer readback as shader visibility proof.

C4k3b2b/k3b2/k3b/k3 are now Contract Complete / Prototype Complete for the
explicit private bounded writer/consumer domain, with scoped actual Metal
Backend Validated evidence. The shared scalar SPIR-V verifier/physical observer
preserves strict writer proof and separately proves readonly Storage/Uniform
uint32 equality consumers. Actual Slang storage MSL device uint* reflects
ReadWrite1 and explicitly diagnoses; separately authored Uniform consumption
requires StorageBuffer|UniformBuffer source usage and actual ReadOnly0/UInt33/
span4/alignment4. There is no automatic fallback. Native two-render-encoder
writer/dependency/consumer produces green only for word0x12345678; word0x12345679
control is red and three guard words remain unchanged. Owner union36/59 exact/
one-less, measured cold quota, descriptor/seal/ABI/tracking/native/control-block
negatives, open/finished cancellation and retained/dropped event-delayed native
completion pass. Accepted32+32 draws/finish allocate0 at exhausted quota/hostile
PMR. Completion now exposes Pending/Completed/Failed, publishes actual command/
wait-command success and terminal precommit failure distinctly, and shares the
wait predicate mutex during notification. Actual precommit quota failure rolls
back state and accepts no GPU work. 43 targets,61 regressions,6 focused quick/
Null smoke,28 SDK syntax checks and4 new SPIR-V validations pass. Final evidence
ordinary-writable-shader-consumer-report.yaml and graphics-storage-write-contract.md.
Public versions0 and public execution gates stay closed: Null remains validator-
only; Vulkan/WebGPU/non-Apple Metal remain diagnostic failure for this private
path. RenderDoc is unavailable; visual/Platform Validated/Production Ready and
the entire bindless plan stay incomplete. Continue c4l indirect contract/owner/
fetch/producer audit; the four-byte atomic proof cannot authorize draw packets.

c4l1/c4l2a evidence: [IndirectArgumentConsumption/1](indirect-argument-consumption-contract.md)
freezes explicit immutable host arguments and exact selected fetch observations;
GPU provenance rejects with GPU-PROOF-REQUIRED. A private Metal preparation root
binds that content to its exact issuer-owned index snapshot, sealed dense native
argument upload and original canonical device/registry/payload/object/token
owners. Shared upload utility preserves indexed execution ownership. Actual
40-byte data,27 unique owners, quota/identity/state/seal/issuer/lifetime negatives,
43 target builds,62 regressions and29 SDK syntax checks pass. Contract Complete /
Prototype Complete; scoped Backend Validated for storage only. Null remains
Equivalent validator-only, all GPU current bounded indirect execution conclusions
remain DiagnosticFailure and public versions0. Continue host native recording
c4l2b and independent GPU producer/order/generation/fetch c4l3; neither storage
nor the existing scalar atomic proof grants an indirect draw ticket. Full plan,
RenderDoc visual/Platform Validated/Production Ready remain unaccepted.

Current c4l2b/l3a/l3b1 progression supersedes the storage-only execution status
above. c4l2b is Contract Complete / Prototype Complete with scoped Backend
Validated private opted-in immutable host Metal indexed-indirect recording:
32 calls/128 native records/132 command units per scenario, eight format/base/
lifetime scenarios, full-batch fetch preflight, owner37/47/57 and push40/41
exact/one-less, allocation0 and real delayed native completion/cancellation.
TriangleList disabled-restart maxUint16 index participates normally; unsupported
TriangleStrip fails at the ordinary topology gate before encoding. See
ordinary-indirect-recording-report.yaml and
[indirect-argument-consumption-contract.md](indirect-argument-consumption-contract.md).

l3a establishes the separate ComputeFixedIndirectProducer/1 binary/L2
prerequisite: exactly five ordered literal uint32 stores, LocalSize1/dispatch1,
full20-byte schema/scope/dependency and hostile binary/ABI negatives. The shared
ScalarStorageBinaryProof supports previous Fragment proofs without duplicate
instruction/type/decoration algorithms. Slang SPIR-V/MSL/WGSL/reflection YAML
and spirv-val evidence are generated. l3b1 cold private Metal preparation
compiles retained selected MSL bytes and owns exact native pipeline/library/
function/device controls: queried slot0/ReadWrite1/UInt33/alignment4/scalarSpan4
vs independently proved storeSpan20, six owners, factory peak4424 exact/one-less,
allocation0 revalidation and original-owner retirement pass. Status Contract
Complete / Prototype Complete; scoped Backend Validated for pipeline/ABI only.
47 targets,65 regressions and32 SDK syntax checks pass; reports are
ordinary-indirect-producer-proof-report.yaml and
ordinary-indirect-producer-pipeline-report.yaml. Public versions stay0.

Continue l3b2 protected GPU output, exact immutable index/stream/consumer
firstInstance ABI, exclusive recording/generation lease, native producer/end/
dependency/consumer and real completion. Neither cold pipeline nor literal
proof authorizes existing GPU bytes or packet leases. Null remains validator-
only, public GPU conclusions remain DiagnosticFailure, and c4l3/c4l/full plan
stay open. RenderDoc tools are absent; visual/Platform Validated/Production
Ready and remaining renderer/WOCC/performance/four-backend gates stay open.

### M4e5b3c4l3b2a/b1: protected output and ownership prerequisite

Status: Contract Complete / Prototype Complete. Scoped Backend Validated for
actual private Metal cold suppressed 20-byte Shared/tracked output storage only.
Numeric `GpuOutputExpectation` reuses the complete host schema/fetch analysis
without representing host upload or already-produced GPU bytes. The selected
actual Compute proof, immutable index and every sealed vertex/instance wrapper
are retained; wrapper identity uses reusable `core::SharedObjectIdentity` issued
by core allocation factories, including the existing shareUnique alias holder.
Native payload/device/object/allocation identity and original owners remain
independently checked. Shared sealed upload policy serves index, host indirect
and protected output without changing old diagnostics/owner unions.

Explicit output/expectation/stream/owner and CPU/GPU budgets pass, including
actual remaining GPU20/19-byte admission; child projection78 and measured cold
CPU peak4504 exact/one-less. Ordinary host/GPU descriptor writes and nonzero
suppressed-storage initialization reject. Actual initial output is all-zero;
expected producer words3/2/1/fffffffe/1 remain separate. Original wrappers and
native object/allocation owners survive field replacement and retire on final
root release; final CPU quota0 and validation allocation0 pass.

The ownership-only move lease binds the issued output generation and canonical
factory-issued real command under matching quota/registry. Second writer,
foreign/raw/alias/closed/stale/cross-command observations fail; moves/cancellation
and weak command retirement pass without an ownership cycle or allocation.
There is no approved recorder or native completion transfer yet. Consumer
firstInstance ABI remains unproven; b2b/b2c/l3 and the full plan stay open.

48 targets,66 regressions (quick contract/core unit/Null hidden frame/native cold
path),35 SDK syntax checks pass. Evidence is in
`build/mac-metal-debug/artifacts/bindless-prepared-graphics/ordinary-indirect-output-preparation-report.yaml`
and corresponding logs/XML/capture diagnostic. Public versions stay0; Null is
validator-only and all GPU public lowering gates remain DiagnosticFailure.
RenderDoc tools remain unavailable on this host; no visual/platform/Production
Ready/full-plan closure.

Final ownership audit additionally rejects unrelated alias control blocks for
producer/index/vertex prepared roots and their provider. The output root and
native command also reuse the same core identity primitive, with no local weak
owner implementation. Core factory publication allocates nothing; inline weak
metadata changes the current producer pipeline cold peak from its prior phase
snapshot4424 to4440. The current output peak stays4504 and childOwners78; the
latest regression XML retains the measured values. Historical phase reports
remain evidence for their corresponding earlier implementation snapshots.

### M4e5b3c4l3b2b/c: selected instance ABI and protected native execution

Status: Contract Complete / Prototype Complete; scoped Backend Validated for
private opted-in selected Metal instance ABI and fixed-producer indexed-indirect
sequence only. VertexLocationFloatInput/1 proves pure float locations in the
actual selected binary; InstanceAttributeFetch/1 binds exact queried functions,
PerInstance/stepRate1 descriptors and immutable stream ownership. Native
firstInstance controls remain separate from producer proof.

The protected recorder consumes one canonical cold root, exact L2 scopes and
dependency, complete fixed-word fetch proof and finite nine-command budget.
It admits projection168/union59 before native mutation, then performs one real
Compute dispatch and one real indexed-indirect record using the GPU-produced
20-byte output. It uploads no expected argument words and replays no CPU count.
Record/finish/no-wait submit allocate zero core objects at exhausted quota.
The current command/root/output cold peaks are13192/6464/4512.

GpuIndirectOutputLifetimeContract/1 defines the lifecycle before native code:
Submitted requires actual commit ownership; completion requires the real native
callback. The move-only exclusive generation lease transfers into prepared
submission retention. Native completion releases it even when callers keep the
root; CPU completion flags cannot release it. A submit rejection discards the
recorded stream, rolls back its journal and releases the unconsumed lease;
fresh command retry is allowed. Once submitted, the generation cannot reopen.
Replaced observation fields cannot retire original recorded buffers/functions/
allocation tokens before completion. Cancellation and command/root destruction
combinations have real event-delayed ownership evidence.

51 targets,69 regressions, four focused tests,39 unique SDK syntax checks and
two new no-op spirv-val modules pass. Evidence:
`build/mac-metal-debug/artifacts/bindless-prepared-graphics/ordinary-indirect-execution-report.yaml`.
Null remains Equivalent validator-only; Vulkan/Metal/WebGPU public payload gates
remain DiagnosticFailure/version0. RenderDoc executables/Python are absent,
so visual/Platform Validated/Production Ready/full bindless plan remain open.
Continue fixed-producer boundary coverage and separate public/four-backend,
renderer/WOCC, query and whole-frame performance prerequisites.

b2d boundary coverage is now Contract Complete / Prototype Complete, scoped
Backend Validated for selected fixed-literal/immutable-fetch Metal only. Sixteen
additional positive-base/zero-first-instance native cases pass exact green/red
pixels and all five GPU words across both index formats and four lifetimes.
Four valid selected binary/native producer pipelines with unsafe fetch counts
reject before output allocation, leaving CPU/GPU quota unchanged. 51 targets,
69 regressions, three focused proof/native/Null tests, three changed test syntax
checks and four new spirv-val modules pass. See
`build/mac-metal-debug/artifacts/bindless-prepared-graphics/ordinary-indirect-boundaries-report.yaml`.
Fixed literal proof does not authorize the source-reading canonical packet
processor; its complete producer/consumer/fault correspondence remains a
separate contract and native acceptance prerequisite.

2026-10-03 continuation: OriginalTextureUploadNative/1 completes the selected
private original upload profile, including frozen per-mip decisions, two-mip
array/nonzero-origin and 3D real target bytes, restricted Submitted/Completed
content transitions and existing common completion/error/loss retention. Status:
Contract Complete / Prototype Complete / Backend Validated for selected WebGPU
uncompressed copies only. Queried BC feature false is explicitly Degraded before
creation; BC native execution is not validated. No ordinary exclusive guard,
proof issuer or public/general/bindless/NativeSync capability is broadened.

OriginalTextureReadbackInput/1 is now Contract Complete / Prototype Complete for
exact CPU scalars/ordered mips/labels only; native staging/result ownership is the
next f3b prerequisite. Host/Null10/10, Wasm/browser5/5 and source inventory28/28
pass with clean diagnostics in both browser feature modes. See
`build/web-bindless-apple-debug/artifacts/bindless-continuation/original-texture-native-report.yaml`
and `original-readback-input-report.yaml` in that directory. RenderDoc remains
RHI-RENDERDOC-TOOL-UNAVAILABLE; Platform Validated, Production Ready and the full
bindless plan remain incomplete.

2026-10-04 continuation: the named `TextureReadbackOwnedPreparation/1` WebGPU
Prototype is scoped Backend Validated for one original GpuDevice context,
full cold plan/CPU domains, hot original recording/submission/map, exact detached
CPU bytes/statistics, callback-safe cancel/drop/error/loss/shutdown, concurrent
requests and cross-logical-device identity/source isolation. Static unconfigured
lowering stays closed; actual context resolves the same L2 decision consumed by
availability and preparation. Borrowed WebGPU/Vulkan execution is explicitly
retired with stable diagnostics and both unbounded global result queues removed;
Metal's original explicit bounded Legacy remains separately validated. Numerical
ABI compatibility does not claim borrowed native lifetime equivalence.

Host/Null10/10, both Wasm/browser modes5/5 and source inventory89/89 pass with
success=true/diagnostics=[]/nativeErrors=[]. Vulkan removal/direct dependent TUs
syntax-check; no Vulkan native/platform claim. Evidence is in
`build/web-bindless-apple-debug/artifacts/bindless-continuation/original-readback-owned-public-device-isolation-validation.yaml`
and the corresponding YAML/log/XML archives. Full generic/tracked/query, indexed
public execution, NativeSync, RenderDoc/platform and Production Ready gates remain
open; the named Prototype grants none of those capabilities.

Generic original-source continuation (2026-10-04): core ColdOwnedObjectSlot/1,
GenericOriginalCommandAdmission/1 L2 requirements/closed lowering and typed
OriginalCommandResourceLease/1 Buffer/Texture CPU lifetime prerequisites verified.
Latest host/Null10/10, Wasm/browser6/6, inventory92/92; both actual reports clean.
See [generic admission contract](generic-original-command-admission-contract.md).
Contract Complete / Prototype Complete is scoped to those CPU prerequisites; full
retained graph, original registry/state/recording/submission/completion, four-backend
execution, platform and Production Ready remain pending. Selected owned readback
Prototype preserves its existing native evidence without generic capability promotion.

Further generic continuation: immutable typed Source inventory and ordered
OriginalCommandUsage/1 CPU ownership sequence verified. Latest host/Null10/10,
Wasm/browser6/6, inventory94/94; both genericOriginalUsagePassed=true, reports
clean. CommandUsageSequenceContract reuses core OrderedSpanCursor and preserves
existing WebGPU diagnostics. Contract Complete / Prototype Complete CPU graph
prerequisites only; exact scalar/typed input, registry/state/completion and full
generic backend proof remain pending. No capability promotion.

OriginalCommandScalars/1 exact original usage/variable-store binding now verified:
host/Null10/10, Wasm/browser6/6, inventory95/95; both actual reports clean. Shared
scalar-kind predicate preserves existing WebGPU validation/diagnostic order.
Contract Complete / Prototype Complete CPU field ownership only; full typed
descriptor, original registry/state/completion and generic backend admission remain
pending. Evidence `generic-original-scalars-*`; no base capability promotion.

OriginalCommandInputs/1 positional CPU single-mip copy/dependency binding verified:
host/Null10/10, Wasm/browser6/6, inventory96/96; both actual reports require
genericOriginalInputsPassed=true and remain clean. Exact scalar/source quota,
complete stored fields and incoming typed wrappers, independent owners/descriptors,
cold pressure/caller-drop and hot allocation0 pass. Missing pass/upload/readback
adapters and explicit mips reject. Contract Complete / Prototype Complete CPU
field binding only; full state/completion/native gates remain closed. Evidence
`generic-original-inputs-*`; no broader capability or production promotion.

TransferCopyInputSnapshot/1 complete original scalar/mip CPU binding verified:
host/Null10/10, Wasm/browser6/6, inventory97/97; both reports require
genericOriginalCopyMipsPassed=true and all preceding graph markers remain clean.
Every explicit mip field/order/count is retained under original Graph quota;
per-snapshot/aggregate mapping bounds and distinct-owner projection pass, including
reused snapshot owner dedup with per-command mapping charges. Cold pressure and
caller-drop/hot allocation0 pass. Pointer-free capture still rejects explicit
mips; the complete snapshot has no such truncation. Contract Complete / Prototype
Complete CPU fields only; pass/upload/readback sources, full state/completion and
four generic backend execution remain closed. Evidence `generic-original-copy-mips-*`.

Original TextureView source/parent CPU lifetime prerequisite verified:
host/Null10/10, Wasm/browser6/6, inventory98/98; both actual reports require
genericOriginalViewSourcePassed=true and all preceding graph markers remain
clean. Existing lease/core cold slot retains original view and canonical parent
with full fields/quota; table requires that same parent and graph counts its
closure once. Cold pressure, caller-drop and hot allocation0 pass. Full pass
input/state/completion/native gates remain closed. Contract Complete / Prototype
Complete CPU lifetime only; evidence `generic-original-view-source-*`.

Original canonical RenderPass input CPU binding verified: host/Null10/10,
Wasm/browser6/6 after one targeted Chrome-profile cleanup retry, inventory99/99;
both actual reports require genericOriginalPassInputsPassed=true and all prior
graph markers remain clean. Shared WebGPU/L2 color/resolve/depth ordering and
failure-preserving category consumption, exact Graph snapshot/view/parent, full
field comparison, independent aggregate capacities and owner dedup pass. Cold
rollback and caller-drop/hot allocation0 pass. Contract Complete / Prototype
Complete CPU fields only; four generic execution/state/completion gates remain
closed. Evidence `generic-original-pass-inputs-*`, including initial harness
cleanup failure and successful unchanged-source retry log/XML.

Original upload snapshot quota provenance and canonical TextureUpload CPU input
binding verified 2026-10-04: host/Null10/10, Wasm/browser6/6, inventory101/101.
Both reports require originalUploadInputQuotasPassed,
genericOriginalTextureUploadInputsPassed and sharedIdentityNamespacePassed=true;
all preceding graph markers true, diagnostics/nativeErrors empty. Exact Source
pixel/destination and Graph metadata/label owners, required destination entry,
CPU key namespace, full fields/positional refs and per-command mip/pixel/label
bounds pass. Source closure is usage7/scalars9/input10 for one shared upload used
twice; cold rollback/caller-drop and exhausted hot allocation0 pass. Shared layout
mapper is reused; no fake GPU ID or staging/generation authority exists. Initial
fixture lacked transfer scope and was rejected by the existing order contract;
corrected fixture passes, production gate/budgets unchanged. Contract Complete /
Prototype Complete CPU binding only; generic registry/state/native completion
and all four execution gates remain closed. Evidence texture-upload-original-
input-quotas-* and generic-original-texture-upload-inputs-* YAML/log/XML.
RenderDoc remains RHI-RENDERDOC-TOOL-UNAVAILABLE; no Backend Validated, Platform
Validated or Production Ready promotion. BufferUpload/readback adapters pending.

Original BufferUpload CPU declaration/source binding verified 2026-10-04:
host/Null10/10, Wasm/browser6/6, inventory102/102; both reports require
genericOriginalBufferUploadInputsPassed=true and all preceding graph markers
true, diagnostics/nativeErrors empty. Exact source/destination Buffer table
entries, canonical frozen scalar/label Source/Graph ownership, full incoming
fields/refs, independent byte/label/descriptor/owner caps and original caller-drop
pass. Closure is usage8/scalars10/inputs11; cold exact-less-exhausted rollback and
hot exhausted allocation0 pass. UploadBufferDesc has no CPU byte pointer; this
binder invents no byte copy or native staging initialization. Contract Complete /
Prototype Complete CPU upload adapters only; registry/state/native generation/
recording/completion and four generic gates remain closed. Readback full adapters
are next. Evidence generic-original-buffer-upload-inputs-* YAML/log/XML; no Backend
Validated, Platform Validated or Production Ready promotion.

Original readback full-input quota provenance prerequisite verified 2026-10-04:
host/Null10/10, Wasm/browser6/6, inventory103/103; both reports require
originalReadbackInputQuotasPassed=true and all upload/previous graph markers
true, diagnostics/nativeErrors empty. Immutable original metadata/label budgets,
canonical original snapshot/label owners and complete mip/label bounds pass.
Forged/changed-limit/equal-owner comparisons reject; cold rollback/caller-drop and
exhausted hot allocation0 pass. Contract Complete / Prototype Complete CPU quota
prerequisite only; full readback Request/layout/source/destination closure and
state/completion/map/publication remain pending. Evidence texture-readback-original-
input-quotas-* YAML/log/XML. Four generic execution gates remain closed; no Backend
Validated, Platform Validated or Production Ready promotion.

Existing original readback CPU ownership-chain quota revalidation verified
2026-10-04: host/Null10/10, Wasm/browser6/6, inventory104/104; both reports
require originalReadbackOwnershipQuotasPassed=true and all prior graph markers
true, diagnostics/nativeErrors empty. Layout/Request/source lease keep immutable
original budgets; five exact quota limits and complete canonical chain validate.
Shared deterministic mip mapper rejects frozen mapping corruption; forged/equal
owner substitutions and changed limits fail. Failed cold adoption preserves caller
UniquePtr; allocation-free adoption retains the same single original texture,
caller-drop and hot exhausted allocation0 pass. Contract Complete / Prototype
Complete existing CPU ownership-chain scope only; typed Source/Graph table and
readback positional/destination closure remain pending. Evidence texture-readback-
original-ownership-quotas-* YAML/log/XML. Full registry/state/native recording/
completion/map/publication and all four generic gates remain closed; no Backend
Validated, Platform Validated or Production Ready promotion.

Canonical original readback typed source/input binding verified 2026-10-04:
host/Null10/10, Wasm/browser6/6, inventory105/105; both reports require
genericOriginalTextureReadbackInputsPassed=true and all preceding graph markers
true, diagnostics/nativeErrors empty. Existing adopted source lease and exact
Request/layout/input/label remain original; no second source UniquePtr exists.
Exact Source/Graph relationships, source/destination table dependencies, CPU
Request key namespace and usage12/scalars14/input15 closure pass. Two commands
consume mip4/label16/prospective-result1536 bounds independently; full fields/
Texture+Request refs, forged/foreign/equal-owner/range/quota negatives and cold
rollback/caller-drop/hot allocation0 pass. Original allocation identity/quota/size
preserved. All seven CPU input families now have complete source/field binding
for supported original sources; unsupported source domains fail at signature.
Contract Complete / Prototype Complete CPU input scope only. Full original
logical device/registry/state/completion and native execution remain pending;
public state() values cannot substitute for revision/generation/range ownership.
Evidence generic-original-texture-readback-inputs-* YAML/log/XML. Null remains
ValidatorOnly and Vulkan/Metal/WebGPU DiagnosticFailure for generic execution;
no Backend Validated, Platform Validated or Production Ready promotion.

Original logical state contract verified 2026-10-04: host/Null10/10,
Wasm/browser6/6, inventory106/106. Both genericOriginalStateContractPassed and
preceding graph flags true, diagnostics/nativeErrors empty. All15 source
requirements and independent intent bounds explicit; scalar authority rejects,
four original issuer/execution gates remain closed, exhausted hot allocation0.
Contract Complete requirements/closed lowering only. Evidence generic-original-
state-admission-contract-* YAML/log/XML. Device-owned registration/control/range/
completion/native graph remains pending; no Backend Validated, Platform Validated
or Production Ready promotion.

OriginalDeviceLifetime/1 verified 2026-10-04: host/Null10/10, Wasm/browser6/6,
inventory107/107. Both originalDeviceLifetimePassed and prior graph flags true,
diagnostics/nativeErrors empty. Actual L2 devices own sealed exact-quota two-owner
core epoch domains. Weak exact/foreign/retired/destroyed tickets, mutable-quota
rejection, cold exact/one-less/exhausted rollback and one-shot preparation pass;
exhausted hot issuance/copy/validation/retirement allocation0. Metal compilation
passes; current host has no Vulkan target/SDK. An earlier Chrome temporary-profile
cleanup race occurred after successful reports; failure evidence retained and
final-code rerun6/6 passes. Evidence original-device-lifetime-* YAML/log/XML.
Contract Complete / Prototype Complete logical CPU lifetime only. Four explicit
Equivalent logical lifetime mappings grant no original resource/state/native-loss
or GPU authority; unknown backend fails. Factory registration, original controls,
range ledgers, native loss integration and complete graph binding remain pending.
No Backend Validated, Platform Validated or Production Ready promotion.

OriginalResourceRegistration/1 verified 2026-10-04: host/Null10/10,
Wasm/browser6/6, inventory108/108. Both originalResourceRegistrationPassed and
prior graph flags true, diagnostics/nativeErrors empty. Actual Buffer/Texture/
TextureView factory returns, including supported placed Textures, consume one
sealed L2 registration policy across all four backend sources. Exact base object,
original ID, independent monotonic lifetime generation, canonical original device
and exact registered view parent bind; dead parents/resources and retired/destroyed
devices invalidate tickets. Independent kind/owner/exact quota limits, cold exact/
one-less/exhausted rollback, changed quota/foreign/unregistered/preexisting/equal-ID
negatives, unchanged original allocation charges and hot allocation0 pass. The
canonical Source table revalidates layouts/dependencies and all7 source variants'
GPU records; existing upload/readback fixtures also run on registered actual
devices. Metal compilation passes; Vulkan target/SDK absent on current host.
Evidence original-resource-registration-* YAML/log/XML. Contract Complete /
Prototype Complete typed logical lifetime/source binding only. Registry metadata
has an independent bounded owner closure, but complete state admission still must
count the union with the CPU input graph: Source fixture10 plus records4/fixed3
is17. Existing Source/input owner counts alone do not prove that combined bound.
Original controls/range revisions, pending journals, native loss observation,
completion and full execution remain pending/closed; no Backend Validated,
Platform Validated or Production Ready promotion follows this CPU prerequisite.

Original committed Buffer control /1 verified 2026-10-04: host/Null10/10,
Wasm/browser6/6, inventory109/109. Both originalBufferStatePassed and prior graph
flags true, diagnostics/nativeErrors empty. Null/Vulkan/Metal original Buffer
creation/write boundaries use shared versioned control; WebGPU binds its existing
control only with actual native Buffer and device. Sealed weak observations bind
exact canonical registration/generation/control and committed state/revision.
Pending/foreign/stale/unregistered/dead/retired/illegal-state negatives, original
journal preparation/no-write commit/rollback ABA/destruction detach and exhausted
hot allocation0 pass on actual Null/WebGPU resources. Metal compiles but test
exit0 is availability-gated, with no fixture marker; Vulkan target/SDK absent.
Evidence original-buffer-state-* YAML/log/XML. Contract Complete / Prototype
Complete committed Buffer scope only. Exact frozen Source/input binding, combined
registry/record owner union, Texture ranges, retained pending transaction, native
loss/completion and full execution remain pending. Four full state/generic gates
remain closed; no Backend Validated, Platform Validated or Production Ready.
RenderDoc diagnostic: RHI-RENDERDOC-TOOL-UNAVAILABLE.

Original committed Buffer input graph /1 verified 2026-10-04: host/Null10/10,
Wasm/browser6/6, inventory110/110. Both originalBufferStatesPassed and all prior
flags true, reports clean. The graph retains exact original input shared ownership,
canonical device and every canonical Buffer committed registration/control/revision.
Source10+records4+fixed3=17; Buffer input9+root1+records2+fixed3=15; repeated upload
input11+root1+records2+fixed3=17, with no repeated GPU record charge. Independent
observation/registration/owner/exact state quota, forged/equal-fact foreign graph
and device, unsupported Texture sources, pending/stale/rollback ABA/commit changes,
caller-drop/shutdown and exhausted hot allocation0 pass. Cold exact/one-less/
exhausted tests pass. Initial accessor compile and fixture peak-order failures are
preserved and repaired. Metal compiles but availability-gated fixture did not run;
Vulkan target/SDK absent. Evidence original-buffer-states-* YAML/log/XML.
Contract Complete / Prototype Complete committed Buffer graph scope only.
Normalized Texture controls/ranges, retained pending transaction, native loss,
completion and full execution remain pending; all four full state/generic/native
capability gates stay closed. No Backend Validated, Platform Validated or Production
Ready promotion. RenderDoc: RHI-RENDERDOC-TOOL-UNAVAILABLE.

Original Texture state range geometry /1 verified 2026-10-04: host/Null10/10,
Wasm/browser6/6, inventory111/111. Both originalTextureStateRangePassed and all
prior flags true, reports clean. Reusable core checked row-major grid plus existing
format/layout/aspect validators derive exact canonical mip/layer/aspect cells.
Overlapping ranges share cells; Depth/Stencil are independent; cube faces are
counted once and 3D depth does not create array layers. Empty/overflow/unknown-format/
aspect/range/cell-capacity checks and exhausted hot allocation0 pass. Initial
fixture immutable assignment/BC7 enum build failures preserved and repaired.
Metal compiles but actual fixture availability-gated; Vulkan target/SDK absent.
Evidence original-texture-state-range-* YAML/log/XML. Contract Complete geometry
only; normalized original Texture controls, registration observations and frozen
input graph binding remain pending. Four full original-state/generic issuers stay
closed and WebGPU partial barrier gate unchanged. Creation-state mapping audit
requires original Null/Metal descriptor state, Vulkan Undefined/native-acquired
state and existing WebGPU payload control, never a second control seeded from
state(). No Backend Validated, Platform Validated or Production Ready promotion.
RenderDoc: RHI-RENDERDOC-TOOL-UNAVAILABLE.

Original normalized Texture control /1 verified 2026-10-04: host/Null10/10 plus
extra5/5, Wasm/browser6/6, inventory112/112; both originalTextureControlPassed and
required prior flags true, reports clean. Actual normalized Null/Vulkan/Metal
creation/write boundaries and, at that historical checkpoint, actual WebGPU uniform
native payload control (superseded below by NormalizedTextureStateCreation/1)
are bound to sealed weak factory registration/control/range/revision observations.
Core VersionedMutationArray allocates exact descriptor-derived states and equal
snapshot once; original journal, mixed/overlapping/independent logical planes,
rollback ABA, no-write commit/conflicts, destruction/retirement, cold exact-less-
exhausted ledger rollback and hot allocation0 pass. Any write conservatively
invalidates all ranges. WebGPU partial and independent Depth24Stencil8 observations
reject. Metal compiles, actual fixture availability-gated; Vulkan pinned official
v1.4.350 header syntax passes only. Initial return-type and diagnostic assertions
repaired with logs retained. Evidence original-texture-control-* YAML/log/XML.
Contract Complete / Prototype Complete logical control only; graph binding,
pending transactions/native loss/completion/execution remain pending. No Backend
Validated, Platform Validated or Production Ready promotion. Full issuers4 remain
closed; RenderDoc: RHI-RENDERDOC-TOOL-UNAVAILABLE. Continue exact frozen Texture/View
range/input ownership binding.

Original committed resource input graph /1 verified 2026-10-04: host/Null10/10
plus extra5/5, Wasm/browser6/6, inventory113/113. Both originalResourceStatesPassed
and required prior flags true, reports clean. Ordered allocation-free shared RHI
projection binds explicit copy declarations (including exact full-mip owner),
barriers, whole memory/alias/retain intents, exact view-parent ranges and original
upload/readback inputs to original registered controls. Mixed Buffer1/ranges5 graph
owns input16+root1+records4+fixed3=24; whole intent graph22; repeated upload15 and
readback21. Independent Source/state range/observation, registrations, ownership
and CPU domains, cold exact-less-exhausted, foreign/equal-input/forged owners,
invalid/partial/view mutation, pending/stale/rollback ABA/commit/caller-drop/shutdown
and exhausted hot allocation0 pass. Fixture initialization and peak-measurement
order failures repaired with original logs/XML retained. Metal compiles but actual
fixture availability-gated; pinned Vulkan header syntax only. Evidence original-
resource-states-* YAML/log/XML. Contract Complete / Prototype Complete committed
logical graph only; copy geometry, native plane access, retained pending journals,
native loss/completion and complete execution remain independent obligations.
Four full issuers/generic/bindless/NativeSync remain closed. No Backend Validated,
Platform Validated or Production Ready promotion. RenderDoc tool unavailable.
Continue exact original device native-loss binding; audit shows private WebGPU
loss subscriptions and Vulkan/Metal completion errors cannot prove canonical
public L2 domain invalidation.

Original L2 device native-loss subscription /1 verified 2026-10-04: host/Null10/10
plus extra5/5, Wasm/browser6/6, inventory115/115; both originalDeviceNativeLossPassed
and required prior flags true, clean reports. Sealed weak original receiver,
NativeLost terminal epoch, independent explicit registration1/callback1/owner1
and exact CPU quota, before-registry one-shot binding and backend-owned native
source are implemented. Actual WebGPU subscription cold peak108 exact-less-
exhausted/retry/failure compensation, changed quota/count/owner/detach/destruction
and factory parent native-owner negative gate pass. Real unrelated device loss
leaves the original domain active. Actual browser GPUDevice.destroy/lost revokes
the same original domain, Buffer/Texture/View registrations, committed observations
and pending graph owner20; rollback cannot revive authority. Failed/detached/
destroyed subscriptions safely handle late delivery; six quotas finally0. Hot
validation/detach/rollback allocate0 and delivery code has no allocation. Initial
legacy emdawn header, immutable fixture and ownership split failures repaired;
original logs preserved. Vulkan pinned header syntax only; canonical native binding
is explicit DiagnosticFailure. Metal compiles/availability-gated, binding explicit
DiagnosticFailure; Null ValidatorOnly rejects native binding. WebGPU exact native
subscription has actual browser evidence, without broader Backend Validated,
Platform Validated or Production Ready promotion. Full original/generic/bindless/
NativeSync gates remain closed. Evidence original-device-native-loss-* YAML/log/XML.
RenderDoc tool unavailable. Continue one-shot original registered control/domain
binding and terminal active/prepare/write/pending observation gates; complete
retained transaction quarantine/completion/execution remains pending.


Original registered state-domain checkpoint /1 verified 2026-10-04: host15/15,
Wasm/browser6/6, inventory117/117; both originalStateDomainPassed and preceding
mandatory flags true, reports clean. Genuine publication binds one immutable
embedded weak original domain ticket to existing scalar/normalized/uniform
controls. Retirement/destruction/real captured WebGPU loss closes active,
prepare/write/committed/pending/matching gates; explicit rollback cannot revive
original authority. Reusable sealed array pending stamp and normalized range
observation allocate0; actual legacy barrier diagnostic strings are checked
outside exhausted quotas and have no allocation0 certification. Logical negative
publication fixtures certify no native payload. Owner counts24/22/15/21 and loss
graph20 unchanged; native six quotas retire to0. Evidence
`build/web-bindless-apple-debug/artifacts/bindless-continuation/original-state-domain-evidence.yaml`
and corresponding YAML/log/XML. Contract Complete / Prototype Complete checkpoint
only. Vulkan official-header syntax passes; Metal compiles/availability-gated;
four full execution issuers remain closed. No broad Backend Validated, Platform
Validated or Production Ready; RenderDoc tool unavailable. Continue complete
retained original transaction, quarantine and exact completion ownership.


Original pending enlistment epoch /1 verified 2026-10-04: a red regression first
proved prepare/no-write-commit/reprepare could revive old pending observations.
Reusable core scalar state now seals a non-recycling enlistment epoch separate
from value revision. Arrays and normalized Texture ranges consume that exact
stamp. Same-session prepare remains stable, no-write committed revision stays
unchanged, same/recycled journal address rejects old evidence; zero/last/exhausted
new enlistments refuse before value/snapshot mutation, direct writes remain
independent. Stable resource/query enlistment-exhaustion diagnostics distinguish
revision exhaustion. Core quick and host15/15, Wasm/browser6/6, inventory119/119;
both pendingEnlistmentEpochPassed and prior mandatory original state/loss flags
true, reports clean. Evidence
`build/web-bindless-apple-debug/artifacts/bindless-continuation/original-pending-epoch-evidence.yaml`
and red/final logs, reports and XML. Contract Complete / Prototype Complete pending
identity prerequisite only; original retained transaction still pending. Its
module/ownership audit is `original-retained-state-transaction-audit.yaml`: private
WebGPU native transfer/readback States and borrowed caller journals cannot replace
the canonical registered L2 graph. Continue canonical transaction root, normalized
ordered range preflight, native admission, quarantine and exact completion retention.
All four full issuers/generic/bindless/NativeSync remain closed; Vulkan syntax,
Metal compilation and selected browser proofs do not promote broad Backend
Validated, Platform Validated or Production Ready. RenderDoc tool unavailable.


Canonical original state transaction ownership /1 verified 2026-10-04: host15/15,
Wasm/browser6/6, inventory121/121; both originalTransactionOwnershipPassed and
prior mandatory original state/loss flags true, reports clean. Root retains exact
canonical graph and embedded journal; genuine factory controls bind mutable access
privately, native recordedState getters remain read-only. Frozen Source live-root
count uses original owner alias, no second count-domain allocation/owner. Mixed
control3/observations6/union25 cold peak900 Wasm, exact-less-exhausted/count1 shared
graph versions, independent Recording/range/registration/owner limits, foreign/
forged/quota tests pass. Initial pending snapshots, foreign writer rejection,
no-write CPU commit, abandonment/caller-drop/destruction and genuine shutdown
allocate0. Real captured WebGPU loss revokes canonical pending root control1/
union13 after caller graph drop, commit rejects, rollback cannot revive; unrelated
loss isolation and quotas7 zero. Previous committed unions24/22/15/21 and loss
graph20 unchanged. Evidence
`build/web-bindless-apple-debug/artifacts/bindless-continuation/original-transaction-ownership-evidence.yaml`
plus reports/logs/XML. Contract Complete / Prototype Complete CPU ownership only;
complete supported ordered scalar/normalized/uniform state/access preflight and
final publication, native admission/quarantine/exact completion remain open.
All four full execution issuers/generic/bindless/NativeSync stay closed. Vulkan
syntax, Metal compilation/availability-gating and selected browser proof do not
promote broad Backend Validated, Platform Validated or Production Ready. RenderDoc
unavailable. Continue exact normalized cell/range sequencing with explicit budgets.


Canonical original state sequence /1 and canonical readback access prerequisite
verified 2026-10-04: host15/15, Wasm/browser6/6, inventory123/123; both
originalStateSequencePassed and originalStateSequenceReadbackPassed true, prior
mandatory original state/ownership/loss flags true; reports clean. The exact
original graph supplies initial values, explicit expanded-cell/step/command/debug
limits precede state-plan allocation/mutation, scalar sequence resolver filters
barriers by canonical mip/layer/aspect cells. Normalized overlap finals15/14 and
uniform21/20, scope/before/one-less/range failures, valid transfer-to-attachment
state intents, erroneous original ownership-only copies and attachment state
rejects pass. Reverse grid/aspect tests and exhausted-quota dry scans allocate0;
no original state/revision/control is replaced or mutated. Readback reuses existing
registered destination closure, checks source CopySrc/destination CopyDst, charges
hidden writes; cells5/steps6, one-less step and earlier destination transition
negative tests pass. Four full original/generic/bindless/NativeSync gates stay
closed; native proof friends exactly two. Evidence
`build/web-bindless-apple-debug/artifacts/bindless-continuation/original-state-sequence-evidence.yaml`
plus reports/logs/XML. Contract Complete / Prototype Complete CPU prerequisites
only. Complete mutable plan/final evidence/journal application, copy-layout and
full pass semantics, native admission/quarantine/completion remain open. Alias
without original memory binding and unsupported subpass/read-only/stencil paths
fail deterministically. Vulkan pinned-header syntax only; Metal compiles but
actual-device fixture remains availability-gated; RenderDoc unavailable. No broad
Backend Validated, Platform Validated or Production Ready promotion. Continue
same-root bounded immutable plan storage and exact planned-state observations.


PortableOriginalCommandAccess/1 checkpoint (2026-10-04): Contract Complete /
Prototype Complete CPU preflight. Host15/15, Wasm/browser7/7, inventory125/125;
both browser modes originalAccess/upload/readback flags true, reports clean.
The authored portable opt-in requires 4-byte Buffer copies and the existing
256-byte row-pitch contract, explicit complete ordered mip descriptors and full
padded footprints (not just last-row useful bytes). This deliberately refuses
some native-valid minimally padded buffers; no native equivalence is inferred.
Compressed 3D, layered/subpass/read-only/stencil passes, Present without an
original swapchain closure and alias without original memory binding reject.
Uniform WebGPU controls retain whole-range requirements; separate dual-mip copy
and single-mip pass fixtures cover access semantics without widening controls.
Shared upload/readback packing and exact frozen mip projection are reused.
All-controls write-count plus one rollback revision admission is read-only and
requires external serialization. Null validator and Vulkan/Metal/WebGPU share
CPU validation; all four full execution/issuer decisions remain closed. This
checkpoint grants no Backend Validated, Platform Validated or Production Ready
status and changes no shader or visual output; RenderDoc unavailable.
Evidence: build/web-bindless-apple-debug/artifacts/bindless-continuation/
original-access-evidence.yaml. Continue same-root immutable plan and final seals.


OriginalCpuStatePlan/1 checkpoint (2026-10-04): Contract Complete / Prototype
Complete CPU plan only. Host15/15, Wasm/browser7/7, inventory126/126 and both
originalPlanPassed=true; reports clean. The canonical transaction embeds bounded
immutable ordered access/transition intent and unique final mip/layer/aspect cells
under its existing Recording quota, with no new shared owner or state control.
Complete access scanning and per-control writes-plus-one-rollback admission precede
cold storage and application. Each barrier writes its exact range once; three
barriers consume three revisions, rollback one. Overlapping normalized final
ranges can be nonuniform; exact per-cell pending/committed seals validate the
embedded journal/enlistment epoch/revision rather than stale initial graph values.
Scalar Texture controls require the exact default range and reject combined
Depth24Stencil8 aspect planning. CPU apply/commit are explicitly Prototype
methods; commit proves no GPU order, visibility, submit or completion. Abandon
retains the Source count until root destruction. Actual WebGPU loss with an
applied original Buffer transition preserves pending ownership until abandon,
refuses commit and cannot revive the original domain; retire/shutdown agree.
Cold exact/one-less/exhausted and independent bounds, uploads/readbacks/four copy
families/pass/normalized overlap, caller-drop and exhausted-hot allocation0 pass.
Null remains ValidatorOnly; Vulkan/Metal/WebGPU full native execution/issuer remain
DiagnosticFailure. Existing private WebGPU native transactions are separate graphs
and cannot substitute for this canonical root. Next: version exact native closure
and recording/admission/quarantine/completion contract before backend integration.
No Backend Validated, Platform Validated or Production Ready promotion. Selected
affected targets build/pass; extra all-target build fails existing Unity importer
DirectNative CFG proof. No shader/visual changes; RenderDoc unavailable.
Evidence: build/web-bindless-apple-debug/artifacts/bindless-continuation/
original-plan-evidence.yaml and original-plan-native-boundary-audit.yaml.


CanonicalOriginalNativeClosure/1 checkpoint (2026-10-04): Contract Complete /
Prototype Complete resource closure only. Final host15/15, Wasm/browser7/7,
inventory127/127; both canonicalNativeClosurePassed/originalPlanPassed true,
prior mandatory markers retained, reports clean. Backend-private immutable closure
owns the exact canonical OriginalCommandStateTransaction and its original factory
registry observations, registration generations, native shared owners/objects/
devices/layouts/allocation tokens. Hot registry validation uses nonblocking existing
adapters. No reconstructed registry, second graph/control/journal or recorded proof.
Source-authored payload bound precedes allocation; root Recording quota funds cold
publication. Reused core identity/allocation projection counts shared texture/view
backing once: resources5, root owners30, closure owners42, bytes4224, Wasm cold1652.
Independent limits and exact/one-less/exhausted quota, forged control blocks,
foreign device, native size/object/device/token changes and fresh prepare from
committed root reject deterministically. Pending/committed validation, caller-drop,
genuine shutdown and actual original browser loss pass; original Buffer root
owner13/closure17 stays pending, refuses commit and cannot revive on rollback.
All hot paths allocate0. Null ValidatorOnly refuses absent native resources;
Vulkan/Metal DiagnosticFailure; WebGPU selected Prototype resource binding only.
No canonical native recording/submission/completion has executed, no broad Backend
Validated, Platform Validated or Production Ready promotion. Full generic original,
bindless and NativeSync gates remain closed; proof friends exactly Recording and
Submission. Existing private WebGPU transactions cannot replace the canonical root.
Next: seal CPU mutation authority before native recording and bind independently
budgeted scopes/admission/quarantine/exact completion to that same root. No shader
or visual changes; RenderDoc unavailable. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-native-closure-evidence.yaml`
and snapshotted reports/logs/XML, including preserved initial Wasm Null link failure
and corrected pure-contract-only Wasm fixture boundary.


CanonicalOriginalNativeLifecycle/1 contract checkpoint (2026-10-04): Contract
Complete; Prototype Complete read-only preparation inspection only. Host15/15,
Wasm/browser7/7, inventory128/128; both browser modes lifecycle/closure flags true,
clean reports. The complete transition inspection requires an exclusive one-shot
claim, successful actual recording scopes, an exact submission reservation, and
MaySubmit BEFORE native queue invocation. No pre-submit event commits the embedded
journal. Uncertain submit/cancel invalidates affected original controls and retains
owners; only exact queue AND callback drain or captured original loss with safe
callback teardown permits release. Submitted work never rolls back to reusable
CPU state. Receipt/drain alone is not success; terminal phases cannot revive.
Public event enums and transition inspection issue no lifecycle authority.
Source-authored submission/callback/registration limits and distinct original
Source/Graph/Recording/Submission/Callback quota ownership are inspected against
the genuine Prepared canonical plan. Core SharedOwnerProjection now supplies
reusable allocation-free owner independence validation, rejecting duplicate
objects/control blocks, missing and nonowning/forged aliases. Read-only admission
preserves root/count/journal and rejects Pending/Committed plans. Null ValidatorOnly;
Vulkan/Metal/WebGPU DiagnosticFailure until sealed native issuer payloads exist.
No broad Backend Validated, Platform Validated or Production Ready promotion.
Next: implement the exclusive CPU mutation interlock and pre-record-only ownership
seal, then consume it from real native recording/admission/completion without a
second state graph/journal. Generic original/bindless/NativeSync gates stay closed.
No visual output or shader changes; RenderDoc unavailable. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-native-lifecycle-evidence.yaml`.


CanonicalOriginalRecordingSeal/1 checkpoint (2026-10-04): Contract Complete /
Prototype Complete pre-record CPU ownership interlock only. Host15/15,
Wasm/browser7/7, inventory129/129, quick unit/Null3/3; both browser modes seal,
lifecycle, native closure and original plan flags true, reports clean. A cold
quota-bound seal retains the exact canonical root plus independent Submission and
Callback CPU quotas (root30 + three owners =33). Full source/domain/owner admission
and every shared allocation finish before the one-shot root claim is published.
Failed cold publication leaves the genuine root Prepared and unsealed. The exact
original graph/control/journal/count are preserved; no second state transaction.
Public CPU materialize/apply/begin/commit/abandon now return the stable
RHI-ORIGINAL-TRANSACTION-RECORDING-SEALED diagnostic while claimed. Abandon returns
a diagnostic instead of silently bypassing the interlock. The claim never restores
caller CPU mutation authority. This version exposes no native event API, encoder,
recorded proof, submit or callback, so seal destruction is strictly a pre-record
abandonment. Future native issuance requires private proven-event handling and
runtime ownership through callback drain/quarantine; this destructor is not a
submitted-work release path. Missing/Pending/Committed plan, owner32, exact/one-less/
exhausted quota, duplicates, forged control blocks, foreign original device and
quota aliases reject. Genuine shutdown, caller-drop retention, source count and
quota release, all exhausted-hot validation/refusal/destruction allocation0 pass.
A reusable one-shot quota fixture rebuilds genuine candidates before pressure.
The initial quota-zero assertion kept a test weak control block alive; the observer
is now released before checking allocator bytes, with no production budget change.
Null remains ValidatorOnly for native work; Vulkan/Metal/WebGPU native issuers and
execution remain DiagnosticFailure. CPU seal inspection is portable Prototype;
no broad Backend Validated, Platform Validated or Production Ready promotion.
Native resource closure validation and exact two proof issuer friends are retained.
Next: charge actual original Source submission/callback/registration capacity and
bind cold native admission to the exact closure/seal and original native loss
subscription. The existing private ordinary graph and CPU commit-before-submit
cannot substitute. Original facade shutdown alone never proves a submitted queue
is drained. No shader/visual changes; RenderDoc unavailable. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-recording-seal-evidence.yaml`
and `canonical-recording-native-admission-audit.yaml`, reports/logs/XML.


CanonicalOriginalLifecycleCapacity/1 checkpoint (2026-10-04): Contract Complete /
Prototype Complete cold Source capacity ownership only. Host16/16, Wasm/browser8/8,
inventory130/130, quick unit/Null3/3; both capacity/seal/lifecycle/native-closure/
original-plan browser flags true, clean reports. The original Source now embeds
three independently authored core atomic count budgets for pending submissions,
callback tickets and callback registrations. A quota-bound reservation retains the
exact original recording seal/root and aliases those Source-owned budgets; it never
reconstructs a pool, graph, resource control or journal. Metadata and shared owner
are charged to the independent original Submission CPU quota. Full retained
Submission owner capacity is34 (seal33 + reservation1); a live duplicate reservation
per seal refuses before allocation. Versioned explicit cold demand reserves one
single-root submission and declared nonzero callback/registration units, solely
as capacity intent. It cannot prove a native callback count or issue a native event.
Native lowering must independently derive and compare all actual requirements.
Core AtomicCapacityBudget.canAcquire reuses overflow-safe unsigned-range arithmetic
for non-reserving preflight; rejected valid capacity records its stable refusal.
All cold failures release partial leases and preserve the sealed Prepared root.
Genuine distinct roots sharing one original Source exercise1-4-2/2-3-2/2-4-1/2-4-2
budgets: each independent domain exhausts deterministically, used counts remain
unchanged on rejection, and release restores admission. Exact/one-less/exhausted
quota, owner33, zero/over-budget/version/opt-in requests, duplicate and forged
owners plus foreign genuine seal comparison reject. Caller-drop retains ownership;
original facade shutdown refuses validation but keeps units until pre-record
reservation destruction. Root/journal are unchanged, Source counts and quota bytes
release exactly, and all exhausted-hot validation/refusal/destruction allocate0.
Publication/destruction require external serialization per Source/seal; atomic
leases do not make wrapper interlocks concurrently callable. Null ValidatorOnly;
Vulkan/Metal/WebGPU native issuer/execution DiagnosticFailure. No native callback
registration or canonical GPU work is issued by this primitive. No broad Backend
Validated, Platform Validated or Production Ready promotion. Exact two native proof
issuer friends and generic/bindless/NativeSync gates are preserved. Next: bind the
exact original native loss subscription and backend-derived complete scope/callback/
command requirements, then retain the same closure/seal/reservation in native
runtime completion storage. No shader/visual changes; RenderDoc unavailable.
Evidence: `build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-lifecycle-capacity-evidence.yaml`.


OriginalNativeLossObservation/1 checkpoint (2026-10-04): Contract Complete /
Prototype Complete opaque exact subscription custody only. Host16/16,
Wasm/browser8/8, inventory131/131, quick unit/Null3/3; both browser modes
originalNativeLossObservationPassed and earlier canonical capacity/seal/closure
flags true, diagnostics/nativeErrors empty. Only the original GpuDevice issues
the read-only observation, retaining the exact domain and quota-bound actual
armed native loss binding without retaining the facade. Pointer plus shared-owner
identity and original facade identity refuse foreign observations even over the
same physical browser device. Default/unprepared observations, domain/subscription
quota mutations refuse. Copy/observe/validate/destruction allocate0 under exhausted
quotas; Retired and Shutdown remain distinct after facade drop. Actual original
browser device loss returns the original NativeLost diagnostic in retained
observation/device validation and refuses new observation; unrelated loss isolation,
pending canonical root/journal and rollback-no-revival tests remain intact. No
native handle, receiver, detach or GPU completion authority is exposed. Null
ValidatorOnly; Vulkan/Metal native lifecycle DiagnosticFailure; WebGPU genuine
subscription custody verified, canonical recording/submission execution remains
closed. No broad Backend Validated / Platform Validated / Production Ready
promotion. Next: genuine loss-before-registry plan recipe and backend-derived
complete cold requirements, then native runtime custody. No shader/visual changes;
RenderDoc unavailable. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/original-native-loss-observation-evidence.yaml`.


CanonicalLossBoundNativeClosure/1 checkpoint (2026-10-04): Contract Complete /
Prototype Complete exact native subscription resource custody only. Host16/16,
Wasm/browser8/8, inventory132/132, quick unit/Null3/3; both browser modes
canonicalLossBoundClosurePassed and earlier canonical flags true, reports clean.
The reusable genuine copy/pass plan recipe binds original native loss before the
registry and all resource factories. A selected closure validates the device-issued
observation against the exact original facade and registered canonical root before
cold allocation, then retains that subscription with the existing closure. Root
registry already retains its domain, so resource closure owner42 becomes43; native
resource bytes remain4224 and parent allocation dedup unchanged. Existing and bound
closure Wasm cold metadata now1708 (embedded opaque observation storage). Exact/
one-less/exhausted Recording quota, owner42, foreign facade over the same physical
native device, unissued/unprepared and mutated subscription quota refuse. Unbound
closures expose RHI-WEBGPU-CANONICAL-CLOSURE-LOSS-UNBOUND and remain inspection-only.
Caller-drop keeps exact root/subscription alive; facade shutdown retains its distinct
diagnostic until pre-record destruction. The actual original browser loss fixture
now uses the bound Buffer closure owner18, retaining pending journal, rejecting
commit and preventing rollback revival. Exhausted hot validation/release allocate0.
Null returns its declared ValidatorOnly loss refusal before registry order checks;
initial test diagnostic assumption was corrected without production changes and
failed log/XML preserved. Vulkan/Metal native lifecycle DiagnosticFailure; WebGPU
selected resource custody Prototype only. No encoder/queue/proof/completion is
issued here; generic/bindless/NativeSync and exact two proof issuer friends unchanged.
No broad Backend Validated / Platform Validated / Production Ready promotion.
Next: derive complete native command/scope/callback/registration requirements and
consume the exact Source reservations, then allocate native recording/completion
custody under independent quotas and implement private proven lifecycle events.
No shader/visual changes; RenderDoc unavailable. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-loss-bound-closure-evidence.yaml`.


CanonicalOriginalColdAdmission/1 checkpoint (2026-10-04): Contract Complete /
Prototype Complete immutable backend-derived cold admission only. Host16/16,
Wasm/browser8/8, inventory133/133, quick unit/Null3/3; both browser modes
canonicalColdAdmissionPassed and all prior mandatory flags true, clean reports.
Selected CopyPass/1 verifies exact original ten-command language and Prepared
canonical plan, exact loss-bound closure/root/seal/reservation and independent
CPU quotas. Recording encoder creation and commands share one native error-scope
batch; requirements are recording3 + submission3 + queue1 + runtime loss1 =8
allocated slots /8 new registrations, one pending submission. Existing original
device loss1 is a separately budgeted retained dependency and is never charged or
registered again. Actual native filter order/count and pure cold planner now share
WebGpuErrorScopeContract/1; shader validation consumes that count too. No ordinary
unused creation slots may be allocated or registered by this selected lowering.
Exact Source request1/8/8 is required; smaller or larger reserved demand refuses.
Retained owner union is closure43 + reservation34 - same root30 + admission1 =48;
Recording/Submission/Callback owner limits all checked. Metadata is charged to
exact original Recording quota. Forged requirements, overflow/inconsistent total,
unbound closure, forged owner, owner47, quota mutations, independent count limits,
exact/less/exhausted allocation and missing/foreign root provenance refuse.
Caller-drop preserves the same root and all units; facade shutdown retains units
until pre-record release. Hot validation/release allocate0 and counts/bytes release
exactly. Pure Wasm Null initially imported the modern native callback ABI; extracted
the pure scope contract instead, with failed build log preserved. Null ValidatorOnly;
Vulkan/Metal/WebGPU native lifecycle execution remains DiagnosticFailure. Requirements
are consumed by cold admission, but actual callback metadata/runtime loss observer
and native lifecycle issuers still need implementation. No broad Backend Validated /
Platform Validated / Production Ready promotion. Next: actual opaque device/queue
command shell custody and charged callback/runtime completion preparation. No shader/
visual output change; RenderDoc unavailable. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-cold-admission-evidence.yaml`.


CanonicalOriginalColdCommand/1 checkpoint (2026-10-04): Contract Complete /
Prototype Complete opaque cold native command custody. Selected browser WebGPU
device/queue connection is Backend Validated only for this cold payload. Final
host16/16, Wasm/browser8/8, inventory134/134, quick unit/Null3/3; both browser
modes canonicalColdCommandPassed and all prior mandatory flags true, clean reports.
A backend-private opaque wrapper and native command Impl retain the exact cold
admission/root/closure/seal/Source reservation. Real original device and actual
queue are connected without creating an encoder; immutable device/queue/recording
identity snapshots are revalidated. No native handle, mutable raw command or
recorded proof is exposed. All three additional objects (wrapper/shared command/
owned Impl) allocate under the exact root Recording quota; total owners51 and
Wasm cold bytes752. All three lifecycle owner limits and independent cold budgets
are checked. Every allocation and exact native connection completes before a
sticky claim is published on the original seal. Duplicate wrappers, peer admission
objects and replacement reservations on that same seal cannot issue another shell,
including after original shell destruction. Failed cold allocation leaves no claim.
Exact/less/exhausted allocations use rebuilt genuine one-shot roots; no budget
relaxation. Forged owner, owner50, single-payload/version/opt-in/CPU failures refuse.
Caller-drop retains root and Source1/8/8; facade shutdown fails validation without
releasing units. Actual original browser loss now also closes a genuine native
shell; all units remain until pre-record destruction, then counts and nine quota
bytes release exactly. Native device/queue references die before root/Source. Hot
validation/release allocate0. Generic native open explicitly rejects canonical
shells until private recording events are implemented. Null ValidatorOnly;
Vulkan/Metal DiagnosticFailure; WebGPU Prototype actual native cold custody with
recording/submit/completion unavailable. Exact two proof issuer friends and full
generic/bindless/NativeSync gates unchanged. Actual callback metadata/runtime loss
observer and completion storage remain pending; reserved slots do not prove issued
callbacks. No broad Backend Validated / Platform Validated / Production Ready
promotion. Next: allocate8 Callback quota slots and exact Submission completion
custody, bind charged runtime loss1 independently of facade loss, then proven private
lifecycle events. No shader/visual output change; RenderDoc unavailable. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-cold-command-evidence.yaml`.


### CanonicalOriginalCallbackCustody/1 verified checkpoint — 2026-10-04

Completed the cold eight-slot boundary: recording scopes3, submission scopes3,
queue1 and runtime loss1 allocate under the exact original Callback quota. The
opaque wrapper, runtime state, one-entry core retention pool and graph allocate
under independent original Submission. The retained graph owns the same original
cold shell/root/closure/seal/reservation and embedded journal. No old ordinary
private transaction is substituted. Admitted owner union62; measured Wasm cold
Submission348 and Callback160 bytes. The independently issued runtime device-loss
observer consumes the already reserved Source1/8/8 demand, without recharging the
existing original facade subscription. Seven scope/queue slots remain unarmed.

Failed CPU allocation or actual native binding publishes no custody claim and
unwinds all bytes. Native binding failure after actual promise registration
suppresses captured userdata before release. Successful cancellation/destruction
suppresses native userdata before releasing the graph. Failed suppression returns
a stable diagnostic and preserves bounded runtime retention through wrapper drop
until genuine physical loss. Facade Shutdown alone neither delivers runtime loss
nor proves queue completion. Actual browser loss independently releases live and
Shutdown custody; late callbacks from cancelled observers deliver0.

Validation: host16/16, Wasm/browser8/8, inventory135/135, quick unit/Null3/3,
pinned Vulkan header syntax. Both browser reports are clean and require
canonicalCallbackCustodyPassed. Contract Complete / Prototype Complete for this
cold boundary; Backend Validated only for selected browser WebGPU actual native
loss observation. Null ValidatorOnly; Vulkan/Metal DiagnosticFailure; WebGPU
explicit Prototype. No generic/original/bindless/NativeSync execution, encoder,
recorded proof, queue submit or broad Platform Validated / Production Ready claim.
No visual output change; RenderDoc unavailable. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-callback-evidence.yaml`.

Next: private sealed-root plan/pending/commit/invalidation operations and actual
Recording/Submission issuer callbacks against this same storage. Cold inspection
requires Prepared/no encoder; extend a separate proven live lifecycle boundary
without weakening that evidence. MaySubmit must precede the native queue call;
canonical journal commit must wait for exact queue and all submission scopes.

The final cancellation review also adds reusable core pool `canCancel` inspection
before native suppression. Exact invalid/stale/reused/submitted/quarantined/closed
tickets are tested without hot allocation; a submitted graph cannot lose its
observer through the pre-record cancellation entry. The next phase design audit
is `canonical-private-native-event-audit.yaml` in the same evidence directory.
It identifies sealed public rollback refusal, separate live validation, old
ordinary pre-queue commit and outstanding callback lifetime as required private
issuer integration work; it grants no executable native authority.


### CanonicalOriginalCopyPassLowering/1 verified checkpoint — 2026-10-04

Added exact cold native input projection under the same runtime custody. All copy
fields expand through shared `TransferCopyProjection`; explicit mip geometry,
pitch, offset, relative source/destination mip and layer values are preserved.
The selected browser path binds four single-mip operations to actual original
native payloads and maps complete color/resolve/depth attachment inputs through
shared validators. Whole CopyDst-to-ColorAttachment uses an explicit normative
WebGPU automatic-usage transition contract; empty/layered/subpass/dependency or
unsupported shape fails deterministically. No encoder or GPU work is issued.

One opaque wrapper and runtime-owned payload add two owners (union64); measured
Wasm Recording cold708 bytes. Exact bounded copy/color arrays allocate in original
Recording; inline custody pointer extends its cold Submission footprint to364
bytes, Callback remains160. Publication is sticky after all allocations/preflight;
failure unwinds and preserves retry. Cancellation/loss release borrowed native
descriptor storage before the original graph/root. Caller drop, Shutdown and real
physical browser loss preserve the original ownership chain and safe teardown.

Historical CopyPass checkpoint before NormalizedTextureStateCreation/1 (superseded
by the verified normalized creation checkpoint below): a genuine Null normalized
two-mip CopyPass graph/plan passes (cells6, steps15,
transition1). The actual WebGPU equivalent fails earlier at original graph creation:
its native texture source is uniform and the mip0 attachment view is partial,
producing `RHI-ORIGINAL-TEXTURE-STATE-PARTIAL-CONTROL-UNPROVEN`. This failure is
required and tested, with initial failure evidence retained. Pure CPU multi-mip
projection does not establish native multi-mip readiness. No view widening,
truncated mip data or fixture control substitutes for that missing native source.

Validation: host16/16, Wasm/browser8/8, inventory136/136, quick unit/Null3/3 and
pinned Vulkan header syntax. Both browser reports require canonicalCopyPassLoweringPassed
and have clean native diagnostics. Contract Complete / Prototype Complete for
cold input projection. Backend Validated only for selected browser single-mip
native bindings and actual multi-mip refusal. Null ValidatorOnly, Vulkan/Metal
DiagnosticFailure, WebGPU explicit Prototype. No native encoder/recorded proof/
queue completion, broad Platform Validated / Production Ready, generic/bindless/
NativeSync promotion or visible output change. RenderDoc unavailable. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-copy-pass-evidence.yaml`.

At that historical checkpoint, the next prerequisite was genuine creation-bound
normalized WebGPU texture state with
one authoritative control and authored bounded allocation, as audited in
`canonical-webgpu-normalized-state-audit.yaml`. Null/Vulkan/Metal source bindings
already use the shared normalized control; their native readiness is not inferred
from this audit. Then Recording/Submission issuers consume the exact same private
projection and existing callback slots against the sealed canonical journal.

NormalizedTextureStateCreation/1 is now Contract Complete / Prototype Complete for
selected creation-bound state and cold input projection (2026-10-04). WebGPU real
native payloads bind one common normalized control; borrowed whole consumers
share it without a shadow scalar. Exact descriptor cells fix current/snapshot
storage and core creation-quota charges. Nonuniform/missing whole observations
fail closed; allocation exceptions retain existing RESOURCE-ALLOCATION diagnostics
before native creation, and failed enabled-native creation cannot publish metadata.
Null's normalized validator, existing Vulkan/Metal mappings, actual browser
multi-mip/partial-view bindings, pending/rollback/stale/drop/loss, all-aspect whole
writes and hot allocation0 were assessed. Host17/17, unit Wasm6/6 plus final browser/
inventory3/3, inventory137/137, quick unit/Null3/3 and Vulkan syntax pass. Both
browser reports are clean; genuine mips2 graph cells6/steps15/transition1 projects
copies7 (Recording1020, owner64, Submission364/Callback160). Backend Validated is
limited to selected browser creation/state/bindings/loss. Canonical encoder,
recording, submission, completion, NativeSync, broad Platform Validated and
Production Ready remain unproven. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/normalized-texture-state-evidence.yaml`.
Next: private sealed-root plan mechanics and exact existing Recording/Submission
issuers. `RHI-RENDERDOC-TOOL-UNAVAILABLE`; no new visible output or GPU commands.

Canonical root application and commit now reuse private same-root mechanics;
public CPU methods retain `RHI-ORIGINAL-TRANSACTION-RECORDING-SEALED` checks.
Write/enlistment/final-validation failure directly rolls back the internal journal
and cannot be blocked by public sealed abandonment. The existing seal remains
cold Prepared-only and invokes neither helper; no proof friend, native callback,
encoder or queue execution was added. Contract Complete / Prototype Complete
shared CPU mechanics only: host17/17, unit/browser7/7 plus corrected inventory1/1
(138/138 source assertions), fast contract and Null hidden-frame paths pass. The
actual sealed-native caller and completion commit remain unimplemented, with no
new Backend Validated / Platform Validated / Production Ready promotion.
Evidence: `build/web-bindless-apple-debug/artifacts/bindless-continuation/root-mechanics-evidence.yaml`.
Continue private live ownership/validation and callback drain retention before
arming scopes or queue callbacks. `RHI-RENDERDOC-TOOL-UNAVAILABLE` remains explicit.


### Canonical terminal retention/drain prerequisite — 2026-10-04

TerminalRetention/1 closes original scalar/normalized controls before releasing
any graph. The root becomes Invalidated and discards saved journal snapshots
without restoring uncertain values or claiming successful completion. The root
retains its sole seal friendship; only custody's private terminal-loss helper
can reach this new seal operation. Public CPU mutation stays sealed.

Core CompletionRetentionPool now supports one-shot invalidateAndRetain and exact
releaseInvalidated tickets. The retained ticket refuses cancel/submit/resolve;
immediate invalidate cannot bypass its drain obligation. WebGPU native loss
keeps the exact lowering/shell/root until all gates are Unarmed, Delivered, or
Detached after native suppression. Armed/Delivering includes the current loss
callback. Scope/queue gates are never detached by the loss handler. Cancellation
refuses outstanding scope/queue userdata before detaching the loss observer.

Verified host8/8 and Wasm/browser8/8, inventory138/138, Null hidden-frame and
pinned Vulkan syntax. Core tests cover all128 issued-slot subsets before/after
submit, stale tickets, duplicate invalidation, refused cancellation/completion,
last-callback delivery and exhausted-quota hot allocation0. Genuine browser loss
permanently invalidates a caller-retained original root and releases unissued
custody with all nine quotas zero in both feature modes. The seven scope/queue
slots still remain unarmed: outstanding native scope/queue drain is not yet
Backend Validated and must be exercised by actual issuers in the next stage.

Contract Complete / Prototype Complete for this drain prerequisite; Backend
Validated only for selected browser loss invalidation/unissued release. Null
ValidatorOnly; Vulkan/Metal DiagnosticFailure; WebGPU explicit Prototype with
recording/submission/general/bindless/NativeSync gates closed. No broad Platform
Validated or Production Ready. RenderDoc: RHI-RENDERDOC-TOOL-UNAVAILABLE; no new
encoder, GPU command or visible output. Earlier immediate-loss-release evidence
applies only to unarmed cold storage and is superseded by this retained path.
Evidence: `build/web-bindless-apple-debug/artifacts/bindless-continuation/terminal-retention-evidence.yaml`.
Next: separate retained/live identity checks and actual Recording/Submission
issuers against the existing eight slots; MaySubmit-before-queue and completion
commit remain unimplemented.


### Retained ownership versus live native facts — implementation contract

Ownership inspection validates the exact original root, graph, registered source
lifetime, independent CPU domains, source budgets and retained owner bounds. It
may observe Prepared, owned Pending finals or Committed finals; that read-only
result grants no execution authority. Cold inspection additionally requires
Prepared, an empty journal and no encoder/finished/submitted command. The cold
seal, reservation and admission retain this restriction. Private native live
validation must use the ownership path and exact captured root/seal/reservation/
closure/command/device/queue/recording identity, require owned Pending finals,
and check encoder/finished/submitted payload consistency. Caller-supplied phase
enums or replacement ordinary completion reservations cannot stand in for facts.
Null remains ValidatorOnly and other backend execution gates remain closed until
actual issuer/callback evidence is validated. This split adds no native event API.


Retained/live inspection prerequisite verified — 2026-10-04: host8/8 and
Wasm/browser8/8, inventory139/139, fast contract and Null hidden-frame pass.
Shared ownership checks observe genuine Prepared/Pending/Committed CPU roots,
validate owned final seals and independent quotas, reject forged roots, stale
original descriptor bindings and Shutdown. Cold preparation/seal/reservation/
admission remain Prepared-only; the native cold shell additionally refuses
encoder/finished/submitted state. Private post-record live inspection checks
the exact captured admission/root/seal/reservation/closure, native command owner,
Recording quota, device/queue/recording identity, Pending journal and finished
payload. It uses no old ordinary completion reservation and emits no GPU work.

The same identity helper is exercised by real browser cold validation; the
private live post-record path has no actual issuer caller yet and is not Backend
Validated. Contract Complete / Prototype Complete applies only to shared
read-only ownership and cold identity reuse. No recording/queue evidence, native
completion authority, execution capability or Platform/Production promotion.
Both current browser reports have success=true and empty diagnostics/nativeErrors.
Proof friends remain exactly the two existing issuers; root friendship remains
solely the existing seal. No new allocations, shaders, encoder or native command
path. RHI-RENDERDOC-TOOL-UNAVAILABLE. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/retained-ownership-evidence.yaml`.
Continue cold allocation of actual Recording/Submission objects and evidence,
then scoped encoder, existing slots3/4, MaySubmit-before-queue and genuine
outstanding callback drain/loss. Parent canonical lifecycle and bindless plan
remain incomplete.


### CanonicalOriginalIssuerPreparation/1 — cold inventory contract

Prepare the existing concrete WebGpuOrdinaryRecording and WebGpuOrdinarySubmission
objects over the captured canonical native shell, with an opaque wrapper that
exposes no proof, issuer, native handle or event API. The exact additional owner
inventory is wrapper1 + Recording1 + Submission1 =3 (selected total67 after
lowering64). Inline recording scope3 evidence and existing submission queue+scope4
evidence add bytes only. No new command, pool, graph, state transaction, callback
slot or Source capacity is allocated; Source remains1/8/8 and seven slots Unarmed.

Recording metadata/share ownership charges original Recording. Submission and
wrapper metadata/share ownership charge original Submission. Both independently
charged peaks must be measured from core allocation headers, with exact/less/
exhausted admission tests. Budget/version/opt-in/owner/domain checks and all CPU
allocations precede sticky publication and encoder. Failed candidates unwind
without changing existing custody or publishing a claim; cold retry is allowed.
State retains both issuers, which hold weak State links. Cancellation/loss resets
Submission then Recording before borrowed lowering/graph release. Public ordinary
record/submit paths refuse canonical mode before dereferencing the old completion
reservation; preparation neither manufactures a proof nor satisfies native sync.

Null ValidatorOnly; Vulkan/Metal DiagnosticFailure; WebGPU explicit Prototype
issuer storage only. Native scoped recording/submission/completion, actual
outstanding callback drain and Platform/Production acceptance remain incomplete.
Required paths are fast contract/unit, Null hidden-frame and both genuine browser
feature modes, covering quota failures, repeat/forged preparation, caller drop,
Shutdown, cancellation, real loss and hot allocation0.


### CanonicalOriginalIssuerPreparation/1 verified checkpoint — 2026-10-04

Cold preparation now allocates the actual existing Recording and Submission
concrete classes over the captured command, with an opaque non-proof wrapper.
Additional owner inventory3 yields selected total67. Measured Wasm incremental
Recording316 and Submission280 bytes include actual core metadata/share owner
charges; Callback allocation0 and extra Source demand0. Existing Source1/8/8 and
slots3/3/1/1 are unchanged. Recording's inline scope evidence and Submission's
existing inline queue/scope evidence are bound to the slot counts at compile time.

Both issuer links to State are weak. Native loss/cancellation releases Submission
then Recording before borrowed lowering and retained graph. All fallible CPU
allocations precede sticky issuer publication; failed Recording/Submission
quota probes unwind to the original baselines, leave the root Prepared and
allow cold retry. No old ordinary completion reservation, replacement command
or state transaction is created. Ordinary record/submit deterministically refuse
canonical mode; status/diagnostic inspection never dereferences an absent old
completion owner. The wrapper exposes neither actual issuers nor recorded proof.

Verified host11/11 (including retained allocation, WebGPU memory/staging and Null
hidden-frame), Wasm/browser8/8 and inventory140/140. Both genuine browser modes
report canonicalIssuerPreparationPassed=true, clean diagnostics/nativeErrors,
exact/one-less/exhausted independent quotas, repeat/forged/owner/domain rejection,
cold retry, caller-drop/Shutdown and physical loss with all nine quotas zero.
All hot validation/closed-entry/drop/loss paths allocate0. The initial quota-test
baseline omitted the pressure lease; that fixture assertion was corrected and
its failed reports preserved without changing production budgets or paths.

Contract Complete / Prototype Complete for cold issuer storage only. Backend
Validated remains limited to original browser native bindings and loss teardown;
no actual canonical encoder/scopes/queue completion evidence exists yet. Null
ValidatorOnly, Vulkan/Metal DiagnosticFailure, WebGPU explicit Prototype cold
storage; recording/general/bindless/NativeSync execution gates remain closed.
No Platform Validated or Production Ready. RHI-RENDERDOC-TOOL-UNAVAILABLE; no new
shader, encoder, submitted GPU work or visible output. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/issuer-preparation-evidence.yaml`.
Continue exact sealed-root application and actual encoder/recording scopes using
these already allocated issuers and slots; MaySubmit and native completion
commit remain subsequent required work. Parent lifecycle and bindless plan stay
incomplete.


### CanonicalOriginalScopedRecording/1 checkpoint — 2026-10-04

A separate version1 Prototype opt-in now drives the existing real Recording
issuer over the original sealed root/native command and CopyPass/1 projection.
Private application/rollback passes only through the seal; root friendship and
public CPU mutation refusal remain unchanged. Three existing role/index/ticket
bound recording slots consume real scoped encoder creation, ordered mip copies,
empty render pass and native finish. The actual proof issuer reports Recorded
only after all scopes succeed and drain, userdata clears and private live
validation proves exact native identity and unchanged owned Pending finals.
Cold Prepared/no-encoder inspection remains strict. No replacement transaction,
command, completion reservation, callback allocation or Source demand is added.

Pre-submit error/cancel/drop/Shutdown retain through issued userdata then roll
back once. Real device loss invalidates first, retains while callbacks are Armed
or Delivering and releases the original issuer/lowering/graph only after drain.
Hot recording, callbacks, validation and teardown allocate0 under all nine
exhausted independent quotas. The native-error negative probe produces a genuine
GPUValidationError on the same encoder; no callback or proof is fabricated.

Null ValidatorOnly, Vulkan/Metal DiagnosticFailure, WebGPU explicit Prototype
recording only. Queue, MaySubmit, original completion commit, general/bindless
execution and NativeSync remain unavailable. Contract Complete / Prototype
Complete recording only; Backend Validated selected real browser recording
payload/scopes and loss drain. No full Platform Validated or Production Ready.
No shaders or visible output changed; newly recorded buffers are unsubmitted.
RenderDoc unavailable: RHI-RENDERDOC-TOOL-UNAVAILABLE, no visual acceptance claim.
Authoritative validation is in
`build/web-bindless-apple-debug/artifacts/bindless-continuation/scoped-recording-evidence.yaml`.
Continue c2b4 MaySubmit-before-queue and Submission4 evidence/commit; c2b/c2 and
full bindless readiness remain incomplete.

Verified recording checkpoint: host12/12, Wasm/browser8/8 and inventory142/142.
Both genuine feature modes report canonicalScopedRecordingPassed=true,
diagnostics=[] and nativeErrors=[]. Seven native probes cover success, real scope
error, cancellation, caller drop, Shutdown, changed native binding and reversed genuine scope promises. Each
physical loss observer sees3 Armed recording scopes, then invalidates the root
and drains to all-nine-quotas-zero. Source1/8/8 and owners67 stay unchanged;
current measured cold issuer increments remain Recording316/Submission280 bytes.
Queue invocation and original completion commit are still absent.

CanonicalOriginalQueueSubmission/1 now defines the next independent explicit
intent and MaySubmit-before-queue/commit-after-evidence contract. All four backend
submission/commit/NativeSync execution gates remain closed until real native
payloads are implemented and tested. Null is ValidatorOnly; Vulkan/Metal/WebGPU
are DiagnosticFailure for this submission contract. Existing ordinary submission
commits before queue invocation, so canonical integration must use a separate
private path over the original issuer, ticket, root and Submission4 evidence.
Post-MaySubmit uncertainty/cancel/Shutdown invalidates and retains; successful
queue receipt alone cannot authorize commit. This is Contract Complete planning
only, with no submission Prototype Complete, Backend Validated, Platform
Validated or Production Ready claim. Continue b4b actual native integration.

### CanonicalOriginalQueueSubmission/1 — verified selected private lifecycle

The previous closed-submission checkpoint is superseded for explicit selected
WebGPU Prototype submission. The original Submission issuer consumes the exact
Recorded proof/root/device/queue/non-recycled ticket and publishes pool submitted
retention and MaySubmit before native queue invocation. Only proven invocation,
successful queue plus three scopes, drained userdata, unchanged original Pending
finals and a live loss registration permit private seal commit. Root friendship
remains exclusive to the seal; public mutation and generic/native proof gates stay
closed. Original Source1/8/8 and core owners67 are unchanged. No old ordinary
reservation, replacement transaction, commit-before-queue or post-submit rollback
is used.

NativeCompletionReceipt/1 cold-prepares four foreign states, eight listeners and
four ownership handles in the existing slots. Native phases Prepared/Bound/
Delivered/Suppressed clear userdata before delivery or suppression. A partial then
registration cannot leave usable userdata. Suppressed queue listeners prove no
GPU termination: after registration refusal the exact root/native shell/lowering
stay quarantined until physical loss and complete userdata drain. Thrown native
scope pop closes subsequent canonical recording for that physical device until
replacement. Post-MaySubmit errors/cancel/drop invalidate and retain. Logical
Shutdown closes original lifetime tickets synchronously; the first private issuer
observation invalidates the pending journal while outstanding callbacks retain it.

Final host11/11, Wasm/browser8/8 and inventory146/146 pass, including fast
contract/unit and Null hidden-frame. Both genuine browser feature modes have
empty diagnostics/nativeErrors. Ten native submission probes verify successful
commit, thrown invocation before/after actual native submit, genuine scoped
GPUValidationError, reversed real promises, partial scope registration, cancel,
drop, Shutdown and changed binding. Native destination-buffer readback checks
seeded mip0/mip1 pixel rows and the original buffer-copy gap. Four physical loss
probes verify observer4 outstanding slots, queue-registration retention after scope
drain, genuine pop-then-throw with observer3 remaining slots, and deterministic
subsequent-recording refusal. Final all-nine-quotas-zero and core hot allocation0
are verified. Current Wasm issuer increments remain Recording316/Submission280;
custody measured cold peaks are Submission388/Callback384.

Null remains ValidatorOnly; Vulkan/Metal return deterministic DiagnosticFailure;
WebGPU selected version1 intent is Prototype. General/bindless execution and
NativeSync remain closed. Browser-managed promise/reaction/submit-array byte
budgets are unavailable: fixed foreign counts are documented, but no foreign-byte
or Production Ready claim is made. Contract Complete / Prototype Complete is
restricted to selected CopyPass private lifecycle. Backend Validated is restricted
to actual browser WebGPU queue/scopes/output/uncertainty/loss. Full Platform
Validated and Production Ready are not claimed. RenderDoc tools are absent on
this host (RHI-RENDERDOC-TOOL-UNAVAILABLE); transfer readback is data evidence,
not RenderDoc visual acceptance. No capture or visual acceptance is claimed.

Evidence: `build/web-bindless-apple-debug/artifacts/bindless-continuation/queue-submission-evidence.yaml`
and `canonical-private-native-lifecycle-completion-audit.yaml`, with final
reports/log/XML and a saved RenderDoc diagnostic. The initial full smoke failed
because a new pre-destroy wait allowed old mapping probes to complete; moving
that wait to early cold preparation preserves the original loss assertions and
all production semantics. The failed report remains available. Enclosing selected
cold/private lifecycle tasks are closed; broader original stream/native parent and
full bindless plan remain open for the next scope audit.


## Original stream compilation and selected projection traversal (2026-10-04)

`OriginalCommandStreamCompilation/1` compiles the complete supported immutable
`OriginalCommandInputs` profile into pointer-free ordered operations. Each operation
binds its original command, sparse descriptor, reference span, and family-specific
expansion ordinal. Scalar/dependency operations have no expansion; copy elements,
buffer uploads, texture upload/readback mips and attachment references use separate
ordinal spaces. Explicit descriptors and existing `TransferCopyProjection` and
upload/readback layout contracts determine counts. No extent, pitch, staging content,
native resource generation or completion identity is guessed.

Independent opted-in limits cover commands, descriptors, references, copy elements,
buffer uploads, upload/readback mips, render passes, attachment references, buffer
upload **declared** bytes, texture upload CPU pixel bytes, readback **declared** bytes,
CPU closure owners and metadata. Repeated commands charge repeated work/declared
bytes even when the same original snapshot is retained once. Source and Graph quota
aliases are rejected; materialized program metadata uses an explicitly bounded
Recording quota within the original Source's authored Recording ceiling. CPU owners
are the original Inputs closure plus the program, not native registration owners.

Existing input/order/provenance validation runs first. `PreflightReplay` validates
all operations/counts before cold allocation or the first CPU projection visitor.
The immutable program retains the exact original Inputs; it does not recreate the
root, state graph or journal. `OriginalCommandAccessValidation`, the original state
plan, native closure, issuer, scope/queue/loss bindings and independent Source,
Recording/Submission/Callback/Result/staging budgets remain separate prerequisites.
Zero disables an absent family. A budget failure publishes no partial program and
invokes no projection visitor. Visitor failure stops subsequent consumption and
invalidates the whole cold projection; partially built native work is never submitted.

`OriginalStreamSelectedProjectionTraversal/1` permits the existing private selected
CopyPass lowering to consume this allocation-free operation traversal and exact
sparse descriptor indices. Source-authored command/descriptor/reference limits,
selected admission's render-pass limit and independently authored native copy limit
form its traversal budget; upload/readback categories remain disabled. Every native
copy expansion must agree with the operation's first ordinal/count. This traversal
owns no additional retained program or callback and cannot widen the exact selected
language or either proof issuer. It creates no GPU work.

Four complete-program executor outcomes are explicit: Null is ValidatorOnly;
Vulkan, Metal and WebGPU report stable DiagnosticFailure (`*-EXECUTOR-MISSING`).
The existing private WebGPU CopyPass executor remains separately versioned and
selected. CPU compilation does not promote capability, NativeSync or generic/bindless
execution. Missing native initialized buffer staging, packed texture staging,
readback mapping/publication, draw/dispatch/query bindings and complete ordered
native executors remain unfinished.

Readiness: **Contract Complete** and **Prototype Complete** apply only to CPU stream
compilation. Genuine browser-resource smoke validates ownership/count traversal;
it does not establish complete-program **Backend Validated**, **Platform Validated**
or **Production Ready**. Selected queue/output/error/loss evidence is separately
required for native projection integration. No shader or visual output is changed
by compilation; unavailable RenderDoc capture remains an explicit acceptance gap.


Verified checkpoint: final host **11/11**, Wasm/browser **8/8**, source inventory
**148/148**. Both genuine feature-mode browser reports require
`originalStreamProgramPassed` and retain `canonicalSubmissionPassed`; diagnostics and
nativeErrors are empty. Selected native projection uses shared complete preflight
and sparse operation indices; expansion ordinals/counts match native projection.
No additional retained native owner or callback was introduced. Actual destination
mip/buffer readback and existing submit/error/cancel/drop/physical-loss probes pass.
Program quota object/control-block/non-owning aliases reject; all independent count
refusals run before allocation/visitor, and hot validation/traversal allocates zero.
Host program cold peaks: ten-command single/multi-mip 928 bytes, nine-command
ordered two-pass 872, five-command upload/readback 648; these are allocator
measurements, not native memory estimates. A six-byte copy can compile fields but
fails the separate portable access contract and remains execution-closed.

Evidence: `build/web-bindless-apple-debug/artifacts/bindless-continuation/stream-program-evidence.yaml`
and `stream-program-final-*` reports/logs/XML. `stream-program-renderdoc-diagnostic.yaml`
records the unavailable capture tool. No RenderDoc visual acceptance is claimed.
The next audit (`canonical-original-ordered-executor-next-audit.yaml`) identifies
that native admission still selects exact ten commands and the recorder still
collects copies before one empty pass. Ordered per-pass native payloads/count
requirements, initialized original upload staging and genuine original readback
completion/map/publication remain required. Enclosing/full bindless tasks stay open.


## OrderedOriginalCopyAttachment/1 and explicit cold/lowering /2

Cold admission /2 and copy/attachment lowering /2 select this profile with explicit
Prototype opt-in. /1 preserves its exact ten-command CopyPass vocabulary and rejects
/2-only budget fields. /2 permits only original transfer-pass boundaries, four copy
families, whole texture transitions, empty attachment-pass boundaries and Finish.
Existing original input/order/access/state validators remain mandatory. Unsupported
upload/readback/draw/dispatch/query/debug/alias/buffer-barrier families fail before
GPU work; no empty native payload stands in for them.

Original commands and render passes are derived from the complete immutable stream,
with independent authored command, expanded copy, pass and attachment-reference
bounds. Exactly one real encoder and command buffer serve the complete stream.
Recording scopes3, submission scopes3, queue completion1 and runtime loss1 consume
original capacity1/8/8; original device loss1 stays separately owned and charged.
Pass descriptors/colors/depth fields are backend-private, bounded and cold-allocated
under the exact root's Recording quota. Native per-pass descriptors must retain
stable pointers after their bounded storage is finalized. No new root/journal or
callback owner is introduced. Actual foreign native wrapper byte usage remains a
Prototype limitation; bounded creation counts do not claim quota-metered JS bytes.

Every native command consumes its original ordered operation. Copy native expansion
must match the original ordinal/count. Each BeginRenderPass selects its own immutable
original attachment descriptor; EndRenderPass closes that exact native encoder.
Finish occurs only after the validated complete sequence. Cold allocations finish
before recording; no per-draw pipeline/shader creation, file/network IO or blocking
GPU wait is added. Empty transfer boundaries are L2 grouping on the same native
encoder, not fake completion. Empty attachment passes perform real load/store/clear.

Whole texture transitions validate exact complete CopySrc/CopyDst/ColorAttachment/
DepthAttachment scopes (or explicit Undefined discard) and matching aspect/range.
WebGPU's normative automatic ordered GPU usage provides Equivalent mapping on these
states; the canonical original journal owns pending/final logical states. This
mapping is not a CPU synchronization primitive and cannot promote NativeSync.
Unsupported ranges/scopes/formats/native bindings reject deterministically.

Four profile outcomes: Null ValidatorOnly; Vulkan/Metal DiagnosticFailure with
stable payload-missing IDs; WebGPU explicit private Prototype using the existing
original issuers, scope/queue receipts, uncertainty quarantine and loss lifetime.
General full-program/bindless execution remains closed. Original /1 and /2 admission
and lowering versions must match. Successful metadata inspection cannot record,
submit, commit, publish results or replace missing original source/completion payload.

Acceptance must cover actual variable counts, multiple/interleaved passes/copies,
original native output/order, error/quarantine/loss and all independent quotas.
Contract Complete refers only to this defined profile; Backend Validated requires
actual native evidence. Platform Validated/Production Ready require broader backend,
platform and RenderDoc acceptance; unavailable capture is recorded explicitly.


2026-10-05 verification checkpoint: OrderedOriginalCopyAttachment/1 selected
WebGPU executor is Contract Complete / Prototype Complete / Backend Validated.
Host11/11, Wasm/browser8/8, inventory150/150 passed. Genuine original18 copy3
pass2 gives exact red-clear/copy-red then green-clear/copy-green texels; transfer
only7/copy7/pass0 gives actual queue completion. Native second-pass usage error,
post-submit cancel quarantine, caller-drop, and actual device loss with four
outstanding receipts pass. All nine quotas retire to zero; cold peak1684 passes
exact/one-less/exhausted plus retry and hot core allocation0. Prior /1 lifecycle
probes remain successful. Evidence: ordered-copy-evidence.yaml, final host/web
logs/XML/reports in build/web-bindless-apple-debug/artifacts/bindless-continuation.
RenderDoc unavailable (ordered-copy-renderdoc-diagnostic.yaml); complete Platform
Validated / Production Ready and full bindless execution remain unclaimed.

CanonicalOriginalUploadStaging/1 defines the next L2 prerequisite. It derives
required initialized buffer bytes from exact original BufferUpload declarations,
and packed texture bytes from the original immutable snapshot and destination
layout through the shared TextureUploadLayoutValidation Mapper. Packed row/mip
padding is normative; raw pixel bytes are a separate CPU retention count. Buffer,
packed texture and aggregate staging byte budgets are explicit independent limits.
The inspection allocates0 and publishes no partial requirement on any refusal;
its values provide no initialization/registration/recording/completion authority.
Four outcomes are Null ValidatorOnly; Vulkan/Metal/WebGPU DiagnosticFailure with
stable JOIN-MISSING diagnostics. Selected cold/lowering /2 still rejects uploads.

Native binding must retain exactly the original registered canonical object,
lease, allocation and generation. Backend-owned source initialization must seal
host/GPU writes, prove actual native byte layout and drain genuine initialization
queue/error receipts before source Ready. Buffer frame labels and texture caller
generation fields cannot issue native provenance. Texture binding must use the
exact snapshot/packed mapping/destination and genuine content-ledger expectation.
Every added native source/allocation/callback/CPU payload/resource owner requires
an authored count/byte domain; it cannot silently enter existing Source budgets.
Pending, foreign, missing, stale or aliased sources fail before record; there is
no automatic retry, fallback source, replacement canonical root or second journal.
Sources stay retained until actual canonical execution receipts drain or physical
loss invalidates them; post-submit uncertainty/cancel cannot release GPU owners.
The existing initialized staging/source/packing modules must be extended rather
than duplicated; audit canonical-original-upload-staging-next-audit.yaml identifies
the missing canonical registration and unique object adoption boundary.


Upload requirements verified 2026-10-05: host11/11 (fast contract and Null
hidden-frame included), Wasm/browser8/8, inventory151/151. Original buffer fixture
uploads2 requires16 native initialized bytes; texture fixture uploads2/mips4
retains40 CPU pixel bytes and requires1536 packed staging bytes. Exact/one-less/
invalid/missing/version/opt-in refusals publish no partial requirement; inspection
allocates0. Both real browser feature modes report requirements passed and prior
ordered profile/lifecycle probes remain clean. Evidence: upload-staging-requirements-
evidence.yaml and corresponding host/web logs/XML/reports. Contract Complete /
Prototype Complete read-only byte planning only; initialized canonical upload joins
still fail closed on all native backends. Upload Backend Validated, complete Platform
Validated and Production Ready remain unclaimed. Next: extend existing initialization
with actual canonical registration/lease/object/allocation binding, then texture
content-generation binding, then a new explicit ordered upload execution profile.


InitializedCanonicalBufferStaging/1 (2026-10-05 implementation contract): the
private factory adopts one actual GpuDevice-created, canonical registered buffer
under its original native-loss domain. It requires the exact complete CpuToGpu
CopySrc|CopyDst layout, committed initial CopySrc state, full payload size equal
to the genuine allocation footprint, and unsealed writes. Cold metadata, frozen
payload, original resource object, lease and callbacks have distinct explicit CPU
quotas; per-source native buffer1/retained bytes and owner bounds are independent
retention admission, while the genuine device allocator owns actual GPU capacity.
All inspection and cold allocations precede one-shot lease adoption/write sealing.
There is no replacement resource/registration, relabeled old source or size-only heap.

Initialization publishes MayWrite before a guarded real queue.writeBuffer call.
Three real error-scope receipts and one queue receipt must prove success before
Ready; one independent native loss subscription retains uncertain work. Existing
core ExternalCallbackGate/CompletionEvidenceBatch and native completion/loss
adapters own every foreign callback. Registration suppression is never queue
completion: unknown write/queue registration quarantines until true receipts or
physical loss establish lifetime. Cancel/caller drop before write detaches cold
callbacks; after write it retains owners until receipts drain or actual loss.
Native error never publishes Ready, and original logical shutdown/retirement/loss
closes validation even while exact owners remain retained. Frozen bytes and native
lease/payload/allocation/registration generation must still match at Ready joins.
Buffer frame serial is only an exact declared field comparison, never authority.

Four outcomes: Null ValidatorOnly; Vulkan/Metal payload-missing DiagnosticFailure;
WebGPU explicit private Prototype initialization payload. No public bindless or
NativeSync capability promotion, recording proof, root/journal, destination write,
texture generation or readback result authority follows from source Ready. Foreign
JS wrapper/queue internal bytes remain outside core CPU accounting and are an
explicit Prototype limitation. Backend Validated requires genuine initialized
bytes plus error/cancel/drop/quarantine/loss evidence, not this contract declaration.

CanonicalTextureUploadPackingSource/1 and CanonicalTextureUploadStaging/1
(2026-10-05 implementation contract): L2 binds the exact original texture upload
binder, snapshot owner, destination lease and immutable normative packing. Mip
count, tight pixel bytes, packed bytes and retained owners are independent bounds.
Packing is produced by the existing Mapper/copyRows path; no second layout or
padding algorithm exists. Wrapper, Source, Graph, packing metadata and packed-byte
CPU domains are independent. Every refused preflight allocates0; failed native
joins issue no mapping output.

The WebGPU private staging wrapper retains one genuine canonical initialized
buffer created through GpuDevice and the existing InitializedCanonicalBufferStaging/1
receipt owner. Its frozen bytes must equal every packed byte (including padding),
with exact size/frame equality. Ready requires actual queue plus three scopes,
original native loss, exact destination registration/lease/native allocation and
captured committed resource-state expectation. Foreign owners, altered bytes/frame,
pending/stale target state or changed native layout fail closed. Mapping reuses
TextureUploadPackedCopyMapping and compares the exact original packing-source owner.
No new callback slot, shader, native command, issuer or mutation journal is added.

This is staging provenance only. The destination state expectation's factory-issued
registration generation is a resource lifetime; UploadTextureDesc.resourceGeneration
and mapped contentGeneration remain authoring equality labels. They cannot prove
GPU target content. The old TextureUploadContentTransaction uses its own journal
and commits before the canonical completion boundary, so it cannot implement this
join. A genuine target content expectation integrated with the existing canonical
root/journal and consumed by the two existing native issuers remains required.
All content execution gates remain closed, including WebGPU's stable
RHI-CANONICAL-TEXTURE-STAGING-CONTENT-EXPECTATION-MISSING diagnostic.

Four outcomes: Null ValidatorOnly; Vulkan/Metal DiagnosticFailure payload-missing;
WebGPU explicit private Prototype staging join. Contract Complete/Prototype Complete
may refer only to this limited staging binding after tests. Backend Validated needs
real packed-byte readback and physical-loss evidence. Platform Validated/Production
Ready, generic/bindless/NativeSync and ordered texture upload execution remain
unpromoted. RenderDoc is unavailable on this host; native byte readback grants no
visual acceptance. Foreign browser allocation byte sizes remain explicitly unmetered
Prototype work, with no hot core allocation, IO or compilation.

CanonicalTextureUploadStaging/1 additional admission: wrapper metadata cannot alias
any of the five initialized-buffer CPU domains or any retained packing-source CPU
domain, including packed storage. One native target and its actual destination
texel footprint have an explicit independent limit; source packed bytes cannot pay
for target retention. These are retention bounds; original backend allocators still
own genuine allocation capacity. State expectations are not content expectations.

CanonicalTextureUploadStaging/1 verified 2026-10-05: host11/11 (quick contract and
Null hidden-frame included), Wasm/browser8/8, API inventory155/155. Real canonical
staging768-byte readback validates two mips/20 tight pixels and every zero-padding
byte against independent expected rows. Cold exact/one-less/exhausted/retry,
foreign/forged/quota/byte/count/pending/stale/layout refusal and real physical-loss
four-receipt drain/all14 quotas zero pass; hot core allocation0. Rollback invalidates
prior expectations even when state values restore. Evidence canonical-texture-
staging-evidence.yaml and final host/web logs/XML/reports in bindless-continuation.
Contract Complete / Prototype Complete / Backend Validated selected staging only.
Factory-issued target content ownership/journal integration and ordered uploads
remain incomplete; content execution fails with the stable expectation-missing
id. No broad Platform Validated/Production Ready or visual acceptance; RenderDoc
unavailable diagnostic saved. Continue canonical-texture-content-next-audit.yaml.


CanonicalTextureContentCreation/1 and CanonicalTextureContentPlan/1 (contract):
The explicit L2 factory accepts one new whole-texture content domain per creation
call, an authored texel footprint/generation ceiling and an independent CPU content
quota. Ordinary TextureDesc never opts in. WebGPU uses the genuine canonical device
resource factory, allocation, registration and immutable OriginalUploadExclusive
native access policy. It never relabels an older texture. Null is ValidatorOnly with
creation unavailable; Vulkan/Metal return payload-missing DiagnosticFailure;
WebGPU exclusive creation is explicit Prototype, native content execution remains
unavailable. Fresh Uninitialized/generation0 expectations describe domain ownership
only, never initialized GPU texels or successful asynchronous validation.

Full exact original uploads enlist that texture-owned control in the existing
OriginalCommandStateTransaction journal, with explicit cold plan control/upload
bounds and additional retained-owner capacity. Normative upload Mapper/extent
validation rejects partial regions/ranges. Control-issued monotonic generations are
independent of resource registration, state revisions and caller generation/frame
labels. Rollback restores the value while consuming a new revision and never
recycles issued generation IDs. Pending/stale/foreign/unissued expectations fail
closed; public CPU commit refuses content plans. Only the existing seal's private
native completion path can finalize Completed in the same state/content journal,
after genuine queue/error receipts. No additional proof issuer or independent
content transaction is introduced. Upload execution integration and genuine target
output remain open; do not mark b3b complete from creation/CPU proposals. No broad
Backend Validated, Platform Validated or Production Ready claim.

CanonicalTextureContentCreation/1 and Plan/1 verified 2026-10-05: host11/11,
Wasm/browser8/8, inventory156/156. Creation content216 and root plan2036 cold
peaks; exact/one-less/exhausted/retry, resource/registry and independent domain
failures publish no partial resource/plan. Genuine exclusive canonical native
creation, unissued ordinary/foreign/stale expectations, partial range refusal,
generations1/2 then rollback/drop3/4 and exhaustion pass; public commit refuses.
Actual physical loss invalidates observation; all five creation quotas zero, hot
allocation0. Contract Complete / Prototype Complete selected creation/CPU proposals;
Backend Validated selected WebGPU exclusive creation only. Native target texels,
queue/scopes content completion and ordered upload execution remain unproven/open.
Evidence canonical-content-creation-evidence.yaml and final host/web logs/XML/reports.
RenderDoc unavailable; no broad Platform Validated or Production Ready claim.


CanonicalTextureContentStaging/1 prerequisite: a backend-private composed wrapper
retains the exact existing initialized canonical texture staging and a fresh
factory-issued destination content expectation. Its separate metadata quota cannot
substitute any staging/packing/input/buffer domain or content control quota. A
bounded retained-owner closure pays for the new wrapper and exclusive content
control. Ordinary/unissued targets, pending/stale/foreign/forged sources, whole-range
violations and quota pressure fail before publication. Source packed bytes, target
state revision, factory registration generation and content generation remain
separate. The wrapper validates committed content ownership only; after same-root
proposals it refuses public ready observation. It issues no GPU command, completion,
new journal or proof issuer. Ordered native upload integration remains closed with
RHI-CANONICAL-TEXTURE-UPLOAD-ORDERED-EXECUTION-MISSING on a valid prerequisite.
Null is ValidatorOnly; Vulkan/Metal are payload-missing DiagnosticFailure; WebGPU
is explicit Prototype prerequisite only. No target texel/output readiness claim.

CanonicalTextureContentStaging/1 verified 2026-10-05: host11/11 (quick contract
and Null hidden-frame), Wasm/browser8/8, API inventory157/157. Metadata peak124,
exact/one-less/exhausted/retry; all sixteen CPU domains independently checked,
foreign-device/content/forged-source/not-ready/version/opt-in/quota/owner refusal.
Same-root proposals return Pending; rollback consumes revision and wrapper remains
stale. Real physical loss refuses observation; sixteen quotas zero, hot allocation0.
Contract Complete / Prototype Complete selected prerequisite; Backend Validated
selected WebGPU initialized source/exclusive target binding only. Ordered upload
execution and genuine target content queue/scopes completion remain closed/open.
Null ValidatorOnly; Vulkan/Metal payload-missing. Evidence canonical-content-
staging-evidence.yaml and final host/web logs/XML/reports; continue
canonical-ordered-upload-next-audit.yaml. RenderDoc unavailable; no broad Platform
Validated or Production Ready claim. Parent content-completion task remains open.


OrderedOriginalUpload/1 defines the next explicit canonical native profile. It
preserves the original complete positional stream (ordered copy/empty attachment
vocabulary plus BufferUpload, TextureUpload and exact transfer BufferBarrier).
Upload staging, native copy-command work, texture-upload command counts and
texture-destination write texel work have independent authored limits. Destination
write work is summed per upload; it is not physical/resident allocation or unique
target retention. Native source/target allocation ownership must be separately
bounded by the same-root native closure and initialized-source join.

Inspection reuses OriginalCommandStreamProgram, OriginalCommandUploadStagingContract,
whole-texture upload validation, normative destination footprint and budget ledger.
All counts remain local until the complete stream/profile validates. Caller frames,
resourceGeneration and a CPU access plan never issue initialization/content/native
recording authority. Null is ValidatorOnly; Vulkan/Metal are payload-missing;
WebGPU remains explicit Prototype with native upload joins/record/execute closed
until the full native payload is implemented and tested. Existing /1 and /2 native
profiles do not inherit uploads. No default opt-in, second journal or third issuer.

Implementation must bind each upload ordinal to the exact original registered
initialized buffer lease/payload/allocation and exclusive target expectation.
Recording scopes validate every actual target copy use before queue admission;
an unvalidated creation handle cannot authorize submit. Whole-texture content
proposals enlist the original root journal; only successful real record scopes,
submission scopes, invocation and queue completion publish Completed. Pre-submit
failure drains then rolls back with new revisions/non-recycled IDs; post-submit
uncertainty/cancel/loss invalidates and quarantines. Native output evidence must
cover all destination bytes/mips and genuine lifecycle paths under authored
budgets, with hot allocation0, fast contract and Null/browser smoke. Target output
completion and full b4 remain open until those proofs exist.

OriginalInitializedUploadJoin/1 stores bounded cold entries/native observations in
the original Recording quota, retaining the same root and already created upload
proofs. It must cover every original upload ordinal exactly once, match original
buffer binders or texture packing sources, and require initialized staging leases
as explicit entries of the original canonical Source inventory. Genuine source and
destination allocations have independent deduplicated native byte/object bounds;
Source maxNativePayloadObjects/maxNativeStagingBytes remain execution admission
requirements. Owner capacity uses an explicit conservative per-entry closure bound,
including repeated proof references, never an observed native memory estimate.
No native creation/write/encoder/queue/callback occurs in join publication.
Prepared validation closes on pending/stale/root mismatch, changed payload/layout,
foreign registration/device, forged proof owner and quota-domain replacement. This
prerequisite cannot enable existing CopyPass/1 or OrderedCopyAttachment/1 uploads.


2026-10-05 ordered upload prerequisite validation: OrderedOriginalUpload/1 and
OriginalInitializedUploadJoin/1 are Contract Complete / Prototype Complete for
selected cold metadata only. Actual WebGPU buffer and composed texture proofs
join the same Prepared root at original ordinals 1/2, require the exact initialized
staging lease in Source, retain one unique source/target allocation, and consume
independent Source native object/staging limits and original Recording quota.
Two 20-byte texture writes count 40 texel work bytes while unique staging768 and
target20 remain separately bounded. Join peak876 supports exact/one-less/exhausted
retry. Pending, rollback/stale, foreign device, forged owner, wrong variant/order/
count, native layout and quota-domain substitutions refuse; actual physical loss
refuses both joins, all ownership quotas drain, hot core allocation0. Host11/11,
Wasm/browser8/8, inventory159/159. Backend Validated selected WebGPU native binding
only; no target write or native content completion claim. Null ValidatorOnly,
Vulkan/Metal payload-missing; execution remains closed. Existing journals and two
proof issuers unchanged. Evidence: ordered-upload-join-evidence.yaml and final
host/web XML/logs plus normal/indirectDisabled reports under bindless-continuation.
RenderDoc unavailable diagnostic retained; no broad Platform Validated or
Production Ready promotion. Continue explicit cold/lowering /3 and private
same-root Pending consumption; parent b4/b3b/b3/b/d3/full plan remain open.


CanonicalOriginalColdAdmission/3 selects OrderedOriginalUpload/1 and requires
OriginalInitializedUploadJoin/1 over the same root. Per-command buffer/texture
upload/mip/native-copy counts, declared buffer bytes, packed texture work, raw
pixel bytes and target texel work are authored independently of unique staging/
target resource retention. Failed derivation publishes zero requirements.
/1 and /2 require every /3 upload field to be zero. /3 uses the existing actual
Source-authored pending submission/scope/queue/loss capacities, same seal and
independent Recording/Submission/Callback domains; initialized proof quotas
(including pointer/control-block aliases) cannot substitute these domains.
Cold admission may retain initialized/native ownership but cannot issue a
command: RHI-ORIGINAL-NATIVE-COLD-UPLOAD-ORDERED-LOWERING-MISSING closes the
canonical command factory before any payload allocation or sticky claim.
Null ValidatorOnly and Vulkan/Metal payload-missing; WebGPU selected Prototype
cold join only. Target write/content completion remains unproven.


2026-10-05 ColdAdmission/3 validation: rebuilt host11/11, Wasm/browser8/8 and
inventory161/161. Real initialized buffer/texture joins, native resource closure,
original seal and Source capacity1-8-8 are retained together; cold metadata peak364
passes exact/one-less/exhausted/retry. Missing/forged and valid foreign-root joins
refuse; independent count/byte/owner limits and initialized callback quota pointer
plus control-block aliases refuse. Core allocation quota alias query reuses
existing allocation headers without another domain inventory, is unit-tested and
allocation-free. Failed derivation publishes zero work requirements. /1 and /2
reject upload fields. /3 refuses canonical command creation before allocation/
sticky claim; target writes and content completion remain closed. All tested
retained quotas drain, hot core allocation0. Contract Complete / Prototype Complete
selected cold /3; Backend Validated selected WebGPU ownership joins only. Evidence:
ordered-upload-cold-evidence.yaml, final host/web XML/log/reports and RenderDoc
unavailable diagnostic under bindless-continuation. No broad Platform Validated
or Production Ready claim. Continue b4b2 positional native payload and private
same-root Pending authority via ordered-upload-pending-lowering-next-audit.yaml.

2026-10-05 CopyPassLowering/3 cold projection contract (validation pending):
The /3 descriptor independently bounds regular copies and upload-expanded copies,
buffer commands/declared bytes, texture mips/pixel bytes, staging work and texel
work. /1 and /2 reject these fields. Every upload uses the retained exact-root
initialized native join and its original command ordinal; texture expansion uses
the existing whole-mip packed-copy mapper. Immutable upload geometry occupies a
separate bounded collection and is reinspected before publication. Sealed staging
transitions/writes refuse. Ordinary exclusive texture access remains closed.
Cold /3 may allocate the existing command and callback custody after complete
owner-budget admission. Issuer preparation fails before allocation with
RHI-ORIGINAL-UPLOAD-LOWERING-PENDING-AUTHORITY-MISSING until private same-root
Pending authority is implemented. No encoder work, queue submission, proof or
second journal is granted by projection. Null is ValidatorOnly; Vulkan and Metal
are DiagnosticFailure under OrderedOriginalUpload/1; WebGPU is explicit Prototype
metadata projection only. No Backend/Platform/Production validation is claimed.

2026-10-05 CopyPassLowering/3 projection validation: host11/11, Wasm/browser8/8,
inventory162/162. Both actual native browser modes freeze original buffer uploads
into two copies (Recording metadata peak800), and texture uploads into four exact
whole-mip copies (peak1024). Exact/one-less/exhausted/retry, independent work and
owner/CPU bounds, zero hot allocation and complete quota retirement pass. The
Pending issuer diagnostic remains closed. Contract Complete / Prototype Complete
selected projection; Backend Validated native cold ownership only. Parent b4b2/b4
and full bindless remain open. RenderDoc unavailable; no visual, Platform Validated
or Production Ready claim. Evidence ordered-upload-projection-evidence.yaml and
ordered-upload-projection-{host,web}-results.xml in bindless-continuation.

2026-10-05 InitializedOriginalBufferUpload/1 contract (validation pending):
This explicit Prototype subset consumes CopyPassLowering/3 with initialized
buffer uploads and zero texture uploads. Texture recording deterministically
refuses with RHI-ORIGINAL-UPLOAD-LOWERING-PENDING-AUTHORITY-MISSING until its
content authority is implemented. Null is ValidatorOnly; Vulkan/Metal payloads
are DiagnosticFailure; WebGPU may use the existing Recording/Submission issuers.
The private seal compares a creation-issued staging observation to the exact
root graph object/control/registration/state/revision, requires initial/final
CopySrc and unchanged actual Pending revision, and validates matchesPending on
the root's existing journal. Source readiness, drained initialization receipts,
native layout, frozen bytes/frame and leases are independently revalidated.
Public source validation must still refuse Pending. Frozen upload copy ranges
are emitted at the original operation positions in the existing encoder/scopes.
Only existing queue+scope completion may commit the same root; pre-submit failure
rolls back and post-submit uncertainty invalidates. No second journal, standalone
upload queue, proof issuer, generic capability or NativeSync promotion is allowed.

2026-10-05 InitializedOriginalBufferUpload/1 validation: host11/11, Wasm/browser8/8,
inventory163/163. Both real browser modes prove original target bytes16 after
queue+three submission scopes, staging revision unchanged, private real-journal
Pending authority and public source refusal. Success, pre-submit cancel, actual
record validation error, post-submit caller drop, post-submit cancel and actual
destroyed target submit error all pass; hot allocations0 and all quotas retire.
Contract Complete / Prototype Complete selected buffer subset; Backend Validated
selected WebGPU target payload/lifecycle only. Texture and full /3 execution,
distinct/interleaved output and upload-specific physical loss/uncertainty remain
open. No broad Platform/Production claim; RenderDoc unavailable. Evidence
ordered-upload-buffer-pending-evidence.yaml and final host/web XML/log/reports.

2026-10-05 InitializedOriginalTextureUpload/1 contract (validation pending):
The /3 private texture execution subset extends the same initialized-buffer
root authority to creation-issued texture state and content expectations. It
checks exact registration/control/initial state/revision/range and the original
command's CopyDst access steps, plus the original content initial observation,
real Proposed Pending observation and monotonically minted per-command generation
in the same root journal. The final proposed generation must equal the last
original upload for that control. Authoring generation/frame labels grant no
completion authority. Public source/content validation still refuses Pending.
Only existing Recording may consume the immutable ordinal-bound whole-mip copies
through a private exclusive-content emitter. Ordinary texture copy/read access
remains denied. Existing genuine queue+scope/drain completion commits state and
content together through the sole seal. Null ValidatorOnly; Vulkan/Metal explicit
payload-missing DiagnosticFailure; WebGPU explicit Prototype only. No general
bindless/NativeSync/Platform/Production promotion. Full distinct/interleaved output
and upload-specific uncertainty/loss validation remain separate required tasks.

The completed /3 cold facade continues to grant metadata ownership only, with
RHI-ORIGINAL-NATIVE-COLD-UPLOAD-COLD-JOIN-ONLY and
RHI-ORIGINAL-UPLOAD-LOWERING-COLD-PROJECTION-ONLY. The separately opted-in private
InitializedOriginalBufferUpload/1 and InitializedOriginalTextureUpload/1 execution
decisions are consumed by existing issuers before allocation. Buffer-only work
rejects texture inputs with RHI-ORIGINAL-INITIALIZED-BUFFER-UPLOAD-TEXTURE-PROFILE;
texture work consumes its own approved decision. These labels replace historical
missing-payload reasons; metadata objects still prove no native completion.

2026-10-05 InitializedOriginalTextureUpload/1 validation: rebuilt host11/11,
Wasm/browser8/8 and inventory164/164. Private exact original state/content/control/
registration/initial revision and per-command minted generation checks consume
real Proposed Pending observations in the same sealed root journal. Initialized
staging retains genuine ready/native evidence and unchanged CopySrc revision.
Both buffer and texture paths use original ordinal ranges through the existing
Recording and Submission issuers; proof friends remain exactly those two. Public
source/content Pending validation and ordinary exclusive texture copy/read remain
refused. Actual target readback verifies buffer16 and both texture mips20; texture
content becomes Completed generation2 with revision equal to initial plus two
proposals and one completion write, never authoring generation29. Each path's six
probes cover success, pre-submit cancel rollback, actual recording validation
error, post-submit caller drop retention, post-submit cancel quarantine and actual
destroyed-target submit error/invalidation. Hot allocations0 and all source/
Recording/Submission/Callback/device quota domains retire. The shared typed test
fixture reuses the same cold and native lifecycle builders for both resource kinds.
Contract Complete / Prototype Complete selected cold /3 and private initialized
upload execution; Backend Validated selected WebGPU target payload/lifecycle only.
Null ValidatorOnly; Vulkan/Metal stable payload-missing DiagnosticFailure. No
broad Platform Validated or Production Ready claim; RenderDoc tool unavailable.
Parent b4b/b4b2 closes for this explicit selected execution contract. b4c/b3b/b4/
d3 and full plan remain open for distinct/interleaved writes, upload-specific
physical loss/uncertainty and original readback completion/map/result ownership.
Evidence ordered-upload-texture-pending-evidence.yaml, final status host/web XML/
logs/reports and RenderDoc diagnostic in bindless-continuation. Continue through
ordered-upload-target-lifecycle-next-audit.yaml.

2026-10-05 interleaved upload and invocation verification: host11/11 including
fast contract/core and Null hidden-frame; Wasm/browser8/8 in normal and
indirect-disabled modes; inventory164/164. Buffer A/B uploads occupy ordinals1/9
in three legal transfer passes separated by encoder barriers; actual checkpoint
is A and final buffer B. Texture upload ordinals1/3 surround independent copy
element0; both final mips contain B and content is Completed minted generation2.
Unique native bytes64/1588, staging32/1536, native copies3/5; independent work
and capacity/ordinal negative gates pass. Shared typed fixture now12 lifecycle
probes per kind, including native before/after invocation throw, reverse actual
receipts, partial scope registration, native layout mutation and logical shutdown.
Extracted native probe preserves genuine GPU objects/promises/results and is
reused by existing canonical submission. No second proof/journal, early
publication or hot allocation; all quotas retire. Contract Complete / Prototype
Complete selected acceptance; Backend Validated selected WebGPU paths only.
Null ValidatorOnly; Vulkan/Metal DiagnosticFailure. Physical loss and queue/
scope-stack uncertainty remain open. No Platform Validated / Production Ready
promotion; RenderDoc unavailable. Evidence ordered-upload-interleaved-and-
invocation-evidence.yaml and host/web results/reports in bindless-continuation.

2026-10-05 upload physical-loss verification: host11/11 (fast contract/core +
Null hidden-frame), Wasm/browser8/8, inventory164/164, plus two fresh browser
modes for buffer and texture native scope uncertainty. Five loss paths per kind:
cold, recording with three real scopes outstanding, submitted with four real
queue/scopes outstanding, suppressed queue registration retaining invalidated
shell after scope drain, and genuine native pop-then-throw retaining three
scopes until actual device loss. Scope uncertainty causes subsequent canonical
recording refusal; no stack reset or guessed recovery. Exact device.lost and
genuine native promises/results prove loss/drain; same root/seal/journal
Invalidated, never Completed state/content, all source/device/packing/content/
Recording/Submission/Callback quotas zero, hot allocations0. Existing original
submission now reuses the same native loss probe; selected upload mode replaces
its final scope probe only, leaving ordinary recorded root loss-tested.
Parent initialized upload/content/ordered-execution b3b/b3/b4/b acceptance closes
for the explicit selected /3 private path. Contract Complete / Prototype Complete
selected acceptance, Backend Validated selected WebGPU only; Null ValidatorOnly,
Vulkan/Metal stable DiagnosticFailure. No broad Platform Validated or Production
Ready/public execution promotion. RenderDoc unavailable. d3c original native
readback completion/map/result and enclosing bindless plan remain open. Evidence
ordered-upload-physical-loss-evidence.yaml, host/web reports/XML, separate scope
mode reports and RenderDoc diagnostic in bindless-continuation.

CanonicalOriginalReadbackResult/1 contract-first cold prerequisite (Contract
Complete): exact original Request/destination ownership distinguishes aggregate
command work from unique final snapshot capacity. Two writes of the same Request
to its same destination require four mip copies and declared bytes1536, but one
final padded snapshot768 under independent Result CPU65536. Distinct Requests
sharing a destination are refused as ambiguous concurrent map ownership.
Reuses OriginalCommandStreamProgram, shared identity/allocation projections and
original layout Mapper; IDs/frame/generation labels and CPU metadata grant no
completion/map/publication authority. Failed inspection publishes no partial
requirements. Null ValidatorOnly; Vulkan/Metal/WebGPU stable payload-missing
DiagnosticFailure. Existing Cold/CopyPass /1-/3 remain unchanged and refuse
TextureReadback. Backend-private ordered readback, seal-bound completion, map
receipts and independent Result storage/ownership remain future gates.

Verified 2026-10-05: Contract Complete cold prerequisite only; host11/11
(including quick contract/core and Null hidden-frame), Wasm/browser8/8
(normal and indirect-disabled), inventory164/164. Both native browser reports
require canonicalReadbackResultRequirementsPassed. Exact/one-less budgets,
distinct genuine Requests sharing a destination, no partial requirements and
hot allocation0 verified. No execution/map/publication gate was opened; no
Backend Validated, Platform Validated or Production Ready claim for readback.
Evidence canonical-readback-result-contract-evidence.yaml and matching logs,
XML and preserved reports in bindless-continuation. Continue d3c2 exact native
bindings/projection, then d3c3 completion/map/Result publication.

CanonicalOriginalReadbackJoin/1 contract-first cold binding (Contract Complete):
original ordinal/Request/layout/destination and native allocation identities are
retained under the original root Recording quota; source/staging native budgets
count unique allocation tokens separately from aggregate copy work and Result.
Destination requires exact CopyDst/GpuToCpu and actual native CopyDst|MapRead,
Unmapped bridge/browser map state; GpuOnly or other usage refuses deterministically.
Mip decisions reuse TextureReadbackCopyLowering and the original validated layout.
Only ordinary source content is admitted; exclusive content requires its own
future same-seal read contract. Null ValidatorOnly, Vulkan/Metal DiagnosticFailure,
WebGPU private Prototype cold join only. Existing Cold/CopyPass /1-/3 still refuse
readback; no recording, map, result allocation/publication or legacy proof bridge.
d3c2b will consume these joins in a versioned same-seal recording payload.

Verified 2026-10-05 d3c2a: Contract Complete / Prototype Complete cold joins;
selected WebGPU native binding Backend Validated only. Host11/11 with quick
core/contract and Null hidden-frame, host WebGPU stub rebuild, Wasm/browser8/8
normal/indirect-disabled, inventory166/166. Original ordinals1-2/Request1/mip
copies4 retain exact source20/staging768, aggregate work1536 and unique Result768
requirement independently. Wasm cold peak1604; exact/one-less/exhausted budgets,
forged/equal/foreign/order/count/native mutation, actual native mapped buffer,
Pending/stale, caller-drop/shutdown and genuine physical device.lost tests pass.
Hot allocation0 and all eight quota domains retire. No Result storage allocated,
no recording/completion/map/publication authority. Evidence canonical-readback-
join-evidence.yaml, logs/XML/reports and RenderDoc unavailable diagnostic.
Next d3c2b introduces an explicit /4 same-seal ColdAdmission/CallbackCustody/
ordered recording profile; /1-/3 stay closed to readback. Existing State/Graph
retention and Recording/Submission issuers remain sole native authorities.
d3c2/d3c3/enclosing bindless remain open; no Platform Validated/Production Ready.

OrderedOriginalReadback/1 with Cold/CopyPass /4 (verified 2026-10-05): explicit
Prototype native ordered mip copies of ordinary source content only. /4 requires
independent readback command/mip/declared/unique-Result/CPU budgets and rejects all
upload/copy/pass fields; prior /1-/3 reject all new readback fields and commands.
The exact published seal/root/journal remains the only state authority; cold joins
bind frozen layout decisions to original Closure native observations. Only the
existing Recording/Submission issuers may record/submit; error scopes, genuine
queue completion and actual loss retain the same State/Graph until drain.
Null ValidatorOnly; Vulkan/Metal deterministic payload-missing DiagnosticFailure;
WebGPU private Prototype native-copy path. Dependencies use normative full-range
TransferDependencyLowering/1; no NativeSync promotion, second journal, dummy or
legacy proof bridge. Mapping/publication remain closed pending d3c3 independent
Result ownership and genuine same-root map/completion evidence.

Ordered readback verification: host11/11, Wasm/browser8/8, inventory167/167.
Ten native execution probes and five actual device.lost probes pass, including
reversed genuine scope/queue receipt order, real validation errors, mapped target
refusal, invocation before/after uncertainty, metadata change, logical shutdown,
queue/scope registration uncertainty and exact same-root callback quarantine.
Successful mip pixels match authored bytes1..20; all ten CPU quotas retire to zero
per probe, hot allocation0. Contract Complete / Prototype Complete and selected
WebGPU ordered native-copy Backend Validated only. Canonical Result/map/publication
remain closed; no Platform Validated/Production Ready. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-readback-ordered-*`.
RenderDoc unavailable on macOS, deterministic transfer evidence retained.
Next: d3c3a independent bounded cold Result storage, then same-root genuine map
and immutable publication. No legacy transaction/proof bridge.

CanonicalOriginalReadbackResultStorage/1 (verified 2026-10-05): cold independent
Result CPU allocation only. Exact original Requests deduplicate repeated writes;
unequal Requests/aliased destinations remain rejected by CanonicalResult/1.
Snapshot metadata/storage are allocated under Result, while the bounded cold
collection uses the exact original Recording quota and retains the original /4
Admission. Result must be independent from Source, Graph input/state graph,
Recording, Submission, Callback, original native resource allocations and original
device/loss/registration CPU owners. Capacity failure is atomic, no partial result
escapes. Snapshot retains only immutable Request/layout; it cannot retain root,
source/destination GPU objects or grant completion. No mutable byte view, native
map, publication, synthetic receipt or legacy transaction/proof bridge exists.
All four backend cold-storage outcomes are ValidatorOnly, map/publication false;
WebGPU preparation additionally validates genuine original native /4 joins.
Mapping must separately version Source-authored callback counts and same-root
State/Graph retirement before any map issuance. No Backend/Platform/Production
promotion follows from cold CPU allocation.

CanonicalOriginalReadbackMap/1 (verified 2026-10-05): L2 count/lifetime contract
only; no native issuer or map/publication gate opens. Each exact Request consumes
one map callback and a versioned three-scope batch, so one repeated Request adds
four slots to original eight: Source must author twelve callback tickets and
twelve registrations before seal publication. Source Result/CPU budgets remain
exact. Overflow/one-less failure clears every output. This planning contract
cannot consume fabricated completion events: future runtime must bind exact
same-root Submission/queue/scope evidence before map, preserve State/Graph and
loss ownership across map, copy/unmap before publishing, join all real map/scope
receipts, and retain owners until genuine completion/loss plus foreign callback
drain. Listener suppression alone cannot prove mapping or release its lifetime.
Published/Retired are terminal and cannot revive. Null ValidatorOnly; Vulkan,
Metal and WebGPU deterministic map-payload-missing DiagnosticFailure. Contract
inspection grants no Backend Validated/Platform Validated/Production Ready.

Cold Result verification: host11/11, Wasm/browser8/8, inventory168/168, clean
reports. Request1/writes2 -> one padded768 snapshot; Wasm Recording peak140,
Result peak840, independent exact/one-less pressure. Forged/equal/foreign/aliased
Source/Graph/state graph/Recording/Submission/Callback/native allocation/device
owner domains refuse. Snapshot survives original root/facade destruction without
native retention; drop and actual device.lost retire all eleven quotas, hot
allocation0. Contract Complete / Prototype Complete cold CPU storage only;
map/publication gates remain closed, no new native Backend/Platform/Production
validation. Initial peak-measurement fixture failure preserved and corrected by
a fresh non-exhausted original candidate; production budgets unchanged.
Evidence `build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-result-storage-*`.

Map count/lifetime contract verification: host11/11, Wasm/browser8/8,
inventory169/169; all current browser flags true with no diagnostics/native errors.
Reused Result/1 requirement validation rejects missing copies/submission, old8
Source callback limits, Result/CPU mismatches, one-less and overflow; no partial
requirements. Early map/publication, suppression-only release, unknown phase/event
and terminal revival refuse; hot allocation0. Contract Complete planning only;
native map/publication remain closed on all four backends. No Backend/Platform/
Production promotion. Evidence `build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-readback-map-contract-*`.
Next runtime audit: /5 adds per-Request four cold callbacks to the exact original
State/Graph; foreign-drain must include them and preserve original loss ownership
before current Submission retirement. emdawn bridge state and actual native
GPUBuffer.mapState must agree; reuse genuine C map/read/unmap algorithms without
legacy transaction/proof authority or JS-only bridge compatibility.

Cold ReadbackMap profile contract (cold preparation verified 2026-10-05): NativeCold
and CopyPass /5 retain original readback-only /4 command/mip semantics and add
explicit CanonicalMap/1 requirements. Earlier profiles reject every map field.
Map counts derive from exact unique original Requests, using original recording/
submission/queue/loss counts plus four callbacks per Request; Source must author
the complete demand before seal/reservation. Cold /5 binds Result storage to the
exact Admission, with independent Result quota and immutable original Request
identity. CallbackCustody /2 allocates the derived bounded slot count; /1 stays
exactly eight. Owner demand includes all added slots and independent storage/
snapshot owners within original Recording/Submission/Callback lifecycle limits.
The original loss slot keeps index7; all foreign-drain/cancel loops must cover
additional slots. Null validates; Vulkan/Metal diagnose missing payload; selected
WebGPU permits only genuine cold native connection/projection/callback metadata.
Native map and publication stay unavailable: issuer creation must reject /5
through CanonicalMap's deterministic payload-missing diagnostic until a complete
same-root native map receipt/retirement path consumes this profile. No default
opt-in, legacy proof, second journal, placeholder or dummy success exists.

Cold map /5 validation: host11/11, Wasm/browser8/8, inventory170/170.
Request1 repeated writes2 yields map1/scopes3, original8 -> Source1/12/12,
independent Result snapshot768 and exact native copies4. Source8/11 capacity,
missing/forged/foreign genuine Result Admission, one-less owner/CPU limits reject.
Submission and Callback exact/one-less/exhausted allocation probes unwind and
retry without publication; genuine physical device.lost invalidates same root,
keeps Result unpublished and drains all eleven quotas, hot allocation0.
Contract Complete / Prototype Complete cold metadata only. All four native map/
publication gates remain closed; no map, publication, Backend Validated,
Platform Validated or Production Ready claim. Evidence canonical-map-cold-*.
Continue cold C-map receipts and same-State/Graph mapping retirement.

Canonical map receipt adapter contract /1 (selected adapter verified 2026-10-05):
L2 map intent remains exact original Request -> one MapRead over offset0 and its
padded byteCapacity; no map outcome may grant Submission or snapshot authority.
Null validates intent only; Vulkan/Metal diagnose absent receipt payload; selected
private WebGPU /5 may cold-bind C-map userdata and the existing three native
scope receipt adapters to the exact Result Request and original staging buffer.
Canonical execution/publication remain closed until same-State/Graph retirement
is implemented. Earlier /1-/4 profiles allocate no map receipts.

The backend-private native C adapter must consume actual wgpuBufferMapAsync,
wgpuBufferReadMappedRange and wgpuBufferUnmap algorithms, requiring emdawn and
GPUBuffer.mapState agreement at each access boundary. Map ranges use spec-defined
four-byte size alignment, offset0 and explicit native MapRead usage. All adapter
objects/userdata live in cold Callback-owned original slots; no hot core-owned
allocation or new RecordedCommand proof issuer is allowed. Native scope listeners
are the existing cold NativeCompletionReceipt/1 payloads. Caller/enum/boolean
cannot synthesize delivery. C-map callback reports only native map status.

Guarded native invocation must catch JS throws across a trivial C thunk. Unknown
map/read/unmap invocation fails with stable diagnostics, preserves the exact cold
slot/root/Graph, and never claims callback/lifetime completion. C callbacks may
arrive synchronously when wgpuBufferUnmap aborts a pending emdawn future; caller
must retain the original state through the entire call. Only actual callback plus
bridge/native unmap agreement, or a proven native loss teardown with all future
userdata drained, permits release. Listener suppression alone is insufficient.
Do not manually alter emdawn state/future tables. External emdawn event allocation
is opaque and hot in this library version: selected use remains explicit
Prototype; core hot-allocation0 is not an assertion about bridge/driver allocation,
and cannot promote this path to Production Ready.

Canonical ReadbackMapDrain/1 contract (pure contract verified 2026-10-05): native
map retirement is separate from independent Result publication. Facts describe
original /5 expected maps, actually invoked C maps, genuine map callbacks,
copied-and-unmapped maps, map scopes drained/successful, original foreign receipt
drain and genuine queue/loss evidence. These are validation inputs only; no public
facts/enum/bool may create native authority or retire the original graph.

Each issued C map must deliver its actual callback and prove bridge/native unmap
agreement before its raw userdata or original graph can retire. Every issued map
owns exactly three original scope receipt slots. Suppressed scope listeners may
count only toward foreign userdata drain, never scope success; suppressing map
listeners is not supported by the C adapter. Invocation uncertainty, cancel,
native map/scope errors and physical loss invalidate-and-retain until original
callbacks, every issued C map and safe unmap have drained. Queue suppression is
not GPU completion: MaySubmit requires genuine queue receipt or actual loss even
when all userdata is suppressed. Inconsistent/overflow facts fail closed.

Successful drain requires genuine same-root Submission proof, all expected maps
issued, exact copies/unmaps, all scopes successful and all foreign receipts
drained. It permits native graph retirement with Result still unpublished, never
snapshot publication. Null validates these rules; Vulkan/Metal/WebGPU canonical
execution remain deterministic DiagnosticFailure until their private native
runtime consumes this contract. Existing /4 success retirement must remain
unchanged; /5 must keep the existing State/Graph/loss/seal across mapping and use
Recording/Submission as the only RecordedCommand proof issuers.

Canonical map receipt / drain validation 2026-10-05: host11/11,
Wasm/browser8/8, inventory172/172; both canonicalNativeMapReceiptPassed and
canonicalMapDrainContractPassed true with clean reports. Native C-map adapter
passes eight real execution/error/uncertainty probes and genuine pending-map
physical loss. Native map/read/unmap validates emdawn and actual GPUBuffer state;
read768 bytes match the authored native seed. Throwing unmap clears emdawn's
onUnmap list before its native call; retry state mismatch fails deterministically,
and the negative fixture requires explicit native buffer teardown. No guessed
bridge repair exists. Cold scope receipt partial failure unwinds and retries;
original /5 Source1/12/12, independent Result and original loss slot remain.
ReadbackMapDrain/1 prohibits release before C callback/unmap/scope drain, rejects
suppression as success or queue lifetime, and allows only unpublished native
retirement. Contract Complete / Prototype Complete cold receipts; Backend Validated
selected standalone C-map adapter only. Drain is Contract Complete only. Canonical
map/publication four-backend gates still closed; no Platform Validated or
Production Ready claim. Core hot allocation0 excludes opaque/hot emdawn event
allocation, whose selected use remains explicit Prototype. Preserved failures and
final reports: canonical-map-receipt-* and canonical-map-drain-*. Continue
same-State/Graph native map lifetime before original Submission retirement;
pending Result/joins need explicit original-seal validation. RenderDoc unavailable
on macOS; no visible output changes or broader visual/platform validation claim.

Pending canonical map validation contract /1 (retained validation verified 2026-10-05):
ResultStorage retains its exact original Admission and seal when the root is
sealed Pending; cold creation still requires Prepared. A private retained
validator must preserve immutable Request/layout/Result quota binding and all
original CPU domains, source/native allocations, descriptors, capability and
lifetime checks. Result bytes stay unpublished and no validator issues commands,
commits journal, publishes bytes or synthesizes a proof.

Only the exact genuine NativeBufferMapReceipt bound cold to that Request's
original staging buffer may change the active Request's map-state validation.
Prepared expects Unmapped, Pending requires bridge/native Pending agreement,
delivered native success expects Mapped until proven unmap then Unmapped. Every
other Request still expects Unmapped. Unknown/failed/uncertain C-map receipt cannot
relax state; foreign original Request/buffer/byte range/seal/Admission or aliased
ownership fails deterministically. All repeated writes of the same Request use
the same active validation, never a backend handle lookup heuristic or wildcard.
Null validates semantics; Vulkan/Metal diagnose missing native map payload;
selected private WebGPU permits read-only exact retained binding validation only.
Canonical map/publication gates stay closed until same-State runtime consumes
actual Submission/receipt/drain evidence. Native smoke may exercise genuine
mapping under a retained original graph solely as a fixture; validation cannot
prove that original GPU work was submitted or authorize early map execution.

Pending retained map validation verified 2026-10-05: host11/11,
Wasm/browser8/8, inventory173/173; canonicalRetainedMapValidationPassed true on
both browsers. Same original sealed Recording root/journal/Admission remains
retained through genuine C receipt Prepared/Pending/Mapped/Unmapped validation;
exact staging768 and native read768 bytes match the authored fixture seed.
Repeated Request writes2 share active proof; other Requests must stay Unmapped.
Missing pair, foreign genuine seal/Request/buffer/receipt, forged owner, wrong
size, failed/uncertain native receipt and cancelled/stale root refuse. No
Original Submission or Result publication authority is inferred. Metadata
headers forward-declare the native receipt, preserving offline Wasm contract
ABI; no guessed legacy callback ABI compatibility. Core hot allocation0 and
all eleven quotas retire. Contract Complete / Prototype Complete retained
binding validation only. Canonical map/publication gates still closed, no full
map Backend/Platform/Production promotion. Evidence canonical-map-retained-*.
Continue original Submission -> map callback consumption, private unpublished
Result copy and same-State MapDrain retirement. No visible output changes;
RenderDoc remains unavailable on macOS.

Same-State native readback execution contract /1 (selected /5 runtime integrated):
selected /5 is an explicit Prototype opt-in with Source-authored callback and
ownership budget; public capabilities do not automatically enable mapping.
Genuine original Submission invocation plus queue/scopes success must be latched
inside the existing private State before any native map call. The journal stays
Pending, original loss remains armed, and the original Recording/Submission
issuers, seal, graph ticket and native allocations survive through mapping.

Cold map slots and three native scope adapters per exact unique Request are the
only accepted payloads. Process original Requests in frozen order, at most one
active map/scope batch at a time; this is a declared bounded scheduling policy,
not a compatibility fallback. Additional unissued Request buffers remain
Unmapped. Map callback validates exact retained binding, copies only into that
Request's already-allocated independent unpublished Result range, and proves
native/emdawn unmap before completing its foreign slot. Scope callbacks only
consume native evidence; they never create command or Submission proof.

Private byte writes must not expose any public writer, mutable snapshot, native
handle, native enum or premature publishedBytes. Repeated original writes retain
one final snapshot; no cloned Request/layout/buffer or temporary hot CPU storage
may replace it. Snapshot publication stays closed until its separate canonical
publication contract/runtime is complete.

Success consumes ReadbackMapDrain::RetireUnpublished then calls private original
seal commit through the existing Submission issuer; loss detach and native graph
retirement follow all actual map/scope/unmap evidence. Cancellation/error/throw/
registration uncertainty invalidates-and-retains; original queue/loss evidence
and every issued C future plus safe-unmap must drain before owner retirement.
Native unmap mismatch after a throw cannot be repaired by emdawn table mutation;
physical-loss teardown may destroy the exact quarantined native buffer only when
actual callbacks and bridge/native teardown prove its raw userdata safe.
Suppressed listener is never output/GPU completion. Existing /4 retirement stays
unchanged. All error paths require stable diagnostics and bounded original
retention; core hot allocation0 excludes opaque emdawn event allocation, so this
selected path remains explicit Prototype. Null is ValidatorOnly; Vulkan/Metal
have deterministic missing native payload diagnostics; WebGPU mapping availability
may be promoted only after the complete private executor and its genuine native
negative/loss/drop/cancel probes consume this contract. No capability inference
from backend/OS name or Dummy success is permitted.

Same-State map /1 verification (2026-10-05): Contract Complete / Prototype
Complete; Backend Validated selected WebGPU /5 unpublished map/copy/drain only.
Host11/11, Wasm/browser8/8 and inventory174/174 pass; both reports clean with
canonicalMapExecutionPassed and preserved /4 ordered readback. Real authored
texture20 traverses original writes2/copies4 and exact staging768 into independent
unpublished Result768. Seven success/error/order/cancel/drop probes and four
physical-loss/uncertainty probes prove same-State Pending journal/loss retention,
genuine C callbacks, safe unmap/destruction, MapDrain retirement and eleven quota
domains zero. Scope-stack poisoning stays fail-closed until actual physical loss;
no fixture resets it. Public capability/default and publication remain closed.
Null ValidatorOnly; Vulkan/Metal deterministic missing-payload diagnostics;
WebGPU selected Prototype/unpublished. Core allocation0 excludes opaque hot
emdawn event allocations. No Platform Validated or Production Ready claim.
RenderDoc unavailable on macOS; no visible rendering change. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-map-execution-*`.

CanonicalSnapshotPublication /1 (selected /5 runtime integrated):
Publication requires a separate explicit versioned Prototype intent, consumed
cold with the same /5 original Result/Admission. It adds no callback, native
allocation, temporary byte range or Source capacity: the existing Source-authored
Result and map/callback limits are the only budgets. Mapping-only /5 remains
unpublished. A public fact/enum/validator result cannot issue publication or proof.

The existing original Submission issuer must prevalidate the genuine same-State
MapDrain RetireUnpublished result, exact original Request/layout and independent
Result-quota binding, complete once-only unpublished snapshot coverage and absence
of terminal/cancel/loss/error/uncertainty. Original seal/root commit and empty
journal are mandatory before freezing all independent snapshots. Freeze is a
bounded allocation-free CPU operation with no native calls or callback reentry;
prevalidate the complete set before changing any publication flag. Any failed
precondition keeps every snapshot unpublished and reports a stable diagnostic.
Already-published snapshots cannot be written, mapped, republished or invalidated
by subsequent native resource loss. Independent snapshots retain only original
CPU Request/layout/Result quota and immutable bytes, never State, root, native
closure/allocation, Recording/Submission or loss subscription. Caller shell drop
while submitted still cancels/quarantines; it must not publish from future output.

Reuse the existing owned-snapshot public const-byte access shape; its Legacy
publication/CompletionSignal paths cannot supply canonical native authority.
Null remains ValidatorOnly; Vulkan/Metal publication payloads are deterministic
DiagnosticFailure with distinct stable ids; WebGPU selects explicit Prototype
only through separate cold publication opt-in. Broad Backend/Platform/Production
promotion remains prohibited. Contract-only and selected runtime verification
checkpoints below distinguish the validated scope and remaining platform limits.

CanonicalSnapshotPublication /1 contract verification (2026-10-05): Contract
Complete only. Host contract/inventory/Null hidden-frame3/3, Wasm offline
contract/inventory2/2, inventory175/175. Missing native drain/commit/bindings,
duplicate/partial coverage, terminal/cancel/loss/error/uncertainty, version and
opt-in reject. Publication gates remain closed; native runtime integration is
next. Evidence `artifacts/bindless-continuation/canonical-snapshot-publication-contract-*`.

CanonicalSnapshotPublication /1 runtime verification (2026-10-05): Contract
Complete / Prototype Complete; Backend Validated selected WebGPU /5 immutable
CPU snapshot publication only. An optional separate cold Intent/1 selects
publication; absent intent preserves mapping-only unpublished behavior. The
existing original Submission issuer consumes same-State MapDrain evidence and
commits the exact original root/seal before atomically freezing the prevalidated
snapshot set. Public callers cannot obtain a writer, native payload or proof.
Cold reuse/private writes reject frozen snapshots. No added Source slots,
callbacks, native allocations, temporary bytes or hot core allocations.

Host11/11, Wasm/browser8/8 and inventory176/176 pass; both reports clean with
canonicalSnapshotPublicationPassed, canonicalMapExecutionPassed and preserved
ordered /4 behavior. Seven success/order/cancel/error/drop probes and four
physical-loss/uncertainty probes verify publication suppression and native
retirement. A successful independent snapshot retains exact original CPU
Request/layout and Result bytes after all original graph/seal/issuer/native/
facade owners are gone, remains byte-identical through actual physical loss,
and drains all eleven quota domains when finally dropped. Original CPU Request
metadata is charged to the frozen input metadata domain, not inferred from a
Source/Graph role name. Core allocation0 excludes opaque emdawn map events.

The publication lowering now selects WebGPU Prototype only with separate explicit
opt-in; Null ValidatorOnly and Vulkan/Metal deterministic payload-missing
DiagnosticFailure remain. Map /1 itself still grants no publication. General
RHI/bindless capabilities remain closed. No Platform Validated / Production Ready
claim; RenderDoc unavailable on macOS and no visible rendering change introduced.
Evidence `build/web-bindless-apple-debug/artifacts/bindless-continuation/canonical-snapshot-publication-*`.
Continue complete ordered native executor coverage; enclosing bindless stays open.

CanonicalOriginalTypedSources/1 and CanonicalOriginalTypedInputs/1 now define
original Pipeline/Descriptor/Table/Query ownership, immutable artifact/element
relationships, positional command fields and independently bounded native demand.
See [the typed source contract](canonical-original-typed-sources-contract.md).
The declaration validators bind exact original registered resources/generation,
resource usage and explicit access/ranges; their successful CPU checks cannot
issue original typed adapters or native work. Null remains ValidatorOnly;
Vulkan/Metal/WebGPU each return a stable typed payload-missing DiagnosticFailure.
Status: Contract Complete declarations only; owned adapter/native typed execution,
Platform Validated and Production Ready remain open. Continue d3d2 on the same
canonical Source/Inputs/root/seal/journal, preserving the existing two proof issuers.

Original pipeline factory registration /2 (d3d2b) is **Contract Complete /
Prototype Complete** CPU lifetime/metadata custody only; see
[the versioned factory contract](original-pipeline-factory-registration-contract.md).
Explicit original layout/graphics/compute factories reuse the existing registry,
reserve before backend calls, retain the canonical core layout owner and copy
package metadata. Ordinary objects stay unregistered. Native-loss-bound typed
creation fails with `RHI-ORIGINAL-PIPELINE-NATIVE-LOSS-JOIN-MISSING`. Host12/12,
Wasm/browser8/8 and inventory179/179 pass, preserving Null hidden smoke and prior
map/publication checks. Descriptor/Query factory/content joins, full immutable
pipeline artifacts, typed Source/Inputs and native execution remain open; no
typed Backend Validated, Platform Validated or Production Ready promotion.


Original observed native loss binding /1 (f4eb2c2a2c2b4b2a/b): Contract Complete /
Prototype Complete for bounded CPU observer mechanics and independently opted-in
native subscription/storage preparation. Backend Validated covers actual WebGPU
handler1/callback1, slots3/owned4, binding328 bytes, exact device/domain, suppression
failure retention and physical-loss/reclamation only. No authentic typed observer
is issued; live receiver/custody/Recording/Submission remain DefinedOnly/closed.
The empty-table logical terminal path does not prove possible-submit drain.

Quick/Null5/5, Wasm10/10, inventory235/235 and sequential browsers2/2 pass; flags
379/380 preserve all375/376 and exact1094 prior message strings/counts. Three real
native deliveries/two suppressions clear userdata; caller-drop self-lease and all
CPU quotas reclaim. V1 fields/budgets/allocation messages and A43/B36/pixels3,16,3,12
remain exact. No extra native promise subscription, completion pool, state ledger
or proof issuer. RenderDoc unavailable; no Platform Validated/Production Ready
promotion. See `original-observed-native-loss-binding-contract.md` and
`typed-loss-observer-binding-validation.yaml`. Continue b2c through
`typed-loss-observer-binding-next-audit.yaml`; enclosing bindless goal stays open.


Original typed callback custody /1 (f4eb2c2a2c2b4b2c1): Contract Complete /
Prototype Complete selected authentic cold local observer bridge. Backend Validated
covers real WebGPU one-original-subscription reuse, local cancel/drop, logical
Retired/Shutdown retention, deferred native suppression failure and actual loss
reclamation only. Custody1 explicitly classifies shared native loss1 separately
from future native scope/queue7 and local observer1, reserving demand8. Old loss1
profiles refuse. Generic observer issuance and typed Recording/Submission remain
closed; granular cold preparation opt-in gates consume the versioned contract.

Existing templated State/Graph/sole completion pool and aliasing State slot avoid
extra receiver/native handler/ledger/proof authority. Submission476/Callback640
bytes, exact-less-exhausted independent pressure, failed-claim recovery, sticky
cancel/drop, forged owners and three real loss receivers (including logical
Retired/Shutdown) reclaim all independent quotas. Quick/Null5/5, Wasm10/10,
inventory236/236 and sequential browsers2/2; flags382/383 preserve379/380 and exact
1098 prior message strings/counts including legacy allocations. A43/B36 and
pixels3,16,3,12 unchanged; hot0. Browser173.82/173.45 under unchanged180 deadline.
RenderDoc RHI-RENDERDOC-TOOL-UNAVAILABLE; no Platform Validated/Production Ready
claim. Evidence `typed-callback-custody-validation.yaml`; see
`original-typed-callback-custody-contract.md`. Parent b2c/b2/b4b and full goal stay
open. Continue b3 with private retained/issuer next contract and live-next-audit:
actual issuer-created Pending, scope/queue receipts and possible-submit quarantine
are not proved by cold callbacks. Public Prepared guards and two proof friends
remain unchanged.


Original typed cold issuer preparation /1 (f4eb2c2a2c2b4b3a): Contract Complete /
Prototype Complete cold preparation only; Backend Validated genuine WebGPU native
receipt preparation, exact binding refusals, partial cleanup/retry and physical
loss only. Existing Recording/Submission and same State graph/pool/slots are reused;
seven native states/listeners14, owner-add3, Recording316/Submission288 bytes, no
extra loss handler or proof issuer. Native binding mutation5, independent exact/
one-less/exhausted, full Callback quota, sticky drop and physical loss states21
clear userdata and reclaim all independent quotas. Quick/Null5/5, Wasm10/10,
inventory237/237, sequential browsers2/2; flags385/386 preserve382/383 and exact1101
prior stable messages/counts and legacy allocation sizes. A43/B36 and pixels
3,16,3,12 unchanged. Final179.20/179.02 under unchanged180 deadline; initial failures/
timeouts retained and excluded. RenderDoc unavailable, no Platform Validated/
Production Ready promotion. Evidence `typed-issuer-preparation-validation.yaml`;
see `original-typed-issuer-preparation-contract.md`. Private Pending, real typed
recording/submission and parent/full goal remain incomplete. Continue b3b private
immutable binding factors, then authentic existing issuer-created Pending.


Original typed private immutable binding factors (f4eb2c2a2c2b4b3b): Contract
Complete / Prototype Complete private helpers reached through existing public
cold validators only. Closure native/source/registration/descriptor/device/owner/
allocation checks and payload exact record/pass/maxDraw/full-count replay reuse
existing scans; public Root/Prepared/Stream2/dependency/quota guards stay intact.
No new owned storage, native work, projection replay inside factors or execution
gate. Quick/Null5/5, Wasm10/10, inventory238/238, sequential browsers2/2 preserve
all flags385/386 and complete1104 stable messages/counts without exclusions.
Legacy allocations, native mutation/pass-bound/Pending refusals, A43/B36 and
pixels3,16,3,12 unchanged. Browser179.00/178.51 under unchanged180 deadline. Backend
Validated preserved cold native refusals/probes only; no private Pending proof.
RenderDoc unavailable; no Platform Validated/Production Ready promotion. Evidence
`typed-retained-binding-factor-validation.yaml`; see dedicated factor contract.
Continue parent b3 with `typed-retained-binding-factor-next-audit.yaml`: same real
State/graph/claim/seal/root/reservation/journal authentication and immutable frozen
dependency binding must precede actual existing Recording/Submission native work.
Full bindless goal remains incomplete.


Original selected typed native closure acceptance (2026-10-06): Contract Complete /
Prototype Complete / scoped WebGPU Backend Validated for original scalar-storage
whole and finite compute/graphics. Independently admitted original counts,
frozen positional scope visibility, seal/reservation demand8, observed original
loss and separate CPU domains are consumed by the existing Recording/Submission.
Authentic same-root Pending/journal checks precede immutable payload validation;
real scopes3+3/queue1 and actual native completion precede original sealed commit.
Public cold validators remain Prepared-only; legacy Cold1-5 remain State1/Closure1.
Metadata-only objects retain their historical false/closed diagnostic contracts;
OriginalTypedNativeExecution/1 is the separate explicit Prototype execution opt-in.

Whole and independent Source4 target-memory finite two-set compute produce A43/B36.
Whole graphics preserves all pixels3,16,3,12; Source3 runtime-profile finite graphics
produces1,16,1,1 from exact target view/stride. Old Source3 +7 profile refusal remains.
Pending payload/dependency/position/attachment/draw/finite-footprint mutations,
independent exhausted-quota hot0/query0, cancel/drop/native errors/reverse delivery,
uncertain native invocation/registration, Retired/Shutdown and old/Source4 actual
physical-loss4 with foreign drain are verified. The two loss matrices use distinct
native device sessions; sticky scope-stack uncertainty is never cleared.

The current closure audits are `original-typed-native-closure-audit.yaml` and
`original-finite-subrange-closure-audit.yaml`; they supersede the earlier pending
checkpoint statements above only for their selected scope. Current quick/Null5/5,
Wasm10/10 and inventory244/244 pass. Final native four sessions26/26 and exact
historical flags385/386,1104 strings/1683 occurrences are retained without
exclusions. Recording316/Submission288,180-second deadline and Asyncify16384 remain
unchanged. Evidence: `target-finite-execution-validation.yaml` and comparator.

Null is ValidatorOnly / DiagnosticFailure; Vulkan/Metal selected typed original
payloads remain DiagnosticFailure with stable missing-issuer labels. Generic
OriginalRequest2, uniform/table/general ranges and public BindlessCross capability
remain closed. Accepted original Table work next requires both target-language
eligibility and an exact versioned resource-array ABI, or separately proved
complete-program Equivalent lowering; current real Compute/Fragment
WGSL rejects sized_binding_array and the pinned bridge remains scalar-only.
See `original-table-native-actor-next-contract.md` and its next audit, DefinedOnly.
RenderDoc: RHI-RENDERDOC-TOOL-UNAVAILABLE. No visual, NativeSync, Platform Validated,
Production Ready or full bindless completion is claimed.

Current f4c3b compiler prerequisite (2026-10-06):
OriginalDepthTableScalarExpansion/1 is Contract Complete / Prototype Complete
for the two exact original sampled-depth programs. Logical Inputs capacity3 and
modulo selection are retained; Slang DepthTexture2D plus the shared bounded
switch emitter produce SPIR-V/MSL/WGSL/reflection without target rewrites. Exact
source/schema/budgets/ordered maps and binary/target interface negatives pass.
Real WebGPU shader-only Compute and Fragment selection, ordering and resource
substitution pass in both feature modes (360 pixels/1440 exact components per
mode, actual submitted module hashes). Original ordinary artifact readers
explicitly reject the distinct publication. Host/Null7/7 including full
generator24/24 and expansion5/5, ordinary refusal/Null4/4, Wasm10/10 and
inventory244/244 pass; initial stale host verifier provenance failures are
preserved and resolved by rebuilding, without test exclusions.

This prerequisite does not establish an original Pipeline/Table actor or native
receipt. All four original Table execution gates and native-array ABI1 remain
closed. Next f4c3c1 retains separate original logical/physical immutable lineage
under the defined original-table-expanded-input-next-contract.md boundary, then
genuine accepted creation, original owners/budgets/lifetime and f4c3d issuer work.
Evidence: original-depth-table-scalar-expansion-validation.yaml. RenderDoc remains
RHI-RENDERDOC-TOOL-UNAVAILABLE; no Platform Validated, Production Ready or full
bindless completion is claimed.

Current original depth Table publication increment (2026-10-08):
OriginalDepthTablePublication/1 implements explicit bounded cold publication of
the same logical Inputs capacity3 into three native scalar depth bindings, with
the existing descriptor candidate/creation-scope lifecycle. Compute and Fragment
browser tests validate atomic revision/receipts, real group acceptance/refusal,
original TableSource2 adoption and canonical custody with the same accepted
pipeline, all three views and parents, exact/one-less/exhausted CPU budgets,
allocation0 hot validation and final actual-loss/quota retirement. This is scoped
WebGPU Backend Validated cold publication/custody only; broader uncertain/foreign
publication lifecycle cases and positional shader usage remain open. Null remains
validator-only and the selected original Vulkan/Metal payloads remain diagnosed.
TableLowering1 and public BindlessCross execution remain closed. No command encoder,
queue submission, initialized depth, WOCC readiness or full completion is proved
by group creation. See original-depth-table-expansion-publication-next-contract.md
and original-depth-table-expansion-publication-validation.yaml for current scope,
evidence and the next complete recording/submission/completion boundary.

Current original depth Table positional increment (2026-10-08):
OriginalDepthTableBinding/1 is implemented through independently opted-in
OriginalCommandInputs/4. The existing records, Source2 inventory and separate
binding ceilings are reused. Actual Compute and Graphics browser tests join the
committed table to its exact retained accepted pipeline object; a second genuine
accepted wrapper over equal artifacts refuses. Scope, source, kind, generation,
revision, slot, opt-in, prior-version and graph-capacity negatives pass. Final
Inputs quota admission is measured at392/536 Wasm bytes with exact/one-less/
exhausted probes, and hot validation allocates0/queries0. Graphics retains a
genuine original Rgba16Float attachment and frozen Clear/Store pass intent;
canonical CPU owner counts with two roots are27/32. Pending content, native
mutation, reset and actual loss invalidate the retained Inputs; all14 CPU quota
domains retire at closure. Existing Inputs3/TableLowering1 results are preserved.
MinGW assertion-enabled host artifact/codegen/hostile/Null tests pass5/5, source
inventory249/249, package/root guards8 checks in2 suites, prior expansion browser
smokes4/4 and the selected publication/positional browser smoke1/1. Complete
enabled/disabled indirect-first-instance browser regressions pass2/2 with no
nativeErrors and all public execution flags false. This is scoped WebGPU Backend
Validated positional validation only: Consumption1, State/Stream/native closure,
initialized depth, recording/submission/completion and public Emulated remain
closed. Metal public Graphics, renderer/WOCC integration, the named real browser
GPU-driven workload and visual/Release-performance/lifecycle acceptance remain
part of the full objective. Current evidence and remaining gates are recorded in
original-depth-table-expansion-publication-validation.yaml.

Current selected depth Table shader-footprint increment (2026-10-08):
OriginalCommandDescriptorConsumption/2 extends the existing dry walker and
Inputs4 join with independently opted-in depth full-view footprints and separate
logical/texture access ceilings. Consumption1 remains buffer-only/Inputs3;
State2 explicitly refuses Consumption2 before its buffer visitors can consume
tagged TextureView accesses. Every pipeline descriptor set/table is required;
the current Compute fixture still refuses its missing set1 output. Graphics
projects every ordered logical table element from the same frozen Source2,
accepted pipeline and original artifact mapping, with exact view/parent source
ownership and generations, revision, mip/layer/aspect and Fragment ShaderRead
intent. A later pipeline rebind with a missing table emits no earlier access.
Actual browser owners exercise nonzero mip/layer ranges, independent capacity
failures, pending publication/native mutation/reset/loss invalidation and hot
allocation0/query0. This is Contract Complete / Prototype Complete for dry
projection only. No initialized texture, state/hazard, encoder, submission,
shader-selection readback, completion or public Emulated authority is claimed.
The complete original state/stream/native chain, mixed table/set Compute
ownership, Metal Graphics, renderer/WOCC and P4-P6 acceptance remain required.
Latest verification: assertion-enabled MinGW codegen/artifact/hostile/Null5/5,
source inventory250/250 and package/root8 checks. Existing expansion browser
preflight/emission/creation/registration4/4, selected publication/footprint1/1
(9.01 seconds), and rebuilt complete enabled/disabled indirect-first-instance
browser2/2 (246.42/245.27 seconds). Both complete reports have nativeErrors empty
and public Compute/Graphics/indirect/processor execution false. Four current
report SHA256 hashes are recorded in the publication validation manifest;
fullGoalComplete remains false and production budgets are unchanged.

Current selected depth Table render-union/state-capture increment (2026-10-08):
OriginalRenderDescriptorUsage/2 preserves every original render table bind,
including bindings shadowed before Draw, using the same bound-table projection
as Consumption2. Two binds project6 render uses versus3 active Draw uses.
OriginalCommandResourceStates/3 reuses that projection and the original committed
resource controls to capture explicit attachment ranges, all render-binding
ranges and shader-consumption ranges in one immutable bounded graph. The shared
OriginalRenderDescriptorUsageBudget value is consumed independently of shader
consumption and observation quotas; State1/2 reject its trailing intent.

Actual Graphics browser capture validates7/10 observations for one/two binds,
original parent/view/source and logical-element provenance, exact nonzero
mip/layer ranges, generations and committed revisions. Measured State3 metadata
admission is1036/1348 Wasm bytes with exact/one-less/exhausted probes; owner,
registration, logical/texture-access and observation/range limits refuse without
partial publication. Hot validation allocates0/queries0. Pending publication,
native metadata mutation, arena reset and actual loss invalidate the retained
state graph; Graphics finally retires15 independent quota domains (Compute14).

This increment is Contract Complete / Prototype Complete for selected CPU state
capture, not shader execution or initialized content. Captured Undefined cannot
be promoted to ShaderRead. Existing Sequence1/2, Stream2, UsageProjection1 and
Transaction1/2 reject State3 before buffer-only access, enlistment or mutation;
the State3 transaction plan has an explicit unavailable diagnostic. Texture-aware
sequence/access/usage/hazard and transaction/stream joins, ordered depth clears,
mixed table/set Compute output custody, original native Table closure and genuine
recording/submission/completion remain required. Public Emulated stays closed;
Metal public Graphics, renderer/WOCC and P4-P6 acceptance remain open. See the
publication validation manifest for the final current test/report evidence.

Final State3 verification: rebuilt MinGW assertion-enabled codegen/artifact/
hostile/Null5/5, source inventory252/252, package/root8 checks and browser7/7.
The complete enabled/disabled profiles pass231.83/247.97 seconds with nativeErrors
empty and public Compute/Graphics/indirect/processor false; selected publication/
render/state capture passes17.73 seconds. Current report hashes and saved host/
browser CTest logs are in the publication validation manifest. Full objective
completion remains false; no production budget was raised.

Current Sequence3/Stream3 implementation checkpoint (2026-10-08):
State3 texture points now retain state/stage/access from the original projector.
Sequence3 resolves exact cells at every bind/consumption, checks scope and usage,
and charges the combined render/shader logical ceiling while avoiding duplicate
texture step charges. The genuine browser Graphics recipe's four explicit
barriers, two binds and Draw validate11 commands /14 cells /14 steps /9 implicit
accesses; missing/late/wrong-range/wrong-before/wrong-state transitions refuse.
The shadowed first bind remains required. Declarations do not initialize depth.

Stream3 reuses the same immutable State/Inputs/Source chain and existing
positional mapper with exact shader/render footprint matching. Its retained
program has11 commands /8 descriptors /8 references /2 table binds /1 Draw and
9 command-ordered implicit accesses. Exact/one-less/exhausted CPU admission,
owner/count/footprint/aliased-quota negatives, preflight visitor-output0 and hot
allocation0/query0 pass selected browser validation. Caller drop retains the
original graph; pending publication, mutated native metadata, arena reset and
actual physical loss invalidate the stream. Graphics retires17 quota domains.
Measured Wasm state metadata is1148/1508 bytes for7/10 ranges,1988 bytes for the
14-range transition recipe, and Stream3 metadata1140 bytes. These supersede the
earlier State3 footprint measurements after intent fields were added.

The selected Sequence3/Stream3 browser smoke passed23.54 seconds and rebuilt
assertion-enabled MinGW codegen/artifact/hostile/Null passed5/5; source inventory
254/254 and package/root8 checks pass. All seven browser targets now pass across
the first6/7 run and an independent enabled retry1/1 (226.09 seconds); disabled
passes245.17 seconds and selected publication19.37 seconds. The first enabled
attempt failed on a progress POST transport rejection; its complete failure
report and CTest log are preserved separately. Both final full reports have
diagnostics/nativeErrors empty and public execution flags false. This is not
a claim of a single clean7/7 run. Native stream execution remains
unavailable on all four backends. UsageProjection1, DependencyProjection1,
Transaction1/2, native closure2 and public Emulated remain closed for the new
texture profile. Ordered initialization, mixed Compute table/set custody,
recording/submission/completion, Metal Graphics and WOCC P4-P6 remain required.

Current texture usage/dependency checkpoint (2026-10-08):
UsageProjection2 and StateBoundUsage2 consume exactly the original State3/Stream3
and Consumption2/RenderBinding2 schemas. A distinct sampled TexturePoint retains
the complete original descriptor access, parent/view sources and generations,
frozen revision, logical element and exact mip/layer/aspect. Independent command,
contribution, attachment, sampled-access and texture-cell ceilings refuse before
visitor publication. The selected Graphics recipe projects6 render reads,
3 Draw reads and1 attachment, with10 contributions and10 contributed cells.

DependencyProjection2 reuses the existing captured join, compatibility validator,
original geometry and ordered interval neighborhood. Same-generation sampled
reads may overlap; overlapping attachment/shader writes reject. Every authored
texture dependency cell is inspected separately, without widening to the parent.
A single aggregate record requires homogeneous preceding/following endpoints;
unused or heterogeneous cells refuse. The four selected initial clauses charge
19 projection passes,190 contribution visits,45 pairs and10 endpoint contributions.
Endpoint state/stage/access coverage remains independent from state transitions.
Missing attachment-read coverage, an extra unused cell, one-less ceilings, old
profiles and forged original owners have explicit diagnostic paths.

Freeze1 and native Closure2 remain profile1-only. No native dependency owner,
recording, initialized-depth receipt, visibility or completion is granted by
this CPU projection. All four native execution gates remain unchanged. The first
genuine publication browser smoke passed34.15 seconds; final MinGW assertion-enabled
contract/codegen/artifact/hostile/Null regression passes6/6 and inventory256/256
plus package/root8 checks pass. The final browser suite passed6/7, with Graphics
publication exceeding its30-second profile timeout. The original failed evidence
is preserved. Two fresh longer-timeout runs passed the same final artifact;
the timed Graphics profile took31.27 seconds. The publication test alone now has
a120-second profile timeout; production budgets remain unchanged. Public WebGPU
Emulated, Metal public Graphics and WOCC P4-P6 remain incomplete.

Current texture Transaction3 checkpoint (2026-10-08):
The existing OriginalCommandStateTransaction now consumes the exact State3 graph
and both Consumption2 and RenderBinding2 footprints. A separate Recording quota
charges cold ownership/immutable plan storage; the same original controls and
single MutationJournal provide explicit and destructor rollback. The plan
preserves shadowed table binds, sampled texture cells and explicit attachments/
barriers. Buffer and texture contributions use separate validated paths.

The first genuine browser check validates4 original controls,4 final cells,
14 command-ordered access steps and4 transitions; measured Recording admission
is6396 Wasm bytes with exact/one-less/exhausted checks. Footprint, owner,
observation, range, registration and quota-domain refusal occurs before mutation.
Rollback leaves every parent cell Undefined, including cells outside the sampled
views, and advances revisions so the old State/Stream is stale. The same root
is retained through publication changes, native mutation, reset and physical
loss; Graphics retires18 quota domains. Hot application/validation/rollback
allocate0 and query0. CPU commit refuses with
RHI-ORIGINAL-TEXTURE-TRANSACTION-NATIVE-COMPLETION-REQUIRED.

Four-backend transaction outcomes are explicit: Null validator-only,
Vulkan/Metal missing selected original joins, WebGPU Equivalent CPU ownership
and plan preparation only; every native execution gate remains closed. Old
Closure1/2 cannot consume this profile. ContentPlan1 is still upload-only and
cannot grant partial attachment-clear initialization. ContentPlan2 requires exact
cell coverage, original registered attachment intent, ordered stored writes and
the same root's real completion; it remains unimplemented. There is no initialized
depth, table recording/submission/completion or public Emulated authorization.

Final assertion-enabled MinGW host regression passes6/6 (25.97s), including old
closure refusals and the transaction-specific execution diagnostic. The first
browser run passes6/7; the disabled-feature progress POST failure is preserved
with nativeErrors empty. Bounded ordered progress batching passes protocol units
and fresh complete enabled/disabled browser profiles2/2 (244.12/246.09s), with
complete journals, empty diagnostics/nativeErrors and public flags false. Five
prerequisite browser targets passed in the first run; all seven targets thus pass
across runs, without counting the preserved failure as success. Source inventory
is257/257 and package/root guards pass8 checks at that checkpoint. No Metal Graphics, renderer/WOCC
or platform/production readiness milestone follows from this CPU prerequisite.

ContentCreation2 prerequisite (2026-10-08): existing exclusive texture content
controls now support explicitly bounded fixed original mip/layer/aspect storage
using core VersionedMutationArray and original texture geometry. Range receipts
carry exact range, original registration generation and conservative array stamp;
all cells start Uninitialized. Independent CPU/cell/texel/mutation ceilings and
foreign range/invalid geometry/old profile refusals preserve fail-closed behavior.
Version1 upload controls/expectations are independent and cannot consume version2.
Null remains validator-only; Vulkan/Metal diagnose their missing selected payload;
WebGPU provisions sealed cold storage only. Final assertion-enabled MinGW
retention/Null smoke pass2/2 (1.04s), inventory258/258 and package/root8 checks pass.
Wasm builds; complete enabled/disabled Chrome profiles pass2/2 (242.35/253.66s),
content-cells hard gate true, complete ordered2123-record journals and empty
diagnostics/nativeErrors. Native D32 original nine-cell range observations,
exact1008 logical texel bytes, measured728-byte CPU quota exact/one-less,
invalid/foreign ranges, resource destruction and actual physical loss pass.
All five quota domains retire after expired weak receipts are released.
The initial two-profile quota-zero fixture failure is preserved; it retained a
range receipt's weak registry control block, and now checks invalidation before
dropping the receipt and checking retirement. Evidence lives under
build/web-webgpu-debug/artifacts/bindless-continuation/content-cells2-validated/;
first failures under content-cells2-range-ticket-first-failure/.
Contract Complete / Prototype Complete and scoped Backend Validated apply only
to cold creation/observation/loss of this selected cell domain. ContentPlan2 mutation/native
completion, table recording and public Emulated remain unimplemented/closed.

ContentCellSequence2 adds a bounded allocation-free L2 value timeline for the
same original geometry. Clear remains tentative until matching Store; Load needs
prior stored coverage, Discard removes it, and sampled reads reject active
attachment writers or uninitialized cells. Unknown action, wrong generation,
wrong/unclosed pass and cell/step/visit excess refuse without partial output.
Depth/stencil planes are distinct. The four-backend lowering grants validator
values on Null and CPU validation on WebGPU only; all native execution remains
closed. ContentCreation2's immutable native attachment policy is explicitly
distinct from OriginalUploadExclusive, so old private upload emitters cannot
reinterpret an attachment domain as their destination. Final assertion-enabled
host/Null passes2/2 (2.05s), source inventory259/259 and complete enabled/disabled
Chrome profiles2/2 (248.48/245.33s). Both have the sequence marker, complete2124
messages, empty diagnostics/nativeErrors and all public execution gates false.
Immutable reports live under content-sequence2-validated/.

ContentPlan2 original projection and CPU mutation increment (2026-10-08): the
existing transaction now consumes frozen original Begin/EndRenderPass arguments
and actual shader consumption footprints at their real command ordinals. Multiple
attachment contributions remain independent. Render bindings and barriers do not
initialize contents; subpass/resolve/readonly attachment joins remain diagnosed
until their coverage is proved. All represented resources require original cell
observations. Independent control/cell/step/command/projection ceilings bound the
dry preflight and immutable Plan; failure returns no partial counts. Original
content arrays enlist in the same MutationJournal as resource states. CPU final
proposals preserve untouched cells and use non-recycling control-issued IDs;
explicit/destructor rollback restores original values while advancing revisions.
Public CPU commit and private native closure both refuse completion. Null is
validator-only, Vulkan/Metal selected original joins absent, WebGPU CPU plan only.
Tests include a genuine registered D32 partial Clear/Store then Load/Store,
exact/one-less admission, quota pressure and hot allocation0. Final rebuilt
assertion-enabled host/Null passes2/2 (1.95s); complete enabled/disabled Chrome
profiles pass2/2 (244.01/246.14s), with the plan marker, complete2125 messages,
empty diagnostics/nativeErrors and public execution flags false. Measured Wasm
Recording admission is2652 bytes for1 control/9 cells/4 steps/60 command visits/
164 projection visits. The first two browser failures (fixture Recording budget
missing) are preserved under content-plan2-first-failure/; final immutable reports
under content-plan2-validated/. Production budgets are unchanged. Source inventory
passes260/260, package/root8 checks pass. Existing expansion host/codegen/hostile
tests pass4/4 (23.02s), Compute/Graphics publication browser smoke1/1 (50.02s);
Transaction3 measured Recording admission is now6484 bytes and18 Graphics quotas
retire. Contract Complete / Prototype Complete apply to this CPU join only.
Native execution, public Emulated, Metal Graphics, renderer/WOCC and P4-P6 remain open.

Selected table ContentPlan2 composition (2026-10-09) reuses the existing accepted
Graphics pipeline, original TableSource2 and canonical registry. Three genuine
Creation2 depth parents retain separate sampled and attachment views at matching
mip/layer cells; the actual CopySrc Rgba16Float output also has a canonical
Creation2 control. CopySrc grants creation only, never ordinary transfer/readback
or completed-content authority. The same Inputs4/State3/Stream3 graph contains21
commands, including explicit viewport/scissor, three Clear/Store passes, seven
barriers and final table consumption. Usage2/Dependency2 prove the CPU ordered
scope/access/dependency projection; ContentPlan2 projects4 controls/28 cells/
11 steps/609 command visits/10074 projection visits into the existing single
transaction Plan/journal. Wasm Recording admission is10164 bytes. Exact/one-less
ceilings, quota pressure, missing clear/wrong clear view/discarded content, no
partial proposals, explicit/destructor rollback and hot allocation0 pass. Actual
native device loss invalidates the retained table/output/stream/transaction and
all18 quota domains retire. Selected publication browser smoke1/1 (54.94s),
host/Null2/2 (0.98s), host expansion regression4/4 (20.83s), inventory260/260 and
complete Chrome profiles2/2 (248.19/244.30s) pass. Final reports are under
`build/web-webgpu-debug/artifacts/bindless-continuation/table-content-plan2-validated/`.
First diagnostic-expectation and transport probe-count failures are separately
preserved. This is Prototype Complete for original CPU composition only; native
command encoders/submissions remain0 and public execution false. Native
Dependency2 freeze/custody, recording, shader results and real completion are
the next joins; no Metal/WOCC/platform/production status advances.


Current mixed table/readback custody increment (2026-10-09): Closure4 retains
exact Join2, State3/Stream3/Inputs4 and ContentPlan3 under genuine
TypedNativeLossJoin2. Both canonical native endpoints are authenticated and all
composite Source2 entries are covered. Frozen Dependency3 pairs only Freeze3 and
Closure4; previous payload1/2 explicitly reject it. Existing core identity and
allocation projections count native owners/backing once across parents/views and
readback endpoints; slot counts derive from existing observation types.

Selected real Chrome validates14 payloads /112 closure owners /3600 backing
bytes /8 dependencies including the render-to-transfer clause. Wasm cold
admission measures4716 closure bytes and2932 freeze bytes; Join2 remains1048.
Legacy Closure3 now measures4396 versus historical4380 because its shared schema
retains the optional join and independent readback ceilings; quotas are unchanged.
Budget exact/one-less/exhausted, forged and authentically foreign programs/joins,
native destination mutation, Pending/Abandoned refusal and hot allocation0 pass.
The initial fixture peer-root attempt exceeded its authored one-transaction
ceiling; the repaired negative uses an authentic different program/join over
that same root, without increasing the ceiling.

The selected execution smoke passes1/1 in188.54 seconds with the unchanged
240-second profile /520-second CTest deadline. A bounded ProgressBatch1 journal
retains ordered timing evidence, including on timeout, without granting GPU
authority. Complete legacy Closure3/Cold2 physical-loss coverage remains in the
publication smoke; selected execution no longer repeats that independent case.
No cases are removed from the complete browser suite. First timeout, diagnostic
assertion and profiling failures are preserved separately and excluded from
success. The remaining seven browser regressions pass7/7 in637.09 seconds; enabled243.00 /disabled243.76 /publication126.35. All8 targets pass with selected1/1.

Status is Contract Complete / Prototype Complete selected cold custody, with
scoped actual WebGPU native-resource evidence only. Encoder1/submit1 belongs to
prior Execution2; Closure4 adds no native recording/copy/map/result. Numeric
shader-selection readback and retained same-Join2 physical-loss proof remain
missing; public Emulated, Metal Graphics, renderer/WOCC and P4-P6 stay open.
Evidence: build/web-webgpu-debug/artifacts/bindless-continuation/readback-custody4-validated/.
# TypedReadbackCompletion/1 continuation (2026-10-09, selected Chrome validated)

Independent Execution3 ReadbackCompletionIntent1 is frozen before recording;
submit must consume the same profile, original Cold3/Callback3 map custody,
ResultStorage2, Lifecycle2 seal and Join2. Exact Pending ownership is validated
before replaying admission-issued frozen counts. Existing native queue/scopes,
map/copy/unmap and callback drain must prove completion before the private
ContentPlan3 commit; only written cells complete. Separate pre-admitted
PublicationIntent1 governs result visibility. Null validator; Vulkan/Metal
missing payload; WebGPU Prototype opt-in. Public BindlessCross authorization,
Metal Graphics and renderer/WOCC P4-P6 are unchanged and remain incomplete.

Final rebuilt assertion-enabled MinGW/Null6/6 passes24.66 seconds and selected
Chrome1/1 passes184.10 seconds. Graphics success=true/exit0/errors=[] proves
encoder1/copy1/submit1, genuine map/unmap and exact4x2 RGB quarter/half/
threequarters/quarter with alpha1. The same journal commits, independent
publication and caller-drop release pass; only four of28 content cells change.
Hot core allocation0/device-limit-query0; Asyncify17716/32768 bytes with207
unwinds. Source291/291 and package/root8 pass. Final affected Chrome3/3 passes
643.21 seconds: legacy225.76, recording230.18 and cold-results187.25. Sixty
SHA256 hashes verify under typed-readback-completion1-validated/, including exact
sources captured before lifecycle continuation and binary identities.
This is scoped private Graphics/readback
Backend Validated only. Next require same-root submitted/map-pending physical
loss, unpublished terminal output and genuine callback drain; public execution
stays closed until the complete generic ownership/operation graph is verified.

### Same-root readback physical-loss validation (2026-10-09)

The same Execution3/Cold3/Callback3-map1/Result2/Lifecycle2/Join2/ContentPlan3
chain must invalidate on actual device loss and retain authentic foreign
callback userdata until native drain. The preceding final browser group is2/3:
completion182.05 and submitted-loss181.18 seconds pass; map-loss182.35 fails.
Observation-only retry183.95 confirms pending0/resultAlive1/completed0/
invalidated1/published0. Native unmap legitimately synchronously cancels the
C map future; a held JavaScript map fulfillment alone is insufficient evidence
that a pending record must remain. Failures are archived separately.

The repaired fixture reuses the existing native scope handoff at actual map
invocation to hold all three map-scope promises. Original loss invalidates the
root while outstanding scope userdata retains its owners; release two scopes,
yield to native delivery, retain the root until the final scope drains, then
require unpublished output, completion false and exact quota release. Native
unmap remains free to drain the real C future. This introduces no production
executor, pool, shader, quota or deadline change. Null retains the existing
map-drain validator; Vulkan/Metal refuse missing typed payload; WebGPU remains
explicit private Prototype.

Repair validation now passes: rebuilt assertion-enabled MinGW/Null6/6 /29.66
seconds, source292/292 and package/root8 checks. Selected real Chrome map-loss1/1
passes182.84 seconds with pending1/resultAlive1/completed0/invalidated1/published0,
encoder1/copy1/submit1/map1/unmap1/read0. Two scopes release while the final foreign
scope retains the same root; authentic final drain releases all original quotas.
The same Wasm binary's completion/submitted-loss regression passes2/2 /375.73
seconds (189.40/186.32). All three lifecycle targets pass across these runs;
no single3/3 CTest invocation is claimed. Sixty-nine archived SHA256 hashes verify
under typed-readback-loss1-validated/, including27 exact checkpoint source files
and binary identities captured before the next geometry edits. Hot release0 and
Asyncify17776/32768 bytes are preserved. Both failed map checkpoints remain
separate evidence. This is scoped private lifecycle Backend Validated only;
public Emulated, Metal Graphics and renderer/WOCC P4-P6 remain incomplete.
# Original sampler custody checkpoint (2026-10-10)

GeneralSamplerDescriptorReference/1 continuation passes final rebuilt
assertion-enabled Host/Null/Vulkan synthetic/Metal stub4/4, source324/324,
package/root8/8, Wasm artifact2/2, actual Release Chrome original8 projection1/1
and affected browser regression5/5 plus independent ordinary numeric geometry.
Source2 now retains the exact registered
sampler dependency, checks complete physical/comparison/stage/nonmemory fields,
and rejects sampler tables and finite-buffer footprints. GeneralResources1 emits
and budgets an explicit sampler reference. Stream includes it; State and Usage5/
Dependency5 have no fake sampler buffer state or memory contribution. The same
general graph now has eight original sources, four logical references and three
memory contributions. One-less Stream/logical/owner budgets reject, State steps
remain6, state owners44 fit authored48 and hot replay is allocation/query-free.
The initial browser-only Host helper error is repaired with a platform guard;
failed build evidence is preserved. Native write/closure/loss
join/completion and public Emulated are still closed; no P3-P6 completion follows.

OriginalSamplerCustody/1 extends existing registration/leases/Source inventory with
independent bounded sampler opt-in and full physical layout identity. Four backend
CPU factory contracts are explicit and execution-free; native loss-source joins
and sampler descriptor/native closure/write/completion still require implementation.
Rebuilt Host/Null/Vulkan synthetic/Metal stub4/4, source323/323, package/root8/8,
Wasm artifact2/2, genuine Chrome sampler/general projection1/1 and affected browser
regression5/5 plus independent ordinary numeric geometry pass. This grants Contract
Complete / Prototype Complete for CPU sampler custody only; no public Emulated or
P3-P6 completion. See original-sampler-custody-contract.md for exact evidence.
