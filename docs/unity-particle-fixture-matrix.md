# Unity ParticleSystem Fixture Corpus & Diagnostic Matrix

> **Status: Backend Validated**  
> Covers PVFX-R7 fixture corpus for the first Pika Particle/VFX Prototype Complete milestone.

## Supported Unity Modules (Deterministic Mapping)

| Module | Fixtures | Importer Fields | Converter → Resource |
|--------|----------|-----------------|---------------------|
| **InitialModule** | all 5 fixtures | `startLifetime`, `startSpeed`, `startSize`, `startColor`, `duration`, `loop`, `prewarm`, `simulationSpace`, `playOnAwake`, `maxNumParticles` | `particleLifetimeSeconds`, directed `startSpeed` lowering, `worldSpaceEmission`, `startSize`, `startColorR/G/B/A`, `looping`, `maxParticles` |
| **EmissionModule** | looped_burst, one_shot, color_size_velocity, shape_variation, unsupported | `rateOverTime`, `m_Bursts[]` with `time`, `count`, `cycleCount`, `repeatInterval`, `probability` | `spawnRatePerSecond`, `burstCount` |
| **ShapeModule** | all 5 fixtures | `shapeType` (0/4=Sphere, 2=Cone, 5=Box), `radius`, `angle`, `m_Box` | `emitterShape`, `sphereRadius`, `coneAngleDegrees`, `boxExtents` |
| **ColorOverLifetimeModule** | color_size_velocity | `gradient.alphaKeys` → `colorAlphaOverLifetime` curve | `colorAlphaOverLifetime` keys |
| **SizeOverLifetimeModule** | color_size_velocity, shape_variation | `m_Curve` time/value pairs → `sizeOverLifetime` | `sizeOverLifetime` keys |
| **VelocityOverLifetimeModule** | color_size_velocity, snowfall | `linearX/Y/Z` scalar values | explicit `velocityMin/Max` vector override |
| **TextureSheetAnimationModule** | pika_flipbook_explosion, curated imports with sprite sheets | `m_Tiles` / `numTilesX/Y`, `m_FrameCount`, `m_Cycles`, `m_RandomRow` | `flipbookTilesX/Y`, `flipbookFrameCount`, `flipbookPlayRate`, `flipbookLooping`, `flipbookRandomStart` |
| **SubUVParticlesModule** | contract/smoke inline fixtures | `m_Tiles` / `numTilesX/Y`, `m_FrameCount`, `m_Cycles`, `m_RandomRow` | `flipbookTilesX/Y`, `flipbookFrameCount`, `flipbookPlayRate`, `flipbookLooping`, `flipbookRandomStart` |
| **ParticleSystemRenderer** | all 5 fixtures | `m_Material.guid`, `renderAlignment`, `sortMode` | `materialBindingId` (GUID → UUID format) |
| **CollisionModule** | contract tests; inline focused fixture; legacy `fixture_unsupported_modules.yaml` | `type` (0=Plane only, non-plane → diagnostic), `dampen`, `bounce` | `collisionEnabled`, `collisionPlaneY`, `collisionBounceFactor` |
| **RotationOverLifetimeModule** | contract/smoke inline | `z` (scalar), `scalar` | `rotationSpeed` |
| **NoiseModule** | legacy `fixture_unsupported_modules.yaml` (now subset-supported) | `strength.scalar` | `noiseAmplitude` |
| **ForceOverLifetimeModule** | legacy `fixture_unsupported_modules.yaml` (now subset-supported); smoke | `x/y/z.scalar` | `externalForce` vector |
| **ExternalForcesModule** | contract/smoke inline | `multiplierCurve.scalar` (subset; multi-key → diagnostic) | `externalForcesMultiplier` |
| **InheritVelocityModule** | contract/smoke inline | `curve.scalar` | `inheritVelocityFactor` |
| **TriggerModule** | contract tests; inline focused fixture | `enabled`, `colliderType` (0=Plane only, non-plane → diagnostic), `m_Colliders` list (must contain at most one Plane entry; multiple → diagnostic) | `triggerEnabled`, `triggerColliderType`, `triggerPlaneY`, `triggerSpawnsSubEmitter` (Prototype runtime) |

