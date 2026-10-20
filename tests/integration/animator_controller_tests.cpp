#include "animation/animator_controller.h"

#include <cassert>
#include <string>

namespace {

animation::AnimatorControllerAsset makeController()
{
    animation::AnimatorControllerAsset asset;
    asset.parameters.push_back(animation::AnimatorParameterDefinition{
        "speed",
        animation::AnimatorParameterType::Float,
    });
    asset.parameters.push_back(animation::AnimatorParameterDefinition{
        "attack",
        animation::AnimatorParameterType::Trigger,
    });
    asset.states.push_back(animation::AnimatorState{
        "idle",
        "idle",
        1.0f,
    });
    asset.states.push_back(animation::AnimatorState{
        "run",
        "run",
        0.8f,
    });
    asset.states.push_back(animation::AnimatorState{
        "atk",
        "atk",
        0.6f,
    });
    asset.defaultStateName = "idle";
    asset.transitions.push_back(animation::AnimatorTransition{
        "idle",
        "run",
        0.15f,
        false,
        0.0f,
        {
            animation::AnimatorCondition{
                "speed",
                animation::AnimatorConditionMode::Greater,
                0.1f,
            },
        },
    });
    asset.transitions.push_back(animation::AnimatorTransition{
        "run",
        "idle",
        0.10f,
        false,
        0.0f,
        {
            animation::AnimatorCondition{
                "speed",
                animation::AnimatorConditionMode::Less,
                0.1f,
            },
        },
    });
    asset.transitions.push_back(animation::AnimatorTransition{
        "run",
        "atk",
        0.05f,
        false,
        0.0f,
        {
            animation::AnimatorCondition{
                "attack",
                animation::AnimatorConditionMode::If,
                0.0f,
            },
        },
    });
    return asset;
}

} // namespace

int main()
{
    const auto controller = makeController();
    animation::AnimatorControllerRuntime runtime(controller);

    assert(runtime.currentStateName() == "idle");
    assert(runtime.activeClipName() == "idle");
    assert(runtime.findFloat("speed") == 0.0f);

    assert(runtime.setFloat("speed", 0.5f));
    runtime.advance(0.016f);
    assert(runtime.currentStateName() == "run");
    assert(runtime.transitionProgress() > 0.0f);
    assert(runtime.activeClipName() == "run");

    assert(runtime.fireTrigger("attack"));
    runtime.advance(0.016f);
    assert(runtime.currentStateName() == "atk");
    assert(!runtime.triggerArmed("attack"));

    runtime.advance(0.3f);
    assert(runtime.normalizedTime() > 0.0f);
    assert(runtime.clipTimeSeconds() > 0.0f);

    assert(runtime.playState("idle"));
    assert(runtime.currentStateName() == "idle");
    assert(runtime.activeClipName() == "idle");
    assert(runtime.clipTimeSeconds() == 0.0f);
    assert(runtime.transitionProgress() == 1.0f);
    assert(!runtime.playState("missing"));
    assert(runtime.currentStateName() == "idle");

    return 0;
}
