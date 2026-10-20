# Original BindlessRoot Source /1 and /2

## Current concentrated implementation batch: snapshot faults and scene passes

SnapshotScope/1 retains each original snapshot and its own generation. Native
Vulkan/Metal and WebGPU fault clearing now resolve the Root named by the actual
command reference. WebGPU fault copies use each authenticated readback binding's
original source/destination leases; they retain the exact four-byte, zero-offset
fault range. They no longer require the primary snapshot's latch. The original
inventory, state journal, fault leases and completion remain authoritative.

SceneContent/1 now requires authentic initialized geometry and shader closure
budgets at both public execution fronts. Native direct graphics uses Closure3;
WebGPU direct graphics uses Closure4/5/6 with explicit initialized geometry and
ProducedAttachment/1 custody. Null and WebGL reject execution. Empty scene inputs
preserve the historical attachment scope. These checks do not publish a new
capability or make the closed produced execution profiles executable.

WebGPU reuses the existing full pass checker and native emitter for explicitly
admitted scene HDR color/D32 depth passes, including exact attachment references,
subresources, pipeline definitions and currently selected Root. Cold admission
authenticates initialized bytes once; Pending checks the retained Root identity
and existing seals without observing those buffers as publicly committed content.
This batch still needs actual platform acceptance before any readiness claim.

## Selected stage projection and scene raster preparation (implementation batch)

EntryAccess/2 and /3 keep the compiler-pinned VisibilityConsumer/2 Graphics and
Compute scopes. EntryAccess/4 projects authenticated SceneContent/1 Graphics
pairs, including ordinary buffer/image declarations. Every issued stage must
match exactly one immutable package WGSL module by byte count, SHA-256 and
stage; selected module names determine the scene pair. Native entry-point names
are not assumed to equal authored source names. A binding is physically active
if any selected stage consumes it, but every other stage must independently
prove explicit target inactivity. Missing, duplicate or conflicting module and
binding evidence rejects admission with no partial activity result.

WebGPU emits only these selected physical bindings. A wholly inactive ordinary
set has no native mapping, bind group, layout or recording call. The same Root
still retains every original descriptor owner, publication, registered resource
and generation, and charges their full custody. Revalidation checks both the
complete Source and exact selected physical subset. Native usage-scope alias
validation remains mandatory within and between every emitted group.

SceneRaster/1 prepares the scene's explicit triangle, single-sample RGBA8/HDR
color and D32 color/depth/shadow state through existing RHI pipeline semantics.
Blend factors, color write masks, culling, front face, depth test/write/compare,
bias, viewport/scissor and sample coverage remain authored state. WebGPU rejects
fractional or out-of-range integer depth bias rather than rounding it. The
shared native emitter preserves bindless package identity, selected active
layout, shader byte issuance and existing compact vertex-stream mapping.

| Backend | Entry projection | SceneRaster/1 preparation | Execution grant |
| --- | --- | --- | --- |
| Null | Validator over authentic package/Root owners | Validator | None |
| Vulkan | Selected original reflection | Existing explicit native raster state | None |
| Metal | Selected original target reflection | Existing explicit native raster state | None |
| WebGPU | Verified WGSL stage union and physical subset | Shared explicit native emitter | None |
| WebGL | Diagnostic failure | Diagnostic failure | None |

These are implementation and CPU contract claims. Produced execution gates stay
closed until native accepted workloads and their complete lifecycle fixtures
are ready; final three-platform execution/capture remains after source freeze.

## Public immutable buffer initialization (implementation batch)

`BindlessCrossExtension::prepareOriginalInitializedBuffer` consumes the typed
`OriginalInitializedBufferRequest` and returns the existing backend-issued
`OriginalInitializedGeometrySource`. HostGeometry/1 selects immutable index or
vertex content; Shader/2 selects immutable uniform or read-only storage content.
The request supplies complete backing bytes, the original declaration and four
independent metadata/payload/resource/lease quotas. No initialized flag,
alternate resource identity, journal or completion issuer is introduced.

`queryOriginalInitializedBufferAllocation` obtains the issuing device's actual
dedicated buffer requirement. The factory checks this requirement against the
authored retention limit before backend creation, then authenticates the actor,
initialized size and exact original registered lease. Failed admission publishes
no actor. Existing backend actors freeze the caller bytes and seal subsequent
host and GPU writes. Initialization is host work; native execution and Pending
reads require their separate existing lifecycle/content contracts.

| Backend | Initialization mapping |
| --- | --- |
| Null | Declaration validation; refuses native initialization. |
| Vulkan | Existing original mapped actor, real allocation query and flush. |
| Metal | Existing original shared/managed actor and write publication. |
| WebGPU | Existing original mapped-at-creation actor, write and unmap. |
| WebGL | Deterministic unsupported diagnostic. |

Geometry and immutable shader platform fixtures use this public factory while
retaining backend actor checks only inside the tests. This implementation batch
does not constitute new three-platform execution evidence. Current batch status
is maintained in WOCC's `docs/bindless-engine-closure-batch.md`; earlier sections
below retain their original historical validation scopes.

## Current: public static member admission and temporal skinning256 execution

The original typed static member query now joins selected pipeline/module facts
to the same canonical buffer/range/stage/live generation. Requested Native
member proof is mandatory in original pre-record admission. Its contract and
refusal boundaries are in `bindless-graphics-member-memory-contract.md`.

The same public Graphics Root now executes renderer temporal palette32864 plus
influence32 arithmetic on Vulkan, Metal and WebGPU, with32 exact lanes. Actual
Chrome passes22 lifecycle flows/4 numeric and22 member/range receipts; the old
320+16 regression also passes22/4. Windows5/5, Mac7/7, Wasm/Node3/3, source418/418,
style6/6 and formatter6 pass. Original snapshot, authorization, inventory,
journals, real completion and retirement are retained. No executor/identity/
execution capability is added. Evidence:
`build/bindless-production-skinning256-web1/current-report.json`.

Production material/instance operations, complete normal/tangent/pass semantics,
required sampling/terrain/formats/atomic/indirect/budgets/platform lifecycle and
the common production gate remain open. WOCC remains Traditional. Historical
next actions below describe their own epochs only.

## Historical: original Native source custody and selected pipeline member memory

PackageMemoryLoadRequest/2 now freezes the original parsed SPIR-V graph and
explicit MSL relation rules inside the existing immutable package modules.
The normal retained/authorized Metal Graphics factory compiles the original
selected module bytes and compares the actual driver member graph at each
direct physical stage/slot before pipeline publication. No separate identity,
store or executor is introduced. Failed target budgets or relations clear all
stages and refuse publication; caller intent mutation has no effect.

Windows5/5, actual Mac6/6, Wasm/Node1/1, source418/418, style6/6 and formatter
pass. Public Graphics clean/fault preserves32 exact lanes and real completion/
retirement; push224 regression passes. Fresh submitted gputrace and independently
verified8619-input/source/artifact hashes are retained. No independent Metal
replay is claimed. Evidence:
`build/bindless-native-pipeline-memory1/current-report.json`.

Next: a typed public Root member-memory query must join these existing pipeline
facts with the canonical original buffer/range/generation. The WGSL-specific
query and range-only DriverQuery retain their boundaries. Production indexed
arithmetic and the common gate remain open; WOCC stays Traditional.

## Historical: Native member-memory declaration proof

SPIR-V/MSL memory relation/1 now reuses the bounded typed declaration DAG and
pair visitor. Independent actual Metal selected-function reflection retains
nested member offsets and fixed-array count/stride, with explicit column-memory
matrix pairs. The real WOCC shadow16432, temporal skinning32864 and previous
camera128-byte declarations match; missing rules, same-size wrong scalar type
and wrong array count refuse. Actual collector exact/less budgets and exhausted
core quota verify no partial publication. Windows3/3, Mac2/2, Wasm/Node,
source418/418, style6/6 and formatter pass. Source8615 hashes match the Mac epoch.

This is declaration evidence only. The original artifact/selected-pipeline/
physical-binding/Root join and production indexed arithmetic remain open;
Native public target-memory inspection keeps its explicit refusal. No new
execution capability or common engine gate is granted. See
`docs/spirv-msl-buffer-memory-contract.md` and
`build/bindless-native-member-memory1/current-report.json`.

## Historical: multiple frozen push updates and integrated Mac CMake

The existing Graphics2/Upload1 contract now has actual WebGPU two-update proof.
Each draw consumes its own original full224-byte block after caller bytes are
poisoned. Two scissor columns distinguish the first exact float result from the
second unsaturated color. The complete queried binding span is budgeted: this
device reports256-byte offset alignment, so offset256 plus224 requires480
resident bytes. No alignment constant is inserted into production code or the
test's capacity calculation. Eight invalid upload intents per workflow preserve
ledger, quotas, transaction and unpublished result before recording seal.

Chrome passes44 workflows (single and double updates,11 scenarios x clean/fault)
and8 numerical receipts; the double-update output checks exact float lanes and
UNORM color within half LSB. Ordinary Graphics regression passes22 workflows and
4 receipts/32 exact lanes. Windows3/3, Wasm/Node1/1, source418/418, style6/6 and
formatter8 pass. Cold tests verify different frozen blocks and exact/less update,
byte and Root access budgets. Initial one-draw fixture ceilings refused correctly;
both failures are retained, with no production refusal weakened.
Evidence: `build/bindless-push-multi-update1/current-report.json`.

Mac now configures/builds/runs the exact unmodified integrated root CMake in a
separate8611-input frozen checkout. Package/device and real public Graphics
matrix/push tests pass4/4, with fresh submitted clean/fault gputrace, numerical
readback, recorded cancellation and retirement. Generated shader/package and
executable hashes are retained. This removes the retained-CMake discrepancy for
these targets. It does not prove Native multi-update GPU execution, native
member-memory equivalence, production workload coverage or the common engine
gate. No independent Metal replay is claimed.
Evidence: `build/bindless-mac-integrated-cmake2/current-report.json`.

Next: production material/instance/indexed-skinning ABI and Native target-memory
proof, followed by the required sampling/terrain/formats/atomic/indirect/budget
and full platform lifecycle gates. WOCC remains Traditional. Earlier sections
below retain their own input epochs and must not override these scoped results.

## Historical: WebGPU public Graphics single push execution /2

The explicit original Graphics pipeline overload now consumes the authenticated
selected push interface and independent Upload1 intent. The default pipeline and
version-only operation paths keep their refusal. Shader-module creation receives
the same explicit intent. Physical descriptor groups must be distinct from the
selected push group, including a vertex-only push declaration.

The original native preparation preflights the entire frozen command stream
before claiming its recording seal. Shared consumption/upload sizing uses actual
queried uniform size/alignment and device buffer limits. The existing inline
native program owns a ledger-backed uniform buffer, acquired group layout and
bind group (three additional native objects, two retained owners). A borrowed,
synchronous mapped-at-creation initializer copies the original frozen slices and
zeros padding. Recording only binds the frozen dynamic offset; it performs no
queue upload. Original callback custody retains the buffer and allocator charge
through genuine completion/draining; no second byte store, journal or issuer is
introduced. Full original-plus-added footprint validation remains mandatory.

Evidence: `build/bindless-webgpu-push-execution1/current-report.json`. Actual
Chrome push passes22 lifecycle cases and4 exact numeric receipts, with fault0/1,
frozen224 bytes, VS/FS arithmetic, color2x2, hot0 and zero-residual retirement.
Seven failed upload requests leave quotas, ledger, journal and output unchanged.
Ordinary Graphics regression also passes22 cases/4 receipts/32 exact float lanes.
Windows4/4, Mac cold1/1, Wasm/Node1/1, source418/418 and style6/6 pass.
Current-CMake selective builds report no work to do; the unrelated 2D addition
is preserved. Mac retains its validation CMake and its cold epoch precedes only
the browser HTML completion-trigger correction among1236 recorded inputs.

Status: Backend/Platform Validated for this authored single224-byte update.
Multiple GPU updates/nonzero aligned offsets, production ABI/sampling/terrain,
full budgets/platform lifecycle and the common integrated gate remain open.
WOCC stays Traditional. The following checkpoints are historical input epochs;
their former WebGPU refusal does not override this current scoped execution.

## Historical: Public Root push upload intent /1

`OriginalBindlessRootCallbackColdBudget` and the original immutable
`OriginalBindlessRootProgramRequest` now carry the same typed
`OriginalCommandPushUploadBudget/1`. Nonempty intent requires its own version and
opt-in and is restricted to Graphics/2. All seven fields must match across the
public execution join and backend cold-command inspection. Update capacity cannot
exceed command capacity. Shell-only/1 detects upload fields when checking that its
program is empty. Existing version-only and empty-request operation checks still
reject `PushConstants`; only the explicit request-aware CPU operation contract
accepts it. This does not publish any additional execution capability.

`OriginalBindlessRootPushUploadContract::validateFootprint` projects actual
additional native objects and retained owners onto the complete original command
footprint. It checks the independent upload ceilings, uint32 overflow, source
native/graph capacity, command owner ceiling and recording/submission/callback
owner ceilings. Failure clears the output. The helper allocates nothing; it does
not reserve native storage or prove actual custody. Callers must supply the full
original shell/encoder/callback footprint rather than resource-only counts.

WebGPU currently refuses nonempty upload intent with
`RHI-WEBGPU-ROOT-PUSH-UPLOAD-UNAVAILABLE` before shell preparation, seal claims or
callback publication. The backend-specific refusal assertion belongs to its
device contract test; the portable package test checks the shared public schema
and exact/less count projection without linking an unavailable native backend.

Evidence: `build/bindless-root-push-upload-admission1/current-report.json`.
Windows4/4 including Null smoke and the WebGPU device contract, actual Mac cold1/1
(1235 matching inputs), Wasm/Node plus actual Chrome ordinary Graphics2/2 pass.
Browser22 lifecycle flows and4 numeric receipts, source417/417, style6/6 and
pinned formatter7 files pass. The first Mac backend-specific test link failure is
retained; the final portable test does not require WebGPU backend linkage.

Next: consume the original frozen whole stream using actual queried uniform
limits; reserve the real ring through the device allocator, apply the complete
counts and retain native resources/allocation charges through original callback
draining. Real public WebGPU push execution remains unproven; the common engine
gate remains open and WOCC stays Traditional.

## Original push upload preflight /1 (CPU contract validated)

`OriginalCommandPushUploadBudget` separately bounds updates, logical payload
bytes, resident storage bytes, additional native payload objects and retained
owners. Every field requires explicit version1/opt-in; absent or partial requests
fail. `OriginalCommandPushUploadPlan` stores only counts/ranges, never bytes,
handles, a replacement graph or completion authority. Selected-stage and original
frozen-byte validation remains in `OriginalCommandPushConsumption`.

Shared sizing consumes known device/allocation bounds, checked alignment and the
uint32 dynamic-offset ABI. It checks the entire bound uniform span after every
aligned offset, including the last update. Two224-byte uploads with256-byte
bindings require512 resident bytes even though the writes end at480. Exact/less
capacity, invalid/missing limits, overflow, budgets and failure-preserving counters
are tested without allocation. Existing WebGPU native recording reuses this guard
before queue writes or binding. Vulkan/Metal/Null receive explicit CPU projection
decisions; this sizing profile grants no new execution on any backend.

Evidence: `build/bindless-push-upload-plan1/current-report.json`. Windows3/3,
actual Mac cold1/1 (1235 matching recorded inputs), Wasm/Node and Chrome ordinary
Graphics2/2 pass; the browser regression retains22 lifecycle flows and4 numerical
receipts. Source417/417, style6/6 and pinned formatter7 files pass. The retained
Windows first failure was a stale fixture expecting native capabilities on an
unavailable runtime; the corrected fixture checks actual runtime availability and
retains its subsequent metadata/allocator/lifecycle checks.

That sizing checkpoint did not connect the request to public Root admission. The
new public intent join is described above; actual ring allocation in the device
ledger, native custody and callback draining remain open. Existing empty Graphics
requests retain their push refusal. The common production engine gate remains
open and WOCC remains Traditional.

## Frozen push consumption /1 (CPU contract validated)

`OriginalCommandPushConsumptionBudget/1` is an independent explicit CPU intent
inside the existing RootState budget. Its update and total-byte limits are
additional to the original Source/CommandVariableArguments captured-byte quota.
Empty intent leaves push consumption closed. Budget equality and emptiness are
part of the existing graph/stream/transaction correspondence.

The original Root access walker reads only the original immutable argument store
and the currently bound original prepared pipeline. It uses the shared
AtomicFullBlockPush command/layout/reflection validators, with allocation-free
stable diagnostics, exact selected-stage visibility, offset zero and one full
block. Caller byte/descriptor changes cannot affect frozen input. Duplicate
unconsumed updates refuse atomically. Rebinding or leaving a pass clears readiness;
unchanged bytes may be reused by later draws under the same binding after actual
shader consumption. The walk adds no GPU resource access for CPU push bytes and
creates no owner, stream, state journal, submission pool or completion issuer.

Four-backend lowering at this increment is CPU projection only: Null validates;
Vulkan/Metal/WebGPU share the same original walk; WebGL refuses. Native recording
remains gated. Vulkan must subsequently consume the same bytes with its existing
push helper and queried limit; Metal must use reflected per-stage slots and its
existing constant-byte helper; WebGPU must reserve the proved uniform binding and
queried/aligned bounded upload ring, retained until genuine completion. No fixed
native slot, synthetic callback, layout inference or CPU matrix transpose is allowed.

The selected-stage cold checkpoint below remains sealed. Frozen-byte helper and
shared-validator regression pass Windows6/6, actual Mac cold1/1 with1229 matching
inputs, Wasm/Node1/1, existing Vulkan ordinary Graphics GPU regression1/1,
source417/417, style6/6 and pinned formatting. Evidence:
`D:/arp/build/bindless-push-consumption1/current-report.json`.
Positive original push Root GPU execution and native budgets/completion/retirement
remain unproven.

## Original Graphics push recording /1 (Vulkan and Metal scoped validation)

An explicit bounded `BindlessGraphicsPushRequest/1` overload on the existing
extension creates a selected-stage Graphics pipeline. The old overload keeps its
push refusal. The new overload authenticates the original authorization/package,
uses the shared selected-stage ABI composition and retains the same original
pipeline/layout owners. It must check the independently queried capability and
the actual selected full-block size before creating native objects.

Original command consumption has a separate opt-in recording intent. Empty
recording intent remains CPU-only. Recording consumes the same frozen bytes,
exact selected visibility and update/byte budget; preparation validates the whole
stream before native recording and existing failed-recording custody prevents
publication. No additional bytes store, journal or completion issuer is allowed.
The original PushConstants usage references the exact active BindlessRoot;
ordinary Upload references retain their existing signature. Empty references or
a foreign Root refuse. Root identity is independent of frozen byte ownership.

Four-backend mapping: Null validates the selected pipeline and CPU consumption,
never executes. Vulkan uses queried maxPushConstantsSize and the existing
vkCmdPushConstants helper. Metal requires reflected per-stage constant-byte
slots; WebGPU requires the proved uniform group and aligned bounded upload ring
retained through real completion. Vulkan and Metal implementations are connected;
WebGPU's new pipeline and recording entries still explicitly refuse. WebGL refuses.
No scene mode, production readiness or common engine gate is granted.

The Vulkan checkpoint passes Windows8/8, Mac cold1/1 with1231 matching inputs,
Wasm/Node1/1, source417/417 and style6/6. Actual Vulkan push224 clean/fault and
independent RenderDoc replay prove frozen bytes, VS/FS arithmetic, completion
and retirement. Evidence: `build/bindless-push-recording1/current-report.json`.

Metal recording/1 maps the same full block to the existing encoder setBytes
helper and authenticated function-specific buffer slot. Its native size ceiling
comes from the existing versioned Metal AtomicFullBlockPush contract (4096-byte
specification limit), still bounded by the256-byte L2 ceiling and caller request.
Capability additionally requires the actual Graphics runtime and executable
native push lowering; non-Apple stubs publish no capability. Selected stages
must retain identical composed slot/ABI and must not overlap direct buffers,
argument buffers or vertex streams. No upload resource or extra owner is created:
setBytes copies into the existing native command lifetime. Whole-stream cold
preflight and original frozen CPU consumption run before any recording. WebGPU
remains diagnostic failure pending its independent bounded uniform-ring mapping.
Metal Graphics PipelineInterface/1 also maps an explicitly authored nonzero
primary set without requiring an unrelated ordinary descriptor domain. Earlier
Compute and absent/unknown interface versions retain their refusal; native
reflection, physical slot collision and full device budgets still apply.

Metal checkpoint evidence: `build/bindless-metal-push-recording1/current-report.json`.
Windows8/8, actual Mac registered3/3 plus ordinary Graphics regression and
submitted gputrace, Wasm/Node cold1/1, source417/417, style6/6 and formatter pass;
all1233 recorded Mac inputs match. Actual push224 clean/fault verifies four
numeric lanes and real completion/hot0/retirement. Both recorded-before-submit
cancellations verify unpublished output and quota0. The host's Apple capture
layer asserts when deallocating a texture referenced by a cancelled unsubmitted
native command, also reproduced without the engine; the trace therefore closes
after both submitted GPU cases and both cancellations still run with API
Validation enabled afterwards. Failed attempts, crash and minimal native probe
are retained. This is not independent Metal replay or production gate evidence.

## Selected Graphics push interface /1 (cold composition validated)

Cold `BindlessGraphicsPipelineEntries::compose/1` consumes one original shared
package and exact selected Vertex/optional Fragment names. It reuses the
ordinary Graphics interface merger with the existing authorized-bindless
validator. Both selected push ABIs must agree in full member order, size,
matrix/array facts, MSL slot and WGSL binding; visibility is their stage union.
Only the original package's factory-owned module addresses are returned. Source
identity is authenticated before canonicalizing entry paths. An Emulated push
entry requires its original proved uniform correspondence; Native source facts
do not pretend to prove MSL member layout. Layout gains exactly one offset-zero
full-block range with that selected visibility. Failure clears every output.

This is a shared L2 metadata composition step on Null/Vulkan/Metal/WebGPU,
with WebGL remaining unsupported. Existing four-backend executable composition
continues to reject push packages until the original command consumption,
native lowering, queried budgets and real completion/retirement are validated.
There is no new native executor, resource store, ownership issuer or journal.

Scoped validation: Windows affected regression4/4 and Null hidden-frame1/1,
actual Mac cold contract1/1 (1227 input hashes matched), Wasm/Node1/1,
source417/417, style6/6 and pinned formatter pass. Evidence:
`D:/arp/build/bindless-push-selected-interface1/current-report.json`.
No push command recording, GPU arithmetic or common engine gate is granted.

## Explicit push declaration custody /1 (cold declaration validated)

`BindlessPackagePushLoadRequest/1` is a cold, explicit source-declaration request
on the original immutable package. It supplies a bounded source type graph and
an optional `PushBinaryMemoryPlan/1` for every push entry, plus a full-block byte
ceiling. The authenticated publication, lowering metadata, per-entry reflection
and actual SPIR-V push declaration must agree on size and stage coverage. Rules
are consumed during loading; no caller pointer is retained. Missing, duplicate,
unused or invalid requests fail the whole load. Legacy loads retain ENTRY-ABI
refusal. Querying facts requires the original shared owner and clears outputs on
failure. An optional Emulated target request reuses the original
`TargetBufferMemoryDeclarations` processor over all actual buffers and push.
Only successful `lookupPush` facts are copied into that same immutable module;
source-only owners keep target refusal. Native target requests fail explicitly.
This grants declaration correspondence only, never shader arithmetic.

Four-backend lowering is deliberately limited to declaration custody: Null may
validate either publication; Vulkan and Metal may inspect a Native publication;
WebGPU may inspect an Emulated publication's source declaration and explicitly
proved WGSL push uniform declaration. All public
pipeline composition paths still reject these packages with
`RHI-BINDLESS-PIPELINE-PUSH-EXECUTION-UNAVAILABLE` before changing caller outputs.
WebGL remains unsupported. Native slot residency, WGSL uniform equivalence,
selected-stage merging, frozen command consumption and completion/retirement
must be proven before that shared execution gate can be opened.

Scoped checks pass on Windows (affected3/3, shader1/1 and Null hidden-frame1/1),
actual Mac cold contract1/1 and Wasm/Node1/1. The current registered fixture's
MSL4/4 compiles on Mac and its Chrome Graphics pipeline compiles; full compiler
regression48/48 passes with the rebuilt verifier. Source417/417, style6/6 and
the pinned formatter pass. Evidence:
`D:/arp/build/bindless-push-custody1/current-report.json`.
This checkpoint does not prove GPU push arithmetic or production readiness.

## Push publication prerequisite (compiler correspondence validated)

The next compiler increment reuses AtomicFullBlockPush/1: one nonempty complete
block, offset zero, explicit size and stage visibility. Optional bindless
`pushConstants` compiler intent names contractVersion1, sizeBytes, stages and an
explicit WebGPU group/binding. It requires pipelineInterfaceContractVersion1;
absence keeps the original push refusal. Source size, actual SPIR-V occupied
extent and generated per-entry reflection must agree. Generated MSL owns its
buffer slot; WGSL uses the declared reserved uniform location. A collision or
missing target declaration fails publication. No filename-based slot selection,
partial updates, CPU transposes or implicit budget reservations are allowed.

Four-backend status at this prerequisite: legacy package loads retain push
refusal; explicit declaration custody is a separate increment above. Vulkan
SPIR-V uses the native PushConstant block;
Metal emits a reflected constant-buffer slot; WebGPU emits an explicit uniform
binding. Those last three are compiler correspondence only. Existing runtime
package/Root gates continue rejecting push until authenticated package custody,
selected-stage ABI merging, frozen command bytes, budgeted native lowering and
original completion/retirement are implemented and validated. No execution
capability or common production gate is granted by compilation. Full compiler
regression48/48, SPIRV-val4/4, actual Mac MSL4/4 and Chrome Graphics/Compute
pipeline compilation pass. Evidence:
`D:/arp/build/bindless-push-publication1/current-report.json`.

## Graphics buffer range admission checkpoint (2026-10-17)

Original Graphics state-access projection, public execution preparation and
Vulkan/Metal/WebGPU native closure preparation/revalidation now require a range
proof for every active UniformBuffer, ReadOnlyStorageBuffer and StorageBuffer in
each authenticated selected entry. Missing proof cannot suppress a required
check. Cold Root ownership inspection remains independent. An undersized bind
refuses before implicit access publication or native closure creation.

OrdinaryGraphicsBufferRange/1 supports static declarations. Query/2 additionally
accepts a runtime storage tail with authenticated offset, nonzero stride and
occupied element extent. At least one complete stride must fit the original
descriptor range and access footprint. Capacity comes from that exact range;
proved target strides require equal source/target element counts. Vulkan uses
authenticated selected SPIR-V declaration facts; WebGPU/Null Emulated use proved
WGSL target facts; Metal uses actual selected pipeline-reflection minimum and
alignment, explicitly leaving target runtime offset/stride unknown. No Metal
member-layout equivalence is inferred. WebGL refuses deterministically.

Admission/1 and query/2 publish no new execution authority, owners, registry or
journal. Failure clears query outputs; hot queries allocate nothing. Tests cover
319-byte refusal for320 bytes, actual state-consumer refusal before any emitted
access, forged/stale owners, all selected ordinary buffers, unknown versions
and four-element runtime capacity through existing composed Graphics execution.

Scoped Backend/Platform Validated: Windows4/4 plus legacy Graphics10/10,
browser/Node3/3 (22 runtime-capacity and22 matrix admission/lifecycle receipts),
actual Mac package/Null contract, registered device1/1, GPU clean/fault and
gputrace. Fresh independent Vulkan replay verifies32 exact lanes, uploaded
320+16 bytes, color, fault0/1 and zero validation messages. All1201 recorded Mac
input hashes match; source417/417, style6/6 and pinned formatter pass. Evidence:
`D:/arp/build/bindless-graphics-range-admission/current-report.json`.
The static-only report remains a separate input epoch at
`D:/arp/build/bindless-graphics-buffer-range/current-report.json`.
Production material/instance/indexed skinning/push, required sampling/budgets,
full lifecycle and the common integrated production engine gate stay open.

## Ordinary Graphics buffer range /1

The next admission increment consumes this query at the existing prepared
Graphics state-access projection, public Graphics execution preparation and
Vulkan/Metal/WebGPU native closure preparation/revalidation. It walks every
active ordinary buffer in each authenticated selected entry, so missing proof
cannot suppress a required check. Cold Root ownership inspection remains
independent. An undersized Graphics bind refuses before emitting any implicit
access, creating a native closure or recording commands. This wiring is under
validation; no production readiness is claimed.

Range query /2 additionally accepts a runtime storage tail with an authenticated
offset, nonzero stride and occupied element extent. At least one complete source
stride must fit the bound range and the original access footprint. Source
capacity comes from that exact range. When the target stride is proved (SPIR-V
or WGSL), source and target element counts must agree; failure publishes no
output. Metal retains actual driver minimum/alignment and explicitly leaves
target runtime layout unknown. This is not a Metal member-layout equivalence
claim. Query /1 continues to refuse runtime arrays. Admission walks UniformBuffer,
ReadOnlyStorageBuffer and StorageBuffer declarations of the selected entries.
The existing composed Graphics runtime buffers are part of the regression,
including four-element runtime capacity, old-version refusal and hot0 queries.

