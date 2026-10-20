# Metal ordinary depth GraphicsExecution2

Platform Validated for authored ordinary Fragment depth/comparison: final Mac11/11
and a submitted-only native capture pass. Final Windows3/3, depth codegen1/1,
rebuilt Wasm/Node1/1, source421/style6 and formatting19 pass. Current independent
Vulkan shared-harness capture/replay preserves both Graphics2 comparisons,
three producers and two Compute consumers, with validation0. Scoped evidence:
`build/bindless-metal-ordinary-depth-execution2-1/current-report.json`. The common
engine gate remains false; WOCC remains Traditional.

This backend implements the existing L2 GraphicsExecution2 intent, without a
new journal, content stamp, resource identity or completion issuer. It requires
an authentic selected Fragment Root2/Source6, native Closure16, Content18,
Lifecycle19, Capacity10, depth Pending3, color Pending8 and Submission13.
Indexed images, geometry and ordinary depth outside this selected scope refuse.
The old color execution diagnostic remains Closure1-only.

| Backend | Execution2 and Lifecycle19 |
| --- | --- |
| Null | Validator only; deterministic execution refusal |
| Vulkan | Existing native issuer and fence |
| Metal | Native issuer and actual command-buffer completion; authored Apple execution/capture validated |
| WebGPU | Deterministic refusal until its equivalent path is implemented |

Metal publication additionally requires queried sampled-depth/argument-buffer/
sync/binding limits, original Graphics runtime, and independent Graphics image
opt-in. Compute image opt-in cannot grant Graphics authority. The prepared view
retains intent2; public record/submit reject intent1. Ownership and all budgets
are checked before claiming the one-shot seal. Cancellation must roll back the
same journal; a real successful callback alone publishes fault/readback and
attachment completion. Failed or uncertain completion remains unpublished.

Validation reuses the shared Vulkan/Metal ordinary-depth harness: owner budget
one-less rejection, cancellation after recording, clean/fault submissions,
40 independently expected numeric lanes, exact color and comparison boundaries,
hot allocation checks and physical/metadata quota return. Platform capture and
the previous GI/PBR/Terrain/depth6 cohort remain required. Full-workload budgets,
hardware loss and WOCC scene acceptance remain separate gates.

Native capture omits recorded cancellation, following the existing Metal capture
fixture policy. The first attempt included unsubmitted recorded work and hit
Metal's purgeability assertion; its logs/bundle remain retained. Normal CTest
continues to execute recorded cancellation, checks generation3, and passes11/11;
capture checks generation2 and both clean/fault40-lane/color16 outputs.
