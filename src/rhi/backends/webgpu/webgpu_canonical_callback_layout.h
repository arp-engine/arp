#pragma once
#include "rhi/backends/webgpu/native_validation.h"
#include "rhi/frontend/resource_device_native_loss.h"

namespace arp::rhi_webgpu {
// Shared by cold admission and the real canonical callback pool. A native scope
// policy change must alter both the derived capacity and actual slot positions.
struct WebGpuCanonicalCallbackLayout final {
    static constexpr std::size_t recordingFirst = 0u;
    static constexpr std::size_t submissionFirst = WebGpuErrorScopeContract::count;
    static constexpr std::size_t queueIndex = submissionFirst + WebGpuErrorScopeContract::count;
    static constexpr std::size_t lossIndex = queueIndex + 1u;
    static constexpr std::size_t slotCount =
        lossIndex + rhi::OriginalDeviceNativeLossContract::callbackCount;
    static constexpr std::size_t registrationCount =
        lossIndex + rhi::OriginalDeviceNativeLossContract::registrationCount;
    static_assert(rhi::OriginalDeviceNativeLossContract::callbackCount == 1u
        && rhi::OriginalDeviceNativeLossContract::registrationCount == 1u);
};
} // namespace arp::rhi_webgpu