This range-only query joins the original selected Graphics entry, authenticated
SPIR-V declaration and original ordinary descriptor buffer lease/range. Vulkan
uses binary occupied bytes with unknown alignment; Metal uses the actual selected
MTLRenderPipelineReflection buffer minimum/alignment; WebGPU uses the existing
proved WGSL target declaration; Null validates metadata and WebGL refuses.
Runtime arrays require a separate element/capacity contract and explicitly refuse.
It grants no member-layout equivalence, shader arithmetic or execution authority.
Original buffer access footprints and stage ownership must contain the required
range. Failure clears outputs; queries allocate nothing.

Windows4/4, browser/Node3/3, actual Mac contract/device/GPU/gputrace and fresh
independent Vulkan replay pass. All1201 recorded Mac inputs match this epoch;
source417/417, style6/6 and pinned formatter pass. Evidence:
D:/arp/build/bindless-graphics-buffer-range/current-report.json.
Mandatory production range admission is the next increment.

## Graphics selected-entry identity /1 and ordinary memory inspection /2

The original authorized Graphics factory captures the exact selected Vertex and
optional Fragment package modules from the descriptor used to create its native
payload. The pipeline stores two borrowed module pointers, retained by its
existing package authorization. No additional owner, registry, executor or
allocation is introduced. Ordinary pipelines and unknown inspection versions
return null. Null retains validator metadata; Vulkan and Metal retain the native
selection; WebGPU retains the equivalent scalar payload selection; WebGL refuses.
These facts grant no execution, state, completion or content authority.

The L2 selector checks the current original package authorization, selected stage
mask and module membership before dereferencing a returned pointer. A module from
another package, missing module, wrong stage and unselected same-layout entry
must refuse. Union reflection or equal layout cannot authenticate an entry.

Root ordinary-memory inspection /1 remains Compute-only; /2 also accepts Graphics
with this authenticated selection. It joins existing frozen target-memory facts
to the original descriptor range, buffer lease, generation and canonical source
index. The returned shader stage is entry-specific; pipelineStages keeps the
original descriptor declaration's state scope. Unknown versions, stale or forged
Root ownership, missing target-memory proof and undersized ranges clear all
outputs and refuse. Queries must allocate nothing and publish only after all
checks. Native packages currently lack this target-memory proof and explicitly
refuse memory inspection; authenticated entry identity is still available.

This increment passes Windows registered4/4, Wasm browser/Node3/3, actual Mac
package/Null contract, device contract1/1, native clean/fault GPU and gputrace.
Chrome records22 Root inspection/lifecycle receipts and four exact numeric
receipts. Independent Vulkan RenderDoc replay checks32 exact float lanes,
ordinary uploads, color and fault0/1 with zero validation messages. All1200
recorded Mac inputs match this epoch; its retained CMake differs from Windows.
Source417/417, style6/6 and pinned formatter19 files pass. Evidence:
D:/arp/build/bindless-graphics-root-memory/current-report.json. This is scoped
Backend/Platform Validated, not the common integrated production gate. Native
target-memory proofs, production material/instance/indexed skinning/push and the
remaining workload operations stay open. Earlier checkpoints retain their own epochs.

## Ordinary Graphics matrix execution checkpoint (2026-10-16)

Fragment storage-buffer Store/1 has an explicit operation mapping: Null is
validator-only; Vulkan and Metal map the store natively; WebGPU maps selected
bounded scalar bindings equivalently; WebGL refuses. Other descriptor types,
Vertex stage and unknown versions refuse this mapping. This never grants an
execution ticket. Metal publishes the Store operation only with the existing
Graphics opt-in, queried argument-buffer/storage limits and real original
Graphics result/completion runtime. Without that runtime the earlier
Load/Query/Atomic capability mask remains unchanged. Snapshot table bounds,
fault writes, residency and the original content/state journal remain required.

The existing ordinary descriptor-domain contract permits an exact shared
320-byte current/previous/palette buffer and a separate 16-byte revision buffer
to be consumed by Vertex and Fragment. Package loading must compare the shared
SPIR-V memory representation across all selected entries. Emulated additionally
requires an explicit per-entry TargetMemoryRequest2/Plan2 matrix relation; a
missing entry proof must refuse publication. This probe grants no new capability.

The authored GraphicsMatrixOrdinary workload uses default Slang float4x4 and
the normal row-memory compiler configuration. Its Vertex entry computes weighted
palette0/2 skinning and current/previous transforms. Fragment independently
computes composed transforms and palette1, storing all four Vertex and Fragment
lanes for each of four pixels through the original bindless Results buffer.
The CPU oracle reuses core::multiply and includes nonsymmetric projective w.
Clean/fault runs must yield the same 32 numeric lanes; invalid resource/element
writes must latch fault without corrupting them. Preparation, cancellation,
recording, submit, true completion and retirement reuse existing public Roots,
canonical inventory and state/content journals.

Null validates declarations only. Vulkan/Metal may execute only through their
existing authorized Graphics payloads; WebGPU requires its original Emulated
payload and independently proved target memory. Cross-target compilation or a
Node declaration test alone cannot establish GPU execution on those backends.
Ordinary-memory Root inspection/1 remains Compute-only. Production material and
instance indexing, push operations and the common engine gate remain open.

Actual public Vulkan and Metal clean/fault execution now return all32 exact
float lanes. Independent Vulkan RenderDoc replay checks both shader hashes,
the actual320+16 uploaded bytes, color, fault0/1 and zero validation messages.
Actual Mac package/device/GPU/capture checks pass; all1197 recorded inputs match
the current local files. The Mac build definition is retained separately, so
this is not a common integrated-source production acceptance claim.

Chrome Release also passes22/22 original lifecycle flows and four exact numeric
receipts. The shared fixture now accepts an explicit Graphics data policy;
native command/callback/ownership machinery is unchanged. Its existing composed
Compute/Graphics regression passes. The old alias negative fixture now checks
the existing earlier prepared-binding refusal and verifies all four metadata
quotas return to zero after the candidate retires. An unexpected preparation
failure still asserts; no successful execution is manufactured.

Affected registered validation: Windows5/5; WebGPU browser/Node3/3; Mac device
contract1/1 plus direct package/GPU/capture checks; source417/417, style6/6,
SPIRV-val6/6 and pinned formatter15 files. Exact matrix equality is mandatory;
only RGBA8 UNORM color quantization permits one unit. Snapshot80-byte capacity
comes from actual Vulkan allocation queries and is checked before execution.
Evidence: D:/arp/build/bindless-graphics-matrix-execution/current-report.json.

Next is authenticated selected-entry identity for Graphics Root ordinary-memory
inspection. The selected Vertex/Fragment identities must come from the actual
factory-frozen pipeline, be related to the original authorized package and
retain their precise stage. Union reflection or equal layouts cannot select
between clean/fault entries. Missing, stale, forged or ambiguous correspondence
must refuse with cleared outputs, preserve the original owner/generation/ranges
and remain allocation-free. This grants no new execution or completion rights.

## Explicit push binary memory relation (2026-10-16)

PushBinaryMemoryPlan/1 names each reflected push member and its exact original
SPIR-V graph type. It explicitly permits a zero-offset, single-member fixed-array
value wrapper, and/or the Slang row-major float4x4 to SPIR-V ColMajor declaration
relation. Both flags must be consumed by that member. Wrapper extent, dimensions,
array/matrix strides, member size, scalar kind/width and reflection orientation
must match. Unknown versions, wrong identities, duplicate/unused rules and
exhausted member budgets fail before publication. No type-name heuristic,
automatic unwrapping, source rewrite or matrix arithmetic proof is introduced.

TargetBufferMemoryLoadRequest/2 may carry the optional binary plan; /1 refuses
it and an absent plan preserves existing refusals. The independent Plan2
SPIR-V/WGSL relation is still mandatory. GraphicsTargetMemoryLoadRequest/2 may
carry a separately authored combined binary plan; exact child/combined graph
and target-memory comparisons remain mandatory. /1 rejects combined plans.

Four-backend conclusion: Null can validate declarations only. Vulkan, Metal and
WebGPU may retain the same cold verified facts, but receive no shader operation,
pipeline, recording or completion rights. WebGL has no bindless execution grant.
Windows affected artifact/package regressions and main shader contract pass13/13;
actual Mac arm64 entry/module/original-matrix checks pass3/3 with1050 input hashes
matched, and Wasm Release/Node passes3/3. Source guards417/417, style6/6 and
clang-format21.1.8 checks for all nine changed C++ files pass. Normal compiler
SPIR-V/MSL/WGSL/reflection are sealed for both Graphics stages and the combined
module; SPIRV-val passes3/3. The actual WOCC shadow normal/instanced/skinned
SPIR-V entries each match the224-byte reflected push declaration. No source
rewrite or numerical matrix execution is claimed.

This increment also fixes the reflection cache discarding generated unquoted
hex decoration flags: YAML resolves these as integers, while the old cache
only read strings. Integer flags and legacy quoted hex are now preserved;
negative/overflow/bool/null/invalid/empty encodings fail. Both representations
and negative encodings have main shader contract coverage. Initial Mac attempts
omitted this implementation translation unit; the verified shader source closure
includes it and all1050 hashes match. Failed attempts remain archived.

This is Platform Validated cold push declaration/Graphics push aggregation.
Ordinary Graphics material/instance/skinning aggregation, matrix GPU operations,
the common engine gate and WOCC migration remain open. Evidence:
build/bindless-push-matrix-abi/current-report.json.

## Explicit ordinary package memory request (2026-10-16)

BindlessPackageMemoryLoadRequest/1 is a cold Emulated-package admission request.
Each unique authenticated entry names an exact ordinary-buffer binding cover,
using TargetBufferMemoryLoadRequest/1 or /2 with the existing explicit graph
relations and independent graph/pair/array/matrix budgets. Bindless metadata,
fault and table resources are excluded from this cover. Unmatched entries,
missing/extra/duplicate bindings, unused/invalid rules and push plans fail
without publishing a package. Native packages explicitly reject this WGSL
relation request; their source ABI validation remains unchanged.

Verified POD declaration facts are frozen inside the same original module and
package under its quota. Original-owner query clears output on failure and
does not allocate, read files or retain caller plans. This does not grant Root,
matrix arithmetic, backend execution, synchronization or completion rights on
Null, Vulkan, Metal or WebGPU. The prior Request1 no-proof matrix refusal remains.
The compiler fixture and package/Compute Root checks pass Windows17/17, shader1/1,
actual Mac3/3 (1029 recorded inputs matched), Wasm Release/Node2/2, source417/417
and style6/6. The Mac Root check uses Null CPU correspondence, not Metal matrix
GPU execution. Local transfer_lowering.h changed after the recorded Mac epoch;
this is not a same-integrated-source common gate. Evidence:
build/bindless-package-matrix-abi/current-report.json.

The next Root ordinary-memory inspection /1 joins those package facts to its
exact selected Compute pipeline, canonical inventory, original descriptor and
buffer lease. It resolves the existing whole/finite descriptor range, refuses
insufficient declared coverage and clears every output on failure. The Root and
its original quota retain all resources; the inspection creates no new owner or
execution ticket. Graphics inspection and actual matrix operations remain open.
Null provides validation only; Vulkan, Metal and WebGPU share this CPU inspection
contract without acquiring shader/command/completion rights. WebGL explicitly
refuses. This is Platform Validated declaration custody and Compute Root CPU
inspection only; Graphics and matrix GPU execution remain unvalidated.

## Historical: explicit matrix artifact request integration (2026-10-16)

TargetBufferMemoryLoadRequest/2 carries the existing Lowering Plan2 through the
original artifact loader. Matrix rules name exact source/target graph nodes and
the target form; a separate maxMatrixRules bounds their sum across every binding
and push block. Request1 cannot borrow Plan2 or matrix budgets. Unknown versions,
missing/duplicate/unused/invalid rules, pointer/count inconsistencies and exhausted
budgets fail without an artifact or partially published declaration graphs.
Atomics and descriptor arrays retain their separate refusal boundary.

The declaration processor freezes the verified result in the same original
artifact under its original quota and ownership checks. Caller request/rule
mutation cannot alter those facts. Graphics aggregation retains its existing
child and combined source/target type comparisons. Null, Vulkan, Metal and WebGPU
acquire no execution capability from declaration proof; matrix arithmetic,
skinning, package/Root admission and production operation gates remain separate.

The test-only buffer_matrix_probe uses the normal Slang compiler with explicit
row-major layout, generating SPIR-V/MSL/WGSL/reflection and an original single-entry
publication. It covers current/previous matrices and a fixed palette in a320-byte
uniform declaration. Windows original-artifact and related regression checks
pass13/13; two additional mixed package regressions pass after the planning fixture
explicitly supplies fault-readback capability and checks refusal when it is absent.
Actual Mac original-artifact/L2 checks pass with1012 input hashes matched; Wasm
Release/Node passes its registered original-artifact check. Source guards417/417,
style5/5 and clang-format21.1.8 checks pass.

This is scoped Platform Validated original single-entry declaration custody.
At that input epoch, ordinary bindless package/Root integration, Graphics matrix
aggregation and actual matrix operations were unvalidated. Current declaration
and Compute Root inspection increments are documented above. Evidence is under
build/bindless-mixed-public-execution/production-abi/request2-report.json.

## Mixed retirement and production matrix declaration increment (2026-10-16)

Vulkan lifecycle17 is a callback-free mixed stream. Its device-idle lowering now
consumes the same exact-runtime native idle receipt as the existing fence domains.
A new four-backend test fails before the correction and passes after it. Null,
Metal and WebGPU do not acquire device-idle rights; callback lifecycle18 remains
closed to idle. Eight Vulkan scenarios x clean/fault pass16 real executions,
including uncertain queue/fence observations and a refused native invocation.
Invalidated journals/results stay unpublished while retained quota returns to zero.
Mac separately passes four regressions and ten mixed lifecycle executions with1001
input hashes matched. Browser Release clean/fault and termination evidence remains
tracked individually in the single WOCC delivery table; the final matrix is pending.

SpirvWgslBufferMemoryLowering Plan2 adds explicit matrix column-memory pairs,
including Slang's existing struct-of-fixed-column-array target representation.
It compares complete typed children, dimensions, column stride, occupied extent,
member offsets and array rank. The source matrix and compiler targets are not
rewritten. Plan1 keeps matrices closed. Missing, invalid, duplicate, unused and
over-budget rules fail without publishing any facts; atomics stay closed.
Windows, actual Mac and Wasm/Node run the contract. Real WOCC shadow skinning,
PBR temporal skinning and previous-camera declarations match16432/32864/128 bytes;
missing rules and copied targets with incorrect column width are refused.

These are cold declaration facts. Existing artifact-loader/Root admission still
requires its own versioned integration; matrix multiplication, skinning, resource
ownership, execution authority and the common production gate are not granted by
Plan2. Next integrate authenticated production ABI requests and obtain actual
matrix/instance/material numeric execution before scene capability publication.
Evidence: build/bindless-mixed-public-execution/production-abi/current-report.yaml.

## Mixed numerical and view retirement oracle (2026-10-16)

The D32-to-RGBA16F copy checks the finite binary16 representations produced by
nearest-even or toward-zero format conversion, using the shared core encoder.
It does not impose an undocumented nearest-even guarantee on native image
writes. Vulkan and Metal independently produced 8642 for .01125 (nearest-even
is 8643). Every copied pixel must match one of the two exact encodings, its RGB
lanes must agree and alpha must equal 1. Reduction is then checked bit-for-bit
against those independently observed copy texels; Results must reproduce the
exact copy/reduce bytes. No epsilon, arbitrary adjacent-value allowance or
backend-specific expected output is used. WebGPU and Traditional references
are subject to the same format rule; Null supplies no numeric execution proof.

WebGPU's non-owning texture-view cache must release expired weak control blocks
and their core-allocated list nodes when a view dies while its parent remains
live. No vector spare capacity remains charged to a retired view's quota. Genuine pending command owners
still retain the view and parent until completion. Cache retirement neither
issues completion nor changes resource identity, state or content authority.
The retained-allocation contract now checks the child's quota with a live
parent, in addition to the existing ownership-cycle check. Vulkan/Metal retain
their existing native view ownership; Null remains a validator.

Current failures before these fixes are preserved under
build/bindless-mixed-public-execution: Windows attempt3 and Mac attempt7 show
the same quantization mismatch; Chrome clean shows 304 CPU bytes retained after
cold planning. Exact core half/retention contracts pass2/2; actual Chrome
subsequently passes the cache/quota boundary and records three real dispatches.
Normal Native mixed clean/fault numerics, completion and retirement pass on
Vulkan and Metal. Vulkan independent RenderDoc replay passes with no validation
messages; Metal has genuine gputrace/readback. Browser completion and the mixed
termination matrix remain incomplete; see the single delivery status table.

## Three-entry public stream validation (2026-10-16)

Native11 and Emulated12 now have explicit matching lowering and queried device
capability correspondence. The shared execution fixture reuses the existing
admission, inventory, graph, journal, fault Result and completion chain for three
dispatches, exact cell dependencies and five-texel/eight-lane numerical checks.
Cold Preparation10/Closure13 stay cold, and generic scene execution stays closed.
Normal Native GPU evidence is available; the browser mixed numeric/completion
case and full mixed termination matrix remain open. Source417/style5 and the
fast contract pass.
See D:/wocc/docs/bindless-delivery-status.md for current evidence and open gates.
The following zero-capability checkpoint describes its earlier source epoch.

## Historical input epoch: independent mixed execution profiles (2026-10-16)

Native Execution11 maps only to Closure14, Lifecycle17, Capacity9, Content15 and
Completion11. WebGPU Execution12 maps only to Closure15, CallbackAdmission5,
Program5, Lifecycle18, Capacity6, Content16 and Completion12. Public inputs retain
an explicit typed finalRoot, and public budgets require EntryFlow1 opt-in plus a
bounded declaration-visit ceiling. All previous execution profiles refuse these
extra inputs/budgets. Preparation10 and Closure13 remain cold-only.

Three prepared entries share the original canonical inventory, snapshot and
authorization. Native commands retain and choose each authentic pipeline in
copy/reduce/read order. WebGPU keeps the third closure through the original cold
admission, program and callback state; native owner union projects all three
closures through SharedIdentityProjection, preserving genuine identity and
deduplicating common resources. It never sums independently charged closures to
estimate a union. Pending depth validation and completion use the existing seal,
content/state journal and unpublished Result boundary.

Capability publication remains zero and mixed execution lowering remains
DiagnosticFailure on all backends. Backend compilation and old-domain regression
do not prove this new mixed execution. Genuine three-dispatch numerical/fault,
visibility, completion/publication and retirement evidence is still required.

## Authenticated mixed EntryFlow1 (2026-10-16)

OriginalBindlessMixedEntryFlowContract inspects three authentic Compute Roots in
their original canonical inventory with explicit opt-in and a declaration-visit
ceiling. Their snapshot and authorization must have the same real shared owner.
Authenticated active declarations determine roles: completed-depth read plus one
color write, that exact color read plus a distinct color write, then both exact
color reads and one Results write. Metadata and fault access stay bound to the
original internal buffer leases. No entry name or binding-number convention
substitutes for these roles. Exact resource/subresource correspondence reuses
the original identity and range; output write cells must not overlap.

Failed inspection returns no source indices or partial visit receipt. Missing,
duplicate, reordered or forged owners and insufficient visits fail closed.
Null validates metadata only; Vulkan/Metal/WebGPU use the same CPU inspection.
WebGL explicitly refuses this domain. No command, content mutation, execution,
completion or application-mode authority follows from successful inspection;
the old two-entry storage execution contract continues to reject mixed entries.

Final Windows3/3, actual Mac3/3 and actual Chrome pass, with source417/417 and
style5/5. All 999 synchronized Mac input hashes match the local input epoch.
Evidence: D:/arp/build/bindless-mixed-entry-flow/current-report.json.

## Independent mixed native Closure13 (2026-10-16)

Independent Closure13 retains three authentic Compute Roots over the same
Preparation10 snapshot, authorization and canonical source inventory. Per-entry
shader access uses the existing authenticated EntryAccess1 masks. Original D32
completion proof, RGBA16F exact mip views, Results and all three snapshot/fault
buffer leases retain their original resource identity/generation and quota owners.
There is no new registry, resource store, state journal or completion issuer.

The L2 contract requires mixed Preparation10, Compute-only entries, no ordinary
additional domains, explicit metadata/native/retained-owner ceilings and a matching
completed-depth input budget. Earlier Closure1-12 still refuse mixed profiles;
Closure13 refuses snapshot-only Preparation9, old storage profiles, output
attachments and initialized geometry. Null remains validator-only; Vulkan/Metal
provide Native correspondence and WebGPU Equivalent scalar correspondence. None
of these grants mixed recording, execution, completion or application selection.

Real Vulkan/Metal/Chrome create and validate each of the three closures in one
canonical inventory. Exact/one-less resource/native/retained-owner and depth input
budgets, wrong depth proof profile, exhausted CPU quota, forged shared owner and
all-or-nothing cleanup pass. Entry masks exclude inactive resources and the
copy-only fault destination. GPU/source/native metadata charges return to their
original baseline. The earlier Preparation10 binding and recording-refusal checks
remain, as do the original independent depth/storage execution regressions.

Affected validation: Windows8/8, actual Mac3/3, actual Chrome, source417/417 and
style5/5. Chrome has zero runtime/native validation errors and its complete native
call ledger and layouts are identical to Preparation10 (encoder13/submit9/map6);
Closure13 adds no GPU resource creation, encoder, submission or map. The original
five depth execution cases and three numerical receipts remain. These are scoped
Backend/Platform Validated cold correspondence results, not mixed GPU output.

The first Mac run found two old unknown-version probes still using13; they now
check14. The next run found the new fixture wrongly requiring a shader declaration
index for the copy-only fault destination; it now explicitly requires SIZE_MAX
for that original copy declaration. Neither correction relaxes production gates.
The final Metal check derives color-cell presence from actual retained sources,
independently from the retained depth proof. Failure logs are preserved.

Actual Mac inputs and current local inputs are archived separately with explicit
differences. Same integrated production source/workload is not certified. Previous
reports remain immutable. Evidence:
D:/arp/build/bindless-mixed-root-closure/current-report.json / verification.json.

| Delivery scope | Current evidence | Remaining action and exit condition |
| --- | --- | --- |
| Shared mixed Root/native Closure13 | Three typed Roots, authentic entry masks and original proof/view/buffer custody on Vulkan/Metal/Chrome; exact/less/quota/owner refusal and baseline retirement | Real mixed command program, state/content journal and submission/completion/retirement |
| Mixed D32 -> RGBA16F execution | Shared compiler package, snapshot, authorization, prepared bindings and native closures; Content15/16 CPU planning | Actual copy/reduce/read numerical contribution, write/read visibility, fault, failure publication and genuine completion on three backends |
| Production common engine gate | Open; independently validated depth/storage and other existing scopes retain their evidence | Production material/instance/skinning ABI, stages, sampling/gradients, terrain arrays and complete device/reserved/in-flight budgets; same frozen source/workload |
| renderer/WOCC -> GPU-driven -> acceptance | Open; WOCC Traditional | After common gate: mesh -> character -> vegetation -> terrain, each color/depth/shadow, cull/indirect, finite browser Emulated scene and visual/Release/platform acceptance |

## Independent mixed entry Preparation10

Preparation10 requires its own published capability plus the queried Preparation9
snapshot and Preparation8 storage binding prerequisites. It prepares the same
mixed image domain while independently permitting authorization and authentic
Compute pipelines/physical bindings. Preparation9 authorization remains refused.
Both retain execution=false; generic application selection and old Closure1-12
remain closed. No old storage/depth execution profile is broadened.

The shared physical image-cell exporter validates proof kind per declaration:
completed D32 requires the original depth proof and no color-cell identity;
sampled/storage RGBA16F requires the original cell identity and no depth proof.
Descriptor allocation, registry, source identity and generation remain unchanged.
WebGPU authenticates entry inactivity from issued WGSL bytes and the sealed
reflection, projects only active physical bindings, then validates real writable
aliases. Native bindings retain original physical owners. Mixed metadata does
not synthesize content or grant completion authority.

| Backend | Preparation10 lowering | Execution authority |
| --- | --- | --- |
| Null | Validator-only, capability unavailable; refuse creation/authorization | None |
| Vulkan | Native set with mixed typed proofs; authentic three pipelines/prepared bindings | None |
| Metal | Native mixed declaration validation, retained depth/cell proofs and authentic prepared bindings | None |
| WebGPU | Equivalent scalar Compute pipelines, authenticated active groups and original native resources | None |

Real Vulkan/Metal/Chrome preparation smoke plus L2/Null/package regression pass:
Windows8/8, Mac3/3, source417/417, style5/5 and Chrome depth execution5/numerical3.
The three ordinary-record negative probes have independent finite command CPU
budgets. Browser encoder13 includes those three unsubmitted probes; submit9/map6
remain the original depth workload. Pools and resource quotas return to baseline.
Preparation10 is scoped Backend/Platform Validated only for this cold domain.
Mixed Root/native closure, dispatch, fault/completion/retirement and the production
common gate remain open. The actual Mac source epoch is archived independently;
current backend-factory/browser-driver differences does not grant integrated-source acceptance.
Evidence: D:/arp/build/bindless-mixed-root-preparation/current-report.json.

## Shared mixed-image prerequisite and cold Preparation9

The authenticated DepthPyramid package fixes six scalar resource roles and
per-entry consumption masks without adding native access or execution authority.
Native capacity1 declarations use the reflected scalar ABI; capacity>1 arrays
retain their existing indexing. Inspected all-inactive Metal argument-buffer
namespaces remain explicitly Available; inactivity still requires both original
Metal namespaces and absence of a logical active binding. Actual Mac MSL6/6,
final-publication Chrome compilation3/3, package/schema/cross-target4/4 and
compiler46/46 pass. This establishes compiler prerequisites only.

Independent Preparation9 admits cold snapshots containing both original completed
D32 and exact RGBA16F storage/sampled cells. Its capability requires the separately
queried depth and storage prerequisites; generic execution and old profiles cannot
infer support. Null refuses. Three real backend snapshot smoke tests pass.
Authorization and all old closure/execution profiles explicitly refuse
this new cold scope until their own mixed contracts and evidence exist. No content
mutation, replacement resource identity or completion store is introduced.

SnapshotAllocation/1 is a cold Preparation9 query over the same three buffer
descriptors used by snapshot creation. It returns the greater of the immutable
package's logical byte minimum and the sum of reported allocation requirements.
Zero, underreported extents, overflow, unsupported versions or unavailable
capability fail closed before snapshot publication. A local artifact projection
may enlarge only snapshotBytes, with matching request/pool capacity; the package,
shader bytes, physical bindings and resource identities remain immutable.
The existing authorization reproduction projects this same resident envelope and
the plan hash includes it. Exact/one-less resident budgets are checked before
creating snapshot buffers. No query is added to command recording/completion.

| Backend | SnapshotAllocation/1 mapping | Execution authority |
| --- | --- | --- |
| Null | Explicit unavailable; validator cannot issue Preparation9 | None |
| Vulkan | Existing native buffer allocation query; actual VkMemoryRequirements for metadata/latch/readback | None |
| Metal | Dedicated-buffer ResourceAllocation length ledger, consistent with verified native buffer.length | None |
| WebGPU | Dedicated-buffer ResourceAllocation size ledger, consistent with validated native buffer size | None |

These are the existing backend accounting domains; they do not independently
prove total driver memory, scene peak residency or the final device-budget gate.
Observed Vulkan allocation176 vs logical144 and Metal/WebGPU144 reproduce actual
ledger extents. Windows mixed1/1 + regression7/7, Mac3/3, source417/417, style5/5 and
actual Chrome depth execution5/numerical3 pass. Compiler46 used the earlier stable
verifier; final package tests cover current Preparation9 and allocation failures.
This is scoped cold-snapshot Backend/Platform Validated only. Platform input
epochs, parallel-source differences, binaries and logs are archived explicitly in
D:/arp/build/bindless-mixed-shared-snapshot/current-report.json / verification.json.

## Content15/16: mixed read-only depth and storage planning

Content15 selects Native storage control6 plus completed D32 control4; Content16
selects Emulated storage control7 plus completed D32 control2. Budget admission
requires both domains. Canonical projection authenticates the original Root/view/
proof and requires depth dispatch consumption, storage writes and storage reads.
All failed preflights clear partial counts and leave plan/journal/quotas untouched.
Read-only depth is validated with its existing depth profile and is never enlisted
for content mutation. Storage proposals use the existing range timeline/journal.

Native positive tests reuse actual completed public D32 production and three
prepared public Roots over one canonical stream: controls2/cells4/steps10/commands33.
Original snapshots and authorizations remain retained independently. This is a
CPU proposal/rollback contract. Emulated Content16 also passes the actual Chrome
positive composition: controls2/cells5/steps12/commands33. Its existing public D32
producer has two independently completed cells, whose individual expectations
remain unchanged after hot0 apply/rollback. The authored command traversal budget
is one recording scan plus one scan per actual cell. Exact/one-less and exhausted
quota checks pass; five original public depth execution cases and three numerical
checks remain in the same browser run. Mixed planning adds no native GPU work.
Actual mixed GPU numerical contribution, a combined snapshot, completion and
retirement remain open. Lifecycle0-16 and Completion0-10 refuse these plans on every old issuer.
The generic execution gate remains DiagnosticFailure/executablePayload=false.

Current delivery status: D:/wocc/docs/bindless-delivery-status.md.
Evidence: D:/arp/build/bindless-mixed-emulated-plan/current-report.json.
Prior Native checkpoint: D:/arp/build/bindless-mixed-depth-storage/current-report.json.

