# WebGPU initialized Geometry/1

This is code in the engine closure batch. Target-toolchain syntax checks and
offline shader generation are development evidence. Browser execution, numerical
readback and lifecycle validation of this revision remain pending the frozen
three-backend validation batch. It is not an engine exit gate.

The extension composes the existing Graphics callback/2, public Execution3,
Closure5 and Lifecycle12. Closure4/6 and Execution2/4 have schema support; the
current numerical fixture covers the sampled 2D Execution3 family. No evidence
is inferred for other combinations. Closure3/Lifecycle9 on Vulkan and Metal keep
their separate native fence contract.

All geometry fields default to zero. A caller must request Geometry/1 in both the
closure and native program and supply InitializedFetch2 in cold admission. Missing
or mismatched opt-in, actors, budgets or profiles fail before recording. Null is
a validator; Vulkan/Metal refuse this callback extension; WebGL refuses bindless.
Legacy requests still reject vertex/index binds and indexed draws.

The public inputs retain authentic typed initialized actors whose exact buffer
leases belong to the canonical inventory. Actors retain frozen bytes and separate
metadata, payload, resource and lease quotas. The closure captures each original
committed-state observation; the original recording seal validates that capture
while Pending. Actor quotas and payload owners enter the original owner budget.
No replacement identity, graph, journal, completion flag or retirement authority
is created. Indexed extrema and complete vertex/instance fetch ranges are checked
using the shared initialized-fetch inspector before native recording.

Pipeline creation validates the selected authenticated Vertex reflection against
the authored streams. Ordinary and bindless pipelines share backend vertex-layout
conversion. Device limits come from the created WebGPU device. Logical stream
slots map to frozen physical layout ordinals; native buffer binds include the
authored offsets and complete retained ranges. Native recording consumes those
frozen records through SetVertexBuffer, SetIndexBuffer and DrawIndexed.

The browser entry `original-root-geometry-execution-only` covers Vertex and
Instance rate with sentinel bytes, vertex offset16, index offset2, uint16 indices,
nonzero firstInstance1, clean/fault outputs and the existing cancellation,
recording/submission error, reversed callbacks, caller drop and logical device
retirement scenarios. It rejects forged actors, insufficient byte/index/owner
budgets and missing opt-in without publishing results. Actual browser execution
is deferred with the rest of this batch.

This initialized CPU-upload profile does not admit GPU-written geometry or
indirect parameters. Those require the separate visibility producer, state,
indirect-fetch and completion closure.
# Concentrated engine batch boundary (2026-10-20)

The original native program now freezes the selected closure, render/compute
pipeline and bind group for each prepared binding. Pending compares these fields
with the same original program; emission uses the selected closure's ordinary
groups. It also contains bounded indexed-indirect payload emission, which remains
unavailable to historical public Program1–6. Root6 producer/content/lifecycle
admission is still required; this addition is not new browser execution evidence.
