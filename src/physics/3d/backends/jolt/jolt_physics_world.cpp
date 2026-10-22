#include "physics/3d/backends/jolt/jolt_physics_world.h"

#include "physics/3d/backends/jolt/jolt_shape_factory.h"

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/IssueReporting.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/RegisterTypes.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

namespace arp::physics::jolt_backend {

namespace {

// One-time Jolt global registration. Jolt requires RegisterDefaultAllocator
// + RegisterTypes + a Factory before any physics object is created. We do
// this exactly once per process via a flag; subsequent calls are no-ops.
std::once_flag gJoltRegisterOnce;
bool gJoltRegistered = false;

// Non-fatal Trace callback: surfaces Jolt diagnostic messages on stderr.
// Must be a plain function (GCC cannot convert a variadic lambda to a
// function pointer).
void joltTraceImpl(const char* inFMT, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, inFMT);
    std::vsnprintf(buffer, sizeof(buffer), inFMT, args);
    va_end(args);
    std::fprintf(stderr, "[Jolt] %s\n", buffer);
}

// AssertFailed callback: prints the assertion message and message box text
// to stderr, then returns false (do NOT trigger a debug break). This keeps
// the process alive so the test harness can report the failure cleanly.
// In production (non-debug) builds JPH_ENABLE_ASSERTS is undefined and this
// callback is never installed.
#ifdef JPH_ENABLE_ASSERTS
bool joltAssertFailedImpl(
    const char* inExpression, const char* inMessage, const char* inFile, std::uint32_t inLine)
{
    std::fprintf(stderr, "[Jolt ASSERT] %s(%u): %s", inFile, inLine, inExpression);
    if (inMessage != nullptr && inMessage[0] != '\0') {
        std::fprintf(stderr, " (%s)", inMessage);
    }
    std::fprintf(stderr, "\n");
    return false; // do not break
}
#endif

void ensureJoltRegistered()
{
    std::call_once(gJoltRegisterOnce, []() {
        JPH::RegisterDefaultAllocator();
        if (JPH::Trace == nullptr) {
            JPH::Trace = &joltTraceImpl;
        }
#ifdef JPH_ENABLE_ASSERTS
        if (JPH::AssertFailed == nullptr) {
            JPH::AssertFailed = &joltAssertFailedImpl;
        }
#endif
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
        gJoltRegistered = true;
    });
}

JPH::EMotionType toJoltMotionType(BodyMotionType t)
{
    switch (t) {
    case BodyMotionType::Static:
        return JPH::EMotionType::Static;
    case BodyMotionType::Kinematic:
        return JPH::EMotionType::Kinematic;
    case BodyMotionType::Dynamic:
        return JPH::EMotionType::Dynamic;
    }
    return JPH::EMotionType::Static;
}

JPH::ObjectLayer toJoltLayer(CollisionLayer l)
{
    return static_cast<JPH::ObjectLayer>(l);
}

JPH::Vec3 toJoltVec3(const arp::core::Vec3& v)
{
    return JPH::Vec3(v.x, v.y, v.z);
}

JPH::Quat toJoltQuat(const arp::core::Quat& q)
{
    return JPH::Quat(q.x, q.y, q.z, q.w);
}

arp::core::Vec3 fromJoltVec3(const JPH::Vec3& v)
{
    return arp::core::Vec3{v.GetX(), v.GetY(), v.GetZ()};
}

arp::core::Quat fromJoltQuat(const JPH::Quat& q)
{
    return arp::core::Quat{q.GetX(), q.GetY(), q.GetZ(), q.GetW()};
}

class QueryObjectLayerFilter final : public JPH::ObjectLayerFilter {
public:
    explicit QueryObjectLayerFilter(const PhysicsQueryFilter& filter) : filter_(filter) {}

