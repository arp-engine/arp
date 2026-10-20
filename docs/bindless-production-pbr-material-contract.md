> Current execution-boundary update (2026-10-20): public SceneContent/1 requires
> initialized shader/geometry custody and explicit WebGPU attachment opt-in.
> The WebGPU original program now composes SceneRaster/1 with its existing full
> HDR/depth pass checker; legacy empty-scene programs retain their old scope.
> Pending revalidation retains original immutable byte admission and does not
> repeat committed-state reads. This is implementation evidence, not a completed
> public scene numerical or platform acceptance result.
>
> Scene input construction now reuses the original Root/inventory builder with
> typed buffer-lease factories. Material records are encoded against the actual
> candidate snapshot before sealing; immutable platform inputs use the public
> Shader/2 initializer and keep its original actor/lease. Shared family data
> covers two PBR materials and independent samplers, TerrainMaterial240/Page32,
> Frame400, Palette32864, Instance64 and Parameters64. Null composition does not
> create or impersonate a native initialized actor. Positive public scene
> admission, numerical readback and lifecycle validation remain open.
> Reflection-packed FamilyInput now shares its actual vertex/index bytes and
> public Geometry/1 initialization factory across the five families. The shared
> Graphics stream composes Vertex-only Draw16, zero binding offsets/firstInstance,
> HDR color and D32 depth/shadow passes. Null verifies authentic Inputs7 and
> explicitly refuses native member-memory/push recording proof. Emulated Null
> reflection projects the accesses, but graph admission rejects the independent
> native push-recording intent before allocating a graph. Neither mode creates
> a native content transaction.
> BufferReadbackBinding/6 explicitly joins direct Root5/Inputs7 graphics results
> and fault to the original graph/program/journal. Binding/5 remains restricted
> to Root6/Inputs8; version 6 cannot authorize produced/indirect workloads.
> Only declared storage-buffer source ranges can be read, with exact destination,
> generation, copy coverage and Pending custody checks. Four-backend lowering
> grants CPU correspondence only; WebGL rejects the profile. The shared color
> stream now includes the 23-lane-per-pixel result copy and original state
> transitions, with a joint result/fault input for completion. Depth/shadow do
> not write that buffer and retain fault-only results; attachment readback and
> positive native scene/lifecycle acceptance remain open.
>
> WebGL1 is an explicit rejected workload: no BindlessCross discovery, no
> SceneRaster/EntryAccess/RootSequence/Program/produced execution permission.
> A Traditional selection is not admission for the full WOCC workload.
>
> Current implementation batch (2026-10-20): shared `GeometryFamily/1` now lives in
> `shaders/renderer/bindless_pbr_geometry.slang`; mesh/skin/grass/canopy share their
> actual color/depth/shadow deformation. `TerrainGeometry/1` composes that geometry
> with TerrainMaterial240/Page32 and explicit Parameters64, including all 27 material
> textures and the two independent array weight pages. The new families are code and
> offline-compilation work, not a public execution or integrated platform grant.
> Current work and remaining gates: `D:/wocc/docs/bindless-engine-closure-batch.md`.

# Production PBR material sampling workload /1

## Current Native direct Traditional validation (2026-10-19)

Current scoped increment (2026-10-19): Native direct Traditional PBR/Terrain27
parity is Platform Validated for the authored workloads. An ordinary public-RHI
reference executes implicit sampling before the public Root clean/fault draws;
128 numeric lanes, uploaded mip/layer bytes and four color pixels are compared.
Vulkan independent RenderDoc replay passes both workloads with validation0.
Metal genuine completion/readback and two captures pass; no independent Metal
replay is claimed. Windows regression14/14, final Vulkan GPU2/2 and Mac18/18
pass. The final Apple epoch has9258 source inputs/271 artifacts;33 capture files
are hash verified. Upload staging uses exact derived two-mip/layer capacity.
No runtime capability is expanded. This is not full production shader/scene
parity or integrated three-backend acceptance. Common gate=false;
WOCC=Traditional; renderer migration has not started.
Evidence: `D:/arp/build/bindless-native-traditional-parity1/current-report.json`.

Next: complete workload/device/in-flight budgets, production pass semantics,
cross-pass atomic/indirect consumption and remaining lifecycle; then the same
integrated three-backend gate -> renderer/WOCC -> GPU-driven -> final acceptance.