## Unsupported Modules (Deterministic Diagnostics)

| Module | Fixture | Diagnostic Behavior |
|--------|---------|-------------------|
| **TrailsModule** | unsupported_modules | Tracked with `TrailsModule` diagnostic |
| **SubEmittersModule** | unsupported_modules | Tracked with `SubEmittersModule` diagnostic |
| LightsModule | — | Would produce diagnostic if enabled |
| CustomDataModule | — | Would produce diagnostic if enabled |
| LimitVelocityOverLifetimeModule | — | Would produce diagnostic if enabled |
| ColorBySpeedModule | — | Would produce diagnostic if enabled |
| SizeBySpeedModule | — | Would produce diagnostic if enabled |
| RotationBySpeedModule | — | Would produce diagnostic if enabled |

## Fixture Corpus Summary

| Fixture File | Particles | Emitters | Key Features Tested |
|-------------|-----------|----------|-------------------|
| `fixture_looped_burst.yaml` | 500 | 1 | Looping + emission rate + 2 burst entries + sphere shape + renderer material |
| `fixture_one_shot.yaml` | 100 | 1 | Non-looping + single burst + no rate over time (pure one-shot) + unique color |
| `fixture_color_size_velocity.yaml` | 300 | 1 | ColorOverLifetime (3 alpha keys) + SizeOverLifetime (3 curve keys) + VelocityOverLifetime (linear X/Y/Z) |
| `fixture_shape_variation.yaml` | 200 | 1 | Cone shape (type=2) + angle + SizeOverLifetime |
| `fixture_unsupported_modules.yaml` | 100 | 1 | Mixed diagnostic fixture: `ForceOverLifetimeModule`/`NoiseModule`/`CollisionModule` (plane type 0)/`TextureSheetAnimationModule` exercise curated subset-supported parsing; `TrailModule`/`SubModule` remain fully unsupported and produce deterministic diagnostics. The fixture file name is retained for backwards reference only — it no longer means all listed modules are rejected. |

## Budget Validation

| Budget Rule | Validation Function | Test Coverage |
|------------|-------------------|-------------|
| `maxParticles == 0` → reject | `validateParticleEffectResource` | budget tests |
| `maxEmitters == 0` → reject | Same | budget tests |
| Emitter count exceeds `maxEmitters` → reject | Same | budget tests |
| Emitter `maxParticles == 0` → reject | Same | budget tests |
| Emitter `particleLifetimeSeconds <= 0` → reject | Same | budget tests |
| Total emitter capacity exceeds effect `maxParticles` → reject | Same | budget tests |
| Material binding count exceeds `maxMaterialBindings` → reject | Same | budget tests |
| Texture binding count exceeds `maxTextureBindings` → reject | Same | budget tests |
| Invalid curves (unsorted keys) → reject | Same | budget tests |
| Duplicate emitter names → reject | Same | budget tests |
| Invalid material/asset/prefab binding UUID → reject | Same | budget tests |

## Fixture Corpus Tests

All 5 fixture YAML files are parsed through `parseUnityParticleSystemFile()` and converted through `convertUnityParticleToResource()` → `validateParticleEffectResource()` → `buildPackageEntry()` → `ParticleEffectResource::loadInto()` (ResourceManager round-trip).

`simulationSpace` lowering is now explicit and deterministic: local-space emitters (`worldSpaceEmission = false`) move both future spawns and alive particles with host displacement from `ParticleFrameInput::hostVelocity`, while world-space emitters keep absolute particle positions after spawn.

See `tests/unity_particle_fixture_corpus_tests.cpp` for the full test implementation.
