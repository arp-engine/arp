#pragma once

#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_runtime.h"

namespace arp::tests {

// Explicitly accepts HostDiagnosticDevice/1 for host-only contract evidence.
// Native tests continue through the original production factory.
inline core::UniquePtr<rhi::GpuDevice> createMetalContractDevice(
    const rhi_metal::MetalRhiDeviceDesc& desc)
{
    return rhi_metal::describeMetalRhiNativeRuntimeBoundary().runtimeAvailableOnThisPlatform
        ? rhi_metal::createMetalRhiDevice(desc)
        : rhi_metal::createMetalRhiHostDiagnosticDevice(desc, 1u);
}

} // namespace arp::tests
