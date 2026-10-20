# Particle Collision / Trigger / Sub-emitter — L2 Resource & Runtime Contract

> Status: **Contract Complete** for Collision and `Contract Complete` for Trigger importer; runtime wiring for Trigger is **Prototype Complete** and must stay flagged until scene-collider linking is added.
> This document closes TODO P1-3 “定义基础 collision / trigger / sub-emitter contract” by collecting the facts across `ParticleEmitterAssetDesc`, `ParticleEmitterDesc`, `ParticleSystem::update`, `parseCollisionModule`, `parseTriggerModule`, and the contract tests / smoke tests / visible-sample-equivalents into a single authoritative contract.
>
> Any subset not implemented below must fail closed with deterministic diagnostics or be explicitly marked `Prototype/Legacy fallback` per `engine-wide constraints`.

---

## 1. Domain Concepts

| Concept | Description | Code Anchor |
|---------|-------------|-------------|
| Collision | Hard surface event: a particle’s path intersects an explicit plane at world `y = collisionPlaneY` and is reflected by `collisionBounceFactor`. Optional sub-emitter is spawned on contact. | `ParticleEmitterDesc::collisionEnabled`, `parseCollisionModule`, `ParticleSystem::update` bounce branch |
| Trigger | Inside / outside / enter event scoped to an explicit collider shape per particle. Curated subset accepts Plane only (collider `y = triggerPlaneY`); scene-collider linking and non-plane colliders must fail closed. | `ParticleEmitterAssetDesc::trigger*`, `parseTriggerModule` |
| Sub-emitter | Secondary `ParticleSystem` emitter spawned by a primary emitter on a deterministic event. The curated subset supports “spawn when collision occurs” or “rule-based tags for trigger”; both back onto `collisionSpawnsSubEmitter` plumbing today. | `ParticleEmitterDesc::collisionSpawnsSubEmitter`, `subEmitterMaxParticles` |
| Spawn budget | Hard cap on the number of sub-emitter particles that may be allocated per primary event. Closes unbounded recursion: `subEmitterMaxParticles` is enforced at runtime before any sub-emitter allocation. | `subEmitterMaxParticles`, telemetry `subEmitterSpawns` |
| Recursion guard | A sub-emitter may not itself spawn another sub-emitter via collision. The runtime refuses recursive sub-emitters and increases `telemetry.budgetDenials` instead of allocating. | `ParticleSystem` sub-emitter spawn guard |

---

## 2. Curated Subset — Collision

| Field | Type | Behavior | Code Anchor |
|-------|------|----------|-------------|
| `collisionEnabled` | bool | Authoring toggle. | Resource + runtime + importer |
| `collisionPlaneY` | float | Plane at `y = collisionPlaneY`. Default 0.0. | Runtime bounce branch |
| `collisionBounceFactor` | float in `[0,1]` | Reflection velocity scalar after collision. | `ParticleSystem::update` |
| `collisionSpawnsSubEmitter` | bool | When true and a collision event occurs, spawn particles from the linked sub-emitter up to `subEmitterMaxParticles`. | Runtime sub-emitter branch |
| `subEmitterMaxParticles` | uint32 | Hard ceiling for one-collision sub-emitter spawn; `0` is rejected by the validator when `collisionSpawnsSubEmitter = true`. | Validator invariant |
| `collisionUsesWorldPlane` | bool | True (default): the plane is in world space. False is reserved and currently must be true for deterministic behaviour. | Not used in non-world plane yet — refined separately |

**Unity importer mapping (`CollisionModule`)**:

- `enabled: 1` required.
- `type: 0` required. Non-plane types reject with `Unsupported CollisionModule type: <n>`.
- `dampen` consumed as `(1 - dampen)` for `collisionBounceFactor`.
- `bounce` consumed directly for `collisionBounceFactor` when present.
- Other Unity `CollisionModule` fields (`radius`, `quality`, plane normal) are out of the curated subset and ignored only when `type == 0`; out-of-curved-plane fields do not silently lower to runtime and must not be present in fixtures.

**Runtime deterministic invariants:**

