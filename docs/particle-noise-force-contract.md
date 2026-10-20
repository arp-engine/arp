# Particle Noise / Force / External Field — L2 Resource & Runtime Contract

> Status: **Contract Complete** for importer pipeline (NoiseModule, ForceOverLifetimeModule, ExternalForcesModule). Runtime perturbation is **Prototype Complete**. The noise/force fields are non-visual (particle motion only) so visual-fidelity tracking is not applicable; the contract focuses on deterministic behaviour and bounded allocation.
> This document closes TODO P2-2 "定义 noise / force field 资源 contract" by collecting the facts across `ParticleEmitterAssetDesc`, `ParticleEmitterDesc`, `ParticleSystem::update` perturbation branches, `parseNoiseModule`, `parseForceModule`, `parseExternalForcesModule`, and the importer contract tests into a single authoritative contract.
>
> Any subset not implemented below must fail closed with deterministic diagnostics or be explicitly marked `Prototype/Legacy fallback` per `engine-wide constraints`.

---

## 1. Domain Concepts

| Concept | Description | Code Anchor |
|---------|-------------|-------------|
| Noise perturbation | Per-particle, deterministic, seed-stable, time-varying position offset. Uses the particle's `seed` and `normalizedAge` as phase inputs to a multi-axis trigonometric function; no external random source. | `ParticleEmitterDesc::noiseFrequency`, `noiseAmplitude`; `ParticleSystem::update` noise branch |
| External force (constant) | Per-emitter acceleration vector applied to every particle's velocity each frame. Typically used for wind, gravity well, or directed push. | `ParticleEmitterDesc::externalForce`; runtime velocity integration |
| External force multiplier | Per-emitter scalar that scales `externalForce` at runtime. Mapped from Unity `ExternalForcesModule.multiplierCurve`. | `ParticleEmitterDesc::externalForcesMultiplier`; `parseExternalForcesModule` |
| Deterministic noise | Given the same `seed`, `noiseFrequency`, `noiseAmplitude`, `particleAge` sequence, two independent `ParticleSystem` runs produce identical particle trajectories. | Verified by `hidden_smoke_noise_deterministic` |
| Bounded perturbation | Noise and force offsets are linear in `deltaSeconds`; there is no unbounded accumulation, no external data dependency, and no per-particle state beyond the existing position/velocity. | All perturbation terms scale by `deltaSeconds` |

---

## 2. Curated Subset — Noise Perturbation

| Field | Type | Behaviour | Code Anchor |
|-------|------|----------|-------------|
| `noiseFrequency` | float | Base oscillation frequency in Hz. Controls the rate at which the trigonometric phase advances with `normalizedAge()`. Unity noise default ≈ 1.0; converter sets `noiseFrequency = 1.0f` as fixed value for the curated subset. | `ParticleEmitterDesc::noiseFrequency` |
| `noiseAmplitude` | float | Maximum spatial displacement per axis (in world units). Unity `strength.scalar` maps directly. | `ParticleEmitterDesc::noiseAmplitude`; `parseNoiseModule` `strength.scalar` → `noiseStrength` |

**Unity importer mapping (`NoiseModule`):**

- `enabled: 1` required. When disabled the module is ignored.
- `strength.scalar` → `noiseStrength` (stored as importer asset field, lowered to `noiseAmplitude` at conversion).
- `frequency` — not parsed; produces deterministic diagnostic if non-default values are present. The curated subset uses a fixed `noiseFrequency = 1.0f` at conversion time.
- Multi-axis strength (`strengthX`, `strengthY`, `strengthZ`) — not parsed; produces deterministic diagnostic. The curated subset applies a uniform 3D displacement.
- Other Unity `NoiseModule` fields (`scrollSpeed`, `damping`, `octaves`, `octaveMultiplier`, `octaveScale`, `remap`, `positionAmount`, `rotationAmount`, `sizeAmount`) are out of the curated subset and produce a deterministic diagnostic.

