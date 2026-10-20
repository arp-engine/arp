// Foot IK contract tests (Phase 4).
//
// Validates foot IK against flat ground, slopes, contact-weight gating,
// null-query failure, and offset clamping. Uses hand-built skeletons with
// simple leg geometry so expected positions are calculable by hand.

#include "animation/foot_ik.h"
#include "animation/skeleton.h"
#include "core/cpu_allocator.h"
#include "core/math.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

bool near(float lhs, float rhs, float epsilon = 0.01f)
{
    return std::fabs(lhs - rhs) <= epsilon;
}

void requireNear(const char* label, float lhs, float rhs, float epsilon = 0.01f)
{
    if (!near(lhs, rhs, epsilon)) {
        std::printf("%s expected %.6f got %.6f\n", label, rhs, lhs);
        std::exit(1);
    }
}

// A flat ground at height=0.
float flatGround(void* /*ctx*/, float /*x*/, float /*z*/)
{
    return 0.0f;
}

// A slope: height = x * slope.
struct SlopeCtx {
    float slope;
};

float slopeGround(void* ctx, float x, float /*z*/)
{
    return x * static_cast<SlopeCtx*>(ctx)->slope;
}

// Build a minimal skeleton: root(0) -> hipL(1) -> kneeL(2) -> ankleL(3) -> footL(4).
// All joints along -Y (standing). Inverse bind = identity for simplicity.
animation::Skeleton makeLegSkeleton()
{
    animation::Skeleton skel;
    skel.joints.push_back({"root", -1, animation::Mat4::identity()});
    skel.joints.push_back({"hipL", 0, animation::Mat4::identity()});
    skel.joints.push_back({"kneeL", 1, animation::Mat4::identity()});
    skel.joints.push_back({"ankleL", 2, animation::Mat4::identity()});
    skel.joints.push_back({"footL", 3, animation::Mat4::identity()});
    return skel;
}

// Build a local pose: root at (0,4,0), then hip at (0,-1,0), knee at (0,-1,0),
// ankle at (0,-1,0), foot at (0,0,0). This puts the ankle at world Y=1 (4-1-1-1=1).
std::pmr::vector<animation::LocalTransform> makeStandingPose()
{
    std::pmr::vector<animation::LocalTransform> pose(5);
    pose[0].translation = {0.0f, 4.0f, 0.0f};
    pose[1].translation = {0.0f, -1.0f, 0.0f};
    pose[2].translation = {0.0f, -1.0f, 0.0f};
    pose[3].translation = {0.0f, -1.0f, 0.0f};
    pose[4].translation = {0.0f, 0.0f, 0.0f};
    return pose;
}

} // namespace

