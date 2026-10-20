# NativeFactoryAdmission/1

Contract: NativeFactoryAdmission/1 and UniformRingCompletionAdmission/1.
Implementation/validation status is recorded in rhi-webgl1-validation.md; this
definition alone grants no Backend/Platform/Production readiness.

L2 object creation success requires an executable payload from the issuing device.
Missing runtime, native creation failure and capability rejection must not publish a
placeholder resource, allocation charge, registration or pipeline. A failed native
operation is final; it cannot be retried through a host allocation fallback.
Descriptor arenas may defer native set allocation, but their allocation consumer
must require a successful native descriptor payload. CPU-only layout descriptors
do not claim executable payloads.

| Backend | Production outcome | Explicit diagnostic fixture |
| --- | --- | --- |
| Null | Validator objects; never native evidence | Existing Null contract |
| Vulkan | Reject failed native resource/pipeline creation, independently of the historical requireNativeResourceAllocation flag; default runtime requires an original native owner | Explicit enableSyntheticRuntime=true remains validator-only; platform creation never selects it |
| Metal | Production device factory rejects unavailable native runtime | Separate HostDiagnosticDevice/1 factory; never backend/platform evidence |
| WebGPU | Production device factory rejects unavailable native device | Separate HostDiagnosticDevice/1 factory; never backend/platform evidence |
| WebGL | Graphics/1 production execution remains unavailable until P2 | No host factory promotion |

HostDiagnosticDevice/1 uses an explicit version argument, accepts only version 1,
and is limited to hosts without that backend's native runtime. It exposes existing
diagnostic wrappers for contract tests. Native-capable test runs use the production
factory. No descriptor layout, existing factory signature or serialized enum changes.
Stable diagnostics are RHI-METAL-NATIVE-DEVICE-UNAVAILABLE,
RHI-WEBGPU-NATIVE-DEVICE-UNAVAILABLE, RHI-HOST-DIAGNOSTIC-DEVICE-VERSION,
RHI-HOST-DIAGNOSTIC-DEVICE-NATIVE-RUNTIME and
RHI-VULKAN-NATIVE-CREATION-FAILED. Native errors remain available as secondary
diagnostics. Recovery requires a valid runtime, a corrected descriptor or budget,
or an explicit diagnostic fixture request; production callers cannot opt in by
clearing requireNativeResourceAllocation.

Validation must call the original production factory and resource methods before
the diagnostic fixture. Negative cases include default no-runtime creation,
invalid descriptors, native allocation failure, unknown fixture versions and a
native runtime passed to the host-only fixture. Existing native tests and Null
hidden-frame remain required. Host fixture success does not close P0-09 by itself.

## UniformRingCompletionAdmission/1

The legacy uniform-ring executor has no completion issuer or retained command
ownership input. Submit acceptance cannot authorize cursor reset or slot reuse.
For all five backends this overload rejects before recording or submission with
RHI-FRAME-GRAPH-UNIFORM-RING-COMPLETION-SOURCE-UNAVAILABLE. The public legacy
onFrameComplete(slot) cannot authenticate completion and throws
RHI-FRAME-GRAPH-UNIFORM-RING-COMPLETION-UNAUTHENTICATED without changing cursors.
Null does not manufacture permission for this production interface. Its ordinary
non-ring executor and existing fence paths remain independently validated.

No in-tree production caller constructs this ring. P4's ring integration must
provide an original completion or ordered-reuse issuer, bounded ownership and
failure handling before reenabling this overload. This admission fix requires
no alias group implementation and does not claim to implement that P4 feature.

## Graphics/1 execution entry audit

| Entry and authority | Actual consumer / refusal | Verification |
| --- | --- | --- |
| Runtime identity, queried limits, versions, extension opt-in | WebGlRuntimeProfile::admit/admitWithin leaves executable=false even when compatible; malformed versions, unavailable/unenabled extensions and budgets fail earlier | rhi_webgl_profile_contract_tests; dispatch decision fixture |
| Production platform factory | createRenderSystem consumes RenderSystemBackendSelection before touching a window/device; WebGL remains RHI-WEBGL1-RENDERSYSTEM-UNAVAILABLE | Original platform factory invoked by the Vulkan hidden-smoke executable's --feature-gate-only mode; five-backend/unknown route tests |
| Shader artifact and reflection | Existing native package validation precedes pipeline allocation; WebGL production target has no approved executable artifact before P1 | shader-target contract; original Vulkan factory rejects missing package/SPIR-V/reflection and wrong AtomicFullBlockPush version without shader/pipeline creation |
| Native device/resources | Metal/WebGPU public factories require actual runtime availability. Vulkan buffer/image/sampler/pipeline failure never falls through to a host wrapper; arenas require native set creation; foreign views and no-runtime swapchain/sync reject | Original factory negatives; explicit host fixture versions; real Vulkan native error injection and real payload retry |
| Prepare/record | lowerCommandWorkload and native pipeline lowering reject WebGL even with another backend's feature vocabulary/query facts or requested CpuReplay. No WebGL GpuDevice factory is installed | profile and dispatch tests; existing native record consumers require issuing registry and native command payload |
| Submit | Vulkan rejects foreign/absent native command payload before acceptance; Metal queue requires native command and sync identities; WebGPU generic submit requires its completion-resource graph and rejects unsupported timelines | Original Vulkan queue negative; existing Metal/WebGPU device contracts and ordinary submission gates |

This closes only the current Graphics/1 default admission definition and its
reachable entry guards after validation. It grants no WebGL payload, extension,
completion, browser or production readiness. P1/P2 must replace each currently
denied entry with its approved artifact/source/payload consumer and retain the
negative tests. describe*Device reports remain descriptor/metadata validation,
not evidence that create*Device succeeded. Host fixture outputs and early-return
native sections are not counted as Apple/browser execution.

The private device construction mode is immutable. WebGPU's production payload
policy remains required even if the runtime disappears after device creation;
its host fixture never promotes itself when a native device appears. Metal's
creation consumers reject a runtime/mode mismatch before allocating objects.
The fixture mode does not alter any public descriptor or L2 object layout.
