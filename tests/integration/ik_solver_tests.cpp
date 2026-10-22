// Two-Bone IK + Look-at IK solver contract tests (Phase 3).
//
// Validates the pure kinematic solvers with known-geometry cases: straight
// chains, reach clamping, weight blending, and angle limits.

#include "animation/ik.h"
#include "core/math/math.h"
#include "core/memory/cpu_allocator.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

bool near(float lhs, float rhs, float epsilon = 0.001f)
{
    return std::fabs(lhs - rhs) <= epsilon;
}

void requireNear(const char* label, float lhs, float rhs, float epsilon = 0.001f)
{
    if (!near(lhs, rhs, epsilon)) {
        std::printf("%s expected %.6f got %.6f\n", label, rhs, lhs);
        std::exit(1);
    }
}

float angleBetween(arp::core::Vec3 a, arp::core::Vec3 b)
{
    const auto na = arp::core::normalize(a);
    const auto nb = arp::core::normalize(b);
    const float d = std::max(-1.0f, std::min(1.0f, arp::core::dot(na, nb)));
    return std::acos(d);
}

} // namespace

int main()
{
    using namespace arp::core;

    // ---- Two-Bone IK: straight chain reaching sideways ----
    // root=(0,0,0), mid=(1,0,0), end=(2,0,0). Target=(1,0,1) -> end moves up.
    {
        animation::TwoBoneIKRequest req;
        req.rootPos = {0.0f, 0.0f, 0.0f};
        req.midPos = {1.0f, 0.0f, 0.0f};
        req.endPos = {2.0f, 0.0f, 0.0f};
        req.targetPos = {1.0f, 0.0f, 1.0f};
        req.poleVector = {0.0f, 1.0f, 0.0f};
        req.weight = 1.0f;

        const auto res = animation::solveTwoBoneIK(req);
        assert(res.success);

        // Apply rootRotation to (mid-root), then midRotation to (end-mid).
        const auto oldRootToMid = Vec3{1.0f, 0.0f, 0.0f};
        const auto newRootToMid = rotate(res.rootRotation, oldRootToMid);
        const auto newMid = req.rootPos + newRootToMid;

        const auto oldMidToEnd = Vec3{1.0f, 0.0f, 0.0f};
        const auto newMidToEnd = rotate(res.midRotation, oldMidToEnd);
        const auto newEnd = newMid + newMidToEnd;

        // End should be close to target (within chain reach).
        requireNear("twobone end x", newEnd.x, 1.0f, 0.01f);
        requireNear("twobone end z", newEnd.z, 1.0f, 0.01f);
        // Mid should have moved up (Y > 0) due to pole.
        requireNear("twobone mid y sign", newMid.y, 0.7071f, 0.05f);
    }

    // ---- Two-Bone IK: target beyond reach -> clamped ----
    {
        animation::TwoBoneIKRequest req;
        req.rootPos = {0.0f, 0.0f, 0.0f};
        req.midPos = {1.0f, 0.0f, 0.0f};
        req.endPos = {2.0f, 0.0f, 0.0f};
        req.targetPos = {10.0f, 0.0f, 0.0f}; // Way beyond 2-unit arm.
        req.poleVector = {0.0f, 1.0f, 0.0f};

        const auto res = animation::solveTwoBoneIK(req);
        assert(res.success);

        const auto newRootToMid = rotate(res.rootRotation, Vec3{1.0f, 0.0f, 0.0f});
        const auto newMid = req.rootPos + newRootToMid;
        const auto newMidToEnd = rotate(res.midRotation, Vec3{1.0f, 0.0f, 0.0f});
        const auto newEnd = newMid + newMidToEnd;

        // Fully extended: end at max reach = 2.0 along +X.
        requireNear("twobone clamped end x", newEnd.x, 2.0f, 0.01f);
        requireNear("twobone clamped end y", newEnd.y, 0.0f, 0.01f);
    }

    // ---- Two-Bone IK: zero-length chain -> fail ----
    {
        animation::TwoBoneIKRequest req;
        req.rootPos = {0.0f, 0.0f, 0.0f};
        req.midPos = {0.0f, 0.0f, 0.0f}; // Same as root.
        req.endPos = {1.0f, 0.0f, 0.0f};
        req.targetPos = {1.0f, 0.0f, 0.0f};
        req.poleVector = {0.0f, 1.0f, 0.0f};

        const auto res = animation::solveTwoBoneIK(req);
        assert(!res.success);
    }

    // ---- Two-Bone IK: weight=0.5 -> end halfway ----
    {
        animation::TwoBoneIKRequest req;
        req.rootPos = {0.0f, 0.0f, 0.0f};
        req.midPos = {1.0f, 0.0f, 0.0f};
        req.endPos = {2.0f, 0.0f, 0.0f};
        req.targetPos = {1.0f, 0.0f, 1.0f}; // Reachable (dist=sqrt(2)<2).
        req.poleVector = {0.0f, 1.0f, 0.0f};
        req.weight = 0.5f;

        const auto res = animation::solveTwoBoneIK(req);
        assert(res.success);

        const auto newRootToMid = rotate(res.rootRotation, Vec3{1.0f, 0.0f, 0.0f});
        const auto newMid = req.rootPos + newRootToMid;
        const auto newMidToEnd = rotate(res.midRotation, Vec3{1.0f, 0.0f, 0.0f});
        const auto newEnd = newMid + newMidToEnd;

        // Effective target = lerp(end, target, 0.5) = lerp((2,0,0),(1,0,1),0.5)
        // = (1.5, 0, 0.5). End should reach that point.
        requireNear("twobone weight0.5 end x", newEnd.x, 1.5f, 0.05f);
        requireNear("twobone weight0.5 end z", newEnd.z, 0.5f, 0.05f);
    }

    // ---- Look-at IK: target straight ahead -> identity ----
    {
        animation::LookAtIKRequest req;
        req.jointPos = {0.0f, 0.0f, 0.0f};
        req.jointCurrentRotation = Quat::identity();
        req.jointForwardLocal = {0.0f, 0.0f, 1.0f};
        req.targetPos = {0.0f, 0.0f, 10.0f}; // Straight ahead.

        const auto res = animation::solveLookAtIK(req);
        assert(res.success);
        requireNear("lookat ahead w", res.rotationDelta.w, 1.0f, 0.001f);
    }

    // ---- Look-at IK: target 90 degrees to the side ----
    {
        animation::LookAtIKRequest req;
        req.jointPos = {0.0f, 0.0f, 0.0f};
        req.jointCurrentRotation = Quat::identity();
        req.jointForwardLocal = {0.0f, 0.0f, 1.0f};
        req.targetPos = {10.0f, 0.0f, 0.0f}; // 90 deg to +X.

        const auto res = animation::solveLookAtIK(req);
        assert(res.success);
        // Delta should be a 90-degree rotation about Y.
        const auto newForward = rotate(res.rotationDelta, Vec3{0.0f, 0.0f, 1.0f});
        requireNear("lookat 90 x", newForward.x, 1.0f, 0.01f);
        requireNear("lookat 90 z", newForward.z, 0.0f, 0.01f);
    }

    // ---- Look-at IK: maxAngle clamps rotation ----
    {
        animation::LookAtIKRequest req;
        req.jointPos = {0.0f, 0.0f, 0.0f};
        req.jointCurrentRotation = Quat::identity();
        req.jointForwardLocal = {0.0f, 0.0f, 1.0f};
        req.targetPos = {10.0f, 0.0f, 0.0f}; // 90 deg.
        req.maxAngleRadians = 0.785398f;     // 45 deg.

        const auto res = animation::solveLookAtIK(req);
        assert(res.success);
        const auto newForward = rotate(res.rotationDelta, Vec3{0.0f, 0.0f, 1.0f});
        // After clamping to 45 deg, forward should be at 45 deg from +Z.
        const float ang = angleBetween(Vec3{0.0f, 0.0f, 1.0f}, newForward);
        requireNear("lookat clamp angle", ang, 0.785398f, 0.02f);
    }

    // ---- Look-at IK: weight=0.5 -> half the rotation ----
    {
        animation::LookAtIKRequest req;
        req.jointPos = {0.0f, 0.0f, 0.0f};
        req.jointCurrentRotation = Quat::identity();
        req.jointForwardLocal = {0.0f, 0.0f, 1.0f};
        req.targetPos = {10.0f, 0.0f, 0.0f}; // 90 deg.
        req.weight = 0.5f;

        const auto res = animation::solveLookAtIK(req);
        assert(res.success);
        const auto newForward = rotate(res.rotationDelta, Vec3{0.0f, 0.0f, 1.0f});
        const float ang = angleBetween(Vec3{0.0f, 0.0f, 1.0f}, newForward);
        requireNear("lookat weight0.5 angle", ang, 0.785398f, 0.02f); // ~45 deg.
    }

    // ---- worldRotationToLocal: identity parent -> delta unchanged ----
    {
        const auto worldDelta = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, 0.5f);
        const auto localDelta = animation::worldRotationToLocal(Quat::identity(), worldDelta);
        requireNear("worldToLocal w", localDelta.w, worldDelta.w, 0.001f);
        requireNear("worldToLocal y", localDelta.y, worldDelta.y, 0.001f);
    }

    std::printf("ik_solver_tests: all passed\n");
    return 0;
}
