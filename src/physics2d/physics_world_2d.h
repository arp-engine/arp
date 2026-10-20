#pragma once
#include "core/generation_pool.h"
#include "core/math.h"
#include "scene/transform.h"

namespace arp::physics2d {
using BodyHandle = core::PoolHandle;
using ColliderHandle = core::PoolHandle;
using JointHandle = core::PoolHandle;
AENUM()
enum class BodyType {
    Static,
    Kinematic,
    Dynamic,
};
AENUM()
enum class ShapeType {
    Circle,
    Capsule,
    Box,
    Convex,
    StaticChain,
};
AENUM()
enum class JointType {
    Distance,
    Revolute,
    Prismatic,
};

struct WorldConfig {
    core::Vec2 gravity;
    std::uint32_t maxBodies = 0, maxColliders = 0, maxJoints = 0, maxNativeShapes = 0;
    std::uint32_t maxEvents = 0, maxQueryResults = 0, maxChainPoints = 0, substeps = 4;
    float fixedSeconds = 1.f / 60.f;
};
ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct BodyDesc {
    APROP()
    BodyType type = BodyType::Static;
    APROP()
    core::Vec2 position{};
    APROP()
    core::Vec2 velocity{};
    APROP()
    float angle = 0;
    APROP()
    float angularVelocity = 0;
    APROP()
    float gravityScale = 1;
    APROP()
    float linearDamping = 0;
    APROP()
    float angularDamping = 0;
    APROP()
    bool bullet = false;
    APROP()
    bool fixedRotation = false;
    std::uint64_t objectId = 0;
};
ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct CollisionFilter {
    APROP()
    std::uint64_t category = 1;
    APROP()
    std::uint64_t mask = UINT64_MAX;
    APROP()
    std::int32_t group = 0;
};
ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct ShapeDesc {
    APROP()
    ShapeType type = ShapeType::Circle;
    APROP()
    core::Vec2 center{};
    APROP()
    core::Vec2 halfSize{};
    APROP()
    core::Vec2 pointA{};
    APROP()
    core::Vec2 pointB{};
    APROP()
    float radius = 0;
    APROP()
    float density = 1;
    APROP()
    float friction = .6f;
    APROP()
    float restitution = 0;
    core::pmr::Vector<core::Vec2> points{&core::defaultCpuMemoryResource()};
    AFUNC()

    bool set_points(const reflection::hlist& values)
    {
        core::pmr::Vector<core::Vec2> prepared;
        prepared.reserve(values.size());
        for (const auto& value : values) {
            if (!value.is<core::Vec2>())
                return false;
            auto v = value.cast<core::Vec2>();
            if (!std::isfinite(v.x) || !std::isfinite(v.y))
                return false;
            prepared.push_back(v);
        }
        points = std::move(prepared);
        return true;
    }
    APROP()
    CollisionFilter filter;
    APROP()
    bool sensor = false;
    APROP()
    bool loop = false;
};
ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct JointDesc {
    APROP()
    JointType type = JointType::Distance;
    BodyHandle bodyA, bodyB;
    APROP()
    core::Vec2 localAnchorA{};
    APROP()
    core::Vec2 localAnchorB{};
    APROP()
    core::Vec2 localAxisA{1, 0};
    APROP()
    float length = 1;
    APROP()
    float referenceAngle = 0;
    APROP()
    float lowerLimit = 0;
    APROP()
    float upperLimit = 0;
    APROP()
    float motorSpeed = 0;
    APROP()
    float maxMotorForce = 0;
    APROP()
    bool enableLimit = false;
    APROP()
    bool enableMotor = false;
    APROP()
    bool collideConnected = false;
};
ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct BodyState {
    APROP()
    core::Vec2 position{};
    APROP()
    core::Vec2 velocity{};
    APROP()
    float angle = 0;
    APROP()
    float angularVelocity = 0;
};
enum class EventType { ContactBegin, ContactEnd, SensorBegin, SensorEnd };

struct ContactEvent {
    EventType type;
    ColliderHandle a, b;
};

struct QueryHit {
    ColliderHandle collider;
    core::Vec2 point{}, normal{};
    float fraction = 0;
};

struct PhysicsTelemetry {
    std::uint32_t bodies = 0, colliders = 0, joints = 0;
    std::uint64_t ticks = 0, thirdPartyBytes = 0, thirdPartyPeakBytes = 0;
};
// Single owner thread. No backend types escape. Native shape IDs are private.
// Config fixes all engine container capacities. Box2D's own growth is measured
// and uses the engine allocator; hot-path prewarming is validated separately.
ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct PhysicsQueryHit {
    APROP()
    std::uint64_t objectId = 0;
    APROP()
    core::Vec2 point{};
    APROP()
    core::Vec2 normal{};
    APROP()
    float fraction = 0;
};
ACLASS(Export = "physics_world", Ownership = Engine, Lifecycle = Explicit)

class PhysicsWorld2D final {
public:
    explicit PhysicsWorld2D(const WorldConfig&);
    ~PhysicsWorld2D();
    PhysicsWorld2D(const PhysicsWorld2D&) = delete;
    PhysicsWorld2D& operator=(const PhysicsWorld2D&) = delete;
    bool valid() const;

