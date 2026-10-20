# Particle Inherit Velocity / Attachment Motion — L2 Resource & Runtime Contract

> Status: **Contract Complete** for importer pipeline (InheritVelocityModule). Runtime host-velocity inheritance is **Prototype Complete**.
> This document closes TODO P2-3 "增加粒子对宿主对象速度继承的内部 contract" by collecting the facts across `ParticleEmitterAssetDesc`, `ParticleEmitterDesc`, `ParticleSystem::update` host-velocity branches, `parseInheritVelocityModule`, and the importer contract tests into a single authoritative contract.
>
> Any subset not implemented below must fail closed with deterministic diagnostics or be explicitly marked `Prototype/Legacy fallback` per `engine-wide constraints`.

---

## 1. Domain Concepts

| Concept | Description | Code Anchor |
|---------|-------------|-------------|
| Host velocity | The instantaneous world-space velocity of the particle emitter's parent object (e.g. character, projectile). Passed per frame via `ParticleFrameInput::hostVelocity`. | `ParticleFrameInput::hostVelocity`; `ParticleSystem::hostVelocity_` |
| Inherit velocity (blend) | Particles blend their own velocity toward the host velocity at a per-emitter blend factor. Produces trailing, projectile wake, or running-dust motion relative to the moving host. | `ParticleEmitterDesc::inheritVelocityFactor`; `ParticleSystem::update` lines 778-784 |
| World-space emission | When `worldSpaceEmission == false`, particle positions are also offset each frame by `hostVelocity * deltaSeconds` so particles stay attached to the moving host's local space regardless of the inherit-velocity blend. | `ParticleSystem::update` lines 772-776 |

**Typical use cases:**
- **Character sprint trail**: Particles linger behind a sprinting character with `inheritVelocityFactor ≈ 0.3-0.6` producing a smooth velocity transition from the character's speed to slow drift.
- **Projectile wake**: Projectile-attached particles inherit most of the projectile velocity (`inheritVelocityFactor ≈ 0.8-1.0`) and then decelerate via drag or external force.
- **Running dust kick-up**: Dust particles spawn with low inherit factor but the host position offset (`worldSpaceEmission = false`) keeps them near the running path.

---

## 2. Curated Subset — Inherit Velocity

| Field | Type | Behaviour | Code Anchor |
|-------|------|----------|-------------|
| `inheritVelocityFactor` | float | Per-emitter blend factor in [0, 1]. Controls how strongly particle velocity converges toward host velocity each frame. Unity `curve.scalar` maps directly. | `ParticleEmitterDesc::inheritVelocityFactor`; `parseInheritVelocityModule` |

**Unity importer mapping (`InheritVelocityModule`):**

- `enabled: 1` required. When disabled the module is ignored.
- `curve.scalar` → `inheritVelocityFactor`. Default 0.0 when absent.
- `mode` (0 = Current, 1 = Initial) — not parsed. The curated subset always blends toward the current frame's host velocity (equivalent to Unity mode `Current`).
- Multi-axis curves (`curve.maxCurve`, `curve.minCurve`, multi-key curves) are not parsed. The curated subset uses a single scalar value.
- Other `InheritVelocityModule` fields are out of the curated subset and are ignored; they must not produce silent observable differences.

**Runtime behaviour:**

```
velocity += inheritVelocityFactor * (hostVelocity - velocity) * deltaSeconds
```

Where:
- `hostVelocity` is the per-frame host object velocity from `ParticleFrameInput`.
- When `inheritVelocityFactor == 0.0f`, the branch is skipped entirely.
- When `inheritVelocityFactor >= 1.0f` and `deltaSeconds` is large enough, the particle velocity converges to host velocity in a single frame (exponential approach).
- The blend is applied per-frame every update; convergence rate follows `1 - exp(-inheritVelocityFactor * dt)`.

**World-space emission interaction:**

```
if (!worldSpaceEmission) {
    position += hostVelocity * deltaSeconds;
}
```

This runs before the inherit-velocity blend, ensuring that even particles with `inheritVelocityFactor = 0` remain attached to the moving host in local space.

