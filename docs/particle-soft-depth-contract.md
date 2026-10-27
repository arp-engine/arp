# Soft Particle / Depth Fade / Distortion — L2 Resource & Runtime Contract

> Status: **Contract Complete** for resource and runtime fields (`depthFadeEnabled`, `depthFadeDistance`). Renderer descriptor binding and deterministic diagnostic are **Prototype Complete**: `ParticleRenderer` allocates a depth descriptor set, binds it in `recordFrameSet()`, and returns a deterministic diagnostic when `depthFadeEnabled == true` but `frame.depthTexture == nullptr`. Backend capability and visible sample validation remain **Not Complete**.
> This document closes TODO P3-2 "定义 soft particle / depth fade / distortion contract".
>
> Any subset not implemented below must fail closed with deterministic diagnostics or be explicitly marked `Prototype/Legacy fallback` per `AGENTS.md`.

---

## 1. Domain Concepts

| Concept | Description | Code Anchor |
|---------|-------------|-------------|
| Soft particle | Per-particle alpha fade when the particle quad approaches a scene geometry surface. Uses the difference between the particle's depth and the scene depth from the depth buffer. | `ParticleEmitterDesc::depthFadeEnabled`, `depthFadeDistance` |
| Depth fade distance | World-space distance over which the soft fade occurs. When `particleDepth - sceneDepth < depthFadeDistance`, the particle alpha is reduced proportionally. | `ParticleEmitterDesc::depthFadeDistance` |
| Screen-space distortion | Per-particle refraction-like offset of the scene behind the particle. Requires reading the scene color texture. | Not yet implemented; reserved for future work. |
| Backend depth texture binding | The particle render pass must bind the scene depth texture. Backends without depth texture support (e.g. certain WebGPU configurations) must fail with a deterministic diagnostic. | `ParticleRenderer` allocates depth descriptor set and binds depth texture when `depthFadeEnabled == true` and `frame.depthTexture != nullptr`. When `depthFadeEnabled == true` but `frame.depthTexture == nullptr`, emits deterministic diagnostic. |

---

## 2. Curated Subset — Soft Particle / Depth Fade

| Field | Type | Behaviour | Code Anchor |
|-------|------|----------|-------------|
| `depthFadeEnabled` | bool | Enable depth-aware soft fade. When false, the depth-fade branch is skipped entirely. | `ParticleEmitterDesc::depthFadeEnabled` |
| `depthFadeDistance` | float | World-space fade distance. Particle alpha fades from 1.0 (distance >= fadeDistance) to 0.0 (distance <= 0). | `ParticleEmitterDesc::depthFadeDistance` |

**Unity importer mapping:**

