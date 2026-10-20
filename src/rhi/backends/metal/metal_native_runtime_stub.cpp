#include "rhi/backends/metal/metal_native_runtime.h"

#include <memory>

namespace arp::rhi_metal {

std::shared_ptr<void> metalRhiNativeDeviceIdentity()
{
    return {};
}

void* metalRhiNativeDeviceHandle()
{
    return nullptr;
}

rhi::FrameCaptureCapability metalRhiFrameCaptureCapability(const rhi::FrameCaptureTool tool)
{
    return rhi::unavailableFrameCaptureCapability(
        rhi::Backend::Metal, tool, "ARP-RHI-METAL-FRAME-CAPTURE-RUNTIME-UNAVAILABLE");
}

rhi::FrameCaptureResult beginMetalRhiFrameCapture(const rhi::FrameCaptureRequest&)
{
    return rhi::unavailableFrameCaptureResult("ARP-RHI-METAL-FRAME-CAPTURE-RUNTIME-UNAVAILABLE");
}

rhi::FrameCaptureResult endMetalRhiFrameCapture()
{
    return rhi::unavailableFrameCaptureResult("ARP-RHI-METAL-FRAME-CAPTURE-RUNTIME-UNAVAILABLE");
}

MetalRhiNativeRuntimeBoundaryReport describeMetalRhiNativeRuntimeBoundary()
{
    MetalRhiNativeRuntimeBoundaryReport report;
    report.appleFrameworkHeadersIsolated = true;
    report.diagnostics =
        "Metal RHI native runtime is diagnostic-only outside Apple Objective-C++ builds";
    return report;
}

} // namespace arp::rhi_metal
