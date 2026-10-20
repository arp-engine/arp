#pragma once

// Jolt backend: shape lowering factory (internal, backend-private).
//
// Lowers the L2 ColliderShapeDesc into a Jolt Shape. This is the single
// place that interprets the L2 shape descriptor's union-style fields and
// maps them to native Jolt shapes. The L2 contract is one-way: this file
// is the ONLY place that reads ColliderShapeKind and produces JPH::Shape.
//
// Memory: shapes are returned as JPH::Ref<Shape> (Jolt's ref-counted
// ownership). The caller (JoltPhysicsWorld) stores them for the body's
// lifetime. No engine allocator is involved for the shape itself because
// Jolt owns its shape allocator (RegisterDefaultAllocator is used).

#include "physics/physics_types.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

namespace arp::physics::jolt_backend {

/// Result of lowering a collider shape. On success `shape` holds a
/// ref-counted Jolt shape. On failure `shape` is null and `error` has a
/// diagnostic message (no silent fallback: unknown/invalid shapes fail).
struct ShapeLoweringResult {
    JPH::RefConst<JPH::Shape> shape;
    const char* error = nullptr;
};

/// Lower an L2 collider descriptor into a Jolt shape.
/// `shape` must outlive any body created from it.
ShapeLoweringResult lowerColliderShape(const ColliderShapeDesc& desc);

} // namespace arp::physics::jolt_backend