Status: implementation in progress. The common production engine gate is false;
WOCC remains Traditional. No new execution capability is introduced.

The source of material semantics is the current WOCC `assets/shaders/pbr_scene.slang`:
baseColorAlpha, pbrParams, pbrExtraParams, the explicit authored-leaf overload of
environmentParams.x, and five independently addressed sampled texture roles.
There is no production GPU material table today. The 96-byte record below is an
explicit test transport of those inputs and resource selectors, not an invented
production material ABI or renderer migration. Frame GI/debug/temporal state is
not inferred as material identity.

The reusable core shader evaluates base RGB/alpha, metallic saturation, roughness
clamp [0.045,1], AO strength saturation, emissive strength, alpha-mode coverage,
authored leaf dielectric F0/translucency/saturation, and orthonormal tangent-space
normal mapping with the current single-sided/back-face convention. Leaf HSV
arithmetic reuses core/leaf_surface.slang. Lighting, weather, GI, temporal output,
wind, and complete color/depth/shadow scene equivalence are separate requirements.

The fixture transports four float4 material lanes followed by two uint4 resource
selector lanes, offsets 0/16/32/48/64/80, stride96. A runtime buffer contains a
sentinel and two selected records; a separate uint4 probe supplies selection.
The fixture is finite by declaration: ten texture descriptors (two sets of five
roles), two independent samplers, and one writable numeric-result buffer. These
are test demand counts, never production device capacities. All allocations,
reserved descriptors, snapshot/fault owners and device limits still use original
admission. Failure must not partially publish work or lend execution authority.

Sampling-coordinate ddx/ddy must execute before material-index dispatch, any
resource-index switch, fault branch or coverage discard. Every role uses the
actual UV gradients. Native non-uniform resource indexing and Emulated scalar
switch dispatch must preserve mip selection, sampler selection and alpha
semantics. Fixed LOD, derivative diagnostics suppression, hidden batching and
quality reduction are forbidden. Existing generated bounds/fault checks apply.

## Five-role implicit reference, filtering and discard /2

The same record96/runtime288 and Selection16 test transport is retained. Probe
Selection.z=1 requests actual coverage discard. All five role samples and two
independent sampler probes execute before material dispatch/discard; numeric
storage writes occur before discard so both covered and rejected material lanes
remain observable. The rejected right column retains explicit black/alpha1
attachment clear; the left column writes (0.25,0.5,0.75,1).

Ten 2x2/1x1 mip chains retain the distinct mip1 colors. Mip0 corner p/channel c
is `(255 - mip1[c] + 17*p) % 256`, p=0..3 in row-major order. Five role UVs
are pixel-position xy, with gradients1 selecting mip1. An additional Image0
probe uses UV `0.5 + position.xy*0.125` and gradients0.125, selecting mip0.
Lanes24..27 are nearest.r, linear.r, nearest.g, linear.g; they replace the
redundant base sample copy. All other numerical lanes retain their meaning.
The nonconstant corners distinguish nearest and bilinear filtering at every
pixel, independently of the role mip1 check. Fixed LOD or sampler substitution
must change observable output and fail acceptance.

An independent Traditional oracle binds ten ordinary textures/two samplers and
the same authored material/selection/result bytes. Its Slang Fragment executes
twelve real implicit Sample operations before non-uniform selection and one
actual discard after numeric writes. Normal sealed ordinary Graphics module
publication must preserve implicit WGSL and SPIR-V sampling, selected-stage
reflection and row-major layout. The oracle reuses core material arithmetic;
the existing independent CPU oracle checks every output lane and uploaded byte.
Its raw test-only GPU commands never grant public RHI execution authority.

Null retains original ABI/range/owner refusal validation. Vulkan/Metal consume
the existing Native sampleGrad/resource/sampler paths; WebGPU consumes the
existing scalar-switch sampleGrad path with precomputed role/probe gradients.
No operation, resource domain, production ABI, capacity or capability expands.
Required evidence is authentic public Root completion/fault/retirement, matching
numeric/pixel receipts and the independent implicit oracle. Missing receipts,
resealed explicit-LOD shaders, changed sampler/filter values, omitted discard,
wrong bytes and native errors must fail. Metal requires actual current Mac
execution; compiler success cannot close that platform gate.

