#include "scriptvm/aot/aot_v5.h"

int main()
{
    arp::scriptvm::ScriptAotPackageRegistry registry;
    const auto host = registry.registrationHost();
    return host.contractVersion == arp::scriptvm::kAotContractVersion
            && host.registerPackage != nullptr
        ? 0
        : 1;
}
