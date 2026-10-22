#include "animation/animation.h"

#include "animation/pose_blend.h"
#include "core/memory/cpu_allocator.h"

#include <algorithm>
#include <cmath>

namespace animation {

namespace {

float clampTime(const AnimationClip& clip, const float timeSeconds)
{
    if (clip.durationSeconds <= 0.0f) {
        return 0.0f;
    }
    return std::max(0.0f, std::min(timeSeconds, clip.durationSeconds));
}

float normalizedBetween(const float begin, const float end, const float value)
{
    const auto duration = end - begin;
    if (duration <= 0.0f) {
        return 0.0f;
    }
    return std::max(0.0f, std::min(1.0f, (value - begin) / duration));
}

Vec3 lerp(const Vec3 a, const Vec3 b, const float t)
{
    return Vec3{
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
    };
}

Quat normalize(const Quat value)
{
    const auto length =
        std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w);
    if (length <= 0.000001f) {
        return Quat::identity();
    }
    return Quat{
        value.x / length,
        value.y / length,
        value.z / length,
        value.w / length,
    };
}

Quat nlerp(Quat a, const Quat b, const float t)
{
    const auto dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0.0f) {
        a.x = -a.x;
        a.y = -a.y;
        a.z = -a.z;
        a.w = -a.w;
    }
    return normalize(Quat{
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t,
    });
}

JointTransform interpolateTransform(const JointTransform& previous,
    const JointTransform& next,
    const std::pmr::string& targetPath,
    const float t)
{
    auto result = previous;
    if (targetPath == "rotation") {
        result.rotation = nlerp(previous.rotation, next.rotation, t);
    } else if (targetPath == "scale") {
        result.scale = lerp(previous.scale, next.scale, t);
    } else {
        result.translation = lerp(previous.translation, next.translation, t);
    }
    return result;
}

JointTransform sampleChannel(const AnimationChannel& channel, const float timeSeconds)
{
    if (channel.keyframes.empty()) {
        return JointTransform{};
    }
    if (timeSeconds <= channel.keyframes.front().timeSeconds) {
        return channel.keyframes.front().transform;
    }
    if (timeSeconds >= channel.keyframes.back().timeSeconds) {
        return channel.keyframes.back().transform;
    }

    for (std::size_t i = 1; i < channel.keyframes.size(); ++i) {
        const auto& previous = channel.keyframes[i - 1];
        const auto& next = channel.keyframes[i];
        if (timeSeconds <= next.timeSeconds) {
            const auto t = normalizedBetween(previous.timeSeconds, next.timeSeconds, timeSeconds);
            return interpolateTransform(previous.transform, next.transform, channel.targetPath, t);
        }
    }
    return channel.keyframes.back().transform;
}

} // namespace

bool sampleClip(
    const AnimationClip& clip, const float timeSeconds, std::pmr::vector<JointTransform>& outPose)
{
    const auto sampledTime = clampTime(clip, timeSeconds);
    for (const auto& channel : clip.channels) {
        if (!channel.targetPath.empty() && channel.targetPath != "translation"
            && channel.targetPath != "rotation" && channel.targetPath != "scale") {
            continue;
        }
        if (channel.jointIndex >= outPose.size()) {
            return false;
        }
        const auto sampled = sampleChannel(channel, sampledTime);
        if (channel.targetPath == "rotation") {
            outPose[channel.jointIndex].rotation = sampled.rotation;
        } else if (channel.targetPath == "scale") {
            outPose[channel.jointIndex].scale = sampled.scale;
        } else {
            outPose[channel.jointIndex].translation = sampled.translation;
        }
    }
    return true;
}

/// Determine the joint count a clip targets (max jointIndex + 1), so the
/// state machine can size its sampled/pose buffers. Returns 0 for a clip
/// with no channels.
std::size_t jointCountOf(const AnimationClip& clip)
{
    unsigned int maxIndex = 0;
    bool any = false;
    for (const auto& channel : clip.channels) {
        if (channel.jointIndex > maxIndex || !any) {
            maxIndex = channel.jointIndex;
            any = true;
        }
    }
    return any ? static_cast<std::size_t>(maxIndex + 1) : 0;
}

void AnimationStateMachine::ensurePoseCapacity(const std::size_t jointCount)
{
    if (jointCount == 0) {
        return;
    }
    if (currentSampled_.size() < jointCount) {
        currentSampled_.resize(jointCount);
    }
    if (previousSampled_.size() < jointCount) {
        previousSampled_.resize(jointCount);
    }
    if (currentPose_.size() < jointCount) {
        currentPose_.resize(jointCount);
    }
    if (previousPose_.size() < jointCount) {
        previousPose_.resize(jointCount);
    }
    if (blendedPose_.size() < jointCount) {
        blendedPose_.resize(jointCount);
    }
}

