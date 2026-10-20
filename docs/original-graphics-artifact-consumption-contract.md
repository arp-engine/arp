# Original graphics artifact consumption /1

Status: Contract Complete / Prototype Complete / Backend Validated for selected
WebGPU combined-byte consumption /1. Verified 2026-10-05: host19/19,
Wasm/browser8/8 after one targeted Chrome-profile cleanup retry, inventory190/190.
Original graphics memory input /1 is a separate boundary. No graphics accepted
creation receipt, original L2 factory, canonical Pipeline Source, typed draw,
NativeSync, Platform Validated or Production Ready promotion follows from this
contract or a returned native pipeline object.

A backend-private startup factory consumes the genuine OriginalGraphicsPipelineInput
and an explicit complete RenderPipelineDesc. Freeze the full descriptor through
RenderPipelineStateCopy before native construction. Require its shader package,
selected entries and layout declarations to match the input exactly. Retain the
original input in the native issued contract. The combined WGSL bytes must be
passed directly to the existing shader emitter, and the ordinary file factory
and memory factory must share the same render pipeline emitter. File reads,
reflection hydration, YAML reparsing, target copies and per-draw construction are
outside this profile.

The selected /1 profile requires TriangleList, no render-pass compatibility or
nonzero subpass, no enabled depth bias, WebGPU specification sample counts1/4
and vertex stream slots densely ordered as native layout indices. Other state
rejects stable profile diagnostics before native calls. This avoids claiming an
unproven strip-index mapping, sparse stream mapping, fractional integer bias
conversion or subpass lowering. The frozen descriptor preserves inactive fields.

Before any native call, validate complete render state, selected entries against
the original package, exact pipeline layout against reflected bindings, memory
binding-space lowering and the original vertex child's reflected input via
VertexInputConsumptionContract. A caller cannot substitute an ordinary shader
package or unrelated selected entry. Byte size is bounded by an authored target
ceiling; metadata including frozen nested descriptor owners and issued native
facts is charged to an exact authored quota independent of input and artifact
quotas. Failed preparation/native creation releases its charges and returns a
stable diagnostic. There is no dummy pipeline or speculative layout.

The native issued contract owns the exact original graphics input separately
from compute input. It preserves two selected stages sharing one actual native
shader module, exact combined WGSL digest/length, native device/pipeline/layout,
logical-to-physical binding facts and complete graphics compatibility. Hot
validation must verify genuine pipeline/input custody, immutable quota ceilings,
current real device and issued facts without allocation or IO. An ordinary
pipeline observation must reject this original receipt before allocating; only
a subsequent graphics creation-acceptance actor may establish the required real
scope receipts. No new command proof issuer, root, graph, seal, journal or State
is introduced.

Four backend outcomes are explicit: Null is Equivalent ValidatorOnly with no
native execution; Vulkan and Metal are DiagnosticFailure until their own approved
graphics consumption payload exists; WebGPU Native consumption requires an
actual live native device and the approved memory emitter. All four retain
executionAvailable=false. Prototype opt-in is mandatory. Even WebGPU consumption
does not prove asynchronous native creation success. A later actor must join
all actual validation/out-of-memory/internal scope receipts before Ready.

Verification retains fast contracts and Null hidden-frame, plus both browser
modes. Cover exact/one-less/exhausted/retry metadata and target ceiling,
independent/forged/missing owners, wrong selected entry, incorrect shader package,
layout/push/vertex mismatch before native calls, complete authored directory
absence, caller-drop custody, hostile default PMR, hot allocation0, real native
module/digest and graphics facts, blocked ordinary bridge, actual loss and final
quota/native-object retirement. No shader source or visible-output change is
needed for this construction stage. RenderDoc unavailable is recorded explicitly
as RHI-RENDERDOC-TOOL-UNAVAILABLE and cannot prove visible correctness.

Evidence: graphics-consumption-validation.yaml, native-arguments host/web logs,
targeted clean retry log and saved initial/final browser reports. Native cold
peak1124 bytes and source939 bytes are measured from real original input, not
fixed runtime budgets. Native API spies compare the actual UTF-8 code bytes,
two selected entries, shared actual shader module and authored raster target;
negative metadata/profile probes emit zero native shader/pipeline calls. Exact,
one-less, exhausted/retry, hostile PMR, hot allocation0, complete authored-folder
absence and actual-loss retirement of three quotas/two native wrappers pass.
Both modes have all prior mandatory flags true, diagnostics/nativeErrors empty.
An initial negative-push test expected a different validator's diagnostic; the
existing binding-space diagnostic was correct and the test was corrected.
Chrome temporary-profile cleanup briefly raised ENOTEMPTY after a successful
standard-mode report; its targeted rerun passed cleanly. These are recorded test
failures, not production fallback. Graphics real scope acceptance and original
L2/canonical joins remain pending under the creation contract.
