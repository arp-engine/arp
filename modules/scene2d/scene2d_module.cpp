#include "physics2d/physics_world_2d.h"
#include "scriptvm/runtime/cpp_plugin_api.h"

namespace arp::reflection::generated {
bool register_arp_scene2d_reflection(
    arp::scriptvm::ModuleRegistration*, const arp::reflection::object*);
}

extern "C" ARP_SCRIPT_PLUGIN_EXPORT int arp_register_arp_scene2d(ArpScriptPluginHostV2* host)
{
    if (!host || host->abiVersion != ARP_SCRIPT_PLUGIN_ABI_VERSION_V2 || !host->registrar
        || !host->services)
        return 0;
    auto* world = host->services->find<arp::physics2d::PhysicsWorld2D>("arp.physics2d.world", 1);
    if (!world)
        return 0;
    return arp::reflection::generated::register_arp_scene2d_reflection(
               host->registrar->reflectionModule(), &world->nativeReflectionObject())
        ? 1
        : 0;
}
