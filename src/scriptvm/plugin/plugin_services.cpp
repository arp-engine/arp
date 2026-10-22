#include "scriptvm/plugin/plugin_services.h"

namespace arp::scriptvm {

bool ScriptPluginServiceRegistry::provide(
    core::pmr::String serviceId, std::uint32_t version, void* value)
{
    if (serviceId.empty() || version == 0u || value == nullptr)
        return false;
    for (auto& service : services_) {
        if (service.serviceId == serviceId) {
            if (service.version != version)
                return false;
            service.value = value;
            return true;
        }
    }
    services_.push_back(ScriptPluginService{std::move(serviceId), version, value});
    return true;
}

void ScriptPluginServiceRegistry::clear()
{
    services_.clear();
}

void* ScriptPluginServiceRegistry::find(
    const core::pmr::String& serviceId, std::uint32_t minimumVersion) const
{
    for (const auto& service : services_) {
        if (service.serviceId == serviceId && service.version >= minimumVersion)
            return service.value;
    }
    return nullptr;
}

std::uint32_t ScriptPluginServiceRegistry::versionOf(const core::pmr::String& serviceId) const
{
    for (const auto& service : services_) {
        if (service.serviceId == serviceId)
            return service.version;
    }
    return 0u;
}

} // namespace arp::scriptvm
