#include "rhi/backends/metal/metal_runtime_smoke.h"

#include "core/cpu_allocator.h"
#include "core/diagnostic_builder.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_native_runtime.h"

#include <array>
#include <cstring>
#include <utility>

namespace arp::rhi_metal {

namespace {

std::uint64_t hashBytes(const std::uint8_t* bytes, std::size_t count)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (std::size_t index = 0; index < count; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ull;
    }
    return hash == 0u ? 1u : hash;
}

MetalRhiAppleRuntimeSmokeReport failSmoke(
    MetalRhiAppleRuntimeSmokeReport report, std::pmr::string diagnostics)
{
    report.diagnostics = std::move(diagnostics);
    return report;
}

} // namespace

MetalRhiAppleRuntimeSmokeReport runMetalRhiAppleRuntimeSmoke(
    const MetalRhiAppleRuntimeSmokeDesc& desc)
{
    MetalRhiAppleRuntimeSmokeReport report;
    const auto runtime = describeMetalRhiNativeRuntimeBoundary();
    report.runtimeAvailable = runtime.runtimeAvailableOnThisPlatform;
    if (!report.runtimeAvailable) {
        report.diagnosticOnly = true;
        report.appleRuntimeHashPlatformGap = true;
        report.diagnostics = runtime.diagnostics.empty()
            ? "Metal RHI Apple runtime smoke requires an Apple Metal runtime"
            : runtime.diagnostics;
        arp::core::appendDiagnostic(report.diagnostics,
            "Metal screenshot/hash capture is a platform gap until Apple runtime is available");
        return report;
    }

    auto device = createMetalRhiDevice(desc.deviceDesc);
    if (!device) {
        return failSmoke(report, "Metal RHI Apple runtime smoke device creation failed");
    }
    report.publicDevicePathExercised = true;

    auto source = device->createBuffer({
        "MetalRuntimeSmokeSource",
        256,
        rhi::ResourceUsage::CopySrc | rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::CpuToGpu,
        rhi::ResourceState::CopySrc,
    });
    auto destination = device->createBuffer({
        "MetalRuntimeSmokeDestination",
        256,
        rhi::ResourceUsage::CopyDst | rhi::ResourceUsage::CopySrc,
        rhi::MemoryUsage::GpuToCpu,
        rhi::ResourceState::CopyDst,
    });
    auto textureReadbackBuffer = device->createBuffer({
        "MetalRuntimeSmokeTextureReadback",
        2048,
        rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::GpuToCpu,
        rhi::ResourceState::CopyDst,
    });
    auto texture = device->createTexture({
        "MetalRuntimeSmokeColor",
        {4, 4, 1},
        desc.colorFormat,
        rhi::TextureUsage::Sampled | rhi::TextureUsage::RenderAttachment
            | rhi::TextureUsage::CopySrc | rhi::TextureUsage::CopyDst,
        rhi::MemoryUsage::GpuOnly,
        rhi::ResourceState::CopyDst,
    });
    auto textureView = texture ? device->createTextureView(*texture,
                                     {
                                         "MetalRuntimeSmokeColorView",
                                         desc.colorFormat,
                                         rhi::TextureUsage::Sampled | rhi::TextureUsage::CopySrc
                                             | rhi::TextureUsage::CopyDst,
                                         rhi::TextureViewDimension::Texture2D,
                                         0,
                                         1,
                                         0,
                                         1,
                                         rhi::TextureAspect::Color,
                                     })
                               : nullptr;
    auto sampler = device->createSampler({
        "MetalRuntimeSmokeSampler",
        rhi::SamplerFilter::Linear,
        rhi::SamplerFilter::Linear,
        rhi::SamplerAddressMode::ClampToEdge,
        rhi::SamplerAddressMode::ClampToEdge,
        rhi::SamplerAddressMode::ClampToEdge,
    });
    if (!source || !destination || !textureReadbackBuffer || !texture || !textureView || !sampler) {
        return failSmoke(report, "Metal RHI Apple runtime smoke resource creation failed");
    }

    const auto sourcePayload = inspectMetalRhiNativePayload(*source);
    const auto destinationPayload = inspectMetalRhiNativePayload(*destination);
    const auto texturePayload = inspectMetalRhiNativePayload(*texture);
    const auto viewPayload = inspectMetalRhiNativePayload(*textureView);
    const auto samplerPayload = inspectMetalRhiNativePayload(*sampler);
    report.nativeBufferCreated =
        sourcePayload.nativeRuntimeBacked && destinationPayload.nativeRuntimeBacked;
    report.nativeTextureCreated = texturePayload.nativeRuntimeBacked;
    report.nativeTextureViewCreated = viewPayload.nativeRuntimeBacked;
    report.nativeSamplerCreated = samplerPayload.nativeRuntimeBacked;
    report.nativeResourceStatsObserved = report.nativeBufferCreated
        && inspectMetalRhiNativePayload(*textureReadbackBuffer).nativeRuntimeBacked
        && report.nativeTextureCreated && report.nativeTextureViewCreated
        && report.nativeSamplerCreated;
    report.nativeHandleExposedToUpperLayer = sourcePayload.nativeHandleExposedToUpperLayer
        || destinationPayload.nativeHandleExposedToUpperLayer
        || texturePayload.nativeHandleExposedToUpperLayer
        || viewPayload.nativeHandleExposedToUpperLayer
        || samplerPayload.nativeHandleExposedToUpperLayer;
    if (!report.nativeResourceStatsObserved) {
        return failSmoke(report, "Metal RHI Apple runtime smoke did not observe native resources");
    }

    std::array<std::uint8_t, 128> sourceBytes{};
    for (std::size_t index = 0; index < sourceBytes.size(); ++index) {
        sourceBytes[index] = static_cast<std::uint8_t>(index);
    }
    const auto wroteSource = device->writeBuffer(*source, sourceBytes.data(), sourceBytes.size());
    if (!wroteSource.success) {
        return failSmoke(report, wroteSource.diagnostics);
    }
    std::array<std::uint8_t, 1024> textureBytes{};
    for (std::uint32_t row = 0; row < 4u; ++row) {
        for (std::uint32_t column = 0; column < 4u; ++column) {
            const auto offset =
                static_cast<std::size_t>(row) * 256u + static_cast<std::size_t>(column) * 4u;
            textureBytes[offset + 0u] = static_cast<std::uint8_t>(column * 17u);
            textureBytes[offset + 1u] = static_cast<std::uint8_t>(row * 31u);
            textureBytes[offset + 2u] = 0x7fu;
            textureBytes[offset + 3u] = 0xffu;
        }
    }

    auto encoder = device->createCommandEncoder({"MetalRuntimeSmokeCommands"});
    auto transfer = encoder ? encoder->beginTransferPass({"MetalRuntimeSmokeBlit"}) : nullptr;
    if (!encoder || !transfer) {
        return failSmoke(report, "Metal RHI Apple runtime smoke command encoder failed");
    }
    const auto copied = transfer->copyBufferToBuffer({
        source->id(),
        destination->id(),
        0,
        0,
        128,
    });
    const auto uploaded = transfer->uploadTexture({
        "MetalRuntimeSmokeTextureUpload",
        textureView->id(),
        {4u, 4u, 1u},
        {0u, 0u, 0u},
        desc.colorFormat,
        256u,
        4u,
        0u,
        0u,
        {0u, 1u, 0u, 1u, rhi::TextureAspect::Color},
        {},
        textureBytes.data(),
        static_cast<std::uint32_t>(textureBytes.size()),
        rhi::StagingIntent::Upload,
        true,
    });
    const auto textureCopied = transfer->copyTextureToBuffer({
        textureView->id(),
        textureReadbackBuffer->id(),
        {0u, 1u, 0u, 1u, rhi::TextureAspect::Color},
        512u,
        256u,
        4u,
        {4u, 4u, 1u},
    });
    if (!copied.success || !uploaded.success || !textureCopied.success
        || !transfer->end().success) {
        return failSmoke(report, "Metal RHI Apple runtime smoke command recording failed");
    }
    auto commands = encoder->finish();
    if (!commands) {
        return failSmoke(report, encoder->lastValidation().diagnostics);
    }
    if (desc.exerciseNativeSync) {
        auto timeline = device->createTimelineSemaphore();
        if (!timeline) {
            return failSmoke(
                report, "Metal RHI Apple runtime smoke native sync payload creation failed");
        }
        report.nativeSyncPayloadCreated = true;

        const auto submitted =
            device->queue().submit(*commands, nullptr, 0u, timeline.get(), 1u, nullptr);
        if (!submitted.success) {
            return failSmoke(report, submitted.diagnostics);
        }
        report.nativeSyncSignalSubmitObserved = true;
        timeline->hostWait(1u, 1'000'000'000u);
        report.nativeSyncCompletedValue = timeline->getValue();
        report.nativeSyncHostWaitObserved = report.nativeSyncCompletedValue >= 1u;
        if (!report.nativeSyncHostWaitObserved) {
            return failSmoke(
                report, "Metal RHI Apple runtime smoke native sync signal host wait timed out");
        }

        auto syncEncoder = device->createCommandEncoder({"MetalRuntimeSmokeSyncWaitCommands"});
        auto syncTransfer = syncEncoder
            ? syncEncoder->beginTransferPass({"MetalRuntimeSmokeSyncWaitBlit"})
            : nullptr;
        if (!syncEncoder || !syncTransfer) {
            return failSmoke(
                report, "Metal RHI Apple runtime smoke native sync wait encoder failed");
        }
        const auto syncCopied = syncTransfer->copyBufferToBuffer({
            source->id(),
            destination->id(),
            0,
            0,
            64,
        });
        if (!syncCopied.success || !syncTransfer->end().success) {
            return failSmoke(
                report, "Metal RHI Apple runtime smoke native sync wait recording failed");
        }
        auto syncCommands = syncEncoder->finish();
        if (!syncCommands) {
            return failSmoke(report, syncEncoder->lastValidation().diagnostics);
        }
        const auto chainedSubmit =
            device->queue().submit(*syncCommands, timeline.get(), 1u, timeline.get(), 2u, nullptr);
        if (!chainedSubmit.success) {
            return failSmoke(report, chainedSubmit.diagnostics);
        }
        report.nativeSyncWaitSubmitObserved = true;
        timeline->hostWait(2u, 1'000'000'000u);
        report.nativeSyncCompletedValue = timeline->getValue();
        report.nativeSyncHostWaitObserved = report.nativeSyncCompletedValue >= 2u;
        if (!report.nativeSyncHostWaitObserved) {
            return failSmoke(
                report, "Metal RHI Apple runtime smoke native sync chained host wait timed out");
        }
    } else {
        const auto submitted = device->queue().submit(*commands);
        if (!submitted.success) {
            return failSmoke(report, submitted.diagnostics);
        }
    }
    device->waitIdle();

    std::array<std::uint8_t, 128> destinationBytes{};
    std::array<std::uint8_t, 1024> textureReadbackBytes{};
    std::pmr::string readbackDiagnostics;
    const bool bufferReadback = readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*destination),
        destinationBytes.data(),
        destinationBytes.size(),
        0u,
        destination->desc().sizeBytes,
        readbackDiagnostics);
    if (!bufferReadback) {
        return failSmoke(report, readbackDiagnostics);
    }
    const bool textureReadback =
        readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*textureReadbackBuffer),
            textureReadbackBytes.data(),
            textureReadbackBytes.size(),
            512u,
            textureReadbackBuffer->desc().sizeBytes,
            readbackDiagnostics);
    if (!textureReadback) {
        return failSmoke(report, readbackDiagnostics);
    }
    report.nativeBufferReadbackObserved =
        std::memcmp(destinationBytes.data(), sourceBytes.data(), sourceBytes.size()) == 0;
    report.nativeTextureReadbackObserved = true;
    for (std::uint32_t row = 0; row < 4u && report.nativeTextureReadbackObserved; ++row) {
        for (std::uint32_t column = 0; column < 4u; ++column) {
            const auto offset =
                static_cast<std::size_t>(row) * 256u + static_cast<std::size_t>(column) * 4u;
            if (std::memcmp(textureReadbackBytes.data() + offset, textureBytes.data() + offset, 4u)
                != 0) {
                report.nativeTextureReadbackObserved = false;
                break;
            }
        }
    }
    report.deterministicReadbackHash =
        hashBytes(textureReadbackBytes.data(), textureReadbackBytes.size());
    if (!report.nativeBufferReadbackObserved || !report.nativeTextureReadbackObserved) {
        return failSmoke(report, "Metal RHI Apple runtime smoke readback checksum mismatch");
    }

    const auto commandStats = inspectMetalRhiNativeCommandStats(*commands);
    report.nativeCommandBufferCreated = commandStats.nativeCommandBufferCreated;
    report.nativeBlitEncoderCreated = commandStats.nativeBlitEncoderCreated;
    report.nativeBlitCopyEncoded = commandStats.nativeBlitCopyEncoded;
    report.nativeCommandBufferFinished = commandStats.nativeCommandBufferFinished;
    report.nativeCommandStatsObserved = commandStats.nativeRuntimeBacked
        && report.nativeCommandBufferCreated && report.nativeBlitEncoderCreated
        && report.nativeBlitCopyEncoded && report.nativeCommandBufferFinished;
    report.nativeHandleExposedToUpperLayer =
        report.nativeHandleExposedToUpperLayer || commandStats.nativeHandleExposedToUpperLayer;

    const auto queueStats = inspectMetalRhiNativeQueueStats(device->queue());
    report.queueSubmitCount = queueStats.nativeQueueSubmitCount;
    report.nativeQueueSubmitObserved =
        queueStats.nativeRuntimeBacked && queueStats.nativeQueueSubmitCount >= 1u;
    report.nativeHandleExposedToUpperLayer =
        report.nativeHandleExposedToUpperLayer || queueStats.nativeHandleExposedToUpperLayer;

    auto swapchain = device->createSwapchain({
        desc.surfaceName,
        desc.extent,
        desc.colorFormat,
        2,
    });
    if (!swapchain) {
        return failSmoke(report, "Metal RHI Apple runtime smoke swapchain creation failed");
    }
    auto frame = swapchain->acquireNextFrame();
    const auto presented = frame.success
        ? swapchain->present(frame)
        : rhi::makePresentFailureResult(
              "Metal runtime smoke present skipped because acquire failed",
              frame.frameSerial,
              frame.frameIndex);
    const auto resized = desc.exerciseResize
        ? swapchain->resize({desc.extent.width + 1u, desc.extent.height + 1u})
        : rhi::SwapchainLifecycleResult{true, "", true, true, false, false, desc.extent};
    const auto layerLost = desc.exerciseLifecycle
        ? swapchain->setSurfaceAvailable(false)
        : rhi::SwapchainLifecycleResult{true, "", true, true, false, false, desc.extent};

    const auto swapchainStats = inspectMetalRhiNativeSwapchainStats(*device);
    report.nativeDrawableAcquireObserved =
        frame.success && swapchainStats.nativeDrawableAcquireCount >= 1u;
    report.nativeDrawablePresentObserved =
        presented.success && swapchainStats.nativeDrawablePresentCount >= 1u;
    report.nativeResizeObserved =
        resized.success && (!desc.exerciseResize || swapchainStats.nativeResizeCount >= 1u);
    report.nativeLifecycleObserved = layerLost.success
        && (!desc.exerciseLifecycle
            || (layerLost.releaseSurfaceResources && !layerLost.acquireAllowed
                && swapchainStats.layerLossCount >= 1u && swapchainStats.backgroundPauseCount >= 1u
                && swapchainStats.memoryWarningCount >= 1u));
    report.boundedFrameSlotCount = swapchainStats.boundedFrameSlotCount;
    report.nativeHandleExposedToUpperLayer =
        report.nativeHandleExposedToUpperLayer || swapchainStats.nativeHandleExposedToUpperLayer;

    report.success = report.publicDevicePathExercised && report.nativeResourceStatsObserved
        && report.nativeBufferReadbackObserved && report.nativeTextureReadbackObserved
        && report.nativeCommandStatsObserved && report.nativeQueueSubmitObserved
        && (!desc.exerciseNativeSync
            || (report.nativeSyncPayloadCreated && report.nativeSyncSignalSubmitObserved
                && report.nativeSyncWaitSubmitObserved && report.nativeSyncHostWaitObserved))
        && report.nativeDrawableAcquireObserved && report.nativeDrawablePresentObserved
        && report.nativeResizeObserved && report.nativeLifecycleObserved
        && report.boundedFrameSlotCount == 2u && !report.nativeHandleExposedToUpperLayer;
    if (!report.success && report.diagnostics.empty()) {
        report.diagnostics = "Metal RHI Apple runtime smoke did not observe all native telemetry";
    }
    // Present telemetry is not an observation of rendered output pixels.
    return report;
}

} // namespace arp::rhi_metal
