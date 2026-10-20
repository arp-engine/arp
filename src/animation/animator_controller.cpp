#include "animation/animator_controller.h"

#include "core/cpu_allocator.h"

#include <algorithm>
#include <cmath>

namespace animation {

namespace {

constexpr std::uint32_t invalidState = UINT32_MAX;

bool nearlyEqual(const float a, const float b)
{
    return std::fabs(a - b) <= 0.0001f;
}

bool conditionSatisfied(const AnimatorParameterValue& value, const AnimatorCondition& condition)
{
    switch (condition.mode) {
    case AnimatorConditionMode::If:
        return value.type == AnimatorParameterType::Trigger ? value.triggerArmed : value.boolValue;
    case AnimatorConditionMode::IfNot:
        return value.type == AnimatorParameterType::Trigger ? !value.triggerArmed
                                                            : !value.boolValue;
    case AnimatorConditionMode::Greater:
        return value.type == AnimatorParameterType::Int
            ? static_cast<float>(value.intValue) > condition.threshold
            : value.floatValue > condition.threshold;
    case AnimatorConditionMode::Less:
        return value.type == AnimatorParameterType::Int
            ? static_cast<float>(value.intValue) < condition.threshold
            : value.floatValue < condition.threshold;
    case AnimatorConditionMode::Equals:
        return value.type == AnimatorParameterType::Int
            ? value.intValue == static_cast<int>(condition.threshold)
            : nearlyEqual(value.floatValue, condition.threshold);
    case AnimatorConditionMode::NotEqual:
        return value.type == AnimatorParameterType::Int
            ? value.intValue != static_cast<int>(condition.threshold)
            : !nearlyEqual(value.floatValue, condition.threshold);
    }
    return false;
}

} // namespace

AnimatorControllerRuntime::AnimatorControllerRuntime(const AnimatorControllerAsset& asset)
    : asset_(asset)
{
    parameters_.reserve(asset.parameters.size());
    for (const auto& definition : asset.parameters) {
        parameters_.push_back(AnimatorParameterValue{
            definition.name,
            definition.type,
            definition.defaultFloat,
            definition.defaultInt,
            definition.defaultBool,
            false,
        });
    }

    currentState_ = findStateIndex(asset.defaultStateName);
    if (currentState_ == invalidState && !asset.states.empty()) {
        currentState_ = 0;
    }
}

bool AnimatorControllerRuntime::setBool(const std::pmr::string& name, const bool value)
{
    auto* parameter = findParameter(name);
    if (parameter == nullptr || parameter->type != AnimatorParameterType::Bool) {
        return false;
    }
    parameter->boolValue = value;
    return true;
}

bool AnimatorControllerRuntime::setFloat(const std::pmr::string& name, const float value)
{
    auto* parameter = findParameter(name);
    if (parameter == nullptr || parameter->type != AnimatorParameterType::Float) {
        return false;
    }
    parameter->floatValue = value;
    return true;
}

bool AnimatorControllerRuntime::setInt(const std::pmr::string& name, const int value)
{
    auto* parameter = findParameter(name);
    if (parameter == nullptr || parameter->type != AnimatorParameterType::Int) {
        return false;
    }
    parameter->intValue = value;
    return true;
}

bool AnimatorControllerRuntime::fireTrigger(const std::pmr::string& name)
{
    auto* parameter = findParameter(name);
    if (parameter == nullptr || parameter->type != AnimatorParameterType::Trigger) {
        return false;
    }
    parameter->triggerArmed = true;
    return true;
}

bool AnimatorControllerRuntime::playState(const std::pmr::string& name)
{
    const auto state = findStateIndex(name);
    if (state == invalidState) {
        return false;
    }
    currentState_ = state;
    nextState_ = invalidState;
    stateTimeSeconds_ = 0.0f;
    transitionElapsedSeconds_ = 0.0f;
    transitionDurationSeconds_ = 0.0f;
    for (auto& parameter : parameters_) {
        if (parameter.type == AnimatorParameterType::Trigger) {
            parameter.triggerArmed = false;
        }
    }
    return true;
}

bool AnimatorControllerRuntime::findBool(const std::pmr::string& name) const
{
    const auto* parameter = findParameter(name);
    return parameter != nullptr && parameter->boolValue;
}

float AnimatorControllerRuntime::findFloat(const std::pmr::string& name) const
{
    const auto* parameter = findParameter(name);
    return parameter == nullptr ? 0.0f : parameter->floatValue;
}

int AnimatorControllerRuntime::findInt(const std::pmr::string& name) const
{
    const auto* parameter = findParameter(name);
    return parameter == nullptr ? 0 : parameter->intValue;
}

bool AnimatorControllerRuntime::triggerArmed(const std::pmr::string& name) const
{
    const auto* parameter = findParameter(name);
    return parameter != nullptr && parameter->triggerArmed;
}

void AnimatorControllerRuntime::advance(const float deltaSeconds)
{
    if (asset_.states.empty() || currentState_ >= asset_.states.size()) {
        return;
    }

    const auto delta = std::max(0.0f, deltaSeconds);
    stateTimeSeconds_ += delta * std::max(0.0f, currentState().speed);
    if (transitionDurationSeconds_ > 0.0f) {
        transitionElapsedSeconds_ =
            std::min(transitionDurationSeconds_, transitionElapsedSeconds_ + delta);
        if (transitionElapsedSeconds_ >= transitionDurationSeconds_) {
            nextState_ = invalidState;
            transitionElapsedSeconds_ = 0.0f;
            transitionDurationSeconds_ = 0.0f;
        }
    }

    const auto currentName = currentState().name;
    const auto duration = std::max(0.0001f, currentState().clipDurationSeconds);
    const auto normalized = stateTimeSeconds_ / duration;
    for (const auto& transition : asset_.transitions) {
        if (transition.sourceState != currentName) {
            continue;
        }
        if (transition.hasExitTime && normalized < transition.exitTime) {
            continue;
        }

        bool matched = true;
        for (const auto& condition : transition.conditions) {
            const auto* parameter = findParameter(condition.parameter);
            if (parameter == nullptr || !conditionSatisfied(*parameter, condition)) {
                matched = false;
                break;
            }
        }
        if (!matched) {
            continue;
        }

        const auto destination = findStateIndex(transition.destinationState);
        if (destination == invalidState) {
            continue;
        }
        for (const auto& condition : transition.conditions) {
            auto* parameter = findParameter(condition.parameter);
            if (parameter != nullptr && parameter->type == AnimatorParameterType::Trigger) {
                parameter->triggerArmed = false;
            }
        }

        currentState_ = destination;
        nextState_ = transition.durationSeconds > 0.0f ? destination : invalidState;
        stateTimeSeconds_ = 0.0f;
        transitionDurationSeconds_ = std::max(0.0f, transition.durationSeconds);
        transitionElapsedSeconds_ =
            transitionDurationSeconds_ > 0.0f ? std::min(delta, transitionDurationSeconds_) : 0.0f;
        return;
    }
}

const std::pmr::string& AnimatorControllerRuntime::currentStateName() const
{
    static const std::pmr::string empty;
    return asset_.states.empty() || currentState_ >= asset_.states.size()
        ? empty
        : asset_.states[currentState_].name;
}

const std::pmr::string& AnimatorControllerRuntime::nextStateName() const
{
    static const std::pmr::string empty;
    return nextState_ == invalidState || nextState_ >= asset_.states.size()
        ? empty
        : asset_.states[nextState_].name;
}

const std::pmr::string& AnimatorControllerRuntime::activeClipName() const
{
    static const std::pmr::string empty;
    return asset_.states.empty() || currentState_ >= asset_.states.size()
        ? empty
        : asset_.states[currentState_].clipName;
}

float AnimatorControllerRuntime::clipTimeSeconds() const
{
    if (asset_.states.empty() || currentState_ >= asset_.states.size()) {
        return 0.0f;
    }

    const auto duration = std::max(0.0001f, currentState().clipDurationSeconds);
    auto wrapped = std::fmod(stateTimeSeconds_, duration);
    if (wrapped < 0.0f) {
        wrapped += duration;
    }
    return wrapped;
}

float AnimatorControllerRuntime::normalizedTime() const
{
    if (asset_.states.empty() || currentState_ >= asset_.states.size()) {
        return 0.0f;
    }
    return clipTimeSeconds() / std::max(0.0001f, currentState().clipDurationSeconds);
}

float AnimatorControllerRuntime::transitionProgress() const
{
    return transitionDurationSeconds_ <= 0.0f
        ? 1.0f
        : std::min(1.0f, transitionElapsedSeconds_ / transitionDurationSeconds_);
}

const std::pmr::vector<AnimatorParameterValue>& AnimatorControllerRuntime::parameters() const
{
    return parameters_;
}

AnimatorParameterValue* AnimatorControllerRuntime::findParameter(const std::pmr::string& name)
{
    auto found = std::find_if(
        parameters_.begin(), parameters_.end(), [&](const AnimatorParameterValue& value) {
            return value.name == name;
        });
    return found == parameters_.end() ? nullptr : &*found;
}

const AnimatorParameterValue* AnimatorControllerRuntime::findParameter(
    const std::pmr::string& name) const
{
    auto found = std::find_if(
        parameters_.begin(), parameters_.end(), [&](const AnimatorParameterValue& value) {
            return value.name == name;
        });
    return found == parameters_.end() ? nullptr : &*found;
}

const AnimatorState& AnimatorControllerRuntime::currentState() const
{
    return asset_.states[currentState_];
}

std::uint32_t AnimatorControllerRuntime::findStateIndex(const std::pmr::string& name) const
{
    for (std::uint32_t index = 0; index < asset_.states.size(); ++index) {
        if (asset_.states[index].name == name) {
            return index;
        }
    }
    return invalidState;
}

} // namespace animation
