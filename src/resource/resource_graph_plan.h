#pragma once

#include "resource/resource_manager.h"

namespace arp::resource {

struct ResourceGraphPlanResult {
    bool success = false;
    core::pmr::Vector<std::size_t> order{&core::defaultCpuMemoryResource()};
    core::pmr::String resourceId{&core::defaultCpuMemoryResource()};
    core::pmr::String resourceType{&core::defaultCpuMemoryResource()};
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

ARP_API ResourceGraphPlanResult buildResourceGraphPlan(const ResourceGraphRequest& request);

} // namespace arp::resource
