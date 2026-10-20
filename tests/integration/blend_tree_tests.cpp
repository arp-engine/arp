// 1D BlendTree contract tests (Phase 2).
//
// Validates BlendTree1D validity rules and evaluation: threshold clamping,
// phase-synchronised sampling, and linear blend between bracketing nodes.

#include "animation/animation.h"
#include "animation/blend_tree.h"
#include "animation/skeleton.h"
#include "core/cpu_allocator.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

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

// Build a clip where joint 0 translates along an axis from 0 to `end` over 1s.
animation::AnimationClip makeTranslateClip(const char* name, float endX, float endY, float endZ)
{
    animation::AnimationClip clip;
    clip.name = name;
    clip.durationSeconds = 1.0f;
    clip.channels.push_back(animation::AnimationChannel{
        0,
        {
            {0.0f, {{0.0f, 0.0f, 0.0f}}},
            {1.0f, {{endX, endY, endZ}}},
        },
        "translation",
        "",
    });
    return clip;
}

} // namespace

int main()
{
    // ---- valid(): insufficient nodes ----
    {
        animation::BlendTree1D tree;
        animation::BlendTree1DNode node;
        animation::AnimationClip dummy = makeTranslateClip("A", 1, 0, 0);
        node.clip = &dummy;
        node.threshold = 0.0f;
        tree.nodes.push_back(node);
        assert(!tree.valid());
    }

    // ---- valid(): null clip ----
    {
        animation::BlendTree1D tree;
        animation::AnimationClip a = makeTranslateClip("A", 1, 0, 0);
        animation::BlendTree1DNode n0{&a, 0.0f};
        animation::BlendTree1DNode n1{nullptr, 2.0f};
        tree.nodes.push_back(n0);
        tree.nodes.push_back(n1);
        assert(!tree.valid());
    }

    // ---- valid(): non-ascending thresholds ----
    {
        animation::BlendTree1D tree;
        animation::AnimationClip a = makeTranslateClip("A", 1, 0, 0);
        animation::AnimationClip b = makeTranslateClip("B", 0, 1, 0);
        tree.nodes.push_back({&a, 2.0f});
        tree.nodes.push_back({&b, 0.0f});
        assert(!tree.valid());
    }

    // ---- valid(): correct tree ----
    {
        animation::BlendTree1D tree;
        animation::AnimationClip a = makeTranslateClip("A", 1, 0, 0);
        animation::AnimationClip b = makeTranslateClip("B", 0, 1, 0);
        tree.nodes.push_back({&a, 0.0f});
        tree.nodes.push_back({&b, 2.0f});
        assert(tree.valid());
    }

    // ---- evaluate: 3-node tree, value at midpoint ----
    {
        animation::AnimationClip idle = makeTranslateClip("Idle", 0, 0, 0);
        animation::AnimationClip walk = makeTranslateClip("Walk", 2, 0, 0);
        animation::AnimationClip run = makeTranslateClip("Run", 6, 0, 0);
        animation::BlendTree1D tree;
        tree.parameter = "Speed";
        tree.nodes.push_back({&idle, 0.0f});
        tree.nodes.push_back({&walk, 2.0f});
        tree.nodes.push_back({&run, 4.0f});

        // value=1.0 -> bracket [0,2], localT=0.5.
        // At normalizedTime=1.0: idle@1.0 = (0,0,0), walk@1.0 = (2,0,0).
        // blend(0.5) -> (1,0,0).
        std::pmr::vector<animation::LocalTransform> out;
        assert(animation::evaluateBlendTree1D(tree, 1.0f, 1.0f, out));
        requireNear("blendtree mid x", out[0].translation.x, 1.0f, 0.001f);

        // value=3.0 -> bracket [2,4], localT=0.5.
        // walk@1.0=(2,0,0), run@1.0=(6,0,0). blend(0.5)->(4,0,0).
        assert(animation::evaluateBlendTree1D(tree, 3.0f, 1.0f, out));
        requireNear("blendtree mid2 x", out[0].translation.x, 4.0f, 0.001f);
    }

    // ---- evaluate: clamp below first threshold ----
    {
        animation::AnimationClip idle = makeTranslateClip("Idle", 0, 0, 0);
        animation::AnimationClip walk = makeTranslateClip("Walk", 2, 0, 0);
        animation::BlendTree1D tree;
        tree.nodes.push_back({&idle, 0.0f});
        tree.nodes.push_back({&walk, 2.0f});

        std::pmr::vector<animation::LocalTransform> out;
        // value=-5 (below 0) -> first node (idle). idle@0.5 -> (0,0,0).
        assert(animation::evaluateBlendTree1D(tree, -5.0f, 0.5f, out));
        requireNear("blendtree clamp-low x", out[0].translation.x, 0.0f, 0.001f);
    }

    // ---- evaluate: clamp above last threshold ----
    {
        animation::AnimationClip idle = makeTranslateClip("Idle", 0, 0, 0);
        animation::AnimationClip run = makeTranslateClip("Run", 6, 0, 0);
        animation::BlendTree1D tree;
        tree.nodes.push_back({&idle, 0.0f});
        tree.nodes.push_back({&run, 4.0f});

        std::pmr::vector<animation::LocalTransform> out;
        // value=100 (above 4) -> last node (run). run@1.0 -> (6,0,0).
        assert(animation::evaluateBlendTree1D(tree, 100.0f, 1.0f, out));
        requireNear("blendtree clamp-high x", out[0].translation.x, 6.0f, 0.001f);
    }

    // ---- evaluate: invalid tree returns false ----
    {
        animation::BlendTree1D tree; // empty
        std::pmr::vector<animation::LocalTransform> out;
        assert(!animation::evaluateBlendTree1D(tree, 0.0f, 0.0f, out));
    }

    // ---- evaluate: normalizedTime phase sync ----
    {
        animation::AnimationClip idle = makeTranslateClip("Idle", 0, 0, 0);
        animation::AnimationClip walk = makeTranslateClip("Walk", 10, 0, 0);
        animation::BlendTree1D tree;
        tree.nodes.push_back({&idle, 0.0f});
        tree.nodes.push_back({&walk, 2.0f});

        std::pmr::vector<animation::LocalTransform> out;
        // value=1.0 (localT=0.5), normalizedTime=0.0.
        // idle@0 = (0,0,0), walk@0 = (0,0,0). blend -> (0,0,0).
        assert(animation::evaluateBlendTree1D(tree, 1.0f, 0.0f, out));
        requireNear("blendtree phase0 x", out[0].translation.x, 0.0f, 0.001f);

        // normalizedTime=0.5: idle@0.5=(0,0,0), walk@0.5=(5,0,0). blend(0.5)->(2.5,0,0).
        assert(animation::evaluateBlendTree1D(tree, 1.0f, 0.5f, out));
        requireNear("blendtree phase0.5 x", out[0].translation.x, 2.5f, 0.001f);
    }

    std::printf("blend_tree_tests: all passed\n");
    return 0;
}