**Runtime deterministic invariants (full contract):**

```
phase = normalizedAge * noiseFrequency * 2π + seed * 0.127
noiseX = sin(phase * 1.3) * cos(phase * 0.7)
noiseY = cos(phase * 0.9) * sin(phase * 1.1)
noiseZ = sin(phase * 0.8) * cos(phase * 1.4)
position += {noiseX, noiseY, noiseZ} * noiseAmplitude * deltaSeconds
```

- The trigonometric composition is deterministic: the same `(seed, noiseFrequency, noiseAmplitude, sequence of age deltas)` produces the same trajectory.
- Noise is additive to position only; velocity is unchanged.
- When `noiseFrequency <= 0.0f` or `noiseAmplitude <= 0.0f`, the noise branch is skipped entirely.

---

## 3. Curated Subset — External Force

| Field | Type | Behaviour | Code Anchor |
|-------|------|----------|-------------|
| `externalForce` | `core::Vec3` | Per-emitter constant acceleration in world units/s². Applied to every particle's velocity each frame. Lowered from Unity `ForceOverLifetimeModule` `{x, y, z}`. | `ParticleEmitterDesc::externalForce`; `parseForceModule` |
| `externalForcesMultiplier` | float | Scalar that multiplies `externalForce` at runtime. Maps from Unity `ExternalForcesModule.multiplierCurve.scalar`. Default 1.0. | `ParticleEmitterDesc::externalForcesMultiplier`; `parseExternalForcesModule` |

**Unity importer mapping (`ForceOverLifetimeModule`):**

- `enabled: 1` required.
- `x.scalar`, `y.scalar`, `z.scalar` → `forceX`, `forceY`, `forceZ` (importer fields lowered to `externalForce` at conversion).
- Multi-axis curves (`x.curve`, `y.curve`, `z.curve`) are not parsed; produce deterministic diagnostic.
- Other `ForceOverLifetimeModule` fields (`randomize`, `space`) are out of the curated subset and produce a deterministic diagnostic.

**Unity importer mapping (`ExternalForcesModule`):**

- `enabled: 1` required.
- `multiplierCurve.scalar` → `externalForcesMultiplier`. Default 1.0 when absent.
- Multi-key multiplier curve (`multiplierCurve.maxCurve`, `multiplierCurve.minCurve`, multi-key curves) is not parsed; produces deterministic diagnostic.

**Runtime behaviour:**

```
velocity += externalForce * externalForcesMultiplier * deltaSeconds
```

- When `externalForce == {0, 0, 0}`, the force branch is skipped entirely.
- Multiplier is applied per-frame; there is no energy integration or acceleration limit.

---

## 4. Non-goals & Closed Failures

The following are **explicitly out** of the curated subset. If encountered in production data they must either:
- (a) be ignored deterministically without observable effect (documented below), or
- (b) produce a deterministic failure with diagnostic message.

| Scenario | Handling | Justification |
|----------|----------|---------------|
| NoiseModule multi-axis curve (`strengthX`, `strengthY`, `strengthZ`) | Deterministic diagnostic from importer | Not in contract; uniform displacement used |
| NoiseModule frequency curve | Deterministic diagnostic from importer | Not in contract; converter sets `noiseFrequency = 1.0f` |
| NoiseModule octaves / damping / scroll | Deterministic diagnostic from importer | Not in contract |
| ForceOverLifetime randomize / space | Deterministic diagnostic from importer | Not in contract; constant world-space force used |
| ExternalForcesModule multi-key multiplier curve | Deterministic diagnostic from importer | Not in contract |
| Per-particle noise override | Not implemented | Not in contract |
| External force field components (Unity `ForceField`) | Not implemented | Requires scene-level force-field system (out of scope for P2-2) |
| Noise animated via curve over particle lifetime | Not implemented | Uses fixed frequency; amplitude is per-emitter constant |
| Per-particle noise override | Not implemented | Not in contract |
| External force field components (Unity `ForceField`) | Not implemented | Requires scene-level force-field system (out of scope for P2-2) |
| Noise animated via curve over particle lifetime | Not implemented | Uses fixed frequency; amplitude is per-emitter constant |