## Previous checkpoint: public Native/Emulated storage on three backends (2026-10-16)

WebGPU now implements scoped Execution10/Closure12/Lifecycle16/Program4/
Content14/Completion10 through the existing public BindlessCross Root and original
queue/scope/map/loss/custody/drain chain. Vulkan/Metal retain Native9/Closure11/
Lifecycle15/Content13/Completion9. Both authenticated Compute entries share one
snapshot/authorization and canonical inventory/graph/journal. The same public
command stream performs full RGBA16F mip1/mip2 Clear/Store, storeMain, explicit
write/read dependencies, readMain, fault copy and real completion/retirement.

WebGPU freezes each original prepared pipeline/group/index per bind record.
Authenticated WGSL source hashes and entry reflection select inactive bindings;
no dummy resource or copied descriptor store bypasses physical alias validation.
Both original closures and their union of native owners stay retained. Callback
publication accepts exactly Lifecycle16/Admission4/Content14/Completion10;
other completion profiles and device-idle16 remain refused. Snapshot buffers and
descriptor authorization recognize the explicit storage plan, without opening
the generic execution gate.

The browser suite covers eleven terminal cases x clean/fault: normal completion,
caller drop, in-flight retirement, recorded cancellation, retirement before
submit, native recording validation failure, reversed genuine recording/submission
receipts, native submission validation failure, throws before/after actual queue
invocation and pending cancellation. Only normal/reverse cases publish; caller
drop cancels publication under the existing callback policy. Real drain releases
resources and returns all capacities/quotas. Exhausted core CPU quota proves zero
core allocations during public submit/completion. Recording failures cannot commit
content; failed/retired work never publishes results. Hardware loss remains open.

Four browser numerical receipts match the Native byte pattern: two independent
Results buffers [.75,.125,.25,1] / [.75,.125,1.25,1], five RGBA16F texels including
three untouched clear values [.0625,.25,.5,1], and exact fault0/1. Untouched mip0
stays Uninitialized with no generation. Content observations remain metadata and
cannot independently prove execution. The full browser Root regression also
checks two storage cases alongside existing graphics, ranges and sampled paths.

Current validation: Windows4/4, Node/Wasm2/2, Mac12/12, actual Chrome storage22/22 and full public Root regression, source417/417, style5 and lifecycle driver13/evidence18 pass. Vulkan clean/fault independent replay has no validation messages; latest Metal capture/readbacks are retained. Source, remote-input and evidence correspondence: D:/arp/build/bindless-webgpu-storage-execution/current-report.json / verification.json. This scoped storage domain is Backend/Platform Validated. The common engine gate and Production Ready remain open.

## Previous checkpoint: Vulkan/Metal public storage execution (2026-10-16)

Scoped Execution9/Closure11/Lifecycle15/Capacity8/Completion9 now runs on Vulkan
and Metal using the same public dual-entry command stream and original Content13
inventory/graph/journal. Native clear/store fully initializes two RGBA16F cells;
store/read, actual completion, fault publication and retirement preserve original
identities and generation. Vulkan shares the existing Graphics native pass factory
with exact subresource ranges and retains/revalidates both Roots and attachments.
Preparation8 stays cold; ordinary recording refuses9. Null/WebGPU explicitly
refuse this storage execution scope. Metadata observations remain non-authoritative.

Windows7/7, Node2/2, Mac12/12, actual Chrome regression, source417/style5 and
lifecycle13/18 pass. Native clean/fault x five terminal states each, hot-submit/
completion allocation0 and retirement0 pass. Vulkan/Metal pixels are bit-identical.
RenderDoc independently replays both entries, numeric buffers, five texels and
fault0/1 without validation messages. Current submitted Metal trace/readbacks are
archived. Evidence: D:/arp/build/bindless-vulkan-storage-execution/current-report.json
and verification.json. Detailed current table: D:/wocc/docs/bindless-delivery-status.md.
WebGPU storage, mixed D32/storage, production ABI/budgets, independent Metal replay,
hardware loss and the common engine gate remain open; WOCC stays Traditional.

## Previous checkpoint: Metal public storage execution (2026-10-16)

Scoped Execution9/Closure11/Lifecycle15/Capacity8/Completion9 reuses Content13,
the original canonical inventory, resource identities and journal. Two prepared
Roots share one snapshot/authorization; real clear/store initializes disjoint
RGBA16F mip1/mip2 before store/read and native completion/fault/readback/retirement.
CPU Root identity names the prepared entry. Existing GPU registration accounting
deduplicates the shared descriptor set; duplicate entry append refuses atomically.
Fault-copy accepts shared Roots only for the same snapshot/auth/canonical buffers.
Authenticated entry roles are checked at public and backend-private preparation.
Preparation8 remains cold and ordinary recording refuses Execution9.
Null validates/refuses; Vulkan/WebGPU deterministically refuse this scoped domain.

Windows3/3, Node3/3, Mac12/12, real Chrome regression and source/style/lifecycle
guards pass; 471 remote inputs match. Ten Metal terminal/fault cases, live Results,
five texels, generation, core hot-submit/completion0, rollback and retirement0 pass.
Submitted clean/fault trace and four raw pixel readbacks are archived. Metadata
observations never independently prove execution. Independent replay, discarded
command capture, hardware loss, mixed D32/storage, cross-backend storage execution,
production ABI/budgets and the common engine gate remain open. WOCC Traditional.
Evidence: D:/arp/build/bindless-storage-execution/current-report.json / verification.json.
Current detailed state: D:/wocc/docs/bindless-delivery-status.md.

## Previous checkpoint: disjoint storage physical bindings (2026-10-16)

Preparation8 uses two active mip1/mip2 views and independent Results. Canonical
Root retains every original view; entry projection verifies both cells. The L2
pipeline-interface contract checks writable texture overlap. WebGPU checks actual
selected physical slots before native group emission and cross-group aliases at
Root revalidation; real Chrome rejects both old padded texture and buffer aliases.
Windows3/3, Node3/3, Mac11/11, Chrome GPU and source/style/lifecycle guards pass;
469 Mac input hashes match. This remains preparation, not storage dispatch,
full initialization, completion or write/read numerical evidence.
Evidence: D:/arp/build/bindless-storage-layout/current-report.json / verification.json.
Current detailed state and next actions: D:/wocc/docs/bindless-delivery-status.md.
WOCC remains Traditional; P0-P6 and Production Ready are open.

## Previous checkpoint: Metal public D32 Compute execution (2026-10-16)

Metal now implements existing Preparation6/Execution6, Source2/Publication4,
Closure9, Content12/Lifecycle14 and Completion8 through the same original
inventory, graph and journal. Public D32 Graphics -> Compute nonuniform2
load/sampleLevel/query, numerical/fault, real completion and lifecycle pass.
Windows5/5, Vulkan Graphics4/4/depth, Node3/3, Mac11/11, Chrome GPU, source417/417
and focused guards pass. Capture/readbacks are archived; independent replay and
capture of discarded commands remain open. Storage execution is still closed.
Evidence: D:/arp/build/bindless-metal-depth/current-report.json / verification.json.
Current detailed state and next actions: D:/wocc/docs/bindless-delivery-status.md.
WOCC remains Traditional; P0-P6 and Production Ready are open.

## Previous checkpoint: authenticated storage entry access projection (2026-10-16)

EntryAccess1 selects the unique authenticated Compute module matching the issued
pipeline source/lowering identity, complete reflected descriptor interface and
workgroup dimensions. Missing, ambiguous or mismatched correspondence fails.
Inactive declarations require explicit target mapping evidence; absent reflection
alone cannot remove an access. The existing canonical dispatch projector now
uses that selected entry for Preparation8 shader consumption. Retention still
keeps all nine declarations and every original owner in the same inventory.
Store projects four accesses; read projects six, including physical padding.
The fault readback destination belongs only to its explicit copy.

Windows5/5, Vulkan Graphics4/4, Node/Wasm3/3, Mac10/10, real Chrome default Root
regression, source417/417, style5, lifecycle driver13/evidence18 and Vulkan
validation-layer smoke pass. All 466 observed Mac inputs match local
hashes, including the changed public dispatch projection header. Wrong interface,
stage/version/index, exact/one-less access and source-visit budgets, hot allocation
count and CPU/GPU retirement are checked on the three real devices.
Evidence: D:/arp/build/bindless-storage-entry/current-report.json and verification.json.

This validates access metadata through the genuine public command-input
projection, not native storage dispatch. Full native binding usage/alias checks
remain required independently. Storage completion and write/read numerical proof
remain open; Auto stays Traditional, Required and ordinary recording refuse.
Next: legal store/read pass resources, mixed completed D32 plus storage content
in the same journal, genuine GPU command/completion/fault/readback/retirement,
then production ABI/budgets, the common engine gate, renderer/WOCC and GPU-driven.
P0-P6 and Production Ready remain open; WOCC is Traditional.

## Previous checkpoint: cold typed storage native closure (2026-10-16)

NativeClosure10 freezes authenticated Preparation8 storeMain/readMain resources
over the same canonical Root and inventory on Vulkan, Metal and Chrome WebGPU.
It retains pipeline/prepared bindings, original view/parent and buffer payloads,
registration generations, exact descriptor correspondence and deduplicated owners.
The selected WebGPU physical group omits only authenticated inactive declarations;
the full snapshot and original sources remain retained. Metal keeps its exact
argument-buffer and native device/allocation ownership. Legacy exporters stay closed.

The genuine-device fixture uses independent writable Results buffers. Exact and
one-less entry/native-owner/retained-owner limits, exhausted quota rollback,
forged closure-owner refusal, legacy closure refusal, revalidation and destruction
return to CPU/GPU baseline pass. This is cold payload correspondence only: there
is no state proposal, native command/dispatch, storage write/read numeric proof or
storage completion. Auto remains Traditional; Required and ordinary recording
remain refused. Neither the same-cell sampled/storage cold arrangement nor its
texture padding proves an executable WebGPU pass layout.

Windows5/5, Vulkan Graphics4/4, Node/Wasm3/3, Mac10/10, real Chrome default Root
regression, source417/417, style5, lifecycle driver13/evidence18 and Vulkan
validation-layer smoke pass. All 463 observed Mac inputs match local hashes.
Evidence: D:/arp/build/bindless-storage-closure/current-report.json and verification.json.

Next, join mixed completed D32 inputs and storage cells to the existing command
graph/journal, with an authenticated per-entry access projection and legal native
pass resource arrangements. Retaining an inactive resource never proves shader
access, and a shader guard never exempts an actually bound resource from WebGPU
usage-scope alias rules. Validate genuine store-to-read completion, faults, budget
and lifetime, then production ABI/complete budgets and the common engine gate.
WOCC remains Traditional; P0-P6 and Production Ready are open.

## Previous checkpoint: authenticated cold storage Root preparation (2026-10-16)

OriginalPreparation8 adds authenticated StorageVector storeMain/readMain pipeline,
physical binding and canonical typed Root Source preparation on Vulkan, Metal and
Chrome WebGPU. It reuses the RGBA16F single-mip cell snapshot, original resource
leases, inventory and access declarations. Native arrays and bounded Emulated
scalar bindings preserve active/padding ownership. Pool exhaustion/release,
record refusal and CPU/GPU return to baseline pass on all three devices.

Authenticated per-entry reflection explicitly identifies inactive declarations.
Metal checks both direct and argument-buffer namespaces; WebGPU joins issued WGSL
source hashes to the verified package and checks its unique inactive mapping.
Missing, duplicate, active or conflicting mappings are refused. The full snapshot
and resource closure stay retained even when an entry's physical layout omits an
inactive declaration. This grants no pass-usage alias exemption or execution.

Preparation7 remains snapshot-only and refuses authorization. Preparation8 is
also non-executable: Auto selects Traditional, Required rejects, and ordinary
recording and legacy descriptor exporters stay closed. The Root Source has no
storage native closure, dispatch, storage completion or write/read numeric proof.
The cold fixture's alias padding is not an executable WebGPU resource arrangement.

Windows5/5, Vulkan Graphics4/4, Node/Wasm3/3, Mac10/10, real Chrome default Root
regression, source417/417, style5, lifecycle driver13/evidence18 and Vulkan
validation-layer smoke pass. All 462 observed Mac inputs match local hashes;
Vulkan and Metal consume byte-identical freshly generated Native shader packages.
Evidence: D:/arp/build/bindless-storage-root/current-report.json and verification.json.

Next: typed storage native closure, mixed completed D32 input, real storage
command/store-to-read/completion, numerical/fault/budget/lifetime acceptance,
production ABI and complete budgets, then the common engine gate and WOCC.
P0-P6, full-scene execution and Production Ready remain open; WOCC is Traditional.
Current delivery table: D:/wocc/docs/bindless-delivery-status.md.

## Previous checkpoint: cold typed storage-cell snapshots (2026-10-16)

OriginalPreparation7 admits bounded RGBA16F single-mip Compute snapshots on real
Vulkan, Metal and Chrome WebGPU. Native arrays and scalar Emulated layouts retain
the same original texture-cell sources, padding aliases and three internal buffer
leases. Seal/immutability, inactive-handle refusal, exact/one-less/exhausted CPU
quotas, pool retirement, generation advance and the original journal's Pending,
rollback and destruction paths pass. Store|Query requires ShaderWrite only.

The shader interface in this smoke is synthetic. It proves real-device cold
snapshot allocation and metadata ownership; it does not prove a compiled storage
pipeline, authorization, Root command, shader write/read or native completion.
Preparation7 cannot select Indexed via Auto/Required and executable authorization
remains explicitly refused. Legacy native descriptor exporters remain closed.

CPU pressure exposed retained backend registry capacity charged to a retired
candidate. Vulkan now uses per-registration list nodes; Metal/WebGPU use ordered
map nodes. Unregister returns each node's charge without retaining vector/bucket
capacity; existing locks and original registration budgets remain authoritative.
This changes cold registry storage, not hot command admission or completion.

Windows5/5 plus Vulkan Graphics4/4, Node/Wasm3/3, Mac10/10, actual Chrome default Root regression,
source417/417, style5, lifecycle driver13 and evidence18 pass. All 448 observed
Mac inputs match the local source manifest. Vulkan validation-layer smoke passes.
Evidence: D:/arp/build/bindless-storage-snapshot/current-report.json and verification.json.

| Delivery boundary | Current evidence | Remaining exit action |
| --- | --- | --- |
| Typed cold storage snapshot | Three real devices, padding/seal/internal buffers/quota/journal | Canonical typed Root authorization, pipeline and prepared projection |
| Public RGBA16F storage write -> sampled read | Declaration, creation, descriptor and snapshot prerequisites | Mixed completed D32 inputs; genuine command/completion; numeric/fault/budget/lifetime |
| Public sampled Graphics and ordinary buffers | Existing scoped evidence retained; current Mac regression | Production geometry/material/instance ABI, complete device budgets, independent Metal replay |
| Common engine gate / renderer / GPU-driven | Open; WOCC Traditional | Three-backend common gate, then mesh -> character -> vegetation -> terrain with color/depth/shadow |

Continue canonical Root and genuine storage completion, production ABI/budgets,
common engine gate, renderer/WOCC, GPU-driven and integrated acceptance. P0-P6,
full-scene public execution and Production Ready remain open. General WebGPU
execution remains DiagnosticFailure. Do not expand unrelated private prototypes.

## Previous checkpoint: typed texture-cell descriptor publication (2026-10-16)

TextureCellDescriptor1 (native creation6) and /2 (WebGPU creation7) independently
publish retained metadata for authentic RGBA16F single-mip Compute views:
sampled read or WriteOnly storage. The retained range receipt authenticates
resource/control identity, generation and exact range only. Its old content
stamp is never reused as completion evidence. Full content validation still
refuses Pending/stale observations; initialization and completion remain with
ContentPlan13/14, Sequence4 and the original journal.

Real Vulkan, Metal and Chrome WebGPU resources pass mip1 publication, wrong
range/profile/owner/budget refusal, source replacement refusal, failure without
descriptor revision publication, retained owner lifetime and journal
proposal/rollback/drop validation. Identity-only descriptor revalidation stays
valid across Pending and rollback, with no hot allocation; ordinary content
validation remains closed. Metal authenticates native mip dimensions using the
shared extent contract and checks ShaderWrite usage independently. The three
legacy native payload exporters refuse the new descriptors. WebGPU's legacy
Completed factory also refuses them; its new cell factory retains metadata only.

Windows5/5, Node/Wasm3/3, Mac10/10, actual Chrome default Root regression,
source inventory417/417, WGSL style5, lifecycle driver13 and evidence18 pass. 445 actual
remote source/artifact inputs match local files. Vulkan's new publication smoke
also passes with the validation layer. Evidence:
D:/arp/build/bindless-texture-cell/current-report.json and verification.json.
This is scoped real-resource descriptor metadata validation, not a storage
dispatch, argument-buffer command, readback or completed content receipt.

| Current delivery boundary | Evidence | Remaining exit action |
| --- | --- | --- |
| Typed RGBA16F cell descriptor metadata | Three real-device mip1 publication/refusal/lifetime/journal smokes | Positive typed snapshot/padding and canonical Root preparation/projection |
| Original ContentPlan13/14 journal | Existing scoped evidence retained and rerun | Mixed completed D32 input, genuine command/completion consumer |
| Public RGBA16F WriteOnly -> sampled read | Declaration, creation and descriptor prerequisites | Same-Root numerical/fault/budget/lifetime verification on all three backends |
| Public RGBA8 sampled Graphics + ordinary buffers | Previous GPU/capture evidence; current Mac10 regression | Production geometry/material/instance ABI and complete reserved budgets; Metal independent replay |
| Common engine gate / renderer / GPU-driven | Open; WOCC Traditional | Common gate, then mesh -> character -> vegetation -> terrain, color/depth/shadow together |

The snapshot API now carries the typed cell record and inactive-slot aliases;
its positive device integration remains open and is not inferred from descriptor
tests. Next close typed Root preparation, mixed depth inputs and genuine storage
write/read completion, production ABI/budgets, then the common engine gate and
renderer/WOCC. P0-P6, full-scene execution and Production Ready remain open.
General WebGPU execution remains DiagnosticFailure. Metal independent capture
replay remains unverified. Historical private prototypes are not reopened.

## Previous checkpoint: storage cell content plan and original journal (2026-10-16)

ContentPlan13 (native creation6) and /14 (WebGPU creation7) now preflight and
populate the existing range-content arrays with Sequence4. Their original
controls propose into the same state journal; exact/one-less quotas, cold
allocation failure, hot0, explicit rollback and pending-root destruction are
validated using genuine Vulkan, Metal and Chrome WebGPU resources. A clear/store
proposal never becomes Completed on CPU; all cells remain Uninitialized after
rollback and generations/revisions are not recycled. Legacy content and native
completion profiles retain their refusal boundaries.

The new bounded Root Compute texture projection authenticates canonical parent,
view owner, descriptor declaration, resource generation, exact mip, stage and
WriteOnly RGBA16F access. This branch remains a contract prerequisite: typed
storage descriptor publication, mixed completed-depth inputs, authentic public
native command consumers and real completion are still open. The device smoke
validates attachment initialization proposals and journal rollback, not a storage
shader dispatch or GPU write/read receipt. No execution capability is published.

Current Windows4/4, Node3/3, Mac10/10, actual Chrome default Root regression,
source inventory417, WGSL style5 and lifecycle evidence18 pass. The 442 actual
remote source/artifact inputs match local files. Vulkan also passes the new
journal smoke under the validation layer. Evidence:
D:/arp/build/bindless-storage-plan/current-report.json and verification.json.
Contract rules and the scoped real-resource CPU journal path are validated;
public storage GPU execution and production readiness remain open.

| Current delivery boundary | Evidence | Remaining exit action |
| --- | --- | --- |
| RGBA16F storage cell plan / original journal | Three real-device resource-owner smokes; proposal/rollback/drop, exact-less, hot0 and quota0 | Typed descriptor publication, mixed completed depth inputs and genuine command/completion consumers |
| Public RGBA16F WriteOnly -> sampled read | Slang declaration ABI and resource creation evidence retained | Same-Root numerical/fault/budget/lifetime validation on Vulkan, Metal and WebGPU |
| Public RGBA8 sampled Graphics + ordinary buffers | Existing scoped evidence; current Mac10 regression | Production geometry/material/instance ABI and full reserved budgets; Metal independent replay |
| Common engine scene gate | Open | Same frozen workload and complete queried budgets across three backends |
| Renderer/WOCC / GPU-driven | WOCC Traditional | After common gate: mesh -> character -> vegetation -> terrain, color/depth/shadow together |

Continue public storage publication and real write/read completion, production
ABI/budgets, common engine gate, then renderer/WOCC and GPU-driven. P0-P6 remain
incomplete; full-scene public execution, Production Ready and Metal independent
replay are not claimed. General WebGPU execution remains DiagnosticFailure.

## Previous checkpoint: initialized storage content and resource creation (2026-10-16)

Sequence4 extends the existing allocation-free cell timeline with storage writes.
Only an already initialized cell can be written: partial shader stores never
initialize a whole mip. It refuses active attachments, stale generations and
same-dispatch overlapping sampled read/write in either projection order; distinct
mips remain independent. State/hazard authority still belongs to the original
graph. This value contract grants no native command, journal commit or completion.

ContentCreation6 (queried native allocation) and /7 (WebGPU bounded texel
accounting) create exclusive RGBA16F 2D mip cells with storage/sampled/attachment
views. Every cell remains Undefined/Uninitialized. Legacy upload/attachment plans
cannot mutate the new domain. Public storage graph projection, proposals and
genuine completion remain to be connected to the existing journal.

Real creation and rollback pass on Vulkan, Metal and Chrome WebGPU. For the
authored three-mip texture, logical size is168 bytes; Vulkan queries1536 and
Metal384 bytes, while WebGPU explicitly accounts168 texel bytes without claiming
a native allocation query. Exact/one-less budgets, failure rollback, hot0,
shutdown invalidation and final quota0 are checked. The browser test exposed a
partial registry publication on CPU quota failure; both texture registration
paths now reuse a complete record constructor before publishing the entry.

Current Windows4/4, Node3/3, Mac10/10, browser default Root regression,
inventory417, style5 and lifecycle driver13 pass. 441 remote observed inputs
match the local files. Vulkan creation also passes with the validation layer.
Evidence: D:/arp/build/bindless-storage-content/current-report.json and
verification.json. Resource creation is scoped Platform Validated; the cell
timeline is L2 Contract Complete. RGBA16F public GPU write/read numerical,
fault and lifecycle acceptance remain open. Existing sampled-image numerical
and capture checkpoints remain valid for their declared scopes; no new storage
dispatch, image capture or Metal independent replay is claimed here.

| Current delivery boundary | Evidence | Remaining exit action |
| --- | --- | --- |
| Initialized storage cell timeline / resource creation | Sequence4 counterexamples; genuine Vulkan/Metal/WebGPU creation, exact budgets and retirement | Original graph projection, same-journal proposal/completion, public write -> sampled-read |
| RGBA16F WriteOnly declaration ABI | Sealed StorageImage/1 package, SPIR-V/MSL/browser pipeline evidence | Public numerical/fault/budget/lifetime acceptance on all three backends |
| Public RGBA8 sampled Graphics with ordinary buffers | Sealed Vulkan/WebGPU results; current Metal10/10 regression | Production geometry/material/instance ABI, remaining formats and complete budgets; Metal independent replay |
| Common three-backend engine scene gate | Open | Same frozen workload and complete queried budgets |
| Renderer/WOCC and GPU-driven | WOCC Traditional | Immediately after common gate: mesh -> character -> vegetation -> terrain, color/depth/shadow together |

Next connect initialized storage cells to the original Root graph and journal,
then validate public RGBA16F write/read on the three backends, close production
ABI/budgets and enter renderer/WOCC. P0-P6, full-scene execution and Production
Ready remain open. General WebGPU execution remains DiagnosticFailure.

## Previous checkpoint: RGBA16F storage declaration contract (2026-10-16)

StorageImage/1 defines the production depth-pyramid output as four-lane
RGBA16Float WriteOnly, using Slang WTexture2D<float4>.Store. Subsequent reading
uses a sampled view after an explicit dependency. Existing scalar R32Float
ReadWrite remains unchanged. Missing/unsupported version, RGBA16F ReadWrite,
storage loads and rehashed wrong SPIR-V/WGSL format/access are refused. The
four-backend declaration lowering grants no command execution capability.

Native/Emulated packages pass independent loading and counterexample tests.
Four SPIR-V modules validate, all four MSL modules compile on the real Mac,
and the actual browser creates both Compute pipelines without errors. Current
Windows3/3, Node3/3, Mac10/10, compiler2+4, inventory417 and style5 pass.
436 remote source/artifact inputs match the current local inputs, including
regenerated regression packages. Normal Windows root CMake generation and
focused build now pass; the earlier editor dependency-cycle blocker is historical.

Evidence: D:/arp/build/bindless-storage-vector/current-report.json and
verification.json. This increment is Contract Complete for declarations;
RGBA16F public execution and write/read numerical acceptance remain open.
The sealed Metal sampled-Graphics captures/readbacks and WebGPU/Vulkan GPU
evidence remain inherited checkpoints. Metal sampled Graphics still passes
the current Mac regression; its independent capture replay is unverified.

| Current delivery boundary | Evidence | Remaining exit action |
| --- | --- | --- |
| RGBA16F WriteOnly StorageImage/1 declaration ABI | Native/Emulated loading and refusals;4 SPIR-V/4 real Metal compiles;2 real browser pipelines | Public same-inventory/journal write -> sampled-read, real completion, numerical/fault/budget/lifetime verification |
| Metal public RGBA8 sampled Graphics + ordinary buffers | Sealed20 workflows,16 ordinary/8 pixel receipts,2 captures; current10/10 regression | Production resource formats, geometry/material/instance ABI and full budgets; independent replay |
| WebGPU/Vulkan public RGBA8 sampled Graphics + ordinary buffers | Previous sealed browser44 workflows/reference parity and Vulkan matrix/independent RenderDoc | Production ABI, remaining formats/storage and complete scene budgets |
| Common three-backend production scene gate | Open | Same frozen workload and queried complete budgets on all three |
| Renderer/WOCC and GPU-driven | WOCC Traditional | Immediately after common gate: mesh -> character -> vegetation -> terrain, color/depth/shadow together |

Next close the public RGBA16F storage-write/sampled-read path, production
geometry/material/instance ABI and full budgets, then the common engine gate
and renderer/WOCC. General WebGPU execution remains DiagnosticFailure.
P0-P6 and Production Ready remain open; compiler acceptance is no GPU receipt.

## Previous checkpoint: Metal public completed-image sampled Graphics (2026-10-16)

Metal public Graphics1 / Root2 now consumes authentic completed RGBA8 2D/2DArray
Fragment images with Load/Query/SampleLevel/SampleGrad, alongside ordinary
Vertex/Fragment Uniform/ReadOnlyStorage and Fragment StorageBuffer groups.
It retains the same original Sources, canonical inventory, authorization,
Content9, state journal and real Metal completion; sampled input barriers are
separate from attachment barriers. Ordinary descriptor/native execution refuses
these exclusive image Sources. The independent image opt-in defaults false;
queried native texture/sampler limits and actual Graphics result runtime gate
capability publication. Compute8 and WebGPU callback profiles remain distinct.

Current Mac10/10 passes20 sampled clean/fault/cancel/drop/retirement workflows,
16 ordinary buffer receipts and8 committed pixel receipts. Four actual RGBA
files match the numerical oracle; their PNGs have been inspected. Two real
Metal captures are retained.409 remote observed source/artifact hashes match
the current local inputs. Windows3/3, Node3/3, affected Vulkan2/2,
inventory417/417 and style5/5 pass. This is scoped Platform Validated evidence;
Metal independent replay remains unverified.

Evidence: D:/arp/build/bindless-metal-scene-sampling/current-report.json and
verification.json. The previous sealed WebGPU44-workflow/browser-reference
and Vulkan matrix/independent RenderDoc evidence is inherited, not rerun here.
Windows validation uses the saved focused Ninja graph because an unrelated
editor RESCAN dependency cycle blocks normal root CMake regeneration. The Mac
retains its isolated CMake and explicit focused registrations.

| Current delivery boundary | Evidence | Remaining exit action |
| --- | --- | --- |
| Metal public RGBA8 sampled Graphics + ordinary scalar buffers | Current Mac10/10,20 workflows,16 ordinary/8 pixel receipts,2 captures,409 matching hashes | Production formats/storage, geometry/material/instance ABI, full budgets; independent replay |
| WebGPU public sampled Graphics + ordinary scalar buffers | Previous sealed44 workflows,8 receipts, implicit reference parity | Production ABI, required formats/storage and complete scene budgets |
| Vulkan public sampled Graphics + ordinary scalar buffers | Previous sealed matrix/RenderDoc; current affected2/2 regression | Production ABI and remaining scene resource domains |
| Common three-backend production scene gate | Open | Same frozen workload/operations and queried complete budgets on all three |
| Renderer/WOCC and GPU-driven | WOCC Traditional | Start immediately after common gate: mesh -> character -> vegetation -> terrain, color/depth/shadow together |

Next close frozen production formats/storage and geometry/material/instance
ABI, then the common engine gate and renderer/WOCC. General WebGPU remains
DiagnosticFailure/executablePayload=false. Scoped RGBA8 evidence grants no
depth/comparison/storage-image, Vertex-image or general implicit-sampling
authority. P0-P6 and Production Ready remain open.

## Previous checkpoint: public sampled Graphics with ordinary buffer groups (2026-10-16)