- A particle whose `position.y` crosses the plane during the most-recent update step generates exactly one collision event with telemetry counter `collisionEvents++`.
- The reflected velocity is `v.y = -v.y * collisionBounceFactor`; tangential velocity is unchanged. This is the entire curated behaviour.
- When `collisionSpawnsSubEmitter == true`, the spawned sub-emitter may not have `collisionSpawnsSubEmitter == true` itself; the runtime refuses recursion.

---

## 3. Curated Subset — Trigger

| Field | Type | Behavior | Code Anchor |
|-------|------|----------|-------------|
| `triggerEnabled` | bool | Authoring toggle. | Resource + importer |
| `triggerColliderType` | int | Only `0` (Plane) is accepted at import time; non-plane is a deterministic reject. | `parseTriggerModule` |
| `triggerPlaneY` | float | Plane at `y = triggerPlaneY`. Default 0.0. Curated subset uses the same world-plane model as Collision. | Runtime Prototype today; reserved |
| `triggerSpawnsSubEmitter` | bool | Reserved authoring flag for “spawn sub-emitter when trigger event fires”. Curated subset accepts the field but the trigger ⇒ sub-emitter link is **Prototype Complete**: it does not chain sub-emitters automatically today and must be tagged accordingly. | Validator cross-field |
| `triggerSubEmitterName` | string | Logical sub-emitter name. Validator requires non-empty when `triggerSpawnsSubEmitter == true`. PPtr / scene-collider GUID resolution is out of scope and fails closed in the importer. | Validator cross-field |

**Unity importer mapping (`TriggerModule`)**:

- `enabled: 1` required.
- Single Plane collider via inline `colliderType: 0` accepted.
- Inline `colliderType > 0` rejects with `Unsupported TriggerModule colliderType: <n> (only Plane colliderType 0 is supported)`.
- `m_Colliders` list with a non-Plane `colliderType` rejects with `Unsupported TriggerModule non-Plane collider …`.
- `m_Colliders` list with more than one collider rejects with `… multiple colliders …` regardless of type (curated subset allows exactly one Plane).
- Each `m_Colliders` PPtr with a `guid` (i.e. scene collider reference) is treated as a non-plane collider for the purposes of this curated subset because the importer cannot resolve the referenced collider shape; fixtures that contain any `m_Colliders` list with `guid` fields must be rejected at the resource/validator layer as Prototype support for trigger-by-name only.

**Runtime deterministic invariants:**

- `triggerEnabled == true` is accepted; no runtime behaviour change is mandated in the curated subset. The runtime flag is reserved for a future “death-on-plane-crossing” parity with collision bounce.
- Trigger enabling does NOT silently enable collision bounce. This is a hard contract: enabling `TriggerModule` does not auto-reflect particles — that would be silent fallback. If both behaviours are required, the author must enable `CollisionModule` separately.
- The sub-emitter link is honest about being Prototype: enabling `triggerSpawnsSubEmitter == true` does NOT spawn particles from `triggerSubEmitterName` automatically; the runtime still emits deterministic telemetry rather than speculative spawns.

**Out of curated subset (must fail closed):**

- Non-plane collider types (sphere, box, mesh) at import or in resource.
- Multiple colliders per emitter.
- Scene-collider linking (PPtr / GUID).
- Inside/outside/enter/exit event distinction — the curated subset supports a single trigger event and does not distinguish between them.
- Per-event velocity-magnitude-dependent sub-emitter spawn count.

---

## 4. Curated Subset — Sub-emitter

The curated subset of sub-emitter is wired through Collision only:

| Origin event | Field | Behavior | Telemetry |
|--------------|-------|----------|-----------|
| Collision contact | `collisionSpawnsSubEmitter` | Spawn up to `subEmitterMaxParticles` particles from a linked sub-emitter at the contact point. | `subEmitterSpawns += N` (never exceeds `subEmitterMaxParticles`) |
| Trigger event | `triggerSpawnsSubEmitter` | Authoring flag only; runtime does NOT spawn automatically (Prototype Complete). | No automatic increment |

**Recursion guard:**

- A sub-emitter emitter that itself has `collisionSpawnsSubEmitter == true` is rejected at runtime rather than recursively spawning. Telemetry records the deny.
- A sub-emitter emitter that has `triggerSpawnsSubEmitter == true` is allowed only because the trigger path is non-spawning in the current runtime; if a future iteration wires trigger spawning, the same recursion guard must hold.

