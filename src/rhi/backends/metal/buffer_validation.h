#pragma once

#include "rhi/backends/metal/metal_native_resources.h"

// region metal_tracked_buffer
namespace arp::rhi_metal {

// MetalIndependentTrackedBuffer/1: storage policy and queried device extent
// precede native creation; explicit tracked options and actual query follow.
// This value validator cannot grant synchronization or writable capability.
class MetalRhiTrackedBufferContract final {
public:
    static constexpr std::uint32_t version = 1u, trackedMode = 2u;

    static const char* validateCreation(std::uint64_t bytes,
        MetalRhiNativeStorageMode storage,
        std::uint64_t deviceMaximumBytes) noexcept
    {
        switch (storage) {
        case MetalRhiNativeStorageMode::Private:
        case MetalRhiNativeStorageMode::Shared:
        case MetalRhiNativeStorageMode::Managed:
            break;
        default:
            return "RHI-METAL-TRACKED-BUFFER-STORAGE-POLICY";
        }
        if (!deviceMaximumBytes)
            return "RHI-METAL-TRACKED-BUFFER-DEVICE-LIMIT";
        return bytes && bytes <= deviceMaximumBytes ? nullptr : "RHI-METAL-TRACKED-BUFFER-SIZE";
    }

    static const char* validateQuery(bool available, std::uint32_t mode) noexcept
    {
        if (!available)
            return "RHI-METAL-TRACKED-BUFFER-QUERY-UNAVAILABLE";
        return mode == trackedMode ? nullptr : "RHI-METAL-TRACKED-BUFFER-QUERY-MISMATCH";
    }
};

} // namespace arp::rhi_metal

// endregion metal_tracked_buffer
