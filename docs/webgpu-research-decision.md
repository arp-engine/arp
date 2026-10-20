# Optional WebGPU Research

Status: Decision Complete.

Decision: WebGPU remains a deferred optional target.

Rationale:

- The current mobile-first roadmap needs Android Vulkan and Apple Metal before a
  browser target.
- WebGPU resource states, bind groups, shader target generation, canvas
  swapchain, and timestamp limitations need a dedicated compatibility layer.
- Existing RHI contracts must not be weakened to fit WebGPU prematurely.

RHI WebGPU runtime profiles now record browser capability facts and deterministic
diagnostics in a testable form so future work can revisit WebGPU without
distorting the core renderer.
