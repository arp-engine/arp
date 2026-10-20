# WebGL platform integration

## Plan

1. Contract Complete: define OrderedRenderSystem/1 creation, budgets, pipeline loading,
   failure gates and five-backend outcomes; retain a fast MinGW contract test.
   Profile, backend selection and RenderSystem MinGW contract tests passed (3/3).
2. Prototype Complete: implement the platform-owned WebGL factory behind createRenderSystem,
   keeping original RHI runtime/resource/issuer ownership. Expose only L2 device,
   ordered workload, pipeline creation and explicit lifecycle capabilities.
   The public factory/pipeline API and migrated Queen game compile to Release Wasm.
3. Platform Validated (Windows Chrome): migrate Queen off backend headers and validate negative creation,
   resize, suspension/resumption and a full browser game without WebGPU.
   Independent platform smoke passed six real GPU frames, zero extent, resize,
   explicit pause/resume, native API rejection and complete event teardown.
   Queen won a full level (score 88), advanced, restarted and minimized/restored.
4. Backend Validated / Platform Validated (Windows Chrome ANGLE D3D11):
   RenderDoc running/result captures replayed successfully. AI inspected both
   replay images. Pinned clang-format 21.1.8 checks passed. Native MinGW platform
   and Queen builds, three contract tests and Queen Null smoke also passed.

## Contract

`RenderSystemDesc::orderedGraphics` selects a versioned, explicit Prototype
profile. A matching compiled WebGL platform factory consumes that profile; no
profile means the generic WebGL production factory stays closed. Defaults never
enable extensions. Sampled depth requires caller opt-in and a real native query.
The platform maps declared logical/CPU budgets into existing RHI ownership
contracts. The backend still owns contexts, GL objects, identity and execution.

The public pipeline loader consumes an exact artifact path, bounded load/compile
budgets and complete L2 layout/state. It validates the original artifact and
reflection through existing shader/RHI contracts before creating a pipeline.
There is no filename guess, source compilation fallback or native handle exposure.

| Backend | OrderedRenderSystem/1 |
| --- | --- |
| Null | Validator only; creation rejects with stable diagnostic |
| Vulkan | DiagnosticFailure; native execution profile remains separate |
| Metal | DiagnosticFailure; native execution profile remains separate |
| WebGPU | DiagnosticFailure; native execution profile remains separate |
| WebGL1 | Explicit Prototype lowering through original ordered issuer |

Successful presentation/recycling grants no fence, timeline, GPU completion,
readback or external publication. Native frame APIs that require those semantics
reject under this profile. Context loss fails diagnostically; automatic restore
and asset reload are outside this version. Hidden documents suspend frames and
resume only after browser visibility dispatch has completed. Resize is explicit;
the application still owns its render-target resolution policy.
One live platform surface owner per document is admitted; another creation fails
before allocating a new context. The owner unregisters its visibility listener
at destruction. Browser events mark pending changes even while RAF is stopped;
the next RAF consumes all changes through the original surface lifecycle contract.

## Readiness

Contract Complete: OrderedRenderSystem/1 profile, failure gates and five-backend
outcomes. Prototype Complete: public WebGL factory, pipeline loader, lifecycle
and migrated Queen. Backend Validated: this original ordered WebGL1 path through
ANGLE D3D11. Platform Validated: Windows Chrome, independent platform smoke and
full game acceptance. Production Ready: not claimed. Metal/WebGPU ordered
execution, other browser/platform validation and context restoration remain
outside this profile.

## Evidence

Paths are relative to the repository root:

- `build/queen-webgl1/platform-renderdoc/acceptance.yaml`: final evidence index
  with bundle and receipt hashes.
- `build/queen-webgl1/platform-smoke-final/report.json`: six actual GPU frames,
  zero extent/resize, explicit pause/resume and event subscription retirement.
- `build/queen-webgl1/platform-renderdoc/gameplay.json`: full winning level,
  next level/restart/minimize recovery and public platform telemetry.
- `build/queen-webgl1/platform-renderdoc/queen_frame205.rdc` and
  `queen_frame372.rdc`: running and front-facing result captures.
- `build/queen-webgl1/platform-renderdoc/replay-report.json`: matching shadow/world
  geometry, real depth, 3D preview, framework UI and no medium/high replay errors.
- `build/queen-webgl1/platform-native-contracts.log` and
  `platform-native-smoke.log`: MinGW contracts and Queen Null smoke.

The build manifest deliberately records build-only status; gameplay and replay
acceptance live in the separate index. No presentation receipt claims native
GPU completion.
