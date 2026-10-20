#pragma once

// L2 Physics Event Listener contract.
//
// Upstream code (gameplay, ColliderComponent) implements this interface to
// receive physics contact events. It is the ONLY event surface the physics
// layer exposes; backend-native listeners (e.g. Jolt ContactListener) are
// bridged internally and never referenced upstream (AGENTS.md §1: the upper
// layer only expresses L2 intent, never observes backend types).

#include "physics/physics_types.h"

namespace arp::physics {

class PhysicsEventListener {
public:
    virtual ~PhysicsEventListener() = default;

    /// Called when a body this listener is registered against produces a
    /// contact event. Called on the physics thread; implementations must
    /// not block, allocate unboundedly, or touch GPU resources.
    virtual void onPhysicsEvent(const PhysicsContactEvent& event) = 0;
};

} // namespace arp::physics
