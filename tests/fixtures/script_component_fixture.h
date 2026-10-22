#pragma once
#include "2d/scene_component_factory.h"
#include "scene/script_component_module.h"
#include "scriptvm/plugin/plugin_runtime_scope.h"

bool arp_add_arp_scene_static_plugin(arp::scriptvm::StaticPluginCatalog&);
bool arp_add_arp_scene2d_static_plugin(arp::scriptvm::StaticPluginCatalog&);

namespace arp::tests {
struct ScriptComponentFixture {
    scene::Scene mainScene;
    scene::MainSceneRegistry scenes;
    scriptvm::ScriptPluginRuntimeScope scope;
    core::SharedPtr<physics2d::PhysicsWorld2D> world;

    bool installDomains()
    {
        if (!install())
            return false;
        physics2d::WorldConfig config{{0, 0}, 32, 32, 8, 32, 64, 32, 16, 4, 1.f / 60};
        world = core::makeShared<physics2d::PhysicsWorld2D>(config);
        return world->valid() && scope.services().provide("arp.physics2d.world", 1, world.get())
            && arp_add_arp_scene2d_static_plugin(scope.staticCatalog())
            && bool(scope.modules().loadModule("arp_scene2d"));
    }

    bool install()
    {
        auto& services = scope.services();
        return services.provide(scene::kMainSceneService, 1, &mainScene)
            && services.provide(scene::kMainSceneRegistryService, 1, &scenes)
            && arp_add_arp_scene_static_plugin(scope.staticCatalog())
            && (scope.modules().setStaticPluginCatalog(&scope.staticCatalog(), &services), true)
            && bool(scope.modules().loadModule("arp_scene"));
    }

    static scriptvm::AotExecutionBudgetV5 budget()
    {
        scriptvm::AotExecutionBudgetV5 value;
        value.instructionLimit = 100000;
        value.recursionLimit = 64;
        value.maxContainerValues = 1024;
        value.maxCallArguments = 16;
        value.maxFrameValues = 1024;
        value.maxMemoryBytes = 16 * 1024 * 1024;
        value.maxContinuationFrames = 64;
        return value;
    }
};
} // namespace arp::tests
