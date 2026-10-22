#include "3d/animator_component.h"
#include "core/memory/cpu_allocator.h"
#include "games/pika/app/character_runtime_asset.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

namespace {

constexpr const char* kMale000Root =
    "D:/xygame/UnityProj/Assets/ResourceLib/Role/player/human/Male/male000";

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

bool near(const float lhs, const float rhs, const float epsilon = 0.0001f)
{
    return std::fabs(lhs - rhs) <= epsilon;
}

const animation::AnimationClip* findClip(
    const arp::assets::AnimatedMeshAsset& character, const arp::core::pmr::String& name)
{
    const auto found = std::find_if(
        character.clips.begin(), character.clips.end(), [&](const animation::AnimationClip& clip) {
            return clip.name == name;
        });
    return found == character.clips.end() ? nullptr : &*found;
}

arp::games::pika::app::CharacterRuntimeAsset loadMale000()
{
    using namespace arp::games::pika::app;

    CharacterRuntimeAssetRequest request;
    request.characterRoot = kMale000Root;
    request.initialState = text("idle");
    auto result = CharacterRuntimeAssetLoader().load(request);
    if (!result.ok()) {
        std::cerr << result.diagnostic().message << "\n";
    }
    assert(result.ok());
    return result.asset();
}

arp::scene3d::AnimatorComponentDesc descFor(
    const arp::games::pika::app::CharacterRuntimeAsset& asset)
{
    arp::scene3d::AnimatorComponentDesc desc;
    desc.character = &asset.character();
    desc.controller = &asset.imported().stateMachine.animator;
    desc.defaultStateName = asset.defaultStateName();
    desc.materialCount = asset.materialCount();
    return desc;
}

arp::scene3d::AnimatorEvaluationRequest requestState(
    const char* stateName, const float deltaSeconds)
{
    arp::scene3d::AnimatorEvaluationRequest request;
    request.stateName = text(stateName);
    request.deltaSeconds = deltaSeconds;
    return request;
}

void assertEvaluatesLoadedCharacter()
{
    const auto asset = loadMale000();
    arp::scene3d::AnimatorComponent animator(descFor(asset));

    auto result = animator.evaluate(requestState("idle", 0.0f));
    if (!result.ok()) {
        std::cerr << result.diagnostic().message << "\n";
    }
    assert(result.ok());

    const auto& frame = result.frame();
    // Phase 3: GPU-skinning-only path -- AnimatorEvaluationFrame no longer
    // exposes a CPU-baked mesh; bounds come from the bind-pose mesh and the
    // renderer consumes skinMatrices() via the GPU palette. We assert the
    // animation-state metadata that the frame still carries.
    assert(frame.materialCount() == asset.materialCount());
    assert(frame.bounds().valid());
    assert(frame.stateName() == text("idle"));
    assert(!frame.clipName().empty());
    assert(frame.clipTimeSeconds() == 0.0f);
    assert(frame.jointCount() > 0u);
    assert(!frame.exposesBackendHandles());

    result = animator.evaluate(requestState("idle", 0.033f));
    assert(result.ok());
    const auto advancedTime = result.frame().clipTimeSeconds();
    assert(advancedTime > 0.0f);

    result = animator.evaluate(requestState("idle", -1.0f));
    assert(result.ok());
    assert(near(result.frame().clipTimeSeconds(), advancedTime));
}

void assertAdvancesWithImportedClipPhysicalDuration()
{
    const auto asset = loadMale000();

    const animation::AnimatorState* selectedState = nullptr;
    const animation::AnimationClip* selectedClip = nullptr;
    for (const auto& state : asset.imported().stateMachine.animator.states) {
        const auto* clip = findClip(asset.character(), state.clipName);
        if (clip == nullptr) {
            continue;
        }
        if (!near(clip->durationSeconds, 1.0f, 0.01f)) {
            selectedState = &state;
            selectedClip = clip;
            break;
        }
    }

    assert(selectedState != nullptr);
    assert(selectedClip != nullptr);
    assert(near(selectedState->clipDurationSeconds, selectedClip->durationSeconds));

    arp::scene3d::AnimatorComponent animator(descFor(asset));
    const auto deltaSeconds = selectedClip->durationSeconds + 0.125f;
    const auto result = animator.evaluate(requestState(selectedState->name.c_str(), deltaSeconds));
    if (!result.ok()) {
        std::cerr << result.diagnostic().message << "\n";
    }
    assert(result.ok());
    assert(near(result.frame().clipTimeSeconds(), 0.125f, 0.001f));
}

void assertDiagnostic(arp::scene3d::AnimatorEvaluationResult result,
    const arp::scene3d::AnimatorEvaluationDiagnosticCode expected)
{
    assert(!result.ok());
    assert(result.diagnostic().code == expected);
    assert(!result.diagnostic().message.empty());
}

void assertDeterministicDiagnostics()
{
    const auto asset = loadMale000();

    {
        auto desc = descFor(asset);
        desc.character = nullptr;
        arp::scene3d::AnimatorComponent animator(desc);
        assertDiagnostic(animator.evaluate(requestState("idle", 0.0f)),
            arp::scene3d::AnimatorEvaluationDiagnosticCode::MissingAsset);
    }
    {
        auto desc = descFor(asset);
        desc.controller = nullptr;
        arp::scene3d::AnimatorComponent animator(desc);
        assertDiagnostic(animator.evaluate(requestState("idle", 0.0f)),
            arp::scene3d::AnimatorEvaluationDiagnosticCode::MissingAnimator);
    }
    {
        auto desc = descFor(asset);
        arp::assets::AnimatedMeshAsset invalidCharacter;
        desc.character = &invalidCharacter;
        arp::scene3d::AnimatorComponent animator(desc);
        assertDiagnostic(animator.evaluate(requestState("idle", 0.0f)),
            arp::scene3d::AnimatorEvaluationDiagnosticCode::MissingSkinnedMesh);
    }
    {
        arp::scene3d::AnimatorComponent animator(descFor(asset));
        assertDiagnostic(animator.evaluate(requestState("not_a_state", 0.0f)),
            arp::scene3d::AnimatorEvaluationDiagnosticCode::MissingAnimationState);
    }
    {
        auto controller = asset.imported().stateMachine.animator;
        controller.states.clear();
        controller.states.push_back(animation::AnimatorState{
            text("empty_state"),
            text(""),
            1.0f,
            1.0f,
        });
        controller.defaultStateName = text("empty_state");

        auto desc = descFor(asset);
        desc.controller = &controller;
        desc.defaultStateName = text("empty_state");
        arp::scene3d::AnimatorComponent animator(desc);
        assertDiagnostic(animator.evaluate(requestState("empty_state", 0.0f)),
            arp::scene3d::AnimatorEvaluationDiagnosticCode::MissingActiveClip);
    }
    {
        auto controller = asset.imported().stateMachine.animator;
        assert(!controller.states.empty());
        controller.states.front().name = text("idle");
        controller.states.front().clipName = text("missing_clip");
        controller.defaultStateName = text("idle");

        auto desc = descFor(asset);
        desc.controller = &controller;
        desc.defaultStateName = text("idle");
        arp::scene3d::AnimatorComponent animator(desc);
        assertDiagnostic(animator.evaluate(requestState("idle", 0.0f)),
            arp::scene3d::AnimatorEvaluationDiagnosticCode::SkinningFailed);
    }
}

} // namespace

int main()
{
    assertEvaluatesLoadedCharacter();
    assertAdvancesWithImportedClipPhysicalDuration();
    assertDeterministicDiagnostics();
    return 0;
}
