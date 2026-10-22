#pragma once

// Jolt backend: ContactListener bridge (internal, backend-private).
//
// Implements JPH::ContactListener and translates native contact callbacks
// into L2 PhysicsContactEvent, dispatched to the registered
// PhysicsEventListener per body. This is the only place that touches the
// Jolt ContactListener API; upstream code only implements PhysicsEventListener.

#include "physics/3d/physics_event_listener.h"
#include "physics/3d/physics_types.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <cstdint>

namespace arp::physics::jolt_backend {

/// Maps Jolt BodyID (uint32) to the L2 PhysicsBodyHandle the engine issued.
/// The JoltPhysicsWorld owns this map; the bridge queries it to translate
/// native body IDs back into L2 handles for event delivery.
class JoltBodyIdMap {
public:
    void associate(JPH::BodyID joltId, PhysicsBodyHandle l2Handle)
    {
        idToL2_[joltId.GetIndexAndSequenceNumber()] = l2Handle;
    }

    void dissociate(JPH::BodyID joltId) { idToL2_.erase(joltId.GetIndexAndSequenceNumber()); }

    PhysicsBodyHandle resolve(JPH::BodyID joltId) const
    {
        const auto it = idToL2_.find(joltId.GetIndexAndSequenceNumber());
        return it == idToL2_.end() ? PhysicsBodyHandle{} : it->second;
    }

private:
    core::pmr::UnorderedMap<std::uint32_t, PhysicsBodyHandle> idToL2_{
        &core::defaultCpuMemoryResource()};
};

/// Bridges Jolt contact callbacks to L2 PhysicsEventListener instances.
/// Thread-safety: callbacks are invoked from Jolt jobs; listener
/// implementations must be thread-safe (documented on PhysicsEventListener).
class JoltContactBridge final : public JPH::ContactListener {
public:
    explicit JoltContactBridge(const JoltBodyIdMap& idMap) : idMap_(idMap) {}

    /// Register/remove an L2 listener for a given L2 handle. The bridge
    /// does not own the listener pointer.
    void setListener(PhysicsBodyHandle handle, PhysicsEventListener* listener);

    // JPH::ContactListener overrides.
    JPH::ValidateResult OnContactValidate(const JPH::Body& inBody1,
        const JPH::Body& inBody2,
        JPH::RVec3Arg inBaseOffset,
        const JPH::CollideShapeResult& inCollisionResult) override;

    void OnContactAdded(const JPH::Body& inBody1,
        const JPH::Body& inBody2,
        const JPH::ContactManifold& inManifold,
        JPH::ContactSettings& ioSettings) override;

    void OnContactPersisted(const JPH::Body& inBody1,
        const JPH::Body& inBody2,
        const JPH::ContactManifold& inManifold,
        JPH::ContactSettings& ioSettings) override;

    void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override;

private:
    void dispatch(
        JPH::BodyID a, JPH::BodyID b, const JPH::ContactManifold& manifold, PhysicsEventKind kind);

    const JoltBodyIdMap& idMap_;
    core::pmr::UnorderedMap<std::uint64_t, PhysicsEventListener*> listeners_{
        &core::defaultCpuMemoryResource()};
};

} // namespace arp::physics::jolt_backend
