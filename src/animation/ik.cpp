#include "animation/ik.h"

#include "core/cpu_allocator.h"
#include "core/math.h"

#include <algorithm>
#include <cmath>

namespace animation {

namespace {

constexpr float kEpsilon = 1e-6f;

} // namespace

TwoBoneIKResult solveTwoBoneIK(const TwoBoneIKRequest& r)
{
    TwoBoneIKResult result;

    const auto a = r.rootPos;
    const auto b = r.midPos;
    const auto c = r.endPos;

    const float lenA = arp::core::length(b - a);
    const float lenB = arp::core::length(c - b);
    if (lenA < kEpsilon || lenB < kEpsilon) {
        return result; // Degenerate chain.
    }

    // Apply weight to the target: effective target is between end and target.
    const auto effectiveTarget =
        arp::core::lerp(c, r.targetPos, std::max(0.0f, std::min(1.0f, r.weight)));

    // Distance from root to effective target, clamped to reachable range.
    const auto targetDir = effectiveTarget - a;
    float targetDist = arp::core::length(targetDir);
    const float maxReach = lenA + lenB - kEpsilon;
    const float minReach = std::fabs(lenA - lenB) + kEpsilon;
    if (targetDist > maxReach) {
        targetDist = maxReach;
    }
    if (targetDist < minReach) {
        targetDist = minReach;
    }

    // Direction from root to (clamped) target.
    const auto reachDir = (arp::core::length(targetDir) < kEpsilon)
        ? arp::core::Vec3{0.0f, 1.0f, 0.0f}
        : arp::core::normalize(targetDir);

    // Cosine rule: angle at root between (root->mid) and (root->target).
    // cos(alpha) = (lenA^2 + targetDist^2 - lenB^2) / (2 * lenA * targetDist)
    const float cosAlpha =
        (lenA * lenA + targetDist * targetDist - lenB * lenB) / (2.0f * lenA * targetDist);
    const float alpha = std::acos(std::max(-1.0f, std::min(1.0f, cosAlpha)));

    // Project the pole vector onto the plane perpendicular to reachDir to
    // get the bend direction. The new mid position lies in the plane spanned
    // by reachDir and this projected pole.
    auto poleProjection =
        r.poleVector - arp::core::scale(reachDir, arp::core::dot(r.poleVector, reachDir));
    if (arp::core::length(poleProjection) < kEpsilon) {
        // Pole is parallel to reach; pick an arbitrary perpendicular.
        poleProjection = arp::core::cross(reachDir,
            (std::fabs(reachDir.x) < 0.9f) ? arp::core::Vec3{1.0f, 0.0f, 0.0f}
                                           : arp::core::Vec3{0.0f, 1.0f, 0.0f});
    }
    const auto bendDir = arp::core::normalize(poleProjection);

    // New mid position: rotate reachDir by alpha toward the bend direction,
    // scaled by lenA.
    // newMid = a + lenA * (cos(alpha)*reachDir + sin(alpha)*bendDir)
    const auto newMid = a
        + arp::core::scale(arp::core::scale(reachDir, std::cos(alpha))
                + arp::core::scale(bendDir, std::sin(alpha)),
            lenA);

    // New end position: along reachDir at targetDist from root.
    const auto newEnd = a + arp::core::scale(reachDir, targetDist);

    // Root rotation: maps old (root->mid) direction to new (root->newMid).
    const auto oldRootToMid = arp::core::normalize(b - a);
    const auto newRootToMid = arp::core::normalize(newMid - a);
    result.rootRotation = arp::core::quatFromToRotation(oldRootToMid, newRootToMid);

    // Mid rotation: maps old (mid->end) direction to new (newMid->newEnd).
    const auto oldMidToEnd = arp::core::normalize(c - b);
    const auto newMidToEnd = arp::core::normalize(newEnd - newMid);
    result.midRotation = arp::core::quatFromToRotation(oldMidToEnd, newMidToEnd);

    result.success = true;
    return result;
}

LookAtIKResult solveLookAtIK(const LookAtIKRequest& r)
{
    LookAtIKResult result;

    const auto forward =
        arp::core::normalizeOr(r.jointForwardLocal, arp::core::Vec3{0.0f, 0.0f, 1.0f});
    const auto toTarget = r.targetPos - r.jointPos;
    if (arp::core::length(toTarget) < kEpsilon) {
        // Target is at the joint: no rotation needed.
        result.rotationDelta = arp::core::Quat::identity();
        result.success = true;
        return result;
    }

    const auto desiredDir = arp::core::normalize(toTarget);
    const auto currentDir =
        arp::core::normalize(arp::core::rotate(r.jointCurrentRotation, forward));

    auto delta = arp::core::quatFromToRotation(currentDir, desiredDir);

    // Clamp to maxAngle: decompose to axis-angle, clamp angle, rebuild.
    const float angle = 2.0f * std::acos(std::max(-1.0f, std::min(1.0f, delta.w)));
    if (angle > r.maxAngleRadians) {
        const float halfClamped = r.maxAngleRadians * 0.5f;
        const float s = std::sin(halfClamped);
        const float invLen =
            (arp::core::length(arp::core::Vec3{delta.x, delta.y, delta.z}) < kEpsilon)
            ? 0.0f
            : s / std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        delta = arp::core::Quat{
            delta.x * invLen,
            delta.y * invLen,
            delta.z * invLen,
            std::cos(halfClamped),
        };
    }

    // Apply weight: slerp from identity to delta.
    const float w = std::max(0.0f, std::min(1.0f, r.weight));
    result.rotationDelta = arp::core::quatSlerp(arp::core::Quat::identity(), delta, w);
    result.success = true;
    return result;
}

arp::core::Quat worldRotationToLocal(
    const arp::core::Quat parentModelRotation, const arp::core::Quat worldDelta)
{
    // localDelta = inverse(parent) * worldDelta
    // For a unit quaternion, inverse = conjugate.
    const auto invParent = arp::core::normalize(arp::core::conjugate(parentModelRotation));
    return arp::core::normalize(invParent * worldDelta);
}

} // namespace animation
