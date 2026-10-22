#include "core/memory/cpu_allocator.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/frontend/frame_slot_state_machine.h"

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <utility>

namespace arp::rhi_metal {

namespace {

bool isMetalSwapchainFormatSupported(rhi::TextureFormat format)
{
    return format == rhi::TextureFormat::Bgra8Unorm || format == rhi::TextureFormat::Rgba8Unorm
        || format == rhi::TextureFormat::Rgba8UnormSrgb
        || format == rhi::TextureFormat::Rgba16Float;
}

rhi::SwapchainDesc normalizeMetalSwapchainDesc(rhi::SwapchainDesc desc)
{
    // Metal's drawable layer and this backend's frame-slot state machine are
    // both deliberately bounded to two live images.
    desc.imageCount = 2u;
    return desc;
}

class MetalRhiSwapchain final : public rhi::Swapchain {
public:
    MetalRhiSwapchain(rhi::SwapchainDesc desc,
        std::shared_ptr<MetalRhiNativeSwapchainPayload> nativePayload,
        std::shared_ptr<MetalRhiNativeSwapchainStats> swapchainStats,
        std::shared_ptr<MetalRhiResourceRegistry> registry)
        : desc_(normalizeMetalSwapchainDesc(std::move(desc))),
          nativePayload_(std::move(nativePayload)), swapchainStats_(std::move(swapchainStats)),
          registry_(std::move(registry))
    {
        if (desc_.imageCount < 2) {
            desc_.imageCount = 2;
        }
        if (swapchainStats_) {
            swapchainStats_->boundedFrameSlotCount = maxFramesInFlight();
        }
    }

    std::uint32_t imageCount() const override { return desc_.imageCount; }

    std::uint32_t maxFramesInFlight() const override { return 2; }

    rhi::AcquiredFrame acquireNextFrame() override
    {
        recycleCompletedFrameSlots();
        if (!nativePayload_) {
            return rhi::makeAcquireFailureResult("RHI-METAL-DRAWABLE-NATIVE-PAYLOAD-MISSING",
                slots_.frameSerial(),
                0,
                rhi::AcquireStatus::SurfaceLost);
        }
        {
            std::pmr::string diagnostics;
            if (nativePayload_->consumePresentFailure(diagnostics)) {
                return rhi::makeAcquireFailureResult(
                    diagnostics, slots_.frameSerial(), 0, rhi::AcquireStatus::SurfaceLost);
            }
        }
        if (!surfaceAvailable_) {
            return rhi::makeAcquireFailureResult("Metal RHI drawable surface is unavailable",
                slots_.frameSerial(),
                0,
                rhi::AcquireStatus::SurfaceLost);
        }
        const auto acquired = slots_.acquireSlot();
        if (!acquired.success) {
            return rhi::makeAcquireFailureResult(
                "Metal RHI drawable has no free double-buffer frame slot",
                slots_.frameSerial(),
                0,
                rhi::AcquireStatus::NotReady);
        }
        resetFrameSlotCompletion(acquired.slot);
        frameSlotSerials_[acquired.slot] = acquired.serial;
        std::pmr::string diagnostics;
        if (!nativePayload_->acquireDrawable(acquired.slot, diagnostics)) {
            slots_.clearSlot(acquired.slot);
            frameSlotSerials_[acquired.slot] = 0u;
            resetFrameSlotCompletion(acquired.slot);
            return rhi::makeAcquireFailureResult(
                diagnostics, slots_.frameSerial(), acquired.slot, rhi::AcquireStatus::SurfaceLost);
        }
        return rhi::makeAcquireSuccessResult(acquired.serial, acquired.slot);
    }

    bool isFrameAcquired(const rhi::AcquiredFrame& frame) const noexcept override
    {
        return frame.success && slots_.slotInFlight(frame.frameIndex, frame.frameSerial)
            && slots_.slotAcquired(frame.frameIndex, frame.frameSerial);
    }

