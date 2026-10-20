#pragma once

#include "rhi/backends/webgpu/webgpu_native_completion_receipt.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/backends/webgpu/webgpu_native_runtime.h"

#include <chrono>

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
#include <emscripten/emscripten.h>
#endif

namespace arp::rhi_webgpu {
// Cold original creation only. Scope transaction is released before awaiting
// genuine callbacks; accepted actor ownership is independent of this deadline.
class WebGpuConformanceArtifactWait final {
public:
    template <class Creation>
    static const char* initialize(
        const std::shared_ptr<Creation>& creation, std::uint64_t timeoutNanoseconds)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const auto now = std::chrono::steady_clock::now();
        if (!timeoutNanoseconds || timeoutNanoseconds > INT64_MAX)
            return "RHI-CONFORMANCE-CLEAR-NATIVE-BUDGET";
        const auto timeout = std::chrono::nanoseconds(timeoutNanoseconds);
        if (timeout > std::chrono::steady_clock::time_point::max() - now)
            return "RHI-CONFORMANCE-CLEAR-NATIVE-BUDGET";
        const auto deadline = now + timeout;
        auto device = emscripten_webgpu_get_device();
        if (!device
            || !WebGpuNativeCompletionReceipt::claimScopeTransaction(device, creation.get())) {
            if (device)
                wgpuDeviceRelease(device);
            return "RHI-CONFORMANCE-CLEAR-SCOPE-TRANSACTION-BUSY";
        }
        const auto* id = Creation::initialize(creation);
        const bool released =
            WebGpuNativeCompletionReceipt::releaseScopeTransaction(device, creation.get());
        wgpuDeviceRelease(device);
        if (!released)
            return "RHI-CONFORMANCE-CLEAR-SCOPE-TRANSACTION-IDENTITY";
        if (id)
            return id;
        while (!creation->scopesObserved() && !webGpuNativeDeviceLost()
            && std::chrono::steady_clock::now() < deadline)
            emscripten_sleep(0);
        if (!creation->scopesObserved() && !webGpuNativeDeviceLost())
            return "RHI-CONFORMANCE-ARTIFACT-CREATION-DEADLINE";
        return Creation::validateReadyOriginal(creation);
#else
        (void)creation;
        (void)timeoutNanoseconds;
        return "RHI-CONFORMANCE-CLEAR-WEBGPU-NATIVE-UNAVAILABLE";
#endif
    }
};
} // namespace arp::rhi_webgpu
