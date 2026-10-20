# DdgiRecordingFailure/1

DDGI's void record methods execute inside FrameGraph's exception boundary.
They must throw an owned core::DiagnosticError on the first failed required
resource/view, transition, descriptor write, pass begin, pipeline/binding,
push constants, timestamp, copy, dispatch or pass end. They must not return as
if recording succeeded, issue later work, commit descriptor cache success, or
advance history after such a failure. Uninitialized/destroyed renderers reject
before dereferencing resources. Existing nonempty diagnostics take precedence;
an operation-specific stable diagnostic is required for APIs without a result.

L2 semantics do not change: invalid resources, layouts and command state still
fail their original validators; Native completion and ownership are unchanged.
Lowering is explicit for all four backends: Null validates and reports errors
without native output; Vulkan, Metal and WebGPU propagate their native/validator
results without substituting payloads or changing capabilities. Unsupported
operations remain deterministic failures. Native validation of each backend is
tracked separately from this shared renderer behavior.

FrameGraph abandons an unsubmitted failed recording, retains earlier accepted
work until completion, and does not invoke PassSubmissionObserver for the failed
attempt. Open encoders unwind through existing backend ownership; no cleanup
error may replace the first error. Descriptor caches publish a valid signature
only after every required write has succeeded, and are invalidated before a
partial update so a later retry cannot use mixed bindings as a cache hit.
Resolve retries may already have the probe state buffer in ShaderRead. They
must avoid a redundant read-to-read state change; this does not omit a required
write dependency, nor relax any backend's resource-state validator.

WOCC retains the original error in strict readback's first-error state. Tests
separate malformed resources from injected driver errors: depth presented as a
normal texture and a destroyed material view are real RHI validation negatives,
not claims of driver allocation failure. Full app, negative controls, and fast
tests must prove failures stop recording/submission and keep histories unchanged.
Scheduler/temporal candidate state and native loss/recovery are separate work.
