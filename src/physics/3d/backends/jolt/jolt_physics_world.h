#pragma once

// Jolt backend: PhysicsWorld implementation (internal, backend-private).
//
// JoltPhysicsWorld is the only production PhysicsWorld implementation. It
// owns the JPH::PhysicsSystem, BodyInterface, temp allocator, job system,
// layer filters and contact bridge. All Jolt types are private; upstream
// code only sees the L2 PhysicsWorld interface.
//
// Memory: the world itself is allocated via core::makeUnique (core
// allocator). Jolt-internal allocations go through Jolt's own allocator
// (RegisterDefaultAllocator). The L2 -> Jolt boundary is the lowering
// functions (shape factory, layer mapping, contact bridge).

#include "physics/3d/backends/jolt/jolt_contact_listener_bridge.h"
#include "physics/3d/backends/jolt/jolt_layers.h"
#include "physics/3d/physics_world.h"

#include <Jolt/Core/JobSystem.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <memory>

namespace arp::physics::jolt_backend {

class JoltPhysicsWorld final : public PhysicsWorld {
public:
    JoltPhysicsWorld();
    ~JoltPhysicsWorld() override;

    JoltPhysicsWorld(const JoltPhysicsWorld&) = delete;
    JoltPhysicsWorld& operator=(const JoltPhysicsWorld&) = delete;

    bool init(const PhysicsWorldConfig& config) override;

    PhysicsBodyHandle createBody(const RigidBodyDesc& desc,
        const arp::core::Vec3& position,
        const arp::core::Quat& rotation) override;
    void destroyBody(PhysicsBodyHandle handle) override;

    void setBodyTransform(PhysicsBodyHandle handle,
        const arp::core::Vec3& position,
        const arp::core::Quat& rotation) override;
    bool setBodyShape(PhysicsBodyHandle handle, const ColliderShapeDesc& shape) override;
    void getBodyTransform(PhysicsBodyHandle handle,
        arp::core::Vec3& outPosition,
        arp::core::Quat& outRotation) const override;

    void setBodyVelocity(PhysicsBodyHandle handle, const arp::core::Vec3& velocity) override;
    arp::core::Vec3 getBodyVelocity(PhysicsBodyHandle handle) const override;
    void addImpulse(PhysicsBodyHandle handle, const arp::core::Vec3& impulse) override;

    bool raycast(const RaycastQuery& query, RaycastHit& outHit) override;
    bool sweepCapsule(const CapsuleSweepQuery& query, CapsuleSweepHit& outHit) override;
    bool overlapsCapsule(const CapsuleOverlapQuery& query) override;

    bool step(float deltaSeconds) override;

    void registerEventListener(PhysicsBodyHandle handle, PhysicsEventListener* listener) override;

    bool setTerrainHeightField(
        const TerrainPhysicsSource& source, const TerrainPhysicsBounds& bounds) override;

    PhysicsBodyHandle terrainHeightFieldHandle() const noexcept override { return terrainHandle_; }

    bool clearTerrainHeightField(PhysicsBodyHandle expected) override;

    PhysicsWorldDiagnostic lastDiagnostic() const override { return diagnostic_; }

private:
    // Translate an L2 handle to its Jolt BodyID. Returns invalid BodyID if
    // the handle is stale or unknown.
    JPH::BodyID resolveJoltBodyId(PhysicsBodyHandle handle) const;

    bool initialized_ = false;
    PhysicsWorldConfig config_{};
    PhysicsWorldDiagnostic diagnostic_{};

    // Jolt infrastructure. Owned by this world; destroyed in reverse order.
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator_;
    std::unique_ptr<JPH::JobSystem> jobSystem_;
    std::unique_ptr<JoltLayerInterface> layerInterface_;
    std::unique_ptr<JoltObjectVsBroadPhaseLayerFilter> objVsBpFilter_;
    std::unique_ptr<JoltObjectLayerPairFilter> objPairFilter_;
    std::unique_ptr<JPH::PhysicsSystem> physicsSystem_;
    JPH::BodyInterface* bodyInterface_ = nullptr;

    // Contact bridge + body-id map.
    JoltBodyIdMap bodyIdMap_;
    std::unique_ptr<JoltContactBridge> contactBridge_;

    // L2 handle -> Jolt BodyID mapping. The L2 handle id is an ever-increasing
    // counter (never reused); the Jolt BodyID may be reused by Jolt after
    // destruction, so we erase from this map on destroyBody().
    struct BodyEntry {
        JPH::BodyID joltId;
        ColliderShapeDesc shapeDesc; // kept for diagnostics
    };

    core::pmr::UnorderedMap<std::uint64_t, BodyEntry> bodies_{&core::defaultCpuMemoryResource()};
    std::uint64_t nextHandleId_ = 1;

    // Terrain body (static). Replaced on each setTerrainHeightField call.
    PhysicsBodyHandle terrainHandle_;
};

/// Factory entry point called by createPhysicsWorld() when ARP_HAS_JOLT=1.
/// Returns nullptr + diagnostic on failure (fail-closed).
arp::core::UniquePtr<PhysicsWorld> createJoltPhysicsWorld(PhysicsWorldDiagnostic& outDiagnostic);

} // namespace arp::physics::jolt_backend