- No Unity module maps directly. Depth fade is an authoring-only field in the Pika YAML resource format. There is no `UnityEngine.ParticleSystem` module that maps to soft particles directly (soft particles are typically enabled through the renderer's material settings in Unity).
- When `depthFadeEnabled` is `false`, the field is equivalent to "not present" and produces no observable effect.
- `depthFadeDistance` must be > 0 to have any effect. Values <= 0 disable the fade regardless of `depthFadeEnabled`.

**Shader contract (proposed):**

```hlsl
float depthFade(VertexOutput input, float sceneDepth)
{
    // Reconstruct linear depth from particle clip position and scene depth.
    // Fade alpha from 1.0 down to 0.0 as particle approaches scene geometry.
    float particleLinearDepth = input.position.z / input.position.w;
    float alpha = saturate((sceneDepth - particleLinearDepth) / depthFadeDistance);
    return alpha;
}
```

**Runtime deterministic invariants:**

- When `depthFadeEnabled == false`, the fragment shader skips the depth texture read.
- When no depth texture is bound (backend fallback), the depth fade path fails closed with a deterministic diagnostic rather than silently continuing without the effect.
- The depth fade is deterministic given the same depth buffer content.

---

## 3. Non-goals & Closed Failures

| Scenario | Handling | Justification |
|----------|----------|---------------|
| Screen-space distortion (refraction) | Not implemented | Requires scene color texture binding; reserved for future |
| Soft particle with orthographic projection | Not verified | Current contract assumes perspective projection |
| Per-particle depth fade distance | Not implemented | Uses per-emitter constant distance |
| Depth fade on trail/beam geometry | Not implemented | Trail/beam have separate vertex/fragment shaders |
| Distortion texture / normal map | Not implemented | Reserved for future |
| Backend depth texture unavailable | Deterministic diagnostic | If the backend cannot provide a readable depth texture (e.g. certain mobile tilers), the pass must fail closed with a diagnostic rather than silently skipping depth fade. |

---

## 4. Resource Contract — `ParticleEmitterAssetDesc`

| Resource Field | Type | Lowered To | Importer Source |
|---------------|------|------------|-----------------|
| `depthFadeEnabled` | bool | `ParticleEmitterDesc::depthFadeEnabled` | Authoring resource field |
| `depthFadeDistance` | float | `ParticleEmitterDesc::depthFadeDistance` | Authoring resource field |

Fingerprint includes both fields (see `ParticleEffectResource::appendContentFingerprint` in `particle_asset_resources.cpp`).

---

## 5. Backend Capability Contract

| Capability | Required | Diagnostic Behaviour |
|------------|----------|---------------------|
| Depth texture accessible in fragment shader | `depthFadeEnabled == true` | If unavailable, emit deterministic diagnostic; pass must fail closed |
| Depth texture format (D32, D24S8, etc.) | Readable as float | If depth texture is write-only (certain mobile tilers), emit deterministic diagnostic |
| Sampler for depth comparison | Linear or point filter | Point filter is sufficient for per-pixel depth comparison |

---

## 6. Test Coverage

| Test | File | What It Verifies |
|------|------|------------------|
| Depth fade resource fields in fingerprint | `particle_asset_resource_contract_tests.cpp` | `depthFadeEnabled` and `depthFadeDistance` survive round-trip validation |

*Shader contract tests verify `DepthTexture` reflection. Renderer depth descriptor set allocation is tested by `rhi_particle_textured_hidden_smoke` (which validates full ParticleRenderer create/draw). Visible sample smoke tests remain deferred (Prototype status).*

---

## 7. Readiness Matrix

| Field | Status | Doc Reference |
|-------|--------|---------------|
| Resource contract (fields + validator) | `Contract Complete` — §2, §4 | `ParticleEmitterAssetDesc::depthFadeEnabled`, `depthFadeDistance` |
| Runtime desc | `Contract Complete` — §2 | `ParticleEmitterDesc::depthFadeEnabled`, `depthFadeDistance` |
| Fingerprint | `Contract Complete` | `particle_asset_resources.cpp` lines 427-428 |
| Shader implementation (Slang) | `Prototype Complete` | `particles.slang` fragment shader samples `DepthTexture` when `depthFadeEnabled != 0`; computes `fadeAlpha = saturate((sceneDepth - particleDepth) / depthFadeDistance)`. Shader contract test validates `DepthTexture` reflection and push constant layout. |
| Renderer descriptor binding | `Prototype Complete` | `ParticleRenderer` allocates depth descriptor set, binds in `recordFrameSet()`, deterministic diagnostic when no depth texture. |
| Backend depth texture binding | `Prototype Complete` | Depth set allocated from arena, texture view created per-frame, bound at descriptor set index `kParticleDepthSetIndex (2u)`. |
| Visible sample (ground fog) | Not yet | — |
| Platform validated | Not yet | — |

---

## 8. Future Directions (Out of Scope for P3-2)

- Screen-space distortion / refraction particles.
- Per-particle depth fade distance curve.
- Soft particles for trail/beam geometry.
- Depth-based particle collision (separate from CollisionModule plane).
- Sub-surface scattering approximation via depth-thickness.