    bool ShouldCollide(const JPH::ObjectLayer layer) const override
    {
        const auto index = static_cast<std::uint32_t>(layer);
        if (index >= static_cast<std::uint32_t>(CollisionLayer::Count)) {
            return false;
        }
        if (!filter_.includeTriggers && layer == toJoltLayer(CollisionLayer::Trigger)) {
            return false;
        }
        return (filter_.layerMask & (1u << index)) != 0u;
    }

private:
    const PhysicsQueryFilter& filter_;
};

class QueryBodyFilter final : public JPH::BodyFilter {
public:
    QueryBodyFilter(const JPH::BodyID ignored, const bool includeTriggers)
        : ignored_(ignored), includeTriggers_(includeTriggers)
    {}

    bool ShouldCollide(const JPH::BodyID& bodyId) const override
    {
        return ignored_.IsInvalid() || bodyId != ignored_;
    }

    bool ShouldCollideLocked(const JPH::Body& body) const override
    {
        return (ignored_.IsInvalid() || body.GetID() != ignored_)
            && (includeTriggers_ || !body.IsSensor());
    }

private:
    JPH::BodyID ignored_;
    bool includeTriggers_ = false;
};

bool validQueryFilter(const PhysicsQueryFilter& filter)
{
    constexpr std::uint32_t validMask =
        (1u << static_cast<std::uint32_t>(CollisionLayer::Count)) - 1u;
    return filter.layerMask != 0u && (filter.layerMask & ~validMask) == 0u;
}

} // namespace

JoltPhysicsWorld::JoltPhysicsWorld() = default;

JoltPhysicsWorld::~JoltPhysicsWorld()
{
    // Destroy all bodies before tearing down the physics system. Each body
    // must be removed from the system before DestroyBody. The terrain body
    // (if any) is stored in bodies_ too, so the single loop below handles it;
    // do NOT destroy terrainHandle_ separately (would double-destroy).
    if (bodyInterface_) {
        for (auto& [id, entry] : bodies_) {
            if (bodyInterface_->IsAdded(entry.joltId)) {
                bodyInterface_->RemoveBody(entry.joltId);
            }
            bodyInterface_->DestroyBody(entry.joltId);
        }
    }
    bodies_.clear();

    // Tear down in reverse construction order: release the PhysicsSystem
    // (which references contactBridge_, layer filters) BEFORE those objects
    // are destroyed.
    bodyInterface_ = nullptr;
    physicsSystem_.reset();
    contactBridge_.reset();
    objPairFilter_.reset();
    objVsBpFilter_.reset();
    layerInterface_.reset();
    jobSystem_.reset();
    tempAllocator_.reset();
}

bool JoltPhysicsWorld::init(const PhysicsWorldConfig& config)
{
    ensureJoltRegistered();

    config_ = config;

    // Validate config: bounded, positive budgets (AGENTS.md §6).
    if (config_.maxBodies == 0 || config_.maxBodyPairs == 0 || config_.maxContactConstraints == 0
        || config_.collisionSteps == 0) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::InvalidArgument;
        diagnostic_.message = "PhysicsWorldConfig budgets must be non-zero";
        return false;
    }

    // Temp allocator: 16 MiB pre-allocated (bounded, no per-step alloc).
    tempAllocator_ = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);

    // Browsers are single-threaded in the current Web target. Keep the same
    // Jolt job ordering and PhysicsSystem contract without constructing a
    // native thread; desktop/server retain the worker pool for throughput.
#if defined(__EMSCRIPTEN__)
    jobSystem_ = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
#else
    const int numThreads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    jobSystem_ = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, numThreads);