    rhi::AcquiredSwapchainImage acquiredImage(const rhi::AcquiredFrame& frame) override
    {
        if (!isFrameAcquired(frame)) {
            return {false,
                "Metal RHI acquired drawable image requires a live acquired frame",
                {},
                nullptr};
        }
        if (!nativePayload_ || !registry_) {
            return {
                false, "Metal RHI acquired drawable image requires backend ownership", {}, nullptr};
        }
        std::pmr::string diagnostics;
        auto nativeTexture = nativePayload_->acquiredDrawableTexture(frame.frameIndex, diagnostics);
        if (!nativeTexture) {
            return {false, diagnostics, {}, nullptr};
        }
        const auto imageId = rhi::allocateGpuResourceId();
        const rhi::TextureDesc textureDesc{
            "MetalSwapchainImage",
            {desc_.extent.width, desc_.extent.height, 1u},
            desc_.format,
            rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::CopySrc
                | rhi::TextureUsage::CopyDst,
            rhi::MemoryUsage::GpuOnly,
            rhi::ResourceState::Undefined,
            1u,
            1u,
            1u,
            false,
        };
        const rhi::ResourceAllocation allocation{
            rhi::ResourceMemoryClass::DeviceLocal,
            0,
            false,
            true,
        };
        auto texture = createMetalRhiTexture(
            imageId, textureDesc, allocation, std::move(nativeTexture), registry_);
        if (!texture) {
            return {false, "Metal RHI acquired drawable texture wrapping failed", {}, nullptr};
        }
        registry_->registerTexture(*texture);
        return {
            true,
            "",
            {texture->id(),
                frame.frameIndex,
                frame.frameSerial,
                desc_.extent,
                desc_.format,
                rhi::ResourceState::Present},
            std::move(texture),
        };
    }

    rhi::FrameDiscardResult discardFrame(const rhi::AcquiredFrame& frame) override
    {
        if (!isFrameAcquired(frame)) {
            return rhi::makeFrameDiscardFailureResult(
                "Metal RHI drawable discard frame does not match a live acquired frame",
                frame.frameSerial,
                frame.frameIndex);
        }
        if (!nativeLifecycleAvailable()) {
            return rhi::makeFrameDiscardFailureResult(
                "RHI-METAL-DRAWABLE-NATIVE-RUNTIME-UNAVAILABLE",
                frame.frameSerial,
                frame.frameIndex,
                rhi::FrameDiscardStatus::SurfaceLost);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->discardDrawable(frame.frameIndex, diagnostics)) {
            slots_.clearSlot(frame.frameIndex);
            frameSlotSerials_[frame.frameIndex] = 0u;
            resetFrameSlotCompletion(frame.frameIndex);
            return rhi::makeFrameDiscardFailureResult(diagnostics,
                frame.frameSerial,
                frame.frameIndex,
                rhi::FrameDiscardStatus::SurfaceLost);
        }
        slots_.clearSlot(frame.frameIndex);
        frameSlotSerials_[frame.frameIndex] = 0u;
        resetFrameSlotCompletion(frame.frameIndex);
        return rhi::makeFrameDiscardSuccessResult(frame.frameSerial, frame.frameIndex);
    }

    rhi::PresentResult present(const rhi::AcquiredFrame& frame,
        const rhi::FramePresentationCompletion& completion = {}) override
    {
        {
            std::pmr::string diagnostics;
            if (nativePayload_ && nativePayload_->consumePresentFailure(diagnostics)) {
                return rhi::makePresentFailureResult(diagnostics,
                    frame.frameSerial,
                    frame.frameIndex,
                    rhi::PresentStatus::SurfaceLost);
            }
        }
        if (!surfaceAvailable_) {
            return rhi::makePresentFailureResult(
                "Metal RHI drawable present blocked by unavailable surface",
                frame.frameSerial,
                frame.frameIndex,
                rhi::PresentStatus::SurfaceLost);
        }
        if (!isFrameAcquired(frame)) {
            return rhi::makePresentFailureResult(
                "Metal RHI drawable present frame does not match a live acquired frame",
                frame.frameSerial,
                frame.frameIndex);
        }
        if (!nativeLifecycleAvailable()) {
            return rhi::makePresentFailureResult("RHI-METAL-DRAWABLE-NATIVE-RUNTIME-UNAVAILABLE",
                frame.frameSerial,
                frame.frameIndex,
                rhi::PresentStatus::SurfaceLost);
        }
        if (!slots_.markPresented(frame.frameIndex, frame.frameSerial)) {
            return rhi::makePresentFailureResult("Metal RHI drawable present frame is not acquired",
                frame.frameSerial,
                frame.frameIndex);
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->presentDrawable(
                frame.frameIndex, frameSlotCompletions_[frame.frameIndex], diagnostics)) {
            slots_.clearSlot(frame.frameIndex);
            frameSlotSerials_[frame.frameIndex] = 0u;
            resetFrameSlotCompletion(frame.frameIndex);
            return rhi::makePresentFailureResult(
                diagnostics, frame.frameSerial, frame.frameIndex, rhi::PresentStatus::SurfaceLost);
        }
        // Presentation completion and render completion are distinct. The
        // slot cannot be reused until both backend-private presentation and
        // the exact L2 graph submission have retired.
        frameSlotRenderCompletions_[frame.frameIndex] = completion.renderCompletion;
        // Keep the slot in flight until the backend-private native completion
        // callback runs. A full slot set is an L2 NotReady result, never a
        // blocking wait or permission to allocate another transient frame.
        return rhi::makePresentSuccessResult(frame.frameSerial, frame.frameIndex);
    }

