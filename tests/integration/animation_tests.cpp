#include "animation/animation.h"
#include "core/cpu_allocator.h"

#include <cassert>
#include <string>
#include <vector>

int main()
{
    animation::AnimationClip run;
    run.name = "Run";
    run.durationSeconds = 1.0f;
    run.channels.push_back(animation::AnimationChannel{
        0,
        {
            {0.0f, {{0.0f, 0.0f, 0.0f}}},
            {1.0f, {{10.0f, 0.0f, 0.0f}}},
        },
    });
    run.events.push_back(animation::AnimationEvent{"footstep", 0.25f});
    run.events.push_back(animation::AnimationEvent{"attack_window", 0.75f});

    std::pmr::vector<animation::JointTransform> pose(1);
    assert(animation::sampleClip(run, 0.5f, pose));
    assert(pose[0].translation.x == 5.0f);

    animation::AnimationClip turn;
    turn.name = "Turn";
    turn.durationSeconds = 1.0f;
    turn.channels.push_back(animation::AnimationChannel{
        0,
        {
            {0.0f,
                animation::JointTransform{
                    {},
                    animation::Quat::identity(),
                    {1.0f, 1.0f, 1.0f},
                }},
            {1.0f,
                animation::JointTransform{
                    {},
                    {0.0f, 0.0f, 0.70710678f, 0.70710678f},
                    {1.0f, 1.0f, 1.0f},
                }},
        },
        "rotation",
    });
    assert(animation::sampleClip(turn, 1.0f, pose));
    assert(pose[0].rotation.z > 0.7f);
    assert(pose[0].rotation.w > 0.7f);

    std::pmr::vector<animation::AnimationEvent> fired;
    fired.reserve(4);
    animation::collectEvents(run, 0.2f, 0.8f, false, fired);
    assert(fired.size() == 2);
    assert(fired[0].name == "footstep");
    assert(fired[1].name == "attack_window");

    animation::AnimationStateMachine machine;
    assert(machine.play(run));
    machine.advance(0.5f);
    assert(machine.currentStateName() == "Run");
    assert(machine.currentTimeSeconds() == 0.5f);

    animation::AnimationClip idle;
    idle.name = "Idle";
    idle.durationSeconds = 2.0f;
    assert(machine.transitionTo(idle, 0.25f));
    machine.advance(0.125f);
    assert(machine.currentStateName() == "Idle");
    assert(machine.blendWeight() == 0.5f);
    machine.advance(0.125f);
    assert(machine.blendWeight() == 1.0f);

    return 0;
}