#endif

    layerInterface_ = std::make_unique<JoltLayerInterface>();
    objVsBpFilter_ = std::make_unique<JoltObjectVsBroadPhaseLayerFilter>();
    objPairFilter_ = std::make_unique<JoltObjectLayerPairFilter>();

    physicsSystem_ = std::make_unique<JPH::PhysicsSystem>();
    physicsSystem_->Init(config_.maxBodies,
        config_.numBodyMutexes,
        config_.maxBodyPairs,
        config_.maxContactConstraints,
        *layerInterface_,
        *objVsBpFilter_,
        *objPairFilter_);

    bodyInterface_ = &physicsSystem_->GetBodyInterface();

    // Contact bridge.
    contactBridge_ = std::make_unique<JoltContactBridge>(bodyIdMap_);
    physicsSystem_->SetContactListener(contactBridge_.get());

    // Apply gravity.
    physicsSystem_->SetGravity(JPH::Vec3(config_.gravity.x, config_.gravity.y, config_.gravity.z));

    // Optimize broadphase after bodies are added (called once before first
    // step; Jolt tolerates an empty broadphase optimization).
    physicsSystem_->OptimizeBroadPhase();

    initialized_ = true;
    diagnostic_.code = PhysicsWorldDiagnostic::Code::None;
    diagnostic_.message.clear();
    return true;
}

JPH::BodyID JoltPhysicsWorld::resolveJoltBodyId(PhysicsBodyHandle handle) const
{
    if (!handle.valid()) {
        return JPH::BodyID();
    }
    const auto it = bodies_.find(handle.id);
    if (it == bodies_.end()) {
        return JPH::BodyID();
    }
    return it->second.joltId;
}

PhysicsBodyHandle JoltPhysicsWorld::createBody(
    const RigidBodyDesc& desc, const arp::core::Vec3& position, const arp::core::Quat& rotation)
{
    if (!initialized_) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::InitFailed;
        diagnostic_.message = "createBody called before successful init()";
        return PhysicsBodyHandle{};
    }

    // Validate desc: dynamic body must have positive mass.
    if (desc.motionType == BodyMotionType::Dynamic && desc.mass <= 0.0f) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::InvalidArgument;
        diagnostic_.message = "Dynamic body mass must be positive";
        return PhysicsBodyHandle{};
    }

    // Lower the collider shape.
    auto shapeResult = lowerColliderShape(desc.shape);
    if (!shapeResult.shape) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::ShapeLoweringFailed;
        diagnostic_.message = shapeResult.error ? shapeResult.error : "shape lowering failed";
        return PhysicsBodyHandle{};
    }

    const auto motionType = toJoltMotionType(desc.motionType);
    const auto layer = toJoltLayer(desc.layer);

    JPH::BodyCreationSettings settings(shapeResult.shape,
        JPH::RVec3(position.x, position.y, position.z),
        toJoltQuat(rotation),
        motionType,
        layer);
    settings.mFriction = desc.friction;
    settings.mRestitution = desc.restitution;
    settings.mIsSensor = desc.isTrigger;
    if (motionType == JPH::EMotionType::Dynamic) {
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = desc.mass;
    }

    const JPH::BodyID bodyId =
        bodyInterface_->CreateAndAddBody(settings, JPH::EActivation::Activate);
    if (bodyId.IsInvalid()) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::BodyCreationFailed;
        diagnostic_.message = "CreateAndAddBody returned invalid BodyID";
        return PhysicsBodyHandle{};
    }

    const PhysicsBodyHandle handle{nextHandleId_++};
    bodies_[handle.id] = BodyEntry{bodyId, desc.shape};
    bodyIdMap_.associate(bodyId, handle);
    diagnostic_.code = PhysicsWorldDiagnostic::Code::None;
    diagnostic_.message.clear();
    return handle;
}

void JoltPhysicsWorld::destroyBody(PhysicsBodyHandle handle)
{
    const auto joltId = resolveJoltBodyId(handle);
    if (joltId.IsInvalid()) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::StaleHandle;
        diagnostic_.message = "destroyBody: handle not live";
        return;
    }
    if (bodyInterface_->IsAdded(joltId)) {
        bodyInterface_->RemoveBody(joltId);
    }
    bodyInterface_->DestroyBody(joltId);
    bodyIdMap_.dissociate(joltId);
    contactBridge_->setListener(handle, nullptr);
    bodies_.erase(handle.id);
    if (terrainHandle_ == handle)
        terrainHandle_ = {};
    diagnostic_.code = PhysicsWorldDiagnostic::Code::None;
    diagnostic_.message.clear();
}

