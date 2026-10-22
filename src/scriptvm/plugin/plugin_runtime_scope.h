#pragma once

#include "scriptvm/runtime/module_registry.h"

namespace arp::scriptvm {

/// Owns the per-world script registry/service scope. A reload creates a fresh
/// scope and generation; callers never migrate script instances or awaitables
/// from the previous generation.
class ARP_API ScriptPluginRuntimeScope final {
public:
    using ReloadBuilder = std::function<bool(ModuleRegistry&,
        ScriptPluginServiceRegistry&,
        StaticPluginCatalog&,
        std::uint64_t,
        core::pmr::String&)>;

    explicit ScriptPluginRuntimeScope(std::uint64_t generation = 1u)
        : generation_(generation == 0u ? 1u : generation)
    {}

    ModuleRegistry& modules() { return modules_; }

    ScriptPluginServiceRegistry& services() { return services_; }

    StaticPluginCatalog& staticCatalog() { return staticCatalog_; }

    std::uint64_t generation() const { return generation_; }

    // Build a replacement registry/service scope off to the side. The
    // current scope remains fully usable until builder returns true; failed
    // candidates are discarded without changing generation or services.
    bool reload(const ReloadBuilder& builder, core::pmr::String& diagnostics);

    void beginReload()
    {
        services_.clear();
        staticCatalog_.clear();
        modules_ = ModuleRegistry{};
        modules_.setStaticPluginCatalog(&staticCatalog_, &services_);
        ++generation_;
        if (generation_ == 0)
            generation_ = 1;
    }

private:
    ModuleRegistry modules_;
    ScriptPluginServiceRegistry services_;
    StaticPluginCatalog staticCatalog_;
    std::uint64_t generation_ = 1;
};

} // namespace arp::scriptvm
