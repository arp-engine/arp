#pragma once

// IK solver contract (L2 animation domain).
//
// Pure kinematic solvers: no physics, no forces, no native resources. Given
// joint positions / orientations and a target, compute rotation deltas that
// can be post-multiplied onto the joint's local rotation.
//
// All math uses core::math types (Vec3/Quat) to avoid the animation::Mat4
// column-major vs core::Mat4 row-major ambiguity (AGENTS.md Matrix Layout).
// Callers convert between model-space and local-space using the helpers
// provided here.
//
// Fail-closed: degenerate inputs (zero-length chains, parallel vectors)
// return success=false rather than producing NaN.

#include "core/api.h"
#include "core/math/math.h"

namespace animation {

/// Two-bone IK: solve a 3-joint chain (root -> mid -> end) so that `end`
/// reaches `targetPos`. All positions are in the same space (typically
/// model/skeleton space). The pole vector controls which way the middle
/// joint bends.
struct TwoBoneIKRequest {
    arp::core::Vec3 rootPos;
    arp::core::Vec3 midPos;
    arp::core::Vec3 endPos;
    arp::core::Vec3 targetPos;
    arp::core::Vec3 poleVector; ///< Bend direction; need not be normalized.
    float weight = 1.0f;        ///< [0,1]: 0=no change, 1=full reach.
};

struct TwoBoneIKResult {
    /// Rotation delta to apply to the root joint (maps old root->mid dir
    /// onto the new root->mid dir). Post-multiply onto the root's current
    /// world/model rotation, then convert to local space.
    arp::core::Quat rootRotation;
    /// Rotation delta to apply to the mid joint (maps old mid->end dir onto
    /// the new mid->end dir).
    arp::core::Quat midRotation;
    bool success = false;
};

ARP_API TwoBoneIKResult solveTwoBoneIK(const TwoBoneIKRequest& r);

/// Look-at IK: rotate a joint so its local forward axis points toward a
/// target. Returns a rotation delta to post-multiply onto the joint's
/// current local rotation.
struct LookAtIKRequest {
    arp::core::Vec3 jointPos;             ///< Joint position (model/world space).
    arp::core::Quat jointCurrentRotation; ///< Current world/model rotation.
    arp::core::Vec3 jointForwardLocal;    ///< Forward axis in joint local space.
    arp::core::Vec3 targetPos;            ///< Where to aim.
    float maxAngleRadians = 3.14159265f;  ///< Per-call rotation clamp.
    float weight = 1.0f;                  ///< [0,1]: 0=no rotation.
};

struct LookAtIKResult {
    arp::core::Quat rotationDelta; ///< Post-multiply onto jointCurrentRotation.
    bool success = false;
};

ARP_API LookAtIKResult solveLookAtIK(const LookAtIKRequest& r);

/// Convert a model/world-space rotation delta into the joint's local space,
/// given the parent joint's model/world rotation. Used when applying IK
/// results (which are computed in model space) as local-space additives.
/// localDelta = inverse(parentModelRot) * worldDelta.
ARP_API arp::core::Quat worldRotationToLocal(
    arp::core::Quat parentModelRotation, arp::core::Quat worldDelta);

} // namespace animation
