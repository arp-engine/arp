#pragma once
#include "rhi/backends/webgpu/native_validation.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"

namespace arp::rhi_webgpu {
// One native scope-stack transaction; all userdata is cold core-owned.
class WebGpuNativeErrorScopeBatch final {
public:
    static constexpr std::uint32_t count = WebGpuErrorScopeContract::count;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void push(WGPUDevice device) noexcept
    {
        for (const auto filter : WebGpuErrorScopeContract::filters) {
            switch (filter) {
            case WebGpuErrorScopeContract::Filter::Internal:
                wgpuDevicePushErrorScope(device, WGPUErrorFilter_Internal);
                break;
            case WebGpuErrorScopeContract::Filter::OutOfMemory:
                wgpuDevicePushErrorScope(device, WGPUErrorFilter_OutOfMemory);
                break;
            case WebGpuErrorScopeContract::Filter::Validation:
                wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
                break;
            }
        }
    }

    template <class Owner, class Callback, std::size_t N>
    static void pop(WGPUDevice device,
        const std::shared_ptr<Owner>& owner,
        std::array<core::UniquePtr<Callback>, N>& callbacks,
        std::size_t first,
        WGPUPopErrorScopeCallback complete) noexcept
    {
        for (std::size_t i = first; i < first + count; ++i) {
            callbacks[i]->owner = owner;
            auto info = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
            info.mode = WGPUCallbackMode_AllowSpontaneous;
            info.callback = complete;
            info.userdata1 = callbacks[i].release();
            (void)wgpuDevicePopErrorScope(device, info);
        }
    }
#endif
};
} // namespace arp::rhi_webgpu