| Backend | Required mapping / refusal |
| --- | --- |
| Null | Original package/member/range validator only. No native execution claim. Unsupported/mismatched framing and undersized ranges refuse. |
| Vulkan | Existing original Root/SPIR-V Native non-uniform indexing, runtime element96 and canonical bound range. Unsupported operation/stage/queried budgets refuse before recording. |
| Metal | Existing original Root/MSL Native argument-buffer resources and selected driver RuntimeElement/Block memory proof. Missing proof/residency/limits refuse. |
| WebGPU | Existing original Root/WGSL runtime-array proof and bounded scalar texture/sampler switch with precomputed gradients. Missing equivalence or binding/stage/budget capacity refuses before recording. |

Acceptance requires cross-target sealed Slang publication and negative contract
checks, host ABI and independent arithmetic oracle, original Null validation,
then actual Vulkan/Metal/Chrome numeric/fault/completion/retirement execution and
capture/readback. A Traditional implicit-sampling reference must verify the same
five-role workload, including mip and alpha boundaries. Declaration success alone
does not close sampling or the common production engine gate.

## Geometry ordinary matrix declarations

GeometryFamily/1 keeps Frame400 and TemporalPalette32864 in the same original
ordinary descriptor domains as its selected Vertex/Fragment entries. Package
record verification compares both descriptor set and binding; an ordinary
binding with the same number cannot be interpreted as a primary table record.

NativeBufferMemoryLoadRequest/3 extends the existing /2 relation with explicit
target member/array paths. Source matrix identities and all path steps are
frozen by the original package. Metal resolves these paths against the actual
driver-collected buffer graph before its existing complete memory comparison.
It never guesses driver type IDs or accepts declaration size alone. Missing
members, wrong array/matrix kinds, exhausted step/allocation budgets and partial
resolution refuse with cleared outputs. This happens during cold pipeline
preparation, under the existing allocator and metadata quota; Pending retains
neither requests nor borrowed path spans.

Null checks declaration structure only. Vulkan consumes its authenticated
SPIR-V binary identity. Metal needs the real driver relation described above.
WebGPU keeps its separate explicit SPIR-V/WGSL matrix-column relation. These
declaration proofs grant no backend capability, command or completion authority.
The existing sampled/array Graphics4 scope is evaluated for Emulated geometry;
the unrelated OrdinaryColor13 primary-buffer scope is not widened.

## Native direct Traditional reference protocol

Native parity uses the existing ordinary_pbr_material graphics publication and
the public ordinary RHI. A shared test fixture authors the two descriptor sets,
hydrates the generated artifact metadata, uploads the same two-mip images and
material/selection bytes, and records an ordinary render pass with no Root,
snapshot, authorization or bindless prepared binding. A genuine queue fence must
complete before reading the numeric result and attachment. Numeric output is
checked independently against the CPU oracle, then compared with every clean
and fault Root result at the existing tolerance. Attachment pixels must retain
the authored covered/discarded columns. Readback is diagnostic test observation;
it cannot issue content, state or execution authority.

The same protocol applies to ordinary_terrain27:25 individual2D images and two
two-layer array pages, both mips, one sampler, the original96+16 buffer bytes
and128 numeric lanes. Vulkan and Metal use their existing ordinary descriptor,
upload, render, transfer and real completion mappings. Null remains a validator;
WebGPU retains its independently executed browser Traditional reference. No
runtime version or capability changes. This protocol is pending native execution
until the corresponding current target, GPU readback and capture pass.

## Current implementation evidence (2026-10-17, incomplete)

Native/Emulated Slang publications compile for SPIR-V/MSL/WGSL. Windows host,
Null layout/owner/range and compiler refusal checks pass; Wasm/Node PBR data,
memory, compiler and Instance64 memory regression pass4/4. Latest source guards
pass421/421 and style6/6. These are scoped checks, not production admission.

Vulkan actual public clean/fault execution verifies128 numeric lanes, real
completion and zero-residual retirement in a standalone diagnostic run
(`build/bindless-production-pbr-material-1/vulkan-diagnostic9.json`,238 seconds).
Earlier registered60-second Debug attempts timed out; the current scope3
acceptance below passes in7.99s. The full retained shared
cold conformance matrix is expensive at this ten-texture demand; no timeout was
increased. Those earlier epochs do not certify the current acceptance. Independent RenderDoc capture
and replay now verify both clean/fault draws,128 numeric lanes, exact ordinary
bytes, all10 authored texture mip chains, color and fault0/1, with validation0
(`build/bindless-production-pbr-material-1/vulkan/replay.json`). The canonical query measured137 transaction owners;11 content
controls and one stream need149. The fixture now explicitly reserves one
content-control owner per registered image plus the original stream, rather
than inheriting the smaller workload's145-owner ceiling.

