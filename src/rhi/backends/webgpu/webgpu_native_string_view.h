#pragma once

#include "rhi/backends/webgpu/webgpu_native_payloads.h"

#include <string_view>

namespace arp::rhi_webgpu {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
// Borrow the caller's literal or owned bytes through the synchronous API call.
// Converting a literal to an owned PMR string here allocates and leaves native
// descriptors with dangling data after the assignment expression.
inline WGPUStringView webGpuNativeStringView(std::string_view text) noexcept
{
    return {text.data(), text.size()};
}
#endif
} // namespace arp::rhi_webgpu