WebGPU OriginalExecution3/4 now composes RGBA8 2D/2DArray Fragment sampling with
ordinary Uniform/ReadOnlyStorage buffers in Vertex/Fragment and StorageBuffer
in Fragment. Root2/PipelineInterface1 retains logical sets0/1/2 in the same
inventory, journal, Content9 and genuine queue/map lifecycle12. Coordinate
gradients still precede nonuniform dispatch and discard. Preparation4 remains
the independent depth profile; it cannot gain Array execution4 authority.

Real browser validation passes44 clean/fault/lifecycle cases and8 actual pixel
and ordinary-buffer receipts. Independent ordinary implicit-derivative reference
parity passes for both dimensions; this grants no general implicit opcode.
Ordinary outputs are[98,98,98,98] and[98,0,98,98], preserving Array discard.
Budget accounting includes typed descriptor and buffer/capacity owners, and
readback shares the inventory owner ceiling. Existing capacity rejection,
failure publication, callback ordering, hot0 and retirement checks remain active.
Full Root and composed browser regressions pass.

Vulkan current positive/partial/terminal matrices pass. Independent RenderDoc
replay identifies the actual sampled shaders, verifies three draws' pixels,
all three ordinary buffers, clean/fault copy and zero validation messages.
The pre-existing Geometry2/3 cold tests use their existing vertex/instance
packages; their exclusion of ordinary groups is preserved. Terminal tests still
exercise the original negative hooks/custody beneath the public view.

Windows5/5 plus package4/4, Node3/3, Mac7/7, inventory417/style5/driver13 pass.
Mac32 observed source/artifact hashes match. Metal evidence is shared regression,
not sampled Graphics validation. Actual browser and replay PNGs are inspected.
Evidence: D:/arp/build/bindless-webgpu-scene-sampling/current-report.json.

| Current delivery boundary | Evidence | Remaining exit action |
| --- | --- | --- |
| WebGPU public sampled Graphics + scalar ordinary buffers | 2D/2DArray44 lifecycle cases,8 receipts, implicit reference parity, full/composed regressions | Production geometry/material/instance ABI; required formats/storage and complete scene budgets |
| Vulkan public sampled Graphics + scalar ordinary buffers | Current driver matrix and independent RenderDoc pixels/buffers/fault | Production scene ABI and required remaining resource domains |
| Metal public buffers/color Compute | Current Mac7/7 and32 matching hashes | Public completed-image sampled Graphics, native argument mapping/residency and real readback/capture; independent replay |
| Common three-backend scene gate | Open | Same frozen workload/operations and queried complete budgets on all three |
| Renderer/WOCC and GPU-driven | WOCC Traditional | Begin after the common engine gate; mesh -> character -> vegetation -> terrain, color/depth/shadow together |

General WebGPU execution remains DiagnosticFailure/executablePayload=false.
Ordinary images/arrays/dynamic offsets and Vertex writable buffers are refused.
P0-P6 and the common engine gate remain open. Next close public Metal sampling
and the production scene domains, then immediately enter renderer/WOCC.

## Previous checkpoint: public WebGPU Root/2 scalar buffer Graphics (2026-10-16)

OriginalExecution2 now composes Vertex/Fragment scalar ordinary Uniform and
ReadOnlyStorage buffers plus Fragment StorageBuffer with indexed Fragment
buffers. PipelineInterface1/Source2 retains authentic Sources and uses the same
canonical inventory, state journal, closure4, Content9 and real queue/map
lifecycle12. Shared selected-entry reflection builds all active sets; recording
binds every prepared ordinary group. Vertex writable storage and ordinary
images/arrays/dynamic offsets still fail before execution. General execution
authority is unchanged.

The compiler's non-location WGSL interpolation normalization fixes Slang internal
struct copies without changing actual location IO. Five focused tests pass.
Real browser Compute/Graphics44/8 plus pre-record alias refusal, full/sampled/
array regressions, Windows3/3, Node3/3, Mac7/7, Vulkan composed2/2,
inventory417/style5/driver13 pass. Mac28 observed hashes match. Actual Graphics
receipts and inspected PNGs show red/green columns, indexed[777,778] and
ordinary output[875,876,875,876]; no independent browser capture is claimed.
Evidence: D:/arp/build/bindless-webgpu-composed-graphics2/current-report.json.

Scope remains scalar buffer composition. Public scene sampling with ordinary
groups, production geometry/material/instance ABI, scene formats/storage and the
common frozen three-backend gate remain open. WOCC remains Traditional; Metal
independent replay is still pending. Continue scene domains before renderer.

## Previous checkpoint: public WebGPU Root/2 scalar buffer Compute (2026-10-16)

PipelineInterface1/Source2 now composes Compute scalar ordinary buffers with
OriginalExecution1 and genuine callback lifecycle11. Physical preparation uses
issued(set,binding) mappings for all groups, authentic original buffer leases,
finite ranges, canonical inventory/CommandUsage and the original state journal.
Budget and immutable native correspondence checks precede recording; submission,
fault publication and retirement use real queue/map completion.

The four-backend interface contract explicitly rejects overlapping writable
physical buffer binding intervals on WebGPU. Inactive scalar slots still count;
shader guards cannot prove away native alias restrictions. Null only validates,
Vulkan/Metal preserve their existing mappings; readonly aliases and disjoint
finite slices remain admitted by the interval check. General execution authority
is unchanged. No dummy padding resources or hidden batches are created.

Real browser22/4 plus alias rejection, existing full/sampled/array browser
regressions, Windows2/2, Node2/2, Mac7/7, inventory417/style5/driver13 pass.
The Mac consumed regenerated current Slang outputs;12 observed remote hashes
match local sources/artifacts. Evidence:
D:/arp/build/bindless-webgpu-composed-root2/current-report.json.

Scope is Compute ordinary Uniform/ROStorage/Storage scalar buffers only. Ordinary
Graphics/images/arrays/dynamic offsets remain refused; the full production
three-backend scene gate and WOCC integration remain open. Next compose ordinary
Vertex/Fragment groups and scene ABI/operations on the same original Root.

## Previous checkpoint: public completed color Compute on Vulkan and Metal (2026-10-16)

OriginalExecution/8 now admits Vulkan as well as Metal through the same
callback-free Closure1/ContentPlan8/lifecycle8/capacity4 budget and completion5.
Vulkan reuses its existing native command and fence custody. Its public Compute
view freezes version6 or8 at preparation; cross-version record/submit requests
fail before dispatch. Queried RGBA8 sampling/filtering and descriptor budgets
gate capability. Ordinary Compute passes refuse public8 pipelines.

Vulkan registered tests pass28 normal/fault/terminal cases and two repeated
same-snapshot executions. Exhausted closure/seal/view quotas and incorrect
budgets never publish; cancellation, caller drop, missing queue retry, unknown
queue/fence quarantine and exact-runtime idle retirement use the same original
inventory and journal. Submit/completion allocate0. Two RenderDoc captures replay
the exact SPIR-V and numerical outputs, dimensions/mips and fault copies with no
validation messages. Evidence:
D:/arp/build/bindless-vulkan-public-color8/current-report.json.

Scope remains whole RGBA8 2D/2DArray Compute Load/Query/SampleLevel, explicit mip,
and scalar ordinary buffers. Null/WebGPU reject Native8. Fragment implicit
derivatives, scene formats/storage images, production ABI/geometry and the common
three-backend scene gate remain open. Metal has native capture/readback evidence
but no independent replay. WOCC remains Traditional.

## Previous checkpoint: Metal public completed color Compute (2026-10-16)

OriginalExecution/8 is a separate opt-in Native Compute domain over whole RGBA8
2D/2DArray control3 content. The public fence budget requires Closure1,
ContentPlan8/lifecycle8/capacity4 and completion5; neither buffer7 nor callback
budgets authorize it. Native facts and explicit image opt-in gate capability.
The lowering permits Load/Query/SampleLevel, rejects implicit derivatives,
depth/storage images/cube and accepts scalar ordinary buffer domains only.
Null, Vulkan and WebGPU refuse this new public Native profile explicitly.

Authorization and cold preparation use an exact snapshot exporter which retains
the actual published typed view Sources. Every sampled element must have an
authentic completed publication; ordinary export and ordinary/standalone
prepared consumption fail closed. The original native closure checks immutable
resource identities, published Source correspondence, native tracked parent
storage, physical mappings, generation and complete inventory/owner budgets.
The same program, seal and journal record whole-image barriers, compute work,
fault clear/copy and completion; read-only content stamps never advance. Only
the real completed handler commits and publishes. Failed sealed cold preparation
abandons its one-shot unsubmitted transaction; retry rebuilds that transaction.

Both dimensions pass Mac numerical/fault/terminal tests, same-snapshot repeats,
hot submit/completion allocation0 and final quota retirement;13 affected Mac
regressions, Windows3/3 and rebuilt Wasm/shared guards pass. Two native captures
and16 binary outputs are retained. Vulkan consumes the extracted shared program
builder and passes its full positive runner plus color/GI regression. No
independent Metal replay, scene visuals, implicit gradients, production geometry
or full-workload equivalence is claimed. Evidence:
D:/arp/build/bindless-metal-color8/current-report.json.

## Previous checkpoint: Metal completed texture publication: scoped Platform Validated (2026-10-16)

Existing Publication/1 now admits Metal whole RGBA8 sampled 2D/2DArray/single-cube
views after genuine control/3 upload completion. No new L2 version is introduced.
The set retains the authentic typed Source, parent and content stamp, authenticates
registry/view-parent correspondence and actual Metal shape/range, and freezes
native owner identity. Original image/view wrappers also freeze identity at
creation so a completed content stamp cannot be transferred to a replacement
native object before its first descriptor publication. Failed publication does
not change writes/revision; quota exhaustion uses the existing short RHI-OOM
diagnostic so reporting cannot allocate from the exhausted quota again.

Mac API Validation passes three publication shapes, all15 original upload cases
and13 affected regressions. Uninitialized/stale content, forged Source/native
owners, write/budget errors, genuine Source replacement and exhausted quota fail;
caller release retains the Source, retirement invalidates it and set destruction
releases it. Hot validation allocates0; final CPU/GPU budgets return to baseline.
Windows contract/Null3/3, rebuilt Wasm shared contract, inventory417/417 and
style5/5 pass. Evidence: D:/arp/build/bindless-metal-publication1/current-report.json.

This is cold publication only. Its ordinary native descriptor export remains
closed, with no prepared sampled payload, original sampled Root execution or
state authority. Null is validator-only; Vulkan and WebGPU retain their existing
publication profiles, and non-Apple Metal observation fails closed. Next carry
this same proof through the public Root/prepared binding and original
graph/journal/completion, then close frozen scene operations/geometry/ABI and
WebGPU domains before the common three-backend gate and renderer/WOCC.

## Previous checkpoint: Metal original texture upload (2026-10-16)

WholeOriginalUploadFence/7 now executes the existing ContentPlan/7 from the same
original inventory, graph, recording seal and journal on Metal. Cold staging
writes and flushes every packed byte, seals its original buffer, freezes native
objects/device/allocation owners and charges their complete retention. The native
command authenticates the exact six-operation upload program, canonical operands,
whole texture and per-layer native call budget before recording. One preallocated
encoder slot and the existing native submission/completed handler retain custody;
there is no second completion receipt or content journal. The same callback path
continues to drive existing buffer Compute/Graphics through a shared backend
custody interface. Non-Apple execution fails closed.

The validated upload scope is RGBA8 2D/2DArray/six physical cube layers, three
mips. Metal API Validation passes15 cases: normal, cancel, pre-submit original
device retirement, caller-command drop while queue-blocked and original device
retirement while queue-blocked, for each shape. Six successful outputs are read
back independently and compared row-by-row at every mip/layer (1512 logical
bytes); packed binary outputs are retained. Failure and retirement never publish.
Cold staging quota exact/one-less/exhausted probes, native/logical byte and owner
bounds, forged owner denial, hot submit/completion allocation0, GPU budget return
and final CPU quota0 pass. Affected Mac Compute/Graphics/image regression13/13,
Windows contract/Null3/3 and rebuilt Wasm shared contract pass. Final regression
and source/evidence hashes are recorded in:
D:/arp/build/bindless-metal-upload7/current-report.json.

This is scoped Platform Validated original upload support. Completed sampled
descriptor publication and original BindlessRoot image execution remain closed;
RGBA16F/3D upload, full scene image operations, production geometry/material ABI
and WebGPU Root/2 ordinary domains remain open. Original image creation and the
previous private image Compute results do not authorize those paths. The common
three-backend engine gate remains open; WOCC stays Traditional. Continue with
completed texture publication and the frozen scene sampling/ABI/geometry scope,
then the common gate before renderer/WOCC and GPU-driven integration.

## Previous checkpoint: Metal original images cold prerequisites

NativeClosure/1 retains sampled/storage views, exact original texture parents
and independent samplers through the existing typed Source inventory. Physical
binding/type/stage/array correspondence, original generations, native
device/object/allocation owners, texture shapes and heap identity are frozen.
The same owner projection includes both view and parent and deduplicates shared
native allocation owners. Native sampler caching is normalized only at cold
registry admission, against the original weak native owner; pointer equality
with a mutated payload is insufficient. Existing original buffer commands reject
image closures before lifecycle reservation. No new image execution gate opens.

ContentCreation/3 reuses the existing whole-upload domain on Metal: independent
2D/2DArray/Cube/3D textures begin Undefined and Uninitialized, reserve queried
native descriptor bytes and check actual independent allocatedSize. Cube
resources retain physical 2D-array layer indexing; cube semantics belong to the
view. The existing single-layer ContentCreation/4 attachment scope is preserved.
Null remains validator-only; Vulkan retains /3 native creation; WebGPU explicitly
refuses native-allocation /3 while keeping its separate logical-accounting
profiles. Creation grants no upload, sampling, transition or completion receipt.

Mac image correspondence1/1, affected regression12/12 and eight RGBA8/RGBA16F
creation cases pass with native validation. Cached-sampler forged-owner denial,
native/shape/heap mutations, owner/quota failures, hot allocation0, ordinary
exclusive-use denial and final retirement0 are covered. Windows contract/Null3/3,
rebuilt Wasm shared contract, inventory417/417 and style5/5 pass. Current WOCC
demand is regenerated and hash matched:15 variants/49 selected entries; requirement
and WGSL declaration tests19/19 pass. Evidence:
D:/arp/build/bindless-metal-image-root1/current-report.json.

Next is the same original upload staging/program, content journal and real
native completion, followed by completed publication and scene sampling/ABI and
geometry. Original image execution, new image output/capture, full three-backend
scene readiness and renderer/WOCC are not granted by these cold prerequisites.

## Metal public Root/2 scalar buffer Graphics (scoped Platform Validated)

OriginalGraphicsExecution/1 now also consumes ordinary scalar buffer sets through
the existing Root/2. Complete `(set,binding)` layouts use the existing shared
layout builder; per-entry compiler active/inactive mappings remain explicit.
An authored inactive stage may lack an argument-buffer interface, but must still
have its sealed compiler inactive direct record. Active-stage absence retains
the existing two-interface requirement. No guessed slot or inactive resource is
bound. Ordinary groups use the same native Graphics preparation with exact zero
GPU argument-buffer capacity. Closure owner projection and actual command
recording cover every group and both functions.

Uniform/read-only ordinary buffers admit Vertex/Fragment; writable ordinary
buffers admit Fragment only. Indexed Fragment StorageBuffer Load joins the
existing Query/Atomic scope; indexed Store remains refused. Graphics-only opt-in
publishes ordinary-buffer admission and group/slot ceilings from the native
probe; complete pipeline binding slots are checked across all groups.
No new L2 version, graph, journal, completion authority or runtime mode switch is
introduced. Ordinary images, arrays, dynamic offsets and indexed geometry remain
outside this Metal public profile.

Mac Root/1 public20, Root/2 public20, cold pass1/1 and affected regression9/9 pass
with validation. Each Vertex/Fragment stage reads Frame17+Shared32=49; four
ordinary output words875/876/875/876 and fault0/1 are exact. RGBA8/RGBA16F,
cancellation, caller-view drop, real original lifetime retirement, failure
publication refusal, allocation0 record/submit/completion and final retirement0
are verified. Submitted Root/2 clean/fault native trace is retained; original
attachment pixel readback and independent replay remain unverified. Evidence:
D:/arp/build/bindless-metal-graphics-domains1/current-report.json. Three-backend
scene acceptance, renderer/WOCC, GPU-driven and P0-P6 remain open.

## Metal public original buffer Graphics (scoped Platform Validated)

OriginalGraphicsExecution/1 is a public view over the same original Root/1,
canonical inventory, stream, Content9 transaction, lifecycle9/capacity5 and
actual native command completed handler. No parallel graph, journal, receipt or
publication authority is introduced. GraphicsPass/1 freezes the concrete
pipeline factory's compatibility facts, exact registered attachment sources,
native device/object/allocation owners and generations. It preflights separate
CPU/native capacity before creating one pass descriptor, reuses existing views,
and uses the existing Metal pass/encoder implementation. Ordinary attachment,
barrier and copy paths continue to refuse the exclusive attachment marker.
Only this same command's Pending custody may consume its prepared pass.

The finite buffer profile admits a single pass/bind/draw, authored viewport and
scissor, original attachment barriers, retained snapshot, fault clear/copy and
finish. Native completion commits the existing Graphics submission profile6
before publishing the fault result; device retirement invalidates publication.
Public capability additionally requires actual Graphics and native removal
result support. Ordinary Graphics domains, indexed geometry and images remain
explicitly refused by this profile.

Mac cold-pass test1/1, public execution1/1 (20 cases) and affected regression9/9
pass with validation enabled. RGBA8/RGBA16F, clean/fault, cancellation, view drop,
real original lifetime retirement, forged shared ownership, exact/one-less
owner and CPU budgets, quota overlap/exhaustion, native capacity exhaustion,
native identity mutation, hot record/submit/completion allocation0 and final
retirement0 are covered. Counters102/202 and fault0/1 are exact. Native submitted
clean/fault trace is retained; no original attachment pixel readback or
independent replay is claimed. Windows contract/Null3/3, rebuilt Vulkan composed
GPU3/3, Wasm shared contract, inventory417/417 and style5/5 pass. Evidence:
D:/arp/build/bindless-metal-pass1/current-report.json. The frozen three-backend
scene gate, renderer/WOCC and P0-P6 remain incomplete.

Earlier sections below are historical checkpoints; their closed-execution
statements describe those increments, not the current scoped public profile.

## Metal original loss and attachment prerequisites (scoped Platform Validated)

NativeResultLoss/1 reuses the already-budgeted native completed handler and
reports only an exact native command-buffer device-removal error before the
original journal/result publication. Its source belongs to the actual shared
device, retains only a weak bounded subscription and preserves first-terminal
semantics. Native delivery atomically invalidates the L2 lifetime without
touching its externally serialized epoch. Hot validation observes atomic
subscription identity without locking the cold subscription mutex. A dedicated process verifies real
healthy native completion, foreign-device refusal, duplicate subscription,
quota exhaustion, detach/rearm and concurrent injected loss. The injected
negative case is not physical device-removal evidence.

Descriptor-only loss registration/1 and /2 authenticate the actual issuing
registry and exact native buffer/texture/view/sampler owners, including live
view parents. Arena/set checks observe authentic registry ownership without
materializing native descriptor payloads. Pipeline/table joins remain separate.
ContentCreation/4 admits independent tracked single-layer 2D attachments from
Undefined. A queried native descriptor reservation precedes allocation; the
actual independent native allocation must fit it. Device accounting retains
that reservation until the final native owner releases it. Independent CPU
content metadata, original resource registration and Uninitialized cells do
not grant transition, sampling, rendering or completion authority. Private
payload/view creation markers prohibit ordinary descriptor, state, attachment,
copy and readback access even when native owners outlive their wrappers.

Mac RGBA8/RGBA16F/D32 creation, short native budget, exhausted/overlapping CPU
quotas, forbidden ordinary access, native retention and quota0 pass. Registered
new test1/1, affected Mac8/8, Windows contract/Null3/3, rebuilt existing Vulkan
composed GPU3/3, rebuilt Wasm shared contract, inventory417/417 and style5/5 pass. Evidence:
D:/arp/build/bindless-metal-content1/current-report.json. Original Graphics
Content9 journal, cold pass, execution and publication remain the next gate;
this prerequisite grants no Graphics execution capability or new capture claim.

## Metal original Graphics native correspondence (scoped Platform Validated)

NativeClosure/1 also freezes the existing single-set buffer Graphics Root.
The same canonical inventory, registration generations and physical descriptor
declarations are reobserved. Both compiled functions, sealed stage layouts,
prepared function records, descriptors and actual argument buffers are retained
with independent entry/native/retained-owner and metadata budgets. Shared owner
projection deduplicates the complete stage/pipeline closure. The existing Metal
Graphics function validator checks native mappings; no new mapping algorithm,
shader ABI or L2 version is introduced. Frozen stage/layout/descriptor owners
reject same-content replacements; frozen functions reject foreign owners even
when the raw pointer is unchanged. Closure validation remains allocation-free.

Mac clean/fault closure tests pass ownership/identity mutation, short budgets,
quota exhaustion, hot allocation0, original device retirement and quota0;
affected native regression7/7, host contract/Null3/3, inventory417/417 and style5/5
pass. Evidence: D:/arp/build/bindless-metal-graphics-root1/current-report.json.
This grants no original Graphics pass, journal publication or execution authority.
Ordinary Graphics domains remain refused. The buffer Compute issuer explicitly
rejects Graphics closures. Original attachment/content, pass and completion
correspondence are the next required increment before enabling this execution.

## Native buffer Compute public profile7 (scoped Platform Validated)

OriginalExecution/7 is the existing public execution interface over native
buffer Root/1 or Root/2, canonical inventory and original journal/result.
It selects Native explicitly and requires its independent queried capability,
NativeClosure1, lifecycle6, capacity2 and separate bounded view metadata.
Uniform/read-only/writable buffers and Compute are the entire scope. Native
completed handlers alone commit/publish, retaining custody after the caller
drops the view. Public preparation, recording and submission remain distinct;
recording is sealed, cancellation before submit rolls back, and in-flight
cancellation is refused. There is no L2 callback ticket or external submission
event domain. Metal implements this scoped adapter; Null, Vulkan and WebGPU
reject profile7 explicitly, retaining their independent existing profiles.
This profile never authorizes depth, images, Graphics, indirect, original
uploads or WOCC integration. Twenty-four public cases (twelve Root/1 and twelve
Root/2) pass on the Mac: clean/fault, cancellation, caller view drop and real
device lifetime retirement while native work is pending. Root/1 counters14/16,
Root/2 Frame17+Shared32=49, ordinary49/49 or49/50, fault0/1, no failed publication,
entry/owner/quota failure atomicity and hot submit/completion allocation0 pass.
Final Mac regression11/11, Windows contract/Null3/3, package Null mutations,
Emscripten shared contract/package checks, inventory417/417 and style5/5 pass.
The rebuilt current-source Vulkan composed Compute/Graphics/color-GI GPU
regression passes3/3.
The submitted clean/fault pair has a native trace and numerical readback;
independent Metal replay is not claimed. A mixed cancellation capture hit a
Metal debug-layer purgeability assertion; lifecycle tests run independently
from the final submitted-work capture. Evidence and retained diagnostic:
D:/arp/build/bindless-metal-domains1/current-report.json.

## Metal original buffer Compute join (scoped Platform Validated)

NativeClosure/1 freezes the existing authentic buffer Compute Root, canonical
inventory, prepared primary function bindings and scalar ordinary sets. It
retains each real buffer allocation, device/object, descriptor snapshot,
function, pipeline and argument buffer under a separate metadata quota. Owner
projection deduplicates identities and enforces entry/native/retained budgets;
partial failure publishes nothing. Original descriptor revisions, registrations,
full set/binding/element identities and native objects are reobserved before use.
The same Metal function mapper prepares scalar ordinary sets with a zero GPU
argument-storage budget; no new binding layout algorithm is introduced.

The original buffer Compute lifecycle6 now records that same stream and
submits a genuine Metal command. Only its native completed handler commits the
original journal and publishes the existing fault result; no second journal,
completion receipt or result store is introduced. Recording is sealed before
publication. External submission events remain explicitly refused in this
profile. Real delayed completion, caller command drop, cancellation and original
device lifetime retirement pass twelve combinations on Mac. Counters14/16,
fault0/1, failed-publication refusal, hot submit/completion allocation0 and
retirement quota0 pass. Mac regression9/9, Windows contract/Null3/3, Emscripten
shared contract, inventory417/417 and style5/5 pass. Evidence:
D:/arp/build/bindless-metal-root1/current-report.json.

Null remains validator-only; Vulkan/WebGPU retain their existing closures.
This earlier checkpoint did not grant public OriginalExecution or Root/2
execution. The profile7 increment above now proves the buffer Compute join;
geometry, original Graphics/images/upload and the three-backend WOCC gate
remain independent and incomplete.

## Ordinary color GI Compute join (scoped Backend Validated)

The existing whole RGBA8 upload/control3, completed descriptor Publication1,
Content8, Root Source2 and native fence lifecycle8 compose ordinary color GI.
A single cube is exactly six square faces; all authored mips/faces must be
uploaded, retained and sampled through the original whole-view journal. The
same single-cube shape helper is used by publication and read-plan validation.
This additive shape mapping grants no completion or shader authority by itself.
Null validates; Vulkan uses the original array-face native upload/view mapping;
Metal retains Publication1 refusal and WebGPU Publication2/Content10 retain
their existing shape bounds. Ordinary cube/2D/2DArray and independent regular
sampler admission requires actual native format queries, complete set budgets,
typed descriptor custody and original completed content before command claim.
This increment admits these ordinary image/sampler bindings in Compute only;
Vertex/Fragment are rejected by stable stage diagnostics until separately proven.
Depth/comparison, 3D and storage images remain separate closed execution domains.
The real-driver numerical/fault/cancellation/terminal/quota tests pass. An
independent RenderDoc replay verifies seven genuine dispatches, three sets each,
every uploaded mip/layer/face byte, 36 float outputs, fault0/1 and no validation
messages. Shared Mac regression9/9, four package/Null checks, four Xcode MSL
compiles, host3/3, affected compiler2/2 and Wasm21 actual package calls pass.
Final-source GI plus composed buffer Compute/Graphics GPU3/3, inventory417/417
and style5/5 pass. Full existing Vulkan regression passes330.43s on the retained
runner before final Compute-only stage refusal hardening; final-source tests
cover the new refusals and all three composed workloads. Exact evidence and
runner/source boundaries: D:/arp/build/bindless-color-gi1/current-report.json.
This does not open Metal/WebGPU Root/2 or the three-backend workload gate.

## Vulkan Root/2 Graphics scalar buffer join

The next admitted subset reuses Source/2, PipelineInterface/1, RootState/4,
Graphics Content/9, the existing public OriginalGraphicsExecution/1 and its
genuine fence lifecycle. Scalar ordinary uniform/read-only/writable buffers
retain exact set/binding/range/stage identities and all original owners. The
same closure entry/native-owner/retained-owner/CPU quota budgets include every
ordinary descriptor. Preparation failures publish nothing; cancellation rolls
back the original journal and only real completion publishes fault/content.
Vertex and Fragment reads and disjoint Fragment writes have exact numeric
and independent capture evidence. Plain snapshot binding still refuses these
packages. Additional-domain images/samplers and Root/2 initialized geometry
remain rejected. Null validates; Metal/WebGPU retain explicit native-closure
refusal. The registered public clean/fault workload passes cancellation/retry,
complete cold entry/owner/quota failures and zero hot record/submit allocation.
Vertex and Fragment each read17+32=49; indexed777/778 and ordinary
875/876/875/876 agree. Independent replay sees two real draws, three stores per
draw, exact red/green2x2, fault0/1 and zero validation messages. Mac current
shared/Metal regression9/9, four package/Null checks and six Xcode MSL compiles
pass. Windows affected compiler2/2, host3/3, composed GPU2/2, inventory417/417,
style5/5 and Wasm five compiler tests with45 actual package calls pass. Full
existing Vulkan positive/partial/terminal regression passes318.38s. Current
selected sources, fixed runners/packages, capture/log/hash evidence:
D:/arp/build/bindless-root-graphics-domains1/current-report.json.
No three-backend scene claim.

## Vulkan Compute scalar buffer composition (Backend Validated for this scope)

PipelineInterface/1 execution admission is explicit per ordinary descriptor
resource class in `BindlessCapabilities::ordinaryDescriptorDomains`. No table,
backend name or shader publication enables an ordinary class implicitly. Null
keeps Validator inspection; Metal/WebGPU retain their current refusal. Vulkan
admits Compute scalar uniform/read-only/writable buffers after real runtime and queried
limit enablement. The Graphics buffer join above extends that scope; images/
samplers in extra sets remain rejected until their operation, content and
completion contracts are implemented and validated.

The shared cold layout builder includes the indexed set and every ordinary set
by its complete `(set,binding)` identity. Vulkan counts all these bindings in
per-stage, whole-pipeline and writable-stage feature checks. Root/2 closure
retains and freezes every authentic ordinary native descriptor payload and
resource, bounded by the existing entry/owner/quota ceilings. Original source
positions, epochs, successful-write revisions, device identity, native handles
and exact buffer ranges are revalidated before recording and while Pending.

