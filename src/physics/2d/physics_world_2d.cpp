#include "physics/2d/physics_world_2d.h"

#include "core/math/polygon_validation.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <box2d/box2d.h>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <stdexcept>

namespace arp::physics2d {
namespace {
std::atomic<std::uint64_t> bytes{0}, peak{0};
std::atomic<std::uint64_t> allocations{0};

void* allocateBox(unsigned size, int alignment)
{
    auto result = core::defaultCpuAllocator().allocate(
        {size, std::size_t(alignment), core::CpuAllocationIntent::General, "Box2D 3.1.1"});
    // Box2D 3.1.1 has no recoverable allocation-failure protocol. Fail loudly
    // before it dereferences null; never substitute a partially valid world.
    if (!result.ptr) {
        std::fputs("ARP-PHYSICS2D-ALLOCATION-FAILURE\n", stderr);
        std::abort();
    }
    auto live = bytes.fetch_add(size) + size;
    allocations.fetch_add(1);
    auto high = peak.load();
    while (high < live && !peak.compare_exchange_weak(high, live)) {
    }
    return result.ptr;
}

void freeBox(void* memory)
{
    if (!memory)
        return;
    const auto info = core::inspectCpuAllocationCharge(memory);
    bytes.fetch_sub(info.sizeBytes);
    core::defaultCpuAllocator().deallocate(memory, {});
}

b2Vec2 native(core::Vec2 value)
{
    return {value.x, value.y};
}

core::Vec2 vector(b2Vec2 value)
{
    return {value.x, value.y};
}

bool finite(core::Vec2 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y);
}

b2Filter filter(CollisionFilter f)
{
    return {f.category, f.mask, f.group};
}

b2QueryFilter queryFilter(CollisionFilter f)
{
    return {f.category, f.mask};
}

bool nativeBodyType(BodyType type, b2BodyType& result)
{
    switch (type) {
    case BodyType::Static:
        result = b2_staticBody;
        return true;
    case BodyType::Kinematic:
        result = b2_kinematicBody;
        return true;
    case BodyType::Dynamic:
        result = b2_dynamicBody;
        return true;
    }
    return false;
}
} // namespace

ComponentUsage requireComponentUsage(ComponentUsage usage)
{
    if (usage != ComponentUsage::Simulation && usage != ComponentUsage::Authoring)
        throw std::invalid_argument("ARP-PHYSICS2D-COMPONENT-USAGE");
    return usage;
}

const char* validateBodyDesc(const BodyDesc& d) noexcept
{
    if (!finite(d.position) || !finite(d.velocity) || !std::isfinite(d.angle)
        || !std::isfinite(d.angularVelocity) || !std::isfinite(d.gravityScale)
        || !std::isfinite(d.linearDamping) || d.linearDamping < 0
        || !std::isfinite(d.angularDamping) || d.angularDamping < 0)
        return "ARP-PHYSICS2D-BODY-CONFIG";
    b2BodyType type{};
    return nativeBodyType(d.type, type) ? nullptr : "ARP-PHYSICS2D-BODY-TYPE";
}

