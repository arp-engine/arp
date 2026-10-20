#pragma once
#include <cstdint>

namespace arp::rhi::semantics {
struct BindlessWorkloadResourceDemand {
    std::uint64_t maxBufferBytes = 0u, maxUniformBindingBytes = 0u, maxStorageBindingBytes = 0u;
    // Zero means all offsets are zero. Each device alignment must divide the
    // nonzero gcd; the gcd is not a minimum ceiling.
    std::uint64_t uniformOffsetDivisor = 0u, storageOffsetDivisor = 0u;
    std::uint32_t texture2DDimension = 0u, texture3DDimension = 0u, textureArrayLayers = 0u;
};

// Cached query facts only. No default ceilings or execution authority.
struct BindlessWorkloadResourceLimits {
    std::uint32_t contractVersion = 0u;
    std::uint64_t maxBufferBytes = 0u, maxUniformBindingBytes = 0u, maxStorageBindingBytes = 0u;
    std::uint32_t uniformOffsetAlignment = 0u, storageOffsetAlignment = 0u;
    std::uint32_t texture2DDimension = 0u, texture3DDimension = 0u, textureArrayLayers = 0u;
};
} // namespace arp::rhi::semantics
