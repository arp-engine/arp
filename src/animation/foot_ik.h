#pragma once

// Foot IK contract (L2 animation domain).
//
// Adjusts leg poses so feet conform to static terrain. This is pure
// kinematic IK -- no physics. Ground height is queried via an injected
// function pointer so the animation layer does not depend on the pcg
// module (AGENTS.md: no silent coupling).
//
// Pipeline per leg:
//   1. Read ankle world position from modelPoseIn + rootWorldPos.
//   2. Query ground height at (x, z).
//   3. Compute offset = clamp(groundY - ankleY, -maxDown, +maxUp).
//   4. Scale offset by contactWeight (swing phase -> 0).
//   5. Solve Two-Bone IK (hip->knee->ankle) to reach adjusted target.
//   6. Align foot to ground normal via Look-at.
//   7. Convert model-space deltas to local-space, write to localPoseOut.
//
// Fail-closed: null ground query, joint index out of range, or size
// mismatch returns false.

#include "animation/ik.h"
#include "animation/skeleton.h"
#include "core/api.h"
#include "core/cpu_allocator.h"
#include "core/math.h"

namespace animation {

struct FootIKLegInput {
    int hipJoint = -1;
    int kneeJoint = -1;
    int ankleJoint = -1;
    int footJoint = -1; ///< Foot orientation joint (ankle child).
    arp::core::Vec3 footForwardLocal{0.0f, 0.0f, 1.0f};
};

struct FootIKGroundQuery {
    /// Sample terrain height at world (x, z). Must be non-null for evaluation.
    /// Pure and deterministic; mirrors TerrainHeightField::sampleHeight.
    float (*sampleHeight)(void* ctx, float x, float z) = nullptr;
    void* ctx = nullptr;
};

struct FootIKRequest {
    FootIKLegInput left;
    FootIKLegInput right;
    FootIKGroundQuery ground;
    float leftContactWeight = 0.0f; ///< [0,1]: 1=planted, 0=swing.
    float rightContactWeight = 0.0f;
    float maxFootOffsetUp = 0.3f;   ///< Max upward correction (m).
    float maxFootOffsetDown = 0.3f; ///< Max downward correction (m).
    arp::core::Vec3 worldUp{0.0f, 1.0f, 0.0f};
};

/// Apply foot IK to a local-space pose. modelPoseIn must be the model-space
/// pose computed from localPoseIn (caller builds it first). rootWorldPos
/// offsets model-space positions into world space for ground queries.
/// On success, localPoseOut contains the corrected local-space pose.
ARP_API bool applyFootIK(const Skeleton& skeleton,
    const std::pmr::vector<LocalTransform>& localPoseIn,
    const std::pmr::vector<Mat4>& modelPoseIn,
    arp::core::Vec3 rootWorldPos,
    const FootIKRequest& req,
    std::pmr::vector<LocalTransform>& localPoseOut);

} // namespace animation