const char* validateShapeDesc(const ShapeDesc& d, std::uint32_t maxChainPoints, bool query) noexcept
{
    const char* diagnostic = nullptr;
    const auto fail = [&](const char* value) {
        diagnostic = value;
        return false;
    };
    const auto check = [&]() -> bool {
        if (!finite(d.center) || !finite(d.halfSize) || !finite(d.pointA) || !finite(d.pointB)
            || !std::isfinite(d.radius) || d.radius < 0 || !std::isfinite(d.density)
            || d.density < 0 || !std::isfinite(d.friction) || d.friction < 0
            || !std::isfinite(d.restitution) || d.restitution < 0 || d.restitution > 1)
            return fail("ARP-PHYSICS2D-SHAPE-NONFINITE");
        switch (d.type) {
        case ShapeType::Circle:
            return d.radius > 0 || fail("ARP-PHYSICS2D-CIRCLE-RADIUS");
        case ShapeType::Capsule:
            return (d.radius > 0 && core::dot(d.pointA - d.pointB, d.pointA - d.pointB) > 0)
                || fail("ARP-PHYSICS2D-CAPSULE");
        case ShapeType::Box:
            return (d.halfSize.x > 0 && d.halfSize.y > 0) || fail("ARP-PHYSICS2D-BOX-EXTENT");
        case ShapeType::Convex:
            if (d.points.size() < 3 || d.points.size() > B2_MAX_POLYGON_VERTICES)
                return fail("ARP-PHYSICS2D-CONVEX-COUNT");
            return core::strictlyConvexCcw(d.points) || fail("ARP-PHYSICS2D-CONVEX-WINDING");
        case ShapeType::StaticChain:
            if (query || d.sensor || d.points.size() < 4 || d.points.size() > maxChainPoints)
                return fail("ARP-PHYSICS2D-CHAIN-CONTRACT");
            // 3.1.1 chain spacing requirement: > B2_LINEAR_SLOP (0.005 metres).
            return core::simplePolyline(d.points, d.loop, .005f)
                || fail("ARP-PHYSICS2D-CHAIN-GEOMETRY");
        }
        return fail("ARP-PHYSICS2D-SHAPE-TYPE");
    };
    if (!check())
        return diagnostic;
    if (d.type == ShapeType::Convex) {
        std::array<b2Vec2, B2_MAX_POLYGON_VERTICES> points{};
        for (std::size_t i = 0; i < d.points.size(); ++i)
            points[i] = native(d.points[i]);
        const auto hull = b2ComputeHull(points.data(), int(d.points.size()));
        if (hull.count != int(d.points.size()))
            return "ARP-PHYSICS2D-CONVEX-HULL-CHANGED";
    }
    return nullptr;
}

const char* validateJointDesc(const JointDesc& d) noexcept
{
    if (d.type != JointType::Distance && d.type != JointType::Revolute
        && d.type != JointType::Prismatic)
        return "ARP-PHYSICS2D-JOINT-TYPE";
    if (!finite(d.localAnchorA) || !finite(d.localAnchorB) || !finite(d.localAxisA)
        || !std::isfinite(d.length) || d.length <= 0 || !std::isfinite(d.referenceAngle)
        || !std::isfinite(d.lowerLimit) || !std::isfinite(d.upperLimit)
        || d.lowerLimit > d.upperLimit || !std::isfinite(d.motorSpeed)
        || !std::isfinite(d.maxMotorForce) || d.maxMotorForce < 0)
        return "ARP-PHYSICS2D-JOINT-CONFIG";
    if (d.type == JointType::Prismatic
        && std::abs(core::dot(d.localAxisA, d.localAxisA) - 1) > 1e-5f)
        return "ARP-PHYSICS2D-JOINT-AXIS";
    return nullptr;
}

struct PhysicsWorld2D::Impl {
    struct Body {
        b2BodyId native{};
        BodyType type;
        std::uint64_t objectId = 0;
    };

    struct Collider {
        b2ShapeId native{};
        b2ChainId chain{};
        BodyHandle body;
    };

    struct Joint {
        b2JointId native{};
        BodyHandle a, b;
    };

    struct ShapeSlot {
        std::uint64_t key = 0;
        ColliderHandle handle;
    };

