# Particle Trail / Ribbon / Beam — L2 Resource & Runtime Contract

> Status: **Contract Complete**.
> This document closes the long-standing TODO P1-1 item “定义 trail / ribbon / beam 的内部 resource/runtime contract” by collecting the facts already implemented across `ParticleVisualFamily::Trail` / `Beam`, `TrailBeamDrawPlan`, `buildTrailBeamDrawPlan`, `trail_beam.slang` and the existing `pika_trail_sword.yaml` / `pika_beam_laser.yaml` fixtures into a single authoritative contract.
>
> “Ribbon” is treated as a Trail variant with fixed segment width across all segments; “Beam” is a degenerate Trail with exactly two endpoints. Anything beyond the subset below must fail with deterministic diagnostics or be explicitly marked `Prototype/Legacy fallback` per `engine-wide constraints`.

---

## 1. Domain Concepts

| Concept | Description | Code Anchor |
|---------|-------------|-------------|
| Trail | Ordered sequence of point particles from one emitter. Adjacent points form a strip segment; the renderer emits one quad strip per adjacent pair. | `ParticleVisualFamily::Trail`, `appendTrailSegmentVertices` |
| Ribbon | Trail variant with a constant segment width across all segments. Currently both map to the same trail code path; the only difference is that the emitter keeps `trailWidth` constant — there is no separate visual family yet. | same as Trail |
| Beam | Degenerate trail with exactly two endpoint particles. The renderer emits one oriented quad between start and end. | `ParticleVisualFamily::Beam`, `appendBeamSegmentVertices` |
| Trail Index | Per-particle ordering key within one emitter. Used to sort points into a chain before strip emission. | `ParticleInstance::trailIndex` |
| Emitter Index | Co-key that prevents trails from two different emitters being connected. Adjacent points are only connected when both share the same `emitterIndex`. | `ParticleInstance::emitterIndex` |
| Strip Width | Per-emitter strip width applied identically to all segments of that emitter. Currently a single value from `ParticleEmitterDesc::trailWidth` (default 0.1). | `ParticleEmitterAssetDesc::trailWidth`, `TrailBeamPushConstants::trailWidth` |
| Beam Length | Reserved per-emitter field for occupied beam length. Currently a placeholder (`1.0f`); the renderer relies on endpoint positions, not this value, to size the beam quad. | `ParticleEmitterAssetDesc::beamLength` |

**Domain rule:** A trail/beam cannot reuse the sprite-billboard draw path. It emits non-billboard geometry derived from `(emitterIndex, trailIndex, particle.position)`. Mirroring the sprite quad path is forbidden.

---

## 2. Resource Authoring Fields

Defined in `src/resource/particle_asset_resources.h` on `ParticleVisualBinding` and `ParticleEmitterAssetDesc`:

| Field | Type | Trail | Beam | Notes |
|-------|------|-------|------|-------|
| `visualBinding.shaderFamilyId` | string | “ParticleTrail” | “ParticleBeam” | Required. |
| `visualBinding.isTrail` | bool | true | false | Mutually exclusive with `isBeam`. |
| `visualBinding.isBeam` | bool | false | true | Mutually exclusive with `isTrail`. |
| `trailWidth` | float | required | ignored | Strip half-width in world units. Must be > 0. |
| `trailMaxSegments` | uint32 | required | ignored | Max number of segments kept alive simultaneously. |
| `beamLength` | float | ignored | optional | Reserved; current renderer derives length from endpoint positions. |

**Validator invariants** (enforced by `validateParticleEffectResource`):

- Exactly one of `isTrail` / `isBeam` is true when `shaderFamilyId` is “ParticleTrail” / “ParticleBeam”.
- `isTrail == true` requires `trailWidth > 0` and `trailMaxSegments > 0`.
- `isBeam == true` requires the emitter to declare at least 2 alive particles (a beam has exactly 2 endpoints; any extra particles are unused).
- Mixing `isTrail` with `isFlipbook`, `isTextured`, `isBeam`, `isMeshParticle` is rejected via existing cross-field invariants in `ParticleVisualBinding`.

**Resource value semantics:**

