# MetalSecondaryHostStorage/1

L2 secondary commands recorded through the CPU command encoder require a
CPU-writable command payload. The producer is the record API, not inferred
from a backend name. Execution still consumes native commands on the GPU;
host recording is not CPU replay. Recorded commands and inherited native
resources remain owned through completion under existing command contracts.

| Backend | Lowering |
| --- | --- |
| Null | Validate recorded command sequence; no native execution claim |
| Vulkan | Host records native secondary command buffers through the command API; existing command-pool ownership |
| Metal | Host records MTLIndirectRenderCommand into a Shared ICB; private storage cannot be accessed with indirectRenderCommandAtIndex. Capability probe uses the same storage and verifies a host command is accessible. Render pipelines explicitly enable supportIndirectCommandBuffers from the executable IndirectCommandBuffers capability so native commands may inherit them |
| WebGPU | Existing unsupported secondary lowering fails explicitly; no promotion of CPU replay or render-bundle capability |

This is the allocation intent of the existing native-secondary lowering,
not a new L2 memory option. The pipeline flag is backend-private, fixed at
creation from the device capability contract, and never inferred by the game.
Metal ICB inheritance supports buffer arguments, including lowered argument
buffer/table roots, but not direct texture/sampler arguments. The generated
Metal binding-space contract determines compatibility before pipeline creation.
An incompatible shader remains valid for direct rendering, with ICB support
explicitly disabled; secondary execution with it rejects before native execution
with `RHI-METAL-SECONDARY-PIPELINE-BINDINGS-UNSUPPORTED`. There is no retry or CPU
replay. General secondary texture support requires an argument-buffer shader
lowering and remains an open C1 gate; this diagnostic is not feature completion.
GPU-authored indirect commands retain their
independent lowering and are not covered by this change. ICB creation or
host-command access failure reports unavailable/creation failure, never a
Dummy or a replay fallback. Bounded command count and device/lifetime gates
remain mandatory. Physical ICB byte-budget accounting remains a separate T4
gate; this correction alone cannot close secondary production readiness.

The WOCC vertex-stream Metal fixture records a real secondary draw and
executes it with inherited streams and push constants across ten frames,
verifying every RGBA output pixel. Run with Metal API Validation: the old
Private allocation aborts at the first CPU command access. Contract/lowering
tests remain necessary; successful allocation alone is insufficient evidence.
