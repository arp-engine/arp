# Particle Billboard Alignment - L2 Resource & Runtime Contract

> Status: **Contract Complete** for `billboardMode` resource/runtime fields. Runtime mode propagation is **Prototype Complete**.
> This document closes TODO P2-4 "billboard alignment contract" by collecting the implemented facts across `ParticleVisualBinding.billboardMode`, `ParticleMaterialBinding.billboardMode`, `billboardModeFromString()`, `ParticleInstance::billboardMode`, `buildParticleDrawPlanSet()`, and the focused tests.
>
> Any subset not implemented below must fail closed with deterministic diagnostics or be explicitly marked `Prototype/Legacy fallback`.

---

## 1. Supported Modes

| Mode String | Lowered Enum | Meaning |
|---|---|---|
| `ViewAligned` | `0` | Screen-facing sprite billboard |
| `LocalAligned` | `1` | Local/upright alignment subset |
| `WorldAligned` | `2` | World-space fixed alignment subset |

Unknown mode strings must reject deterministically in the resource validator.

---

## 2. Resource & Runtime Contract

| Layer | Field | Behaviour |
|---|---|---|
| Resource | `ParticleVisualBinding.billboardMode` | Required when visual binding is present |
| Runtime binding | `ParticleMaterialBinding.billboardMode` | Carries mode string into emitter/runtime |
| Particle instance | `ParticleInstance::billboardMode` | Stores lowered enum per particle |
| Draw plan | `ParticlePushConstants::billboardMode` | Exposes mode to shader path |

This is an explicit L2/runtime semantic. Backend defaults must not choose billboard orientation.

---

## 3. Fail-Closed Boundaries

| Scenario | Handling |
|---|---|
| Unknown mode string | Deterministic validator failure |
| Backend-native default billboard orientation | Forbidden |
| Trail/Beam reusing billboard alignment modes | Forbidden; separate contract |

---

## 4. Evidence

| Evidence | File | What It Proves |
|---|---|---|
| Resource validator | `src/resource/particle_asset_resources.cpp`, `tests/particle_asset_resource_contract_tests.cpp` | Only known billboard modes are accepted |
| Runtime lowering | `src/resource/unity_particle_converter.cpp`, `src/renderer/particles/particle_system.h` | Mode lowers deterministically into runtime enum |
| Hidden smoke | `tests/particle_system_hidden_smoke.cpp` | Three modes map to expected enum values |
| Visible-sample-equivalent | `tests/pika_particle_visible_sample_equivalents.cpp` | Pika-facing path preserves mode mapping |

---

## 5. Status Ladder

| Slice | Status | Evidence |
|---|---|---|
| Resource/runtime mode contract | Contract Complete | Validator + lowering |
| Runtime propagation | Prototype Complete | Hidden smoke + visible-sample-equivalent |
| Native visible pixel parity | Not claimed | Outside this document |
