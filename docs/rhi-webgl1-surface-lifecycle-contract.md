# WebGlSurfaceLifecycle/1

L2 intent: explicit Prototype surface suspension and cold resize, independent
of context recovery and GPU completion. Enable once while context Ready and
declare maxMutations. Caller availability, original document visibility,
pagehide/pageshow and explicit pixel extent form the execution gate. No DPR,
CSS size or input coordinate is inferred; the platform supplies physical pixels.
Platform input/DPR integration remains P4.

| Backend | Lowering |
|---|---|
| Null | Validator only; no native surface permission |
| Vulkan | DiagnosticFailure here; original WSI lifecycle unchanged |
| Metal | DiagnosticFailure here; original drawable lifecycle unchanged |
| WebGPU | DiagnosticFailure here; original surface lifecycle unchanged |
| WebGL | Explicit Prototype original DOM canvas mutation and epoch invalidation |

Pause is immediate and may happen with retained recording/submitted owners.
All new creation/record/submit/present through the domain fails closed while
suspended. Accepted native work is never rolled back or called completed.
Actual visibility/page availability transitions and caller mutations consume
the declared bound. Duplicate observations are idempotent. Exhaustion is sticky
DiagnosticFailure and cannot be repaired by toggling visibility or resize.
No polling, blocking wait or per-event allocation is introduced.

Every accepted lifecycle mutation advances a separate surface epoch. Old
canvas/image/framebuffer/commands remain invalid after resume; ordinary context
resources/pipelines retain their original context generation. A new canvas is
created through the original bounded factory only after all old surface owners
are retired. First new frame still requires Clear/Store. No old drawing-buffer
contents are reused. Pause does not allow descriptor or recorded-reader reuse.

Resize requires the original canvas to own its returned image, no acquired or
submitted lease and no retained framebuffer/command or pending state journal.
Before native mutation validate the declared extent against actual queried
viewport/renderbuffer limits and the signed DOM dimension range. Native extent
must match exactly; mutation/query failure closes the surface, without silently
restoring guessed dimensions. Zero dimensions stay zero in authored canvas and
logical surface state; no 1x1 image or executable surface is fabricated. Resume
from zero requires explicit nonzero resize. The lifecycle result requests cold
swapchain rebuilding; the old wrapper never becomes a resized texture.

Browser event registration and cleanup retain the exact original DOM targets,
independent of a subsequently rebound selector. Native destruction remains
permitted on the original context while paused, and stale context handles remain
protected by WebGlContextRecovery/1. Shutdown/cancel reports CPU ownership and
native completion separately; unsupported deadline drain stays DiagnosticFailure.

Acceptance: five-backend/request unit negatives; original browser pause/resume,
zero/nonzero and retained-owner resize negatives; real first-frame Clear/Store,
stale epoch rejection, bounded mutation exhaustion, exact owner cleanup; Null
hidden smoke and existing backend regressions. Visual output requires RenderDoc
replay plus AI image inspection. Synthetic visibility events do not prove real
mobile lifecycle, mini-program platforms or Production Ready.

## WebGlCpuRetirement/1

Explicit Prototype CPU retirement closes original domain admission first,
advances its CPU lifetime, cancels the device's unissued prepared encoder,
releases selected publication/layout and clears original pipeline/descriptor
cache entries. Repeated retirement is idempotent. It is valid while surface
suspended, context lost or native domain failed; wrong-thread calls reject.
Retained external command/pass/descriptor/resource owners remain charged until
their own exact destructors run. Report retainedResourceCount is CPU ownership,
and gpuCompletionKnown is always false. No fake completion or drain is issued.

Null is validator-only, Vulkan/Metal/WebGPU reject this WebGL-specific request;
their original native shutdown contracts remain unchanged. Shared cache
clearForShutdown only removes CPU cache ownership, sets before arenas and
pipelines before layouts, preserving bounded reserved buckets and all externally
retained backend payload owners. It never marks a frame complete or substitutes
eviction based on an invented frame serial. Native deletion remains governed by
each original backend's payload destructor. This new explicit operation is only
consumed by the WebGL Prototype retirement path.

GpuDevice drainForShutdown continues to use ShutdownDrain/1 invalid, expired
and unavailable reports. Real NativeSync and exact deadline GPU drain remain
unavailable even after successful CPU retirement. Context destruction unregisters
the original callbacks and releases CPU metadata when its last original domain
owner retires; driver final-release timing remains unobserved.

## Accepted evidence, 2026-10-17

Contract Complete / Prototype Complete; Backend Validated only for Chrome
WebGL1 / ANGLE D3D11. Root manifest:
`build/webgl1-evidence/p3/lifecycle-root-final-2/20261017T150319Z-ff053e00/build-manifest.yaml`.
Acceptance: `build/webgl1-evidence/p3/lifecycle-native-final-2/checkpoint.yaml`.
13 MinGW targets, 14/14 CTest, Null hidden frame, six root browser modes,
24 RenderDoc captures, 24 pixel-verified draws and zero replay errors.
Resize first-frame 48x32 clear: frame27 event19; following 64x64 clear:
frame28 event7. AI inspected both exact images and the four original draw images.

Retirement verifies healthy, suspended, lost, surface-failed and native-requery
failed owners. Internal pending preparation refunds immediately; externally
retained finished commands keep their exact CPU quota, reject submission and
refund only after the last command/pass/encoder owner retires. Native delete,
framebuffer parent retention, descriptor reset/rewrite and pipeline publication
custody retain the original smoke paths. No CPU teardown advances GPU completion.
Synthetic lifecycle/loss does not prove real mobile or mini-program behavior;
Platform Validated, generic production admission and Production Ready remain open.
