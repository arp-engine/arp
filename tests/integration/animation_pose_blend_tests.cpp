// Cross-fade pose blending contract tests (Phase 1).
//
// Validates blendPoses endpoint/invariant behaviour and the
// AnimationStateMachine cross-fade producing an actual blended pose (not
// just a scalar blendWeight).

#include "animation/animation.h"
#include "animation/pose_blend.h"
#include "animation/skeleton.h"
#include "core/cpu_allocator.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

bool near(float lhs, float rhs, float epsilon = 0.0001f)
{
    return std::fabs(lhs - rhs) <= epsilon;
}

void requireNear(const char* label, float lhs, float rhs, float epsilon = 0.0001f)
{
    if (!near(lhs, rhs, epsilon)) {
        std::printf("%s expected %.6f got %.6f\n", label, rhs, lhs);
        std::exit(1);
    }
}

} // namespace

int main()
{
    // ---- blendPoses endpoint identity ----
    {
        std::pmr::vector<animation::LocalTransform> a(2);
        a[0].translation = {0.0f, 0.0f, 0.0f};
        a[1].translation = {10.0f, 0.0f, 0.0f};
        std::pmr::vector<animation::LocalTransform> b(2);
        b[0].translation = {4.0f, 0.0f, 0.0f};
        b[1].translation = {20.0f, 0.0f, 0.0f};
        std::pmr::vector<animation::LocalTransform> out(2);

        // t=0 -> a
        assert(animation::blendPoses(a, b, 0.0f, out));
        requireNear("blend t=0 j0 x", out[0].translation.x, 0.0f);
        requireNear("blend t=0 j1 x", out[1].translation.x, 10.0f);

        // t=1 -> b
        assert(animation::blendPoses(a, b, 1.0f, out));
        requireNear("blend t=1 j0 x", out[0].translation.x, 4.0f);
        requireNear("blend t=1 j1 x", out[1].translation.x, 20.0f);

        // t=0.5 -> midpoint
        assert(animation::blendPoses(a, b, 0.5f, out));
        requireNear("blend t=0.5 j0 x", out[0].translation.x, 2.0f);
        requireNear("blend t=0.5 j1 x", out[1].translation.x, 15.0f);
    }

    // ---- blendPoses size mismatch returns false ----
    {
        std::pmr::vector<animation::LocalTransform> a(3);
        std::pmr::vector<animation::LocalTransform> b(2);
        std::pmr::vector<animation::LocalTransform> out(3);
        assert(!animation::blendPoses(a, b, 0.5f, out));
    }

    // ---- blendPoses rotation nlerp shortest-arc ----
    {
        std::pmr::vector<animation::LocalTransform> a(1);
        a[0].rotation = animation::Quat::identity();
        std::pmr::vector<animation::LocalTransform> b(1);
        // 90-degree rotation about Y.
        b[0].rotation = {0.0f, 0.70710678f, 0.0f, 0.70710678f};
        std::pmr::vector<animation::LocalTransform> out(1);
        assert(animation::blendPoses(a, b, 0.5f, out));
        // 45 degrees about Y: y ~= sin(22.5) ~= 0.3827, w ~= cos(22.5) ~= 0.9239
        requireNear("blend rot y", out[0].rotation.y, 0.38268343f, 0.001f);
        requireNear("blend rot w", out[0].rotation.w, 0.92387953f, 0.001f);
    }

    // ---- AnimationStateMachine cross-fade produces blended pose ----
    {
        // Clip A: root joint translates from (0,0,0) to (10,0,0) over 1s.
        animation::AnimationClip clipA;
        clipA.name = "Walk";
        clipA.durationSeconds = 1.0f;
        clipA.channels.push_back(animation::AnimationChannel{
            0,
            {
                {0.0f, {{0.0f, 0.0f, 0.0f}}},
                {1.0f, {{10.0f, 0.0f, 0.0f}}},
            },
            "translation",
            "",
        });

        // Clip B: root joint translates from (0,0,0) to (0,20,0) over 1s.
        animation::AnimationClip clipB;
        clipB.name = "Idle";
        clipB.durationSeconds = 1.0f;
        clipB.channels.push_back(animation::AnimationChannel{
            0,
            {
                {0.0f, {{0.0f, 0.0f, 0.0f}}},
                {1.0f, {{0.0f, 20.0f, 0.0f}}},
            },
            "translation",
            "",
        });

        animation::AnimationStateMachine machine;
        assert(machine.play(clipA));
        machine.advance(0.5f);
        // Walk at t=0.5 -> x=5.
        requireNear("walk sample x", machine.blendedPose()[0].translation.x, 5.0f, 0.001f);

        // Transition to Idle over 1.0s. blendWeight starts at 0.
        assert(machine.transitionTo(clipB, 1.0f));
        // Immediately after transitionTo, blendWeight=0 -> blendedPose = Walk @0.5
        requireNear("transition init x", machine.blendedPose()[0].translation.x, 5.0f, 0.001f);

        // Advance 0.5s: blendWeight=0.5, Walk time=1.0 (clamped), Idle time=0.5.
        // Walk @1.0 -> x=10. Idle @0.5 -> y=10.
        // blend(0.5): x = lerp(10, 0, 0.5) = 5, y = lerp(0, 10, 0.5) = 5.
        machine.advance(0.5f);
        requireNear("crossfade mid x", machine.blendedPose()[0].translation.x, 5.0f, 0.001f);
        requireNear("crossfade mid y", machine.blendedPose()[0].translation.y, 5.0f, 0.001f);

        // Advance another 0.5s: blendWeight=1.0, transition done.
        // Idle @1.0 -> y=20 (clip goes 0->20 over 1s).
        machine.advance(0.5f);
        requireNear("crossfade end y", machine.blendedPose()[0].translation.y, 20.0f, 0.001f);
        requireNear("crossfade end x", machine.blendedPose()[0].translation.x, 0.0f, 0.001f);
    }

    // ---- blendPosesWeighted: two poses 50/50 ----
    {
        std::pmr::vector<animation::LocalTransform> a(1);
        a[0].translation = {10.0f, 0.0f, 0.0f};
        std::pmr::vector<animation::LocalTransform> b(1);
        b[0].translation = {0.0f, 0.0f, 0.0f};
        std::pmr::vector<const std::pmr::vector<animation::LocalTransform>*> poses{&a, &b};
        std::pmr::vector<float> weights{1.0f, 1.0f};
        std::pmr::vector<animation::LocalTransform> out(1);
        assert(animation::blendPosesWeighted(poses, weights, out));
        requireNear("weighted 50/50 x", out[0].translation.x, 5.0f, 0.001f);
    }

    // ---- blendPosesWeighted: all-zero weights leaves out unchanged ----
    {
        std::pmr::vector<animation::LocalTransform> a(1);
        a[0].translation = {7.0f, 0.0f, 0.0f};
        std::pmr::vector<animation::LocalTransform> b(1);
        b[0].translation = {3.0f, 0.0f, 0.0f};
        std::pmr::vector<const std::pmr::vector<animation::LocalTransform>*> poses{&a, &b};
        std::pmr::vector<float> weights{0.0f, 0.0f};
        std::pmr::vector<animation::LocalTransform> out(1);
        out[0].translation = {99.0f, 0.0f, 0.0f};
        assert(animation::blendPosesWeighted(poses, weights, out));
        requireNear("weighted zero unchanged x", out[0].translation.x, 99.0f, 0.001f);
    }

    // ---- blendPosesWeighted: size mismatch returns false ----
    {
        std::pmr::vector<animation::LocalTransform> a(2);
        std::pmr::vector<animation::LocalTransform> b(1);
        std::pmr::vector<const std::pmr::vector<animation::LocalTransform>*> poses{&a, &b};
        std::pmr::vector<float> weights{1.0f, 1.0f};
        std::pmr::vector<animation::LocalTransform> out(2);
        assert(!animation::blendPosesWeighted(poses, weights, out));
    }

    std::printf("animation_pose_blend_tests: all passed\n");
    return 0;
}
