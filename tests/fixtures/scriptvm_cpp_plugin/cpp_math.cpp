#include "cpp_math.h"

#include "scriptvm/plugin/cpp_plugin_api.h"

namespace arp::reflection::generated {
bool register_cpp_math_reflection(arp::scriptvm::ModuleRegistration* module);
}

namespace arp::tests::cpp_plugin {

int add(int lhs, int rhs)
{
    return lhs + rhs;
}

} // namespace arp::tests::cpp_plugin

extern "C" ARP_SCRIPT_PLUGIN_EXPORT int arp_register_cpp_math(ArpScriptPluginHostV2* host)
{
    if (host == nullptr || host->abiVersion != ARP_SCRIPT_PLUGIN_ABI_VERSION_V2
        || host->registrar == nullptr) {
        return 0;
    }

    return arp::reflection::generated::register_cpp_math_reflection(
               host->registrar->reflectionModule())
        ? 1
        : 0;
}