    rhi::SwapchainLifecycleResult setSurfaceAvailable(bool available) override
    {
        recycleCompletedFrameSlots();
        if (slots_.anySlotInFlight())
            return {false,
                "RHI-METAL-SURFACE-MUTATION-IN-FLIGHT",
                false,
                false,
                true,
                false,
                desc_.extent};

        if (!nativeLifecycleAvailable()) {
            return {
                false,
                "RHI-METAL-DRAWABLE-NATIVE-RUNTIME-UNAVAILABLE",
                false,
                false,
                false,
                !available,
                desc_.extent,
            };
        }
        std::pmr::string diagnostics;
        const bool pauseOrResume = available ? nativePayload_->foregroundResume(diagnostics)
                                             : nativePayload_->backgroundPause(diagnostics);
        if (!pauseOrResume) {
            return {
                false,
                diagnostics,
                false,
                false,
                available,
                !available,
                desc_.extent,
            };
        }
        if (!nativePayload_->setDrawableLayerAvailable(available, diagnostics)) {
            return {
                false,
                diagnostics,
                false,
                false,
                available,
                !available,
                desc_.extent,
            };
        }
        if (!available && !nativePayload_->memoryWarning(diagnostics)) {
            return {
                false,
                diagnostics,
                false,
                false,
                true,
                true,
                desc_.extent,
            };
        }
        surfaceAvailable_ = available;
        clearFrameSlots();
        return {true, "", available, available, available, !available, desc_.extent};
    }

    rhi::SwapchainLifecycleResult resize(const rhi::Extent2D& extent) override
    {
        recycleCompletedFrameSlots();
        if (slots_.anySlotInFlight())
            return {false,
                "RHI-METAL-SURFACE-MUTATION-IN-FLIGHT",
                false,
                false,
                true,
                false,
                desc_.extent};

        if (extent.width == 0 || extent.height == 0) {
            return {
                false,
                "Metal RHI drawable resize extent is invalid",
                surfaceAvailable_,
                surfaceAvailable_,
                false,
                false,
                desc_.extent,
            };
        }
        if (!nativeLifecycleAvailable()) {
            return {
                false,
                "RHI-METAL-DRAWABLE-NATIVE-RUNTIME-UNAVAILABLE",
                surfaceAvailable_,
                surfaceAvailable_,
                false,
                false,
                desc_.extent,
            };
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->resizeDrawableLayer(extent, diagnostics)) {
            return {
                false,
                diagnostics,
                surfaceAvailable_,
                surfaceAvailable_,
                true,
                false,
                desc_.extent,
            };
        }
        desc_.extent = extent;
        clearFrameSlots();
        return {true, "", surfaceAvailable_, surfaceAvailable_, true, true, desc_.extent};
    }

    rhi::SwapchainLifecycleResult reconfigure(
        const rhi::SwapchainReconfigureRequest& request) override
    {
        recycleCompletedFrameSlots();
        if (slots_.anySlotInFlight())
            return {false,
                "RHI-METAL-SURFACE-MUTATION-IN-FLIGHT",
                false,
                false,
                true,
                false,
                desc_.extent};

        const auto normalizedDesc = normalizeMetalSwapchainDesc(request.desc);
        if (normalizedDesc.extent.width == 0 || normalizedDesc.extent.height == 0
            || !isMetalSwapchainFormatSupported(normalizedDesc.format)) {
            return {false,
                "RHI-METAL-SWAPCHAIN-RECONFIGURE-DESC-INVALID",
                surfaceAvailable_,
                surfaceAvailable_,
                true,
                false,
                desc_.extent};
        }
        if (!nativeLifecycleAvailable()) {
            return {false,
                "RHI-METAL-DRAWABLE-NATIVE-RUNTIME-UNAVAILABLE",
                surfaceAvailable_,
                surfaceAvailable_,
                true,
                false,
                desc_.extent};
        }
        std::pmr::string diagnostics;
        if (!nativePayload_->reconfigureDrawableLayer(normalizedDesc, diagnostics)) {
            return {false,
                diagnostics,
                surfaceAvailable_,
                surfaceAvailable_,
                true,
                false,
                desc_.extent};
        }
        desc_ = normalizedDesc;
        clearFrameSlots();
        return {true, "", surfaceAvailable_, surfaceAvailable_, true, true, desc_.extent};
    }

