#include "resource/resource_module.h"

extern "C" ARP_SCRIPT_PLUGIN_EXPORT int arp_register_arp_resources(ArpScriptPluginHostV2* host)
{
    return arp::scriptvm::registerResourceScriptPlugin(host);
}
