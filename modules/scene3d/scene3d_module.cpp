#include "reflection/reflection.h"
#include "scriptvm/plugin/cpp_plugin_api.h"

namespace arp::reflection::generated {
bool register_arp_scene3d_reflection(arp::scriptvm::ModuleRegistration*);
}

extern "C" ARP_SCRIPT_PLUGIN_EXPORT int arp_register_arp_scene3d(ArpScriptPluginHostV2* host)
{
    if (host == nullptr || host->abiVersion != ARP_SCRIPT_PLUGIN_ABI_VERSION_V2
        || host->registrar == nullptr || host->services == nullptr)
        return 0;
    if (!arp::reflection::generated::register_arp_scene3d_reflection(
            host->registrar->reflectionModule()))
        return 0;
    return 1;
}