Binding the prepared Root records all its sets, without a second inventory or
new command/state identity. Existing Root/2 CommandUsage, state journal, recording
seal, real Vulkan fence, fault/readback publication and retirement remain the
only execution chain. Closure correspondence alone grants no execution. The
Vulkan exception in Source/2 native lowering does not enable Metal/WebGPU or
grant an application/WOCC mode. Acceptance requires the registered composed
Root workload, exact output from both ordinary and indexed buffers, fault and
cancellation/terminal/owner/quota checks before claiming Backend Validated.

The registered three-set workload now passes seven real Vulkan scenarios.
Binding3 repeats across sets0/1/2; Frame17 plus Shared32 produces indexed49 and
ordinary49/49, or49/50 with fault1 when the second logical index is inactive.
No-invocation, uncertain submit, missing entrypoints, withheld completion,
shutdown and genuine fence/device-idle retirement preserve the existing no-
publication rule. Unsubmitted cancellation returns all quotas. Entry/native/
retained owner budgets short by one and quota exhaustion refuse without partial
publication; hot submit/completion allocation remains0. Standalone snapshots
refuse composed domains. RenderDoc independently replays five actual dispatches,
each with three descriptor stores, exact outputs/fault and zero validation messages.
No scene, Graphics composition or three-backend engine gate follows from this.
Evidence: D:/arp/build/bindless-root-domains2-native1/current-report.json.

## Ordinary descriptor composition /2 (Contract Complete for custody and Null journal)

Root Source /2 retains the same authentic snapshot, prepared binding and
authorization/package, plus every ordinary descriptor domain required by that
package. Each domain names an exact canonical inventory position containing an
authentic DescriptorSet Source /2; equal layouts or resource IDs do not substitute
for that original owner. No parallel inventory, resource identity or pipeline
issuer is introduced. The descriptor Source keeps its existing weak inventory
join, so Root ownership cannot create a cycle through that inventory.

The complete set list, exact binding layouts including stages/depth/comparison,
successful-write revisions, allocation epoch, resource registrations and full
typed footprints are checked before publication and again on validation. All
domain elements are scalar, non-dynamic descriptors. `maxOrdinaryDomains` and
`maxOrdinaryElements` bound cold metadata and complete coverage; the existing
source quota pays every owned vector. Their owners already occur in the same
canonical inventory and use its existing owner/capacity union. No descriptor
write, GPU allocation, submission or partial Root publication occurs here.

Read projection appends ordinary buffer/view accesses to the existing Root
declarations, with original ranges, stages, access and parent/subresource
identity. Samplers stay retained without manufacturing a state access. Existing
CommandUsage and Root state projection remain the only graph/journal path.
The Null pipeline validator builds the complete package layout while keeping
execution false. RootState accepts ordinary Descriptor Source /2 only at the
exact positions claimed by a Root Source /2 in its canonical inventory; unrelated
typed descriptor/pipeline profiles remain rejected. No new graph is built.
This proves custody and declarations, not initialized contents or completion.
Root /1 refuses packages with additional domains. Native closures refuse Root /2
outside the admitted Vulkan Compute/Graphics scalar-buffer scope until their backend explicitly consumes all ordinary sets and
passes lifecycle/completion validation. No public capability is enabled here.

| Backend | Root /2 ownership/declaration validation | Existing native closure |
| --- | --- | --- |
| Null | Validator; complete canonical closure required | Refused |
| Vulkan | Same shared contract; typed scalar buffer admission | Compute and vertex-ID Graphics buffers; original fence/journal and quota checks passed |
| Metal | Same shared contract; existing additional-domain planning gate retained | Refused |
| WebGPU | Same shared contract; existing additional-domain planning gate retained | Refused |

All three required native paths must consume this same Root /2 and original
inventory/journal through genuine completion before the scene engine gate opens.

Current proof covers real Native/Emulated packages with ordinary uniform,
read-only/storage buffers, cube/2D/2DArray/3D/depth sampled views and independent
regular/comparison samplers. The shared fixture exercises both retention-only
and prepared dispatch access projection, the existing normalized journal,
apply/abandon, quota failure without partial publication, owner forgery, exact
position/layout/set coverage, stale successful-write revisions and arena reset.
Caller-drop retention and final owner/quota return pass with hot allocation0.
RootState admits a descriptor only after its owning Root is in the inventory.
These original dispatch/journal tests use Null; the newer Vulkan evidence above
adds GPU execution only for its explicit Compute scalar-buffer scope.

Windows/Mac compiler39/39, Wasm focused4/4 with41 actual package invocations,
source417/417 and style5/5 pass. Current Mac debug-layer existing platform
regression is8/8; Xcode compiles all8 ordinary-buffer/GI fixture MSL modules.
Exact sources/runners/packages and retained failed attempts are indexed by
`D:/arp/build/bindless-original-buffer-sources1/root-domains2-current-report.json`.
The additional Metal Compute platform case reuses this fixture with CPU-state
exercise disabled. Root/1 custody uses actual registered Metal resources and
prepared argument buffers; hot validation and final quota return pass. The
rebuilt existing Compute workload still reads counters114/226 with fault0/1
and passes cancellation/completion retention. This does not grant Metal Root
dispatch/journal/completion authority, Root/2 or public Graphics execution.

## Current Vulkan depth producer and publication4 (2026-10-15)

The existing public GraphicsExecution1 direct path now has actual D32Float
depth-only producer evidence on the same Content9/Lifecycle9 journal and real
fence. Source2 captures one completed control4 cell; independent Publication4
retains that proof/view/parent in original Vulkan descriptor storage. Typed
cross-profile rejection, cancel/retry, re-clear, stale proof/descriptor refusal,
fresh publication, hot validation and quota return pass. Independent RenderDoc
checks two Draw3 submissions on the same depth resource and the expected four
interpolated depth pixels, with no validation messages. Source1/Publication3
remain the independent WebGPU domain; ReadInput1 refuses Source2.

This is scoped Backend Validated Prototype producer/publication support. It
does not grant Vulkan depth Root/shader consumption or WebGPU/Metal execution.
Continue the original public depth consumer chain, production scene operations
and queried budgets, then renderer/WOCC, GPU-driven and Metal last.

## Current public depth Compute execution5 (2026-10-15)

Explicit WebGPU OriginalExecution5 consumes the same authentic Root/inventory,
Content11/RootState4 normalized journal and seal. Closure8, CallbackCold3,
Program3, Lifecycle13 and Completion7 retain the exact depth input and actual
native callback custody through recording, real queue/scope/map completion,
fault publication and retirement. There is no general execution capability
grant. D32Float 2D Load/Query/integer mip with a non-filtering independent sampler
is the scoped Prototype domain. Actual Chrome clean/fault, cold/recorded cancel
and reverse callback tests pass. Seven additional independent terminal runs
cover recording/submission errors, Pending cancel, caller drop, submission
exceptions and original lifetime retirement, with unpublished output and real
drain. Hardware loss is unproven. Null/Vulkan/Metal refuse this execution profile. Full scene depth,
storage, Vertex and workload gates remain open; WOCC stays Traditional.

See `bindless-sampled-depth-contract.md` for the authoritative version coupling,
Pending proof, operation/refusal and native writable-binding rules, and
`D:/wocc/docs/bindless-delivery-status.md` for current evidence and remaining work.

## Earlier SampledDepth/1 compiler boundary (2026-10-15)

The versioned depth compiler/package ABI is implemented; raw fractional LOD is
rejected by Slang's explicit ArpBindlessDepthMip type. SPIR-V Depth=1, MSL depth2d,
WGSL depth and ordinary/physical reflection are checked. Public planning rejects
with RHI-BINDLESS-SAMPLED-DEPTH-EXECUTION-UNAVAILABLE; original color profile
scope refusals remain independent. No backend depth execution is inferred.
See docs/bindless-sampled-depth-contract.md and the single current delivery
index D:/wocc/docs/bindless-delivery-status.md. Next: original control2 cell
range/generation/revision -> descriptor/content/state/native closure -> genuine
completion/fault/retirement. Storage/Vertex and scene budgets remain open;
renderer/WOCC follows the required Vulkan/WebGPU gate; Metal remains last.
Evidence: D:/arp/build/bindless-original-buffer-sources1/sampled-depth1-current-report.json.

## Public WebGPU implicit-reference parity (2026-10-15; scoped Backend Validated)

The public OriginalExecution3/4 Root outputs now match an independent ordinary
Traditional implicit-sampling reference for RGBA8 Texture2D and Texture2DArray.
The ordinary Slang shaders use static texture/sampler bindings and real Sample
derivatives before non-uniform material selection and array discard. The Root
computes gradients of the same UV before dispatch/discard and uses sampleGrad.
All sixteen output bytes match both clean public Root receipts in each cohort,
including the true non-base mip. Two uploaded textures, independent nearest/
linear samplers, two mips, and one/two layers share the documented input ABI.

The explicit ordinary compiler policy --preserve-implicit-sampling propagates
to sealed selected entries. Combined/Fragment WGSL retain textureSample and
SPIR-V retains OpImageSampleImplicitLod. A re-sealed legacy fixed-LOD shader and
matching base-level receipts are rejected by negative tests. Native derivative
uniformity validation remains enabled. The test-only reference creates no RHI
execution capability; original public C++ inventory, journal, genuine completion,
fault and retirement remain authoritative. Its independent ordinary device,
buffers and textures are destroyed after real queue/map and three error scopes.

Current integrated checks: CTest3/3 (two browser cohorts plus five-case reference
contract); public Root44 clean/fault lifecycle flows and8 numerical receipts;
source415/415; routing13/13; browser-process9/9. Both cohorts report hot0,
retirement quotas0 and no native errors/diagnostics. Exact durations, sealed
reference roles, source/binary hashes, snapshots and readbacks are archived at
D:/arp/build/bindless-original-buffer-sources1/root-web-implicit-parity-current-report.json.
The intentionally aborted first attempt (staged-script/source mismatch) remains
separate evidence. Browser assets now stage independently of the Wasm link.
Baseline-only Wasm and assertion-enabled O1 correctness evidence do not establish
default compiler or Release performance readiness.

| Delivery scope | Current evidence/status | Remaining action | Exit condition |
|---|---|---|---|
| Public RGBA82D/2DArray Root gradient sampling | Scoped Backend Validated; independent implicit reference, fault/lifecycle current | Actual scene UV/material/mip/filter semantics and required formats | Selected production artifacts preserve scene quality |
| Required Vulkan/WebGPU scene domains | Open; general executablePayload=false | Depth/storage; Vertex instance/skinning ABI and bounded-index/fault provenance; indirect hazards; full queried budgets | Public numerical/fault/budget/lifecycle gate on frozen requirements |
| renderer/WOCC and GPU-driven | Open; WOCC Traditional | After required scene gate, mesh -> character -> vegetation -> terrain, each color/depth/shadow | Same-generation real material/cull/draw/readback |
| Metal and integrated acceptance | Open; Metal last | Native execution/capture, integrated visual/Release/platform/server regression | Complete operation/capacity/workload gates |

This closes the finite reference fixture gate, not full-scene implicit sampling.
The old general WGSL fixed-LOD rewrite and WOCC selected Traditional artifacts
still need explicit quality migration/verification. No additional Null, Vulkan,
Metal or WebGPU execution capabilities are inferred. Null refuses execution;
Vulkan native-profile evidence stays independent; Metal MSL output is compiler
evidence only. General WebGPU executablePayload=false, WOCC Traditional and
P0-P6 incomplete. Next: required depth/storage closure/content/state semantics
and four-backend refusal matrix. WebGPU indexed Vertex operations still reject
with RHI-BINDLESS-VERTEX-FAULT-PROVENANCE-REQUIRED: no writable Vertex fault sink
can be inferred from Fragment success; an upstream sealed bounded-index proof
must cover actual instance/skinning inputs. Then remaining Vulkan/WebGPU scene gate ->
renderer/WOCC -> GPU-driven -> Metal last -> final acceptance.
Reference contract: D:/arp/docs/bindless-implicit-sampling-reference-contract.md.

## Earlier checkpoint: public WebGPU original 2DArray execution4 (2026-10-15; scoped Backend Validated)

OriginalExecution4 / NativeClosure6 executes completed RGBA8 Texture2DArray
Fragment load/query/sampleLevel/sampleGrad through public BindlessCross. Two
actual uploaded textures, each2 layers and2 mips, use independent nearest/linear
samplers and non-uniform selection. Gradients precede selection/discard and
select real mip1. Invalid layer/mip/texture/sampler return zero and latch fault;
discard preserves the explicitly authored opaque-black pass clear. Actual GPU
readback images were inspected; they are numerical fixtures, not a WOCC scene.

The original inventory, graph, Content9/read10/control1/Pending6 journal and
Lifecycle12 genuine queue/map completion remain shared. ColdPreparation3 and
Execution3 do not grant array execution. Legacy Compute sampled input still
rejects arrays. Upload bytes, packing, joins, copy work and mip/layer state cells
are derived from original input/resource geometry; one-less state capacity
rejects before publication. Failed publication, real callback drain, hot0 and
retirement quotas0 pass across22 clean/fault workflows and4 numerical receipts.
Three outer native error scopes drain without error.

Current focused checks: browser1/1; host2/2; Native/Emulated array and2D package4/4;
Wasm/Node1/1; source415/415; routing11/11; browser-process9/9; WOCC requirements15/15.
The affected2D22-flow/4-receipt cohort also passes on the same integrated binary.
Exact durations, source/binary hashes, source snapshot, readback and failure
history: D:/arp/build/bindless-original-buffer-sources1/
root-web-array-execution4-current-report.json and related files. Single-thread
Binaryen linking preserves the compiler pipeline; explicit baseline-only Wasm
and assertion-enabled O1 test TU provide correctness evidence, not default
compiler or Release performance evidence.

| Delivery scope | Current evidence/status | Remaining action | Exit condition |
|---|---|---|---|
| Public WebGPU completed2D and2DArray Fragment sampling | Scoped Backend Validated, independent version gates; current numerical/fault/lifecycle regression | Independent implicit parity; required formats and operations | Frozen scene requirements through public path |
| Required Vulkan/WebGPU scene domains | Open; general executablePayload=false | Implicit parity, depth/storage, indexed Vertex/instance/skinning ABI, indirect/hazards and complete queried budgets | Public numerical/fault/budget/lifecycle gate |
| renderer/WOCC and GPU-driven | Open; WOCC Traditional | After required scene gate, mesh -> character -> vegetation -> terrain, each color/depth/shadow | Same-generation real material/cull/draw/readback |
| Metal and final acceptance | Open; Metal last | Metal execution/capture, integrated visual/Release/platform/server regression | Complete operation/capacity/workload gates |

Four-backend Execution4 matrix: WebGPU equivalent public execution/completion;
Null refuses execution, Vulkan refuses this WebGPU-specific profile, Metal
explicitly unavailable. Closure6 provides correspondence only and grants no
execution independently. Other supported Native profiles retain their evidence.
General executablePayload=false, WOCC Traditional and P0-P6 incomplete.
Next: independent implicit-sampling parity, then remaining required Vulkan/WebGPU
scene operations -> renderer/WOCC -> GPU-driven -> Metal last -> final acceptance.
The legacy general WGSL fixed-LOD0 rewrite cannot serve as a same-quality
implicit reference; finite bindless packages do not use that rewrite.

## Earlier checkpoint: public WebGPU original sampled execution3 (2026-10-15)

OriginalExecution3 / NativeClosure5 now executes real completed RGBA8 2D
Fragment load/query/sampleLevel/sampleGrad through the public BindlessCross Root.
Actual view/parent/sampler owners, Content9/read10/control1/Pending6, original
inventory/graph/state journal and Lifecycle12 queue/map completion remain shared.
Non-uniform texture indices and independent nearest/linear samplers produce the
expected pixels, including true gradient-selected mip1. Fault0/1, cancellation,
record/submit errors, reversed callbacks, caller drop, throw-before/after-submit
and logical lifetime retirement suppress failed publication and release only
after actual callback drain. Hot allocations0, retirement quotas0 and all3 outer
native error scopes pass. Root checks its authentic registered descriptor
lifetime before legacy ValidationResult diagnostics can allocate on retirement.

Focused browser1/1 (263.36sec), host2/2 (1.87sec), Native/Emulated package2/2
(0.73sec), Wasm/Node1/1 (0.28sec), source415/415, driver10/10,
browser-process9/9 and WOCC requirements15/15 pass; diagnostics/nativeErrors0.
Evidence: D:/arp/build/bindless-original-buffer-sources1/
root-web-sampled-execution3-current-report.json, browser-report.json,
tested-source-sha256.json and tested-binary-sha256.json (same prefix).
Source snapshots and failed attempts are retained separately. This is scoped
Backend Validated, using the explicit baseline-only Wasm compiler policy and an
assertion-enabled O1 test TU; no default-compiler or Release performance claim.
The older110-flow cohort timed out and is not relabeled as current success.

Implicit sampling parity, 2DArray, depth/storage, indexed Vertex/instance/skinning,
indirect hazards and complete scene budgets remain open. General WebGPU
executablePayload=false, WOCC remains Traditional, and P0-P6 is incomplete.
Next: required Vulkan/WebGPU scene domains -> renderer/WOCC -> GPU-driven ->
Metal last -> comprehensive acceptance. Do not expand unrelated private pilots.

## Earlier implementation checkpoint: original WebGPU sampled execution3 (2026-10-15)

Independent OriginalExecution3 / NativeClosure5 retains real physical2D
texture views/parents and independent samplers with the same original Root,
Content9/read10/control1/Pending6 journal and Lifecycle12 completion. Slang
gradients precede non-uniform dispatch. The focused browser target requires22
clean/fault lifecycle flows,4 numerical receipts and final3-scope drain; it
does not borrow success from older buffer cohorts. Build/runtime validation
is pending. Scope is RGBA8 2D load/query/sampleLevel/sampleGrad only; implicit,
array, depth/storage and full scene execution remain closed. Null/Vulkan/Metal
refuse this independent WebGPU profile. No Backend Validated claim yet.

The initial cohort reached20/22 flows and4 correct numerical receipts, then
found allocation in the terminal lifetime path. Original Root validation now
checks the authentic descriptor registration lifetime before snapshot/prepared
ValidationResult diagnostics can allocate. This shared L2 prerequisite applies
to all four backends; no native execution scope is widened. The fixture verifies
the retired ID under exhausted original quotas. Host2/2 passes; browser rerun
remains pending, so the failed attempt is retained as failure evidence.

### Prior completed-content planning increment

Content9 explicitly selects bindlessCompletedReadContractVersion1, independent
read10/control1 and Pending6/Lifecycle12. Default read8/control3 is unchanged.
Read10 accepts authentic completed whole2D uploads; mixed planning retains the
same Root/inventory/graph/stream/state journal without proposing content again.
Null validates only, WebGPU permits CPU planning, Vulkan and Metal refuse this
independent canonical profile. General Content6 cannot borrow it; no sampled
recording, submission, completion, native closure or execution is granted.

The browser fixture authors CopyDst->ShaderRead dependencies and plans2 sampled
controls/reads plus1 attachment. It verifies CopyDst remains captured, result
unpublished and hot validation allocation0. Host2/2, Node1/1 and source415/415
pass; the full browser cohort remains pending. Chrome renderer OOM was confirmed
from exception0xe0000008; child-crash detection now fails promptly (9/9 tests),
and an explicit baseline-only Wasm correctness run avoided that observed OOM.
Browser8 passed88 Graphics flows but timed out at900sec after Compute scenario0
started; no complete success or final scope-drain proof follows. The compact
assertion-enabled O1 test fixture changes no backend/resource quota and provides
no default-compiler or Release performance evidence. The prior preparation-only browser attempt
passed its new real-upload marker but timed out at480sec in Compute scenario8;
its failed full cohort is retained, not a validated checkpoint.

## Public WebGPU completed texture publication2 (2026-10-15; scoped Backend Validated)

Independent CompletedTextureDescriptor2 consumes canonical whole-upload control1,
Completed state, nonzero generation/revision, authentic view/parent leases and
actual wrapper registry/native shape. Vulkan publication1/control3 remains
independent; Null is validator-only and Metal refuses. Ordinary access stays
closed. Candidate writes/resources/completed publications commit together only
after successful retained metadata creation; allocation failure publishes none.
This grants no state transition, physical bind group or sampled execution.

Real browser canonical upload independently reads20 bytes/2mips, then publishes
authentic completed metadata. Repeated exhausted-admission failures publish
nothing; stale/forged/mutated source, binding/schema/layout and duplicate proof
are refused. Exact/one-less/zero remaining bytes, hot0, source retention and final
retirement quotas0 pass. wasm32 libc++ needs an allocation-free short RHI-OOM
diagnostic; threshold measurement reuses the history-independent quota fixture.
The old two-argument validateSource exported symbol is retained.

Host2/2 (1.97sec), browser1/1 (343.67sec), Node1/1 (0.41sec), source413/413,
driver8/8 and WOCC15/15 pass. Cold3 cases, Graphics88/16 including finite44/8,
and Compute22/4 remain green; diagnostics/nativeErrors0. Evidence:
D:/arp/build/bindless-original-buffer-sources1/root-web-published2-current-report.json,
root-web-published2-browser-report.json and tested-source-sha256.json. This is
Backend Validated for 2D completed descriptor publication only. No sampled draw,
shader completion or application bindless grant; general executablePayload=false.
Next: same Root Content9, texture/sampler native closure and real sampling.

## Public WebGPU sampled Graphics cold preparation3 (2026-10-15; scoped Backend Validated)

Independent OriginalPreparation3 admits Fragment RGBA8 sampled 2D/2DArray,
independent non-comparison samplers and the existing scalar buffers. It retains
actual native views, parents, samplers, pipeline and physical group; exact
publication and generated mapping precede cold emission. Captured owners,
handles, shape and view subresources are compared during allocation-free
validation. Null/Vulkan/Metal reject this independent WebGPU profile; existing
Vulkan sampled execution remains separate. Storage/depth/cube/Vertex indexed
resources refuse. This grants no content, recording, submission, completion or
application Indexed choice; Required rejects and Auto chooses Traditional.

SPIR-V/MSL/WGSL and reflection use the existing Slang sampled/array fixtures.
Real browser3 cold cases pass: 2D/2DArray, independent samplers, actual registry
owners, shape/subresource mutation, exact/one-less quota, hot0 and retirement.
Prior Graphics88 flows/16 receipts, finite44/8 and Compute22/4 remain green.
Host2/2 (1.73sec), browser1/1 (451.40sec), Node1/1, source412/412, WOCC15/15.
Evidence: build/bindless-original-buffer-sources1/root-web-sampled3-current-report.json
and root-web-sampled3-browser-report.json. No sampled content/draw/completion
claim follows. General execution remains false; required sampled content/Root
closure/execution is next. Publication2 is a separate in-progress increment.

## Authenticated finite buffer range publication1 (2026-10-15; scoped Backend Validated)

The existing OriginalBufferRangeDescriptor1 typed intent now permits WebGPU CPU
descriptor publication against authentic original leases. Null remains validator,
Vulkan retains native publication and Metal explicitly refuses. Publication alone
grants no shader, command or completion authority. Generic finite writes and
ordinary native consumers remain closed.

Every finite write uses schema2 with queried device uniform/storage alignment and
maximum binding sizes; dynamic offsets and insufficient/out-of-bounds intervals
reject before publication. The backend copies a candidate write list and matching
original lease publications; only a successful retained-metadata factory commits
all lists. It verifies native payload ownership against the actual registry.
Subsequent whole metadata bindings cannot erase the authenticated finite views.

Public OriginalExecution2/Preparation2 consumes these independent typed range
publications through the same snapshot and Root: cold physical group creation
matches native metadata to the successful immutable L2 write, checks actual
pipeline minimum and queried limits, and emits the exact offset and size. A native
layout minimum0 is a deferred use check; the authenticated generated table ABI
provides its independent nonzero minimum. Neither value is invented. It does
not enlarge the view or change resource identity. The snapshot metadata records
the finite runtime element count, and canonical state/journal/completion uses the
same access interval and original owners. No new execution version or general
capability is inferred from this independent publication contract.

Real browser verification passes88 Graphics lifecycle flows and16 numerical
receipts:44 finite flows /8 receipts plus44 whole flows /8 receipts. Prior
Compute22 /4 remains green. Finite readback independently checks runtime length,
exact interval values and unchanged complete prefix/tail canaries; four mapped
images were inspected. Host7/7, browser1/1 (428.47sec), Node1/1, source411/411,
WOCC requirements15/15 pass. Evidence: `build/bindless-original-buffer-sources1/
root-web-ranges1-current-report.json`, browser report, images and logs.
Vulkan live2 capture/replay is inherited independent evidence, not a new capture
for this WebGPU increment. Generic finite consumers remain closed; required
texture/sampler/array/depth/storage, Vertex and indirect domains remain open.
Metal remains last; WOCC is Traditional and P0–P6 remains incomplete.

## Historical public WebGPU Fragment Graphics execution2 (2026-10-15; scoped Backend Validated)

OriginalExecution2 uses NativeClosure4, callback admission2, native program2,
Lifecycle12 and Content9 through the existing public BindlessCross Root view.
Real recording/queue/scopes/map/unmap completion gates the same journal, fault
Result and retirement. No extra graph, Result store or completion flag exists.
Fragment scalar whole buffers plus unindexed Vertex are the exact scope;
finite ranges, Vertex indexed fault provenance and other resource classes refuse.
General executablePayload stays false; WOCC remains Traditional.

ContentCreation5 is WebGPU logical-texel accounting, not driver allocation query;
native allocation budget0, Content9 mandatory, fresh attachments only. Vulkan
queried-allocation control4 remains independent; WebGPU refuses4. Descriptor
loss-registration3 authenticates concrete CPU arena/set ownership and required
native factory policy against registry9. It issues no physical execution grant.
Null/Vulkan/Metal reject this independent WebGPU Graphics execution profile;
Vulkan public indexed Graphics retains its separately validated contract.

Actual browser: 44 lifecycle flows and8 numerical receipts (buffer/material/
vegetation records; clean/fault; success/reverse); Compute22 and4 retained.
Same Result, failure nonpublication, hot0, callback draining and budget return
pass. Host7/7, browser/Node2/2, source410/410, WOCC15/15. Current Vulkan RDC replay
verifies8 geometry +6 buffer/record draws, validation0; captured inputs and
mapped images are independently inspected. Initial capture wrapper timed out90sec;
replay success does not replace a missing full application-exit report.
Evidence: `build/bindless-original-buffer-sources1/root-web-graphics-live2-*`.
Required scene domains → renderer/WOCC → GPU-driven → Metal last → acceptance
remain open. Full physical loss/shutdown matrix was not repeated.

## Historical WebGPU Fragment Graphics preparation2 (2026-10-15; scoped Backend Validated)

Independent queried Graphics preparation2 accepts Fragment scalar buffers and
an unindexed Vertex stage. Actual authorized render pipeline stages, physical
group, buffer owners and canonical Root inventory are validated. Compute1 remains
independent; finite buffer ranges explicitly refuse before physical emission.
One shared prepared-buffer algorithm serves both typed pipeline domains.
No record, submit, completion or application Indexed selection is granted by
preparation2. Vertex indexed fault provenance still rejects; general WebGPU
execution remains DiagnosticFailure/executablePayload=false and WOCC Traditional.
Four-backend cold decision: WebGPU preparation only; Null/Vulkan/Metal explicit
profile refusal. Existing public Vulkan execution retains its independent scope.

Real browser4 Graphics preparation flows (buffers/records × clean/fault) and
original Compute scenarios pass; final browser/Node2/2, source409/409, host7/7,
WOCC requirements15/15. Shared Slang SPIR-V/MSL/WGSL/reflection regenerate; actual
Vulkan capture passes16 public flows and independently replays8 geometry plus6
scalar-buffer draws with validation0. Midpoint RGBA8 quantization checks only the
two adjacent codes at authored0.5; other channels and draw fields remain exact.
Saved images were inspected. Emscripten LINK_DEPENDS prevents stale publications.
Evidence: `build/bindless-original-buffer-sources1/root-web-graphics2-current-report.json`
and `root-web-graphics2-*`. Public WebGPU Graphics execution, required resources/
operations, WOCC and final acceptance remain open; Metal is last.


## Public original Graphics adapter /1 (2026-10-14)

Independent explicit Graphics execution intent uses BindlessCross with typed
original Root/inventory/transaction/readback, initialized geometry sources and
CPU/native/pass/fetch budgets. Backend factories authenticate concrete geometry
actors and original owners. Vulkan cold preparation reserves the same native
command/fence/pass; public record applies the original plan and records it;
submit and nonblocking poll reuse the existing queue/fence. After original
custody drains, completion requires that same committed transaction and published
Result. No new graph/journal/completion flag/result store or submission pool.

Separate Vulkan capability requires actual indexing and Fragment side-effect
enablement. Null execution refuses, Metal/WebGPU explicitly reject this profile;
public WebGPU scalar buffer Compute remains independent. Actual public16 flows,
8 indexed clean/fault draws with repeated target/generation4 and hot0 pass.
RenderDoc independently verifies8 draws, actual geometry, exact pixels and target
identity, validation0; MinGW5/5, Web3/3, source408/408, WOCC requirements15/15.
Evidence: `build/bindless-original-buffer-sources1/root-public-graphics1-*`.
This is scoped Backend Validated, not complete scene/WOCC/Production readiness.
Next is WebGPU public Graphics and required scene operations; Metal stays last.


## Explicit repeated attachment clear /1 (2026-10-14)