int main()
{
    using namespace arp::core;

    // ---- Flat ground, ankle already on ground (y=0): no correction ----
    {
        auto skel = makeLegSkeleton();
        auto pose = makeStandingPose();
        // Root at y=0 so ankle = 0-1-1-1 = -3. Put root at y=3 so ankle=0.
        pose[0].translation = {0.0f, 3.0f, 0.0f};

        auto modelPose = animation::buildModelPose(skel, pose);
        animation::FootIKRequest req;
        req.left = {1, 2, 3, 4, {0.0f, 0.0f, 1.0f}};
        req.right = {-1, -1, -1, -1}; // Disable right leg.
        req.ground.sampleHeight = flatGround;
        req.leftContactWeight = 1.0f;

        std::pmr::vector<animation::LocalTransform> out;
        assert(animation::applyFootIK(skel, pose, modelPose, {0, 0, 0}, req, out));
        // Ankle is already at y=0 (ground), so no rotation change.
        requireNear("flat no-op hip rot w", out[1].rotation.w, 1.0f, 0.001f);
        requireNear("flat no-op knee rot w", out[2].rotation.w, 1.0f, 0.001f);
    }

    // ---- Slope: ankle above ground -> should correct downward ----
    {
        auto skel = makeLegSkeleton();
        auto pose = makeStandingPose();
        // Root at y=2.5, so ankle at y=-0.5. Ground at x=0 is 0.0.
        // Ankle is 0.5 below ground -> offset = +0.5 (push up). Hip at y=1.5.
        // Target = ankle + (0,0.5,0) = (0,0,0). Hip-to-target = 1.5 < arm=2.0,
        // so the knee bends.
        pose[0].translation = {0.0f, 2.5f, 0.0f};

        auto modelPose = animation::buildModelPose(skel, pose);
        SlopeCtx slope{0.0f}; // Flat at 0, ankle below ground.
        animation::FootIKRequest req;
        req.left = {1, 2, 3, 4, {0.0f, 0.0f, 1.0f}};
        req.right = {-1, -1, -1, -1};
        req.ground.sampleHeight = slopeGround;
        req.ground.ctx = &slope;
        req.leftContactWeight = 1.0f;
        req.maxFootOffsetUp = 0.5f;
        req.maxFootOffsetDown = 0.5f;

        std::pmr::vector<animation::LocalTransform> out;
        assert(animation::applyFootIK(skel, pose, modelPose, {0, 0, 0}, req, out));
        bool kneeChanged = std::fabs(out[2].rotation.w - 1.0f) > 0.01f
            || std::fabs(out[2].rotation.x) > 0.01f || std::fabs(out[2].rotation.y) > 0.01f
            || std::fabs(out[2].rotation.z) > 0.01f;
        if (!kneeChanged) {
            std::printf("slope: expected knee rotation change but got identity\n");
            std::exit(1);
        }
    }

    // ---- Contact weight=0 (swing phase): no correction ----
    {
        auto skel = makeLegSkeleton();
        auto pose = makeStandingPose();
        pose[0].translation = {0.0f, 3.1f, 0.0f}; // Ankle 0.1 above ground.

        auto modelPose = animation::buildModelPose(skel, pose);
        animation::FootIKRequest req;
        req.left = {1, 2, 3, 4, {0.0f, 0.0f, 1.0f}};
        req.right = {-1, -1, -1, -1};
        req.ground.sampleHeight = flatGround;
        req.leftContactWeight = 0.0f; // Swing phase.

        std::pmr::vector<animation::LocalTransform> out;
        assert(animation::applyFootIK(skel, pose, modelPose, {0, 0, 0}, req, out));
        requireNear("swing no-op hip w", out[1].rotation.w, 1.0f, 0.001f);
        requireNear("swing no-op knee w", out[2].rotation.w, 1.0f, 0.001f);
    }

    // ---- Null ground query -> returns false ----
    {
        auto skel = makeLegSkeleton();
        auto pose = makeStandingPose();
        auto modelPose = animation::buildModelPose(skel, pose);
        animation::FootIKRequest req;
        req.left = {1, 2, 3, 4, {0.0f, 0.0f, 1.0f}};
        req.ground.sampleHeight = nullptr; // Null.
        req.leftContactWeight = 1.0f;

        std::pmr::vector<animation::LocalTransform> out;
        assert(!animation::applyFootIK(skel, pose, modelPose, {0, 0, 0}, req, out));
    }

    // ---- Offset clamping: extreme slope, offset exceeds maxUp ----
    {
        auto skel = makeLegSkeleton();
        auto pose = makeStandingPose();
        // Ankle at y=-0.5, ground at y=10 (steep wall) -> offset would be 10.5.
        // Clamped to maxUp=0.3.
        pose[0].translation = {0.0f, 2.5f, 0.0f}; // ankle = 2.5-1-1-1 = -0.5

        auto modelPose = animation::buildModelPose(skel, pose);
        float wallHeight = 10.0f;
        animation::FootIKRequest req;
        req.left = {1, 2, 3, 4, {0.0f, 0.0f, 1.0f}};
        req.right = {-1, -1, -1, -1};
        req.ground.sampleHeight = [](void* ctx, float, float) {
            return *static_cast<float*>(ctx);
        };
        req.ground.ctx = &wallHeight;
        req.leftContactWeight = 1.0f;
        req.maxFootOffsetUp = 0.3f;
        req.maxFootOffsetDown = 0.3f;

        std::pmr::vector<animation::LocalTransform> out;
        assert(animation::applyFootIK(skel, pose, modelPose, {0, 0, 0}, req, out));
        // Correction should be applied (clamped), knee should change.
        bool kneeChanged = std::fabs(out[2].rotation.w - 1.0f) > 0.01f
            || std::fabs(out[2].rotation.x) > 0.01f || std::fabs(out[2].rotation.y) > 0.01f
            || std::fabs(out[2].rotation.z) > 0.01f;
        if (!kneeChanged) {
            std::printf("clamp: expected knee rotation change but got identity\n");
            std::exit(1);
        }
    }

    std::printf("foot_ik_tests: all passed\n");
    return 0;
}
