#include "scriptvm/plugin/static_plugin_catalog.h"

#include "scriptvm/plugin/cpp_plugin_api.h"

#include <cctype>
#include <string>

namespace arp::scriptvm {

namespace {

bool isPluginModuleIdentifier(const core::pmr::String& name)
{
    if (name.empty()
        || !(std::isalpha(static_cast<unsigned char>(name.front())) || name.front() == '_')) {
        return false;
    }
    for (const char character : name) {
        if (!(std::isalnum(static_cast<unsigned char>(character)) || character == '_')) {
            return false;
        }
    }
    return true;
}

} // namespace

bool StaticPluginCatalog::add(ScriptPluginDescriptor descriptor)
{
    if (!isPluginModuleIdentifier(descriptor.module) || descriptor.contractVersion != 2u
        || descriptor.moduleVersion == 0u || descriptor.exportContractHash.empty()
        || descriptor.entry == nullptr)
        return false;
    for (const auto& existing : plugins_) {
        if (existing.module == descriptor.module)
            return false;
    }
    plugins_.push_back(std::move(descriptor));
    return true;
}

std::shared_ptr<ModuleExports> StaticPluginCatalog::load(const core::pmr::String& module,
    ScriptPluginServiceRegistry* services,
    core::pmr::String& diagnostics) const
{
    for (const auto& descriptor : plugins_) {
        if (descriptor.module != module)
            continue;
        // Dynamic and static loading must reject the same service contract
        // before executing the plugin entry. Loading an export table with a
        // missing or older service would otherwise make the two paths
        // observably different and could leave a partially registered module.
        for (const auto& required : descriptor.requiredServices) {
            if (services == nullptr
                || services->find(required.serviceId, required.version) == nullptr) {
                diagnostics = "static C++ plugin requires unavailable service ";
                diagnostics += required.serviceId;
                diagnostics += "/v";
                diagnostics += std::to_string(required.version).c_str();
                return nullptr;
            }
        }
        ScriptPluginRegistrar registrar(
            core::pmr::String(module, &core::defaultCpuMemoryResource()), services);
        ArpScriptPluginHostV2 host{2u,
            &registrar,
            services,
#if defined(__EMSCRIPTEN__)
            "emscripten",
#elif defined(_WIN32)
            "windows",
#else
            "native",
#endif
#if defined(NDEBUG)
            "release"
#else
            "debug"
#endif
            ,
            "script,reflection,services"};
        if (descriptor.entry(&host) == 0) {
            diagnostics = "static C++ plugin rejected module: ";
            diagnostics += module;
            return nullptr;
        }
        auto exports = registrar.build();
        if (!exports || exports->symbols.empty()) {
            diagnostics = "static C++ plugin exported no symbols: ";
            diagnostics += module;
            return nullptr;
        }
        exports->moduleVersion = descriptor.moduleVersion;
        exports->exportContractHash = descriptor.exportContractHash;
        exports->reflectionRegistryHash = descriptor.reflectionRegistryHash;
        exports->requiredServices = descriptor.requiredServices;
        return exports;
    }
    return nullptr;
}

} // namespace arp::scriptvm
