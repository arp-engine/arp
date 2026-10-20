#pragma once
#include "network/network_module.h"
#include "tests/fixtures/http_server_fixture.h"

namespace arp::scriptvm::test {
using core::pmr::String;

class ScriptServerFixture : public arp::network::http::test::ServerFixture {
public:
    ScriptServerFixture()
    {
        ScriptPluginServiceRegistry services;
        assert(services.provide(
            String(kNetworkIoContextService), kNetworkIoContextServiceVersion, &context));
        ScriptPluginRegistrar registrar(String("arp_network"), &services);
        ArpScriptPluginHostV2 host{
            ARP_SCRIPT_PLUGIN_ABI_VERSION_V2, &registrar, &services, "windows", "debug", nullptr};
        assert(registerNetworkScriptPlugin(&host) == 1);
        assert(modules.installPluginModule(registrar.name(), registrar.build()));
        assert(modules.hasExport(String("arp_network"), String("HttpServer")));
        assert(modules.hasExport(String("arp_network"), String("HttpServerRequest")));
        assert(modules.hasExport(String("arp_network"), String("HttpResponse")));
    }

    ModuleRegistry modules;
};

} // namespace arp::scriptvm::test
