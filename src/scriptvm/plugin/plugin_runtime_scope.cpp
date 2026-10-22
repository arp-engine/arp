#include "scriptvm/plugin/plugin_runtime_scope.h"

namespace arp::scriptvm {

bool ScriptPluginRuntimeScope::reload(const ReloadBuilder& builder, core::pmr::String& diagnostics)
{
    diagnostics.clear();
    if (!builder) {
        diagnostics = "script runtime reload builder is empty";
        return false;
    }

    ModuleRegistry candidateModules;
    ScriptPluginServiceRegistry candidateServices;
    StaticPluginCatalog candidateCatalog;
    candidateModules.setStaticPluginCatalog(&candidateCatalog, &candidateServices);

    auto candidateGeneration = generation_ + 1u;
    if (candidateGeneration == 0u)
        candidateGeneration = 1u;
    if (!builder(candidateModules,
            candidateServices,
            candidateCatalog,
            candidateGeneration,
            diagnostics)) {
        if (diagnostics.empty())
            diagnostics = "script runtime reload candidate was rejected";
        return false;
    }

    services_ = std::move(candidateServices);
    staticCatalog_ = std::move(candidateCatalog);
    modules_ = std::move(candidateModules);
    generation_ = candidateGeneration;
    // candidateModules pointed at its own members; repair those pointers
    // after committing the moved containers.
    modules_.setStaticPluginCatalog(&staticCatalog_, &services_);
    return true;
}

} // namespace arp::scriptvm
