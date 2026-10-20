# Original Graphics Pending read composition

This isolated successor fixes a missing Closure10 retained-read join before
actual Graphics8 recording. It does not grant submission, shader output or public
bindless authorization. Live sources, Callback24/frozen6/registration, original
libraries/payloads and all capacity/memory/deadline limits remain unchanged.

L2 buffer Pending/4 adds ImmutableGraphicsShaderBufferRead4 as a distinct use.
It requires the authenticated finite shader-resource root with ContentPlan6;
the original mixed-content counts must include completed sampled controls,
attachment controls, render-binding reads and draw reads. Captured, initial,
final and journal Pending buffer states are all ShaderRead. The same original
buffer registration/control/generation/revision and sole journal remain mandatory.
Buffer Pending/1-3 retain their exact use/profile/state decisions.

Completed texture Pending/3 selects the same authenticated finite ContentPlan6.
It preserves the existing completed parent/control/generation/revision and whole
CopyDst-to-ShaderRead transition, with unchanged completed sampled content and no
sampled content proposal/write. Attachment proposals belong to the independent
mixed plan and never become completed shader inputs. Pending/1-2 and standalone
completed texture read-plan versions4/5 retain their decisions; /6 is not added
to the standalone read-plan schema.

The original recording seal requires Lifecycle5 for both new Pending uses;
Lifecycle1-4 cannot borrow them. No new seal, journal, receipt, authority, graph,
allocation or owner demand is introduced. Missing opt-in, mismatched legacy/root
profiles, stale or forged captures, wrong state/range/stage/control and expired
owners continue to fail before encoding. Pending is not public Prepared success.

| Backend | General Pending read lowering/3 | Recording / submission authority |
|---|---|---|
| Null | Validator only; no native payload | None |
| Vulkan | DiagnosticFailure; Graphics retained-read join missing | None |
| Metal | DiagnosticFailure; Graphics retained-read join missing | None |
| WebGPU | Explicit private Prototype retained-read validation | None from this lowering |

The backend consumes this decision only for exact Closure10. Its immutable buffer
and completed texture callbacks reuse the original seal/root validation. Closure8
continues BufferPending2/TexturePending1, Closure9 BufferPending3/TexturePending2,
and geometry closures continue their original geometry Pending use. Existing
Execution8 record-only lowering remains separate and submission stays closed.

Required order: assertion-enabled L2 tests including exhaustive profile/use/state
and legacy differential checks; four-backend lowering tests; copied seal/root
integration; actual backend TU compilation and affected native objects/library
linking; real browser Pending/cancel/drop/error/drain/retirement after genuine
Compute final/aggregate and Callback96 acceptance. Compile or Null results cannot
prove native execution. Original engine integration, Vulkan/Metal Native domains,
renderer/WOCC P4/P5 and P6 remain required.
