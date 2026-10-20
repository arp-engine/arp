#pragma once

namespace arp::rhi_webgpu {
// Foreign JS exceptions cannot cross C++ shared-owner destructors. Thunks must
// contain only borrowed/POD locals; strong ownership stays at their caller.
class WebGpuNativeInvocation final {
public:
    static bool guarded(void (*)(void*) noexcept, void*) noexcept;
};
} // namespace arp::rhi_webgpu
