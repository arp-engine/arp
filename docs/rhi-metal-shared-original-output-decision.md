# Metal shared original-output implementation decision

Status: implementation decision recorded; native shared-output join is not yet
implemented or Backend/Platform Validated. This remains mandatory P4-14 work.
The choice is to implement the four existing bounded workloads through an
original Metal issuer. Diagnostic refusal on the current Windows host does not
remove Apple implementation or target evidence from the plan.

## One delivery package

Clear/Resize, Sampled, PingPong and Storage/MRT must consume the same immutable
workload, input bytes, Slang publications and shared recorder already used by
Vulkan/WebGPU. Deliver original creation, finite recording, actual owned output,
original drawable present, lifetime/error handling, negative tests and Apple
runtime evidence together. Helpers and facade interfaces are not progress units.

The dependency remains L2 intent/input -> approved native lowering -> native
commands/actual bytes/original present -> sealed audit -> readiness. Readiness,
RenderDoc, report files and caller-provided hashes cannot authorize native work.

## Existing reuse boundaries

| Responsibility | Existing reusable entry | Required integration boundary |
|---|---|---|
| L2 ownership and outputs | `rhi/conformance/{clear,sampled,storage_mrt}_execution.h`, `original_output.h` | Add an issuer-private constructor key only with a genuine implementation; preserve public read-only audit |
| Allocations and immutable bytes | `core/cpu_allocator`, original initialized-input/artifact contracts | Explicit quotas, source hashes, complete ranges and retirement; no ordinary same-byte object used as original authority |
| Native recording ownership | `metal_original_command_custody.h`, `metal_native_commands.mm` | Reuse before-submit/native-commit/completed custody; reject a second or foreign custody, retain accepted owners through genuine terminal callbacks |
| Native shader creation | `metal_backend_pipelines.cpp`, `metal_native_pipelines.mm` | Consume exact original generated MSL/reflection, verify row-major numeric contract; path/reflection labels alone are insufficient |
| Buffer/texture copy | `metal_texture_readback.cpp`, backend native blit/buffer facilities | Reuse format/size/allocation/copy utilities, add issuer-owned observation; the borrowed diagnostic schedule/poll path is not sealed-run authority |
| Drawable ownership | `metal_native_swapchain.{h,mm}`, frame-slot/present completion states | Join same acquired image, frame slot, serial and surface epoch; prove all original copies precede original present and recycling |
| Loss and shutdown | `metal_original_device_native_loss.*` and native completed state | Revoke stale audits, keep accepted work until authentic retirement; no host stub, immediate signal or synthetic completion |

The existing borrowed readback gate and original-content exclusivity checks
remain intact. Reusing its public facade to bypass those gates is prohibited.
Reuse backend-private primitives only under the original issuer's custody.
Ordinary production factories and NativeSync are not enabled by this package.

## Lowering and capability obligations

Reuse the existing complete L2 descriptors and device capability contract. Native
texture formats/usages, MRT count, storage binding ranges, dispatch dimensions,
vertex/index layout, push/UBO bindings, access scopes and resource ownership must
be derivable from actual target facts or a versioned native specification.
Unsupported requests fail before record/submit with stable diagnostics. Do not
select a lowering from an OS/backend name, infer native defaults, silently
substitute content or transfer bindless-only authorization to ordinary graphics.

| Backend | This decision's behavior |
|---|---|
| Null | Existing validator/hidden frame; no native output audit |
| Vulkan | Existing four original execution/output issuers retained |
| WebGPU | Existing four bounded original execution/output issuers retained |
| Metal | Shared join stays closed with `RHI-CONFORMANCE-METAL-ORIGINAL-OUTPUT-JOIN-UNAVAILABLE` until native issuer and exact gates are implemented; native Apple acceptance remains required |
| WebGL1 | Existing ordered Clear/Sampled/PingPong retained; Storage/MRT remains explicitly unavailable |

## Acceptance boundary

Use the existing quick workload contracts and Null hidden-frame smokes, plus
Metal host negative gates; these do not prove Apple execution. Apple validation
must cover all supported native scenarios, input/artifact identity, actual output
bytes and same drawable present, resize, owner/quota invalidation, lifecycle,
accepted deadline and genuine callback retirement. Negative cases must reach
the intended rejection point; a generic native error cannot satisfy an output
mismatch or timeout case. Budgets include native input/texture/copy allocations,
padded and compact outputs, metadata and callbacks; declare cold creation and
completion deadlines independently.

Keep the frozen same-input Vulkan/RenderDoc outputs as the independent visual
baseline, compare actual Apple bytes, and preserve native Apple capture/diagnostic
records for command and resource inspection. Establish the capture tool's native
Metal support explicitly; never describe a host stub, Vulkan replay or Xcode
capture as a RenderDoc Metal capture. Missing required native/platform/visual
evidence keeps P4-14 open. Android/iOS lifecycle and mini-program validation,
Platform Validated and Production Ready remain separate mandatory target gates.
