# WebGPU ordinary Fragment depth/comparison

Status: Platform Validated for authored cold clean/fault pipelines. The common engine gate remains false;
WOCC remains Traditional. Declaration evidence grants no execution authority.

The existing ordinary D32/comparison declaration domain is lowered to WebGPU
core depth-2D texture and comparison sampler bindings. Explicit cold
OriginalPreparation12 allows a Fragment buffer-indexed package with scalar
ordinary color/depth inputs and independent samplers. It requires at least one
Fragment depth texture and one comparison sampler, authenticated package and
authorization, queried complete binding/stage/buffer limits, and explicit opt-in.
Ordinary buffers/color retain their existing Graphics shapes. Indexed images,
Vertex/Compute depth, descriptor arrays, dynamic offsets and table-backed
ordinary bindings remain refused. Preparation11/Execution13 retain color-only
scope. This adds no generic executable payload or application mode selection.

| Backend | Preparation12 | Ordinary depth pipeline declaration |
| --- | --- | --- |
| Null | Validator/refusal only | Existing validator |
| Vulkan | Reject this Emulated cold profile | Existing native mapping |
| Metal | Reject this Emulated cold profile | Existing Fragment native mapping |
| WebGPU | Cold Emulated authorization/pipeline, never execution | Fragment depth2D/comparison native layout and Slang-generated WGSL |
| WebGL | Deterministic rejection | Deterministic rejection |

Use the same public shader package, snapshot and authorization factories. Native
pipeline ownership and selected-entry mapping must preserve the original set,
binding, dimension, sampler type and stage. Missing capability/opt-in/budget or
unsupported shape must fail before native work. Native pipeline failure must
not publish an authorized usable pipeline. Fast contract and Null smoke precede
backend changes; real Chrome must create clean/fault pipelines through public
authorization with no native validation errors. Existing Vulkan/Metal mappings
and old WebGPU refusal boundaries remain covered.

Next, retain authentic canonical completed-depth Sources beside whole-upload
color Sources in the existing Root/inventory, state/content journal and callback
completion machinery. WebGPU control2 depth and control1 color need an explicit
composition profile; Native Source6/Input3 control4 and Content18 remain unchanged. Public
execution will need independently versioned capability and correspondence,
40-lane/color/fault numeric evidence and complete lifecycle/budget checks.
No new executor, journal, synthetic completion or fixed-LOD substitution is
permitted. The full original delivery plan remains required.

## Validated cold pipeline increment (2026-10-19)

Actual Chrome creates both public clean/fault pipelines with two ordinary
textures (one depth) and three independent samplers (two comparison), no native
validation errors, and lifetime/registration quota return. This is Preparation12
only: no Root depth closure, comparison draw, numeric readback or execution grant.
Final Windows4/4, codegen1/1, rebuilt Node2/2 and Chrome1/1, source421/style6 and
pinned formatting15 files pass. Mac regression12/12 retains its authenticated
9212-source/165-native-artifact epoch; only the browser main's explicit extension
include and browser HTML changed afterward. Neither is part of a native target.
Retained failures include the first mixed-object Windows negative fixture,
Mac1's obsolete undefined-version assertion and Wasm2's missing direct include.
Evidence: `D:/arp/build/bindless-webgpu-ordinary-depth-pipeline1/current-report.json`.

Next: explicit canonical ordinary depth/color native closure for cold
Preparation12, reusing the existing typed descriptor Source6 with Input1 and
completed Source1/control2, alongside whole-upload Publication6/control1 color.
The existing Source6 validator already supports Input1; no Source2/Input3 native
control4 authority is borrowed. Preserve the same Root/inventory and proceed to
an explicit same-journal composition and original callback execution profile.
