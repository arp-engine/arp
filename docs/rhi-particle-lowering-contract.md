# RHI Particle Billboard Draw — Lowering Contract

> Status: **Contract Complete**.
> This document defines how each RHI backend (Null, Vulkan, Metal, WebGPU) lowers the L2 particle billboard draw semantics defined in `docs/rhi-particle-l2-draw-semantics.md`.

---

## 1. Capability Support Matrix

| Capability | Null | Vulkan | Metal | WebGPU |
|------------|------|--------|-------|--------|
| Graphics pipeline | ✓ (validation only) | ✓ | ✓ | ✓ |
| Push constants (64B, VF) | ✓ | ✓ | ✓ | Emulated via backend-owned buffer (§6) |
| Vertex buffer (slot 0) | ✓ (validation only) | ✓ | ✓ | ✓ |
| Indexed draw | ✓ | ✓ | ✓ | ✓ |
| Transfer: buffer upload | ✓ (validation only) | ✓ | ✓ | ✓ |
| Backend-owned staging | ✓ (enforced) | ✓ (staging pool) | ✓ (MTLBuffer) | ✓ (GPUBuffer mapped) |
| Blend state (none) | ✓ | ✓ (explicit) | ✓ (explicit) | ✓ (explicit) |
| Depth/stencil (disabled) | ✓ | ✓ (explicit) | ✓ (explicit) | ✓ (explicit) |
| Cull mode (None) | ✓ | ✓ (explicit) | ✓ (explicit) | ✓ (explicit) |
| Descriptor binding (texture set) | ✓ (validation) | ✓ | ✓ | ✓ |
| Depth texture binding (particle soft fade) | ✓ (validation + diagnostic) | ✓ | ✓ (contract) | ✓ (contract) |

**Legend:**
- ✓ = Fully supported with native path.
- "validation only" = Null backend records and validates the command sequence without executing.
- "Emulated" = WebGPU lacks native push constants; uses a small backend-owned uniform buffer.

---

## 2. Resource States & Barriers

| Resource | Initial State | Before Upload | Before Draw | After Draw |
|----------|-------------|---------------|-------------|------------|
| `vertexBuffer` (GpuOnly) | `Undefined` | `CopyDst` | `VertexBuffer` (implied by binding) | Unchanged |
| `uploadBuffer` (CpuToGpu) | `Undefined` | `CopySrc` | N/A | Unchanged |

**Lowering details:**

| Backend | Vertex Buffer Transition | Upload Transition |
|---------|------------------------|-------------------|
| **Null** | Validates explicit state transitions; records barrier metadata. | Validates `CopyDst` for destination; `CopySrc` for source. |
| **Vulkan** | `VK_ACCESS_TRANSFER_WRITE_BIT → VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT`, `VK_PIPELINE_STAGE_TRANSFER → VK_PIPELINE_STAGE_VERTEX_INPUT`. | `uploadBuffer` is HOST_VISIBLE, stays HOST_COHERENT if available. |
| **Metal** | `MTLStorageModePrivate` buffer; `MTLResourceStorageModePrivate`; use `MTLBlitCommandEncoder` for upload, then vertex draw. | Upload buffer is `MTLStorageModeShared`. Synchronization via `-[MTLBlitCommandEncoder synchronizeResource:]` if needed. |
| **WebGPU** | `GPUBufferUsage::VERTEX | COPY_DST`; transition handled implicitly by WebGPU. | Upload via `Queue.writeBuffer()`. No explicit barriers needed. |

---

## 3. Descriptor & Pipeline Layout

Particle rendering currently uses **push constants only** (no descriptor bindings).

| Backend | Push Constant Implementation |
|---------|------------------------------|
| **Null** | Validates push constant range against pipeline layout (64 bytes, Vertex|Fragment stages). |
| **Vulkan** | `vkCmdPushConstants(layout, VK_SHADER_STAGE_VERTEX_BIT \| VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64, data)`. |
| **Metal** | `[renderEncoder setVertexBytes:data length:64 atIndex:0]` + `[renderEncoder setFragmentBytes:data length:64 atIndex:0]`. |
| **WebGPU** | No native push constants. Backend allocates a small uniform buffer (`GPUBufferUsage::UNIFORM | COPY_DST`) per frame, writes viewProjection via `Queue.writeBuffer()`, and binds it as a descriptor at set 0. |

**Pipeline layout lowering:**

