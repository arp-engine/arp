# Particle Flipbook / TextureSheetAnimation / SubUV - L2 Resource & Runtime Contract

> Status: **Contract Complete** for importer/resource fields (`flipbookTilesX/Y`, `flipbookFrameCount`, `flipbookPlayRate`, `flipbookLooping`, `flipbookRandomStart`). Runtime frame propagation and draw-plan UV selection are **Prototype Complete**.
> This document closes TODO P1-2 "为粒子资源补充 flipbook/subUV authoring contract" by collecting the implemented facts across `ParticleEmitterAssetDesc`, `ParticleEmitterDesc`, `ParticleInstance`, `buildParticleDrawPlanSet`, `parseTextureSheetAnimationModule`, `parseSubUvParticlesModule`, and the focused tests / smoke paths.
>
> Any subset not implemented below must fail closed with deterministic diagnostics or be explicitly marked `Prototype/Legacy fallback` per `engine-wide constraints`.

---

## 1. Domain Concepts

| Concept | Description | Code Anchor |
|---|---|---|
| Flipbook atlas | A regular 2D tile atlas addressed by `(tilesX, tilesY)` and a linear frame index. | `ParticleEmitterDesc::flipbookTilesX/Y`; `ParticlePushConstants::flipbookTilesX/Y` |
| Frame count | Number of legal frames in the atlas playback sequence. Must be `> 0` when flipbook is enabled. | `ParticleEmitterDesc::flipbookFrameCount` |
| Play rate | Frames-per-second style playback scalar used to advance `ParticleInstance::flipbookFrame`. | `ParticleEmitterDesc::flipbookPlayRate`; `ParticleSystem::update` |
| Random start | Deterministic per-particle random start frame seeded from spawn seed. | `ParticleEmitterDesc::flipbookRandomStart`; spawn logic |
| Draw-plan UV lowering | Runtime frame index is lowered into sprite draw-plan push constants so shader-side UV selection is deterministic. | `buildParticleDrawPlanSet`; `ParticlePushConstants` |

---

## 2. Curated Subset

| Field | Type | Behaviour |
|---|---|---|
| `flipbookTilesX` / `flipbookTilesY` | uint32 | Atlas grid dimensions. Must be `> 0` when flipbook is enabled. |
| `flipbookFrameCount` | uint32 | Number of playable frames. Must be `> 0`. |
| `flipbookPlayRate` | float | Constant playback rate. Non-curve, emitter-wide. |
| `flipbookLooping` | bool | When false, playback clamps at the last legal frame. |
| `flipbookRandomStart` | bool | Enables deterministic random start frame. |
| `visualBinding.isFlipbook` | bool | Declares flipbook path explicitly in resource metadata. |

Unity importer curated subset:

- `TextureSheetAnimationModule` grid-based tile atlases only.
- `SubUVParticlesModule` grid-based tile atlases only.
- `m_Tiles` / `numTilesX/Y`, `m_FrameCount`, `m_Cycles`, `m_RandomRow` are lowered.
- Sprite-list / mesh-UV / non-grid / unsupported UV channel variants must reject deterministically.

---

## 3. Resource Contract

Lowered resource/runtime fields:

| Resource Field | Runtime Field | Notes |
|---|---|---|
| `visualBinding.isFlipbook` | `ParticleMaterialBinding::isFlipbook` | Explicit visual contract flag |
| `visualBinding.flipbookTilesX/Y` | `ParticleMaterialBinding::flipbookTilesX/Y` | Metadata copy for runtime binding |
| `flipbookTilesX/Y` | `ParticleEmitterDesc::flipbookTilesX/Y` | Runtime atlas dimensions |
| `flipbookFrameCount` | `ParticleEmitterDesc::flipbookFrameCount` | Runtime legal frame count |
| `flipbookPlayRate` | `ParticleEmitterDesc::flipbookPlayRate` | Constant rate |
| `flipbookLooping` | `ParticleEmitterDesc::flipbookLooping` | Clamp vs loop |
| `flipbookRandomStart` | `ParticleEmitterDesc::flipbookRandomStart` | Deterministic spawn-time selection |

Validator invariants in `particle_asset_resources.cpp`:

- `isFlipbook == true` requires `flipbookTilesX > 0` and `flipbookTilesY > 0`.
- Invalid atlas dimensions or unsupported Unity module variants fail with deterministic diagnostics.

---

## 4. Runtime & Draw-Plan Semantics

Runtime:

- Spawn stamps each particle with atlas metadata and optional deterministic random start frame.
- Update advances `ParticleInstance::flipbookFrame` using emitter-wide `flipbookPlayRate`.
- Non-looping playback clamps; looping playback wraps in the curated subset.

Draw-plan:

- `buildParticleDrawPlanSet()` copies atlas dimensions and frame index into push constants.
- Shader-side UV selection remains grid-based and emitter-uniform except for per-particle frame index.
- No per-segment/per-row/per-sprite-list remapping is allowed in production paths without a new contract revision.

---

## 5. Explicit Non-goals / Fail-Closed Cases

| Scenario | Handling |
|---|---|
| Sprite-list flipbook frames | Deterministic diagnostic |
| Mesh UV sourced SubUV | Deterministic diagnostic |
| Non-grid atlas addressing | Deterministic diagnostic |
| Per-particle custom atlas remap curves | Not implemented |
| Material-side guessed fallback to non-flipbook sprite | Forbidden |

---

## 6. Evidence

| Evidence | File | What It Proves |
|---|---|---|
| Importer contract | `tests/unity_particle_importer_tests.cpp` | TextureSheetAnimation / SubUV curated subset lowers deterministically |
| Resource contract | `tests/particle_asset_resource_contract_tests.cpp` | Flipbook metadata and validator invariants are enforced |
| Hidden smoke | `tests/particle_system_hidden_smoke.cpp` | Flipbook particles spawn, advance frames, and stay alive through runtime |
| Visible-sample-equivalent | `tests/pika_particle_visible_sample_equivalents.cpp` | End-to-end runtime frame propagation through Pika-facing particle path |
| Fixture corpus | `tests/unity_particle_fixture_corpus_tests.cpp` | Pika-native flipbook fixture validates against resource contract |
| Shader artifact | `tests/shader_tests.cpp`, `shaders/stylized/particles.slang` | Slang-only shader path consumes atlas/frame push constants |

---

## 7. Status Ladder

| Slice | Status | Evidence |
|---|---|---|
| Importer + resource fields | Contract Complete | Importer/resource tests |
| Runtime frame propagation | Prototype Complete | Hidden smoke + visible-sample-equivalent |
| Shader artifact generation | Contract Complete | Slang + SPIR-V/MSL/WGSL/reflection tests |
| Native visible pixel parity | Not claimed | Outside this document |
