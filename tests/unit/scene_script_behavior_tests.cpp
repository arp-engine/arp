#include "scene/scene.h"
#include "scene/script_behavior_component.h"

#include <array>
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

// Native teardown must release ownership even when no gameplay hook is allowed.
class LifecycleProbe final : public arp::scene::Component {
public:
    LifecycleProbe(std::array<int, 8>& calls, int& destructors)
        : calls_(calls), destructors_(destructors)
    {}

    ~LifecycleProbe() override { ++destructors_; }

    void onEnable(const SceneUpdateContext&) override { ++calls_[0]; }

    void onStart(const SceneUpdateContext&) override { ++calls_[1]; }

    void onUpdate(const SceneUpdateContext&) override { ++calls_[2]; }

    void onFixedUpdate(const SceneUpdateContext&) override { ++calls_[3]; }

    void onLateUpdate(const SceneUpdateContext&) override { ++calls_[4]; }

    void onEvent(const arp::scene::SceneEvent&) override { ++calls_[5]; }

    void onDisable(const SceneUpdateContext&) override { ++calls_[6]; }

    void onDestroy(const SceneUpdateContext&) override { ++calls_[7]; }

private:
    std::array<int, 8>& calls_;
    int& destructors_;
};

void passive_scene_rejects_dispatch_and_cleans_up_without_hooks()
{
    using namespace arp::scene;
    std::array<int, 8> calls{};
    int destructors = 0;
    {
        Scene document("authoring", SceneId::fromValue(7), SceneExecutionMode::Passive);
        assert(document.executionMode() == SceneExecutionMode::Passive);
        document.configureEventCapacity(4);
        document.addComponent<LifecycleProbe>(calls, destructors);
        auto& object = document.createObject("component removal");
        auto& probe = object.addComponent<LifecycleProbe>(calls, destructors);
        const auto reject = [](const SceneUpdateResult& result) {
            assert(!result.ok() && result.visitedObjectCount() == 0);
            assert(result.diagnostic().code == SceneDiagnosticCode::PassiveExecutionRejected);
            assert(result.diagnostic().message == "ARP-SCENE-PASSIVE-EXECUTION");
        };
        reject(document.update({0.25f, 1, 16}));
        reject(document.fixedUpdate({0.25f, 1, 16}));
        assert(!object.send_event("must not queue", {}));
        reject(document.dispatchEvents({}));
        reject(document.dispatchPhysicsEvents({}, {}));
        assert(document.extractTraversalSnapshot({}).ok());
        auto& script = object.addComponent<ScriptBehaviorComponent>(
            [&](const char*, const SceneUpdateContext&) {
                ++calls[2];
                return true;
            });
        script.setEventInvoke([&](const SceneEvent&) {
            ++calls[5];
            return true;
        });
        script.setPhysicsEventInvoke([&](const char*, GameObject*) {
            ++calls[5];
            return true;
        });
        script.onUpdate({});
        script.onEvent({object.id(), "direct", {}});
        assert(!script.dispatchPhysicsEvent("on_collision_enter", &object));
        assert(script.failed() && script.failureDiagnostic() == "ARP-SCENE-PASSIVE-EXECUTION");
        assert(object.removeComponent(probe.id()));
        assert(destructors == 1);
        auto& parent = document.createObject("parent");
        auto& child = document.createObject("child");
        child.addComponent<LifecycleProbe>(calls, destructors);
        assert(document.reparent(child.id(), parent.id()).ok());
        assert(document.destroyObject(parent.id()).ok());
        assert(destructors == 2);
        {
            Scene::RestorationTransaction rollback(document);
            document.createObject("failed candidate")
                .addComponent<LifecycleProbe>(calls, destructors);
        }
        assert(destructors == 3);
        document.createObject("close").addComponent<LifecycleProbe>(calls, destructors);
    }
    assert(destructors == 5);
    assert((calls == std::array<int, 8>{}));

    {
        Scene runtime;
        assert(runtime.executionMode() == SceneExecutionMode::Runtime);
        runtime.configureEventCapacity(1);
        auto& object = runtime.createObject("runtime");
        object.addComponent<LifecycleProbe>(calls, destructors);
        assert(runtime.update({}).ok());
        assert(runtime.fixedUpdate({}).ok());
        assert(object.send_event("runtime event", {}));
        assert(runtime.dispatchEvents({}).ok());
    }
    assert((calls == std::array<int, 8>{1, 1, 1, 1, 1, 1, 1, 1}));
    assert(destructors == 6);
}

} // namespace

int main()
{
    lifecycle_hooks_are_ordered_and_forward_context();
    failed_hook_stops_later_dispatch();
    missing_callback_is_a_noop();
    scene_update_dispatches_script_components_and_fails_closed();
    passive_scene_rejects_dispatch_and_cleans_up_without_hooks();
    return 0;
}
