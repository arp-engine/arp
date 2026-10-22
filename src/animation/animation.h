#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"

#include <string>
#include <vector>

namespace animation {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct ARP_API Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    static Quat identity();
};

struct JointTransform {
    Vec3 translation{};
    Quat rotation = Quat::identity();
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

/// Local-space joint transform. Defined here (not in skeleton.h) to break the
/// circular include: skeleton.h includes animation.h for Vec3/Quat, and
/// animation.h needs LocalTransform for pose blending. Keeping LocalTransform
/// in animation.h (which defines its dependencies Vec3/Quat) breaks the cycle.
struct LocalTransform {
    Vec3 translation{};
    Quat rotation = Quat::identity();
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

struct JointKeyframe {
    float timeSeconds = 0.0f;
    JointTransform transform{};
};

struct AnimationChannel {
    unsigned int jointIndex = 0;
    std::pmr::vector<JointKeyframe> keyframes;
    std::pmr::string targetPath = "translation";
    std::pmr::string jointName;
};

struct AnimationEvent {
    std::pmr::string name;
    float timeSeconds = 0.0f;
};

struct AnimationClip {
    std::pmr::string name;
    float durationSeconds = 0.0f;
    std::pmr::vector<AnimationChannel> channels;
    std::pmr::vector<AnimationEvent> events;
};

ARP_API bool sampleClip(
    const AnimationClip& clip, float timeSeconds, std::pmr::vector<JointTransform>& outPose);

ARP_API void collectEvents(const AnimationClip& clip,
    float previousTimeSeconds,
    float currentTimeSeconds,
    bool looping,
    std::pmr::vector<AnimationEvent>& outEvents);

class ARP_API AnimationStateMachine {
public:
    bool play(const AnimationClip& clip);
    bool transitionTo(const AnimationClip& clip, float durationSeconds);
    void advance(float deltaSeconds);

    const std::pmr::string& currentStateName() const;
    float currentTimeSeconds() const;
    float blendWeight() const;

    /// Returns the blended local-space pose from the most recent advance().
    /// During a cross-fade transition this is the nlerp of the previous and
    /// current clips sampled at their respective times, weighted by
    /// blendWeight(). When no transition is active it equals the current
    /// clip's sampled pose. Empty until the first advance() after play().
    const std::pmr::vector<LocalTransform>& blendedPose() const { return blendedPose_; }

private:
    const AnimationClip* current_ = nullptr;
    const AnimationClip* previous_ = nullptr;
    float currentTimeSeconds_ = 0.0f;
    float previousTimeSeconds_ = 0.0f;
    float transitionDurationSeconds_ = 0.0f;
    float transitionElapsedSeconds_ = 0.0f;
    float blendWeight_ = 1.0f;

    std::pmr::vector<JointTransform> currentSampled_;
    std::pmr::vector<JointTransform> previousSampled_;
    std::pmr::vector<LocalTransform> currentPose_;
    std::pmr::vector<LocalTransform> previousPose_;
    std::pmr::vector<LocalTransform> blendedPose_;

    void ensurePoseCapacity(std::size_t jointCount);
    void sampleCurrentIntoPoses();
};

} // namespace animation