void AnimationStateMachine::sampleCurrentIntoPoses()
{
    if (current_ == nullptr) {
        return;
    }
    const auto jointCount = jointCountOf(*current_);
    if (jointCount == 0) {
        return;
    }
    ensurePoseCapacity(jointCount);

    // Sample current clip.
    for (std::size_t i = 0; i < jointCount; ++i) {
        currentSampled_[i] = JointTransform{};
    }
    if (!sampleClip(*current_, currentTimeSeconds_, currentSampled_)) {
        return;
    }
    for (std::size_t i = 0; i < jointCount; ++i) {
        currentPose_[i].translation = currentSampled_[i].translation;
        currentPose_[i].rotation = currentSampled_[i].rotation;
        currentPose_[i].scale = currentSampled_[i].scale;
    }

    if (previous_ != nullptr && blendWeight_ < 1.0f) {
        const auto prevJointCount = jointCountOf(*previous_);
        if (prevJointCount > 0) {
            ensurePoseCapacity(prevJointCount);
            for (std::size_t i = 0; i < prevJointCount; ++i) {
                previousSampled_[i] = JointTransform{};
            }
            if (sampleClip(*previous_, previousTimeSeconds_, previousSampled_)) {
                for (std::size_t i = 0; i < prevJointCount; ++i) {
                    previousPose_[i].translation = previousSampled_[i].translation;
                    previousPose_[i].rotation = previousSampled_[i].rotation;
                    previousPose_[i].scale = previousSampled_[i].scale;
                }
                blendPoses(previousPose_, currentPose_, blendWeight_, blendedPose_);
                return;
            }
        }
    }

    // No transition active: blendedPose = currentPose.
    for (std::size_t i = 0; i < jointCount; ++i) {
        blendedPose_[i] = currentPose_[i];
    }
}

void collectEvents(const AnimationClip& clip,
    const float previousTimeSeconds,
    const float currentTimeSeconds,
    const bool looping,
    std::pmr::vector<AnimationEvent>& outEvents)
{
    outEvents.clear();
    if (!looping || currentTimeSeconds >= previousTimeSeconds) {
        for (const auto& event : clip.events) {
            if (event.timeSeconds > previousTimeSeconds
                && event.timeSeconds <= currentTimeSeconds) {
                outEvents.push_back(event);
            }
        }
        return;
    }

    for (const auto& event : clip.events) {
        if (event.timeSeconds > previousTimeSeconds || event.timeSeconds <= currentTimeSeconds) {
            outEvents.push_back(event);
        }
    }
}

bool AnimationStateMachine::play(const AnimationClip& clip)
{
    if (clip.durationSeconds <= 0.0f) {
        return false;
    }
    current_ = &clip;
    previous_ = nullptr;
    currentTimeSeconds_ = 0.0f;
    previousTimeSeconds_ = 0.0f;
    transitionDurationSeconds_ = 0.0f;
    transitionElapsedSeconds_ = 0.0f;
    blendWeight_ = 1.0f;
    ensurePoseCapacity(jointCountOf(clip));
    sampleCurrentIntoPoses();
    return true;
}

bool AnimationStateMachine::transitionTo(const AnimationClip& clip, const float durationSeconds)
{
    if (clip.durationSeconds <= 0.0f) {
        return false;
    }
    previous_ = current_;
    previousTimeSeconds_ = currentTimeSeconds_;
    current_ = &clip;
    currentTimeSeconds_ = 0.0f;
    transitionDurationSeconds_ = std::max(0.0f, durationSeconds);
    transitionElapsedSeconds_ = 0.0f;
    blendWeight_ = transitionDurationSeconds_ <= 0.0f ? 1.0f : 0.0f;
    ensurePoseCapacity(jointCountOf(clip));
    if (previous_ != nullptr) {
        ensurePoseCapacity(jointCountOf(*previous_));
    }
    sampleCurrentIntoPoses();
    return true;
}

void AnimationStateMachine::advance(const float deltaSeconds)
{
    if (current_ == nullptr) {
        return;
    }

    const auto delta = std::max(0.0f, deltaSeconds);
    currentTimeSeconds_ += delta;
    if (current_->durationSeconds > 0.0f && currentTimeSeconds_ > current_->durationSeconds) {
        currentTimeSeconds_ = current_->durationSeconds;
    }

    if (transitionDurationSeconds_ > 0.0f && blendWeight_ < 1.0f) {
        transitionElapsedSeconds_ += delta;
        previousTimeSeconds_ += delta;
        if (previous_ != nullptr && previousTimeSeconds_ > previous_->durationSeconds) {
            previousTimeSeconds_ = previous_->durationSeconds;
        }
        blendWeight_ = std::min(1.0f, transitionElapsedSeconds_ / transitionDurationSeconds_);
        if (blendWeight_ >= 1.0f) {
            previous_ = nullptr;
        }
    }

    sampleCurrentIntoPoses();
}

const std::pmr::string& AnimationStateMachine::currentStateName() const
{
    static const std::pmr::string empty;
    return current_ == nullptr ? empty : current_->name;
}

float AnimationStateMachine::currentTimeSeconds() const
{
    return currentTimeSeconds_;
}

float AnimationStateMachine::blendWeight() const
{
    return blendWeight_;
}

} // namespace animation