    rhi::SwapchainPresentationStatus presentationStatus() const override
    {
        return {desc_.presentationMode,
            rhi::PresentationMode::Auto,
            desc_.presentationMode != rhi::PresentationMode::Auto,
            "metal-layer-managed"};
    }

private:
    void resetFrameSlotCompletion(const std::uint32_t slot)
    {
        if (slot < frameSlotCompletions_.size()) {
            frameSlotCompletions_[slot] =
                arp::core::makeShared<MetalRhiNativeFrameSlotCompletionState>();
            frameSlotRenderCompletions_[slot].reset();
        }
    }

    void recycleCompletedFrameSlots()
    {
        for (std::uint32_t slot = 0u; slot < frameSlotCompletions_.size(); ++slot) {
            if (slots_.slotInFlight(slot, frameSlotSerial(slot)) && frameSlotCompletions_[slot]
                && frameSlotCompletions_[slot]->isComplete()
                && (!frameSlotRenderCompletions_[slot]
                    || frameSlotRenderCompletions_[slot]->isSignaled())) {
                slots_.clearSlot(slot);
                frameSlotSerials_[slot] = 0u;
                resetFrameSlotCompletion(slot);
            }
        }
    }

    std::uint64_t frameSlotSerial(const std::uint32_t slot) const
    {
        // FrameSlotStateMachine intentionally exposes serial validation, not
        // slot internals. Track the exact serial at acquisition for retirement.
        return frameSlotSerials_[slot];
    }

    void clearFrameSlots()
    {
        slots_.clearAllSlots();
        for (std::uint32_t slot = 0u; slot < frameSlotCompletions_.size(); ++slot) {
            frameSlotSerials_[slot] = 0u;
            resetFrameSlotCompletion(slot);
        }
    }

    bool nativeLifecycleAvailable() const
    {
        return nativePayload_ && swapchainStats_ && swapchainStats_->nativeRuntimeBacked;
    }

    rhi::SwapchainDesc desc_;
    std::shared_ptr<MetalRhiNativeSwapchainPayload> nativePayload_;
    std::shared_ptr<MetalRhiNativeSwapchainStats> swapchainStats_;
    std::shared_ptr<MetalRhiResourceRegistry> registry_;
    bool surfaceAvailable_ = true;
    rhi::FrameSlotStateMachine<2> slots_;
    std::array<std::shared_ptr<MetalRhiNativeFrameSlotCompletionState>, 2> frameSlotCompletions_{
        arp::core::makeShared<MetalRhiNativeFrameSlotCompletionState>(),
        arp::core::makeShared<MetalRhiNativeFrameSlotCompletionState>(),
    };
    std::array<std::shared_ptr<const rhi::GpuFence>, 2> frameSlotRenderCompletions_;
    std::array<std::uint64_t, 2> frameSlotSerials_{};
};

} // namespace

arp::core::UniquePtr<rhi::Swapchain> createMetalRhiSwapchain(const rhi::SwapchainDesc& desc,
    std::shared_ptr<MetalRhiNativeSwapchainStats> swapchainStats,
    std::shared_ptr<MetalRhiResourceRegistry> registry)
{
    if (desc.extent.width == 0 || desc.extent.height == 0
        || !isMetalSwapchainFormatSupported(desc.format)) {
        return nullptr;
    }
    if (!swapchainStats) {
        swapchainStats = createMetalRhiNativeSwapchainStats();
    }
    auto normalizedDesc = normalizeMetalSwapchainDesc(desc);
    auto nativePayload = createMetalRhiNativeSwapchainPayload(normalizedDesc, swapchainStats);
    if (describeMetalRhiNativeRuntimeBoundary().runtimeAvailableOnThisPlatform
        && (!nativePayload || !swapchainStats->nativeRuntimeBacked)) {
        return nullptr;
    }
    return arp::core::makeUnique<MetalRhiSwapchain>(std::move(normalizedDesc),
        std::move(nativePayload),
        std::move(swapchainStats),
        std::move(registry));
}

} // namespace arp::rhi_metal