An optional typed AttachmentClear1 intent admits genuine Completed control4
content with nonzero generation. Default0 remains fresh-only. Contradictory
state/generation, Proposed, foreign control, malformed version/opt-in and old
General6 all reject. Content9 and cold pass1 freeze identical intents. Existing
range inspectors, graph cells, journal and completion perform each new write;
no content reset or implicit generation reuse exists. Null validates, Vulkan
prepares this explicit original profile, Metal/WebGPU reject it. The policy
itself grants no standalone execution authority.

Actual vertex/instance × clean/fault cases each reuse the same target twice,
including cancellation rollback and completed generation4. Explicit WAW and
ShaderRead reuse retain the original state/seal/fence. Native8/8 and captured8/8
pass; independent RenderDoc verifies every draw, same target and exact pixels,
validation0. MinGW5/5 + final retention1/1, Web3/3, source407/407 pass. Evidence
prefix: `build/bindless-original-buffer-sources1/root-attachment-clear1-*`.
This predates the later public adapter implementation. WOCC remains pending.


## Private indexed GeometryExecution1 (2026-10-14)

RootState5, Closure3, initialized Fetch2, Content9/Geometry1, Lifecycle9 and
readback-binding3 join the existing original issuer. The typed geometry Pending
use5 authenticates the exact original seal, journal, registered buffer and cold
capture; public observations grant no Pending bypass. Immutable actors validate
the current genuine read-only observation after retirement; every new closure
freezes its own exact capture. Capture validity is checked against the original
controls, including rollback revisions, rather than assuming every read-only
completion changes revisions. No new graph, issuer or completion machine exists.

Cold fetch/range/capacity validation precedes native claim and fault custody.
Native vertex/index binds and DrawIndexed consume that exact stream. The issuer
rejects a missing bound index before the legacy helper can allocate scratch data.
Null validates declarations; Vulkan permits this explicit private execution;
Metal/WebGPU reject it. Ordinary profiles cannot borrow geometry fetch budgets.

Real vertex/instance attribute shaders use independent texture/sampler access
and coordinate gradients before table dispatch. Four clean/fault executions
validate cancel reuse, caller-drop custody, firstInstance1, genuine fence and
Result publication, quota retirement and fresh closure recapture of immutable
actors. RenderDoc independently verifies actual bytes/offsets/stride and exact
2×2 output, validation0. Source406/406, MinGW5/5 and browser/Node3/3 pass.

The public scene adapter, repeated writes to completed attachments and the full
scene/lifecycle/Release matrix remain open. Content9/pass1 still require fresh
attachment content; explicit repeat-clear semantics must precede steady-frame
use. The single current delivery index is `D:/wocc/docs/bindless-delivery-status.md`.

## Earlier initialized geometry custody and content composition (2026-10-14)

NativeClosure3 retains each authentic HostGeometryContent1 actor in the existing
canonical geometry entry. Every actor must match the exact inventory lease and
native payload; missing, foreign, forged and duplicate actors reject before
publication. Separate actor, frozen-byte and retained-owner ceilings apply.
Actor metadata/payload/resource quota owners are deduplicated; leases, native
payloads and runtime keep the original inventory/closure accounting. The caller
can drop every actor while the closure retains initialization. Destruction returns
its budgets, including independently charged weak-control-block storage.
No second inventory, graph, journal, command, callback or Result is introduced.

ContentPlan9/Geometry1 is an independent explicit composition intent. It admits
exact Inputs7/RootState5 and the same geometry footprint, graph and retained
Stream1. Completed sampled textures and native attachment control4 cells use
the existing dry/populate inspectors and quotas. Ordinary ContentPlan9 refuses
this profile; finite General ContentPlan6 cannot borrow it. Null is a declaration
validator, Vulkan permits cold composition, Metal/WebGPU refuse this profile.
All execution decisions remain false. Initialization and cold content composition
do not authorize Pending reads, seal publication or indexed native submission.

Next: join authentic actors through the existing seal's exact graph-cell/state
capture and Pending journal, then extend the original Vulkan issuer to indexed
and instance draw with actual Slang vertex-input use, fence/fault/readback and
failure/retirement evidence. Renderer/WOCC follows required Vulkan/WebGPU gates;
Metal remains last per user instruction.

## Earlier geometry cold composition (2026-10-14)

OriginalHostGeometryContent/1 provides typed, mutually exclusive index or
vertex/instance initialization intents. Complete backing bytes, memory/state,
stride/rate/count, byte order and independent CPU/native/owner budgets are
mandatory. Vulkan queries real buffer memory requirements before admission,
freezes the full payload under its own quota, performs a genuine mapped write
and native flush, seals host/GPU writes, and adopts the exact registered lease.
The source retains authentic native payload/runtime and original state capture.
There is no caller-issued initialized flag, callback receipt or GPU completion.
Null is declaration-only; Metal/WebGPU reject this specific mapped-write profile.
Their eventual geometry initialization must use their own real completion rules.

The same Root/Stream Fetch2 consumes these original Vulkan actors. Hot checks
authenticate shared owner, canonical lease, native identity, frozen content
intent and original state; index extrema and vertex/instance bounds reuse the
existing L2 inspectors. Frozen bytes survive caller mutation. Failed cold
admission publishes no Source and returns CPU/resource/registration charges.
The initializer does not authorize Pending reads or indexed command execution;
the existing content/lifecycle/journal issuer must still provide that join.

Inputs7 and RootState5 join GeometryInputAccess/1 and GeometryDrawConsumption/1
to the exact prepared Graphics Root. Positions retain the same inventory and
render-pass/pipeline epoch. `preparedGeneration` is the authentic snapshot
generation; the unrelated ordinary pipeline registration field stays zero.
Buffer registrations preserve their existing identities. The borrowed
BindlessGeometryDefinition/1 is frozen by the actual pipeline factory, with no
caller-authored substitute or new pipeline Source. Null validates; Vulkan
freezes creation layouts; Metal/WebGPU explicitly reject this new profile.
Older Root profiles refuse vertex-input pipelines.

Vertex/index bindings and draw consumption share the original geometry walker,
graph, sequence budgets and Stream1. Independent geometry ceilings are mandatory
and must match graph/transaction/stream. Binding footprints cover complete tails;
initialized index extrema and actual vertex fetch remain separate gates.
Indexed metadata alone grants no native execution.

OriginalIndexedDrawFetch/2 consumes exact Inputs7 and the factory-frozen prepared
Graphics pipeline. It borrows the original index-extrema analyzer and latest
geometry-binding walker; vertex streams validate baseVertex/index extrema while
instance streams validate firstInstance/instanceCount. Independent commands,
draws, streams, scans and analyzed-index limits apply before caller output.
The initialized overload requires both index and vertex custody adapters;
range-only `visit` refuses /2. Stream1/RootState5 checks the same operation
correspondence, without another graph or layout. Ordinary Inputs5/Fetch1 and
GeometryContentCustody1 remain isolated. Null/Vulkan allow CPU projection only;
Metal/WebGPU reject the missing geometry definition. CPU analyzer fixtures do
not establish native initialization, Pending custody or indexed execution.

NativeClosure2 adds genuine canonical geometry buffer payloads to the same
Vulkan closure, with existing owner deduplication, registration/device/payload
correspondence and cold quota. Failed admission preserves original resources.
Null is validator-only; Metal/WebGPU refuse the closure profile. Existing
Lifecycle9/ContentPlan9 issuers explicitly reject it. No second graph, journal,
callback pool, command issuer or Result is introduced. Next: initialized fetch,
Graphics content and the existing native issuer join, followed by actual
indexed/instance/indirect numerical and completion evidence.

## Current OriginalExecution /1 and public L2 adapter (2026-10-14)

Lifecycle11 consumes original Stream4 through existing WebGPU Recording/Submission
and canonical Root pool. Same authentic snapshot, authorization, prepared binding,
closure, identities, graph, seal, journal and Result. Genuine queue/scopes/map/unmap
drain precedes commit/publication; failure never publishes. Preparation1/Lifecycle10
remain nonexecuting. No second graph, callback pool or result store.

Real browser validates11 scenarios times clean/fault,22 groups, counters14/16,
fault0/1, hot0 and budget return. Node+Root2/2 and affected MinGW6/6 pass;
source399/399. Logical lifetime retirement is included; physical loss/shutdown
and full workload acceptance remain open. Evidence `webgpu-root-live-report22.json`
and `webgpu-root-live-*-final22.log` under `build/bindless-original-buffer-sources1/`.

BindlessCross::prepareOriginalRootExecution accepts typed public original inputs
and cold budgets, verifies the actual device issuer/capability/executable profile
and returns an OriginalBindlessRootExecution view of the existing quota-bound
issuer. No wrapper allocation or native handle is exposed; record/submit refuse
forged shared owners. All22 groups now pass through the public adapter: rebuilt
Node+Root2/2 (9.72 sec), MinGW6/6 (21.11 sec), source400/400. Version/profile,
foreign issuer, capacity and forged-owner record/submit rejection pass before
mutation. `webgpu-root-public-report.json` records the scoped public capability;
general scene capability remains false. Null/Vulkan/Metal refuse this profile.
Rebuilt generic WebGPU browser1/1 passes (67.70 sec) after fixing its obsolete
faultReadback0 assertion; the prior failure log is retained. Evidence
`webgpu-root-public-generic-tests.log`.
Final rebuilt Vulkan GPU plus device contract2/2 pass (152.97 sec),
`webgpu-root-public-vulkan-tests.log`, preserving existing native execution.

Queried devices publish independent originalExecution1/faultReadback1 for scalar
buffer-only Compute Prototype. General scene execution stays DiagnosticFailure/
executablePayload=false; Graphics, textures, indirect and full WOCC buffer ABI
remain separate gates. The preparation checkpoints below are historical.

## OriginalRootProgram /1 and ColdCommand /2 (2026-10-14)

The original command shell embeds bounded value records for the original
Stream4 operations. Handles are borrowed from its strongly retained authentic
Root closure and prepared binding; no second owner graph, journal, callback pool
or CPU replay stream is created. The L2 request requires independent typed
version1/opt-in, command and operation-visit ceilings. ColdCommand/1 refuses this
profile, while /2 requires it. Null/Vulkan/Metal explicitly reject preparation;
WebGPU permits preparation and grants no execution or completion.

WebGPU freezes clear/bind/dispatch/copy fields and native identities, checking
the original graph's ranges, root references and readback binding. Every
operation/reference/native resource lookup is charged against the visit ceiling.
Actual device maxComputeWorkgroupsPerDimension is queried before claim and
retained for allocation-free revalidation; no assumed dispatch limit is used.
CommandRecordingOrder supplies structural scopes. Transfer brackets use native
encoder-copy scope; compute/copy usages remain distinct and barriers outside
both pass scopes. Native encoders and all execution callbacks stay unopened.

Real browser clean /1 and fault /2 preparation pass, including budget/dispatch
overflow, quota retry, immutable correspondence, hot allocation0, original pool
retention and cancellation retirement. The /2 program contains13 commands and86
visits, with device limit65535 and shell metadata2400 bytes. Node plus Root
browser2/2 pass (4.75 sec). Intent/refusal is Contract Complete; genuine native
preparation is Backend Validated only. Actual recording, queue/fault/map
completion and same-seal commit/publication remain open. Evidence prefix:
`build/bindless-original-buffer-sources1/webgpu-root-native-program-*`.
Final affected MinGW6/6 pass (23.24 sec), with Vulkan device contract,
source399/399 and style5 internal cases.
Rebuilt generic WebGPU browser1/1 also passes (65.76 sec). Broader Vulkan GPU
regression has three negative-process90-second timeouts, including an isolated
run. A direct negative probe then completed every assertion with exit0. The
expanded positive-plus-terminal negative process now gets a bounded180s;
normal/partial remain90s and outer CTest240s. Final rebuilt Vulkan GPU1/1 passes
(180.70 sec) with uncertain-submit and same-runtime-idle cases. All original
failures are preserved; no WebGPU Root execution or scene capability is granted.

## OriginalExecution /1 intent and refusal contract (2026-10-14)

Execution intent is independent from OriginalPreparation/1: typed opt-in,
capability version, plan field, identity and telemetry must all agree. Both
profiles together fail closed. The new scope is scalar buffer-only Compute,
snapshot metadata1 and fault latch/reporting1, without extra/reserved groups,
dynamic offsets or indirect ABI. Queried limits, physical and table shape,
operation/nonuniform support, buffer ABI and complete budgets remain checked.
Fault completion capability is mandatory even when general Emulated execution
is closed. A prospective scoped device capability does not widen that general
surface. Original descriptors and retained fault readback are required.

Snapshot/backend issuance, authorization plan reproduction and prepared-owner
equality include the profile. Real clean/fault browser tests deliberately keep
the digest while altering the profile and confirm authorization and prepared
bindings refuse it; restoring the profile preserves the original preparation.
Required refuses absent capability; Auto selects Traditional with the reason.
Synthetic planner tests exercise prospective capability inputs only.

Contract Complete for intent/refusal only. No backend publishes this capability.
Null/Vulkan/Metal explicitly refuse; WebGPU reports the missing original live
issuer. OriginalPreparation1 and Lifecycle10 remain nonexecuting. The original
Stream4 recording, genuine fault/Pending/map/unmap, same-seal commit and result
publication remain open. Final scalar profile checks pass affected MinGW6/6
(22.26 sec), Node plus Root browser2/2 (4.21 sec; browser3.99 sec), source397/397
and style5 cases. Evidence prefix
`build/bindless-original-buffer-sources1/webgpu-root-execution-profile-*`.
Final rebuilt Vulkan GPU1/1 (183.41 sec) and generic browser1/1 (65.02 sec)
also pass, preserving existing execution without authorizing Root execution.

## RootIssuer /1 (2026-10-14)

The authentic Root callback pool now retains the existing ordinary Recording and
Submission issuers. Their weak association resolves only as Root RuntimeState;
legacy/typed casts are prohibited. Recording metadata charges the original
Recording quota; Submission plus facade charge Submission. Existing native
receipt adapters cold-prepare recording scopes3, submission scopes3 and queue1
against the original command device/queue and slot addresses. All fallible
preparation precedes one-shot publication. Failure permits retry without changing
cold custody; facade drop leaves issuers retained until original pool retirement.

Typed and Root delivery reuse one implementation for authenticated receipt,
ticket/generation, gate, evidence and foreign-owner draining. No callbacks are
issued during preparation. Root ordinary record/submit and completion are closed.
Owner ceiling108, Recording316/Submission288 bytes, no extra callback/result
allocation, three-domain quota equality, exhausted-domain retry, owner/capacity/
version/opt-in rejection, forged facade, duplicate claim, hot0 and full return are
real clean/fault browser validated. Affected host5/5, Node bindless1/1, guard396/396,
style5 internal cases, Root browser1/1 (3.98 sec) and rebuilt generic browser1/1
(70.17 sec) pass. Generic queue/scope/map execution validates the shared delivery
refactor without granting Root execution.
Evidence prefix `build/bindless-original-buffer-sources1/webgpu-root-issuers-*`.

Null/Vulkan/Metal refuse the explicit native issuer preparation profile. WebGPU
offers preparation only. Lifecycle10 execution/completion remain closed on every
backend; OriginalPreparation1 remains nonexecuting. Next requires a separate
executable plan/authorization, original stream native consumption, move-only
fault custody and genuine Pending/map/fault completion/publication.

## Root ResultStorage /3, ColdCommand /1 and CallbackCustody /1 (2026-10-14)

The original buffer Result and native buffer are retained by the existing
canonical result storage. Result quota live bytes must not increase. A genuine
shell freezes device/queue/recording identity and permanently claims the original
Lifecycle10 seal after every fallible allocation and validation. No encoder opens.
The canonical RuntimeState/retention pool/gates/native loss/map adapters retain
this Root command with twelve slots. The native loss subscription is real;
cancellation proves suppression before the graph or userdata owners are released.
Cold map receipt and its scopes are prepared against the same original buffer.
No map starts, no queue/scope work is issued, no journal applies or result publishes.

Separate versioned requests reuse canonical count/owner derivation. Recording,
submission and callback quota limits and independent domains must match the
original lifecycle; Source capacities are not reserved a second time. Owner
ceilings include storage88, command90 and custody105. Real clean/fault browser
checks pass original result/native correspondence, quota failure/retry, owner
limits, forged-owner rejection, one-shot claims, caller-drop retention, hot0,
native loss arm/suppression and full budget return. Recording metadata is
180/760 bytes; custody submission492/callback960 bytes in this Wasm build.

Current MinGW affected5/5, Node bindless1/1, source395/395, style5 internal cases
and Root browser1/1 (3.43 sec) pass. Rebuilt generic execution browser1/1 passes
(70.76 sec), confirming existing queue/scope/map behavior without granting Root
execution. Evidence prefix
`build/bindless-original-buffer-sources1/webgpu-root-custody-*`.
Null/Vulkan/Metal refuse these native cold profiles. WebGPU offers preparation
and loss custody only. Lifecycle10 issuers/execution/completion remain closed;
OriginalPreparation1 remains nonexecuting and public bindless execution=false.
Next: original issuers, genuine Pending/map/fault custody and a distinct
capability/plan/authorization-consistent executable profile.

## RootCallbackCold /1 and Lifecycle /10 (2026-10-14)

The WebGPU cold admission retains the same authentic Root native closure,
canonical buffer graph/Stream4, original buffer request/result, recording seal
and typed Source lifecycle reservation. Lifecycle10 accepts only ContentPlan0,
State1/RootState4/Sequence1 and independent recording/submission/callback quotas.
CapacityRequest6 is isolated from legacy callbacks and Vulkan fence requests.
It requires callback capacity; device-idle completion is unavailable.

Shared canonical slot layout derives recording scopes3 + submission scopes3 +
queue1 + loss1. The existing ReadbackMap1 contract adds one map receipt and three
map scopes, for twelve tickets and registrations. Source-authored typed budgets
are reserved before any recording and returned without creating another budget
pool. Existing immutable buffer result storage and exact sequence comparison are
reused. No native callback is issued, no journal applied and no result published.

Real-browser cleanMain/faultMain cold admission validates owners ceiling84,
recording CPU peak460 bytes, exact/one-less command/visit/owner/map/result limits,
allocation pressure, forged owner/quota-domain rejection, hot allocation0 and
zero final reservations. Current MinGW affected checks5/5, rebuilt Node1/1,
source guard392/392, style5/5, Root browser1/1 and rebuilt generic browser1/1
(64.27 sec) pass. Generic execution validates shared callback policy and existing
queue/scope/map behavior only, not Root execution. Evidence:
`build/bindless-original-buffer-sources1/webgpu-root-callback-cold-*` and the
original preparation browser report. This is scoped Backend Validated cold
correspondence; public execution remains false.

Null: L2 validator, native cold profile refused. Vulkan: callback cold profile
refused, existing fence profiles unchanged. Metal: native cold profile refused,
deferred. WebGPU: cold preparation only. Lifecycle10 issuer/execution remains
closed on all four. Next is the canonical native command/pool and actual
buffer-map/result custody; execution requires its own explicit executable
profile, never dispatching an OriginalPreparation1 nonexecuting plan.

## WebGPU original preparation /1 (2026-10-14)

Explicit OriginalPreparation/1 accepts only Emulated buffer-only Compute with
the existing snapshot metadata/fault ABI. It validates actual queried limits,
operation support, physical bindings and budget before preparation. The plan
is successful but nonexecutable; ordinary snapshot prepare, ordinary recording
and Required selection refuse it. No application mode is selected by this plan.

The authorized actual Slang pipeline signs its binding mapping. Logical
descriptor payloads remain logical; the shared native physical-group builder
creates an independent real bind-group from the issued mapping. Prepared
binding freezes pipeline/group/native buffer owners and original writes; native
buffer size is observed cold only. Root closure strongly retains the same
canonical inventory, original snapshot/authorization/prepared binding and all
registered buffer owners, including the independent fault readback. No graph,
journal, recording or completion is synthesized.

| Backend | This preparation profile | Execution |
| --- | --- | --- |
| Null | DiagnosticFailure: NULL-PROFILE; existing Validator path unchanged | Closed |
| Vulkan | DiagnosticFailure: VULKAN-PROFILE; existing native issuers unchanged | No new authority |
| Metal | DiagnosticFailure: METAL-UNAVAILABLE | Deferred |
| WebGPU | Equivalent cold payload correspondence; real browser Backend Validated | Closed |

Real cleanMain/faultMain preparation passes entries9/native12/retained51, CPU
peak556 bytes, exact/one-less capacities, fake shared-owner and mutated native
payload rejection, hot allocation0, caller release, facade destruction and
zero-budget retirement. Three genuine error scopes drain without native errors.
No shader dispatch/fault/queue/map completion is claimed. The compiler now
preserves emitted atomic calls across helper cloning; unsupported/missing
operations and unsupported writes fail before rewriting an artifact.

Current host24/24, Node17/17, source390/390, style5/5, full real codegen1/1
(29 internal cases), Root browser1/1 and generic browser1/1 pass. Logs use
`webgpu-root-preparation-*` under `D:/arp/build/bindless-original-buffer-sources1/`;
fresh scoped report is
`D:/arp/build/web-webgpu-release/artifacts/bindless-original-root-preparation/report.json`.
Next: join the original Root to canonical typed recording and original buffer
fault/result map custody, using the same stream/seal/state journal and genuine
queue/error/loss/map drain before publication or retirement.

## VectorRecord /1 declaration increment (2026-10-14)

`vectorRecordContractVersion: 1` explicitly adds authored nonempty records of
float4, uint4 or int4 fields to uniform/readonly/writable buffer tables. Each
field occupies 16 bytes at its declared offset; stride and minimum binding
size are 16 times field count. Layout identity hashes ordered names and types.
No matrix, narrow vector, nested/array member or record-member atomic operation
is accepted. The earlier raw uint32 atomic ABI remains unchanged.

Compiler generation uses Slang only. Publication proves actual SPIR-V member
offsets, scalar signedness, vectors and runtime-array stride with existing
layout facts. Runtime independently parses actual SPIR-V and, for Emulated,
WGSL with existing cold declaration graphs; target group/binding and uniform/
readonly/readwrite storage must match reflection. Rehashing a forged manifest
or target cannot grant layout authority. This is a declaration proof, not a
resource registration, state/completion or execution issuer.

The authored workload uses material32, the real `pcg::InstanceDataGpu`64 and
output48, all with two nonuniform resource indices. Vulkan validation passes
through the existing original Root/cold pass/stream/journal/fence. Inputs remain
byte-identical and both96-byte writable outputs are exact after completion;
table/element==length failures only latch faults. Cancellation does not publish.
Ordinary public authorization and complete production ABI remain closed.

| Backend | Declaration lowering | Execution |
| --- | --- | --- |
| Null | Native/Emulated typed package validation | ValidatorOnly |
| Vulkan | Slang SPIR-V with original snapshot range validation | Existing explicit GraphicsRootFence9; scoped real-driver and independent replay proof |
| Metal | Slang MSL and exact reflected record contract | Original Graphics issuer unavailable; platform deferred |
| WebGPU | Slang scalar/switch WGSL, independently proven record shape/storage | Original Graphics issuer unavailable; browser gate open |

Current affected host24/24, Node16/16, guard389/389, style5/5 and GPU1/1 pass.
Full real-compiler/runtime codegen registration PASS1/1 (123.43 sec), including
binary type/offset/stride substitution, WGSL signedness and readonly-storage substitution
after rehashing artifacts/metadata. Fresh RenderDoc draws754/786 match exact
SPIR-V hashes, outputs and fault copies757/789; validation empty and PNG checked.
Evidence `root-graphics-record-capture/current/` and `root-graphics-record-current/report.json`
under `D:/arp/build/bindless-original-buffer-sources1/`; the latter keeps full
Root=false, scoped Graphics/records/terminal=true and stderr empty.

## Root Source ownership

### Original buffer range descriptor /1

An explicit cold request1 admits only DescriptorBufferBindingRange2 static
views on authentic registered uniform/readonly/writable buffer leases. Bounds,
minimum binding size, target element stride/alignment and actual queried device
offset alignment/maximum binding sizes must all pass before publication.
Dynamic offsets and ordinary finite writes remain closed. Snapshot captures the
exact bound view and strongly retains its original lease; metadata counts derive
from view size/stride, not allocation size. Padding aliases retain the same
original view/source. Root uses the same inventory/state cells/stream/journal;
the full allocation remains independently budgeted and owned.

| Backend | Publication lowering | Execution |
| --- | --- | --- |
| Null | Equivalent validator publication; explicit alignment profile16 | ValidatorOnly |
| Vulkan | Native VkDescriptorBufferInfo offset/range from validated static view; actual device limits | Scoped GraphicsRootFence9 Backend Validated; no new issuer |
| Metal | DiagnosticFailure, original range issuer missing; alignment unknown | Deferred |
| WebGPU | DiagnosticFailure, public original range issuer missing; private scalar finite issuers do not grant this path | Closed |

Failure leaves revision/update count/access declarations unchanged. No shader,
registration or native command capability is granted by a published view.

Current host24/24, Emscripten Release/Node17/17, guard389/389 and style5/5 pass.
The partial logical-table fixture checks missing mutable-device rejection,
physical padding aliases, same-owner closure, deduplicated state observations,
view-relative bounds, cancellation and quota return. Native/Emulated manifests
remain independently type/layout validated; Node does not grant browser execution.
The full registered Vulkan matrix passes1/1 in160.73sec, including positive,
partial, terminal and earlier upload/Graphics regressions. The partial invocation
executes this full matrix too: measured38.22sec, so it uses the same bounded90sec
deadline as the other full invocations, within the registered240sec wrapper.

Fresh independent RenderDoc replay verifies six captured finite descriptors:
two material views offset64/size32/allocation112; two instance views
offset16/size128/allocation160; two output views offset16/size96/allocation128.
Offsets come from the actual queried Vulkan alignment, not portable constants.
Draw819/851 use exact clean/fault SPIR-V hashes; fault copies822/854 read0/1.
All inputs and both output records match byte-for-byte; all prefix/suffix bytes
remain0xcd. Whole-view regression draws755/787 still pass, with VK_WHOLE_SIZE
resolved against the same captured allocation. Validation messages are empty;
the agent inspected `record-range-pixels-preview.png`. Evidence under
`D:/arp/build/bindless-original-buffer-sources1/root-buffer-range-capture/current/`;
`root-buffer-range-current/report.json` preserves ranges/records/Graphics/
terminal=true, fullRoot=false and stderr empty. Dynamic offsets, full workload
ABI, public WebGPU and WOCC remain open.

The Source retains an authentic sealed BindlessSnapshot, its exact issued
authorization and prepared Compute/Graphics binding. Its descriptor storage
remains owned by the original snapshot, not an aliased/reconstructed wrapper.
Every physical descriptor element, including metadata, fault and padding, joins
an exact typed Buffer/View/Sampler owner in the same canonical inventory. View
parents and descriptor/arena registrations remain original device authority.
Root /1 validates finite sampler arrays without treating them as scalar General
descriptor footprints. Comparison mode and nonmemory stage/access requirements
remain exact; ordinary General source profiles retain their scalar restriction.
An explicit SnapshotOriginalDescriptorRequest /1 selects the existing original
device factories at creation. Ordinary snapshots cannot acquire registration
by post-hoc adoption. The original arena is shared with its authentic device
registration; its immutable set remains uniquely owned by the snapshot.

Preparation is cold, explicitly opted in and bounded by authored element,
owner and CPU metadata ceilings. Captured dependencies retain the original
wrappers; the inventory reference is weak to avoid root/inventory cycles.
Content revision, snapshot generation, authorization identity, prepared owners
and registration lifetime are revalidated. No second graph or journal exists.
Prepared bindings carry a weak original factory identity issued only after the
existing preparation pool validates the backend payload. Caller subclasses
cannot confer this identity by overriding virtual validation with success.

Canonical Source assembly /6 adds BindlessRoot /1 to the existing /5 descriptor
and pipeline assembly. Earlier assembly versions reject the new domain. Common
CPU ownership accounting deduplicates capacity leases/budgets and prepared
owners through the existing identity projection.
The same Source implements BindlessAccessDeclarations by borrowing exact sealed
snapshot access entries. Samplers contribute custody but no memory access;
an existing fault-readback child contributes its actual copy range. This is
an immutable declaration view, not a second graph or an execution/state proof.

| Backend | Source custody | Native execution |
| --- | --- | --- |
| Null | Equivalent typed CPU preparation and validator | Closed |
| Vulkan | Equivalent typed CPU preparation; real prepared binding factory is separately GPU validated | Closed pending public state/payload join |
| Metal | Equivalent typed CPU preparation | Closed pending public state/payload join and Apple validation |
| WebGPU | Equivalent typed CPU preparation | Closed pending public state/payload/completion join |

Source preparation is not shader footprint, initialized contents, native
recording, submit, completion, fault readback or retirement proof. A canonical
inventory containing a root still reports
`RHI-ORIGINAL-BINDLESS-ROOT-STATE-JOIN-MISSING` to legacy state paths without
the independent RootState opt-in below. Public capabilities cannot be enabled
by Source preparation.

## RootState /1

An independent, explicit CPU state opt-in joins Root /1 retention declarations
to OriginalCommandResourceStates /1. It uses the same canonical inventory,
original committed Buffer controls and exact Texture parent subresource cells.
Independent root/access/source-visit ceilings bound projection before capture;
failed admission leaves committed state unchanged. Samplers remain state-free.
Earlier callers without this opt-in still reject Root. Shader consumption,
fault clear/readback commands, native recording and completion remain closed.
All four backends provide Equivalent CPU projection (Null validator only), with
executionAvailable=false. This is a state prerequisite, not public GPU evidence.

