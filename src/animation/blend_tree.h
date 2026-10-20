#pragma once

// 1D BlendTree contract (L2 animation domain).
//
// A BlendTree1D blends multiple animation clips based on a single scalar
// parameter (e.g. Speed driving Idle/Walk/Run). It is a state-internal
// evaluation node -- the AnimatorController selects a state, and if that
// state carries a BlendTree, the tree replaces single-clip sampling.
//
// Evaluation is a pure function: given the tree, a parameter value and a
// normalised time phase, produce a local-space pose. No native resources,
// no state mutation. Fail-closed: an invalid tree returns false.

#include "animation/animation.h" // AnimationClip
#include "animation/skeleton.h"  // LocalTransform
#include "core/api.h"
#include "core/cpu_allocator.h"

#include <string>

namespace animation {

struct BlendTree1DNode {
    const AnimationClip* clip = nullptr; ///< Non-null required for validity.
    float threshold = 0.0f;              ///< Position on the parameter axis.
};

struct BlendTree1D {
    std::pmr::string parameter;              ///< Driving parameter name.
    std::pmr::vector<BlendTree1DNode> nodes; ///< >=2 nodes, ascending threshold.

    /// Valid when: >=2 nodes, all clips non-null, thresholds strictly ascending.
    bool valid() const;
};

/// Evaluate the blend tree at a given parameter value and normalised time.
///
/// `parameterValue` selects which two adjacent nodes to blend (or clamps to
/// the first/last node when out of range). `normalizedTime` in [0,1] is the
/// loop phase: each clip is sampled at normalizedTime * clip.durationSeconds,
/// so all clips stay phase-synchronised.
///
/// On success, outPose contains the blended local-space pose. Returns false
/// when the tree is invalid. No silent fallback.
ARP_API bool evaluateBlendTree1D(const BlendTree1D& tree,
    float parameterValue,
    float normalizedTime,
    std::pmr::vector<LocalTransform>& outPose);

} // namespace animation
