#pragma once

// L2 Physics Domain Semantics (Contract Layer)
// ============================================
//
// This header defines the engine's own physics domain types. They are the
// single source of truth for what the physics layer *means*: collider
// shapes, body motion kinds, collision layers, rigid body descriptors and
// contact events. They MUST NOT depend on, include or expose any backend
// type (no Jolt, no Bullet, ...). Backends lower these descriptors into
// their native equivalents; the contract is one-way (AGENTS.md §1:
// contract-first, backend code only executes approved lowerings).
//
// Memory: dynamic containers use core::pmr aliases and owned objects go
// through core allocators (AGENTS.md CPU allocator rule). Vec3/Quat come
// from core::math so the physics layer never redefines math primitives.

#include "core/math/math.h"
#include "core/memory/cpu_allocator.h"
#include "reflection/reflection_macros.h"

#include <cstdint>

namespace arp::physics {

/// Kind of collider shape a body carries. Each kind is lowered by the
/// backend's shape factory into a native shape; unknown kinds must fail
/// with a deterministic diagnostic (no silent fallback).
AENUM()
enum class ColliderShapeKind : std::uint8_t {
    Box,         ///< Axis-aligned box, half extents in `halfExtents`.
    Sphere,      ///< Sphere, radius in `radius`.
    Capsule,     ///< Capsule along Y, half-height in `height`, radius in `radius`.
    Cylinder,    ///< Cylinder along Y, half-height in `height`, radius in `radius`.
    ConvexHull,  ///< Convex hull of `convexPoints`.
    HeightField, ///< Terrain height field (static only); built by PhysicsWorld.
    Mesh,        ///< Triangle mesh (static only); `convexPoints` holds verts.
};

/// How a body participates in simulation. Determines who owns the transform.
AENUM()
enum class BodyMotionType : std::uint8_t {
    Static,    ///< Immovable; never moves. Owner transform is read-only to physics.
    Kinematic, ///< Moved explicitly by the game; physics reads transform, never writes.
    Dynamic,   /// Fully simulated; physics owns the transform, game reads it back.
};

/// Collision layer. Drives the layer-pair filter (which layers collide).
/// Layers are a bounded, versioned contract: adding a layer requires
/// updating the collision matrix in physics_types.cpp and its contract test.
AENUM()
enum class CollisionLayer : std::uint16_t {
    Static = 0,    ///< Terrain, walls, immovable world geometry.
    Dynamic = 1,   ///< Props, debris, simulated rigid bodies.
    Trigger = 2,   ///< Sensor volumes; generate events but no physical response.
    Character = 3, ///< Player/NPC character controllers.
    Count = 4,
};

/// A collider shape descriptor. Fields are interpreted by `kind`; unused
/// fields are ignored. This is a union-style POD; the lowering function is
/// the only place that interprets the active variant.
ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct ColliderShapeDesc {
    APROP(Serialize, Editor)
    ColliderShapeKind kind = ColliderShapeKind::Box;
    /// Half extents for Box; ignored otherwise.
    APROP(Serialize, Editor)
    arp::core::Vec3 halfExtents{0.5f, 0.5f, 0.5f};
    /// Radius for Sphere / Capsule / Cylinder.
    APROP(Serialize, Editor)
    float radius = 0.5f;
    /// Half height (from center to cap) for Capsule / Cylinder.
    APROP(Serialize, Editor)
    float height = 1.0f;
    /// Convex hull / mesh vertices (object space). Owned via core allocator.
    APROP(Serialize)
    arp::core::pmr::Vector<arp::core::Vec3> convexPoints;
};

/// Descriptor for creating a rigid body. All native backend creation paths
/// consume exactly these fields; no implicit defaults are read from the
/// backend (AGENTS.md §4: no native defaults).
ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct RigidBodyDesc {
    APROP(Serialize, Editor)
    ColliderShapeDesc shape;
    APROP(Serialize, Editor)
    BodyMotionType motionType = BodyMotionType::Dynamic;
    APROP(Serialize, Editor)
    CollisionLayer layer = CollisionLayer::Dynamic;
    APROP(Serialize, Editor)
    float mass = 1.0f;
    APROP(Serialize, Editor)
    float friction = 0.2f;
    APROP(Serialize, Editor)
    float restitution = 0.0f;
    /// When true the body is a sensor: it reports events but does not
    /// physically push or be pushed. Only valid with Static/Kinematic.
    APROP(Serialize, Editor)
    bool isTrigger = false;
};

/// Opaque handle to a physics body. Stable for the body's lifetime; never
/// reused after destruction. `valid()` is the only legal liveness check.
struct PhysicsBodyHandle {
    std::uint64_t id = 0;