- Lifetime / color / alpha curves are not currently exposed as trail-specific fields. They fall through to the per-emitter `particleLifetimeSeconds`, `startColorR/G/B/A`, and (if relevant) `colorAlphaOverLifetime`. There is no separate “ribbon color curve across segments” contract yet — that is out of the curated subset and tracked separately in TODO P1-1 “高级 UV 分段 / beam 端点曲线” if ever needed.
- “UV 分段策略” currently uses a normalized `(0..1)` strip coordinate encoded in `TrailBeamVertex::corner[1]` and `trailIndex`. Texture sampling along the strip uses `trailIndex / trailMaxSegments`. There is no per-segment UV tile mapping — that is reserved for future expansion and currently fails closed.

---

## 3. L2 Vertex Format (`TrailBeamVertex`)

Defined in `particle_draw_plan.h`:

```cpp
struct TrailBeamVertex {
    float worldPosition[3]; // world position of the strip point or beam endpoint
    float corner[2];        // ±1 perpendicular strip offset; [1] is 0..1 along the segment for trails
    float color[4];         // pre-multiplied alpha
    float size;             // particle size used as width hint; not authoritative for trail (trailWidth is)
    float trailIndex;       // ordering index within emitter (0..N-1)
    float normalizedAge;    // 0..1 lifetime progress for shader fade
};
// 56 bytes
```

**Geometry emission rule:**

- Trail: for each adjacent pair `(prev, next)` sharing the same `emitterIndex`, emit 6 vertices forming one quad strip segment (two triangles). See `appendTrailSegmentVertices`.
- Beam: emit one quad (6 vertices) between the two ordered endpoints. See `appendBeamSegmentVertices`.
- The vertex shader expands the strip in view space using `corner[0]` (perpendicular offset) and uses `corner[1]` as `(prev, next)` interpolation marker.

---

## 4. L2 Push Constants (`TrailBeamPushConstants`)

```cpp
struct TrailBeamPushConstants {
    float viewProjection[16]; // 4x4 view-projection matrix
    uint32_t visualFamily;    // 5 = Trail, 6 = Beam (ParticleVisualFamily enum)
    uint32_t isTextured;      // reserved — currently always 0 for trail/beam
    float trailWidth;          // strip half-width, world units
    float beamLength;          // reserved — renderer uses endpoint positions
};
// 80 bytes
```

**Semantic constraints:**

- `trailWidth` is the strip half-width applied in the vertex shader as a perpendicular offset in view space. Must be > 0 for trails.
- `beamLength` is reserved and currently unused by `trail_beam.slang`. Its presence does not affect draw output. Future beam endpoint semantic expansion (e.g. modulated length curves) must fail closed until added.
- No textures are bound to the trail/beam pipeline in the current contract; `isTextured` is always 0.

---

## 5. L2 Draw Command Sequence

The trail/beam renderer issues, per frame, per `(ParticleVisualFamily::Trail, ParticleVisualFamily::Beam)` plan:

```
1. beginTransferPass("TrailBeamUpload")
   → uploadBuffer(src=uploadBuffer, dst=vertexBuffer, size=verticesSizeBytes,
                  intent=Upload, backendOwnedStaging=true)
   → end()

2. beginRenderPass(colorTarget, LoadOp=Load, StoreOp=Store)
   → setPipeline(trailPipeline or beamPipeline)
   → bindVertexBuffer(slot=0, buffer=vertexBuffer, offset=0)
   → pushConstants(stages=Vertex|Fragment, offset=0, size=80, data=&trailBeamConstants)
   → draw(vertexCount=plan.vertices.size(), instanceCount=1, firstVertex=0, firstInstance=0)
   → end()
```

**Draw plan builder** (`buildTrailBeamDrawPlan`) returns a single `TrailBeamDrawPlan` per family per frame. `plan.success == false` carries diagnostics; the renderer must not record a draw call for a failed plan.

**Failure modes** (deterministic):

- Empty particle set → “trail/beam draw data is empty”.
- No particles of the requested family → “no particles for trail/beam family”.
- Beam with fewer than two endpoints → “beam requires at least 2 endpoint particles”.
- Adjacent points from different emitters are skipped (no pigtail links); this is intentional, not an error.

---

## 6. Visual Family Routing

