#pragma once
#include "core/cpu_allocator.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/semantics/render_output_observation.h"

#include <cstdint>
#include <string>

namespace arp::rhi_metal {

struct MetalRhiAppleRuntimeSmokeDesc {
    MetalRhiDeviceDesc deviceDesc;
    std::pmr::string surfaceName = "MetalRuntimeSmokeLayer";
    rhi::Extent2D extent{320u, 180u};
    rhi::TextureFormat colorFormat = rhi::TextureFormat::Bgra8Unorm;
    bool exerciseResize = true;
    bool exerciseLifecycle = true;
    bool exerciseNativeSync = true;
};

struct MetalRhiAppleRuntimeSmokeReport {
    bool success = false;
    std::pmr::string diagnostics;
    bool runtimeAvailable = false;
    bool diagnosticOnly = false;
    bool publicDevicePathExercised = false;
    bool nativeResourceStatsObserved = false;
    bool nativeCommandStatsObserved = false;
    bool nativeQueueSubmitObserved = false;
    bool nativeDrawableAcquireObserved = false;
    bool nativeDrawablePresentObserved = false;
    bool nativeResizeObserved = false;
    bool nativeLifecycleObserved = false;
    bool nativeBufferCreated = false;
    bool nativeBufferReadbackObserved = false;
    bool nativeTextureCreated = false;
    bool nativeTextureReadbackObserved = false;
    bool nativeTextureViewCreated = false;
    bool nativeSamplerCreated = false;
    bool nativeCommandBufferCreated = false;
    bool nativeBlitEncoderCreated = false;
    bool nativeBlitCopyEncoded = false;
    bool nativeCommandBufferFinished = false;
    bool nativeSyncPayloadCreated = false;
    bool nativeSyncSignalSubmitObserved = false;
    bool nativeSyncWaitSubmitObserved = false;
    bool nativeSyncHostWaitObserved = false;
    std::uint64_t nativeSyncCompletedValue = 0;
    bool nativeHandleExposedToUpperLayer = false;
    bool appleRuntimeRenderOutputHashCaptured = false;
    const char* renderOutputEvidenceDiagnostic =
        arp::rhi::semantics::RenderOutputObservationContract::unavailableDiagnostic;
    std::uint32_t renderOutputHashCaptureCount = 0;
    std::uint64_t deterministicRenderOutputHash = 0;
    std::uint64_t deterministicReadbackHash = 0;
    bool renderOutputHashIncludesDrawablePresent = false;
    bool appleRuntimeHashPlatformGap = false;
    std::uint32_t queueSubmitCount = 0;
    std::uint32_t boundedFrameSlotCount = 0;
};

MetalRhiAppleRuntimeSmokeReport runMetalRhiAppleRuntimeSmoke(
    const MetalRhiAppleRuntimeSmokeDesc& desc);

} // namespace arp::rhi_metal