    bool valid() const { return id != 0; }

    friend bool operator==(PhysicsBodyHandle lhs, PhysicsBodyHandle rhs)
    {
        return lhs.id == rhs.id;
    }

    friend bool operator!=(PhysicsBodyHandle lhs, PhysicsBodyHandle rhs) { return !(lhs == rhs); }
};

/// Kind of contact event reported to a PhysicsEventListener.
enum class PhysicsEventKind : std::uint8_t {
    ContactBegin,   ///< First contact this pair.
    ContactPersist, ///< Ongoing contact.
    ContactEnd,     ///< Pair separated.
    TriggerEnter,   ///< Body entered a trigger volume.
    TriggerLeave,   ///< Body left a trigger volume.
};

/// A contact event delivered to a listener. `other` is the body the
/// listener-owning body collided with. Point/normal are in world space.
struct PhysicsContactEvent {
    PhysicsBodyHandle other;
    arp::core::Vec3 point{0.0f, 0.0f, 0.0f};
    arp::core::Vec3 normal{0.0f, 1.0f, 0.0f};
    float penetrationDepth = 0.0f;
    PhysicsEventKind kind = PhysicsEventKind::ContactBegin;
};

/// Explicit world-space coverage for a terrain collider. Physics must not
/// infer a symmetric square from asset data because TerrainAsset bounds may
/// be offset and rectangular.
struct TerrainPhysicsBounds {
    float minX = 0.0f;
    float maxX = 0.0f;
    float minZ = 0.0f;
    float maxZ = 0.0f;

    bool valid() const noexcept { return minX < maxX && minZ < maxZ; }
};

/// Shared filtering contract for scene queries. `layerMask` uses one bit per
/// CollisionLayer value. Sensors are excluded unless explicitly requested.
struct PhysicsQueryFilter {
    std::uint32_t layerMask = (1u << static_cast<std::uint32_t>(CollisionLayer::Count)) - 1u;
    PhysicsBodyHandle ignoredBody;
    bool includeTriggers = false;
};

struct RaycastQuery {
    arp::core::Vec3 origin{0.0f, 0.0f, 0.0f};
    arp::core::Vec3 direction{0.0f, -1.0f, 0.0f};
    float distance = 1.0f;
    PhysicsQueryFilter filter;
};

struct RaycastHit {
    PhysicsBodyHandle body;
    arp::core::Vec3 position{0.0f, 0.0f, 0.0f};
    arp::core::Vec3 normal{0.0f, 1.0f, 0.0f};
    float fraction = 1.0f;
};

struct CapsuleSweepQuery {
    arp::core::Vec3 center{0.0f, 0.0f, 0.0f};
    arp::core::Quat rotation{0.0f, 0.0f, 0.0f, 1.0f};
    float radius = 0.4f;
    float halfHeight = 0.9f;
    arp::core::Vec3 displacement{0.0f, 0.0f, 0.0f};
    PhysicsQueryFilter filter;
};

struct CapsuleSweepHit {
    PhysicsBodyHandle body;
    arp::core::Vec3 position{0.0f, 0.0f, 0.0f};
    arp::core::Vec3 normal{0.0f, 1.0f, 0.0f};
    float fraction = 1.0f;
};

struct CapsuleOverlapQuery {
    arp::core::Vec3 center{0.0f, 0.0f, 0.0f};
    arp::core::Quat rotation{0.0f, 0.0f, 0.0f, 1.0f};
    float radius = 0.4f;
    float halfHeight = 0.9f;
    PhysicsQueryFilter filter;
};

/// Terrain heightfield sampling contract. The backend samples this on a
/// configured grid to build a HeightFieldShape. A source must explicitly
/// reject unavailable terrain data; unavailable samples are a build failure,
/// never an implicit zero-height fallback.
struct TerrainPhysicsSource {
    /// Strict sample form for TerrainSurface and other streaming sources.
    /// Returns false when a location is not ready or is invalid.
    bool (*trySampleFn)(void* ctx, float x, float z, float& outHeight) = nullptr;

    /// Legacy sample form. This remains only for existing analytic callers;
    /// the backend rejects non-finite results. New asset-backed callers must
    /// use trySampleFn so unavailability is observable.
    float (*sampleFn)(void* ctx, float x, float z) = nullptr;
    void* ctx = nullptr;
};

/// Query whether two collision layers interact. Centralised so the matrix
/// is a single versioned contract (not scattered backend branches).
/// Returns true iff bodies on these layers should collide physically.
bool layersCollide(CollisionLayer a, CollisionLayer b);

} // namespace arp::physics
