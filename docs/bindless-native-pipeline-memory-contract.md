# Native selected Graphics member memory /1

Status: Contract Complete; Backend/Platform Validated for the authored320+16-byte
Graphics workload on actual Metal. No new public execution or Root member-memory
inspection capability is granted.

PackageMemoryLoadRequest/2 freezes original SPIR-V member declarations and
explicit MSL correspondence intent. A selected Graphics pipeline consuming a
requested module must compare that intent against the actual driver reflection
from the exact generated MSL bytes and entry function used for its native
pipeline creation. Every requested ordinary binding must resolve to one original
direct physical buffer slot in its own Vertex/Fragment namespace. An argument
buffer, missing/duplicate slot, absent member graph, unsupported type, exhausted
budget or failed relation refuses pipeline publication. Matching total size is
insufficient.

The existing native payload owns each queried bounded target graph and relation
facts. The existing pipeline retains the original package and selected modules;
there is no new owner identity, store, executor or execution ticket. All metadata
uses core allocator quotas. Failed collection clears all stages before returning
a diagnostic payload; no executable pipeline is published. No caller request
pointer survives loading, creation or publication.

| Backend | Lowering |
| --- | --- |
| Null | Freezes/validates source intent only. No native member proof. |
| Vulkan | Retains source intent; SPIR-V native consumption remains distinct from MSL target proof. No MSL proof is issued. |
| Metal | Original selected package bytes/function, physical stage/slot, actual driver graph and frozen relation are mandatory for requested entries. Legacy entries without a request issue no member proof. |
| WebGPU | Emulated packages continue to require their existing WGSL proof and refuse Native source requests. No MSL proof is issued. |

The next public member-memory query must authenticate the original pipeline,
selected module and Root canonical buffer/range/generation. The existing WGSL
query and range-only DriverQuery remain unchanged. These new private facts do
not replace that Root join or prove production skinning arithmetic, sampling,
lifecycles, budgets or the common engine gate.

Evidence: `build/bindless-native-pipeline-memory1/current-report.json`.
Windows5/5, actual Mac6/6, Wasm/Node source custody1/1, source418/418,
style6/6 and pinned formatter pass. Native target node/member budget failures and
wrong matrix target types refuse with zero residual quota. An invalid unselected
cleanFragment does not poison faultFragment. Original frozen plans survive
caller mutation. Actual public Graphics clean/fault keeps32 exact float lanes,
genuine completion and retirement; push224 numeric/cancellation regression also
passes. A fresh submitted `.gputrace` contains12 hashed files. All8619 source
inputs and built native artifacts were independently rehashed. No independent
Metal replay or production indexed256 arithmetic is claimed.
