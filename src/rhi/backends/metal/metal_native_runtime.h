#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/gpu_frame_capture.h"

#include <memory>
#include <string>

namespace arp::rhi_metal {

struct MetalRhiNativeRuntimeBoundaryReport {
    bool objectiveCxxTranslationUnit = false;
    bool appleFrameworkHeadersIsolated = true;
    bool nativeDeviceFactoryAvailable = false;
    bool drawableLayerTypeAvailable = false;
    bool runtimeAvailableOnThisPlatform = false;
    std::pmr::string diagnostics;
};

MetalRhiNativeRuntimeBoundaryReport describeMetalRhiNativeRuntimeBoundary();

/// All Metal native payloads in one RHI instance must use one device identity.
/// The opaque shared ownership keeps the Objective-C device alive without
/// exposing Metal headers outside Objective-C++ translation units.
std::shared_ptr<void> metalRhiNativeDeviceIdentity();

/// Returns the borrowed native device pointer represented by the identity.
/// This is for Objective-C++ translation units only.
void* metalRhiNativeDeviceHandle();

/// The Metal backend is the sole owner of MTLCaptureManager and its native
/// device/session objects. Upper layers receive only these L2 results.
rhi::FrameCaptureCapability metalRhiFrameCaptureCapability(rhi::FrameCaptureTool tool);
rhi::FrameCaptureResult beginMetalRhiFrameCapture(const rhi::FrameCaptureRequest& request);
rhi::FrameCaptureResult endMetalRhiFrameCapture();

} // namespace arp::rhi_metal
