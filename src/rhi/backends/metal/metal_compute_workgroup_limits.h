#pragma once

#include <array>
#include <cstdint>

namespace arp::rhi_metal {

// MetalComputeWorkgroupLimits/1. Native facts are supplied by the backend;
// passing this numeric check never supplies recording or submission authority.
struct MetalRhiComputeWorkgroupLimits {
    std::uint64_t x = 0u, y = 0u, z = 0u, total = 0u;

    const char* validate(
        std::uint32_t sizeX, std::uint32_t sizeY, std::uint32_t sizeZ) const noexcept
    {
        const std::array<std::uint64_t, 3u> limits{x, y, z};
        const std::array<std::uint32_t, 3u> sizes{sizeX, sizeY, sizeZ};
        std::uint64_t count = 1u;
        for (std::size_t i = 0u; i < sizes.size(); ++i) {
            if (!sizes[i] || !limits[i] || sizes[i] > limits[i] || count > total / sizes[i])
                return "RHI-METAL-COMPUTE-WORKGROUP-LIMIT";
            count *= sizes[i];
        }
        return nullptr;
    }

    static std::uint32_t deviceInvocationEnvelope(
        std::uint64_t x, std::uint64_t y, std::uint64_t z) noexcept
    {
        if (!x || !y || !z || x > UINT32_MAX || y > UINT32_MAX || z > UINT32_MAX)
            return 0u;
        const auto xy = x > y ? x : y;
        return static_cast<std::uint32_t>(xy > z ? xy : z);
    }
};

} // namespace arp::rhi_metal
