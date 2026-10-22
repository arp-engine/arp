#pragma once

#include "core/memory/cpu_allocator.h"
#include "resource/resource_manager.h"

#include <cstddef>
#include <cstdint>

namespace arp::resource {

using ResourceGeneration = std::uint64_t;

struct ResourceReloadTelemetry {
    ResourceGeneration generation = 0u;
    std::uint32_t oldContentHash = 0u;
    std::uint32_t newContentHash = 0u;
    bool committed = false;
    bool rolledBack = false;
    core::pmr::Vector<core::pmr::String> invalidatedDependents{&core::defaultCpuMemoryResource()};
    std::uint64_t retiredCount = 0u;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

struct ResourceReloadResult {
    bool success = false;
    bool unchangedContent = false;
    ResourceState state = ResourceState::Failed;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    ResourceReloadTelemetry telemetry;
};

class ResourceReloadTransaction;

using ResourceReloadTransactionPtr = core::UniquePtr<ResourceReloadTransaction>;

} // namespace arp::resource
