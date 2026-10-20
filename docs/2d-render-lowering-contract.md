# Sprite draw / 1

L2: XY world/+Y up; Z-only unit quaternion; six float4 per instance (96B):
origin, axisX, axisY, UV rectangle, linear tint, flip flags. Instance vertex
stream slot0, locations0..5, offsets0/16/32/48/64/80, triangle list, six vertices,
no vertex/index geometry, firstInstance0 and explicit stream byte offset.
One float4 projection push constant (16B); no cross-language matrix.
Atlas is explicit RGBA8 sRGB or linear format, straight alpha, nearest/clamp;
fragment emits premultiplied linear RGBA; color/alpha blend ONE/ONE_MINUS_SRC_ALPHA.
No depth/stencil/cull, full color writes, sample count1, dynamic viewport/scissor.
ResourceManager sprite+atlas generation must be ready at preparation and draw;
unknown/missing/stale inputs reject before recording. Stable transparent order,
only contiguous atlas-compatible items may merge. CPU plan/GPU buffers bounded
by configured instance/atlas/frame-slot budgets. Upload/import is cold.

| Backend | Lowering / admission |
|---|---|
| Null | ValidatorOnly, never executable visual evidence |
| Vulkan | Ordinary instance-rate vertex stream, sampled texture/sampler,
  push constants and native timeline completion; Native only after real create succeeds |
| Metal | Same L2 intent, reflected vertex/descriptor/push slots; unvalidated
  native execution is never inferred from Vulkan success |
| WebGPU | Instance vertex layout, texture/sampler bind group, existing
  versioned push uniform lowering; target execution remains unvalidated |

Completion uses the narrower timelineSync capability; NativeSync also covers
binary semaphores, which this renderer does not request. A backend without a
native/equivalent timeline execution payload is DiagnosticFailure for this executor.
Missing timelineSync/core graphics/pipeline/binding/texture/completion is deterministic
failure. No dummy atlas, CPU replay, implicit bindless opt-in, pipeline per draw,
unbounded cache or frame-buffer overwrite. In-flight slot is polled, never waited
on in frame recording. Shutdown drain may block. A failed recording returns its
diagnostic; accepted submit owners remain until genuine completion.
