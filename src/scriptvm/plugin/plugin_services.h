#pragma once

#include "core/memory/cpu_allocator.h"

namespace arp::scriptvm {

// Services are supplied by the embedding runtime and are deliberately
// addressed by a stable id/version pair. Plugins receive only the services
// they declare in their manifest; they do not reach back into ModuleRegistry
// or construct a second host-module abstraction.
struct ScriptPluginService {
    core::pmr::String serviceId{&arp::core::defaultCpuMemoryResource()};
    std::uint32_t version = 0;
    void* value = nullptr;
};

struct ScriptPluginServiceRequirement {
    core::pmr::String serviceId{&arp::core::defaultCpuMemoryResource()};
    std::uint32_t version = 0u;
};

class ARP_API ScriptPluginServiceRegistry final {
public:
    bool provide(core::pmr::String serviceId, std::uint32_t version, void* value);
    void clear();
    void* find(const core::pmr::String& serviceId, std::uint32_t minimumVersion = 1u) const;
    std::uint32_t versionOf(const core::pmr::String& serviceId) const;

    template <typename T>
    T* find(const core::pmr::String& serviceId, std::uint32_t minimumVersion = 1u) const
    {
        return static_cast<T*>(find(serviceId, minimumVersion));
    }

private:
    core::pmr::Vector<ScriptPluginService> services_{&arp::core::defaultCpuMemoryResource()};
};

} // namespace arp::scriptvm
