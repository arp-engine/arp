# Animation Blending & IK Implementation Plan

## Status: Contract Complete + Prototype Complete

All five phases are code-complete with contract/unit/integration tests passing
through the CMake build system on the Win/MinGW/Vulkan preset.

## Phases

### Phase 0: core::math IK/blend primitives — ✅ Contract Complete

Added lerp, distance, quatSlerp (shortest-arc with nlerp fallback),
quatFromToRotation (handles parallel/anti-parallel), quatLookAt to
`src/core/math.h`. These are the reusable math foundation for all blending
and IK -- no per-module reimplementation.

- Tests: `tests/unit/core_math_tests.cpp` → `arp_core_math_tests`
- Status: Contract Complete (all assertions pass)

### Phase 1: Cross-fade pose blending — ✅ Contract Complete + Prototype Complete

`src/animation/pose_blend.{h,cpp}`: `blendPoses(a, b, t, out)` per-joint TRS
nlerp blend; `blendPosesWeighted(poses, weights, out)` N-pose normalized blend.
Fail-closed on size mismatch.

`AnimationStateMachine` now samples both previous and current clips during a
transition and cross-fades via `blendPoses`, exposing `blendedPose()`.

`SkinnedMeshPlayer` gains `update(const std::pmr::vector<LocalTransform>&)`
overload for externally blended poses.

- Tests: `tests/integration/animation_pose_blend_tests.cpp` → `arp_animation_pose_blend_tests`
- Status: Contract Complete + Prototype Complete

### Phase 2: 1D BlendTree — ✅ Contract Complete + Prototype Complete

`src/animation/blend_tree.{h,cpp}`: `BlendTree1D` with parameter-driven
clip blending. `valid()` enforces ≥2 nodes, non-null clips, strictly
ascending thresholds. `evaluateBlendTree1D` phase-synchronizes sampling
via normalizedTime × durationSeconds, then blends the bracketing pair.

- Tests: `tests/integration/blend_tree_tests.cpp` → `arp_blend_tree_tests`
- Status: Contract Complete + Prototype Complete

### Phase 3: Two-Bone IK + Look-at IK — ✅ Contract Complete + Prototype Complete

`src/animation/ik.{h,cpp}`:

- `solveTwoBoneIK`: cosine rule for root angle, pole-vector projection onto
  plane perpendicular to reach direction, quatFromToRotation for joint deltas.
- `solveLookAtIK`: desired-direction rotation with max-angle clamp via
  axis-angle decomposition, weight slerp.
- `worldRotationToLocal(parentModelRotation, worldDelta)`: converts model-space
  delta to local via inverse(parent)·delta.

Uses `core::math` types exclusively -- avoids the animation::Mat4 column-major
vs core::Mat4 row-major ambiguity (engine-wide constraints Matrix Layout constraint).

- Tests: `tests/integration/ik_solver_tests.cpp` → `arp_ik_solver_tests`
- Status: Contract Complete + Prototype Complete

### Phase 4: Foot IK (static terrain) — ✅ Contract Complete + Prototype Complete

`src/animation/foot_ik.{h,cpp}`: `applyFootIK` adjusts leg poses so feet
conform to static terrain. Ground height is injected via a function pointer
(`FootIKGroundQuery::sampleHeight`) so the animation layer has zero coupling
to the pcg module. Per-leg pipeline: ground query → offset clamp → contact
weight → two-bone IK → foot normal alignment via finite-difference gradient.

`SkinnedMeshPlayer` integration: `setFootIK()`, `setFootContactWeights()`,
`setRootWorldPos()`, `clearFootIK()`. The IK pass runs inside
`finalizeFromLocalPose()` after the first model-pose build, then rebuilds
model pose + skin matrices with the corrected local pose. Scratch buffer is
sized once and reused per frame (no per-frame allocation).

- Tests: `tests/integration/foot_ik_tests.cpp` → `arp_foot_ik_tests`
- Tests: `tests/integration/skinned_mesh_player_foot_ik_tests.cpp` → `arp_skinned_mesh_player_foot_ik_tests`
- Status: Contract Complete + Prototype Complete

## Test Summary

All 6 test targets pass via CTest on `win-auto-debug` preset:

| Target | Tests |
|--------|-------|
| `arp_core_math_tests` | lerp/distance/quatSlerp/quatFromToRotation/quatLookAt |
| `arp_animation_pose_blend_tests` | blend endpoints, size mismatch, nlerp, cross-fade, weighted |
| `arp_blend_tree_tests` | valid() rules, midpoint blend, clamping, phase sync |
| `arp_ik_solver_tests` | two-bone reach/clamp/weight, look-at ahead/90°/clamp, worldRotationToLocal |
| `arp_foot_ik_tests` | flat ground, slope, swing phase, null query, offset clamping |
| `arp_skinned_mesh_player_foot_ik_tests` | flat no-op, lift, disabled, swing |

## What's NOT Done (Future Work)

- **Backend Validated**: Not yet. All IK/blending is CPU-only; no GPU/skinning
  backend changes were made. Visual validation via RenderDoc capture has not
  been performed (no game-side integration yet).
- **Platform Validated**: Windows only. No Android/iOS validation.
- **Game integration**: The WoCC/Project Lane character pipelines do not yet
  call `setFootIK` / `BlendTree1D` / `AnimationStateMachine::blendedPose()`.
  The APIs are available but unused by game code.
- **2D BlendTree**: Only 1D BlendTree implemented (per user request:
  "Cross-fade + 1D BlendTree").
- **Animation event/notify system**: Out of scope for this plan.

## Architecture Notes

### Matrix Layout Compliance (engine-wide constraints)

- `animation::Mat4` is **column-major** (m[c*4+r], translation in m[12..14]).
- `core::Mat4` is **row-major** (m[r*4+c]).
- IK solvers (`ik.cpp`) use `core::math` types exclusively, avoiding the
  column-major/row-major ambiguity.
- `foot_ik.cpp` explicitly extracts translation from `animation::Mat4` via
  `matTranslation()` (reads m[12],m[13],m[14]) and converts rotation via
  `matRotationToQuat()` (reads column basis vectors). These helpers are
  local to `foot_ik.cpp` and documented with the column-major convention.

### Allocator Compliance (engine-wide constraints)

- All runtime containers use `std::pmr::vector` / `std::pmr::string`.
- `SkinnedMeshPlayer::footIkScratch_` is sized once in the constructor and
  reused per frame — no per-frame allocation on the hot path.
- No raw `new`/`delete` in any new code.