    const reflection::object& nativeReflectionObject() const { return reflection_; }
    AFUNC()
    reflection::hlist overlap_hits(
        const ShapeDesc& shape, core::Vec2 position, float angle, CollisionFilter filter);
    AFUNC()
    reflection::hlist shape_cast_hits(const ShapeDesc& shape,
        core::Vec2 position,
        float angle,
        core::Vec2 translation,
        CollisionFilter filter);
    AFUNC()
    reflection::hlist raycast_hits(
        core::Vec2 position, core::Vec2 translation, CollisionFilter filter);
    BodyHandle createBody(const BodyDesc&);
    ColliderHandle createCollider(BodyHandle, const ShapeDesc&);
    // Creates the replacement within the configured collider/native-shape
    // budgets before releasing the current collider. Failure preserves handle.
    ColliderHandle replaceCollider(ColliderHandle, const ShapeDesc&);
    JointHandle createJoint(const JointDesc&);
    bool destroyBody(BodyHandle);
    bool destroyCollider(ColliderHandle);
    bool destroyJoint(JointHandle);
    // Pose and object identity remain owned by the scene.
    bool updateBodyProperties(BodyHandle, const BodyDesc&);
    bool state(BodyHandle, BodyState&) const;
    bool objectId(ColliderHandle, std::uint64_t&) const;
    bool setVelocity(BodyHandle, core::Vec2, float angularVelocity = 0);
    bool setPose(BodyHandle, core::Vec2, float angle);
    bool applyImpulse(BodyHandle, core::Vec2 impulse, core::Vec2 worldPoint);
    bool setFilter(ColliderHandle, CollisionFilter);
    bool step();
    bool overlap(const ShapeDesc&, core::Vec2 position, float angle, CollisionFilter);
    bool raycast(core::Vec2 origin, core::Vec2 translation, CollisionFilter);
    bool shapeCast(const ShapeDesc&,
        core::Vec2 position,
        float angle,
        core::Vec2 translation,
        CollisionFilter);
    // One hit per public collider (chain segments coalesce), nearest cast
    // fraction retained. Results are ordered by fraction then collider index.
    const core::pmr::Vector<QueryHit>& queryResults() const;
    const core::pmr::Vector<ContactEvent>& events() const;
    PhysicsTelemetry telemetry() const;
    const char* diagnostic() const;

private:
    reflection::hlist scriptHits() const;
    reflection::object reflection_ = reflection::object::native_reference(this);
    struct Impl;
    core::UniquePtr<Impl> impl_;
};
} // namespace arp::physics2d
