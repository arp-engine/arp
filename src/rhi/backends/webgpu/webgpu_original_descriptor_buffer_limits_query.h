#pragma once

#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/frontend/descriptor_buffer_write_validation.h"

#include <limits>
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
#include <emscripten/emscripten.h>
#endif

namespace arp::rhi_webgpu {
// Cold exact-device query only. The accepted factories alone can stamp the
// returned scalar facts in a registered wrapper; these values confer no custody.
class WebGpuOriginalDescriptorBufferLimitsQuery final {
public:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static rhi::OriginalDescriptorBufferBindingContract::Limits query(WGPUDevice device) noexcept
    {
        using Limits = rhi::OriginalDescriptorBufferBindingContract::Limits;
        if (!device || webGpuNativeDeviceLost())
            return {};
        Limits result;
        result.contractVersion = 1u;
        const auto u32Maximum = static_cast<double>(std::numeric_limits<std::uint32_t>::max());
        result.uniformOffsetAlignment = static_cast<std::uint32_t>(
            positiveInteger(device, "minUniformBufferOffsetAlignment", u32Maximum));
        result.storageOffsetAlignment = static_cast<std::uint32_t>(
            positiveInteger(device, "minStorageBufferOffsetAlignment", u32Maximum));
        // JS safe-integer representability is checked before the C++ conversion.
        // A zero maximum selects that specified JS bound, not a device default.
        result.maxUniformBindingBytes =
            static_cast<std::uint64_t>(positiveInteger(device, "maxUniformBufferBindingSize", 0.0));
        result.maxStorageBindingBytes =
            static_cast<std::uint64_t>(positiveInteger(device, "maxStorageBufferBindingSize", 0.0));
        return result;
    }

private:
    static double positiveInteger(WGPUDevice device, const char* property, double maximum) noexcept
    {
        return EM_ASM_DOUBLE(
            {
                // clang-format off
                try {
                    const device = WebGPU.getJsObject(Number($0));
                    if (!device || device !== Module.preinitializedWebGPUDevice)
                        return 0;
                    const limits = device.limits;
                    const value = limits && limits[UTF8ToString($1)];
                    const bound = $2 > 0 ? $2 : Number.MAX_SAFE_INTEGER;
                    return Number.isSafeInteger(value) && value > 0 && value <= bound ? value : 0;
                } catch (_) {
                    return 0;
                }
                // clang-format on
            },
            device,
            property,
            maximum);
    }
#endif
};
} // namespace arp::rhi_webgpu
