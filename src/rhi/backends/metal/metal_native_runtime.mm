#include "rhi/backends/metal/metal_native_runtime.h"

#include <Metal/MTLCaptureManager.h>
#include <Metal/Metal.h>
#include <QuartzCore/CAMetalLayer.h>
#include <filesystem>
#include <memory>

namespace arp::rhi_metal {

namespace {

std::shared_ptr<void> retainNativeDevice(id<MTLDevice> device)
{
    if (device == nil) {
        return {};
    }
    return std::shared_ptr<void>(reinterpret_cast<void*>(device), [](void* nativeObject) {
        if (nativeObject) {
            [reinterpret_cast<id>(nativeObject) release];
        }
    });
}

} // namespace

std::shared_ptr<void> metalRhiNativeDeviceIdentity()
{
    static const auto identity = retainNativeDevice(MTLCreateSystemDefaultDevice());
    return identity;
}

void* metalRhiNativeDeviceHandle()
{
    return metalRhiNativeDeviceIdentity().get();
}

MetalRhiNativeRuntimeBoundaryReport describeMetalRhiNativeRuntimeBoundary()
{
    MetalRhiNativeRuntimeBoundaryReport report;
    report.objectiveCxxTranslationUnit = true;
    report.appleFrameworkHeadersIsolated = true;

    @autoreleasepool {
        id<MTLDevice> device = reinterpret_cast<id<MTLDevice>>(metalRhiNativeDeviceHandle());
        report.nativeDeviceFactoryAvailable = device != nil;
        report.drawableLayerTypeAvailable = [CAMetalLayer class] != Nil;
        report.runtimeAvailableOnThisPlatform =
            report.nativeDeviceFactoryAvailable && report.drawableLayerTypeAvailable;
    }

    if (!report.runtimeAvailableOnThisPlatform) {
        report.diagnostics =
            "Metal RHI native runtime requires an Apple platform with a Metal device";
    }
    return report;
}

rhi::FrameCaptureCapability metalRhiFrameCaptureCapability(const rhi::FrameCaptureTool tool)
{
    if (tool != rhi::FrameCaptureTool::NativeGpuTrace) {
        return rhi::unavailableFrameCaptureCapability(
            rhi::Backend::Metal, tool, "ARP-RHI-FRAME-CAPTURE-TOOL-UNSUPPORTED");
    }
    const auto runtime = describeMetalRhiNativeRuntimeBoundary();
    if (!runtime.runtimeAvailableOnThisPlatform) {
        return rhi::unavailableFrameCaptureCapability(
            rhi::Backend::Metal, tool, "ARP-RHI-METAL-FRAME-CAPTURE-RUNTIME-UNAVAILABLE");
    }

    @autoreleasepool {
        auto* manager = [MTLCaptureManager sharedCaptureManager];
        if (manager == nil
            || ![manager supportsDestination:MTLCaptureDestinationGPUTraceDocument]) {
            return rhi::unavailableFrameCaptureCapability(
                rhi::Backend::Metal, tool, "ARP-RHI-METAL-FRAME-CAPTURE-GPUTRACE-UNAVAILABLE");
        }
    }

    rhi::FrameCaptureCapability capability;
    capability.backend = rhi::Backend::Metal;
    capability.tool = tool;
    capability.mode = rhi::CapabilityMode::Native;
    capability.available = true;
    capability.diagnostics = "ARP-RHI-METAL-FRAME-CAPTURE-NATIVE";
    return capability;
}

rhi::FrameCaptureResult beginMetalRhiFrameCapture(const rhi::FrameCaptureRequest& request)
{
    const auto capability = metalRhiFrameCaptureCapability(request.tool);
    if (!capability.available) {
        return rhi::unavailableFrameCaptureResult(capability.diagnostics.c_str());
    }
    if (request.outputPath.empty()) {
        return rhi::unavailableFrameCaptureResult(
            "ARP-RHI-METAL-FRAME-CAPTURE-OUTPUT-PATH-MISSING");
    }

    const std::filesystem::path outputPath(request.outputPath.c_str());
    if (!outputPath.is_absolute() || !outputPath.parent_path().is_absolute()
        || !std::filesystem::is_directory(outputPath.parent_path())) {
        return rhi::unavailableFrameCaptureResult(
            "ARP-RHI-METAL-FRAME-CAPTURE-OUTPUT-PATH-INVALID");
    }

    @autoreleasepool {
        auto* manager = [MTLCaptureManager sharedCaptureManager];
        if (manager == nil || manager.isCapturing) {
            return rhi::unavailableFrameCaptureResult("ARP-RHI-METAL-FRAME-CAPTURE-ALREADY-ACTIVE");
        }
        auto* descriptor = [[MTLCaptureDescriptor alloc] init];
        descriptor.captureObject = reinterpret_cast<id<MTLDevice>>(metalRhiNativeDeviceHandle());
        descriptor.destination = MTLCaptureDestinationGPUTraceDocument;
        descriptor.outputURL =
            [NSURL fileURLWithPath:[NSString stringWithUTF8String:outputPath.c_str()]];
        NSError* error = nil;
        const BOOL started = descriptor.captureObject != nil &&
            [manager startCaptureWithDescriptor:descriptor error:&error];
        [descriptor release];
        if (!started) {
            return rhi::unavailableFrameCaptureResult("ARP-RHI-METAL-FRAME-CAPTURE-START-FAILED");
        }
    }

    rhi::FrameCaptureResult result;
    result.state = rhi::FrameCaptureState::Capturing;
    result.diagnostics = "ARP-RHI-METAL-FRAME-CAPTURE-ACTIVE";
    return result;
}

rhi::FrameCaptureResult endMetalRhiFrameCapture()
{
    @autoreleasepool {
        auto* manager = [MTLCaptureManager sharedCaptureManager];
        if (manager == nil || !manager.isCapturing) {
            return rhi::unavailableFrameCaptureResult("ARP-RHI-METAL-FRAME-CAPTURE-NOT-ACTIVE");
        }
        [manager stopCapture];
    }

    rhi::FrameCaptureResult result;
    result.state = rhi::FrameCaptureState::Completed;
    result.diagnostics = "ARP-RHI-METAL-FRAME-CAPTURE-COMPLETED";
    return result;
}

} // namespace arp::rhi_metal