RootStateSequence /1 independently opts the same graph into the existing
OriginalCommandStateSequence /1. It charges actual buffer contributions and
exact texture cells, validates ranges, current states, stages and usage, and
rejects fault/shader commands. Its plan materializes in the original transaction
and MutationJournal. Transaction admission must match the exact authored root
footprint. Pending enlistment retains every original control. Cancellation
rolls back and invalidates old committed observations; callers must recapture
the graph. Neither CPU plan application nor cancellation is GPU completion.

## Prepared command and RootState /2

BindPreparedComputeBinding and BindPreparedGraphicsBinding each reference one
authentic Root /1. Their scalar payload is empty: the exact immutable prepared
pipeline, authorization, snapshot, generation and complete descriptor closure
already belong to that Source. Binding is allowed only in its corresponding
pass and establishes the active pipeline and descriptor together. It cannot
borrow an ordinary pipeline or independently supplied descriptor.

The explicitly selected RootState /2 projection records shader accesses at each
actual dispatch/draw, with the selected shader stages and exact original buffer
ranges and texture parent cells. Retention alone contributes no shader access
in /2. Each pass resets the active Root; changing an ordinary pipeline clears
the prepared binding. Invocations without a matching active Root reject.
Fault readback destinations are not shader accesses. Fault clear/copy and
indirect/geometry consumption still require their own exact command joins.
The current /2 domain projects direct dispatch and non-indexed draw accesses;
it explicitly rejects vertex/index consumption and indirect commands. This
shader-state projection does not prove a draw's vertex layout, attachment/pipeline
compatibility, viewport/scissor, initialized contents or native execution.

The independent Stream Root /2 opt-in extends the canonical Stream /1 program.
It requires that exact RootState /2 graph and sequence footprint, counts prepared
pipeline binds and actual shader invocations separately, and attaches implicit
access ordinals to consuming commands. Earlier Stream /1 callers cannot borrow
the opt-in. Smaller invocation/access ceilings or a mismatched root footprint
reject before operation publication. No second resource/state/journal is created.

All four backends share Equivalent CPU command/state projection. Null remains
validator-only. Native recording, queue completion and public capability gates
remain closed until that same complete program is lowered and verified.

## Snapshot fault clear and RootState /3

RootState /3 and the matching independent Stream Root /3 extend the /2 CPU
projection with `ClearSnapshotFault`. The command projects exactly one four-byte
TransferWrite/CopyDst access to the original snapshot fault-latch lease. It checks
the canonical lease's shared owner, not only its resource ID. The independent
`maxFaultClears` ceiling bounds clear commands; older profiles reject this field
and operation. Shader stages are empty at clear; later shader invocations retain
their own stage-filtered access declarations.

The existing sequence must contain a valid transition to CopyDst before clear
and a transition back to the shader's declared state before invocation. That same
plan/journal retains the original control through pending ownership, cancellation
invalidates captured observations, and all reservations return after retirement.
Stream implicit access ordinals include the clear while shader-consumption counts
remain shader-only. This does not create a separate state graph or fault buffer.

All four backend lowerings provide CPU projection with execution closed. Buffer
fault clear, special fault readback/map/result publication, exact graphics and
geometry/indirect joins, native recording/submission and real completion remain
separate pending gates. Clearing a CPU state plan is not GPU fault validation.

## Snapshot fault copy and RootState /4

OriginalDescriptors /2 explicitly retains a fault readback child at snapshot
creation. The child uses the existing four-byte GpuToCpu/CopyDst recipe and the
original registration, lease and snapshot capacity owners. The artifact's budget
must cover both latch and readback before creation. /1 rejects the new field;
/2 without the explicit retention opt-in rejects. Retention grants no fault
readback capability, map authority or result publication, including on Null.

RootState /4 and its matching Stream Root /4 authenticate the existing explicit
BufferCopy from the snapshot's original latch to its original readback lease.
Both operands must have the exact canonical shared owners, zero offsets and
four-byte extent. Reversed, foreign or ambiguous snapshot associations reject.
All BufferCopy commands in this finite profile must meet this association; this
profile does not authorize unrelated buffer transfers. The separate
`maxFaultCopies` ceiling and bounded source scans are preflighted; earlier
profiles reject the new field.

The existing BufferCopy operation ordinal and its two explicit transfer accesses
remain authoritative. The join adds no implicit access, shader consumption,
resource graph or journal. The existing sequence validates CopySrc/CopyDst and
explicit transitions; the existing transaction retains the same buffer controls.
The four backend lowerings still publish CPU projection only, with execution
closed. Native copy execution, independently typed buffer request/result,
submission-exclusive fault custody, actual completion/map/error drain and result
publication remain pending. CPU plan cancellation is not GPU completion.

## Genuine buffer request/result association

`BufferReadbackRequestDeclaration /1` freezes the original buffer ID, actual
registration generation, complete BufferResourceLayout, authored byte range and
opaque request/frame/snapshot-generation correlation from a real registered
OriginalCommandBufferLease. The device and independent Request quota are checked
before allocation. The request itself retains no GPU object. It requires GpuToCpu
and CopyDst, an eight-byte-aligned offset and a four-byte-aligned nonzero size,
using the portable WebGPU map range requirements. Overflow-safe containment and
the caller's byte ceiling reject invalid ranges. These are CPU declaration facts,
not initialized data, native map or completion evidence.

The existing immutable result implementation is shared by texture and buffer
requests. The original texture name/interface remains an alias of its typed
specialization. Both specializations allocate cold, unpublished PMR bytes under
an independent Result quota, require the exact authentic request owner and expose
no public writer. Neither specialization retains GPU resources; buffer metadata
can survive snapshot/device retirement without retaining resource budgets. The
existing backend writer still accepts only its authenticated texture admission;
generalizing storage grants no buffer publication authority.

`OriginalCommandBufferReadbackBinding /1` strongly retains the same original
RootState /4 graph/program and that exact buffer request/result. It validates the
original copy operation, canonical latch/readback shared owners, both registration
and snapshot-generation correspondence, the exact copied range, and one write to
that readback child. Operation visits and immediate retained owners are independently
bounded. It is a cold association, not a second graph, submission seal or issuer.
Retained-pending correspondence is added by /2 below; native callback custody
remains a separate integration gate.

`OriginalCommandBufferReadbackResultContract` projects those authentic bindings
into the existing OriginalCommandReadbackResultContract requirements. It verifies
complete BufferCopy coverage in the original program, one shared graph/program and
Result quota, unique copy/request identities using the existing core projection,
and bounded visits/bytes. Any rejection clears all reported requirements. The
existing Map contract then derives its map/error-scope/ticket/registration ceilings
from the original inventory budget. The existing Drain contract keeps results
unpublished on pending/error/cancellation paths; no new callback protocol is added.
The four backend lowerings remain CPU-only (Null validator); actual native map,
error/queue/loss callback evidence, commit, publication and retirement are pending.

## Original pending readback and exclusive fault reservation

`OriginalCommandBufferReadbackBinding /2` adds CPU inspection of the same authentic
Pending transaction. It checks the exact graph shared owner, every frozen state
sequence budget field and the original journal's pending controls. The program
and copy/request/result correspondence were frozen by the original cold factory;
they are rechecked without applying cold committed-state validation to Pending
cells. Public cold validation remains strict. No additional transaction friend,
mutable plan accessor, graph, journal or state bypass is introduced. The existing
transaction exposes only its immutable authored sequence budget.

Before recording, /2 can acquire the snapshot's existing single fault writer
reservation. Its independently allocated `AtomicCapacityBudget` is created cold
through core, exposed as a const typed owner and projected by the existing Root
inventory CPU identity pass. Multiple Roots share and deduplicate that owner.
The reservation holds the original dynamic snapshot, is move-only, and returns
capacity exactly once on release. It grants no command, map or callback authority.

Successful original acquisition permanently selects journal ownership for that
fault domain. Subsequent legacy acquisition fails deterministically, including
a waiter racing mode selection; a refused in-flight acquisition cannot select
the mode. Original leases never update the legacy prior-state field, even if
the old completion method is called. State publication remains solely with the
canonical transaction and its existing backend-private native issuer.

Pending inspection additionally requires that exact original journal lease.
Wrong transaction owners, a different graph/sequence, /1, absent/foreign/legacy
custody and abandoned/invalidated roots reject. A valid inspection publishes no
bytes and proves no native completion. Null/Vulkan/Metal/WebGPU lowerings all
report CPU correspondence only; actual issuer integration must retain the binding,
seal and exclusive lease until the existing queue/map/error/loss callbacks drain.

## Vulkan prepared payload prerequisite

PreparedComputeBinding /1 and PreparedGraphicsBinding /1 now have real Vulkan
factories. The closed backend objects retain the authentic owner bundle and
freeze actual native pipeline, pipeline layout, descriptor set, device and shared
payload identities. All are rechecked before recording; native pipeline and
descriptor binds reject a foreign device. Graphics stages and topology come from
the validated selected pipeline entries/state, rather than all bindings in a
package. Frontend pool issuance remains the only original factory-owner receipt.

The public Vulkan pass checks that receipt, exact active pipeline, stage/access
coverage and native payload before preflighting a cold-reserved
core::BoundedRetainer. Compute and Graphics share maxBindlessPipelines as their
prepared-owner ceiling. Successful records retain the actual binding, including
count/byte leases, through finished/submitted command ownership. The existing
actual fence observation releases those owners; unsubmitted abandonment releases
them without pretending GPU completion. No new journal or callback mechanism is
introduced. The old snapshot binding and legacy fault preamble/readback path are
reused for this prerequisite only. They do not consume RootState /4 or its original
journal lease, so canonical Root execution remains closed. Public graphics pass
fault-scope preparation also remains version 0.

Current rebuilt MinGW Debug evidence under
`D:/arp/build/bindless-original-buffer-sources1/`:

- `vulkan-prepared-gpu-tests-depth.log`: registered real GPU smoke PASS,
  full and partially populated snapshots, compute numerical/fault readback,
  graphics pixel readback, both prepared-owner fence retirement and unsubmitted
  abandonment/reacquisition. Rejections include wrong generation, an unissued
  native object, incomplete accesses, pool exhaustion and command exhaustion.
  The same package also executes a selected Vertex-only public prepared depth
  pipeline, with Fragment access removed, clean fault, all four D32 depths equal
  to the shader's expected zero and completion-gated binding quota return.
- `vulkan-prepared-baseline-final.log`: 12/12 PASS, including source guard 365/365,
  package/layout/root inventories, Native/Emulated Null contracts, hidden frame
  and rebuilt Vulkan synthetic-device refusal matrix.
- `vulkan-prepared-depth-capture/report.json`: current full/partial GPU runs PASS
  with all four prepared lifecycle/depth indicators true and canonicalRootExecution=false.
- `vulkan-prepared-capture/public-bindless-vulkan_capture.rdc` and
  `public-bindless-renderdoc-report.json`: actual replay PASS, current fragment
  SPIR-V hash matches, direct draw event 88 and indexed-indirect event 117 produce
  independent nonclear targets with matching pixel bytes; validation messages
  are empty. PNG outputs were inspected by the agent.
- `vulkan-prepared-depth-capture/public-bindless-vulkan_capture.rdc` and its
  `public-bindless-renderdoc-report.json` are the latest capture/replay. In addition
  to direct/indirect fragment hash/output validation, depth event 115 has the exact
  current Vertex SPIR-V hash, no Fragment stage and four zero D32 values. Replay
  passes without validation messages; the depth PNG was inspected by the agent.

This grants Backend Validated for this prepared-payload prerequisite only.
Generic Vulkan retention and graphics pass preparation still contain preexisting
dynamic allocation; complete hot-path, loss/drain, canonical state publication,
scene operation coverage and other backend execution gates remain open.

## Vulkan Root native correspondence prerequisite

BindlessRootNativeClosure /1 now has a closed Vulkan factory over the authentic
Root /1 and the same original canonical inventory. The factory obtains its
runtime from the actual device extension; caller handles, payload owners or
native device values cannot issue a closure. A Root absent from that inventory,
foreign logical registration, forged shared owner, synthetic runtime, missing
native payload or quota-domain alias rejects. It retains no second graph,
state owner, journal or transaction.

The immutable closure freezes actual pipeline/layout/set/device identities and
each physical descriptor's original buffer/view/sampler payload, pointer and
control block. Descriptor storage's own retained payload must agree with that
original wrapper. Texture views retain their exact native parent, original view
layout and real descriptor-owned VkImageView. The existing Vulkan descriptor
implementation creates views with the parent's VkFormat; unproved format
reinterpretation therefore rejects. The original fault-readback child is
retained independently of the shader descriptor footprint, at its exact
canonical source index and registration generation.

Native payload owner counting uses core::SharedIdentityProjection; original
inventory counting reuses cpuOwnerCount. A view wrapper and its parent are
distinct owners; repeated references share a single counted identity. This
ceiling covers directly frozen native owners, while the existing resource and
capacity budgets remain in the original inventory. No GPU allocation is added.
An independent CpuMemoryQuota pays for all closure metadata. Factory rejection
does not partially publish, and hot validation allocates nothing. Native view
metadata is borrowed immutably under its retained payload; it is not copied with
an owned label. Existing Vulkan descriptor validation/writes use the same
immutable borrow, so Root/snapshot validation cannot reintroduce label-copy
allocation. The allocation-free regression uses names beyond string SSO.

The original Root fixture now accepts a supplied real device/entry and a scoped
observer, reusing the six-class builder. Native correspondence tests explicitly
exclude its Null initialized-state scenarios: actual Vulkan images start
Undefined and need genuine initialization/barriers in the eventual recording
chain. Internal resource registration includes every actual fault-readback child
selected by the queried capability, including Graphics snapshots. This changes
no production capacity or scene budget.

Evidence under `D:/arp/build/bindless-original-buffer-sources1/`:

- `vulkan-root-closure-gpu-tests-complete.log`: rebuilt RTX 3050 registered GPU
  smoke PASS, full/partial numeric prepared-path regressions plus Compute and
  selected Graphics cold Root correspondence. `canonicalRootNativeClosure=true`
  and `canonicalRootExecution=false` remain separate report fields.
- `vulkan-root-closure-host-final.log`: integrated registered checks 13/13 PASS,
  including GPU; `vulkan-root-closure-host-baseline.log` retains the 12 host-only checks.
- `vulkan-root-closure-source-guard-final.log`: source guard 367/367 PASS.
- `vulkan-root-closure-web-build-final.log` and `vulkan-root-closure-web-tests.log`:
  rebuilt Emscripten Release and Node contract regressions 6/6 PASS.
- `vulkan-root-closure-capture/report.json` and
  `public-bindless-renderdoc-report.json`: current full/partial smoke and replay
  PASS; exact current Fragment/Vertex SPIR-V hashes, matching independent
  direct/indirect targets, Vertex-only D32 output, no validation messages. PNG
  outputs were inspected by the agent. These remain prepared-path regression
  evidence, not canonical Root or WOCC execution evidence.

This native-correspondence checkpoint establishes only cold ownership. The
subsequent RootFaultFence /6 section records the buffer-only execution increment;
general texture/Graphics recording and native loss remain pending. WebGPU error
scope/callback counts must not be fabricated to borrow the Vulkan profile.
Null remains validator-only; Metal/WebGPU Root native correspondence explicitly
rejects. No public execution capability is promoted by this prerequisite.


## RootFaultFence /6: scoped Vulkan buffer execution

This explicit prototype profile consumes Root /1, RootState /4 and the exact
immutable stream with the existing seal, transaction and journal. It admits
one buffer-only Compute bind/dispatch, one original latch clear and one exact
fault copy. Unsupported commands, resources, content profiles, foreign owners,
missing native-payload capacity and CPU quota aliases fail before recording.
It does not authorize general six-resource/Graphics or WOCC execution.

Lifecycle /6 requires zero callback tickets, registrations, owners and CPU
bytes. Its callback quota must be absent. CapacityRequest /2 reserves one
original Source submission unit, with zero callback units. Seal overhead is
therefore two owners, while schemas /1 through /5 retain their original count.
Null validates only, Vulkan implements the closed opt-in issuer, Metal and
WebGPU reject with independent stable diagnostics. Default Root execution
remains closed on all backends.

The genuine native command retains the authentic closure, reservation, binding
and move-only original fault lease. It never has a second graph, journal or
result store. Native command/fence creation and barrier capacity happen cold;
fault fill is recorded at its original stream ordinal, with the declared
CopyDst barrier. Explicit copy and all original state transitions are consumed.
The existing backend retirement ceiling is a fixed 64-slot array, so the first
submission does not allocate or retain a per-Source CPU quota globally.

Only actual queue invocation enters SubmitPending. Native retirement owns the
command before invocation. Queue/fence errors invalidate the original journal
and retain bounded quarantine; timeout retains pending custody. Wrapper shutdown
cannot release canonical pending/quarantined custody. Complete terminal native
loss/drain remains an open acceptance gate; no error becomes completion proof.

A real fence, exact Pending correspondence and mapped-read invalidation precede
copying into the common preallocated typed buffer result. The original journal
must commit before publication. Vulkan writer authority is restricted to the
buffer specialization; texture results receive a complete empty writer type.
Normal fence completion releases custody and fault capacity. Unsubmitted
abandonment rolls back. Published CPU request/result metadata survives GPU and
device retirement without retaining the original GPU closure.

Current RTX 3050 tests use the existing cleanMain/faultMain Slang package with
one ABI element per buffer. Non-uniform loads/queries/stores/atomics produce
counters 14/16, inputs remain 10/11, and faults are exactly 0/1. Tests cover
forged owner rejection, exhausted cold quota, duplicate publication/submit,
sealed CPU refusal, unsubmitted cancellation, native-only seal retention,
submission/completion allocation0 and exact quota return. Fixture cold host
initialization is explicit and does not close the production content/upload ABI.
Evidence is indexed in D:/wocc/docs/bindless-delivery-status.md.


RootFaultFence negative lifecycle validation additionally withholds real fence
observations and rejects a missing queue entrypoint. It verifies timeout and
shutdown drain keep Pending original custody, then restores the real Vulkan
entrypoints and requires actual driver fence success. A separate negative run
calls real vkQueueSubmit, deliberately obscures its successful return and
checks post-invocation quarantine: invalid journal, no result publication,
retained resources and fault budget. A subsequent authentic fence retires the
quarantined command unpublished; it cannot revive the invalid journal. This
injection proves failure handling, not native device-loss semantics. Unknown
invocations without a signalled fence continue to retain bounded custody until
an authentic fence or successful idle of the same retained runtime proves drain.

### Exact-runtime device-idle completion

The existing explicit `GpuDevice::waitIdle()` now invokes a closed backend bridge
that calls real `vkDeviceWaitIdle` and consumes success only for tracked submitted
commands with the same shared runtime owner. It does not poll or retire other
devices. This blocking API is never used as a per-frame or deadline-drain fallback.
Lifecycle /6 has no foreign callback participants; its four-backend device-idle
lowering is Vulkan Prototype, Null ValidatorOnly, Metal/WebGPU DiagnosticFailure.
Other lifecycle profiles reject this completion authority.

Clean/fault submissions use the original completion path: mapped invalidation,
exact Pending check, same journal commit, then typed publication. Quarantined
submissions retire unpublished and cannot revive the journal. Missing native idle
entrypoints, failed idle, synthetic runtimes and wrapper destruction retain
custody. Failure diagnostics borrow stable literals and allocate no memory.
Device-loss capture/detach and deadline-bounded terminal loss remain open.

Rebuilt MinGW checks PASS 14/14; source guard PASS 369/369; rebuilt Emscripten
Release/Node PASS 6/6. Actual RTX 3050 GPU smoke tests hold fence observations
unavailable while proving clean/fault idle completion, foreign-device isolation,
failed/missing idle retention, uncertain real-submit retirement and a rejected
queue invocation whose fence never signals. Missing/failed/successful idle run
under exhausted original quotas with allocation0, and all original submission,
fault and metadata budgets return. These negative injections do not prove native
device loss. Evidence: `build/bindless-original-buffer-sources1/vulkan-root-idle-*`;
fresh smoke report includes `canonicalRootDeviceIdleCompletion.success=true` and
continues to report `canonicalRootExecution=false`.

### NativeResultLoss /1: Vulkan source prerequisite

Vulkan now has a separately selected native-result loss source. Its L2 budget
authors one source subscription, two retained owners (binding and runtime) and
an independent CPU quota. It has zero foreign callbacks and does not reinterpret
the existing WebGPU promise/registration budget. The original
`prepareOriginalLossBinding`, sealed lifetime receiver, loss observation and
resource registration remain authoritative; there is no additional journal or
caller-issued loss/completion receipt.

Four-backend lowering is explicit: Vulkan permits the scoped source factory;
Null validates only; Metal rejects its missing source; WebGPU requires its
existing callback source. Execution capability remains false. Legacy
OriginalNativeLoss /1 lowering and budgets are unchanged.

The runtime owns one typed weak subscription and a sticky native-loss terminal.
The binding retains the runtime under the authored owner budget; preparation,
native result consumption and detach require external serialization. Closed
command consumers forward actual queue submit, fence status/wait and device-idle
results. Only `VK_ERROR_DEVICE_LOST` delivers the sealed receiver. Success,
timeouts, unknown errors, facade destruction and logical detach cannot manufacture
NativeLost, commit a journal or prove GPU drain. Loss remains terminal even after
later successful calls; a lost runtime cannot issue a replacement subscription.

Native buffer/texture/view registration verifies real device handles and the
same resource-registry payload owner; view validation also verifies the real
image parent. This does not enable typed pipeline/table/sampler loss joins or
initialize any texture. Those joins, original texture upload/completion and
complete Root recording/terminal retirement remain required next increments.

Current RTX 3050 tests verify source quota exhaustion/rollback, alias rejection,
duplicate runtime subscription refusal, real three-kind resource correspondence,
foreign real-device identity, explicit detach/rebind, shutdown versus retired
versus native-loss diagnostics, sticky failure and exact budget return. The
loss-result consumer uses negative `VK_ERROR_DEVICE_LOST` injection after actual
successful driver idle; it is not evidence of actual hardware device loss or
completed GPU teardown. NativeResultLoss /1 never grants completion authority.
Evidence is indexed in `D:/wocc/docs/bindless-delivery-status.md`.

### ContentCreation /3: Vulkan native Undefined texture prerequisite

Independent opt-in extends whole creation with a physical native-allocation
ceiling, separate from the exact logical texel footprint and content CPU quota.
Initial state must be Undefined. /1 still requires CopyDst; /2 remains a fixed
attachment-cell domain. /3 rejects cell budgets, absent physical budgets and
non-GpuOnly intent. Four-backend decisions are Vulkan Prototype factory,
Null ValidatorOnly and explicit Metal/WebGPU refusal. No decision grants content
upload, sampling, transition or completion authority; default capabilities stay
unchanged.

`GpuDevice::queryOriginalContentTextureAllocation` returns the exact unbound
image requirement, with the same flags, dimensions and format as allocation.
It never uses placed/alias-image queries, logical estimates or synthetic native
payloads. The factory repeats the query on its own image before resident memory
allocation, checks both per-image and device remaining budgets, allocates
device-local memory with VMA, verifies its actual allocation size and binds that
same image. The returned texture retains original registration, loss observation,
content CPU quota and native allocation owners. Metadata is Uninitialized,
generation zero; actual resource state is Undefined. The exclusive native policy
is inherited by its view. Ordinary descriptor set/table writes, both image
barrier overloads, upload/copy and attachment paths reject exclusive resources.
Only a future independently authenticated original upload path may initialize
them; /3 cannot enter the existing /1 content mutation/completion plan.

Native image-query/allocation failures join the closed native-result loss
consumer. Only actual VK_ERROR_DEVICE_LOST delivers loss; no query/allocation
result issues completion. CPU allocation failures release the device ledger,
native memory/image and registry owner without publishing partial content.

Current RTX 3050 fixture measures 20 logical bytes and 1024 native bytes for the
two-mip 2x2 RGBA8 texture. Exact queried physical budget succeeds; per-image and
whole-device one-less budgets reject. Independent CPU domains, exact/one-less
quota pressure, metadata hot validation, exclusive descriptor/barrier/copy
refusal, view ownership, logical shutdown and all quota return are verified.
Host checks PASS 14/14, Wasm regression PASS 6/6, source guard PASS 371/371.
Fresh smoke/capture/replay PASS with matching shader hashes, no validation
messages and inspected direct/indirect/depth PNGs. This remains an engine fixture;
full Root execution and WOCC readiness are not granted. Current evidence is
`build/bindless-original-buffer-sources1/vulkan-content-creation3-*`.


### ContentPlan /7: original Undefined whole-upload planning

Independent opt-in pairs only ContentCreation /3 with plain original State /1,
Inputs /1 and StateSequence /1. Each fresh Uninitialized control must have one
complete whole-texture upload and one preceding authored whole
Undefined→CopyDst transition. The transition retains exact TopOfPipe/None to
Transfer/TransferWrite scopes; full mip/array coverage comes from the existing
frozen upload-layout validator. Duplicate uploads, partial transitions,
non-upload texture use and borrowed legacy/general/bindless profiles reject.
Control, upload, content-step, command-visit and projection-visit ceilings are
independent. Content, source, input, graph and recording CPU quotas must have
independent owners. Admission and cold plan publication mutate no state.

The existing transaction owns the sole journal. Content proposals explicitly
consume the immutable plan version: control /1 accepts only plan /1 and control
/3 only plan /7. CPU prototype application enlists and proposes original state
and content together. Explicit cancellation or dropping the pending root rolls
both back; the generation issuer never recycles consumed IDs. Public CPU commit
still refuses content completion. No standalone content journal, fake staging
initialization or caller-supplied receipt is introduced.

Four-backend lowering is Null validator-only, Vulkan CPU planning available with
native closure unavailable, and explicit Metal/WebGPU rejection for this profile.
The planning lowering's execution bit stays false. Older lifecycle seals /1–/6
reject content /7; default/general completion profiles /0–/3 also reject it. The historical CPU-only increment therefore
grants no upload, sampling or public Root execution authority. Next, join frozen
packing and a genuinely written/flushed immutable original staging lease, then
record actual image barriers/copies and commit through a separately admitted
same-seal/journal real-fence completion profile.


Historical CPU-only validation: rebuilt MinGW Debug PASS 15/15 (including command-retention),
Emscripten Release/Node PASS 6/6, source guard PASS 372/372. Genuine RTX 3050-issued
two-mip resources exercise the CPU plan, exact 5 command/17 projection visits,
duplicate/missing/partial transition rejection, cold quota pressure, explicit
cancellation/drop, generation IDs 1/2, original stamp invalidation and hot
allocation0. GPU/content/resource ownership returns to the measured live-device
entry baseline; all domains return to zero after teardown. The existing registry
bucket capacity is measured, not mistaken for an uploaded resource leak.
Fresh engine regression capture/replay PASS with exact current shader hashes,
empty validation messages and inspected direct/indirect/depth PNGs. Reports keep
`canonicalRootExecution=false`, and the /7 marker explicitly denotes CPU planning,
not GPU texture contents. Evidence: `build/bindless-original-buffer-sources1/vulkan-content-plan7-*`.

## Genuine immutable staging and whole-upload fence /7 (2026-10-14)

`OriginalTextureUploadStagingContract /1` freezes one original packing closure,
logical/native byte limits, mip mappings and independent metadata/resource CPU
limits. Its closed Vulkan factory creates a genuine CpuToGpu/CopySrc buffer,
checks the actual VMA allocation size, writes every packed byte through the
mapped-write/flush path, then seals host and GPU writes. The adopted lease shares
the canonical Source quota with the original texture/upload Sources; staging
metadata and resource storage remain independent. Cold observations capture the
actual Undefined/uninitialized destination and immutable CopySrc staging.
Vulkan queue submission performs the host-to-device domain operation for prior
flushed host writes. No HostWrite GPU state, caller initialization boolean or
ordinary staging-pool receipt substitutes for this initialization.

NativeLifecycle /7, CapacityRequest /3 and Completion /4 are isolated opt-ins.
Only ContentPlan /7 with plain Inputs/State/Sequence /1 may join. The same
canonical inventory must contain exactly the original destination lease, upload
Source and staging lease. The admitted six-command program is staging memory
dependency, whole Undefined→CopyDst barrier, transfer begin, whole upload,
transfer end and finish. Source-authored native calls, payload/staging capacity
and independently charged submission owners are checked before native recording.
Native barrier/copy emitters are shared with ordinary recording; their ordinary
public wrappers still refuse exclusive original content. The sealed issuer
validates original Pending buffer/texture/content cells and maps each mip from
the normative packing before actual `vkCmdCopyBufferToImage`.

Both RootFaultFence /6 and WholeOriginalUploadFence /7 use the existing native
command queue/fence custody, quarantine and exact-runtime idle retirement. Only
an authentic submitted command's successful driver fence/idle reaches the
private seal commit, using the same journal and Completion /4. Unsubmitted drop
rolls back; unknown/rejected submit invalidates and retains. Genuine completion
retires invalidated custody unpublished. Missing/failed idle cannot release it.
No second journal, standalone native receipt, CPU replay or callback budget is
introduced. Null validates only; Metal and WebGPU explicitly reject this native
profile, and the legacy lifecycle/completion profiles remain closed to /7.