| Component | Null | Vulkan | Metal | WebGPU |
|-----------|------|--------|-------|--------|
| Push constant range | Validated | `VkPushConstantRange` | Argument buffer index | Emulated UBO + descriptor |
| Vertex buffer layout | Validated | `VkVertexInputBindingDescription` stride=48 | `MTLVertexDescriptor` stride=48 | `GPUVertexBufferLayout` stride=48 |
| Vertex attributes | Validated | `VkVertexInputAttributeDescription` | `MTLVertexAttributeDescriptor` | `GPUVertexAttribute` |

---

## 4. Upload Policy

| Aspect | Contract |
|--------|----------|
| **Staging ownership** | Backend-owned for all backends. Upper layer provides `uploadSourceBuffer` from device. |
| **Upload timing** | Per-frame, before render pass. |
| **Upload size** | `drawPlan.verticesSizeBytes` ≤ `maxUploadBytes`. |
| **Null behavior** | Validates `backendOwnedStaging=true` and correct intent. Rejects non-backend-owned upload. |
| **Vulkan behavior** | Uses backend staging pool (`VulkanStagingUploadPool`). Maps upload buffer, memcpy, unmap, then `vkCmdCopyBuffer`. |
| **Metal behavior** | Uses `MTLStorageModeShared` buffer for CPU write + `MTLBlitCommandEncoder` for GPU copy. |
| **WebGPU behavior** | Uses `Queue.writeBuffer()` to upload directly to vertex buffer (no intermediate staging needed for COPY_DST buffers). |

---

## 5. Pipeline State Explicit Lowering

All backends must receive **explicit** pipeline state (no native defaults):

| State | Null | Vulkan | Metal | WebGPU |
|-------|------|--------|-------|--------|
| **Blend** | Validates `blendEnabled=false`. | `VkPipelineColorBlendAttachmentState{blendEnable=VK_FALSE}` (explicit). | `MTLRenderPipelineColorAttachmentDescriptor{blendingEnabled=NO}` (explicit). | `GPUColorTargetState{blend: undefined}` (explicit). |
| **Depth test** | Validates `depthTestEnabled=false`. | `VkPipelineDepthStencilStateCreateInfo{depthTestEnable=VK_FALSE}` (explicit). | `MTLRenderPipelineDescriptor{depthAttachmentPixelFormat=MTLPixelFormatInvalid}` (explicit). | `GPUDepthStencilState{depthWriteEnabled: false}` (explicit). |
| **Depth write** | Validates `depthWriteEnabled=false`. | `depthWriteEnable=VK_FALSE` (explicit). | N/A (no depth attachment). | `depthWriteEnabled: false` (explicit). |
| **Cull mode** | Validates `cullMode=None`. | `VkPipelineRasterizationStateCreateInfo{cullMode=VK_CULL_MODE_NONE}` (explicit). | `MTLRenderPipelineDescriptor{cullMode=MTLCullModeNone}` (explicit). | `GPUPrimitiveState{cullMode: 'none'}` (explicit). |
| **Front face** | Validates `frontFace=CCW`. | `frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE` (explicit). | `frontFaceWinding=MTLWindingCounterClockwise` (explicit). | `frontFace: 'ccw'` (explicit). |
| **Topology** | Validates `topology=TriangleList`. | `VkPipelineInputAssemblyStateCreateInfo{topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST}` (explicit). | `MTLPrimitiveTypeTriangle` (explicit). | `GPUPrimitiveState{topology: 'triangle-list'}` (explicit). |
| **Viewport** | Validates explicit viewport from desc. | `vkCmdSetViewport` from desc (explicit). | `[renderEncoder setViewport:]` from desc (explicit). | Passed in `beginRenderPass` (explicit). |
| **Scissor** | Validates explicit scissor from desc. | `vkCmdSetScissor` from desc (explicit). | `[renderEncoder setScissorRect:]` from desc (explicit). | Passed in `beginRenderPass` (explicit). |

**Contract rule:** If any backend cannot receive an explicit state, it must produce a deterministic diagnostic failure or capability reduction at pipeline creation time (not silently at draw time).

---

## 6. WebGPU Push Constant Emulation

Since WebGPU has no native push constants, the backend:

1. Allocates a `GPUBuffer` (size 64 bytes, `UNIFORM | COPY_DST`) per frame slot at pipeline creation.
2. Before the particle render pass, calls `Queue.writeBuffer(gpuBuffer, 0, viewProjectionData, 64)`.
3. Adds a descriptor set (set 0, binding 0, `UniformBuffer`) to the internal pipeline layout.
4. Binds the UBO before draw: `renderPassEncoder.setBindGroup(0, bindGroup)`.

This emulation is **backend-internal** and must not be visible to the L2 particle contract or any upper layer. The capability `backendFallbacksHiddenFromUpperLayer` must report `true`.

---