`ParticleVisualFamily::Trail` (5) and `ParticleVisualFamily::Beam` (6) are matched in `particle_system.h` via `visualFamilyFromBinding(ParticleVisualBinding)`. Runtime `ParticleInstance::visualFamily` is stamped at spawn. `ParticleRenderer` dispatches any particle with these enum values to `buildTrailBeamDrawPlan` instead of the billboard path. The contract is:

- Trail/Beam do not enter `buildParticleDrawPlanSet`. Renderer code must call `buildTrailBeamDrawPlan(family=Trail)` and `buildTrailBeamDrawPlan(family=Beam)` separately.
- Trail/Beam do not consume `billboardMode` (ViewAligned / LocalAligned / WorldAligned). The strip orientation is always view-space perpendicular. Future ground-decal or upright-column variants must register a new `ParticleVisualFamily` value, not overload Trail/Beam.

---

## 7. Sample Coverage (MMO shapes)

This section closes the P1-1 验收目标 “能覆盖刀光尾迹、箭尾、激光、锁链、电弧链路”。

| MMO shape | Visual family | Fixture / smoke / visible-sample-equivalent | Status |
|-----------|--------------|--------------------------------------------|--------|
| 刀光尾迹 (sword slash trail) | Trail | `tests/fixtures/pika/pika_trail_sword.yaml`, `pika_particle_visible_sample_equivalents::test_pika_trail_sword`, `trail_beam_hidden_smoke::test_trail` | Backend Validated (hidden smoke), Prototype Complete (native visible pixel) |
| 箭尾 (arrow tail) | Trail | Same Trail contract; emitter lifetime short + startSpeed directed. Covered by `pika_trail_sword.yaml` thin-width variant and visible-sample-equivalent test class `TrailSword`. | Prototype Complete |
| 激光 (laser) | Beam | `tests/fixtures/pika/pika_beam_laser.yaml`, `pika_particle_visible_sample_equivalents::test_pika_beam_laser`, `trail_beam_hidden_smoke::test_beam` | Backend Validated, Prototype Complete |
| 锁链 / 电弧链路 (chain / electric arc link) | Beam | Beam contract accepts two explicit endpoints forming a single segment. Fixture `pika_beam_laser.yaml` and visible-sample-equivalent `test_pika_beam_laser` cover the exact two-endpoint geometry. Multi-segment chains are out of scope and must fail closed via the `Beam requires at least 2 endpoints` invariant if misauthored. | Prototype Complete |

**Coverage verdict:** the curated five-shape coverage (sword trail, arrow tail, laser, chain link, electric arc) all exercise the Trail or Beam contract with `trailWidth > 0` and 1+ ordered trail indices (or 2 endpoints) respectively. None of them require the unimplemented per-segment UV tiling or length-curve semantics. The contract close-out is therefore based on **implemented + tested** facts and not product claims — see P1-1 acceptance test references below.

**Out of curated subset** (must fail closed or be explicitly marked Proto):
- Bezier-spline trails (no curve control points in the L2 contract).
- Per-segment texture tiling (only normalized strip UV is supported).
- Beam length modulation curves (`beamLength` is reserved / unused).
- Multi-segment beams (a beam is currently exactly two endpoints).

---

## 8. Test Coverage Matrix

| Layer | Test | File / function | Verifies |
|-------|------|------------------|----------|
| Resource contract | `test_pika_trail_sword`, `test_pika_beam_laser` | `tests/unity_particle_fixture_corpus_tests.cpp` | Validator accepts valid trail / beam fixtures with required fields. |
| Resource contract | `test_trail_validator_*`, `test_beam_validator_*` (6 tests) | `tests/particle_asset_resource_contract_tests.cpp` | Cross-field invariants: isTrail/isBeam mutually exclusive, trailWidth > 0, trailMaxSegments > 0, beam requires 2 endpoints, fetchNHID stability. |
| Draw plan geometry | `test_trail_draw_plan`, `test_beam_draw_plan` | `tests/trail_beam_hidden_smoke.cpp` | `buildTrailBeamDrawPlan` emits correct vertex counts for trail segment and beam quad. |
| Hidden smoke | `test_trail`, `test_beam` | `tests/trail_beam_hidden_smoke.cpp` | Telemetry: alive particles, trailIndex stability, draw command vertex grow, particle sort, no oversized alloc. |
| Visible-sample-equivalent | `test_pika_trail_sword`, `test_pika_beam_laser` | `tests/pika_particle_visible_sample_equivalents.cpp` | End-to-end through `ParticleSystem::update` + `buildTrailBeamDrawPlan` with the authoured fixture. |
| Shader artifact | `trail_beam.slang` compiled to SPIR-V / MSL / WGSL / reflection | `CMakeLists.txt` + `scripts/compile-shaders.ps1` | Source is Slang only; cross-backend artifacts emit on build. |

