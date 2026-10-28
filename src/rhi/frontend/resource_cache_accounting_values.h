#pragma once

#include <cstdint>

namespace arp::rhi {
enum class ResourceCacheAccountingDomain : std::uint8_t {
    Unspecified,
    LogicalDescriptor,
    NativePhysical,
};
} // namespace arp::rhi
