# Particle Shape / Velocity Emission - L2 Resource & Runtime Contract

> Status: **Contract Complete** for curated shape/velocity fields and importer lowering. Runtime emission semantics are **Prototype Complete** and hidden/visible smoke validated for the supported subset.
> This document closes TODO P1-4 "把 Unity ShapeModule 与方向性发射参数真正 lower 到内部 emitter contract" by collecting the facts across `ParticleEmitterAssetDesc`, `ParticleEmitterDesc`, `ParticleSystem::spawnParticle`, `parseShapeModule`, and the shape-focused tests / smoke paths.
>
> Any subset not implemented below must fail closed with deterministic diagnostics or be explicitly marked `Prototype/Legacy fallback`.

---

## 1. Supported Shapes

| Shape | Runtime Enum | Curated Meaning |
|---|---|---|
| Cone | `EmitterShape::Cone` | Directional spray bounded by `coneAngleDegrees` |
| Sphere | `EmitterShape::Sphere` | Radial spawn inside/on sphere radius subset |
| Box | `EmitterShape::Box` | Axis-aligned scatter in emitter-local extents |
| Directional Forward | `EmitterShape::DirectionalForward` | Deterministic forward-only emission |
| Radial Burst | `EmitterShape::RadialBurst` | Shell/burst emission with outward velocity |

---

## 2. Resource & Runtime Fields

| Field | Purpose |
|---|---|
| `shape` | Chosen emitter shape |
| `coneAngleDegrees` | Cone half-angle |
| `sphereRadius` | Sphere/radial burst radius |
| `boxExtents` | Box half-extents |
| `velocityMin` / `velocityMax` | Deterministic bounded spawn velocity range |
| `worldSpaceEmission` | Host-relative vs world emission semantics |

Curated rules:

- `startSpeed` is not collapsed to a guessed `+-speed` all-axis approximation.
- Directionality is preserved through explicit shape/velocity lowering.
- `RadialBurst` requires non-zero scalar speed; zero-speed authoring is rejected deterministically.

---

## 3. Importer Lowering

Unity `ShapeModule` curated subset:

- Sphere / Cone / Box lower directly to runtime shape enums and dimensions.
- Directional-forward and radial-burst are explicit engine-side extensions used to avoid ambiguous production fallback.
- Unsupported collider-like or mesh-like emission shapes remain out of contract and must fail closed or stay unimplemented.

Runtime semantics:

- Spawn position and initial velocity are derived from explicit shape metadata.
- `worldSpaceEmission == false` keeps host-relative attachment semantics explicit.
- No backend/native API default may change emission direction or velocity behaviour.

---

## 4. Fail-Closed Boundaries

| Scenario | Handling |
|---|---|
| Zero-speed `RadialBurst` | Deterministic diagnostic |
| Mesh/spline emission shapes | Not implemented |
| Implicit omnidirectional fallback for directional shapes | Forbidden |
| Hidden velocity sentinel / magic-number speed injection | Forbidden |

---

## 5. Evidence

| Evidence | File | What It Proves |
|---|---|---|
| Importer/runtime lowering | `tests/pika_particle_vfx_runtime_tests.cpp`, `tests/unity_particle_importer_tests.cpp` | Shape metadata lowers deterministically |
| Resource contract | `tests/particle_asset_resource_contract_tests.cpp` | Invalid shape/velocity combinations reject |
| Hidden smoke | `tests/particle_system_hidden_smoke.cpp` | Directional forward and radial burst runtime behaviour |
| Visible-sample-equivalent | `tests/pika_particle_visible_sample_equivalents.cpp` | Cone, sphere burst, box scatter, directional forward, radial burst end-to-end |

---

## 6. Status Ladder

| Slice | Status | Evidence |
|---|---|---|
| Shape field contract | Contract Complete | Resource/importer tests |
| Runtime emission semantics | Prototype Complete | Hidden smoke + visible-sample-equivalent |
| Native visible pixel parity | Not claimed | Outside this document |