Metal built on the actual Mac; its latest transferred epoch passes7/8 tests.
The PBR snapshot failed because the test device omitted image opt-in. That local
fix and the latest range/owner changes await transfer and execution; SSH
currently times out. Earlier Instance64 Metal acceptance retains its own epoch.

The browser PBR workload is now wired into the original executor and genuine
canonical texture uploads. Its first actual execution refused the old native
owner ceiling, which reserved parents for only two images. The caller now
projects one parent per authored sampled image, both attachment owners and
ordinary group owners. The second run passed closure admission and then refused
the callback owner ceiling. Full caller custody is now projected from typed
source counts and the existing seal/reservation, native closure, readback,
result storage, command, callback-slot and issuer contracts. The third execution produced8/22 lifecycle execution receipts and2/4
128-lane numeric receipts before the unchanged480-second workload deadline.
Its terminal failure and browser artifacts are retained in
`build/bindless-production-pbr-material-1/web3`; the complete browser suite
did not pass. Six fresh native CPU stack samples locate repeated complete
graph validation in private transaction plan population, once per buffer/cell
timeline query. `command-state-plan-validation-contract.md` defines the
serialized common-frontend scope; implementation now validates the original
graph once at private traversal entry and reuses existing private resolvers.
Public timeline queries remain guarded. Scope1 Windows regression12/13 and
scope2 Vulkan1/2 retain the PBR60s timeout. Fourth browser scope1 execution
reached9/22 lifecycle and2/4 numeric receipts before480s timeout; fresh failure
report, runner and evidence hashes are archived in `web4`. Profile12 samples
locate remaining sequence per-resource Root validation and ordinary per-binding
range/member revalidation. The current scope3 change extends the same private
synchronous admission scope to those existing walkers/readers, preserving public
version/owner/generation/output checks and all range/member evidence. Source421,
style6 and formatter pass. Current registered Vulkan passes2/2: Instance64
1.49s and PBR7.99s under the unchanged60s deadline. Fresh capture/replay in
`vulkan-plan-scope3` verifies both clean/fault128-lane draws, exact ordinary
bytes,10 mip chains, color/fault and validation0. Current Windows
cold/Root/range/Null regression passes14/14. Current Wasm/Node4/4 passes.
Actual Chrome PBR22/22 lifecycle flows and4/4 numeric128 receipts pass in85.50s
under the unchanged480s deadline; matrix/Instance64/skinning regression adds66
flows/12 numeric receipts. All four browser reports have empty diagnostics /
nativeErrors. Successful web5 artifacts and runner hashes are retained; the
scoped verifier/report is `plan-scope3-report.json`. Current Metal rerun remains
pending; the Mac peer currently reports offline.

Current parity /2 evidence supersedes only the affected authored PBR input:
Windows reference/Native GPU/data/memory/codegen5/5 passes (Vulkan8.79s, same60s
deadline). Fresh independent RenderDoc replay verifies clean/fault128 lanes,
exact288+16 inputs, ten mip chains, nearest/linear probes and actual discarded
right-column pixels/fault with validation0. Nine SPIR-V files validate. New
hostile reference5/5 and original reference regression5/5 pass. Evidence:
`build/bindless-production-pbr-parity-1`. Rebuilt Windows Instance64/Null regression2/2 and
Wasm/Node/reference6/6 pass. Actual Chrome PBR passes22/22 lifecycle workflows,
4/4 numeric receipts and genuine independent Traditional implicit GPU/CPU/Root
128-lane +16-byte pixel comparison in85.04s (unchanged480s deadline). Original
sampled/array implicit references and matrix/instance/skinning browser regression
passes5/5,110 workflows/20 numeric receipts. All browser diagnostics/nativeErrors
are empty. The30 affected input hashes and explicit outputs are verified in
`current-report.json`; this is not whole-tree integrated certification. Current
Metal execution/capture remains pending; earlier Metal inputs do not certify
this increment. Production material table, terrain27, full operation /
budget/platform gates and scene pass equivalence remain open.
