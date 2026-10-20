# WebGlExecutionQuality/1

L2 defines bounded prepared command/push/uniform/owner storage, explicit client
upload lifetime, complete state issue and observable execution outcomes. This
contract adds observation; it does not enable a device capability or grant native
execution. Existing original context/resource/pipeline/command gates still apply.

| Backend | Conclusion |
|---|---|
| Null | Validator only; native bridge observations unavailable |
| Vulkan | DiagnosticFailure for this WebGL-specific report; native telemetry unchanged |
| Metal | DiagnosticFailure for this WebGL-specific report; native telemetry unchanged |
| WebGPU | DiagnosticFailure for this WebGL-specific report; native telemetry unchanged |
| WebGL | Prototype original-domain fixed-size ledger and exact GL issue/update observations |

Prepared storage is cold, quota-bound and fully allocated before recording. No
record/draw/submit/update rescue allocation, shader compilation or pipeline
creation is allowed. Per-workload preparation may allocate within its explicit
quota; reporting must distinguish preparation allocations from allocation-free
record/issue/update. The pre-existing L2 QueueSubmitResult owns its diagnostic
string: rejected submissions are a separate error path, may allocate one bounded
static-diagnostic string, and are measured separately from successful issue. This
is not rescue storage for a command or native payload. Fixed working-set stability is a separate assertion. CPU
metadata, CPU upload shadow and logical texel/buffer bytes retain distinct units;
none is actual GPU allocation. Quota/live-object peaks come from original owners.

Submission outcomes are Accepted, Failed and Lost. Accepted means native issue
completed without a diagnostic, never GPU completion. Failed preserves accepted
operation count and possible native side effects; Lost means the original context
epoch is no longer current when the submission returns. Repeated or foreign work
fails before issue. Counters include attempts at the real submit entry; unsupported
queue sync overloads rejected before that entry are outside this counter.

The native-call counter counts actual GL entry invocations during submit, client
buffer/texture updates and canvas present, including glGetError. It excludes cold
creation/requery/destruction, test oracles, and non-GL Emscripten context/lifecycle
predicates. Draw/uniform/update counts measure attempted calls; uniform bytes are
the bytes passed to native uniform entry points, including sampler integers and
excluding driver copies. Upload bytes exclude authored row padding. These are
not GPU bandwidth or completion metrics. No count is inferred from a backend name
or a draw descriptor; it advances at the actual call site.

Telemetry uses fixed scalar storage and checked addition through the shared core
budget arithmetic. Overflow latches a stable diagnostic and marks the report
incomplete; counters never wrap or silently reset, and observation cannot change
already issued GPU work. No dynamic diagnostic history or per-event allocation.
Snapshots remain available after loss/CPU retirement, with original generation
and retained CPU owners. Restoration creates a new independent domain ledger.

All issue state is authored from the immutable complete L2 pipeline, original
reflection and current pass descriptors; the backend does not assume a persistent
native state cache. Browser acceptance must poison program/FBO/attributes,
textures/samplers/pixel-store/scissor/masks and compare actual GL calls with the
original ledger. Blocking parameter/location queries, readPixels, finish, shader
compile/link and object creation are forbidden in record/issue/update scopes.
Cold test readback and RenderDoc remain outside those scopes.

Budget negatives cover exact, one-less, exhausted and overflow without native
side effects or quota leaks. Quick contract and Null smoke plus real browser
success/failure/loss/restore and RenderDoc/AI image verification are required.
Platform Validated and Production Ready remain separate gates.

## Acceptance 2026-10-19

Contract Complete / Prototype Complete; Backend Validated only for the tested
Chrome WebGL1 / ANGLE D3D11 execution chain. See
`build/webgl1-evidence/p3/quality-native-final-1/checkpoint.yaml` and
`docs/rhi-webgl1-validation.md`. Platform Validated and Production Ready remain
open. The Failed root build manifest is preserved: its compilation and source
custody passed; browser teardown failed, and six modes subsequently passed against
the exact same bundle. No rebuild or successful status was substituted into that
historical manifest.
