#include "scene/main_scene_registry.h"
#include "scene/scene.h"
#include "scene/script_behavior_component.h"

#include <cassert>
#include <memory>
#include <string>
#include <vector>

namespace {

using arp::scene::MainSceneRegistry;
using arp::scene::Scene;
using arp::scene::SceneUpdateContext;

struct SceneOwner {
    std::vector<std::unique_ptr<Scene>> scenes;
    std::uint64_t nextId = 100u;

    Scene* create(const arp::core::pmr::String& name)
    {
        scenes.push_back(std::make_unique<Scene>(
            arp::core::pmr::String(name, &arp::core::defaultCpuMemoryResource()),
            arp::scene::SceneId::fromValue(nextId++)));
        return scenes.back().get();
    }

    bool destroy(Scene& scene)
    {
        for (auto it = scenes.begin(); it != scenes.end(); ++it) {
            if (it->get() == &scene) {
                scenes.erase(it);
                return true;
            }
        }
        return false;
    }
};

void register_request_and_commit_are_frame_bounded()
{
    Scene first;
    Scene second;
    MainSceneRegistry registry;

    assert(!registry.request(first));
    assert(registry.registerScene(first));
    assert(registry.active() == &first);
    assert(registry.pending() == nullptr);
    assert(registry.generation() == 1u);

    assert(registry.registerScene(first));
    assert(registry.generation() == 1u);
    assert(registry.registerScene(second));
    assert(registry.active() == &first);
    assert(registry.request(second));
    assert(registry.pending() == &second);
    assert(registry.active() == &first);
    assert(registry.generation() == 1u);

    assert(registry.commit());
    assert(registry.active() == &second);
    assert(registry.pending() == nullptr);
    assert(registry.generation() == 2u);

    assert(registry.request(second));
    assert(registry.commit());
    assert(registry.generation() == 2u);
}

void direct_scene_factory_keeps_engine_owned_identity()
{
    SceneOwner owner;
    Scene first;
    MainSceneRegistry registry;
    registry.bindFactory(
        [&owner](const arp::core::pmr::String& name) {
            return owner.create(name);
        },
        [&owner](Scene& scene) {
            return owner.destroy(scene);
        });
    first.bindMainSceneRegistry(&registry);
    assert(registry.registerScene(first));

    auto* second = first.create_scene("script-created-scene");
    assert(second != nullptr);
    assert(second != &first);
    assert(registry.contains(*second));
    assert(second->nativeReflectionObject().native_reference_alive());
    assert(second->request_main_scene());
    assert(registry.active() == &first);
    assert(registry.commit());
    assert(registry.active() == second);
    assert(second->is_main_scene());
    assert(second->main_scene_generation() == 2u);

    const auto reference = second->nativeReflectionObject();
    assert(first.destroy_scene(second));
    assert(!reference.native_reference_alive());
    assert(registry.active() == nullptr);
}

void invalid_requests_and_candidate_rollback_are_deterministic()
{
    Scene first;
    Scene candidate;
    Scene unregistered;
    MainSceneRegistry registry;

    assert(registry.registerScene(first));
    assert(registry.registerScene(candidate));
    assert(!registry.request(unregistered));
    assert(registry.request(candidate));
    assert(registry.unregisterScene(candidate));
    assert(registry.active() == &first);
    assert(registry.pending() == nullptr);
    assert(registry.generation() == 1u);
    assert(!registry.contains(candidate));

    assert(registry.unregisterScene(first));
    assert(registry.active() == nullptr);
    assert(registry.pending() == nullptr);
    assert(registry.generation() == 2u);
}

void only_the_committed_scene_is_updated()
{
    // Captured state must outlive both scenes' Runtime teardown hooks.
    std::vector<std::string> firstHooks;
    std::vector<std::string> secondHooks;
    Scene first;
    Scene second;
    MainSceneRegistry registry;
    assert(registry.registerScene(first));
    assert(registry.registerScene(second));

    auto& firstObject = first.createObject("first-script");
    auto& secondObject = second.createObject("second-script");
    firstObject.addComponent<arp::scene::ScriptBehaviorComponent>(
        [&](const char* hook, const SceneUpdateContext&) {
            firstHooks.emplace_back(hook);
            return true;
        });
    secondObject.addComponent<arp::scene::ScriptBehaviorComponent>(
        [&](const char* hook, const SceneUpdateContext&) {
            secondHooks.emplace_back(hook);
            return true;
        });

    assert(registry.active() == &first);
    assert(registry.active()->update({0.016f, 1u, 64u}).ok());
    assert(!firstHooks.empty());
    assert(secondHooks.empty());

    assert(registry.request(second));
    assert(registry.active() == &first);
    assert(registry.active()->update({0.016f, 2u, 64u}).ok());
    assert(secondHooks.empty());

    assert(registry.commit());
    assert(registry.active() == &second);
    assert(registry.active()->update({0.016f, 3u, 64u}).ok());
    assert(!secondHooks.empty());
}

void failed_script_lifecycle_is_fail_closed()
{
    Scene scene;
    MainSceneRegistry registry;
    assert(registry.registerScene(scene));
    auto& object = scene.createObject("failed-script");
    object.addComponent<arp::scene::ScriptBehaviorComponent>(
        [](const char* hook, const SceneUpdateContext&) {
            return std::string(hook) != "on_update";
        });

    const auto result = registry.active()->update({0.016f, 4u, 64u});
    assert(!result.ok());
    assert(result.diagnostic().code == arp::scene::SceneDiagnosticCode::ScriptExecutionFailed);
}

void destroyed_objects_invalidate_all_script_references()
{
    Scene scene;
    MainSceneRegistry registry;
    assert(registry.registerScene(scene));
    auto& object = scene.createObject("ephemeral");
    const auto reference = object.nativeReflectionObject();
    assert(reference.native_reference_alive());
    assert(scene.destroyObject(object.id()).ok());
    assert(!reference.native_reference_alive());
    assert(reference.cast<arp::scene::GameObject*>() == nullptr);
}

} // namespace

int main()
{
    register_request_and_commit_are_frame_bounded();
    direct_scene_factory_keeps_engine_owned_identity();
    invalid_requests_and_candidate_rollback_are_deterministic();
    only_the_committed_scene_is_updated();
    failed_script_lifecycle_is_fail_closed();
    destroyed_objects_invalidate_all_script_references();
    return 0;
}