## 7. Sync & Lifetime

| Resource | Created | Uploaded | Read by GPU | Destroyed |
|----------|---------|---------|-------------|-----------|
| `vertexBuffer` | Once in `ParticleRenderer::create()` | Every frame with particles | During render pass | On renderer `reset()` or device lost |
| `uploadBuffer` | Once in `ParticleRenderer::create()` | CPU write every frame | N/A (upload source) | On renderer `reset()` or device lost |
| `renderPipeline` | Once in `ParticleRenderer::create()` | N/A | Every frame | On renderer `reset()` or device lost |
| `pipelineLayout` | Once in `ParticleRenderer::create()` | N/A | Indirect (via pipeline) | On renderer `reset()` or device lost |

**Frame lifetime:**
- Upload happens inside a `TransferPass` before the render pass.
- The upload must be completed (GPU-side) before the render pass reads the vertex buffer.
- The `frameSerial` in `ParticleRhiDrawDesc` enables the backend to track per-frame resources.

---

## 8. Fallback & Diagnostic Behavior

| Condition | Behavior | Diagnostic |
|-----------|----------|------------|
| Backend has no graphics support | Deterministic failure at pipeline creation | "backend has no graphics capability" |
| Backend push constant limit < 64 bytes | Capability reduction via emulation or failure | "push constant budget exceeded for particle pipeline" |
| Upload exceeds staging pool capacity | Budget denial | "particle RHI vertex upload exceeds renderer budget" |
| `backendOwnedStaging=false` | Deterministic failure | "particle RHI draw requires backend-owned upload staging" |

---

## 9. Depth Texture Binding for Soft Particle Fade

When `depthFadeEnabled == true`, the particle render pass binds the scene depth texture as an additional descriptor. The lowering differs by backend:

| Aspect | Null | Vulkan | Metal | WebGPU |
|--------|------|--------|-------|--------|
| Depth texture binding | Validates descriptor slot exists | `VkDescriptorSet` with `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`, `VK_SHADER_STAGE_FRAGMENT_BIT` | `MTLTexture` bound as fragment argument, `sRGB=false`, `MTLPixelFormatDepth32Float` or `Depth32Float_Stencil8` | `GPUTexture` with `TEXTURE_BINDING` usage, sampled in fragment with `depthComparison` sampler |
| Descriptor set index | Validated at binding | `kParticleDepthSetIndex (2u)` | Slot 2 in argument buffer | Bind group 2 |
| Sampler | Linear filter validated | `VK_FILTER_LINEAR`, `VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE` | `MTLSamplerMinMagFilterLinear`, `MTLSamplerAddressModeClampToEdge` | `filter: 'linear'`, `addressModeU/V: 'clamp-to-edge'` |
| Depth format support | Validates readable | D32_SFLOAT / D24_UNORM_S8_UINT (readable via aspect mask) | MTLPixelFormatDepth32Float | `depth32float` (readable via `TEXTURE_BINDING`) |
| Missing depth texture | Deterministic diagnostic | Diagnostic returned at `recordFrameSet()` | Diagnostic returned at `recordFrameSet()` | Diagnostic returned at `recordFrameSet()` |
| Fallback behavior | Validates diagnostic | Fails closed — no draw execution when diagnostic emitted | Fails closed | Fails closed |

**Contract rules:**
- The depth texture binding is **renderer-owned**: the upper layer (`ParticleRenderer`) allocates the descriptor set, writes the texture view, and records the binding at the correct index. Backend code does not create or manage this binding.
- If the backend cannot provide a readable depth texture at the expected format (e.g. certain mobile tilers with write-only depth), the renderer must emit a deterministic diagnostic at `recordFrameSet()` rather than silently skipping the depth fade.
- The depth texture format may differ from the color attachment format; the shader receives the raw float depth value via `Texture2D<float>.Sample()`.

---

## 10. Status

- **Lowering contract**: Contract Complete for Null and Vulkan. Metal and WebGPU remain deterministic-diagnostic-only for particle depth fade until backend validation is completed.
- **Depth texture binding**: Null/Vulkan renderer path can allocate depth descriptor set and bind depth texture when available. Metal/WebGPU currently fail closed with deterministic diagnostics instead of claiming runtime support.
- **Null validation**: See `tests/rhi_particle_lowering_tests.cpp` (PVFX-R6.5).
- **Vulkan implementation**: Via existing `ParticleRenderer` + `renderer_common.h`.
- **Metal implementation**: Deterministic diagnostic only — pending Metal backend validation / visible smoke.
- **WebGPU implementation**: Deterministic diagnostic only — pending WebGPU backend validation / visible smoke.