Scope is RGBA8 2D and 2DArray whole uploads, two-mip numerical coverage, retaining
the actual sampled destination for a later Root join. Other formats/dimensions,
shader reads, public texture Root and Graphics require their own tested joins.
Current MinGW Debug checks PASS 14/14 plus genuine Vulkan smoke PASS 1/1;
Emscripten Release/Node PASS 6/6 and source guard PASS 374/374. Allocation-pressure
tests also fixed Vulkan buffer publication order so wrapper allocation failure
does not strand weak native control-block charges. CPU exact/one-less, actual
native staging ceilings, same-Source quota, drop rollback, withheld fence,
missing/failed idle and uncertain/rejected queue PASS. Hot submit and completion
allocate0; all quotas return to zero after logical-device teardown.

Fresh capture/replay verifies dedicated 768/1536-byte buffers and every uploaded
mip/layer exactly (2D bytes 1–20, array bytes 1–40). Current shader hashes match,
validation messages are empty, and direct/indirect/depth PNGs were inspected.
Evidence: `build/bindless-original-buffer-sources1/vulkan-upload-fence7-*`,
including `vulkan-upload-fence7-terminal-tests-final.log` and the capture's
`public-bindless-renderdoc-report.json`. This is scoped Prototype Backend
Validated; `canonicalRootExecution=false` and no WOCC/production claim changes.


## Completed texture descriptor publication /1 and loss registration /9

The next increment publishes a genuine completed ContentCreation /3 texture
through an authentic `OriginalCommandTextureViewSource`, its retained original
parent and the engine-issued content observation. Scope is whole RGBA8 sampled
2D/2DArray views. One cold publication preflights one native image view, one
native write and the complete typed Source owner bound. The descriptor's future
ShaderRead layout is authored metadata; this publication does not transition the
committed CopyDst state or authorize shader execution.

The ordinary descriptor entry still rejects exclusive original content. A
separate non-virtual L2 write gate checks opt-in, budget, successful original
registration, exact view/parent identity and current Completed observation.
Vulkan checks the same runtime, logical device, registry payload and native
parent image, then uses the existing descriptor emitter and successful-write
revision path. Snapshot writes and inactive physical aliases retain that same
Source/content provenance and exact graph access range. Native descriptor
storage retains the Source as well, until the physical set is retired.

ContentCreation /3 requires native loss custody. Registration /9 is a separate
descriptor-only join to the existing authentic subscription, using independently
charged registry metadata. Real Vulkan arena candidates must preserve the exact
runtime/registry and require native allocations; set candidates must have a
real current VkDescriptorSet/layout. The existing registry keeps the weak
subscription and invalidates arena/set/view/parent observations on retirement.
Pipeline, sampler and descriptor-table joins are explicitly outside /9; legacy
registration and accepted-pipeline/scoped-group permissions are not extended.
Null validates only, while Metal and WebGPU explicitly refuse these new profiles.

Public texture Root execution remains closed. The next join must extend the
same state graph/read-only content plan to BindlessRoot origins, prove genuine
CopyDst-to-ShaderRead barriers and Pending observations, retain the original
content generation, and commit only through the existing genuine completion.
The full six-class native-loss and terminal/drain domains remain open. Prepared
Root pipelines already retain the existing BindlessCross authorization and
native payload/layout; a separate generic pipeline prototype is not a prerequisite.


Current verification for this publication increment: source guard 378/378;
rebuilt MinGW Debug host 14/14 (20.84 sec), real Vulkan GPU plus the existing
upload terminal scenarios 1/1 (18.39 sec), rebuilt Emscripten Release/Node 6/6.
Publication checks reject Uninitialized/Pending/stale observations, forged Source
owners, wrong state, one-less image-view capacity, foreign devices and retired
subscriptions without advancing descriptor revision. Real native storage keeps
the exact view Source alive after caller/snapshot/set owners are dropped; release
returns all quota domains to zero after device teardown. An old full-pipeline
fixture now rejects unimplemented registration /10 and explicitly refuses
pipeline permission under descriptor-only /9.

Evidence prefix: `build/bindless-original-buffer-sources1/completed-texture-publication-*`.
The capture/replay has four original uploaded images (two 2D and two 2DArray),
all mip/layer bytes exact; clean/fault Root numerical regressions and shader
hashes match, validation messages are empty and output PNGs were inspected.
`completedTexturePublication=true`, `canonicalRootExecution=false`.
This is scoped Prototype Backend Validated, with no scene/WOCC/production claim.

## Descriptor/sampler loss association /2

DescriptorLoss request /2 keeps registration /9 and independently opts original
samplers into the same real device loss subscription. Request /1 continues to
reject any sampler request. Both requests reject pipeline and descriptor-table
budgets. SamplerLossJoin /2 accepts only this descriptor/sampler domain, while
its existing /1 WebGPU typed-registration behavior is unchanged.

The sampler factory checks the exact original registry weak subscription and
device domain, then the Vulkan wrapper's real VkSampler, logical device and
registry payload owner. The issued original lease retains the genuine sampler
and its registration generation. Capacity exhaustion fails before registration
publication; subscription retirement invalidates its original registration and
descriptor use and rejects subsequent sampler creation. The lease remains valid
ownership until released. Native correspondence and
lease validation allocate0 and do not query the driver.

Null validates only; Vulkan admits this scoped source association. Metal and
WebGPU explicitly reject /2. This association supplies ownership for subsequent
Root texture execution, without granting shader/state/completion authority.

Current scoped validation: rebuilt MinGW Debug host 14/14 (26.44 sec), real
Vulkan GPU including upload terminal scenarios 1/1 (17.51 sec), rebuilt
Emscripten Release/Node 6/6 (1.39 sec), source guard 379/379. Genuine sampler
creation, descriptor metadata, exact capacity exhaustion and retired original
registrations PASS; lease ownership remains valid after retirement. Fresh
capture/replay verifies all four original 2D/2DArray mip/layer byte sets and
existing clean/fault, direct/indirect/depth outputs. Current shader hashes match,
validation messages are empty and PNGs were inspected.

Evidence prefix: `build/bindless-original-buffer-sources1/sampler-loss-join2-*`.
`originalSamplerDescriptorLossJoin=true`, `canonicalRootExecution=false`.
This is scoped Prototype Backend Validated, not WOCC or full-resource execution.

## Completed Root texture read plan /8 and view provenance

Root access projection now borrows the exact declaration Source index from the
original physical element. Texture graph points preserve that original view
index alongside the canonical parent lease and Root index. This shares the
existing inventory/projection without guessing a view from its resource ID or
creating a second retained access graph. Native and Emulated ownership fixtures
check the exact parent lease and declared range; hot observation allocates0.

The existing read-only content planner additionally admits ContentPlan /8 for
genuine Completed control3 content under Assembly /6, State/Inputs/Transaction
/1 and BindlessRootState /4. Whole RGBA8 sampled 2D/2DArray view shape, exact
view-parent provenance, bounded projections and actual Draw/Dispatch consumption
are required. Retention alone cannot stand in for consumption. Content is
observed, never proposed or completed again; the same journal owns resource state.

Pending observation /4 checks the authentic content, registration, original
state cells and journal. First use requires exactly one whole CopyDst→ShaderRead
transition with one state revision advance. Repeated ShaderRead use requires
zero transitions and the unchanged state revision. Both preserve completed
content generation and revision. Other Pending/read-plan profiles retain their
existing control1/General scope. Array geometry uses declared mip×layer color
cells instead of the old 2D-only mip ceiling.

This is a frontend implementation and value-contract/provenance prerequisite.
Null validates only; all four read-plan lowerings explicitly grant no execution.
At that frontend checkpoint, existing Lifecycle /1-/7 refused ContentPlan /8.
The subsequent scoped native join below is independent of those profiles.


## CompletedRootTextureFence /8 (2026-10-14)

Lifecycle /8, capacity request /4 and completion profile /5 join read-only
ContentPlan /8 to the existing RootState /4 Compute program, buffer readback
binding /2, canonical inventory and journal. They do not change /6's buffer-only
allowlist or /7's upload permission. Callback tickets/registrations/owners/bytes
must remain zero; independently charged recording/submission domains and an
actual native command/fence are required. Null validates only; Metal/WebGPU
refuse this issuer with stable diagnostics.

Vulkan admits actual Completed control3 sampled 2D texture views and independent
non-comparison native samplers. Array views and comparison samplers reject before
native custody, even though the frontend read/view contract also represents arrays. Admission requires the immutable read plan and same physical
Root closure. Each authored image barrier references the exact canonical parent
payload already retained by that closure; unsupported operations and foreign
parents reject before fault custody or native claim. Recording reuses the
original image barrier emitter. Record, submit and actual fence completion check
Pending /4 against frozen content and original state cells. Fault map/invalidate
precedes the original commit; publication follows commit. Quarantine and exact
runtime idle use the existing native custody. No content proposal/completion or
second journal is added by a sampled read.

Vulkan Compute now publishes SampleLevel, using explicit-LOD SPIR-V image
sampling and the existing verified descriptor/non-uniform feature enablement.
It continues to reject Compute SampleGrad. This does not add implicit PBR
sampling, array shader ABI, Graphics, storage image mutation or scene readiness.

Current genuine RTX 3050 evidence: two RGBA8 2D images uploaded through /7,
two mips each, two distinct nearest/linear samplers, reversed non-uniform image
indices and independently reversed sampler indices. All twelve output words,
fault 0/1, unsubmitted rollback, original quota retirement and a second frame
using the same snapshot/content generation PASS. The second frame authors no
image transition; the completed content stamp stays exact. Fresh capture/replay
independently verifies three shader-hash-matched dispatches and six exact
original texture uploads, with no validation messages. Dedicated /8 uncertain
submit/idle/loss and Graphics/array/storage/scene coverage remain open.

Evidence directory: `D:/arp/build/bindless-original-buffer-sources1/`, prefix
`root-texture-fence8-*`; current capture/replay in `root-texture-fence8-capture/`.
`canonicalRootCompletedTextureExecution=true` and
`canonicalRootCompletedTextureRepeat=true`; full `canonicalRootExecution=false`.


## Completed texture Root /8 terminal checkpoint (2026-10-14)

Current rebuilt Vulkan GPU PASS 1/1 (33.84 sec), source guard PASS 383/383.
The shared /6-/8 terminal fixture consumes the same native executor and actual
queue/fence/idle operations; it does not add a lifecycle or journal. Ten /8
clean/fault cases verify retained native command/closure/seal after caller release,
texture/view/sampler ownership, missing queue retry, withheld fence timeout and
shutdown, unknown real-submit and fence results, rejected invocation, foreign
idle isolation, missing/failed idle, genuine same-runtime fence/idle retirement.
Read-only uploaded content stamps stay exact while uncertain resource state
is invalidated permanently. Failed fault results never publish, hot paths
allocate0, native closure/submission quotas return. Negative observations are
not hardware device-loss evidence. Evidence: engine
`build/bindless-original-buffer-sources1/root-texture-terminal-gpu.log` and
`root-texture-terminal-guard.log`.

Next: required Texture2DArray shader ABI, canonical Graphics/pass/input/indirect
and PBR sampling semantics, followed by matching public WebGPU execution and
WOCC. Metal remains last; full Root/WOCC/Production Ready remain open.


## Sampled Texture2DArray compiler and native Root join (2026-10-14)

TextureShape /1 is explicit in the closed generator schema. Unversioned
variants retain their existing sampled/storage 2D ABI; arrays require the
version and exact dimension. Unknown dimensions, dimensions on samplers/buffers
and array storage images fail. Sampled array query returns uint3(width,height,
layers), load consumes int4(x,y,layer,mip); SampleLevel/SampleGrad take explicit
uint layer plus float2 coordinates (and float2 gradients). Out-of-range layer
and mip loads and sampled layers latch faults before native resource access.
No implicit sample, comparison sampler or fixed-LOD replacement is introduced.

The generator reuses SpirvLayoutFacts image facts; the package loader reuses
SpirvInstructionView via shader/spirv_image_declaration.h. Every active sampled/
storage image independently proves pointer, fixed descriptor count, scalar,
actual dimension/arrayed/MS/sampled/format fields. Slang emits Depth=2 (unknown),
which is retained as an unknown binary fact and never proves a depth capability;
Depth=1 rejects this color profile. Source/view/format contracts supply actual
color semantics. Existing lowering YAML and artifact hashes remain required.
Tests reject altered YAML dimensions and rehashed SPIR-V array-bit substitution.

The same Vulkan Lifecycle /8 now admits sealed package-matched whole RGBA8
2DArray views. /6 remains buffer-only, /7 remains upload-only; Null validates,
Metal/WebGPU /8 retain their explicit issuer-missing diagnostics. No executor,
resource inventory, state journal or completion protocol is duplicated.
The Root CPU fixture now budgets state steps/cells from actual mip x layer
ranges rather than the texture count.

Actual native array Compute PASS: two images, two layers and two mips, independent
nearest/linear samplers, nonuniform image/sampler indices, all 32 result words,
layer/mip faults, unsubmitted rollback, unchanged read-only stamps and a second
frame without image transitions. Ten clean/fault terminal scenarios reuse the
shared /6-/8 native fixture and prove bounded custody and unpublished uncertain
retirement; they do not prove hardware loss. Fresh RenderDoc replay independently
checks 3 current shader-hash-matched array dispatches, 6 exact original array
uploads and every mip/layer/result, with zero validation messages.
Evidence: build/bindless-original-buffer-sources1/root-array-*; capture/replay
in root-array-capture/. Full Root execution/Graphics/WebGPU/WOCC remain open.

## Graphics fault and native attachment creation checkpoint (2026-10-14)

The shared Root fixture now validates Graphics clear/copy/Pending custody with
the unchanged RootState/Stream /4 contract. It expands authored scopes before
constructing original owners, so the actual graph contains BeginRenderPass,
authentic BindPreparedGraphicsBinding, viewport/scissor, Draw and EndRenderPass.
Selected graphics stages replace Compute in the exact fault-latch barriers.
Existing wrong owner/range/snapshot, duplicate-copy, missing-transition,
quota/sequence mismatch, cancellation, hot-allocation and retirement cases are
reused for Native/Emulated packages on host and rebuilt Wasm/Node. No new graph,
resource inventory, journal, fault result store or lifecycle version is added.

ContentCreation /4 adds queried native allocation bounds to the existing
original attachment cell control. Shared cell-layout validation checks the
authored format/mips/layers/aspects before both native query and creation.
Default query /3 remains upload-only; callers select /4 explicitly.
Creation /2 continues to reject physical allocation fields. /4 requires both
logical texel/cell and actual native memory ceilings and independent content,
resource, registry, lifetime and loss CPU owners. Null rejects creation; Vulkan
creates genuine exclusive original attachments; Metal/WebGPU explicitly reject
the native profile. All four remain without content execution authority.

RTX 3050 fixture PASS for RGBA8, RGBA16F, D32 and depth/stencil: actual three-mip,
two-layer image queries and VMA allocations, all original cells Uninitialized,
resource state Undefined, one-less logical/native/cell/CPU refusal, view-parent
exclusive provenance, ordinary render-pass rejection, hot validation0 and exact
shutdown retirement. Native allocations measured 4096 bytes for each fixture,
logical sizes 672/1344/672/672, cells 6/6/6/12. These are fixture facts only.
At this checkpoint, legacy ContentPlan2/3 and private cell proposal/completion reject control4;
neither descriptor writes nor Root Compute /6-/8 can consume these attachments.

Evidence: final `root-graphics-attachment-host-current.log` 14/14 (21.47 sec),
including rebuilt retention and Null hidden frame, `root-graphics-fault-web.log` 6/6,
`root-graphics-attachment-source-guard.log` 384/384,
`root-graphics-attachment-gpu.log` 1/1 and
`root-graphics-attachment-current/report.json` PASS in
`build/bindless-original-buffer-sources1/`. The report records creation /4,
not attachment texels or native Root Graphics execution. Previous RenderDoc
captures remain historical visual evidence; this increment changes exclusive
creation/admission and diagnostic rejection only.

Next: compose control4 attachment mutations and control3 completed sampled reads
in this same original Root plan, then consume actual prepared Graphics/pass/draw
through the genuine queue/fence path. Full Graphics/input/indirect, WebGPU,
WOCC and Production Ready remain pending. Metal is deferred by the user.

The final retention rebuild corrected the previous unknown-version /4 negative
fixture to unsupported /5. It continues to verify upload /3 separately; the new
attachment /4 CPU contract and all four lowering outcomes run in this baseline.

## Original Graphics mixed content /9 (2026-10-14)

ContentPlan /9 now composes genuine prepared Graphics Root /4, native-created
attachment control /4 and uploaded Completed sampled control /3 in the same
original transaction, state cells and mutation journal. General /6 grants no
authority to /9. The exact-type stream builder authenticates the original
graph/input/inventory, all sequence fields including Root version/opt-in, one
genuine selected Graphics Root, Draw-stage accesses and independent CPU owners.
Cold dry-run/population reuse the existing bounded mixed storage and cell/read
inspectors; all capacities are checked before publication.

Attachment control /4 pairs only with plan /9; legacy /2,/3,/6 still refuse it.
The current shape is one whole mip/layer cell per attachment, Clear+Store,
RGBA8/RGBA16F/D32; depth/stencil creation support does not grant /9 execution.
Completed sampled reads reuse the /8 inspector with actual Graphics Draw
provenance. Public CPU apply can propose attachment cells, but CPU commit,
old native seals and the private native completion path still refuse /9.
All four backends publish CPU validation and no /9 execution capability.

Real RTX 3050 owners validate Vertex buffer-only and Fragment sampled Graphics
composition: two genuine /7 texture uploads, independent samplers, exact/one-less
control/cell/step/command/projection/CPU capacities, no partial plan, old profile
refusal, same-journal Pending, cancellation/destructor rollback, recapture,
unchanged completed texture stamps and hot allocation0. This is CPU Prototype
composition using backend-created owners, not native Graphics execution.
Vulkan Fragment buffer operations remain unpublished and must be validated
before that workload can be admitted; the initial package was correctly refused.

Native/Emulated Graphics shader families generate SPIR-V/MSL/reflection and WGSL
as applicable. Current rebuilt host PASS 18/18 (25.41 sec), Wasm/Node 10/10
(2.33 sec), source guard 385/385 and style/embedded JS 5/5. Real-driver regression
PASS 1/1 (56.10 sec), retaining upload/2D/array/terminal checks. Evidence prefix
`root-graphics-content9-*` in `build/bindless-original-buffer-sources1/`; preserved
`root-graphics-content9-current/report.json` records composition=true and native
Graphics/full Root execution=false. No new visual-output claim is made.

Next: typed cold native pass/framebuffer preparation and its independent budget,
actual prepared Graphics/pass/draw consumption and authentic fence completion
of /9 in this same journal. Reuse the existing Vulkan original command/fence
issuer; retain fault custody and prohibit old seal/completion substitution.
Then scene input/indirect/sampling/buffer/depth/storage, public WebGPU and WOCC.
Metal remains last; full execution and Production Ready remain pending.

## Original Graphics cold pass /1 (2026-10-14)

`OriginalBindlessGraphicsPassContract` defines cold, single-pass preparation,
separate attachment/native-object/direct-owner/CPU ceilings, and explicit
four-backend lowering. It authorizes no command or completion. Vulkan Native
preparation is implemented; Null remains ValidatorOnly and Metal/WebGPU return
stable unavailable diagnostics. The finite shape permits up to eight color
attachments plus D32 depth, whole 2D single mip/layer, Clear+Store, sample1;
subpasses/dependencies, resolve, layered, swapchain, aliasing, Load/Discard and
depth-stencil are refused. Device attachment/framebuffer limits are queried.

The actual Vulkan pipeline factory freezes pointer-free L2 compatibility facts
from its validated creation descriptor. Only a genuine backend pipeline exposes
these immutable facts; caller metadata or the absent ordinary registered full
definition cannot substitute. No extra shader/layout pointer lifetime is assumed.
`VulkanOriginalBindlessGraphicsPass` retains the same native Root closure,
canonical inventory, original /9 transaction and exact plan-retained stream.
No new graph or journal is built. Source admission bounds native object demand;
additional direct shared owners are deduplicated. CPU metadata has an independent
quota. A private `AtomicCapacityLease` retains the actual physical capacity
budget for one renderpass, one framebuffer and each attachment view. All
correspondence/capacity checks precede native creation; RAII destroys partial
objects before releasing the physical charge or original owners.

Current RTX 3050 evidence validates single RGBA8 color cold preparation for
Vertex buffer and Fragment sampled Roots. Exact/one-less quotas/objects/owners,
exhausted shared native capacity, foreign stream and quota-domain rejection,
mutated caller pipeline state, hot allocation0 and quota/capacity return pass.
MRT/depth are value contract, not Backend Validated cold creation. Host18,
Wasm/Node10, guard386, style5 and full GPU1 pass. See
`build/bindless-original-buffer-sources1/root-graphics-pass-cold-*`; preserved
`root-graphics-pass-cold-current/report.json` records cold preparation=true,
Graphics/full Root execution=false, stderr empty. The terminal subprocess
completed in 34.54 sec independently and now uses the positive runner's same
bounded 90 sec deadline; registered full regression completed in 68.39 sec.

This stage neither emits a draw nor provides new visual evidence. Old lifecycle
seals, CPU commit and private /9 native completion stay closed. Next, the existing
original Vulkan command issuer must consume the retained preparation with a
distinct Graphics lifecycle/completion profile and genuine fault/fence/content
retirement. Ordinary `recordRenderPassBegin` keeps its content-exclusive gate.

## Original GraphicsRootFence /9 execution (2026-10-14)

The existing original Vulkan command issuer now consumes the exact cold pass,
Root closure, original stream and ContentPlan9 journal. Lifecycle9, capacity
request5, completed sampled Pending use5 and private completion6 are independent
of Compute6/8 and General5; old or mismatched profiles refuse. Four-backend
lowering is explicit: Vulkan prototype issuer, Null ValidatorOnly, Metal/WebGPU
DiagnosticFailure. The cold preparation itself still grants no execution.

Cold preflight checks genuine pass/closure/transaction/stream correspondence,
native object and submission owner capacities, independent CPU quotas and the
complete command allowlist. Recording borrows cold native pass handles and
reuses actual bind/viewport/scissor/draw/barrier/copy operations. Custody retains
the pass until genuine fence or same-runtime idle drain. No ordinary exclusive
attachment operation, second graph/journal, CPU commit or caller completion
flag substitutes for native proof. Completion commits existing attachment/state
cells before publishing fault bytes; read-only uploaded stamps remain exact.
Cancellation restores state/content without reusing its consumed generation.

RTX3050 single RGBA8 sampled Graphics passes real draw/fence/fault0, canceled
generation1 followed by completed generation2 and full physical/CPU retirement.
Five shared terminal scenarios cover withheld completion/shutdown, queue retry,
missing/failed/foreign idle, real fence/idle and uncertain queue/fence/rejected
submission: invalidated attachment/state never revive and results stay unpublished.
Independent RenderDoc event511 matches the actual fragment shader hash and all
16 color bytes; validation messages are empty. Capture/replay evidence is under
`build/bindless-original-buffer-sources1/root-graphics-execution-capture/`.
The standalone full terminal runner passes in49.33sec. Host18, Node10, guard387
and style5 pass; registered current GPU report uses `root-graphics-execution-*`.
Registered GPU1/1 passes in89.21sec; preserved current report has Graphics
execution/terminal success=true and complete Root execution=false.

This closes a scoped prototype execution path only. Fragment gradient/implicit
sampling parity, full array/depth/storage/buffer/input/indirect domains, fault1
Graphics, hardware loss, full workload budgets and public WebGPU remain gates.
MRT/depth cold value contracts do not inherit this single-color execution proof.
WOCC/GPU-driven and deferred Metal remain pending; Production Ready stays closed.

## Original Graphics coordinate-gradient increment (2026-10-14)

The existing sampled Graphics family now derives mip UV from SV_Position and
evaluates ddx/ddy before non-uniform image/sampler indexing or fault branches.
Its actual SampleGrad result selects mip1 for the authored 2x2 texture; no fixed
LOD replaces this operation. Native and Emulated package generation retains
Slang as source and emits SPIR-V/MSL/reflection/WGSL as applicable. Disassembly
contains OpDPdx/OpDPdy and OpImageSampleExplicitLod Grad. WGSL evaluates dpdx/
dpdy before indexed calls and dispatches with explicit textureSampleGrad.

Fresh RTX3050 capture/replay under `root-graphics-gradient-capture/` identifies
event510 by exact shader hash and verifies all16 RGBA8 bytes, including mip1
from actual coordinate gradients; validation messages are empty. Affected host
and Node Native/Emulated package checks each pass4/4. The same original issuer,
fault binding, pass ownership and real completion are used. This is scoped
SampleGrad evidence, not full WOCC implicit PBR/terrain equivalence or public
WebGPU browser execution. Scene arrays/depth/material UV/discard cases remain.
Current registered full GPU1/1 passes90.59sec including five Graphics terminal
cases; `root-graphics-gradient-current/report.json` preserves success, Graphics
execution/terminal=true, full Root=false and empty stderr. Style5/5 passes.

## Original Graphics buffer increment (2026-10-14)

Same GraphicsRootFence9 issuer, no second graph/journal. Vertex/Fragment
UniformBuffer16 load and ReadOnlyStorageBuffer4 load/query; Fragment
StorageBuffer12 load/query/store/atomicAdd pass actual nonuniform indexed draws.
Explicit flush/invalidate and completion-gated counters `[102,10,10]`,
`[202,20,20]`; guarded table/element bounds produce fault0/1 without stray writes.
Capability publication requires fragmentStoresAndAtomics and actual resource
nonuniform features. Fragment StorageTexture remains closed.

Independent RenderDoc buffer draws543/575 match exact SPIR-V hashes; counter
bytes and fault copies546/578 match. RGBA8 quarter-value output permits only
half-LSB quantization (0.5 ->127 or128); other channels and prior Grad draw510
remain exact, validation empty. `root-graphics-buffers-capture/` retains evidence.
Current host20/Node12/guard388/style5/GPU1 pass. Preserved
`root-graphics-buffers-current/report.json` has scoped Graphics/buffers/terminal
true, full Root false, stderr empty. Nonzero descriptor ranges, material/instance
struct ABI, input/indirect, array/depth/storage, hardware loss and public WebGPU
remain gates; WOCC and Production Ready do not follow from this subset.

## Original Graphics array gradients and discard (2026-10-14)

Existing TextureShape1 and GraphicsRootFence9 contracts now have a scoped
Texture2DArray Fragment execution check. No new issuer, graph, state cells,
journal or execution grant: genuine two-layer/two-mip original uploads and
existing samplers/Root/cold pass/fence are reused. Coordinates use actual
SV_Position; ddx/ddy dominate nonuniform table selection and discard. Array layer
is explicit and does not contribute to the 2D coordinate derivatives. Generated
WGSL retains dpdx/dpdy before discard and textureSampleGrad scalar switches;
Native/Emulated Slang emits SPIR-V/MSL/reflection/WGSL as applicable.

Fresh RenderDoc replay matches clean/fault SPIR-V hashes at draws665/722.
Every RGBA8 byte is exact: `[32,128,64,255,0,0,0,0,48,144,80,255,24,48,40,255]`.
The top-right pixel stays authored transparent clear after discard. Original
fault copies668/725 read0/1; layer2, mip2, table2 and sampler2 bounds have no
visible side effect. Existing buffer counters/fault and sampled Grad draw are
revalidated in the same capture; validation messages empty, PNG inspected.
Evidence: `D:/arp/build/bindless-original-buffer-sources1/root-graphics-array-capture/current/`.
Host22/22, Node14/14, source389/389 and style5/5 pass. Full registered GPU1/1 PASS (133.17 sec); preserved
`root-graphics-array-current/report.json` records success, Graphics/buffers/array/
terminal=true, full Root=false and empty stderr.

This proves explicit array SampleGrad/discard for this authored case, not all
terrain/PBR implicit sampling, arbitrary dimensions/formats, full material ABI,
public WebGPU browser execution or WOCC readiness. Metal remains deferred.


## Completed depth source1 checkpoint (2026-10-15)

Typed completed depth source/1 and descriptor publication/3 now preserve the
original view/parent/quota and immutable per-visible-mip control2 expectations.
Actual clear/store completion validates three single-cell sources and WebGPU
native metadata; source/native metadata exact-less-exhausted admission, hot0
and quota rollback pass. Existing Vulkan public regressions remain green.
This does not close public depth Root: typed snapshot/padding, compatible
registry descriptor success, full-cell content/state/native closure and
original completion/fault/retirement remain next. No new depth execution
capability; multi-mip mixed-generation evidence is pending. Current evidence:
D:/arp/build/bindless-original-buffer-sources1/depth-publication3-current-report.json.


## Static ordinary Graphics member memory /1 (2026-10-17)

The original Root now exposes typed member correspondence via the same selected
pipeline facts and canonical original buffer/range/stage/live-generation query.
Explicit Native source/rule requests require this proof in existing pre-record
range admission. Missing proof, foreign owners, unselected entries, undersized
buffers and retired Roots refuse with cleared outputs; hot queries allocate
nothing. Runtime/descriptor arrays refuse /1; existing range query2 and old
WGSL-specific inspection preserve their scopes. Windows4/4, actual Mac6/6,
Wasm/Node1/1, Chrome22 lifecycle/member receipts and4 numerical receipts pass.
Fresh Vulkan independent replay and actual Metal submitted capture retain the
32-lane numerical result. See `bindless-graphics-member-memory-contract.md` and
`D:/arp/build/bindless-graphics-member-root2/current-report.json`. This does not
close production ABI/operations/budgets/lifecycle or the common engine gate.
