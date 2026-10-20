# Particle Rotation / Orbit / Angular Motion — L2 Resource & Runtime Contract

> Status: **Contract Complete** for importer pipeline (RotationOverLifetimeModule). Runtime rotation and orbit are **Prototype Complete**.
> This document closes TODO P2-1 "为 rotation/orbit 增加内部 contract" by collecting the facts across `ParticleEmitterAssetDesc`, `ParticleEmitterDesc`, `ParticleSystem::update` rotation/orbit branches, `parseRotationModule`, and the related tests into a single authoritative contract.
>
> Any subset not implemented below must fail closed with deterministic diagnostics or be explicitly marked `Prototype/Legacy fallback` per `engine-wide constraints`.

---

## 1. Domain Concepts

| Concept | Description | Code Anchor |
|---------|-------------|-------------|
| Rotation speed | Per-emitter angular velocity in radians/second. Applied each frame as a uniform increment to every particle's `rotation` field. Also used as the magnitude for random initial rotation on spawn. | `ParticleEmitterDesc::rotationSpeed`; `ParticleSystem::update` line 821 |
| Start rotation | Per-emitter base rotation (radians) applied at particle spawn. Mixed with a random jitter of `rotationSpeed * random01(seed)` when `rotationSpeed > 0`. | `ParticleEmitterDesc::startRotation`; spawn logic at lines 745-746 |
| Orbit motion | Per-emitter planar rotation of particle positions around `orbitCenter` in the XZ plane (Y-axis up). The orbit angle advances linearly with normalized particle age. | `ParticleEmitterDesc::orbitEnabled`, `orbitCenter`, `orbitSpeed`; update at lines 824-831 |

---

## 2. Curated Subset — Rotation

| Field | Type | Behaviour | Code Anchor |
|-------|------|----------|-------------|
| `rotationSpeed` | float | Angular velocity in radians/second. Applied uniformly each frame. Values can be negative for counter-clockwise rotation. | `ParticleEmitterDesc::rotationSpeed` |
| `startRotation` | float | Initial rotation offset in radians. When `rotationSpeed > 0`, a random jitter in `[0, rotationSpeed)` is added at spawn. | `ParticleEmitterDesc::startRotation` |

**Unity importer mapping (`RotationOverLifetimeModule`):**

- `enabled: 1` required. When disabled the module is ignored.
- `z.scalar` → `rotationSpeed` (importer lowering). Unity uses Z-axis rotation as the primary angular velocity channel for 2D billboard particles.
- Multi-axis curves (`x.scalar`, `y.scalar`, `z.curve`, multi-key) are not parsed. The curated subset uses a single Z-axis angular velocity.
- `separateAxes` — not parsed. When enabled in Unity but not handled, the importer silently uses Z only.

**Runtime behaviour:**

```
// Spawn
particle.rotation = startRotation + rotationSpeed * random01(seed)

// Each frame
particle.rotation += rotationSpeed * deltaSeconds
```

- Rotation is a scalar angle in radians, applied as a uniform Z-rotation in the vertex shader.
- The random jitter at spawn is deterministic: same seed → same initial rotation.
- When `rotationSpeed == 0.0f`, the rotation branch still increments by zero (no-op).

---

## 3. Curated Subset — Orbit

| Field | Type | Behaviour | Code Anchor |
|-------|------|----------|-------------|
| `orbitEnabled` | bool | Enable per-frame orbital motion. | `ParticleEmitterDesc::orbitEnabled` |
| `orbitRadius` | float | Base orbital radius in world units. Used as the initial displacement from orbit center. | `ParticleEmitterDesc::orbitRadius` |
| `orbitSpeed` | float | Orbital angular speed in radians per unit of normalized age. Full rotation every `1/orbitSpeed` lifetime. | `ParticleEmitterDesc::orbitSpeed` |
| `orbitCenter` | `core::Vec3` | World-space center of the orbital path. | `ParticleEmitterDesc::orbitCenter` |
| `orbitAxis` | `core::Vec3` | Axis of rotation (currently Y-up only; other axes are reserved). | `ParticleEmitterDesc::orbitAxis` |

**Runtime behaviour:**

```
orbitAngle = normalizedAge * orbitSpeed * 2π
dx = particle.position.x - orbitCenter.x
dz = particle.position.z - orbitCenter.z
x = orbitCenter.x + (dx * cos(orbitAngle) - dz * sin(orbitAngle))
z = orbitCenter.z + (dx * sin(orbitAngle) + dz * cos(orbitAngle))
```

