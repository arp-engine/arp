#pragma once

#include "core/memory/cpu_allocator.h"

#include <string_view>

namespace arp::tools::survivors {

struct PackageInventoryLimits {
    std::size_t requestBytes = 0;
    std::size_t responseBytes = 0;
};

// Compatibility identity for the Survivors v1/v2 wire contract. The shared
// tools/io inventory owns all validation, acquisition and payload handling.
class PackageFileInventory final {
public:
    static core::pmr::String prepare(std::string_view request, PackageInventoryLimits limits);
};

} // namespace arp::tools::survivors
