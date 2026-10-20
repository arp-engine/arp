# GPU visibility kernel family /1

Code authored and locally compiled, not Backend Validated or Platform Validated.
This family does not change public BindlessCross execution capabilities. The
original Root indirect/state/completion join is still required before delivery.

`pcg/gpu_visibility_layout.h` borrows the production Instance64/DrawGroup112
inputs. Explicit owners permit equal spans shared by material sections; partial
overlap, split owners, mismatched cull semantics and empty spans fail. Independent
visit ceilings cover each group/map/pair traversal. All emitted size arithmetic
uses the shader uint32 domain and actual authored capacities. Failed inspection
zeros its report. Uniform448 has explicit row vectors and eight checked offsets;
it is not a second matrix storage convention.

The Slang family in `shaders/renderer/gpu_visibility.slang` has four entries:

1. Reset the per-owner/per-cascade counters, computation fault and physical
   section count. The original stream must issue exact, bounded dispatch coverage.
2. Cull each original instance once per cascade, resolve its explicit owner,
   apply shared geometry transforms, frustum/distance/HiZ selection, atomic-add
   the owner's counter and scatter inside its assigned span.
3. Emit each original material section at its physical slot. Equal-span aliases
   consume the same owner count. A queried nonzero-firstInstance capability
   selects native base; otherwise the physical base is zero and Mapping16 retains
   the logical instance base for the vertex shader. Each cascade binds its own
   Instance64 slice; firstInstance remains relative to that slice.
4. After a separate emit dependency, finalize all physical slots with a flat
   one-dimensional dispatch. Its immutable output extents come from the real
   buffers: a failed Parameters read cannot hide the slots that must be cleared.
   Any computation fault suppresses every draw and mapping and publishes a zero
   physical count. Emit alone cannot suppress faults discovered concurrently by
   another group. Host coverage must include every draw, base and control slot.

All four scopes and their dependencies must use the existing original inventory,
CommandUsage, resource generations, state journal and native completion. Shader
functions create no barrier, alternate graph, receipt or retirement authority.
Counter additions are bounded by unique authored instances after reset; repeated
or oversized dispatches are not an authorized stream. Scatter also checks local
slots, complete group spans, cascade multiplication and final output addresses.

The combined Control buffer stores a computation fault word and one physical draw
count per cascade. It does not replace the original snapshot fault latch. Public
preflight/readback must retain and consume both. Snapshot/fault/resource/command
and callback budgets continue through their existing independent owners. Control
bytes and Uniform448 are included in the visibility layout report.

Emission preserves holes. A nonempty group count is not a prefix length for these
arguments. Fixed-count indirect consumes all authored slots; an indirect-count
consumer reads the physical section count, including zero-instance holes. A
compacted active-prefix draw requires a separately authenticated producer.

HiZ uses explicit view-projection row intervals over the full sphere. Numerator
and denominator extents use row-vector lengths, so camera rotation/asymmetry
cannot collapse bounds. Near-plane intersections and off-screen footprints remain
visible. Ceil(log2(footprint)) bounds coverage to two cells per axis; four loads
then cover every overlapped cell. Missing coarse mips retain visibility. The
pyramid's existing non-reversed max-depth convention remains unchanged. The wind
envelope is an explicit authored input; no backend-selected padding is supplied.
Disabled HiZ does not remove a statically reflected texture declaration: original
binding validation must still supply the authentic declared resource.

The ordinary publication additionally retains an explicit OutputRanges16 uniform
beside Uniform448. Each entry has its own compiler-authored physical binding map;
authored Vulkan binding numbers must not be assumed to be WGSL binding numbers.
Reset and Finalize query the same complete immutable ranges, including invalid
parameter cases. These range declarations never substitute for real descriptor
capacity or authorization checks.

The ordinary shader compiler reuses its atomic storage lowering. A single-entry
publication may remove whole inactive resources or atomic call sites; it still
rejects malformed remaining declarations, unauthorized atomic operations and
non-atomic accesses left in the target. Entry filtering is explicit and confined
to this family. Physical binding maps remain compiler-authored metadata.

Development checks compile each entry to SPIR-V/MSL/WGSL and compare real SPIR-V
and WGSL buffer strides, Uniform448 size and offsets. This is ABI evidence only.
Numerical equivalence, original fault/readback, same-inventory indirect/vertex
consumption, lifecycle, device budgets and captures await the complete code batch.

The cold native join now has immutable Shader/2 initializer custody in each
original Root Entry. `GpuVisibilityRootInputContract::inspectClosures` borrows
the genuine retained actors for Groups, Instances, Owners and Uniform448; all
four producer closures must retain the same owners. Frozen payload comparison
preserves raw metadata bits and is distinct from a successful GPU shader read.

`GpuVisibilityRootGeometryContract` composes those four closures with the actual
consumer stream, canonical indirect offsets and cascade/group slots, mesh template
index bindings and initialized vertex actors. It derives maximum fetches through
Fetch/3 and checks selected Graphics Root/snapshot correspondence. The original Vulkan/Metal/WebGPU public cold factories now invoke this inspection
before seal creation, using their authentic closures and retained native initialized
actors. The complete Root6 attachment/workload/lifecycle admission remains closed.

The separate closed `gpu_visibility_consumer` package composes those four
compute entries with `visibilityVertexMain` and `visibilityFragmentMain`.
Consumer/1 retains the producer algorithm identity and adds a Vertex-only
Push16 selector (cascade/group/two reserved zero words). Each actual indexed
indirect record must receive its own complete selector. The vertex entry loads
the real Mapping16, checks native/zero-firstInstance addressing, reads the
selected cascade's Instance64 and projects shared instance geometry with explicit
Uniform448 rows. The fragment outputs the authored group color. This is a real
geometry/addressing consumer; it is not the production PBR/terrain shader family.