- Orbit is currently a 2D rotation in the XZ plane (Y-axis up). The `orbitAxis` field is defined in the resource but the runtime always rotates around Y.
- Orbit speed multiplies `normalizedAge` (float in `[0, 1]`), so `orbitSpeed = 1.0` produces exactly one full orbit over the particle's lifetime.
- When `orbitSpeed == 0.0f`, the orbit branch is skipped.
- When `orbitEnabled == false`, the orbit branch is skipped.

---

## 4. Non-goals & Closed Failures

| Scenario | Handling | Justification |
|----------|----------|---------------|
| RotationOverLifetimeModule `separateAxes = true` | Ignored; always uses Z only | Not in contract |
| Rotation multi-axis curves (`x`, `y` angular velocity) | Ignored | Not in contract |
| Rotation curve over lifetime (non-uniform speed) | Not implemented | Linear constant speed used |
| Orbit around arbitrary axis (non-Y) | Not implemented | XZ plane only |
| Orbit radius curve over lifetime | Not implemented | Constant radius |
| Per-particle orbit phase offset | Not implemented | All particles share age-based phase |

---

## 5. Resource Contract — `ParticleEmitterAssetDesc`

| Resource Field | Type | Lowered To | Importer Source |
|---------------|------|------------|-----------------|
| `rotationSpeed` | float | `ParticleEmitterDesc::rotationSpeed` | RotationOverLifetimeModule `z.scalar` |
| `startRotation` | float | `ParticleEmitterDesc::startRotation` | Converter default (0.0); importer sets from `startRotation` if present |
| `orbitEnabled` | bool | `ParticleEmitterDesc::orbitEnabled` | Authoring resource field (not from Unity importer) |
| `orbitRadius` | float | `ParticleEmitterDesc::orbitRadius` | Authoring resource field |
| `orbitSpeed` | float | `ParticleEmitterDesc::orbitSpeed` | Authoring resource field |
| `orbitCenter` | `core::Vec3` | `ParticleEmitterDesc::orbitCenter` | Authoring resource field |
| `orbitAxis` | `core::Vec3` | `ParticleEmitterDesc::orbitAxis` | Authoring resource field |

Fingerprint includes `rotationSpeed`, `orbitEnabled`, `orbitRadius`, `orbitSpeed`, `orbitCenter`, `orbitAxis` (see `ParticleEffectResource::appendContentFingerprint` in `particle_asset_resources.cpp`).

---

## 6. Pipeline Flow

```
Unity ParticleSystem YAML
  └─ RotationOverLifetimeModule.enabled: 1
     └─ z.scalar: 1.5
        └─ parseRotationModule()
           → importer field rotationSpeed = 1.5
              └─ converter: ParticleEmitterAssetDesc.rotationSpeed = 1.5
                 └─ converter: ParticleEmitterDesc.rotationSpeed = 1.5
                    └─ ParticleSystem::update() — rotation advance at line 821

Pika YAML (orbit)
  └─ ParticleEmitterDesc.orbitEnabled = true
     └─ orbitRadius / orbitSpeed / orbitCenter / orbitAxis
        └─ ParticleSystem::update() — orbit motion at lines 824-831
```

---

## 7. Test Coverage

| Test | File | What It Verifies |
|------|------|------------------|
| `RotationOverLifetime Z=1.5 supported` | `unity_particle_importer_tests.cpp` | `z.scalar: 1.5` → `rotationSpeed` via `parseRotationModule` |
| `orbit resource + fingerprint` | `unity_particle_importer_tests.cpp` | Orbit fields survive resource round-trip and produce distinct fingerprints |
| `hidden_smoke_orbit_rune` | `particle_system_hidden_smoke.cpp` | Orbit motion produces alive particles with rotation > 0 |

---

## 8. Readiness Matrix

| Field | Status | Doc Reference |
|-------|--------|---------------|
| RotationOverLifetimeModule importer | `Contract Complete` — §2 | importer contract test `test_rotation_over_lifetime_supported` |
| Rotation runtime (spawn + per-frame) | `Prototype Complete` — §2 | `ParticleSystem::update` line 821 |
| Orbit runtime (XZ plane) | `Prototype Complete` — §3 | `ParticleSystem::update` lines 824-831; `hidden_smoke_orbit_rune` |
| Orbit resource contract | `Contract Complete` — §5 | `test_orbit_contract_parsed` |
| Backend validated | `Backend Validated` | Rotation/orbit are CPU-side only; shader consumes scalar rotation via existing vertex path |
| Platform validated | Not yet | No mobile / WebGPU-specific validation done |

---

## 9. Future Directions (Out of Scope for P2-1)

- Per-axis angular velocity (matching Unity's `separateAxes`).
- Rotation curve over particle lifetime (non-linear speed).
- Orbit around arbitrary axis (currently Y-axis only).
- Orbit radius animated over lifetime.
- Per-particle orbit phase / random start offset.
