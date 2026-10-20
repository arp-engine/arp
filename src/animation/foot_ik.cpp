#include "animation/foot_ik.h"

#include "animation/ik.h"
#include "core/cpu_allocator.h"
#include "core/math.h"

#include <algorithm>
#include <cmath>

namespace animation {

namespace {

constexpr float kEpsilon = 1e-6f;

// animation::Quat and arp::core::Quat share the same {x,y,z,w} layout.
inline const arp::core::Quat& asCoreQuat(const Quat& q)
{
    return reinterpret_cast<const arp::core::Quat&>(q);
}

inline Quat asAnimQuat(const arp::core::Quat& q)
{
    Quat r;
    r.x = q.x;
    r.y = q.y;
    r.z = q.z;
    r.w = q.w;
    return r;
}

// Extract translation (model-space position) from an animation::Mat4
// (column-major: translation in m[12], m[13], m[14]).
arp::core::Vec3 matTranslation(const Mat4& m)
{
    return arp::core::Vec3{m.m[12], m.m[13], m.m[14]};
}

// Transform a direction (no translation) by an animation::Mat4 (column-major).
arp::core::Vec3 matTransformVector(const Mat4& m, arp::core::Vec3 v)
{
    return arp::core::Vec3{
        m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z,
        m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z,
        m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z,
    };
}

// Extract the rotation part of an animation::Mat4 as a core::Quat by reading
// the basis vectors and converting. Column-major: columns are basis vectors.
//   col0 = (m[0], m[1], m[2])  = right
//   col1 = (m[4], m[5], m[6])  = up
//   col2 = (m[8], m[9], m[10]) = forward
arp::core::Quat matRotationToQuat(const Mat4& m)
{
    const arp::core::Vec3 col0{m.m[0], m.m[1], m.m[2]};
    const arp::core::Vec3 col1{m.m[4], m.m[5], m.m[6]};
    const arp::core::Vec3 col2{m.m[8], m.m[9], m.m[10]};
    const float r0 = col0.x, r1 = col0.y, r2 = col0.z;
    const float u0 = col1.x, u1 = col1.y, u2 = col1.z;
    const float f0 = col2.x, f1 = col2.y, f2 = col2.z;
    const float trace = r0 + u1 + f2;
    arp::core::Quat q;
    if (trace > 0.0f) {
        const float s = 0.5f / std::sqrt(trace + 1.0f);
        q.w = 0.25f / s;
        q.x = (u2 - f1) * s;
        q.y = (f0 - r2) * s;
        q.z = (r1 - u0) * s;
    } else if (r0 > u1 && r0 > f2) {
        const float s = 2.0f * std::sqrt(1.0f + r0 - u1 - f2);
        q.w = (u2 - f1) / s;
        q.x = 0.25f * s;
        q.y = (u0 + r1) / s;
        q.z = (f0 + r2) / s;
    } else if (u1 > f2) {
        const float s = 2.0f * std::sqrt(1.0f + u1 - r0 - f2);
        q.w = (f0 - r2) / s;
        q.x = (u0 + r1) / s;
        q.y = 0.25f * s;
        q.z = (f1 + u2) / s;
    } else {
        const float s = 2.0f * std::sqrt(1.0f + f2 - r0 - u1);
        q.w = (r1 - u0) / s;
        q.x = (f0 + r2) / s;
        q.y = (f1 + u2) / s;
        q.z = 0.25f * s;
    }
    return arp::core::normalize(q);
}

struct LegSolution {
    arp::core::Quat hipLocalDelta;
    arp::core::Quat kneeLocalDelta;
    arp::core::Quat footLocalDelta;
    bool applied = false;
};

// `solveTwoBoneIK` produces a rotation in model space.  Foot IK is applied by
// post-multiplying a joint's *local* rotation, so the model-space delta must
// be conjugated by that joint's current model rotation.  The old conversion
// used only the parent inverse, which mixed spaces and could fold a rotated
// FBX leg back through the torso.
arp::core::Quat modelDeltaToLocalPostDelta(
    const arp::core::Quat jointModelRotation, const arp::core::Quat modelDelta)
{
    const auto inverseJoint = arp::core::normalize(arp::core::conjugate(jointModelRotation));
    return arp::core::normalize(inverseJoint * modelDelta * jointModelRotation);
}

arp::core::Quat limitRotationDelta(const arp::core::Quat delta, const float maxRadians)
{
    const float halfAngle = std::acos(std::max(-1.0f, std::min(1.0f, delta.w)));
    const float angle = halfAngle * 2.0f;
    if (angle <= maxRadians || angle < kEpsilon) {
        return delta;
    }
    return arp::core::quatSlerp(arp::core::Quat::identity(), delta, maxRadians / angle);
}

bool solveLeg(const Skeleton& skeleton,
    const std::pmr::vector<Mat4>& modelPoseIn,
    arp::core::Vec3 rootWorldPos,
    const FootIKLegInput& leg,
    float contactWeight,
    float maxUp,
    float maxDown,
    arp::core::Vec3 worldUp,
    const FootIKGroundQuery& ground,
    LegSolution& out)
{
    if (leg.hipJoint < 0 || leg.kneeJoint < 0 || leg.ankleJoint < 0) {
        return false;
    }
    const auto jc = static_cast<int>(modelPoseIn.size());
    if (leg.hipJoint >= jc || leg.kneeJoint >= jc || leg.ankleJoint >= jc) {
        return false;
    }

    // Model-space joint positions.
    const auto hipModel = matTranslation(modelPoseIn[leg.hipJoint]);
    const auto kneeModel = matTranslation(modelPoseIn[leg.kneeJoint]);
    const auto ankleModel = matTranslation(modelPoseIn[leg.ankleJoint]);

    // World-space ankle position.
    const auto ankleWorld = rootWorldPos + ankleModel;

    if (ground.sampleHeight == nullptr) {
        return false;
    }
    const float groundHeight = ground.sampleHeight(ground.ctx, ankleWorld.x, ankleWorld.z);

    // Offset to apply (world up positive).
    float offset = groundHeight - ankleWorld.y;
    offset = std::max(-maxDown, std::min(maxUp, offset));
    offset *= std::max(0.0f, std::min(1.0f, contactWeight));

    if (std::fabs(offset) < kEpsilon) {
        return false; // Nothing to do.
    }

    // New ankle target in model space (offset is world-space along worldUp).
    const auto newAnkleModel = ankleModel + arp::core::scale(worldUp, offset);

    // Pole vector: bend knees forward. Use the world up crossed with the
    // hip->ankle direction to get a stable bend plane. When these are
    // parallel (straight vertical leg), fall back to a forward axis.
    const auto hipToAnkle = ankleModel - hipModel;
    auto pole = arp::core::cross(worldUp, hipToAnkle);
    if (arp::core::length(pole) < kEpsilon) {
        // Leg is vertical: bend forward (+Z) so the knee bends naturally.
        pole = arp::core::Vec3{0.0f, 0.0f, 1.0f};
    }

    TwoBoneIKRequest ikReq;
    ikReq.rootPos = hipModel;
    ikReq.midPos = kneeModel;
    ikReq.endPos = ankleModel;
    ikReq.targetPos = newAnkleModel;
    ikReq.poleVector = pole;
    ikReq.weight = 1.0f;
    const auto ikRes = solveTwoBoneIK(ikReq);
    if (!ikRes.success) {
        return false;
    }

    // Convert model-space rotation deltas to local-space. The hip's parent
    // model rotation is its parent joint's model rotation (or identity for
    // root). The knee's parent is the hip, the foot's parent is the ankle.
    const auto hipModelRot = matRotationToQuat(modelPoseIn[leg.hipJoint]);
    const auto kneeModelRot = matRotationToQuat(modelPoseIn[leg.kneeJoint]);

    // Conservative angular limits make contact correction graceful when an
    // imported animation has an unexpected knee axis or a noisy terrain edge.
    out.hipLocalDelta =
        limitRotationDelta(modelDeltaToLocalPostDelta(hipModelRot, ikRes.rootRotation), 0.30f);
    out.kneeLocalDelta =
        limitRotationDelta(modelDeltaToLocalPostDelta(kneeModelRot, ikRes.midRotation), 0.45f);

    // Foot alignment to ground normal (optional, only if footJoint valid).
    out.footLocalDelta = arp::core::Quat::identity();
    if (leg.footJoint >= 0 && leg.footJoint < jc) {
        // Estimate ground normal via finite differences.
        const float eps = 0.05f;
        const float hL = ground.sampleHeight(ground.ctx, ankleWorld.x - eps, ankleWorld.z);
        const float hR = ground.sampleHeight(ground.ctx, ankleWorld.x + eps, ankleWorld.z);
        const float hD = ground.sampleHeight(ground.ctx, ankleWorld.x, ankleWorld.z - eps);
        const float hU = ground.sampleHeight(ground.ctx, ankleWorld.x, ankleWorld.z + eps);
        const auto normal = arp::core::normalize(arp::core::Vec3{
            hL - hR,
            2.0f * eps,
            hD - hU,
        });
        // Project foot forward onto the ground plane (remove component along normal).
        const auto footModelRot = matRotationToQuat(modelPoseIn[leg.footJoint]);
        const auto footWorldForward =
            matTransformVector(modelPoseIn[leg.footJoint], leg.footForwardLocal);
        auto projectedForward =
            footWorldForward - arp::core::scale(normal, arp::core::dot(footWorldForward, normal));
        if (arp::core::length(projectedForward) > kEpsilon) {
            projectedForward = arp::core::normalize(projectedForward);
            const auto delta = arp::core::quatFromToRotation(
                arp::core::normalize(footWorldForward), projectedForward);
            out.footLocalDelta =
                limitRotationDelta(modelDeltaToLocalPostDelta(footModelRot, delta), 0.25f);
        }
    }

    out.applied = true;
    return true;
}

} // namespace

bool applyFootIK(const Skeleton& skeleton,
    const std::pmr::vector<LocalTransform>& localPoseIn,
    const std::pmr::vector<Mat4>& modelPoseIn,
    const arp::core::Vec3 rootWorldPos,
    const FootIKRequest& req,
    std::pmr::vector<LocalTransform>& localPoseOut)
{
    if (req.ground.sampleHeight == nullptr) {
        return false;
    }
    if (localPoseIn.size() != modelPoseIn.size() || localPoseIn.size() != skeleton.jointCount()) {
        return false;
    }

    // Start from the input pose.
    if (localPoseOut.size() != localPoseIn.size()) {
        localPoseOut.resize(localPoseIn.size());
    }
    for (std::size_t i = 0; i < localPoseIn.size(); ++i) {
        localPoseOut[i] = localPoseIn[i];
    }

    // Solve each leg and apply local-space deltas.
    LegSolution left, right;
    const bool leftOk = solveLeg(skeleton,
        modelPoseIn,
        rootWorldPos,
        req.left,
        req.leftContactWeight,
        req.maxFootOffsetUp,
        req.maxFootOffsetDown,
        req.worldUp,
        req.ground,
        left);
    const bool rightOk = solveLeg(skeleton,
        modelPoseIn,
        rootWorldPos,
        req.right,
        req.rightContactWeight,
        req.maxFootOffsetUp,
        req.maxFootOffsetDown,
        req.worldUp,
        req.ground,
        right);

    if (leftOk && left.applied) {
        localPoseOut[req.left.hipJoint].rotation =
            asAnimQuat(arp::core::quatNormalize(arp::core::quatMul(
                asCoreQuat(localPoseOut[req.left.hipJoint].rotation), left.hipLocalDelta)));
        localPoseOut[req.left.kneeJoint].rotation =
            asAnimQuat(arp::core::quatNormalize(arp::core::quatMul(
                asCoreQuat(localPoseOut[req.left.kneeJoint].rotation), left.kneeLocalDelta)));
        if (req.left.footJoint >= 0) {
            localPoseOut[req.left.footJoint].rotation =
                asAnimQuat(arp::core::quatNormalize(arp::core::quatMul(
                    asCoreQuat(localPoseOut[req.left.footJoint].rotation), left.footLocalDelta)));
        }
    }
    if (rightOk && right.applied) {
        localPoseOut[req.right.hipJoint].rotation =
            asAnimQuat(arp::core::quatNormalize(arp::core::quatMul(
                asCoreQuat(localPoseOut[req.right.hipJoint].rotation), right.hipLocalDelta)));
        localPoseOut[req.right.kneeJoint].rotation =
            asAnimQuat(arp::core::quatNormalize(arp::core::quatMul(
                asCoreQuat(localPoseOut[req.right.kneeJoint].rotation), right.kneeLocalDelta)));
        if (req.right.footJoint >= 0) {
            localPoseOut[req.right.footJoint].rotation =
                asAnimQuat(arp::core::quatNormalize(arp::core::quatMul(
                    asCoreQuat(localPoseOut[req.right.footJoint].rotation), right.footLocalDelta)));
        }
    }

    return true;
}

} // namespace animation
