#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"

#include <cstdint>
#include <string>
#include <vector>

namespace animation {

enum class AnimatorParameterType {
    Bool,
    Float,
    Int,
    Trigger,
};

enum class AnimatorConditionMode {
    If,
    IfNot,
    Greater,
    Less,
    Equals,
    NotEqual,
};

struct AnimatorParameterDefinition {
    std::pmr::string name;
    AnimatorParameterType type = AnimatorParameterType::Float;
    float defaultFloat = 0.0f;
    int defaultInt = 0;
    bool defaultBool = false;
};

struct AnimatorParameterValue {
    std::pmr::string name;
    AnimatorParameterType type = AnimatorParameterType::Float;
    float floatValue = 0.0f;
    int intValue = 0;
    bool boolValue = false;
    bool triggerArmed = false;
};

struct AnimatorCondition {
    std::pmr::string parameter;
    AnimatorConditionMode mode = AnimatorConditionMode::If;
    float threshold = 0.0f;
};

struct AnimatorState {
    std::pmr::string name;
    std::pmr::string clipName;
    float clipDurationSeconds = 1.0f;
    float speed = 1.0f;
};

struct AnimatorTransition {
    std::pmr::string sourceState;
    std::pmr::string destinationState;
    float durationSeconds = 0.0f;
    bool hasExitTime = false;
    float exitTime = 0.0f;
    std::pmr::vector<AnimatorCondition> conditions;
};

struct AnimatorControllerAsset {
    std::pmr::string name;
    std::pmr::string defaultStateName;
    std::pmr::vector<AnimatorParameterDefinition> parameters;
    std::pmr::vector<AnimatorState> states;
    std::pmr::vector<AnimatorTransition> transitions;
};

class ARP_API AnimatorControllerRuntime {
public:
    explicit AnimatorControllerRuntime(const AnimatorControllerAsset& asset);

    bool setBool(const std::pmr::string& name, bool value);
    bool setFloat(const std::pmr::string& name, float value);
    bool setInt(const std::pmr::string& name, int value);
    bool fireTrigger(const std::pmr::string& name);
    bool playState(const std::pmr::string& name);

    bool findBool(const std::pmr::string& name) const;
    float findFloat(const std::pmr::string& name) const;
    int findInt(const std::pmr::string& name) const;
    bool triggerArmed(const std::pmr::string& name) const;

    void advance(float deltaSeconds);

    const std::pmr::string& currentStateName() const;
    const std::pmr::string& nextStateName() const;
    const std::pmr::string& activeClipName() const;
    float clipTimeSeconds() const;
    float normalizedTime() const;
    float transitionProgress() const;
    const std::pmr::vector<AnimatorParameterValue>& parameters() const;

private:
    const AnimatorControllerAsset& asset_;
    std::pmr::vector<AnimatorParameterValue> parameters_;
    std::uint32_t currentState_ = 0;
    std::uint32_t nextState_ = UINT32_MAX;
    float stateTimeSeconds_ = 0.0f;
    float transitionElapsedSeconds_ = 0.0f;
    float transitionDurationSeconds_ = 0.0f;

    AnimatorParameterValue* findParameter(const std::pmr::string& name);
    const AnimatorParameterValue* findParameter(const std::pmr::string& name) const;
    const AnimatorState& currentState() const;
    std::uint32_t findStateIndex(const std::pmr::string& name) const;
};

} // namespace animation
