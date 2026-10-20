# WebGlDeviceComposition/1

This is the next P2 integration batch, after PreparedRenderCommands/1. It does
not amend P0 admission or open the production factory. An explicit versioned
Prototype request composes one original WebGlResources owner into GpuDevice.
Null validates only; Vulkan/Metal/WebGPU reject this WebGL-specific composition
and keep their original device implementations. Host creation rejects without
a native runtime. No Null delegate, native placeholder or completion is allowed.

The device, its existing PipelineCache and DescriptorCache are cold allocated
under an independent caller-sized CPU quota. Both caches require explicit
capacity policies. Resource/upload quotas remain separate. The device exclusively
owns the original context/resource admission lifetime. Retained resources and
commands preserve native ownership but cannot execute after device destruction.

Ordinary buffer/texture/view/sampler/layout/arena factories use the original
resource implementation. Pipeline creation requires prior explicit selection of
an original P1 publication, shared original layout and complete versioned state
input; normal L2 reflection/state validation still applies. Missing or mismatched
inputs reject, never infer an artifact from a filename. Selection is cold and
does not compile a pipeline during recording.

Command preparation cold-allocates a real encoder using the declared original
pipeline/FBO and exact command quota. The ordinary createCommandEncoder factory
transfers that encoder once, with no hot allocation; unsupported additional L2
retention/bindless reservations reject without consuming it. Queue submission is
the original real queue. No wrapper substitutes for native work.

Compute, placed memory, arbitrary buffer views and secondary/bundle/query/sync
reject with stable diagnostics. Canvas creation now consumes WebGlCanvasLease/1,
with its own queried attributes, original image owner and single-use boundary. Generic graphics,
NativeSync and physical budget claims remain unavailable in this Prototype.
MemoryStats counts actual retained logical storage, not queried driver memory.
waitIdle issues no blocking call and records completion-unavailable; shutdown
drain retains its deterministic unsupported result.

Acceptance requires a real browser workload through a GpuDevice reference,
explicit negative factory tests, independent quota rejection, immutable draw
execution, device destruction invalidation, a host quick contract and Null smoke.
The multi-pass/state and single-use canvas/root runtime component integrations
now have native browser evidence. P2 is accepted for this explicitly opted-in single-frame component. Generic
production factory, RenderSystem and engine capability integration belong to P4;
this contract does not authorize them.

PreparedRenderCommands/2 adds an explicitly bounded cold pipeline catalog.
All catalog pipelines must share the exact original artifact and layout owners
and identical vertex stream declarations. Raster/depth/stencil/blend state may
differ. Each draw freezes its original pipeline; switching does not compile,
query native state, grow storage or reinterpret bindings. An undeclared or
ABI-incompatible pipeline rejects before recording changes. The shared original
uniform snapshot layout remains valid; native locations come from the frozen
draw pipeline's own program. Version 1 retains its single-pipeline boundary.

Compile/link failures retain SHA-256 of the exact original program identity and
both submitted ES100 sources, the failing stage, and the real native log. The
existing maxDiagnosticBytes bounds the entire report. Insufficient report or log
capacity returns the explicit diagnostic-budget failure; no truncated log is
presented as complete and no failed native program is published.

PreparedRenderCommands/3 adds a caller-bounded FBO/pass/texture-transition
inventory. Pass handles carry their own ordinal; an old ended handle cannot
operate on a later pass. Textures and buffers transition only outside passes, over their
whole declared range, through the existing L2 state/access validators and
original RecordedTextureStateControl/RecordedResourceStateControl/MutationJournal. ES2 ordered rendering
provides the equivalent attachment-to-sampling dependency; no native barrier
or GPU completion is fabricated. Buffer state is simulated at each draw, including transitions after the draw; final state cannot stand in for its earlier readable state. Transition records retain original resources
and an exact position before a native operation.

Before any native call, the complete command simulates every transition,
attachment Load/Store initialization, content revision and sampled read in
issue order using fixed cold storage. Actual final journal ownership/revisions
must match the sealed record. A previous pass's Clear/Store may initialize a
later sampled input; DontCare, feedback and missing state transitions reject.
Only reads retained by this same command can coexist with an earlier attachment
write; another command's retained reader rejects that write. Native content
revisions advance at each actual pass end; no CPU shadow pixels are generated.
Recording abandonment rolls back the original state journal. Native failure
invalidates affected attachment/state validity and retires the admission domain;
accepted operation counts retain their original meaning. Version 1/2 limits
remain unchanged. Generic production, multiframe and completion claims stay closed.