void JoltPhysicsWorld::setBodyTransform(
    PhysicsBodyHandle handle, const arp::core::Vec3& position, const arp::core::Quat& rotation)
{
    const auto joltId = resolveJoltBodyId(handle);
    if (joltId.IsInvalid()) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::StaleHandle;
        diagnostic_.message = "setBodyTransform: handle not live";
        return;
    }
    bodyInterface_->SetPositionAndRotation(joltId,
        JPH::RVec3(position.x, position.y, position.z),
        toJoltQuat(rotation),
        JPH::EActivation::Activate);
}

bool JoltPhysicsWorld::setBodyShape(const PhysicsBodyHandle handle, const ColliderShapeDesc& shape)
{
    const auto joltId = resolveJoltBodyId(handle);
    if (joltId.IsInvalid()) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::StaleHandle;
        diagnostic_.message = "setBodyShape: handle not live";
        return false;
    }
    auto lowered = lowerColliderShape(shape);
    if (!lowered.shape) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::ShapeLoweringFailed;
        diagnostic_.message = lowered.error ? lowered.error : "shape lowering failed";
        return false;
    }
    bodyInterface_->SetShape(joltId, lowered.shape, true, JPH::EActivation::Activate);
    bodies_.at(handle.id).shapeDesc = shape;
    diagnostic_.code = PhysicsWorldDiagnostic::Code::None;
    diagnostic_.message.clear();
    return true;
}

void JoltPhysicsWorld::getBodyTransform(
    PhysicsBodyHandle handle, arp::core::Vec3& outPosition, arp::core::Quat& outRotation) const
{
    const auto joltId = resolveJoltBodyId(handle);
    if (joltId.IsInvalid()) {
        return;
    }
    JPH::RVec3 pos;
    JPH::Quat rot;
    bodyInterface_->GetPositionAndRotation(joltId, pos, rot);
    outPosition = arp::core::Vec3{float(pos.GetX()), float(pos.GetY()), float(pos.GetZ())};
    outRotation = fromJoltQuat(rot);
}

void JoltPhysicsWorld::setBodyVelocity(PhysicsBodyHandle handle, const arp::core::Vec3& velocity)
{
    const auto joltId = resolveJoltBodyId(handle);
    if (joltId.IsInvalid()) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::StaleHandle;
        diagnostic_.message = "setBodyVelocity: handle not live";
        return;
    }
    bodyInterface_->SetLinearVelocity(joltId, toJoltVec3(velocity));
}

arp::core::Vec3 JoltPhysicsWorld::getBodyVelocity(PhysicsBodyHandle handle) const
{
    const auto joltId = resolveJoltBodyId(handle);
    if (joltId.IsInvalid()) {
        return arp::core::Vec3{};
    }
    return fromJoltVec3(bodyInterface_->GetLinearVelocity(joltId));
}

void JoltPhysicsWorld::addImpulse(PhysicsBodyHandle handle, const arp::core::Vec3& impulse)
{
    const auto joltId = resolveJoltBodyId(handle);
    if (joltId.IsInvalid()) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::StaleHandle;
        diagnostic_.message = "addImpulse: handle not live";
        return;
    }
    bodyInterface_->AddImpulse(joltId, toJoltVec3(impulse));
}

