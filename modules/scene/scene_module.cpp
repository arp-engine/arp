#include "scene/scene.h"
#include "scriptvm/runtime/cpp_plugin_api.h"

namespace arp::reflection::generated {
bool register_arp_scene_reflection(
    arp::scriptvm::ModuleRegistration*, const arp::reflection::object*);
}

extern "C" ARP_SCRIPT_PLUGIN_EXPORT int arp_register_arp_scene(ArpScriptPluginHostV2* host)
{
    if (host == nullptr || host->abiVersion != ARP_SCRIPT_PLUGIN_ABI_VERSION_V2
        || host->registrar == nullptr || host->services == nullptr) {
        return 0;
    }
    auto* mainScene = host->services->find<arp::scene::Scene>(
        arp::scene::kMainSceneService, arp::scene::kMainSceneServiceVersion);
    if (mainScene == nullptr)
        return 0;
    if (auto* registry = host->services->find<arp::scene::MainSceneRegistry>(
            arp::core::pmr::String(
                arp::scene::kMainSceneRegistryService, &arp::core::defaultCpuMemoryResource()),
            arp::scene::kMainSceneRegistryServiceVersion)) {
        mainScene->bindMainSceneRegistry(registry);
        if (!registry->registerScene(*mainScene))
            return 0;
    }
    return arp::reflection::generated::register_arp_scene_reflection(
               host->registrar->reflectionModule(), &mainScene->nativeReflectionObject())
        ? 1
        : 0;
}
