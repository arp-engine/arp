# Core Math Foundation

Status: Contract Complete.

This document records the current ownership and convention contract for reusable
math primitives in `src/core`. It is intentionally narrower than renderer,
animation, asset importer, shader, or backend math policy.

## Ownership

`src/core/math.h` owns dependency-light primitive math:

- `Vec2`, `Vec3`, and `Vec4` storage and arithmetic.
- Scalar helpers such as dot, cross, length, scaling, and normalization.
- `Quat` storage, normalization, axis-angle construction, multiplication, and
  vector rotation.
- The existing `Mat4` type and row-major helpers used by core camera code.

`src/animation` continues to own skeletal pose semantics:

- `animation::Vec3`, `animation::Quat`, `animation::Mat4`, `LocalTransform`,
  `JointTransform`, interpolation policy, and bind-pose composition.
- Any future migration must first prove that the animation matrix convention and
  skeletal interpolation contract are identical to the core contract.

`src/assets` may use core vector primitives for pure mesh processing algorithms
when the numeric policy matches exactly. Import format semantics, diagnostics,
and asset metadata stay in `src/assets` or `src/resource`.

## Matrix Convention

The current `core::Mat4` contract is row-major indexing:

- Element index is `row * 4 + column`.
- Matrix multiplication computes `result[row,column] = lhs[row,k] * rhs[k,column]`.
- `transformPoint` treats points as column vectors and uses translation at
  indices `m[3]`, `m[7]`, and `m[11]`.
- `transformVector` ignores translation and uses the upper-left 3x3 basis.
- `Mat4::translation` writes `m[3]`, `m[7]`, and `m[11]`.
- `Mat4::scale` writes `m[0]`, `m[5]`, and `m[10]`.

The current projection helper is the existing engine camera contract:

- `lookAt` builds the row-major view matrix used by `core::Camera`.
- `perspective` keeps the existing zero-to-one depth style projection and
  y-flip sign used by current tests.
- Invalid or degenerate projection inputs are clamped by the existing helper;
  this phase documents that behavior but does not claim renderer or backend
  validation.

Animation currently uses a different matrix convention in
`src/animation/skeleton.cpp`: translation is stored at `m[12]`, `m[13]`,
`m[14]`, and multiplication is implemented with column-major style indexing.
That difference is intentional until an animation-specific contract proves a
safe migration.

## Status Boundaries

This phase is Contract Complete only. It does not claim Backend Validated,
Platform Validated, or Production Ready math behavior. Shader upload layout,
clip-space backend lowering, renderer pipeline state, and skeletal animation
runtime behavior remain owned and validated by their respective modules.
