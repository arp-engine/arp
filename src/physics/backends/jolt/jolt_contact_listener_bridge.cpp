#include "physics/backends/jolt/jolt_contact_listener_bridge.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Collision/ContactListener.h>

namespace arp::physics::jolt_backend {

void JoltContactBridge::setListener(PhysicsBodyHandle handle, PhysicsEventListener* listener)
{
    if (!handle.valid()) {
        return;
    }
    if (listener == nullptr) {
        listeners_.erase(handle.id);
    } else {
        listeners_[handle.id] = listener;
    }
}

JPH::ValidateResult JoltContactBridge::OnContactValidate(const JPH::Body& inBody1,
    const JPH::Body& inBody2,
    JPH::RVec3Arg inBaseOffset,
    const JPH::CollideShapeResult& inCollisionResult)
{
    (void)inBody1;
    (void)inBody2;
    (void)inBaseOffset;
    (void)inCollisionResult;
    // Accept all contacts; the L2 layer-pair filter already decided whether
    // the pair should collide. Trigger bodies report contacts but their
    // physical response is suppressed via ContactSettings in OnContactAdded.
    return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
}

void JoltContactBridge::OnContactAdded(const JPH::Body& inBody1,
    const JPH::Body& inBody2,
    const JPH::ContactManifold& inManifold,
    JPH::ContactSettings& ioSettings)
{
    // If either body is a trigger (sensor), suppress the physical response
    // so the contact produces an event but no impulse.
    const auto layer1 = static_cast<CollisionLayer>(inBody1.GetObjectLayer());
    const auto layer2 = static_cast<CollisionLayer>(inBody2.GetObjectLayer());
    if (layer1 == CollisionLayer::Trigger || layer2 == CollisionLayer::Trigger) {
        ioSettings.mIsSensor = true;
    }
    dispatch(inBody1.GetID(), inBody2.GetID(), inManifold, PhysicsEventKind::ContactBegin);
}

void JoltContactBridge::OnContactPersisted(const JPH::Body& inBody1,
    const JPH::Body& inBody2,
    const JPH::ContactManifold& inManifold,
    JPH::ContactSettings& ioSettings)
{
    (void)ioSettings;
    dispatch(inBody1.GetID(), inBody2.GetID(), inManifold, PhysicsEventKind::ContactPersist);
}

void JoltContactBridge::OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair)
{
    const auto joltA = inSubShapePair.GetBody1ID();
    const auto joltB = inSubShapePair.GetBody2ID();
    const auto l2A = idMap_.resolve(joltA);
    const auto l2B = idMap_.resolve(joltB);

    PhysicsContactEvent eventA{};
    eventA.other = l2B;
    eventA.kind = PhysicsEventKind::ContactEnd;
    PhysicsContactEvent eventB{};
    eventB.other = l2A;
    eventB.kind = PhysicsEventKind::ContactEnd;

    const auto itA = listeners_.find(l2A.id);
    if (itA != listeners_.end() && itA->second) {
        itA->second->onPhysicsEvent(eventA);
    }
    const auto itB = listeners_.find(l2B.id);
    if (itB != listeners_.end() && itB->second) {
        itB->second->onPhysicsEvent(eventB);
    }
}

void JoltContactBridge::dispatch(
    JPH::BodyID a, JPH::BodyID b, const JPH::ContactManifold& manifold, PhysicsEventKind kind)
{
    const auto l2A = idMap_.resolve(a);
    const auto l2B = idMap_.resolve(b);

    // Contact point: first relative point + base offset (world space).
    JPH::RVec3 worldPoint = manifold.mBaseOffset;
    if (!manifold.mRelativeContactPointsOn1.empty()) {
        worldPoint += manifold.mRelativeContactPointsOn1[0];
    }

    PhysicsContactEvent eventA{};
    eventA.other = l2B;
    eventA.point = arp::core::Vec3{
        float(worldPoint.GetX()), float(worldPoint.GetY()), float(worldPoint.GetZ())};
    eventA.normal = arp::core::Vec3{manifold.mWorldSpaceNormal.GetX(),
        manifold.mWorldSpaceNormal.GetY(),
        manifold.mWorldSpaceNormal.GetZ()};
    eventA.penetrationDepth = manifold.mPenetrationDepth;
    eventA.kind = kind;

    PhysicsContactEvent eventB = eventA;
    eventB.other = l2A;
    // Normal points from body 1 to body 2; for body B it is reversed.
    eventB.normal = arp::core::Vec3{-eventA.normal.x, -eventA.normal.y, -eventA.normal.z};

    const auto itA = listeners_.find(l2A.id);
    if (itA != listeners_.end() && itA->second) {
        itA->second->onPhysicsEvent(eventA);
    }
    const auto itB = listeners_.find(l2B.id);
    if (itB != listeners_.end() && itB->second) {
        itB->second->onPhysicsEvent(eventB);
    }
}

} // namespace arp::physics::jolt_backend
