# Pika Particle / VFX Product Acceptance Checklist

**Document status:** `Prototype Complete` local acceptance passed for PVFX-R0 through PVFX-R8.

This checklist defines what "Prototype Complete" means for the Pika particle/VFX
feature and is checked at the end of PVFX-R8. It does not replace the
`Production Ready` gate, which requires external platform, visible-pixel, and
larger corpus validation.

PVFX-R9 production-gate status is recorded in
`docs/particle-vfx-readiness-matrix.md`.

---

## 1. Import Pipeline

| # | Check | Evidence |
|---|-------|----------|
| 1.1 | Unity ParticleSystem YAML fixture parsed deterministically | `arp_unity_particle_importer_smoke_tests`: hash stability test |
| 1.2 | Unsupported Unity modules produce deterministic diagnostics | `arp_unity_particle_fixture_corpus_tests`: `fixture_unsupported_modules` pipeline test |
| 1.3 | Import → convert → `ParticleEffectResource` conversion works | `arp_unity_particle_importer_smoke_tests`: converter field validation |
| 1.4 | Import → package → ResourceManager round-trip works | `arp_unity_particle_importer_smoke_tests`: smoke test 4 |
| 1.5 | Fixture corpus files import with stable hashes across runs | `arp_unity_particle_fixture_corpus_tests`: deterministic hash test |
| 1.6 | Budget limits enforced (maxParticles, maxEmitters, curves) | `arp_unity_particle_fixture_corpus_tests`: 10 budget tests |
| 1.7 | CLI importer path exists for Unity particle prefabs | `arp_asset_importer --unity-particle-prefab <file> --output-package <dir>` and `asset_importer` ctest |

## 2. Internal Asset Package

| # | Check | Evidence |
|---|-------|----------|
| 2.1 | Package written via `InternalAssetPackageWriter` and read back | `arp_resource_package_io_contract_tests` |
| 2.2 | Package loaded into `ResourceManager` with ready state | `arp_pika_particle_package_smoke` |
| 2.3 | Particle resource findable by UUID and alias | `arp_pika_particle_package_smoke` |
| 2.4 | Package reload: content change triggers generation increment | `arp_resource_package_reload_contract_tests` |
| 2.5 | Package reload: invalid data rolls back cleanly | `arp_resource_package_reload_contract_tests` |
| 2.6 | Hash mismatch rejection on payload tampering | `arp_resource_package_io_contract_tests` |

## 3. VFX Runtime

| # | Check | Evidence |
|---|-------|----------|
| 3.1 | `ParticleVfxRuntimeSession` initialized from `ResourceManager` | `arp_pika_particle_vfx_runtime_tests`: `test_event_to_effect_binding` |
| 3.2 | `TriggerOneShot` event produces active particles | `arp_pika_particle_vfx_runtime_tests` |
| 3.3 | `StartLooping` / `Stop` / `Restart` lifecycle works | `arp_pika_particle_vfx_runtime_tests`: `test_stop_restart_lifecycle` |
| 3.4 | Deterministic seed produces same particle count across runs | `arp_pika_particle_vfx_runtime_tests`: `test_deterministic_reset` |
| 3.5 | Budget exhaustion handled with diagnostic | `arp_pika_particle_vfx_runtime_tests`: `test_budget_exhaustion` |
| 3.6 | Missing resource / stale generation detected | `arp_pika_particle_vfx_runtime_tests`: `test_invalid_binding_diagnostics` |
| 3.7 | Telemetry reports active effects, particles, drops, diagnostics | `arp_pika_particle_vfx_hidden_smoke` |
| 3.8 | `collectActiveParticles()` returns particle data for rendering | `arp_pika_particle_vfx_integrated_hidden_smoke` (Phase 5) |
| 3.9 | Shutdown produces final report with counts | `arp_pika_particle_vfx_runtime_tests` |

## 4. RHI Particle Rendering

| # | Check | Evidence |
|---|-------|----------|
| 4.1 | L2 draw semantics defined for billboard particles | `docs/rhi-particle-l2-draw-semantics.md` |
| 4.2 | Lowering contract covers Null / Vulkan / Metal / WebGPU | `docs/rhi-particle-lowering-contract.md` |
| 4.3 | Particle pipeline created through RHI with explicit state | `arp_rhi_particle_lowering_tests`: pipeline creation test |
| 4.4 | `buildParticleDrawPlan` produces correct vertices + commands | `arp_rhi_particle_lowering_tests`: draw plan build test |
| 4.5 | `recordParticleDrawPlan` issues RHI commands through encoder | `arp_rhi_particle_lowering_tests`: RHI draw recording test |
| 4.6 | Draw plan budget enforcement works (particle count > max) | `arp_rhi_particle_lowering_tests`: budget enforcement test |
| 4.7 | Back-to-front deterministic sort verified | `arp_rhi_particle_lowering_tests`: deterministic sort test |
| 4.8 | Reject foreign (non-encoder-owned) staging buffer | `arp_rhi_particle_lowering_tests`: reject foreign staging test |
| 4.9 | Null backend smoke test: 2 frames recorded and presented | `arp_rhi_particle_render_hidden_smoke` |
| 4.10 | Particle shader compiled from Slang to all targets | CMake slangc rules, shader outputs in `shaders/stylized/` |

## 5. Integrated Pipeline (PVFX-R8)

