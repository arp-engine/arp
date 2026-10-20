#include "animation/blend_tree.h"

#include "animation/pose_blend.h"
#include "core/cpu_allocator.h"

#include <algorithm>
#include <cmath>

namespace animation {

// Forward-declared from animation.cpp (file-local helper shared across the
// animation TU). Returns the joint count a clip targets (max jointIndex+1).
std::size_t jointCountOf(const AnimationClip& clip);

bool BlendTree1D::valid() const
{
    if (nodes.size() < 2) {
        return false;
    }
    for (const auto& node : nodes) {
        if (node.clip == nullptr) {
            return false;
        }
    }
    for (std::size_t i = 1; i < nodes.size(); ++i) {
        if (nodes[i].threshold <= nodes[i - 1].threshold) {
            return false;
        }
    }
    return true;
}

bool evaluateBlendTree1D(const BlendTree1D& tree,
    const float parameterValue,
    const float normalizedTime,
    std::pmr::vector<LocalTransform>& outPose)
{
    if (!tree.valid()) {
        return false;
    }

    const auto clampPhase = std::max(0.0f, std::min(1.0f, normalizedTime));

    // Clamp to first node when below the lowest threshold.
    if (parameterValue <= tree.nodes.front().threshold) {
        std::pmr::vector<JointTransform> sampled;
        const auto jointCount = jointCountOf(*tree.nodes.front().clip);
        if (jointCount == 0) {
            return false;
        }
        sampled.resize(jointCount);
        if (!sampleClip(*tree.nodes.front().clip,
                clampPhase * tree.nodes.front().clip->durationSeconds,
                sampled)) {
            return false;
        }
        if (outPose.size() != jointCount) {
            outPose.resize(jointCount);
        }
        for (std::size_t i = 0; i < jointCount; ++i) {
            outPose[i].translation = sampled[i].translation;
            outPose[i].rotation = sampled[i].rotation;
            outPose[i].scale = sampled[i].scale;
        }
        return true;
    }

    // Clamp to last node when above the highest threshold.
    if (parameterValue >= tree.nodes.back().threshold) {
        std::pmr::vector<JointTransform> sampled;
        const auto jointCount = jointCountOf(*tree.nodes.back().clip);
        if (jointCount == 0) {
            return false;
        }
        sampled.resize(jointCount);
        if (!sampleClip(*tree.nodes.back().clip,
                clampPhase * tree.nodes.back().clip->durationSeconds,
                sampled)) {
            return false;
        }
        if (outPose.size() != jointCount) {
            outPose.resize(jointCount);
        }
        for (std::size_t i = 0; i < jointCount; ++i) {
            outPose[i].translation = sampled[i].translation;
            outPose[i].rotation = sampled[i].rotation;
            outPose[i].scale = sampled[i].scale;
        }
        return true;
    }

    // Find the bracketing pair [i-1, i].
    std::size_t hi = 1;
    while (hi < tree.nodes.size() && tree.nodes[hi].threshold <= parameterValue) {
        ++hi;
    }
    const auto lo = hi - 1;
    const float t0 = tree.nodes[lo].threshold;
    const float t1 = tree.nodes[hi].threshold;
    const float range = t1 - t0;
    const float localT = range > 0.0f ? (parameterValue - t0) / range : 0.0f;

    // Sample both clips at phase-synchronised times.
    const auto jointCount = jointCountOf(*tree.nodes[lo].clip);
    if (jointCount == 0 || jointCountOf(*tree.nodes[hi].clip) != jointCount) {
        return false;
    }

    std::pmr::vector<JointTransform> sampledLo;
    std::pmr::vector<JointTransform> sampledHi;
    sampledLo.resize(jointCount);
    sampledHi.resize(jointCount);

    if (!sampleClip(
            *tree.nodes[lo].clip, clampPhase * tree.nodes[lo].clip->durationSeconds, sampledLo)) {
        return false;
    }
    if (!sampleClip(
            *tree.nodes[hi].clip, clampPhase * tree.nodes[hi].clip->durationSeconds, sampledHi)) {
        return false;
    }

    std::pmr::vector<LocalTransform> poseLo;
    std::pmr::vector<LocalTransform> poseHi;
    poseLo.resize(jointCount);
    poseHi.resize(jointCount);
    for (std::size_t i = 0; i < jointCount; ++i) {
        poseLo[i].translation = sampledLo[i].translation;
        poseLo[i].rotation = sampledLo[i].rotation;
        poseLo[i].scale = sampledLo[i].scale;
        poseHi[i].translation = sampledHi[i].translation;
        poseHi[i].rotation = sampledHi[i].rotation;
        poseHi[i].scale = sampledHi[i].scale;
    }

    return blendPoses(poseLo, poseHi, localT, outPose);
}

} // namespace animation
