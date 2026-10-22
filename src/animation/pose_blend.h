#pragma once

// Pose blending contract (L2 animation domain).
//
// These functions blend per-joint local-space poses (LocalTransform arrays)
// for cross-fade transitions and BlendTree evaluation. They are pure
// functions operating on skeleton-local TRS data; they never touch native
// resources, clips, or skinning.
//
// Memory: output vectors use core::pmr aliases (AGENTS.md CPU allocator
// rule). Failure is fail-closed: mismatched pose sizes return false rather
// than silently truncating.

#include "animation/skeleton.h" // LocalTransform
#include "core/api.h"
#include "core/memory/cpu_allocator.h"

namespace animation {

/// Blend two same-sized poses by interpolating each joint's TRS.
/// t is clamped to [0,1]: t=0 yields a, t=1 yields b.
/// Translation and scale use linear interpolation; rotation uses nlerp
/// (shortest-arc). Returns false (and leaves out unchanged) when a and b
/// differ in size -- no silent truncation.
ARP_API bool blendPoses(const std::pmr::vector<LocalTransform>& a,
    const std::pmr::vector<LocalTransform>& b,
    float t,
    std::pmr::vector<LocalTransform>& out);

/// Weighted blend of N poses. weights need not sum to 1; they are
/// normalised internally. An all-zero weight set leaves out unchanged and
/// returns true (caller's bind pose stays). Returns false when poses and
/// weights differ in count, or any pose has a different joint count than
/// the first.
ARP_API bool blendPosesWeighted(
    const std::pmr::vector<const std::pmr::vector<LocalTransform>*>& poses,
    const std::pmr::vector<float>& weights,
    std::pmr::vector<LocalTransform>& out);

} // namespace animation