bool JoltPhysicsWorld::raycast(const RaycastQuery& query, RaycastHit& outHit)
{
    if (!initialized_) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::InitFailed;
        diagnostic_.message = "raycast called before successful init()";
        return false;
    }
    const float directionLengthSq = query.direction.x * query.direction.x
        + query.direction.y * query.direction.y + query.direction.z * query.direction.z;
    if (!validQueryFilter(query.filter) || !std::isfinite(query.distance) || query.distance <= 0.0f
        || directionLengthSq <= 1.0e-8f) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::InvalidArgument;
        diagnostic_.message =
            "raycast requires a positive distance, non-zero direction and valid layer mask";
        return false;
    }
    const float invLength = 1.0f / std::sqrt(directionLengthSq);
    const JPH::Vec3 displacement(query.direction.x * invLength * query.distance,
        query.direction.y * invLength * query.distance,
        query.direction.z * invLength * query.distance);
    const JPH::RRayCast ray(
        JPH::RVec3(query.origin.x, query.origin.y, query.origin.z), displacement);
    const QueryObjectLayerFilter objectFilter(query.filter);
    const QueryBodyFilter bodyFilter(
        resolveJoltBodyId(query.filter.ignoredBody), query.filter.includeTriggers);
    JPH::RayCastResult nativeHit;
    const bool hit =
        physicsSystem_->GetNarrowPhaseQuery().CastRay(ray, nativeHit, {}, objectFilter, bodyFilter);
    diagnostic_.code = PhysicsWorldDiagnostic::Code::None;
    diagnostic_.message.clear();
    if (!hit) {
        return false;
    }
    const auto point = ray.GetPointOnRay(nativeHit.mFraction);
    JPH::Vec3 normal = JPH::Vec3::sAxisY();
    JPH::BodyLockRead lock(physicsSystem_->GetBodyLockInterface(), nativeHit.mBodyID);
    if (lock.Succeeded()) {
        normal = lock.GetBody().GetWorldSpaceSurfaceNormal(nativeHit.mSubShapeID2, point);
    }
    outHit.body = bodyIdMap_.resolve(nativeHit.mBodyID);
    outHit.position = arp::core::Vec3{static_cast<float>(point.GetX()),
        static_cast<float>(point.GetY()),
        static_cast<float>(point.GetZ())};
    outHit.normal = fromJoltVec3(normal);
    outHit.fraction = nativeHit.mFraction;
    return true;
}

bool JoltPhysicsWorld::sweepCapsule(const CapsuleSweepQuery& query, CapsuleSweepHit& outHit)
{
    if (!initialized_) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::InitFailed;
        diagnostic_.message = "sweepCapsule called before successful init()";
        return false;
    }
    const float displacementLengthSq = query.displacement.x * query.displacement.x
        + query.displacement.y * query.displacement.y + query.displacement.z * query.displacement.z;
    if (!validQueryFilter(query.filter) || query.radius <= 0.0f || query.halfHeight <= 0.0f
        || displacementLengthSq <= 1.0e-8f) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::InvalidArgument;
        diagnostic_.message =
            "capsule sweep requires positive dimensions, displacement and valid layer mask";
        return false;
    }
    const JPH::CapsuleShape shape(query.halfHeight, query.radius);
    const auto transform = JPH::RMat44::sRotationTranslation(
        toJoltQuat(query.rotation), JPH::RVec3(query.center.x, query.center.y, query.center.z));
    const JPH::RShapeCast cast(
        &shape, JPH::Vec3::sReplicate(1.0f), transform, toJoltVec3(query.displacement));
    JPH::ShapeCastSettings settings;
    settings.mReturnDeepestPoint = true;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    const QueryObjectLayerFilter objectFilter(query.filter);
    const QueryBodyFilter bodyFilter(
        resolveJoltBodyId(query.filter.ignoredBody), query.filter.includeTriggers);
    physicsSystem_->GetNarrowPhaseQuery().CastShape(
        cast, settings, JPH::RVec3::sZero(), collector, {}, objectFilter, bodyFilter);
    diagnostic_.code = PhysicsWorldDiagnostic::Code::None;
    diagnostic_.message.clear();
    if (!collector.HadHit()) {
        return false;
    }
    const auto axis = collector.mHit.mPenetrationAxis;
    const float axisLengthSq =
        axis.GetX() * axis.GetX() + axis.GetY() * axis.GetY() + axis.GetZ() * axis.GetZ();
    const float invAxisLength = axisLengthSq > 1.0e-8f ? 1.0f / std::sqrt(axisLengthSq) : 0.0f;
    const JPH::Vec3 normal = invAxisLength > 0.0f ? JPH::Vec3(-axis.GetX() * invAxisLength,
                                                        -axis.GetY() * invAxisLength,
                                                        -axis.GetZ() * invAxisLength)
                                                  : JPH::Vec3::sAxisY();
    outHit.body = bodyIdMap_.resolve(collector.mHit.mBodyID2);
    outHit.position = fromJoltVec3(collector.mHit.mContactPointOn2);
    outHit.normal = fromJoltVec3(normal);
    outHit.fraction = collector.mHit.mFraction;
    return true;
}

