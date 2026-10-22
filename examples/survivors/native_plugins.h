#pragma once
#include "physics/2d/physics_world_2d.h"
#include "scene/script_component_module.h"
#include "scriptvm/plugin/plugin_runtime_scope.h"
bool arp_add_arp_scene_static_plugin(arp::scriptvm::StaticPluginCatalog&);
bool arp_add_arp_scene2d_static_plugin(arp::scriptvm::StaticPluginCatalog&);

namespace arp::survivors {
inline bool installNativePlugins(scriptvm::ScriptPluginRuntimeScope& scope,
    scene::Scene& scene,
    scene::MainSceneRegistry& scenes,
    physics2d::PhysicsWorld2D& world)
{
    auto& services = scope.services();
    if (!services.provide(scene::kMainSceneService, 1, &scene)
        || !services.provide(scene::kMainSceneRegistryService, 1, &scenes)
        || !services.provide("arp.physics2d.world", 1, &world)
        || !arp_add_arp_scene_static_plugin(scope.staticCatalog())
        || !arp_add_arp_scene2d_static_plugin(scope.staticCatalog()))
        return false;
    scope.modules().setStaticPluginCatalog(&scope.staticCatalog(), &services);
    return bool(scope.modules().loadModule("arp_scene"))
        && bool(scope.modules().loadModule("arp_scene2d"));
}
} // namespace arp::survivors
