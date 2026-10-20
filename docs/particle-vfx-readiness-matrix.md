# Pika Particle / VFX Readiness Matrix

**Document status:** `Prototype Complete` local acceptance passed; native visible-pixel signoff is still open, and `Production Ready` is not claimed.

This document is the PVFX-R9 production gate for the Pika particle/VFX work. It separates the local product-complete path from external platform, corpus, native visible-pixel, and product review gates. External validation handling is recorded in `docs/particle-vfx-external-validation-log.md`.

## Status Labels

| Label | Meaning for particle/VFX | Current use |
|---|---|---|
| `Contract Complete` | Public contracts, deterministic validation rules, diagnostics, and focused tests exist. | Unity importer contract, particle resource contract, RHI L2/lowering contracts. |
| `Prototype Complete` | A supported path can run end to end for curated fixtures, but is not broadly backend/platform validated. | Unity ParticleSystem subset and Pika product demo path. |
| `Backend Validated` | The implementation is exercised through the local backend or backend contract tests named in evidence. | Null RHI particle rendering, ResourceManager/package loading, Pika runtime and hidden smoke. |
| `Platform Validated` | A real target platform has been validated with lifecycle, surface/window, memory, and representative workload evidence. | Not claimed for particle/VFX. |
| `Production Ready` | Backend, platform, corpus, performance, visual review, diagnostics, and final smoke gates are closed. | Not claimed. |

## Readiness Matrix