bool JoltPhysicsWorld::overlapsCapsule(const CapsuleOverlapQuery& query)
{
    if (!initialized_) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::InitFailed;
        diagnostic_.message = "overlapsCapsule called before successful init()";
        return false;
    }
    if (!validQueryFilter(query.filter) || query.radius <= 0.0f || query.halfHeight <= 0.0f) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::InvalidArgument;
        diagnostic_.message = "capsule overlap requires positive dimensions and valid layer mask";
        return false;
    }
    const JPH::CapsuleShape shape(query.halfHeight, query.radius);
    const auto transform = JPH::RMat44::sRotationTranslation(
        toJoltQuat(query.rotation), JPH::RVec3(query.center.x, query.center.y, query.center.z));
    JPH::CollideShapeSettings settings;
    JPH::AnyHitCollisionCollector<JPH::CollideShapeCollector> collector;
    const QueryObjectLayerFilter objectFilter(query.filter);
    const QueryBodyFilter bodyFilter(
        resolveJoltBodyId(query.filter.ignoredBody), query.filter.includeTriggers);
    physicsSystem_->GetNarrowPhaseQuery().CollideShape(&shape,
        JPH::Vec3::sReplicate(1.0f),
        transform,
        settings,
        JPH::RVec3::sZero(),
        collector,
        {},
        objectFilter,
        bodyFilter);
    diagnostic_.code = PhysicsWorldDiagnostic::Code::None;
    diagnostic_.message.clear();
    return collector.HadHit();
}

bool JoltPhysicsWorld::step(float deltaSeconds)
{
    if (!initialized_) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::InitFailed;
        diagnostic_.message = "step called before successful init()";
        return false;
    }
    const float dt = std::min(deltaSeconds, PhysicsWorld::maximumStepDeltaSeconds);
    const auto errors = physicsSystem_->Update(
        dt, static_cast<int>(config_.collisionSteps), tempAllocator_.get(), jobSystem_.get());

    // JPH::EPhysicsUpdateError is a bitmask; any non-zero value is a failure
    // that must surface as a diagnostic (no silent best-effort).
    if (errors != JPH::EPhysicsUpdateError::None) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::InitFailed;
        diagnostic_.message = "PhysicsSystem::Update reported errors";
        return false;
    }
    diagnostic_.code = PhysicsWorldDiagnostic::Code::None;
    diagnostic_.message.clear();
    return true;
}

void JoltPhysicsWorld::registerEventListener(
    PhysicsBodyHandle handle, PhysicsEventListener* listener)
{
    contactBridge_->setListener(handle, listener);
}

bool JoltPhysicsWorld::clearTerrainHeightField(PhysicsBodyHandle expected)
{
    if (!initialized_) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::InitFailed;
        diagnostic_.message = "PHYSICS-TERRAIN-CLEAR-NOT-INITIALIZED";
        return false;
    }
    if (!expected.valid() || expected != terrainHandle_
        || resolveJoltBodyId(expected).IsInvalid()) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::StaleHandle;
        diagnostic_.message = "PHYSICS-TERRAIN-CLEAR-LEASE-MISMATCH";
        return false;
    }
    destroyBody(expected);
    return diagnostic_.code == PhysicsWorldDiagnostic::Code::None;
}

