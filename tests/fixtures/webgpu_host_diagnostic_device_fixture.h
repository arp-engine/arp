#pragma once

#include "rhi/backends/webgpu/webgpu_device.h"

namespace arp::tests {

// Explicitly accepts HostDiagnosticDevice/1 for host-only contract evidence.
// A live browser device must use the original production factory.
inline core::UniquePtr<rhi::GpuDevice> createWebGpuContractDevice(
    const rhi_webgpu::WebGpuRhiDeviceDesc& desc)
{
    return rhi_webgpu::webGpuNativeDeviceAvailable()
        ? rhi_webgpu::createWebGpuRhiDevice(desc)
        : rhi_webgpu::createWebGpuRhiHostDiagnosticDevice(desc, 1u);
}

} // namespace arp::tests