| Domain | Current status | Evidence | Production gate |
|---|---|---|---|
| CPU particle runtime | `Backend Validated` | `particle_system`, `particle_system_hidden_smoke`, allocator-governed runtime checks. | Full CI and representative product workload soak. |
| Internal particle resource contract | `Backend Validated` | `particle_asset_resource_contract`, `particle_resource_package_smoke`. | Broader resource migration/corpus compatibility if asset schema expands. |
| Unity ParticleSystem importer | `Backend Validated` for curated subset | `unity_particle_importer_contract`, `unity_particle_importer_smoke`, `unity_particle_fixture_corpus`, `asset_importer`. | Larger Unity corpus, richer `.meta` coverage, artist-approved semantics, and explicit rejection/extension for unsupported modules. |
| Internal package and ResourceManager load/reload | `Backend Validated` | Package write/read/load, hash mismatch rejection, reload generation/rollback tests. | Full CI and production package graph validation with real material/texture/shader assets. |
| Pika VFX runtime binding | `Backend Validated` | `pika_particle_vfx_runtime_tests`, `pika_particle_vfx_hidden_smoke`, UUID stable-ID binding, visual-binding telemetry. | Gameplay/product integration review and product event coverage beyond the demo event. |
| RHI particle L2/lowering | `Contract Complete` plus Null `Backend Validated` | `docs/rhi-particle-l2-draw-semantics.md`, `docs/rhi-particle-lowering-contract.md`, `rhi_particle_lowering_contract`, `rhi_particle_render_hidden_smoke`, `rhi_particle_textured_hidden_smoke`. | Native Vulkan/Metal/WebGPU backend validation and visible pixel verification. |
| Particle shader texture sampling | `Backend Validated` on Null RHI; visible runtime remains `Prototype Complete` | `particles.slang` Slang source, generated SPIR-V/MSL/WGSL/reflection artifacts, `ParticleRenderer` explicit sampled texture descriptor binding, deterministic failure when textured draw lacks a texture binding in the contract path, plus visible-runtime prototype path with renderer-owned fallback texture when a texture resource id is declared but no native texture object is resolved yet. | Native descriptor binding validation, product texture residency wiring, and visible pixel verification on Vulkan/Metal/WebGPU. |
| Flipbook / SubUV contract | `Backend Validated` with local visible-pixel evidence | `docs/particle-flipbook-subuv-contract.md`, importer/resource tests, `particle_system_hidden_smoke::test_flipbook`, `pika_particle_visible_sample_equivalents::test_flipbook_fire`, and `pika_particle_vfx_visible_pixel_tests` hash delta against no-particle baseline for `pika_flipbook_explosion.yaml`. | Broader atlas-feature parity and cross-backend native visible-pixel validation. |
| Shape / velocity emission contract | `Backend Validated` | `docs/particle-shape-velocity-contract.md`, importer/runtime lowering tests, hidden smoke for directional/radial paths, visible-sample-equivalent coverage for cone/sphere/box/directional/radial. | Native visible pixel verification and broader authoring subset coverage. |
| Trail/Beam visual family contract | `Contract Complete` | `ParticleVisualFamily::Trail` / `Beam` enum, `visualFamilyFromBinding` dispatch, `ParticleVisualBinding` validator whitelist with cross-field invariants (`isTrail`/`isBeam`), 6 contract tests in `particle_asset_resource_contract_tests.cpp`, **集中 contract 文档 `docs/particle-trail-beam-contract.md`** 收口轨迹采样/生命周期/宽度/UV 分段/beam 端点 5 类语义与 reserved fail-closed 子集。 | Native visible pixel verification on Vulkan/Metal/WebGPU. |
| Trail/Beam shader and draw path | `Backend Validated` on local Vulkan path | `trail_beam.slang` Slang source with `vertexMain`/`fragmentMain`, generated SPIR-V/MSL/WGSL/reflection, `TrailBeamDrawPlan` non-billboard vertex generation, `ParticleRenderer` trail/beam pipeline dispatch, hidden smoke validates telemetry + draw plan geometry, visible preview has additional trail-width / anchor tuning for local product inspection, and `pika_particle_vfx_visible_pixel_tests` verifies `pika_trail_sword.yaml` changes final hidden-window render output hash against a no-particle baseline. | Cross-backend native visible-pixel verification and product signoff for representative trail/beam fixtures. |
| Collision/Sub-emitter contract | `Backend Validated` | `CollisionModule` importer subset (plane type 0 only), `ParticleEmitterAssetDesc` resource fields (`collisionEnabled`, `collisionPlaneY`, `collisionBounceFactor`, `collisionSpawnsSubEmitter`), `ParticleVfxRuntimeSession::convertEmitter()` lowering, validator invariants, fingerprint, hidden smoke (collision bounce + sub-emitter chain), focused importer contract tests (plane success, non-plane diagnostic). **集中 contract 文档 `docs/particle-collision-trigger-subemitter-contract.md`** 收口碰撞/trigger/sub-emitter/collision-spawns-sub-emitter/触发条件/spawn budget/recursion guard 全部语义。 | Native visible pixel verification. |
| Rotation / orbit contract | `Backend Validated` | `docs/particle-rotation-orbit-contract.md`, importer contract tests, `hidden_smoke_orbit_rune`, visible-sample-equivalent `test_orbit_rune`. | Native visible pixel verification and broader orbit-axis semantics. |
| Noise / force contract | `Backend Validated` | `docs/particle-noise-force-contract.md`, importer/resource tests, hidden smoke deterministic telemetry, visible-sample-equivalent `test_noise_cloud`. | Native visible pixel verification and richer force/noise subsets. |
| Inherit velocity contract | `Backend Validated` | `docs/particle-inherit-velocity-contract.md`, importer/runtime tests, hidden smoke and visible-sample-equivalent evidence for host-velocity propagation. | Native visible pixel verification and broader attachment-motion scenarios. |
| Billboard alignment contract | `Backend Validated` | `docs/particle-billboard-alignment-contract.md`, validator + lowering, `hidden_smoke_billboard_modes`, visible-sample-equivalent `test_billboard_modes`. | Native visible pixel verification and backend-facing orientation parity. |
| Mesh particle contract | `Prototype Complete` | `docs/particle-mesh-particle-contract.md`, runtime smoke, visible-sample-equivalent `test_mesh_particle_spawn`, focused renderer prototype tests for positive path + deterministic unknown-mesh diagnostic. | Native mesh-particle backend validation and production mesh resource binding. |
| Trigger contract | Contract Complete (importer) / Prototype Complete (runtime) | `TriggerModule` plane-only collider importer subset (non-plane colliderType + multiple colliders both reject with deterministic diagnostics); `ParticleEmitterAssetDesc::trigger*` fields + validator cross-field invariants (`triggerColliderType==0`, `triggerSpawnsSubEmitter` requires non-empty `triggerSubEmitterName`); fingerprint stable. Runtime currently tags trigger metadata with no visible particle behaviour; scene-collider linking + trigger-spawns-sub-emitter automatic wiring stays Prototype per engine-wide constraints. | Runtime trigger-spawns-sub-emitter behaviour wiring; scene-collider linking. |
| Pika product executable path | `Prototype Complete` (Pika local acceptance) | `arp_pika.exe --mode particle-vfx-demo --particle-fixture <fixture>` loads real Unity fixtures (electricity/snowfall/dust) via ResourceManager with prototype dependency resources. Visible mode (`--mode particle-vfx-visible --particle-fixture <fixture>`) routes fixture path into visible runtime. Hidden smoke covers demo and integrated package mode, `pika_particle_vfx_visible_smoke` exercises both six representative Unity-importable fixtures and Pika-native representative fixtures for trail / beam / flipbook, and `pika_particle_vfx_visible_pixel_tests` verifies final hidden-window render hash deltas for representative electricity / trail / flipbook fixtures against a no-particle baseline. Pika local integration evidence remains below Platform Validated because broader native visible-window review and external platform validation are still open. | Automated native visible-window pixel validation for broader representative corpus and artist/product visual review. |
| Mobile lifecycle | Not validated | No Android/iOS particle lifecycle evidence yet. | Android/iOS pause/resume, surface/layer loss, memory pressure, drawable/swapchain behavior, and quality downgrade validation. |
| Device-specific performance budgets | Not validated | Local telemetry exists; device budgets are not tuned. | Low/default/high profile budgets per target device class with CPU, upload bytes, draw calls, dropped spawns, and memory limits. |