bool JoltPhysicsWorld::setTerrainHeightField(
    const TerrainPhysicsSource& source, const TerrainPhysicsBounds& bounds)
{
    if (!initialized_) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::InitFailed;
        diagnostic_.message = "setTerrainHeightField called before init()";
        return false;
    }
    if ((source.trySampleFn == nullptr && source.sampleFn == nullptr) || !bounds.valid()
        || !std::isfinite(bounds.minX) || !std::isfinite(bounds.maxX) || !std::isfinite(bounds.minZ)
        || !std::isfinite(bounds.maxZ) || config_.terrainGridResolution < 2) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::InvalidArgument;
        diagnostic_.message = "invalid terrain source or explicit terrain bounds";
        return false;
    }

    const std::uint32_t N = config_.terrainGridResolution;
    const auto count = std::uint64_t(N) * N;
    if (count > config_.maxTerrainSampleBytes / sizeof(float)) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::TerrainBuildFailed;
        diagnostic_.message = "PHYSICS-TERRAIN-SAMPLE-BUDGET";
        return false;
    }
    const float stepX = (bounds.maxX - bounds.minX) / static_cast<float>(N - 1);
    const float stepZ = (bounds.maxZ - bounds.minZ) / static_cast<float>(N - 1);
    // TerrainPhysicsExactLattice/1: preserve source spacing; only exterior
    // native storage is padded with explicit no-collision samples.
    constexpr std::uint32_t nativeBlockSize = 2u;
    const auto nativeSide = std::max<std::uint64_t>(2u * nativeBlockSize,
        ((std::uint64_t(N) + nativeBlockSize - 1u) / nativeBlockSize) * nativeBlockSize);
    JPH::HeightFieldShapeSettings hfSettings;
    hfSettings.mBlockSize = nativeBlockSize;
    if (nativeSide > std::numeric_limits<std::uint32_t>::max()
        || nativeSide * nativeSide > std::numeric_limits<std::size_t>::max() / sizeof(float)
        || !std::isfinite(stepX) || !std::isfinite(stepZ) || stepX <= 0 || stepZ <= 0) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::TerrainBuildFailed;
        diagnostic_.message = "PHYSICS-TERRAIN-GRID-UNSUPPORTED";
        return false;
    }
    if (!std::isfinite(bounds.minX + static_cast<float>(nativeSide - 1u) * stepX)
        || !std::isfinite(bounds.minZ + static_cast<float>(nativeSide - 1u) * stepZ)) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::TerrainBuildFailed;
        diagnostic_.message = "PHYSICS-TERRAIN-GRID-UNSUPPORTED";
        return false;
    }
    core::pmr::Vector<float> samples{&core::defaultCpuMemoryResource()};
    try {
        samples.resize(static_cast<std::size_t>(count));
    } catch (const std::bad_alloc&) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::TerrainBuildFailed;
        diagnostic_.message = "PHYSICS-TERRAIN-SAMPLE-ALLOCATION";
        return false;
    }

    // Sample the heightfield on an NxN grid covering the declared asset bounds.
    // Jolt HeightFieldShape samples are row-major: index = y * N + x.
    for (std::uint32_t iy = 0; iy < N; ++iy) {
        for (std::uint32_t ix = 0; ix < N; ++ix) {
            // Preserve the declared terrain coverage exactly. Computing the
            // final coordinate through float multiplication can exceed maxX/
            // maxZ by one ULP and turn a valid immutable asset endpoint into
            // an out-of-bounds query.
            const float x =
                ix + 1u == N ? bounds.maxX : bounds.minX + static_cast<float>(ix) * stepX;
            const float z =
                iy + 1u == N ? bounds.maxZ : bounds.minZ + static_cast<float>(iy) * stepZ;
            float height = 0.0f;
            const bool available = source.trySampleFn != nullptr
                ? source.trySampleFn(source.ctx, x, z, height)
                : (height = source.sampleFn(source.ctx, x, z), std::isfinite(height));
            if (!available || !std::isfinite(height)
                || height == JPH::HeightFieldShapeConstants::cNoCollisionValue) {
                diagnostic_.code = PhysicsWorldDiagnostic::Code::TerrainBuildFailed;
                char message[256];
                const auto chars = std::snprintf(message,
                    sizeof(message),
                    "terrain source returned unavailable or invalid height"
                    " (grid=%ux%u sample=(%u,%u) world=(%.9g,%.9g) available=%u finite=%u)",
                    N,
                    N,
                    ix,
                    iy,
                    x,
                    z,
                    available ? 1u : 0u,
                    std::isfinite(height) ? 1u : 0u);
                diagnostic_.message.assign(message,
                    chars > 0 ? std::min<std::size_t>(
                                    static_cast<std::size_t>(chars), sizeof(message) - 1u)
                              : 0u);
                return false;
            }
            samples[static_cast<std::size_t>(iy) * N + ix] = height;
        }
    }

    // offset places the grid origin at the declared world-space min corner.
    const JPH::Vec3 offset(bounds.minX, 0.0f, bounds.minZ);
    const JPH::Vec3 scale(stepX, 1.0f, stepZ);

    hfSettings.mOffset = offset;
    hfSettings.mScale = scale;
    hfSettings.mSampleCount = static_cast<std::uint32_t>(nativeSide);
    hfSettings.mHeightSamples.resize(static_cast<std::size_t>(nativeSide * nativeSide),
        JPH::HeightFieldShapeConstants::cNoCollisionValue);
    for (std::uint32_t row = 0u; row < N; ++row)
        std::copy_n(samples.data() + static_cast<std::size_t>(row) * N,
            N,
            hfSettings.mHeightSamples.data() + static_cast<std::size_t>(row) * nativeSide);
    const auto hfResult = hfSettings.Create();
    if (!hfResult.IsValid()) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::TerrainBuildFailed;
        diagnostic_.message = "HeightFieldShapeSettings::Create failed";
        return false;
    }

    JPH::BodyCreationSettings settings(hfResult.Get(),
        JPH::RVec3(0, 0, 0),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Static,
        toJoltLayer(CollisionLayer::Static));
    settings.mFriction = 1.0f;

    const auto bodyId = bodyInterface_->CreateAndAddBody(settings, JPH::EActivation::DontActivate);
    if (bodyId.IsInvalid()) {
        diagnostic_.code = PhysicsWorldDiagnostic::Code::TerrainBuildFailed;
        diagnostic_.message = "terrain CreateAndAddBody returned invalid BodyID";
        return false;
    }

    // Register candidate bookkeeping before retiring the prior native body.
    // No partially registered candidate may survive an allocator failure.
    const PhysicsBodyHandle candidate{nextHandleId_++};
    try {
        bodies_.emplace(candidate.id, BodyEntry{bodyId, ColliderShapeDesc{}});
        bodyIdMap_.associate(bodyId, candidate);
    } catch (const std::bad_alloc&) {
        bodies_.erase(candidate.id);
        bodyIdMap_.dissociate(bodyId);
        bodyInterface_->RemoveBody(bodyId);
        bodyInterface_->DestroyBody(bodyId);
        diagnostic_.code = PhysicsWorldDiagnostic::Code::TerrainBuildFailed;
        diagnostic_.message = "PHYSICS-TERRAIN-REGISTRATION-ALLOCATION";
        return false;
    }
    if (terrainHandle_.valid())
        destroyBody(terrainHandle_);
    terrainHandle_ = candidate;
    diagnostic_.code = PhysicsWorldDiagnostic::Code::None;
    diagnostic_.message.clear();
    return true;
}

arp::core::UniquePtr<PhysicsWorld> createJoltPhysicsWorld(PhysicsWorldDiagnostic& outDiagnostic)
{
    auto world = arp::core::makeUnique<JoltPhysicsWorld>();
    outDiagnostic.code = PhysicsWorldDiagnostic::Code::None;
    outDiagnostic.message.clear();
    return world;
}

} // namespace arp::physics::jolt_backend
