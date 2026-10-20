#pragma once
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
#include <emscripten/emscripten.h>
#endif

namespace arp::rhi_webgpu {
// Emdawn's C++ bridge state and the real spec-defined GPUBuffer.mapState must
// agree at access/publication boundaries. A bridge-only state is not proof.
class WebGpuNativeBufferMapState final {
public:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static bool matches(WGPUBuffer buffer, WGPUBufferMapState expected) noexcept
    {
        if (!buffer || wgpuBufferGetMapState(buffer) != expected)
            return false;
        return EM_ASM_INT(
                   {
                       // clang-format off
                       const b = WebGPU.getJsObject(Number($0));
                       if (!b)
                           return 0;
                       return ($1 && b.mapState === "unmapped") || ($2 && b.mapState === "pending") ||
                              ($3 && b.mapState === "mapped");
                       // clang-format on
                   },
                   buffer,
                   expected == WGPUBufferMapState_Unmapped,
                   expected == WGPUBufferMapState_Pending,
                   expected == WGPUBufferMapState_Mapped)
            != 0;
    }
#endif
};
} // namespace arp::rhi_webgpu