The compiler authenticates the exact six-entry authored source/schema. Push
coverage is per selected entry: the four compute modules and fragment module
have no push block; the vertex module retains Push16's binary/target proof.
The ordinary append API continues refusing a selected push consumer. Authorized
Graphics composition supplies that consumer's full push interface. An unrelated
vertex declaration cannot prevent appending a genuinely push-free compute entry.

`GpuVisibilityRootConsumerContract` checks those selected entries, actual push
bytes, canonical Groups/Visible/Bases/Parameters and the real command order.
The geometry inspector now composes it before Fetch/3. Closed Native/Emulated
packages generate SPIR-V/MSL/WGSL for the actual six entries; this alone grants
no native recording or completion capability.

Root6 permits ordinary buffer copies through the existing transfer operands and
state journal. Any copy touching snapshot internals remains in the exact fault
profile. Buffer readback Binding/5 associates an explicitly declared storage
range with its real readback destination and independent unpublished result.
It cannot acquire a second fault lease. The original Result contract requires
the entire copy set to share the graph/program, snapshot/authorization and result
quota, with independent requests and copy identities. The original native
custody now retains a bounded readback sequence containing exactly one snapshot
fault binding; additional results use that same domain.

Vulkan/Metal completion code reads every retained result before the one journal
commit and then publishes the complete set. WebGPU storage and map-slot traversal
are bounded by the same result set and retain each actual destination payload.
Public factories now forward the full authenticated result set and preflight all
copy identities and retained owners; their historical execution scope remains
unchanged. Control-specific source/range/order association is composed by the
geometry inspector, now called with actual native closures before seal creation.
Root6 execution and the complete lifecycle join remain unfinished; these branches
are not backend/platform proof.

Program7 and Lifecycle21/22/Completion15 now isolate the produced geometry domain
from historical Content9/Geometry1. CPU contracts require the exact original
Root6/Inputs8/Geometry2 plan and precise copy/map coverage, with no new journal or
publication authority. Native lowering remains refused until genuine producer,
geometry, attachment and completion custody is connected in the three factories.


2026-10-20 development batch: the pure closed visibility ABI and cold inspectors
now live in `rhi/frontend/visibility`, with source-compatible `pcg::` aliases.
Original cold inputs carry a finite produced-geometry correspondence; the three
factories authenticate their real initialized actors before creating the original
seal. No caller certificate, additional inventory, journal or completion issuer
is introduced. This join does not open Root6 execution: native attachment,
workload and lifecycle admission is still unfinished.

WebGPU original buffer mapping now freezes allocation/offset/size and preserves
the actual readback request's finite interval in both native map and read calls.
Retained validation checks all three values; concurrent mappings of one physical
destination are refused. The historical whole-buffer entry remains explicit.
These changes have syntax/contract evidence only until the integrated batch is
frozen and subjected to genuine three-backend validation.

## VisibilityConsumer/2 read-only aliases

Consumer/1 is replaced by Consumer/2; compiled package/source identities must be
regenerated. This is development code, not a new backend/platform acceptance.
All ten indexed tables, snapshot metadata and the original atomic fault latch
are Compute-only. Vertex has four ordinary bindings at logical set2: Groups112,
VisibleInstance64, Mapping16 and Parameters448 (bindings0/1/2/3). The first three
are read-only storage; the last is uniform. They alias the exact original
producer sources with offset0 and their complete original ranges. Aliases add
four physical bindings and no allocation. Including Push16, the closed package
has17 bindings; its switch budget remains18. Actual device budgets still apply.

Four conditions establish the bounded Vertex read domain before recording:

1. Cold native admission authenticates immutable Groups/Instances/Owners/Params
   bytes against real initialized actors and validates the entire layout.
2. The original stream runs precisely Reset, Cull, Emit and Finalize with exact
   dispatch coverage and canonical barriers. No foreign producer or subsequent
   write to these inputs, mappings, visible instances or indirect records is
   accepted. Producer-side guarded loads/stores latch the original fault.
3. Emit produces Mapping16 and Indexed20 from the same section, count, cascade
   and base mode. Failed writes or overflow latch the fault; Finalize observes
   the complete emit dependency and suppresses all physical draw slots on fault.
4. Consumer cold inspection authenticates each Push16 selector against its real
   indirect offset and proves aliases against the same canonical source/range,
   actual selected-entry binary/target/driver layout and original state journal.

Vertex therefore consumes the proven Mapping16 directly through read-only
views; it performs no fallible indexed table operation. No no-op fault reporter,
fragment-delayed fault substitute or writable Vertex storage is emitted. The
compiler independently checks the four actual SPIR-V alias layouts against the
producer records, and rejects writable-storage/atomic WGSL in every Vertex entry.
General indexed Vertex table operations remain refused by
RHI-BINDLESS-VERTEX-FAULT-PROVENANCE-REQUIRED. This closed proof does not authorize
arbitrary table lookups, application shaders or a modified consumer source.

EntryAccess/2 projects the two selected authentic Graphics modules, ordinary
aliases and explicitly inactive retained Compute bindings. It preserves the
complete owner closure; inactive shader consumption grants no exemption from
native bind-group usage-scope validation. EntryAccess/3 independently projects
the four Compute entries with the same retained ordinary alias domains. A
Compute-inactive alias requires the package's explicit target inactivity;
table-wide visibility or logical absence alone is insufficient. Both versions
use the immutable package's target ABI for projection, including Null validation;
the authorization plan remains the independent source of execution permission.
Null only inspects/refuses execution;
Vulkan/Metal map read-only views to their original buffers; WebGPU maps them to
read-only-storage/uniform. Lifecycle21/22 and public Root6 admission remain
closed until the complete implementation batch and integrated validation.
