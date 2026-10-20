#include "network/network_module.h"

extern "C" ARP_SCRIPT_PLUGIN_EXPORT int arp_register_arp_network(ArpScriptPluginHostV2* host)
{
    return arp::scriptvm::registerNetworkScriptPlugin(host);
}
