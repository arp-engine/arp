#pragma once

#include "animation/foot_ik.h"
#include "animation/skeleton.h"
#include "assets/glb_animated_mesh_importer.h"
#include "core/api.h"
#include "core/math/math.h"
#include "core/memory/cpu_allocator.h"

#include <string>
#include <vector>

namespace animation {

/// Skinned mesh player. GPU-skinning-only: evaluates clip pose, applies additive
/// overrides and foot IK, then exposes skinMatrices() / modelPose() for the
/// renderer to pack into a GPU skinning palette. The CPU vertex-baking path
/// (frameMesh / skinVerticesCpu) was removed in Phase 3 -- production rendering
/// always consumes the immutable bind-pose vertex stream plus the palette, so
/// no per-frame mesh mutation crosses the logic/render thread boundary.
class ARP_API SkinnedMeshPlayer {
public:
    explicit SkinnedMeshPlayer(const arp::assets::AnimatedMeshAsset& asset);

    bool valid() const;
    bool update(const std::pmr::string& clipName, float timeSeconds);
    bool updateLooping(const std::pmr::string& clipName, float timeSeconds);

    /// Evaluate a frame from an externally supplied local-space pose (e.g.
    /// from AnimationStateMachine::blendedPose() or a BlendTree). The pose
    /// must have exactly jointCount() entries. This bypasses clip sampling
    /// entirely -- the caller owns blending. Additive rotation overrides
    /// (setJointRotationAdditive) are still applied.
    bool update(const std::pmr::vector<LocalTransform>& localPose);

    const arp::assets::AnimatedMeshAsset& asset() const;

    /// Skin matrices from the latest evaluated pose. Matrices use the
    /// animation layer's column-major Mat4 storage and are exposed read-only;
    /// the renderer transposes them at the GPU-palette upload boundary.
    const std::pmr::vector<Mat4>& skinMatrices() const { return skinMatrices_; }

    std::size_t jointCount() const { return asset_.skeleton.jointCount(); }

    /// Returns the model-space bone transforms from the most recent update().
    /// modelPose[i] is the 4x4 column-major matrix that transforms from joint
    /// local space to model space.  Useful for attaching props (weapons, etc.)
    /// to animated bones - multiply the prop's model-space offset by the bone
    /// matrix to get the prop's world-frame placement.
    const std::pmr::vector<Mat4>& modelPose() const { return modelPose_; }

    /// Apply an ADDITIVE local-space rotation to a joint for subsequent
    /// update() calls. The extra rotation is composed (post-multiplied) onto
    /// the joint's sampled clip rotation, so it layers a pose - e.g. a swing,
    /// lunge, or lean - on top of the base animation without replacing it.
    ///
    /// Pass axisX/Y/Z (need not be normalized) and angleRadians. Re-calling
    /// with the same jointIndex overwrites the previous additive; calling
    /// clearJointRotationAdditives() removes all of them.
    void setJointRotationAdditive(
        int jointIndex, float axisX, float axisY, float axisZ, float angleRadians);

    /// Remove every additive joint rotation. Call when the override should
    /// stop (e.g. the attack swing finished).
    void clearJointRotationAdditives();

    // ── Foot IK (static terrain) ──────────────────────────────────────
    // Configures two-bone + look-at foot IK so planted feet conform to the
    // ground. Ground height is injected via a function pointer so the
    // animation layer never depends on pcg. Per-frame contact weights and
    // the character's world root position must be updated before update().

    /// Enable foot IK with the given leg joint indices and ground query.
    /// Passing a null ground query disables IK. The leg input's
    /// footForwardLocal is the foot's rest forward direction in joint-local
    /// space (commonly +Z).
    void setFootIK(const FootIKLegInput& left,
        const FootIKLegInput& right,
        const FootIKGroundQuery& ground,
        float maxFootOffsetUp = 0.3f,
        float maxFootOffsetDown = 0.3f,
        arp::core::Vec3 worldUp = {0.0f, 1.0f, 0.0f});

    /// Per-frame contact weights: 1.0 = foot planted (IK active),
    /// 0.0 = foot in swing (IK off). Both default to 0.
    void setFootContactWeights(float leftWeight, float rightWeight);

    /// Per-frame character world-space root position, used by foot IK to
    /// offset model-space ankle positions into world space for ground queries.
    void setRootWorldPos(arp::core::Vec3 pos);

    /// Disable foot IK (equivalent to setFootIK with a null ground query).
    void clearFootIK();

private:
    const arp::assets::AnimatedMeshAsset& asset_;
    bool valid_ = false;
    std::pmr::vector<JointTransform> sampledPose_;
    std::pmr::vector<LocalTransform> localPose_;
    std::pmr::vector<Mat4> modelPose_;
    std::pmr::vector<Mat4> skinMatrices_;

    /// Additive swing overrides: parallel arrays of joint index + extra
    /// local-space rotation, applied after the clip is sampled.
    std::pmr::vector<int> overrideJointIndices_;
    std::pmr::vector<Quat> overrideRotations_;

    /// Foot IK configuration. footIkEnabled_ is false when no ground query
    /// is set (default), so finalizeFromLocalPose() skips the IK pass.
    bool footIkEnabled_ = false;
    FootIKRequest footIkRequest_;
    arp::core::Vec3 rootWorldPos_{0.0f, 0.0f, 0.0f};
    /// Scratch buffer for applyFootIK output (reused per frame to avoid
    /// per-frame allocation -- hot-path invariant, AGENTS.md §6).
    std::pmr::vector<LocalTransform> footIkScratch_;

    const AnimationClip* findClip(const std::pmr::string& clipName) const;

    /// Build model-space pose from localPose_ and compute skin matrices.
    /// Shared by both update() overloads. If foot IK is enabled, a first
    /// model-pose pass feeds applyFootIK, then localPose_/modelPose_/
    /// skinMatrices_ are rebuilt with the IK correction.
    bool finalizeFromLocalPose();
};

} // namespace animation
