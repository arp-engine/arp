#include "animation/skinned_mesh_player.h"

#include "animation/foot_ik.h"
#include "core/cpu_allocator.h"
#include "core/math.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace animation {

namespace {

/// animation::Quat and arp::core::Quat share the same {x,y,z,w} layout.
/// Reinterpret-cast helpers so animation code can call arp::core quaternion
/// functions without copying memory.
inline const arp::core::Quat& asCore(const Quat& q)
{
    return reinterpret_cast<const arp::core::Quat&>(q);
}

inline arp::core::Quat& asCore(Quat& q)
{
    return reinterpret_cast<arp::core::Quat&>(q);
}

inline Quat asAnim(const arp::core::Quat& q)
{
    Quat r;
    r.x = q.x;
    r.y = q.y;
    r.z = q.z;
    r.w = q.w;
    return r;
}

} // namespace

SkinnedMeshPlayer::SkinnedMeshPlayer(const arp::assets::AnimatedMeshAsset& asset) : asset_(asset)
{
    const auto jointCount = asset_.skeleton.jointCount();
    valid_ = asset_.mesh.validForIndexedDraw()
        && asset_.skinnedVertices.size() == asset_.mesh.vertices.size() && asset_.skeleton.valid()
        && asset_.bindPose.size() == jointCount && !asset_.clips.empty();
    if (!valid_) {
        return;
    }

    sampledPose_.resize(jointCount);
    localPose_.assign(asset_.bindPose.begin(), asset_.bindPose.end());
    modelPose_.resize(jointCount);
    skinMatrices_.resize(jointCount);
    footIkScratch_.resize(jointCount);
}

bool SkinnedMeshPlayer::valid() const
{
    return valid_;
}

bool SkinnedMeshPlayer::update(const std::pmr::string& clipName, const float timeSeconds)
{
    const auto* clip = findClip(clipName);
    if (clip == nullptr || !valid_) {
        return false;
    }

    const auto jointCount = asset_.skeleton.jointCount();
    if (sampledPose_.size() != jointCount || localPose_.size() != jointCount) {
        return false;
    }

    for (std::size_t index = 0; index < jointCount; ++index) {
        localPose_[index] = asset_.bindPose[index];
        sampledPose_[index].translation = localPose_[index].translation;
        sampledPose_[index].rotation = localPose_[index].rotation;
        sampledPose_[index].scale = localPose_[index].scale;
    }

    if (!sampleClip(*clip, timeSeconds, sampledPose_)) {
        return false;
    }
    for (std::size_t index = 0; index < jointCount; ++index) {
        localPose_[index].translation = sampledPose_[index].translation;
        localPose_[index].rotation = sampledPose_[index].rotation;
        localPose_[index].scale = sampledPose_[index].scale;
    }

    return finalizeFromLocalPose();
}

bool SkinnedMeshPlayer::updateLooping(const std::pmr::string& clipName, const float timeSeconds)
{
    const auto* clip = findClip(clipName);
    if (clip == nullptr || !valid_ || clip->durationSeconds <= 0.0f
        || !std::isfinite(timeSeconds)) {
        return false;
    }

    auto wrappedTime = std::fmod(timeSeconds, clip->durationSeconds);
    if (wrappedTime < 0.0f)
        wrappedTime += clip->durationSeconds;
    return update(clipName, wrappedTime);
}

bool SkinnedMeshPlayer::update(const std::pmr::vector<LocalTransform>& localPose)
{
    if (!valid_) {
        return false;
    }
    const auto jointCount = asset_.skeleton.jointCount();
    if (localPose.size() != jointCount || localPose_.size() != jointCount) {
        return false;
    }
    for (std::size_t index = 0; index < jointCount; ++index) {
        localPose_[index] = localPose[index];
    }
    return finalizeFromLocalPose();
}