| # | Check | Evidence |
|---|-------|----------|
| 5.1 | Fixture imported via `buildParticlePackage()` pipeline | `arp_pika_particle_vfx_integrated_hidden_smoke` Phase 1 |
| 5.2 | Package written to disk and read back | `arp_pika_particle_vfx_integrated_hidden_smoke` Phase 2-3 |
| 5.3 | VFX runtime triggered from imported package | `arp_pika_particle_vfx_integrated_hidden_smoke` Phase 4 |
| 5.4 | Simulated particles extracted via `collectActiveParticles()` | `arp_pika_particle_vfx_integrated_hidden_smoke` Phase 5 |
| 5.5 | Draw plan built from runtime particle data | `arp_pika_particle_vfx_integrated_hidden_smoke` Phase 5 |
| 5.6 | RHI commands recorded through Null backend | `arp_pika_particle_vfx_integrated_hidden_smoke` Phase 6 |
| 5.7 | All kFrameCount frames submitted and presented | `arp_pika_particle_vfx_integrated_hidden_smoke` Phase 6 |
| 5.8 | Performance telemetry recorded (active, upload bytes, draw calls, CPU time, drops) | `arp_pika_particle_vfx_integrated_hidden_smoke` Phase 8 |
| 5.9 | Zero unexpected diagnostic events | `arp_pika_particle_vfx_integrated_hidden_smoke` verdict |
| 5.10 | Deterministic: same fixture produces same results | Import hash stability + runtime deterministic reset tests |
| 5.11 | Pika product executable can trigger imported effect | `arp_pika.exe --mode particle-vfx-demo --particle-fixture tests\fixtures\unity_particle\fixture_one_shot.yaml --frames 2` |
| 5.12 | Pika visible runtime can loop representative Unity-importable fixtures through the visible path | `arp_pika_particle_vfx_visible_smoke`: electricity / snowfall / dust / one-shot / looped-burst / shape-variation hidden-window visible smoke |

## 6. Allocator & Hot Path Constraints

| # | Check | Evidence |
|---|-------|----------|
| 6.1 | Runtime containers use `core::pmr` allocators | `particle_vfx_runtime.h/.cpp`: `core::pmr::Vector`, `core::pmr::String` |
| 6.2 | No default STL dynamic containers in production runtime code | Code audit (PVFX-R1 allocator governance) |
| 6.3 | No per-frame shader compilation | Shaders compiled offline via CMake slangc |
| 6.4 | No per-frame pipeline creation (pipeline created once) | Integrated smoke creates pipeline once |
| 6.5 | No file I/O or network I/O on hot path | Integrated smoke: file I/O only in import + package write phases |
| 6.6 | No bare `new`/`delete` in runtime path | Code audit |
| 6.7 | Backend owns upload/staging resources | `ParticleRhiDrawDesc` with `backendOwnedStaging` flag |

## 7. Diagnostics & Error Handling

| # | Check | Evidence |
|---|-------|----------|
| 7.1 | Null ResourceManager → deterministic diagnostic | `test_null_resource_manager` |
| 7.2 | Missing effect resource → `EffectResourceNotFound` | `test_invalid_binding_diagnostics` |
| 7.3 | Stale generation → `staleGenerationDiagnostics` | Runtime stale detection logic |
| 7.4 | Budget exhausted → `BudgetExhausted` diagnostic | `test_budget_exhaustion` |
| 7.5 | Unsupported quality profile → `invalidQualityDiagnostics` | `test_unsupported_quality_profile_rejected` |
| 7.6 | Unsupported Unity modules → deterministic diagnostics | Fixture corpus unsupported-module test |
| 7.7 | Invalid resource contract → validation failure | Resource contract tests (PVFX-R2) |
| 7.8 | Package hash mismatch → rejection | IO contract tests (PVFX-R4) |

## Summary: Status Per Domain

| Domain | Status | Last Verified |
|--------|--------|---------------|
| Import pipeline | `Backend Validated` | PVFX-R7 |
| Internal asset package | `Backend Validated` | PVFX-R7 |
| VFX runtime | `Backend Validated` | PVFX-R7 |
| RHI particle render (Null) | `Backend Validated` | PVFX-R6 |
| RHI particle render (Vulkan) | `Prototype` | RHI lowering contract |
| RHI particle render (Metal) | `Prototype` | RHI lowering contract |
| RHI particle render (WebGPU) | `Prototype` | RHI lowering contract |
| Pika integrated pipeline | `Backend Validated` | PVFX-R8 focused tests passed |
| Product acceptance (this doc) | `Prototype Complete` local acceptance | PVFX-R8 focused tests + product executable telemetry passed |
| Production ready | not yet claimed | see `docs/particle-vfx-readiness-matrix.md` external gates |

## Known Limitations at Prototype Complete

1. **RHI `draw()` (non-indexed) API missing** — The particle draw plan emits
   6 unique vertices per quad (no index buffer). Current RHI only has
   `drawIndexed`. The lowering contract documents this gap. Visible rendering
   on native backends requires a new `draw()` entry on `RenderPassEncoder`.
2. **Vulkan/Metal/WebGPU backends not validated** — Only Null backend has been
   exercised. Native rendering requires the `draw()` API plus backend-specific
   pipeline creation, shader module compilation, and descriptor binding.
3. **Native visible-pixel verification** — The Pika executable now has
   `--mode particle-vfx-demo`, which imports the Unity particle fixture through
   the product path and reports frame/render-submission telemetry. Native
   visible-window pixel validation and artist/product visual review remain
   external validation gates.
4. **ColorOverLifetime alpha key path** — The current importer does not support
   Unity's nested `gradient.m_Gradient.alphaKeys[N].alpha` path for alpha-over-
   lifetime curves. Module tracking and enabled/disabled detection are correct.
5. **Prefab-folder co-import not yet generalized** — The standalone asset
   importer supports `--unity-particle-prefab <file> --output-package <dir>`.
   A broader `--include-particles` mode for mixed character/prefab-folder import
   can be added later if the production asset layout requires it.
6. **Performance budgets not device-specific** — Desktop quality profile defaults
   are used. Mobile-specific budgets (Low/Medium) have not been tuned for
   specific devices.