---

## 3. Non-goals & Closed Failures

The following are **explicitly out** of the curated subset. If encountered in production data they must either:
- (a) be ignored deterministically without observable effect (documented below), or
- (b) produce a deterministic failure with diagnostic message.

| Scenario | Handling | Justification |
|----------|----------|---------------|
| InheritVelocityModule `mode = 1` (Initial) | Ignored; always uses Current mode | Not in contract; initial-velocity capture would require additional per-particle state |
| Multi-key multiplier curve (`curve.maxCurve`, `curve.minCurve`) | Ignored; first scalar used | Not in contract |
| Per-axis inherit factor (Unity per-axis separate curves) | Not implemented | Not in contract; uniform scalar used |
| Inherit velocity with rotation inheritance | Not implemented | Not in contract; velocity-only |

---

## 4. Resource Contract — `ParticleEmitterAssetDesc`

Fields on `ParticleEmitterAssetDesc` (stored in the resource) and their lowering to runtime `ParticleEmitterDesc`:

| Resource Field | Type | Lowered To | Importer Source |
|---------------|------|------------|-----------------|
| `inheritVelocityFactor` | float | `ParticleEmitterDesc::inheritVelocityFactor` | InheritVelocityModule `curve.scalar` |

Fingerprint includes `inheritVelocityFactor` (see `ParticleEffectResource::appendContentFingerprint` in `particle_asset_resources.cpp`). The full emitter fingerprint is:

```cpp
hasher.appendFloat(emitter.inheritVelocityFactor);
```

Validator admits `inheritVelocityFactor` in range `[0, 10]`; negative values are clamped to 0 (branch skipped). Values > 1 produce faster-than-frame convergence which is valid but unusual — the validator emits a warning but does not reject.

---

## 5. Pipeline Flow

```
Unity ParticleSystem YAML
  └─ InheritVelocityModule.enabled: 1
     └─ curve.scalar: 0.75
        └─ parseInheritVelocityModule()
           → importer ParticleEmitter field inheritVelocityFactor = 0.75
              └─ converter: ParticleEmitterAssetDesc.inheritVelocityFactor = 0.75
                 └─ converter: ParticleEmitterDesc.inheritVelocityFactor = 0.75
                    └─ ParticleSystem::update() — velocity blend at lines 778-784
                       └─ velocity += factor * (hostVelocity - velocity) * deltaSeconds
```

---

## 6. Test Coverage

| Test | File | What It Verifies |
|------|------|------------------|
| `InheritVelocityModule factor=0.75 parsed` | `unity_particle_importer_tests.cpp` | `curve.scalar: 0.75` → `inheritVelocityFactor` via `parseInheritVelocityModule` |

*Smoke verification:* Pika visible sample (`arp_pika`) validates attachment motion consistency through the interactive character-sprint and projectile-wake particle scenarios.

---

## 7. Readiness Matrix

| Field | Status | Doc Reference |
|-------|--------|---------------|
| InheritVelocityModule importer | `Contract Complete` — §2 | importer contract test `test_inherit_velocity_module_supported` |
| Inherit velocity runtime blend | `Prototype Complete` — §2 | `ParticleSystem::update` lines 778-784 |
| World-space emission offset | `Prototype Complete` — §2 | `ParticleSystem::update` lines 772-776 |
| Visible sample (character trail) | `Prototype Complete` | Available through Pika interactive sample |
| Backend validated | `Backend Validated` | Inherit velocity is CPU-side only; no shader or GPU backend dependency beyond existing particle render path |
| Platform validated | Not yet | No mobile / WebGPU-specific validation done |

---

## 8. Future Directions (Out of Scope for P2-3)

- Per-axis inherit factor (matching Unity's per-axis curve separation).
- Initial-velocity capture mode (Unity `InheritVelocityModule.Mode.Initial`).
- Rotation inheritance from host object.
- Inherit velocity dampened by particle drag/lifetime curve.
- Multi-key curves (non-uniform blend over particle lifetime).
