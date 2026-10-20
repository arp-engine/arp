# RenderTargetCoordinates/2

L2 intent: world-up geometry appears above the camera center; a full-screen
sample of a render attachment preserves its pixel positions. UI uses top-left
pixel coordinates. CPU raster rows and render-attachment sampling are separate
contracts, even where their lowering agrees. Matrix bytes remain core::Mat4
row-major and shader fields remain default Slang float4x4.

The existing fullscreen shader ABI forms `uv = ndc * 0.5 + 0.5`, followed by an
explicit Y scale/bias. The existing core projection has negative Y scale.

| Backend | Lowering | Projection Y correction | Fullscreen UV Y scale/bias |
| --- | --- | --- | --- |
| Null | Equivalent command validation only | Identity | 1, 0 |
| Vulkan | Native positive-height viewport | Identity | 1, 0 |
| Metal | Native viewport: NDC +Y maps to top, texture row zero is top | Negate projection Y row | -1, 1 |
| WebGPU | Native viewport: NDC +Y maps to top, texture row zero is top | Negate projection Y row | -1, 1 |
| WebGL ES100 target | `WebGlShaderCoordinates/1`, compiler flips clip Y and maps z to 2z-w | CPU identity; compiler is the sole flip owner | -1, 1 for attachment sampling |

Projection-only correction negates m[5]. A composed VP correction negates all
four components of row 1. These operations are not interchangeable. Applying
the composed correction twice restores the canonical VP. No transpose is
permitted as a coordinate correction.

The previous Metal identity mapping inverted geometry relative to the native
viewport and inverted every fullscreen texture read. Multiple post passes could
cancel or reintroduce that inversion, while depth and color consumers disagreed.
Uniform-color and full-screen coverage fixtures cannot detect this failure.

Validation requires a fast four-backend semantic test and native asymmetric
readback: world-positive Y geometry in the upper region, plus an upper-half
render target preserved through successive post passes. Production scene,
depth, shadow, temporal, SSR, water, UI and picking must be revalidated after a
mapping change. Native evidence on Metal does not validate Vulkan or WebGPU
execution, and does not by itself establish platform or production readiness.

Static viewport/scissor behavior is an independently observed open issue; the
asymmetric post fixture uses explicit dynamic state to isolate this contract.

Atlas attachment sampling composes that same transform inside each viewport
rectangle: `(offsetY, scaleY)` becomes `(offsetY + scaleY, -scaleY)` for
Metal/WebGPU and remains unchanged for Null/Vulkan. Caster and receiver retain
identical corrected VP bytes. Sampling bounds must use the minimum/maximum of
the two rectangle endpoints, then inset by half a texel; a negative scale must
not reverse clamp bounds or sample an adjacent atlas strip. This uses the
existing texture rectangle ABI, with no shadow-specific backend switch.

Compute consumers address texture rows directly. A raster VP must be composed
with clip-to-texture Y before Hi-Z projection, or before inversion for DDGI
world reconstruction. `applyRenderTargetSamplingYConvention` performs this
composition on row-major VP bytes. This is independent from the earlier camera
projection correction: Metal/WebGPU require both; Null/Vulkan are identity.
It does not flip texture data or alter an inverse matrix after inversion.

## WebGlShaderCoordinates/1

This versioned shader-target lowering is independent from generic WebGL device
admission. `render_target_coordinates.h` continues to reject generic WebGL
execution until P2 binds the target/profile to the actual context. Existing
Null/Vulkan/Metal/WebGPU decisions are unchanged. The separate
`webgl_shader_coordinates.h` defines the complete ES100 target convention:

| Domain | Exact WebGL target operation |
| --- | --- |
| Clip depth | Compiler emits `z = 2*z-w` once; source remains L2 [0,w] |
| Clip Y | Compiler negates Y once; CPU keeps original canonical row-major VP |
| Winding | Clip Y negation reverses winding; map declared front-face once |
| Viewport/scissor | Top-left `(x,y,w,h)` becomes `(x,H-y-h,w,h)` after range validation |
| CPU raster | Preserve declared top-to-bottom source bytes, unpack flip=false, sample UV identity |
| Attachment texture | Sample with explicit UV Y scale/bias (-1,1); no additional matrix correction |
| Readback | GL readPixels begins at bottom row; explicit top-left CPU output reverses rows once |

The production-program fixture uses a non-symmetric matrix, two separated
triangles, 2x2 red/green CPU raster, an off-center scissor and a full attachment
round trip. It verifies clip rejection outside [0,w], explicit back-face culling
and exact per-pixel RGB preservation. This is limited shader-component browser
evidence; renderer/GUI/scene consumers are P4 and remain unvalidated.
