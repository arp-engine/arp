#pragma once

// Jolt backend: object-layer / broadphase-layer configuration (internal).
//
// Maps the L2 CollisionLayer enum to Jolt's ObjectLayer + BroadPhaseLayer.
// This is backend-private; upstream only sees CollisionLayer. The layer
// count is bounded by CollisionLayer::Count and must stay in sync with
// physics_types.h (covered by physics_l2_contract_tests).

#include "physics/physics_types.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

namespace arp::physics::jolt_backend {

/// Number of distinct object layers. Mirrors CollisionLayer::Count.
inline constexpr JPH::uint kObjectLayerCount = static_cast<JPH::uint>(CollisionLayer::Count);

/// Broadphase layers: NonMoving (Static + Trigger) and Moving (Dynamic +
/// Character). Two broadphase layers keep the static set stable for
/// broadphase culling while dynamic bodies churn.
namespace BroadPhaseLayers {
inline constexpr JPH::BroadPhaseLayer NonMoving(0);
inline constexpr JPH::BroadPhaseLayer Moving(1);
inline constexpr JPH::uint NumLayers = 2;
} // namespace BroadPhaseLayers

/// Maps ObjectLayer -> BroadPhaseLayer. Jolt takes a reference to this
/// instance, so it must outlive the PhysicsSystem.
class JoltLayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
    JoltLayerInterface()
    {
        mObjectToBroadPhase[static_cast<JPH::uint>(CollisionLayer::Static)] =
            BroadPhaseLayers::NonMoving;
        mObjectToBroadPhase[static_cast<JPH::uint>(CollisionLayer::Dynamic)] =
            BroadPhaseLayers::Moving;
        mObjectToBroadPhase[static_cast<JPH::uint>(CollisionLayer::Trigger)] =
            BroadPhaseLayers::NonMoving;
        mObjectToBroadPhase[static_cast<JPH::uint>(CollisionLayer::Character)] =
            BroadPhaseLayers::Moving;
    }

    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NumLayers; }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
    {
        return mObjectToBroadPhase[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
    {
        switch (inLayer.GetValue()) {
        case 0:
            return "NonMoving";
        case 1:
            return "Moving";
        default:
            return "Invalid";
        }
    }
#endif

private:
    JPH::BroadPhaseLayer mObjectToBroadPhase[kObjectLayerCount];
};

/// Filters which object layers can collide with which broadphase layers.
/// Delegates to the L2 layersCollide() contract so the matrix stays
/// versioned in one place (physics_types.cpp).
class JoltObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(
        JPH::ObjectLayer inObject1, JPH::BroadPhaseLayer inBroadPhase2) const override
    {
        const auto layer1 = static_cast<CollisionLayer>(inObject1);
        // Decode broadphase2 back to a representative object layer to reuse
        // the L2 matrix. NonMoving -> Static, Moving -> Dynamic.
        const auto layer2 = (inBroadPhase2 == BroadPhaseLayers::Moving) ? CollisionLayer::Dynamic
                                                                        : CollisionLayer::Static;
        return layersCollide(layer1, layer2);
    }
};

/// Filters which object layers can collide with each other. Delegates to
/// the L2 layersCollide() contract.
class JoltObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
    {
        return layersCollide(
            static_cast<CollisionLayer>(inObject1), static_cast<CollisionLayer>(inObject2));
    }
};

} // namespace arp::physics::jolt_backend