---

## 5. Validation Invariants (`validateParticleEffectResource`)

| Invariant | Failure diagnostic |
|-----------|--------------------|
| `collisionBounceFactor < 0` or `> 1` | `emitter '<name>' has invalid collisionBounceFactor (must be 0-1)` |
| `collisionSpawnsSubEmitter == true` and `subEmitterMaxParticles == 0` | `emitter '<name>' has subEmitterMaxParticles == 0 when collisionSpawnsSubEmitter is true` |
| `triggerEnabled == true` and `triggerColliderType != 0` | `emitter '<name>' has unsupported triggerColliderType (only Plane 0 is supported)` |
| `triggerSpawnsSubEmitter == true` and `triggerSubEmitterName.empty()` | `emitter '<name>' has triggerSpawnsSubEmitter=true but triggerSubEmitterName is empty` |

**Fingerprint stability:** all collision and trigger fields are folded into `appendContentFingerprint`. The order is fixed and testable; changing any field changes the source hash deterministically.

---

## 6. Unity Importer Diagnostics (`parseCollisionModule`, `parseTriggerModule`)

| Symptom | Diagnostic | Action |
|---------|-----------|--------|
| `CollisionModule.enabled = 1`, `type = <n> != 0` | `Unsupported CollisionModule type: <n> (only plane type 0 is supported)` | Bypass lowering; emitter keeps `collisionEnabled = false`. |
| `TriggerModule.enabled = 1`, inline `colliderType = <n> > 0` | `Unsupported TriggerModule colliderType: <n> (only Plane colliderType 0 is supported)` | Bypass lowering; emitter keeps `triggerEnabled = false`. |
| `TriggerModule.enabled = 1`, `m_Colliders` list contains non-plane colliderType | `Unsupported TriggerModule non-Plane collider (only Plane colliderType 0 is supported)` | Bypass lowering. |
| `TriggerModule.enabled = 1`, `m_Colliders` list contains > 1 collider | `Unsupported TriggerModule multiple colliders (curated subset allows exactly one Plane collider)` | Bypass lowering. |

Both parsers must push to `unsupportedDiagnostics` and early-return without mutating other fields. The importer cannot partially apply the module — that would constitute a silent fallback.

---

## 7. Sample Coverage (MMO shapes)

This closes TODO P1-3 acceptance “能覆盖命中炸裂、落地溅射、死亡二次喷发等 MMO 打击感基础效果”。

| MMO shape | Source | Visible / hidden / focused evidence |
|-----------|--------|--------------------------------------|
| 命中炸裂 (hit splash) | `CollisionModule` + `collisionSpawnsSubEmitter` | `particle_system_hidden_smoke::test_collision_bounce`, importer contract test `collision_plane_supported`, converter lowering tests in `unity_particle_importer_smoke_tests.cpp`. |
| 落地溅射 (impact bounce) | `CollisionModule` plane + bounce | Hidden smoke `test_collision_bounce`; visible-sample-equivalent for plane-bounce in `pika_particle_visible_sample_equivalents`. |
| 死亡二次喷发 (death-time sub-emitter) | `CollisionModule` + `collisionSpawnsSubEmitter` + lifetime end | Hidden smoke `test_sub_emitter_chain`; visible-sample-equivalent parity in the same suite. Trigger-based death is Prototype and out of the curated subset. |

Trigger sample coverage is reserved for a future iteration once runtime wiring is added. Until then, enabling TriggerModule in a fixture is an acceptance test only: it exercises the importer + resource + validator and is exercised by the contract tests in this document; the runtime does not add new visible particles for triggers today.

---

## 8. Test Coverage Matrix