---

## 5. Resource Contract — `ParticleEmitterAssetDesc`

Fields on `ParticleEmitterAssetDesc` (stored in the resource) and their lowering to runtime `ParticleEmitterDesc`:

| Resource Field | Type | Lowered To | Importer Source |
|---------------|------|------------|-----------------|
| `noiseFrequency` | float | `ParticleEmitterDesc::noiseFrequency` | Converter fixed to 1.0f |
| `noiseAmplitude` | float | `ParticleEmitterDesc::noiseAmplitude` | NoiseModule `strength.scalar` |
| `externalForce` | `core::Vec3` | `ParticleEmitterDesc::externalForce` | ForceOverLifetimeModule `{x,y,z}.scalar` |
| `externalForcesMultiplier` | float | `ParticleEmitterDesc::externalForcesMultiplier` | ExternalForcesModule `multiplierCurve.scalar` |

Fingerprint includes all four fields (see `ParticleEffectResource::appendContentFingerprint` in `particle_asset_resources.cpp`).

Validator admits any value in range; negative noise amplitude is treated as zero motion (noise branch skipped).

---

## 6. Test Coverage

| Test | File | What It Verifies |
|------|------|------------------|
| `ForceOverLifetime supported` | `unity_particle_importer_tests.cpp` | `x=0.5, y=-1.0, z=0.2` parsed to importer fields |
| `NoiseModule strength=0.75 parsed` | `unity_particle_importer_tests.cpp` | `strength.scalar: 0.75` → `noiseStrength` |
| `ExternalForcesModule multiplier=2.5 parsed` | `unity_particle_importer_tests.cpp` | `multiplierCurve.scalar: 2.5` → `externalForcesMultiplier` |
| `hidden_smoke_noise_cloud` | `particle_system_hidden_smoke.cpp` | Noise perturbation produces alive particles; basic integration test |
| `hidden_smoke_noise_deterministic` | `particle_system_hidden_smoke.cpp` | Same seed → same particle count in two independent runs |
| `noise cloud` (resource contract) | `particle_asset_resource_contract_tests.cpp` | Resource fields survive round-trip validation |

---

## 7. Readiness Matrix Update

| Field | Status | Doc Reference |
|-------|--------|---------------|
| NoiseModule importer | `Contract Complete` — §2 | importer contract test `test_noise_module_supported` |
| ForceOverLifetimeModule importer | `Contract Complete` — §3 | importer contract test `test_force_over_lifetime` (pre-existing) |
| ExternalForcesModule importer | `Contract Complete` — §3 | importer contract test `test_external_forces_module_supported` |
| Noise runtime perturbation | `Prototype Complete` — §2 | `hidden_smoke_noise_deterministic`, `hidden_smoke_noise_cloud` |
| External force runtime | `Prototype Complete` — §3 | Existing emitter test coverage |
| Visible sample (noise cloud) | `Prototype Complete` | `tests/fixtures/unity_particle/` — fixture available |
| Backend validated | `Backend Validated` | Noise/force are CPU-side only; no shader or GPU backend dependency beyond existing particle render path |
| Platform validated | Not yet | No mobile / WebGPU-specific validation done |

---

## 8. Future Directions (Out of Scope for P2-2)

- Per-particle noise seed override (currently seed-derived deterministic).
- Frequency curve over particle lifetime.
- Multi-octave / Perlin-style noise (current is trigonometric only).
- Scene-level force fields (Unity `ForceField` component equivalent).
- Noise that affects rotation or size (currently position-only).
- Curved external forces per emitter over time.