    WorldConfig config;
    b2WorldId world{};
    core::GenerationPool<Body> bodies;
    core::GenerationPool<Collider> colliders;
    core::GenerationPool<Joint> joints;
    core::pmr::Vector<ShapeSlot> shapeSlots{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ShapeSlot> retiredShapes{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<b2Vec2> pointScratch{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<b2ShapeId> shapeScratch{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ContactEvent> events{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<QueryHit> hits{&core::defaultCpuMemoryResource()};
    const char* diagnostic = nullptr;
    bool terminal = false;
    std::uint64_t ticks = 0;

    Impl(const WorldConfig& d)
        : config(d), bodies(d.maxBodies), colliders(d.maxColliders), joints(d.maxJoints)
    {
        if (!d.maxBodies || !d.maxColliders || !d.maxJoints || !d.maxNativeShapes || !d.maxEvents
            || !d.maxQueryResults || d.maxChainPoints < 4 || !d.substeps || d.substeps > INT32_MAX
            || !finite(d.gravity) || !std::isfinite(d.fixedSeconds) || d.fixedSeconds <= 0) {
            fail("ARP-PHYSICS2D-CONFIG");
            return;
        }
        static std::once_flag allocator;
        std::call_once(allocator, [] {
            b2SetAllocator(allocateBox, freeBox);
        });
        shapeSlots.resize(std::size_t(d.maxNativeShapes) + 1);
        retiredShapes.reserve(d.maxNativeShapes);
        pointScratch.reserve(d.maxChainPoints);
        shapeScratch.resize(d.maxNativeShapes);
        events.reserve(d.maxEvents);
        hits.reserve(d.maxQueryResults);
        auto definition = b2DefaultWorldDef();
        definition.gravity = native(d.gravity);
        definition.enableContinuous = true;
        // Versioned Box2D 3.1.1 default solver tuning, single-thread execution.
        world = b2CreateWorld(&definition);
        if (!b2World_IsValid(world))
            fail("ARP-PHYSICS2D-WORLD-CREATE");
    }

    ~Impl()
    {
        if (b2World_IsValid(world))
            b2DestroyWorld(world);
    }

    bool fail(const char* id)
    {
        diagnostic = id;
        return false;
    }

    bool ready()
    {
        diagnostic = nullptr;
        return !terminal && b2World_IsValid(world);
    }

    ColliderHandle lookup(b2ShapeId id) const
    {
        if (id.index1 <= 0 || std::size_t(id.index1) >= shapeSlots.size())
            return {};
        const auto key = b2StoreShapeId(id);
        const auto& slot = shapeSlots[id.index1];
        if (slot.key == key)
            return slot.handle;
        for (const auto& old : retiredShapes)
            if (old.key == key)
                return old.handle;
        return {};
    }

    bool registerShape(b2ShapeId id, ColliderHandle h)
    {
        if (id.index1 <= 0 || std::size_t(id.index1) >= shapeSlots.size())
            return fail("ARP-PHYSICS2D-NATIVE-SHAPE-CAPACITY");
        auto& slot = shapeSlots[id.index1];
        if (slot.key && slot.key != b2StoreShapeId(id)) {
            if (retiredShapes.size() >= config.maxNativeShapes)
                return fail("ARP-PHYSICS2D-RETIRED-CAPACITY");
            retiredShapes.push_back(slot);
        }
        slot = {b2StoreShapeId(id), h};
        return true;
    }

    bool shapeValid(const ShapeDesc& d, bool query = false)
    {
        const auto* diagnostic = validateShapeDesc(d, config.maxChainPoints, query);
        return !diagnostic || fail(diagnostic);
    }

    b2ShapeProxy proxy(const ShapeDesc& d, core::Vec2 position, float angle)
    {
        std::array<b2Vec2, B2_MAX_POLYGON_VERTICES> points{};
        int count = 0;
        float radius = d.radius;
        if (d.type == ShapeType::Circle) {
            points[0] = native(d.center);
            count = 1;
        } else if (d.type == ShapeType::Capsule) {
            points[0] = native(d.pointA);
            points[1] = native(d.pointB);
            count = 2;
        } else if (d.type == ShapeType::Box) {
            points = {native(d.center + core::Vec2{-d.halfSize.x, -d.halfSize.y}),
                native(d.center + core::Vec2{d.halfSize.x, -d.halfSize.y}),
                native(d.center + d.halfSize),
                native(d.center + core::Vec2{-d.halfSize.x, d.halfSize.y})};
            count = 4;
            radius = 0;
        } else {
            count = int(d.points.size());
            for (int i = 0; i < count; ++i)
                points[i] = native(d.points[i]);
            radius = 0;
        }
        const b2Transform transform{native(position), b2MakeRot(angle)};
        for (int i = 0; i < count; ++i)
            points[i] = b2TransformPoint(transform, points[i]);
        return b2MakeProxy(points.data(), count, radius);
    }

    bool beginQuery()
    {
        hits.clear();
        return ready();
    }

    bool addHit(b2ShapeId shape, b2Vec2 point, b2Vec2 normal, float fraction)
    {
        auto h = lookup(shape);
        if (!h) {
            hits.clear();
            return fail("ARP-PHYSICS2D-QUERY-UNMAPPED");
        }
        for (auto& hit : hits)
            if (hit.collider == h) {
                if (fraction < hit.fraction)
                    hit = {h, vector(point), vector(normal), fraction};
                return true;
            }
        if (hits.size() >= config.maxQueryResults) {
            hits.clear();
            return fail("ARP-PHYSICS2D-QUERY-CAPACITY");
        }
        hits.push_back({h, vector(point), vector(normal), fraction});
        return true;
    }

    static bool overlapCallback(b2ShapeId id, void* context)
    {
        return static_cast<Impl*>(context)->addHit(id, {}, {}, 0);
    }

    static float castCallback(
        b2ShapeId id, b2Vec2 point, b2Vec2 normal, float fraction, void* context)
    {
        return static_cast<Impl*>(context)->addHit(id, point, normal, fraction) ? 1.f : 0.f;
    }

    bool finishQuery()
    {
        if (diagnostic)
            return false;
        std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) {
            return a.fraction != b.fraction ? a.fraction < b.fraction
                                            : a.collider.index < b.collider.index;
        });
        return true;
    }
};

PhysicsWorld2D::PhysicsWorld2D(const WorldConfig& d) : impl_(core::makeUnique<Impl>(d))
{}

PhysicsWorld2D::~PhysicsWorld2D()
{
    reflection_.invalidate_native_reference();
}

reflection::hlist PhysicsWorld2D::scriptHits() const
{
    reflection::hlist result;
    result.reserve(queryResults().size());
    for (const auto& hit : queryResults()) {
        PhysicsQueryHit value;
        value.point = hit.point;
        value.normal = hit.normal;
        value.fraction = hit.fraction;
        if (!objectId(hit.collider, value.objectId))
            throw std::runtime_error("ARP-PHYSICS2D-QUERY-STALE");
        result.emplace_back(value);
    }
    return result;
}

reflection::hlist PhysicsWorld2D::overlap_hits(
    const ShapeDesc& s, core::Vec2 p, float a, CollisionFilter f)
{
    if (!overlap(s, p, a, f))
        throw std::runtime_error(diagnostic());
    return scriptHits();
}

reflection::hlist PhysicsWorld2D::shape_cast_hits(
    const ShapeDesc& s, core::Vec2 p, float a, core::Vec2 t, CollisionFilter f)
{
    if (!shapeCast(s, p, a, t, f))
        throw std::runtime_error(diagnostic());
    return scriptHits();
}

reflection::hlist PhysicsWorld2D::raycast_hits(core::Vec2 p, core::Vec2 t, CollisionFilter f)
{
    if (!raycast(p, t, f))
        throw std::runtime_error(diagnostic());
    return scriptHits();
}

bool PhysicsWorld2D::valid() const
{
    return !impl_->terminal && b2World_IsValid(impl_->world);
}

const char* PhysicsWorld2D::diagnostic() const
{
    return impl_->diagnostic;
}

std::uint32_t PhysicsWorld2D::chainPointCapacity() const
{
    return impl_->config.maxChainPoints;
}

BodyHandle PhysicsWorld2D::createBody(const BodyDesc& d)
{
    auto& p = *impl_;
    if (!p.ready())
        return {};
    if (const auto* diagnostic = validateBodyDesc(d)) {
        p.fail(diagnostic);
        return {};
    }
    b2BodyType type{};
    if (!nativeBodyType(d.type, type))
        return {};
    auto h = p.bodies.emplace(Impl::Body{{}, d.type, d.objectId});
    if (!h) {
        p.fail("ARP-PHYSICS2D-BODY-CAPACITY");
        return {};
    }
    auto def = b2DefaultBodyDef();
    def.type = type;
    def.position = native(d.position);
    def.rotation = b2MakeRot(d.angle);
    def.linearVelocity = native(d.velocity);
    def.angularVelocity = d.angularVelocity;
    def.gravityScale = d.gravityScale;
    def.linearDamping = d.linearDamping;
    def.angularDamping = d.angularDamping;
    def.isBullet = d.bullet;
    def.fixedRotation = d.fixedRotation;
    auto id = b2CreateBody(p.world, &def);
    if (!b2Body_IsValid(id)) {
        p.bodies.erase(h);
        p.fail("ARP-PHYSICS2D-BODY-CREATE");
        return {};
    }
    p.bodies.get(h)->native = id;
    return h;
}

ColliderHandle PhysicsWorld2D::createCollider(BodyHandle body, const ShapeDesc& d)
{
    auto& p = *impl_;
    if (!p.ready())
        return {};
    auto* b = p.bodies.get(body);
    if (!b) {
        p.fail("ARP-PHYSICS2D-BODY-STALE");
        return {};
    }
    if (!p.shapeValid(d))
        return {};
    if (d.type == ShapeType::StaticChain && b->type != BodyType::Static) {
        p.fail("ARP-PHYSICS2D-CHAIN-STATIC-ONLY");
        return {};
    }
    auto h = p.colliders.emplace(Impl::Collider{{}, {}, body});
    if (!h) {
        p.fail("ARP-PHYSICS2D-COLLIDER-CAPACITY");
        return {};
    }
    auto& c = *p.colliders.get(h);
    auto def = b2DefaultShapeDef();
    def.density = d.density;
    def.material.friction = d.friction;
    def.material.restitution = d.restitution;
    def.filter = filter(d.filter);
    def.isSensor = d.sensor;
    def.enableSensorEvents = true;
    def.enableContactEvents = true;
    switch (d.type) {
    case ShapeType::Circle: {
        b2Circle shape{native(d.center), d.radius};
        c.native = b2CreateCircleShape(b->native, &def, &shape);
        break;
    }
    case ShapeType::Capsule: {
        b2Capsule shape{native(d.pointA), native(d.pointB), d.radius};
        c.native = b2CreateCapsuleShape(b->native, &def, &shape);
        break;
    }
    case ShapeType::Box: {
        auto shape = b2MakeOffsetBox(d.halfSize.x, d.halfSize.y, native(d.center), b2MakeRot(0));
        c.native = b2CreatePolygonShape(b->native, &def, &shape);
        break;
    }
    case ShapeType::Convex: {
        std::array<b2Vec2, B2_MAX_POLYGON_VERTICES> points;
        for (std::size_t i = 0; i < d.points.size(); ++i)
            points[i] = native(d.points[i]);
        auto hull = b2ComputeHull(points.data(), int(d.points.size()));
        if (hull.count != int(d.points.size())) {
            p.colliders.erase(h);
            p.fail("ARP-PHYSICS2D-CONVEX-HULL-CHANGED");
            return {};
        }
        auto shape = b2MakePolygon(&hull, 0);
        c.native = b2CreatePolygonShape(b->native, &def, &shape);
        break;
    }
    case ShapeType::StaticChain: {
        p.pointScratch.clear();
        for (auto point : d.points)
            p.pointScratch.push_back(native(point));
        auto chain = b2DefaultChainDef();
        chain.points = p.pointScratch.data();
        chain.count = int(p.pointScratch.size());
        chain.isLoop = d.loop;
        chain.filter = filter(d.filter);
        chain.enableSensorEvents = true;
        chain.materials = &def.material;
        chain.materialCount = 1;
        c.chain = b2CreateChain(b->native, &chain);
        break;
    }
    }
    bool success = false;
    if (b2Chain_IsValid(c.chain)) {
        const int count =
            b2Chain_GetSegments(c.chain, p.shapeScratch.data(), int(p.shapeScratch.size()));
        success = count > 0;
        for (int i = 0; i < count && success; ++i)
            success = p.registerShape(p.shapeScratch[i], h);
    } else if (b2Shape_IsValid(c.native))
        success = p.registerShape(c.native, h);
    if (!success) {
        destroyCollider(h);
        p.fail("ARP-PHYSICS2D-SHAPE-CREATE-OR-CAPACITY");
        return {};
    }
    return h;
}

ColliderHandle PhysicsWorld2D::replaceCollider(ColliderHandle handle, const ShapeDesc& desc)
{
    auto& p = *impl_;
    if (!p.ready())
        return {};
    auto* current = p.colliders.get(handle);
    if (!current) {
        p.fail("ARP-PHYSICS2D-COLLIDER-STALE");
        return {};
    }

    const auto body = current->body;
    const auto candidate = createCollider(body, desc);
    if (!candidate)
        return {};
    if (!destroyCollider(handle)) {
        destroyCollider(candidate);
        p.fail("ARP-PHYSICS2D-COLLIDER-REPLACE-COMMIT");
        return {};
    }
    return candidate;
}

JointHandle PhysicsWorld2D::createJoint(const JointDesc& d)
{
    auto& p = *impl_;
    if (!p.ready())
        return {};
    auto* a = p.bodies.get(d.bodyA);
    auto* b = p.bodies.get(d.bodyB);
    if (!a || !b || d.bodyA == d.bodyB) {
        p.fail("ARP-PHYSICS2D-JOINT-CONFIG");
        return {};
    }
    if (const auto* diagnostic = validateJointDesc(d)) {
        p.fail(diagnostic);
        return {};
    }
    auto h = p.joints.emplace(Impl::Joint{{}, d.bodyA, d.bodyB});
    if (!h) {
        p.fail("ARP-PHYSICS2D-JOINT-CAPACITY");
        return {};
    }
    b2JointId id{};
    switch (d.type) {
    case JointType::Distance: {
        auto def = b2DefaultDistanceJointDef();
        def.bodyIdA = a->native;
        def.bodyIdB = b->native;
        def.localAnchorA = native(d.localAnchorA);
        def.localAnchorB = native(d.localAnchorB);
        def.length = d.length;
        def.enableLimit = d.enableLimit;
        def.minLength = d.lowerLimit;
        def.maxLength = d.upperLimit;
        def.enableMotor = d.enableMotor;
        def.motorSpeed = d.motorSpeed;
        def.maxMotorForce = d.maxMotorForce;
        def.collideConnected = d.collideConnected;
        id = b2CreateDistanceJoint(p.world, &def);
        break;
    }
    case JointType::Revolute: {
        auto def = b2DefaultRevoluteJointDef();
        def.bodyIdA = a->native;
        def.bodyIdB = b->native;
        def.localAnchorA = native(d.localAnchorA);
        def.localAnchorB = native(d.localAnchorB);
        def.referenceAngle = d.referenceAngle;
        def.enableLimit = d.enableLimit;
        def.lowerAngle = d.lowerLimit;
        def.upperAngle = d.upperLimit;
        def.enableMotor = d.enableMotor;
        def.motorSpeed = d.motorSpeed;
        def.maxMotorTorque = d.maxMotorForce;
        def.collideConnected = d.collideConnected;
        id = b2CreateRevoluteJoint(p.world, &def);
        break;
    }
    case JointType::Prismatic: {
        auto def = b2DefaultPrismaticJointDef();
        def.bodyIdA = a->native;
        def.bodyIdB = b->native;
        def.localAnchorA = native(d.localAnchorA);
        def.localAnchorB = native(d.localAnchorB);
        def.localAxisA = native(d.localAxisA);
        def.referenceAngle = d.referenceAngle;
        def.enableLimit = d.enableLimit;
        def.lowerTranslation = d.lowerLimit;
        def.upperTranslation = d.upperLimit;
        def.enableMotor = d.enableMotor;
        def.motorSpeed = d.motorSpeed;
        def.maxMotorForce = d.maxMotorForce;
        def.collideConnected = d.collideConnected;
        id = b2CreatePrismaticJoint(p.world, &def);
        break;
    }
    }
    if (!b2Joint_IsValid(id)) {
        p.joints.erase(h);
        p.fail("ARP-PHYSICS2D-JOINT-CREATE");
        return {};
    }
    p.joints.get(h)->native = id;
    return h;
}

bool PhysicsWorld2D::destroyJoint(JointHandle handle)
{
    auto& p = *impl_;
    auto* j = p.joints.get(handle);
    if (!j)
        return p.fail("ARP-PHYSICS2D-JOINT-STALE");
    b2DestroyJoint(j->native);
    return p.joints.erase(handle);
}

bool PhysicsWorld2D::destroyCollider(ColliderHandle handle)
{
    auto& p = *impl_;
    auto* c = p.colliders.get(handle);
    if (!c)
        return p.fail("ARP-PHYSICS2D-COLLIDER-STALE");
    if (b2Chain_IsValid(c->chain))
        b2DestroyChain(c->chain);
    else if (b2Shape_IsValid(c->native))
        b2DestroyShape(c->native, true);
    return p.colliders.erase(handle);
}

bool PhysicsWorld2D::destroyBody(BodyHandle handle)
{
    auto& p = *impl_;
    auto* b = p.bodies.get(handle);
    if (!b)
        return p.fail("ARP-PHYSICS2D-BODY-STALE");
    p.joints.forEach([&](JointHandle h, Impl::Joint& j) {
        if (j.a == handle || j.b == handle)
            destroyJoint(h);
    });
    p.colliders.forEach([&](ColliderHandle h, Impl::Collider& c) {
        if (c.body == handle)
            destroyCollider(h);
    });
    b2DestroyBody(b->native);
    return p.bodies.erase(handle);
}

bool PhysicsWorld2D::state(BodyHandle handle, BodyState& result) const
{
    auto& p = *impl_;
    auto* b = p.bodies.get(handle);
    if (!b)
        return p.fail("ARP-PHYSICS2D-BODY-STALE");
    result = {vector(b2Body_GetPosition(b->native)),
        vector(b2Body_GetLinearVelocity(b->native)),
        b2Rot_GetAngle(b2Body_GetRotation(b->native)),
        b2Body_GetAngularVelocity(b->native)};
    return true;
}

bool PhysicsWorld2D::updateBodyProperties(BodyHandle handle, const BodyDesc& desc)
{
    auto& p = *impl_;
    if (!p.ready())
        return false;
    auto* body = p.bodies.get(handle);
    b2BodyType type{};
    auto properties = desc;
    properties.position = {};
    properties.angle = 0;
    if (!body || validateBodyDesc(properties) || !nativeBodyType(desc.type, type))
        return p.fail("ARP-PHYSICS2D-BODY-PROPERTIES");

    // All fallible validation happens before the first native mutation. These
    // Box2D setters are void and do not allocate, so a valid update is atomic
    // with respect to editor command execution on the world owner thread.
    b2Body_SetType(body->native, type);
    b2Body_SetLinearVelocity(body->native, native(desc.velocity));
    b2Body_SetAngularVelocity(body->native, desc.angularVelocity);
    b2Body_SetGravityScale(body->native, desc.gravityScale);
    b2Body_SetLinearDamping(body->native, desc.linearDamping);
    b2Body_SetAngularDamping(body->native, desc.angularDamping);
    b2Body_SetBullet(body->native, desc.bullet);
    b2Body_SetFixedRotation(body->native, desc.fixedRotation);
    body->type = desc.type;
    return true;
}

bool PhysicsWorld2D::objectId(ColliderHandle handle, std::uint64_t& result) const
{
    auto& p = *impl_;
    const auto* collider = p.colliders.get(handle);
    const auto* body = collider ? p.bodies.get(collider->body) : nullptr;
    if (!body)
        return p.fail("ARP-PHYSICS2D-COLLIDER-STALE");
    result = body->objectId;
    return true;
}

bool PhysicsWorld2D::setVelocity(BodyHandle handle, core::Vec2 velocity, float angular)
{
    auto& p = *impl_;
    auto* b = p.bodies.get(handle);
    if (!b || !finite(velocity) || !std::isfinite(angular))
        return p.fail("ARP-PHYSICS2D-VELOCITY");
    b2Body_SetLinearVelocity(b->native, native(velocity));
    b2Body_SetAngularVelocity(b->native, angular);
    return true;
}

bool PhysicsWorld2D::setPose(BodyHandle handle, core::Vec2 position, float angle)
{
    auto& p = *impl_;
    auto* b = p.bodies.get(handle);
    if (!b || !finite(position) || !std::isfinite(angle))
        return p.fail("ARP-PHYSICS2D-POSE");
    b2Body_SetTransform(b->native, native(position), b2MakeRot(angle));
    return true;
}

bool PhysicsWorld2D::applyImpulse(BodyHandle handle, core::Vec2 impulse, core::Vec2 position)
{
    auto& p = *impl_;
    auto* b = p.bodies.get(handle);
    if (!b || !finite(impulse) || !finite(position))
        return p.fail("ARP-PHYSICS2D-IMPULSE");
    b2Body_ApplyLinearImpulse(b->native, native(impulse), native(position), true);
    return true;
}

bool PhysicsWorld2D::setFilter(ColliderHandle handle, CollisionFilter f)
{
    auto& p = *impl_;
    auto* c = p.colliders.get(handle);
    if (!c)
        return p.fail("ARP-PHYSICS2D-COLLIDER-STALE");
    if (b2Chain_IsValid(c->chain))
        return p.fail("ARP-PHYSICS2D-CHAIN-FILTER-IMMUTABLE");
    b2Shape_SetFilter(c->native, filter(f));
    return true;
}

bool PhysicsWorld2D::step()
{
    auto& p = *impl_;
    if (!p.ready())
        return false;
    b2World_Step(p.world, p.config.fixedSeconds, int(p.config.substeps));
    ++p.ticks;
    p.events.clear();
    auto contacts = b2World_GetContactEvents(p.world);
    auto sensors = b2World_GetSensorEvents(p.world);
    auto add = [&](EventType type, b2ShapeId a, b2ShapeId b) {
        if (p.terminal)
            return;
        if (p.events.size() >= p.config.maxEvents) {
            p.terminal = true;
            p.events.clear();
            p.fail("ARP-PHYSICS2D-EVENT-CAPACITY-AFTER-STEP");
            return;
        }
        auto ha = p.lookup(a), hb = p.lookup(b);
        if (!ha || !hb) {
            p.terminal = true;
            p.events.clear();
            p.fail("ARP-PHYSICS2D-EVENT-UNMAPPED");
            return;
        }
        if ((type == EventType::ContactBegin || type == EventType::ContactEnd)
            && ha.index > hb.index)
            std::swap(ha, hb);
        p.events.push_back({type, ha, hb});
    };
    for (int i = 0; i < contacts.beginCount; ++i)
        add(EventType::ContactBegin,
            contacts.beginEvents[i].shapeIdA,
            contacts.beginEvents[i].shapeIdB);
    for (int i = 0; i < contacts.endCount; ++i)
        add(EventType::ContactEnd, contacts.endEvents[i].shapeIdA, contacts.endEvents[i].shapeIdB);
    for (int i = 0; i < sensors.beginCount; ++i)
        add(EventType::SensorBegin,
            sensors.beginEvents[i].sensorShapeId,
            sensors.beginEvents[i].visitorShapeId);
    for (int i = 0; i < sensors.endCount; ++i)
        add(EventType::SensorEnd,
            sensors.endEvents[i].sensorShapeId,
            sensors.endEvents[i].visitorShapeId);
    p.retiredShapes.clear();
    std::sort(p.events.begin(), p.events.end(), [](const auto& a, const auto& b) {
        if (a.type != b.type)
            return a.type < b.type;
        if (a.a.index != b.a.index)
            return a.a.index < b.a.index;
        return a.b.index < b.b.index;
    });
    return !p.terminal;
}

bool PhysicsWorld2D::overlap(
    const ShapeDesc& shape, core::Vec2 position, float angle, CollisionFilter f)
{
    auto& p = *impl_;
    if (!p.beginQuery() || !p.shapeValid(shape, true) || !finite(position) || !std::isfinite(angle))
        return p.fail("ARP-PHYSICS2D-QUERY-CONFIG");
    auto proxy = p.proxy(shape, position, angle);
    b2World_OverlapShape(p.world, &proxy, queryFilter(f), Impl::overlapCallback, &p);
    return p.finishQuery();
}

bool PhysicsWorld2D::raycast(core::Vec2 origin, core::Vec2 translation, CollisionFilter f)
{
    auto& p = *impl_;
    if (!p.beginQuery() || !finite(origin) || !finite(translation)
        || core::dot(translation, translation) <= 0)
        return p.fail("ARP-PHYSICS2D-QUERY-CONFIG");
    b2World_CastRay(
        p.world, native(origin), native(translation), queryFilter(f), Impl::castCallback, &p);
    return p.finishQuery();
}

bool PhysicsWorld2D::shapeCast(const ShapeDesc& shape,
    core::Vec2 position,
    float angle,
    core::Vec2 translation,
    CollisionFilter f)
{
    auto& p = *impl_;
    if (!p.beginQuery() || !p.shapeValid(shape, true) || !finite(position) || !std::isfinite(angle)
        || !finite(translation))
        return p.fail("ARP-PHYSICS2D-QUERY-CONFIG");
    auto proxy = p.proxy(shape, position, angle);
    b2World_CastShape(p.world, &proxy, native(translation), queryFilter(f), Impl::castCallback, &p);
    return p.finishQuery();
}

const core::pmr::Vector<QueryHit>& PhysicsWorld2D::queryResults() const
{
    return impl_->hits;
}

const core::pmr::Vector<ContactEvent>& PhysicsWorld2D::events() const
{
    return impl_->events;
}

PhysicsTelemetry PhysicsWorld2D::telemetry() const
{
    return {impl_->bodies.liveCount(),
        impl_->colliders.liveCount(),
        impl_->joints.liveCount(),
        impl_->ticks,
        bytes.load(),
        peak.load(),
        allocations.load()};
}
} // namespace arp::physics2d