bool SkinnedMeshPlayer::finalizeFromLocalPose()
{
    const auto jointCount = asset_.skeleton.jointCount();

    // ---- Additive swing/pose overrides ----
    // Layer each override's local rotation on top of the sampled clip pose.
    // Because modelPose_ is built hierarchically from localPose_, rotating a
    // parent joint (e.g. the upper arm) also swings all of its descendants
    // (forearm, hand, hand-attached weapon) for free.
    for (std::size_t oi = 0; oi < overrideJointIndices_.size(); ++oi) {
        const int ji = overrideJointIndices_[oi];
        if (ji >= 0 && ji < static_cast<int>(jointCount)) {
            localPose_[ji].rotation = asAnim(arp::core::quatNormalize(arp::core::quatMul(
                asCore(localPose_[ji].rotation), asCore(overrideRotations_[oi]))));
        }
    }

    // Build the initial model-space pose. This is the base for foot IK: the
    // solver reads ankle/hip/knee world positions from this pose.
    modelPose_ = buildModelPose(asset_.skeleton, localPose_);
    if (modelPose_.size() != jointCount) {
        return false;
    }

    // ---- Foot IK pass (optional) ----
    // applyFootIK reads modelPose_ (ankle positions) and writes a corrected
    // local-space pose into footIkScratch_. We then copy it back and rebuild
    // modelPose_/skinMatrices_ so the skin uses the IK-corrected joints.
    // The scratch buffer is sized once in the constructor and reused per
    // frame (no per-frame allocation -- AGENTS.md hot-path invariant).
    if (footIkEnabled_ && footIkRequest_.ground.sampleHeight != nullptr) {
        if (footIkScratch_.size() != jointCount) {
            footIkScratch_.resize(jointCount);
        }
        if (applyFootIK(asset_.skeleton,
                localPose_,
                modelPose_,
                rootWorldPos_,
                footIkRequest_,
                footIkScratch_)) {
            for (std::size_t i = 0; i < jointCount; ++i) {
                localPose_[i] = footIkScratch_[i];
            }
            // Rebuild model pose with IK-corrected local pose.
            modelPose_ = buildModelPose(asset_.skeleton, localPose_);
            if (modelPose_.size() != jointCount) {
                return false;
            }
        }
    }

    skinMatrices_ = buildSkinMatrices(asset_.skeleton, modelPose_);
    if (skinMatrices_.size() != jointCount) {
        return false;
    }

    // GPU-skinning-only path: skinMatrices_ is the output the renderer packs
    // into a GPU palette. No CPU vertex baking -- the immutable bind-pose
    // vertex stream is consumed by the vertex shader alongside the palette,
    // so no per-frame mesh mutation crosses the logic/render thread boundary.
    return true;
}

const arp::assets::AnimatedMeshAsset& SkinnedMeshPlayer::asset() const
{
    return asset_;
}

const AnimationClip* SkinnedMeshPlayer::findClip(const std::pmr::string& clipName) const
{
    if (asset_.clips.empty()) {
        return nullptr;
    }
    if (clipName.empty()) {
        return &asset_.clips.front();
    }
    const auto found =
        std::find_if(asset_.clips.begin(), asset_.clips.end(), [&](const AnimationClip& clip) {
            return clip.name == clipName;
        });
    return found == asset_.clips.end() ? nullptr : &*found;
}

void SkinnedMeshPlayer::setJointRotationAdditive(
    int jointIndex, float axisX, float axisY, float axisZ, float angleRadians)
{
    if (jointIndex < 0)
        return;
    const Quat extra = asAnim(arp::core::quatFromAxisAngle(axisX, axisY, axisZ, angleRadians));
    for (std::size_t i = 0; i < overrideJointIndices_.size(); ++i) {
        if (overrideJointIndices_[i] == jointIndex) {
            overrideRotations_[i] = extra;
            return;
        }
    }
    overrideJointIndices_.push_back(jointIndex);
    overrideRotations_.push_back(extra);
}

void SkinnedMeshPlayer::clearJointRotationAdditives()
{
    overrideJointIndices_.clear();
    overrideRotations_.clear();
}

void SkinnedMeshPlayer::setFootIK(const FootIKLegInput& left,
    const FootIKLegInput& right,
    const FootIKGroundQuery& ground,
    float maxFootOffsetUp,
    float maxFootOffsetDown,
    arp::core::Vec3 worldUp)
{
    footIkRequest_.left = left;
    footIkRequest_.right = right;
    footIkRequest_.ground = ground;
    footIkRequest_.maxFootOffsetUp = maxFootOffsetUp;
    footIkRequest_.maxFootOffsetDown = maxFootOffsetDown;
    footIkRequest_.worldUp = worldUp;
    // Contact weights are per-frame; preserve any previously set value so the
    // caller can set weights before or after enabling IK.
    footIkEnabled_ = (ground.sampleHeight != nullptr);
}

void SkinnedMeshPlayer::setFootContactWeights(float leftWeight, float rightWeight)
{
    footIkRequest_.leftContactWeight = leftWeight;
    footIkRequest_.rightContactWeight = rightWeight;
}

void SkinnedMeshPlayer::setRootWorldPos(arp::core::Vec3 pos)
{
    rootWorldPos_ = pos;
}

void SkinnedMeshPlayer::clearFootIK()
{
    footIkEnabled_ = false;
    footIkRequest_.ground.sampleHeight = nullptr;
    footIkRequest_.ground.ctx = nullptr;
}

} // namespace animation
