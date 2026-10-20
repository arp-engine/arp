# Particle Mesh Particle - L2 Resource, Runtime, and Prototype Renderer Contract

> Status: **Contract Complete** for resource/runtime fields (`isMeshParticle`, `meshAssetId`, mesh scale range). Draw-plan generation is **Prototype Complete**. Renderer-side native draw is **Prototype Complete** with explicit prototype bounds and deterministic diagnostics; backend/platform validation is not claimed.
> This document closes TODO P3-1 "设计 mesh particle resource/runtime contract" by collecting the implemented facts across `ParticleVisualBinding`, `ParticleEmitterDesc`, `ParticleInstance`, `buildMeshParticleDrawPlan()`, `ParticleRenderer`, and the focused tests.
>
> Any subset not implemented below must fail closed with deterministic diagnostics or be explicitly marked `Prototype/Legacy fallback`.

---

## 1. Domain Concepts

| Concept | Description |
|---|---|
| Mesh particle family | Non-sprite particle represented as mesh instances rather than billboards |
| Mesh asset binding | Engine-owned logical mesh identifier carried as `meshAssetId` |
| Prototype renderer-owned fallback | Current renderer prototype only resolves `meshAssetId == "debris_rock"` to a renderer-owned debug triangle |
| Deterministic unsupported diagnostic | Unknown mesh ids or unavailable native prototype pipeline must fail with explicit diagnostics |

---

## 2. Resource & Runtime Contract

| Field | Layer | Behaviour |
|---|---|---|
| `visualBinding.isMeshParticle` | Resource | Explicitly declares mesh particle family |
| `visualBinding.shaderFamilyId = ParticleMeshParticle` | Resource | Required family identifier |
| `meshAssetId` | Resource/Runtime | Logical mesh binding key |
| `meshScaleMin` / `meshScaleMax` | Runtime | Per-particle scale range |
| `ParticleVisualFamily::MeshParticle` | Runtime | Separates mesh path from sprite/trail/beam paths |

Validator invariants:

- `isMeshParticle == true` requires `shaderFamilyId == "ParticleMeshParticle"`.
- `meshAssetId` is required when mesh particle is enabled.

---

## 3. Runtime & Renderer Semantics

Runtime:

- Spawn stamps `ParticleInstance::meshAssetId`.
- `buildMeshParticleDrawPlan()` groups mesh particles and emits deterministic instance data.

Renderer prototype:

- `ParticleRenderer` allocates a separate mesh-particle pipeline path and indexed draw path.
- Current prototype mesh resolution supports only `meshAssetId == "debris_rock"`.
- If native mesh pipeline creation is unavailable, the renderer records an explicit unavailability diagnostic and only fails when mesh particles are actually drawn.

This is intentionally a bounded prototype seam, not a production-ready mesh asset system.

---

## 4. Fail-Closed Boundaries

| Scenario | Handling |
|---|---|
| Unknown `meshAssetId` | Deterministic diagnostic |
| Silent fallback from mesh particle to sprite billboard | Forbidden |
| Implicit native mesh asset handle exposure above renderer/backend seam | Forbidden |
| Claiming Vulkan/Metal/WebGPU backend validation from prototype path alone | Forbidden |

---

## 5. Evidence

| Evidence | File | What It Proves |
|---|---|---|
| Resource validator | `src/resource/particle_asset_resources.cpp`, `tests/particle_asset_resource_contract_tests.cpp` | Mesh particle contract fields are explicit and validated |
| Runtime smoke | `tests/particle_system_hidden_smoke.cpp` | Mesh particle emitter spawns/simulates deterministically |
| Visible-sample-equivalent | `tests/pika_particle_visible_sample_equivalents.cpp` | Pika-facing runtime path carries mesh particle instances |
| Draw-plan | `src/renderer/particles/particle_draw_plan.h` | Separate mesh-particle plan builder exists |
| Renderer prototype positive path | `tests/rhi_particle_textured_hidden_smoke.cpp` | Null/native-focused path records indexed mesh particle draw |
| Renderer prototype negative path | `tests/rhi_particle_textured_hidden_smoke.cpp` | Unknown mesh id returns deterministic diagnostic |

---

## 6. Status Ladder

| Slice | Status | Evidence |
|---|---|---|
| Resource/runtime contract | Contract Complete | Validator + runtime field propagation |
| Draw-plan | Prototype Complete | MeshParticle draw-plan builder + visible-sample-equivalent |
| Renderer prototype | Prototype Complete | Focused renderer tests + deterministic diagnostics |
| Native visible pixel parity | Not claimed | Outside this document |
