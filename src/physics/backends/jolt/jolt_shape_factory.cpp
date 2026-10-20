#include "physics/backends/jolt/jolt_shape_factory.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

namespace arp::physics::jolt_backend {

namespace {

ShapeLoweringResult fail(const char* msg)
{
    return ShapeLoweringResult{nullptr, msg};
}

} // namespace

ShapeLoweringResult lowerColliderShape(const ColliderShapeDesc& desc)
{
    switch (desc.kind) {
    case ColliderShapeKind::Box: {
        // Jolt BoxShape takes half extents; requires positive min extent.
        if (desc.halfExtents.x <= 0.0f || desc.halfExtents.y <= 0.0f
            || desc.halfExtents.z <= 0.0f) {
            return fail("Box half extents must be positive on all axes");
        }
        const JPH::Vec3 he(desc.halfExtents.x, desc.halfExtents.y, desc.halfExtents.z);
        auto* shape = new JPH::BoxShape(he);
        return ShapeLoweringResult{JPH::RefConst<JPH::Shape>(shape), nullptr};
    }
    case ColliderShapeKind::Sphere: {
        if (desc.radius <= 0.0f) {
            return fail("Sphere radius must be positive");
        }
        auto* shape = new JPH::SphereShape(desc.radius);
        return ShapeLoweringResult{JPH::RefConst<JPH::Shape>(shape), nullptr};
    }
    case ColliderShapeKind::Capsule: {
        // Jolt CapsuleShape(halfHeightOfCylinder, radius) - half height is
        // from center to the cylinder cap, not including the hemisphere.
        if (desc.height <= 0.0f || desc.radius <= 0.0f) {
            return fail("Capsule height and radius must be positive");
        }
        auto* shape = new JPH::CapsuleShape(desc.height, desc.radius);
        return ShapeLoweringResult{JPH::RefConst<JPH::Shape>(shape), nullptr};
    }
    case ColliderShapeKind::Cylinder: {
        if (desc.height <= 0.0f || desc.radius <= 0.0f) {
            return fail("Cylinder height and radius must be positive");
        }
        auto* shape = new JPH::CylinderShape(desc.height, desc.radius);
        return ShapeLoweringResult{JPH::RefConst<JPH::Shape>(shape), nullptr};
    }
    case ColliderShapeKind::ConvexHull: {
        if (desc.convexPoints.size() < 4) {
            return fail("ConvexHull requires at least 4 points");
        }
        // Copy engine Vec3 into a Jolt Vec3 array (ContiguousMemory API).
        JPH::Array<JPH::Vec3> points;
        points.reserve(desc.convexPoints.size());
        for (const auto& p : desc.convexPoints) {
            points.push_back(JPH::Vec3(p.x, p.y, p.z));
        }
        JPH::ConvexHullShapeSettings settings(points);
        const auto result = settings.Create();
        if (!result.IsValid()) {
            return fail("ConvexHullShapeSettings::Create failed");
        }
        return ShapeLoweringResult{result.Get(), nullptr};
    }
    case ColliderShapeKind::HeightField:
        return fail(
            "HeightField shape is built by PhysicsWorld::setTerrainHeightField, not by createBody");
    case ColliderShapeKind::Mesh:
        return fail("Mesh shape lowering not yet implemented (Prototype)");
    }
    return fail("Unknown ColliderShapeKind");
}

} // namespace arp::physics::jolt_backend
