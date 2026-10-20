# RHI Particle Billboard Draw — L2 Semantics Contract

> Status: **Contract Complete**.
> This document defines the L2 (engine-facing) semantics for particle billboard rendering through RHI. It formalizes the existing implementation at `src/renderer/particles/`, identifies contract boundaries, and documents lowering expectations for Null, Vulkan, Metal, and WebGPU backends.

---

## 1. Domain Concepts

| Concept | Description |
|---------|-------------|
| Particle Instance | A single simulated particle with position, velocity, color, size, age, lifetime. Owned by `ParticleSystem`. |
| Particle Emitter | Configures spawn behavior (burst, looping, rate), lifetime, size/color curves, velocity range. |
| Particle Draw Plan | Frame-specific CPU-built vertex data and draw commands derived from alive particle instances. |
| Billboard Quad | Each particle is rendered as a camera-facing quad (two triangles, 6 vertices) with a corner offset for screen-aligned expansion. |
| Back-to-Front Sort | Particles sorted by distance² from camera before vertex generation to ensure correct alpha blending. |
| Per-Frame Vertex Upload | Dynamic vertex data uploaded each frame via backend-owned staging. No static vertex buffers. |

---

## 2. L2 Vertex Format (`ParticleVertex`)

Defined in `src/renderer/particles/particle_draw_plan.h`:

```cpp
struct ParticleVertex {
    float worldPosition[3];   // center of particle in world space
    float corner[2];          // billboard corner offset (-1,-1 to 1,1)
    float color[4];           // RGBA (premultiplied or straight — see §4)
    float size;               // billboard half-extent in screen pixels
    float normalizedAge;      // 0..1 lifetime progress (for shader effects)
};
// stride: 48 bytes
```

**Semantic constraints:**
- `corner` values map to the 6 vertices of a screen-aligned quad: `[-1,-1, 1,-1, 1,1, -1,-1, 1,1, -1,1]`.
- Shader applies `corner * size` as a screen-space offset in clip space (§5).
- `normalizedAge` is advisory; shader may or may not use it for fade/scale effects.

---

## 3. L2 Push Constants (`ParticlePushConstants`)

```cpp
struct ParticlePushConstants {
    float viewProjection[16]; // 4x4 view-projection matrix
};
// size: 64 bytes
```

**Semantic constraints:**
- Must be populated from `SceneConstants::viewProjection` each frame.
- Stages: `Vertex | Fragment`.
- Offset: 0.

---

## 4. L2 Pipeline State Contract

The particle render pipeline uses these explicit state values (from `ParticleRenderer::create()` + `renderer_common.h` helpers):

| State Field | Explicit Value | Notes |
|-------------|---------------|-------|
| **Blend** | Disabled (`blendEnabled=false`) | Current contract; shader outputs premultiplied alpha. If alpha blending is needed, must switch to `SrcAlpha, OneMinusSrcAlpha`. |
| **Depth Test** | Disabled (`depthTestEnabled=false`) | Particles render on top. If depth testing is needed later, use `LessEqual` with write disabled. |
| **Depth Write** | Disabled | |
| **Cull Mode** | `None` | Billboard quads are double-sided. |
| **Front Face** | `CounterClockwise` | Default; not critical since cull is None. |
| **Topology** | `TriangleList` | 6 vertices per quad, 2 triangles. |
| **Color Format** | Frame color target format | Inherited from render frame. |
| **Color Write Mask** | `All` | Full RGBA write. |
| **Vertex Buffers** | Slot 0, stride 48 bytes | Matches `ParticleVertex` layout. |
| **Viewport** | Explicit from frame extent | No default; explicitly set per `RenderPipelineDesc`. |
| **Scissor** | Explicit from frame extent | No default; explicitly set per `RenderPipelineDesc`. |

**Contract rule:** All pipeline state values above must be explicitly set in `RenderPipelineDesc`. The backend must not rely on native defaults for any state. If a backend cannot support an explicit state, it must produce a deterministic diagnostic failure.

---

## 5. L2 Draw Command Sequence

Each frame's particle rendering follows this exact sequence through RHI `CommandEncoder`:

```
1. beginTransferPass("ParticlesUpload")
   → uploadBuffer(source=uploadBuffer, dest=vertexBuffer, size=verticesSizeBytes,
                   intent=Upload, backendOwnedStaging=true)
   → end()

2. beginRenderPass(colorTarget, LoadOp=Load, StoreOp=Store)
   → setPipeline(particlePipeline)
   → bindVertexBuffer(slot=0, buffer=vertexBuffer, offset=0)
   → pushConstants(stages=Vertex|Fragment, offset=0, size=64, data=&viewProjection)
   → drawIndexed(indexCount=vertices.count, instanceCount=1,
                  firstIndex=0, vertexOffset=0, firstInstance=0)
   → end()
```

