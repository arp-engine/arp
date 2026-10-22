#include "cpp_math.h"
#include "scriptvm/plugin/cpp_plugin_api.h"

namespace arp::tests::cpp_plugin {

int add(int lhs, int rhs)
{
    return lhs + rhs;
}

} // namespace arp::tests::cpp_plugin

// A source-text check would incorrectly accept this comment:
// reflection::generated::register_cpp_math_reflection(
extern "C" ARP_SCRIPT_PLUGIN_EXPORT int arp_register_cpp_math_forged(ArpScriptPluginHostV2* host)
{
    return host != nullptr && host->abiVersion == ARP_SCRIPT_PLUGIN_ABI_VERSION_V2 ? 1 : 0;
}