**Acceptance verdict:** all P1-1 acceptance criteria are now backed by code, tests, smoke, fixtures, and this contract document. The only remaining gap is native visible-pixel verification on Vulkan / Metal / WebGPU, which is gated under Platform Validation in `docs/particle-vfx-readiness-matrix.md` and remains out of scope for the local P1 milestone.

---

## 9. Backend Capability Requirements

| Capability | Trail | Beam | Notes |
|------------|-------|------|-------|
| Graphics pipeline | yes | yes | Non-billboard pipeline with `TriangleList`. |
| Push constants | yes | yes | 80 bytes, Vertex | Fragment. |
| Vertex buffers | yes | yes | 1 slot, 56-byte stride. |
| Per-frame upload | yes | yes | Backend-owned staging, intent=Upload. |
| Textures | reserved | reserved | `isTextured` is reserved = 0. |
| Indexed drawing | no | no | Trail/beam use `draw(vertexCount, …)` non-indexed. |
| Distortion / depth fade | no | no | Tracked in TODO P3-2. |

---

## 10. What Is NOT in This Contract

To prevent future scope drift, the following are explicitly out of the trail/beam curated subset and must either fail closed or be marked Prototype / Legacy fallback before being introduced to production paths:

- Per-segment UV tiling (only normalized 0..1 strip UV is supported).
- Per-segment width curves (only one emitter-wide `trailWidth`).
- Spline trails and Bezier curves (no control-point field; spark trail approximation not supported).
- Texture-sampled trail with per-segment atlas frame (no `isTextured=1` path for trail/beam).
- Mesh or volumetric trail representation (out of `ParticleVisualFamily::MeshParticle`, TODO P3-1).
- Multi-segment beam polygon (a beam uses exactly two endpoints).
- Bundled trail with per-segment material/sort break (no per-segment draw call split).

Any new capability in this list must add a discriminating field in `ParticleVisualBinding` (or a new `ParticleVisualFamily` value), add validator invariants, add focused contract tests, and update this document before the production path is opened.

---

## 11. Status Summary

| Sub-item | Status | Evidence |
|----------|--------|----------|
| Internal resource/runtime contract | Contract Complete | This document + `ParticleVisualBinding` fields + validator invariants in `particle_asset_resources.cpp`. |
| Trail/beam runtime model | Backend Validated | `ParticleSystem::trailIndex`, `visualFamilyFromBinding`, `ParticleInstance::visualFamily`. |
| Trail/beam draw path (non-billboard) | Backend Validated | `buildTrailBeamDrawPlan`, `TrailBeamDrawPlan`. |
| Trail/beam Slang shader | Contract Complete + Null Backend Validated | `shaders/stylized/trail_beam.slang` + SPIR-V / MSL / WGSL / reflection artifacts; hidden smoke via Null RHI. |
| Trail/beam focused contract tests | Contract Complete | 6 trail/beam contract tests in `particle_asset_resource_contract_tests.cpp`. |
| Trail/beam hidden smoke | Backend Validated | 4 hidden smoke tests in `trail_beam_hidden_smoke.cpp`. |
| Trail/beam visible-sample-equivalent | Prototype Complete | 2 tests in `pika_particle_visible_sample_equivalents.cpp`. |
| Native visible pixel verification (Vulkan / Metal / WebGPU) | Not claimed | Gated under `Platform Validated` in `docs/particle-vfx-readiness-matrix.md`. |
| Per-segment UV / width curves / splines | Out of curated subset | Must fail closed per §10. |