**Semantic rules:**
- Transfer pass is required only when vertex data changed (always, for dynamic particles).
- Upload buffer (`uploadSourceBuffer`) is backend-owned staging memory (CpuToGpu).
- Vertex buffer (`vertexBuffer`) is backend-owned device-local memory (GpuOnly, CopyDst).
- Render pass uses `LoadOp::Load` to composite over existing scene content.
- All draw commands emit results through `RendererResult` with diagnostics.

---

## 6. Upload & Staging Contract

| Property | Value |
|----------|-------|
| Staging ownership | Backend-owned (`backendOwnedStaging=true`). |
| Upload source | `GpuBuffer` with `CpuToGpu` memory, `CopySrc` usage. |
| Destination | `GpuBuffer` with `GpuOnly` memory, `VertexBuffer | CopyDst` usage. |
| Upload trigger | Every frame the particle system has alive particles. |
| Upload size | `maxParticles * 6 * sizeof(ParticleVertex)` bytes, pre-allocated at renderer creation. |
| Frame serial | Used for upload tracking; provided via `RenderFrameContext.frameSerial`. |

**Contract rule:** Upper layers must not create or manage staging buffers. All staging resources are owned and recycled by the backend. The `uploadSourceBuffer` is provided only as a destination for CPU write; its allocation and lifecycle are backend-managed.

---

## 7. Sorting Semantics

| Property | Behavior |
|----------|----------|
| Sort order | Back-to-front (farthest first, drawn first). |
| Sort key | `distance²(particle.position, camera.position)`. |
| Stability | `std::stable_sort` — preserves emitter order for equal distances. |
| Enabled by default | Yes (`sortBackToFront=true`). |
| Determinism | Same seed + same camera position → same sort order. |

---

## 8. Bounds & Culling

`ParticleSystem::bounds()` returns an AABB computed from all alive particle positions:

```cpp
struct ParticleBounds {
    Vec3 center;
    Vec3 extents;
};
```

**L2 contract:**
- Bounds are computed after each `update()` call.
- Empty system produces `extents = (0,0,0)` and `center = (0,0,0)`.
- Bounds are conservative (extents are half the full AABB size).
- Culling input is advisory; current implementation does not cull — the shader relies on clip-space transform.
- Bounds are not currently passed to the RHI draw descriptor; this is a future extension point (for frustum culling and occlusion).

---

## 9. Debug Telemetry

`ParticleSystemTelemetry` (defined in `particle_system.h`):

| Counter | Source | Purpose |
|---------|--------|---------|
| `aliveParticles` | Runtime | Verified after each update. |
| `spawnedThisFrame` | Runtime | Spawn count for frame budget tracking. |
| `droppedSpawns` | Runtime | Spawns denied due to capacity. |
| `simulatedEmitters` | Runtime | Active emitters processed this frame. |
| `boundsUpdates` | Runtime | Bounds recomputation count. |
| `budgetDenials` | Runtime | Resource budget exceeded events. |

**RHI render telemetry** (future — not yet wired):
- `particleVertexUploadBytes` — per-frame vertex upload size.
- `particleDrawCalls` — drawIndexed call count.
- `particlePipelineBinds` — pipeline set count.

---

## 10. Backend Capability Requirements

| Capability | Required | Notes |
|------------|----------|-------|
| Graphics | Yes | Render pass + draw. |
| Push constants | Yes | 64 bytes, Vertex|Fragment stages. |
| Vertex buffers | Yes | Single slot, 48-byte stride. |
| Indexed drawing | Yes | Non-indexed fallback acceptable if indexed unsupported. |
| Transfer (buffer upload) | Yes | Per-frame dynamic data. |
| Backend-owned staging | Yes | `backendOwnedStaging` must be true in lowering. |
| Blend states | Optional | Current contract uses no blend; future extension. |
| Depth/stencil | Optional | Current contract disables depth. |
| Descriptor binding | No | Current contract uses push constants only. No textures/samplers needed. |

---

## 11. Status

- **L2 semantics**: Contract Complete.
- **Lowering contract**: See `docs/rhi-particle-lowering-contract.md` (PVFX-R6.2).
- **Null backend validation**: Tests in `tests/rhi_particle_lowering_tests.cpp` (PVFX-R6.5).
- **Backend implementation**: Vulkan path via existing `ParticleRenderer` + `renderer_common.h`.
- **Shader**: `shaders/stylized/particles.slang` — Slang source, outputs TBD.
