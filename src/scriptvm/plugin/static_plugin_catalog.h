#pragma once

#include "scriptvm/plugin/plugin_registrar.h"

struct ArpScriptPluginHostV2;

namespace arp::scriptvm {

// Static plugins use the exact same entry contract as dynamically loaded
// plugins. The catalog owns no services; the embedding runtime supplies them
// when a module is materialized.
using ScriptPluginEntry = int (*)(::ArpScriptPluginHostV2*);

struct ScriptPluginDescriptor {
    core::pmr::String module{&arp::core::defaultCpuMemoryResource()};
    std::uint32_t contractVersion = 2u;
    std::uint32_t moduleVersion = 1u;
    ScriptPluginEntry entry = nullptr;
    core::pmr::String exportContractHash{&arp::core::defaultCpuMemoryResource()};
    core::pmr::String reflectionRegistryHash{&arp::core::defaultCpuMemoryResource()};
    core::pmr::Vector<ScriptPluginServiceRequirement> requiredServices{
        &arp::core::defaultCpuMemoryResource()};
};

class ARP_API StaticPluginCatalog final {
public:
    bool add(ScriptPluginDescriptor descriptor);

    void clear() { plugins_.clear(); }

    std::shared_ptr<ModuleExports> load(const core::pmr::String& module,
        ScriptPluginServiceRegistry* services,
        core::pmr::String& diagnostics) const;

#if defined(ARP_WOCC_CONTRACT_TEST_HOOKS)
    // Host-only fault injection for candidate-world contract tests. This is
    // deliberately unavailable to the script ABI and has no production call
    // sites; it lets tests verify AOT contract rejection against the same
    // immutable descriptor metadata used by real catalogs.
    bool overrideContractForTesting(const core::pmr::String& module,
        const core::pmr::String& exportContractHash,
        const core::pmr::String& reflectionRegistryHash)
    {
        for (auto& descriptor : plugins_) {
            if (descriptor.module != module)
                continue;
            descriptor.exportContractHash = exportContractHash;
            descriptor.reflectionRegistryHash = reflectionRegistryHash;
            return true;
        }
        return false;
    }
#endif

private:
    core::pmr::Vector<ScriptPluginDescriptor> plugins_{&arp::core::defaultCpuMemoryResource()};
};

} // namespace arp::scriptvm