## Backend Capability Labels

| Backend | Current particle capability | Diagnostic / fallback rule |
|---|---|---|
| Null | `Backend Validated` for command recording, frame submit/present telemetry, draw-plan validation. | Used for hidden smoke and contract verification only. |
| Vulkan | `Prototype` with local visible smoke evidence. | Must not be promoted until representative Windows hardware validates native pipeline, descriptor, upload, draw, captured pixels, and visible output behavior. |
| Metal | `Prototype` / contract documented. | Must not be promoted until macOS/iOS hardware validates native pipeline, descriptor, upload, draw, and lifecycle behavior. |
| WebGPU | `Prototype` / contract documented. | Must not be promoted until browser/runtime validation covers push-constant emulation, bind groups, upload, canvas lifecycle, and diagnostics. |

## Hot-Path Evidence

| Constraint | Local evidence | Remaining production check |
|---|---|---|
| Bounded runtime allocation | Particle runtime and draw-plan containers use `core::pmr` aliases and pre-sized vectors in focused tests. | Full workload allocation telemetry under representative effect counts. |
| No hot-path file/network I/O | Import/package write happens before runtime trigger; simulation and draw-plan smoke paths do not load files per frame. | Full product frame audit after final product integration. |
| No per-frame shader compilation | Particle shader is Slang source compiled by CMake into SPIR-V/MSL/WGSL/reflection artifacts. | Native backend startup/pipeline cache validation. |
| No per-draw pipeline creation | Integrated smoke creates pipeline/draw plan setup outside repeated draw recording; `ParticleRenderer` reuses alpha/additive pipelines and descriptor slots. | Native pipeline cache evidence under Vulkan/Metal/WebGPU. |
| Backend-owned staging/upload | RHI particle draw command contract uses backend-owned staging intent and rejects foreign staging buffers in contract tests. | Native backend memory/upload telemetry. |
| Deterministic texture binding | Contract path requires an explicit sampled texture binding and fails when it is absent; visible-runtime preview currently allows a renderer-owned prototype fallback texture when a texture resource id exists but no native texture object has been resolved yet. | Product texture-resource upload and descriptor residency validation under native backends, with prototype fallback removed or explicitly retained only as non-production preview behavior. |

## Unsupported Unity Modules And Corpus Limits

The curated PVFX-R7 fixture corpus is the current accepted local corpus. Unsupported or ambiguous Unity semantics must fail with deterministic diagnostics instead of producing guessed production output.