| Layer | Test | Fixture / File | Verifies |
|-------|------|----------------|----------|
| Import contract | `collision_plane_supported` | `tests/unity_particle_importer_tests.cpp` | Plane collision parses successfully. |
| Import contract | `collision_non_plane_rejected` | same | Non-plane collision rejected with deterministic diagnostic. |
| Import contract | `trigger_plane_collide_supported` | same | Plane trigger accepted. |
| Import contract | `trigger_sphere_collide_rejected` | same | Inline non-plane collider rejected. |
| Import contract | `trigger_multiple_colliders_rejected` | same | Multi-collider list rejected. |
| Fixture corpus | `fixture_unsupported_modules` mixed diagnostic | `tests/unity_particle_fixture_corpus_tests.cpp` | Trail / Sub still reject; collision / force / noise / flipbook subset-supported. |
| Hidden smoke | `test_collision_bounce` | `tests/particle_system_hidden_smoke.cpp` | Particle bounced by plane, telemetry increments `collisionEvents`. |
| Hidden smoke | `test_sub_emitter_chain` | same | Sub-emitter spawned on collision up to `subEmitterMaxParticles`; recursion guard prevents infinite sub-emitter chaining. |
| Contract (resource) | `trigger_validator_*` and `collision_validator_*` (already-existing 6 + new 3) | `tests/particle_asset_resource_contract_tests.cpp` | Fingerprint stability and cross-field invariant coverage. |
| Visible-sample-equivalent | plane-bounce emitter path | `tests/pika_particle_visible_sample_equivalents.cpp` | End-to-end through `ParticleSystem::update` for collision + sub-emitter. |

Trigger sample coverage lines do not have visible-sample-equivalent evidence today because runtime wiring is Prototype Complete; the contract tests in this document lock the importer + validator behaviour.

---

## 9. Backend Capability Requirements

| Capability | Required for Collision | Required for Trigger | Notes |
|------------|---------------------|---------------------|-------|
| CPU simulation | yes | yes | Plane crossing computed in `ParticleSystem::update`. |
| RHI draw | yes | no (until prototype lifted) | Visual rendering unchanged; trigger currently produces no new draw geometry. |
| Push constants | unchanged from `ParticlePushConstants` | same | No additional push constants needed for curated subset. |
| Sub-emitter recursion budget | enforced in runtime | Prototype | Sub-emitter bound by `subEmitterMaxParticles` at spawn time. |

---

## 10. What Is NOT in This Contract

To prevent future scope drift:

- Per-collision velocity curve (only flat `collisionBounceFactor`).
- Sphere / box / mesh / arbitrary-plane collider shapes for Collision.
- Any non-Plane collider type for Trigger.
- Scene-collider linking for Trigger (PPtr / GUID resolution).
- Trigger event distinction (inside / outside / enter / exit).
- Multi-trigger chaining (one trigger event per emitter cross).
- Sub-emitter event selection (single linked emitter; multiple sub-emitters per primary emitter out of scope).
- Trigger-spawns-sub-emitter automatic runtime wiring (Prototype Complete today; must remain flagged until a future iteration implements deterministic scene-collider linking).

Any new capability must add a discriminating field in `ParticleEmitterAssetDesc` or `ParticleEmitterDesc`, add validator invariants, add focused contract tests, add documentation here, and update `docs/unity-particle-fixture-matrix.md` and `docs/particle-vfx-readiness-matrix.md` before the production path is opened.

---

## 11. Status Summary

| Sub-item | Status | Evidence |
|----------|--------|----------|
| Collision import contract | Backend Validated | `parseCollisionModule`, plane-type 0 only, focused contract tests. |
| Collision runtime | Backend Validated | Plane crossing + bounce; sub-emitter chain with recursion guard. |
| Collision hidden smoke | Backend Validated | `particle_system_hidden_smoke::test_collision_bounce`, `test_sub_emitter_chain`. |
| Trigger import contract | Contract Complete | `parseTriggerModule` for plane-only collider; diagnostic reject for non-plane / multiple colliders. |
| Trigger resource contract | Contract Complete | `triggerEnabled` / `triggerColliderType` / `triggerPlaneY` / `triggerSpawnsSubEmitter` fields + validator invariants + fingerprint. |
| Trigger runtime wiring | Prototype Complete | Runtime accepts flag; no visible particle behaviour wired yet (Prototype per engine-wide constraints). Telemetry-only upgrade is reserved for a future iteration. |
| Sub-emitter recursion guard | Backend Validated | Enforced in runtime; budget deny recorded in `telemetry.budgetDenials`. |
| Native visible pixel verification | Not claimed | Gated under `Platform Validated` in `docs/particle-vfx-readiness-matrix.md`. |