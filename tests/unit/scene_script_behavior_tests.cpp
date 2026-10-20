#include "scene/scene.h"
#include "scene/script_behavior_component.h"

#include <cassert>
#include <string>
#include <vector>

namespace {

using arp::scene::SceneUpdateContext;
using arp::scene::ScriptBehaviorComponent;

void lifecycle_hooks_are_ordered_and_forward_context()
{
    std::vector<std::string> hooks;
    SceneUpdateContext received{};
    ScriptBehaviorComponent component([&](const char* hook, const SceneUpdateContext& context) {
        hooks.emplace_back(hook);
        received = context;
        return true;
    });

    const SceneUpdateContext context{0.25f, 42u};
    component.onEnable(context);
    component.onStart(context);
    component.onUpdate(context);
    component.onLateUpdate(context);
    component.onDisable(context);
    component.onDestroy(context);

    const std::vector<std::string> expected{
        "on_enable", "on_start", "on_update", "on_late_update", "on_disable", "on_destroy"};
    assert(hooks == expected);
    assert(received.deltaSeconds == 0.25f);
    assert(received.frameIndex == 42u);
    assert(!component.failed());
    assert(component.failureHook() == nullptr);
}

void failed_hook_stops_later_dispatch()
{
    std::vector<std::string> hooks;
    ScriptBehaviorComponent component([&](const char* hook, const SceneUpdateContext&) {
        hooks.emplace_back(hook);
        return std::string(hook) != "on_update";
    });

    SceneUpdateContext context{};
    component.onEnable(context);
    component.onStart(context);
    component.onUpdate(context);
    component.onLateUpdate(context);
    component.onDestroy(context);

    assert((hooks == std::vector<std::string>{"on_enable", "on_start", "on_update"}));
    assert(component.failed());
    assert(std::string(component.failureHook()) == "on_update");
}

void missing_callback_is_a_noop()
{
    ScriptBehaviorComponent component;
    SceneUpdateContext context{};
    component.onEnable(context);
    component.onStart(context);
    component.onUpdate(context);
    component.onLateUpdate(context);
    component.onDisable(context);
    component.onDestroy(context);
    assert(!component.failed());
}

void scene_update_dispatches_script_components_and_fails_closed()
{
    std::vector<std::string> hooks;
    arp::scene::Scene scene;
    auto& object = scene.createObject("scripted");
    assert(scene.reparent(object.id(), scene.rootId()).ok());
    object.addComponent<ScriptBehaviorComponent>([&](const char* hook, const SceneUpdateContext&) {
        hooks.emplace_back(hook);
        return true;
    });

    const auto first = scene.update({0.016f, 1u, 16u});
    assert(first.ok());
    assert((
        hooks == std::vector<std::string>{"on_enable", "on_start", "on_update", "on_late_update"}));

    arp::scene::Scene failedScene;
    auto& failedObject = failedScene.createObject("failed-script");
    assert(failedScene.reparent(failedObject.id(), failedScene.rootId()).ok());
    failedObject.addComponent<ScriptBehaviorComponent>(
        [](const char* hook, const SceneUpdateContext&) {
            return std::string(hook) != "on_update";
        });
    const auto failed = failedScene.update({0.016f, 1u, 16u});
    assert(!failed.ok());
    assert(failed.diagnostic().code == arp::scene::SceneDiagnosticCode::ScriptExecutionFailed);
    assert(failed.diagnostic().message.find("on_update") != arp::core::pmr::String::npos);
}

} // namespace

int main()
{
    lifecycle_hooks_are_ordered_and_forward_context();
    failed_hook_stops_later_dispatch();
    missing_callback_is_a_noop();
    scene_update_dispatches_script_components_and_fails_closed();
    return 0;
}