| Area | Current rule | Production gate |
|---|---|---|
| Unsupported modules | Only `TrailModule` and `SubEmittersModule` remain fully rejected with deterministic diagnostics from `fixture_unsupported_modules.yaml`. `ForceOverLifetimeModule` (x/y/z scalar), `NoiseModule` (strength), `CollisionModule` (plane type 0 only), and `TriggerModule` (plane-only collider — runtime Prototype) are now curated subset-supported — non-plane collision/trigger types and other ambiguous variants still produce deterministic diagnostics. `LightsModule`, `CustomDataModule`, `LimitVelocityOverLifetimeModule`, `ColorBySpeedModule`, `SizeBySpeedModule`, and `RotationBySpeedModule` remain explicitly rejected. `ExternalForcesModule` is now curated subset-supported (scalar multiplier only) with deterministic diagnostics for multi-key multiplier curves. | Review a larger Unity corpus and either implement deterministic mappings or keep explicit rejections for the still-unsupported set. |
| Ambiguous material metadata | Missing or ambiguous renderer material GUID metadata rejects import. | Validate against real project `.meta` files and multi-material authoring patterns. |
| ColorOverLifetime alpha key path | Current importer does not support Unity's nested `gradient.m_Gradient.alphaKeys[N].alpha` path. | Implement deterministic parsing or keep explicit unsupported diagnostics before broad corpus import. |
| Prefab-folder co-import | Standalone `--unity-particle-prefab` exists; mixed character/prefab-folder `--include-particles` is not generalized. | Add only if the production asset layout requires mixed imports. |
| Corpus size | Curated fixtures cover one-shot, looping, material binding, lifetime color/size, velocity/shape variation, budgets, and unsupported-module rejection. | Run and record larger real Unity corpus validation. |

## Mobile Budget Assumptions

Current local acceptance uses desktop/default quality telemetry. Mobile is not platform validated.

Budget profiles (from `ParticleQualityProfile` in `particle_system.h`):

| Profile | Max Particles | Max Emitters | Target Upload Bytes/frame | Dropped Spawn Budget |
|---|---|---|---|---|
| Low | 768 | 16 | ≤ 36 KB | < 10% dropped |
| Medium | 2048 | 32 | ≤ 96 KB | < 5% dropped |
| High | 5000 | 48 | ≤ 234 KB | < 2% dropped |
| Desktop | 12000 | 96 | ≤ 562 KB | < 1% dropped |

| Profile | Current assumption | Required before Production Ready |
| Low | Conservative max particles, lower upload bytes, and stricter dropped-spawn diagnostics are expected but not tuned to devices. | Device-specific Android/iOS budgets with memory pressure and pause/resume behavior. |
| Default | Local desktop/default profile drives current product demo telemetry. | Per-device target budgets for active particles, CPU simulation time, upload bytes, draw calls, and diagnostics thresholds. |
| High | Intended for capable desktop/native hardware only until measured. | Hardware-specific opt-in budget and thermal/performance review. |

## External Validation Gates

These gates remain outside local Prototype Complete. Closing them is required before `Production Ready`:

- Full Windows MinGW CI build and full test suite.
- Windows Vulkan representative hardware validation.
- macOS/iOS Metal validation.
- Browser/runtime WebGPU validation.
- Android lifecycle validation: pause/resume, surface loss, memory pressure, and quality downgrade.
- iOS lifecycle validation: layer/drawable behavior, pause/resume, memory pressure, and quality downgrade.
- Larger Unity particle corpus validation beyond curated fixtures.
- Artist/product visual review of the accepted Pika effect.
- Automated native visible-window pixel verification for the imported particle effect.
- Device-specific low/default/high performance budgets.

Current handling status for these gates is recorded in `docs/particle-vfx-external-validation-log.md`. The handling log records attempted local execution and explicit blockers; it does not close the gates as validation evidence.

## Final R9 Verdict

PVFX-R0 through PVFX-R8 are locally accepted for the curated Pika product path below Platform Validated. Representative trail / flipbook now have local hidden-window visible-pixel hash evidence, but broader native visible-window review and all external validation gates remain open. The feature must remain below `Production Ready` until every external validation gate above is closed and recorded with exact evidence.
