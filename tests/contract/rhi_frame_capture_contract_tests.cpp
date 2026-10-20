#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/gpu_frame_capture.h"

#include <cassert>
#include <cstdio>

int main()
{
    using namespace arp::rhi;

    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::WebGPU}) {
        const auto capability = unavailableFrameCaptureCapability(
            backend, FrameCaptureTool::NativeGpuTrace, "ARP-RHI-FRAME-CAPTURE-UNSUPPORTED");
        assert(capability.backend == backend);
        assert(capability.tool == FrameCaptureTool::NativeGpuTrace);
        assert(capability.mode == CapabilityMode::DiagnosticFailure);
        assert(!capability.available);
    }

    const auto metalCapability =
        arp::rhi_metal::metalRhiFrameCaptureCapability(FrameCaptureTool::NativeGpuTrace);
    assert(metalCapability.backend == Backend::Metal);
    assert(metalCapability.tool == FrameCaptureTool::NativeGpuTrace);
#if defined(__APPLE__)
    assert((metalCapability.available && metalCapability.mode == CapabilityMode::Native)
        || (!metalCapability.available
            && metalCapability.mode == CapabilityMode::DiagnosticFailure));
#else
    assert(!metalCapability.available);
    assert(metalCapability.mode == CapabilityMode::DiagnosticFailure);
#endif

    const auto missingPath = arp::rhi_metal::beginMetalRhiFrameCapture({});
    assert(missingPath.state == FrameCaptureState::DiagnosticFailure);
    assert(!missingPath.success());

    std::printf("FrameCapture/1 metal=%s capability=%s missing-path=%s\n",
        metalCapability.available ? "Native" : "DiagnosticFailure",
        metalCapability.diagnostics.c_str(),
        missingPath.diagnostics.c_str());

    return 0;
}
