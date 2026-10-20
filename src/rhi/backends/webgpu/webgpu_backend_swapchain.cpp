#include "core/cpu_allocator.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/backends/webgpu/webgpu_original_canvas.h"
#include "rhi/frontend/frame_slot_state_machine.h"

#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>

namespace arp::rhi_webgpu {

namespace {

const char* presentationRequestName(rhi::PresentationMode mode)
{
    switch (mode) {
    case rhi::PresentationMode::VSync:
        return "vsync";
    case rhi::PresentationMode::LowLatency:
        return "low-latency";
    case rhi::PresentationMode::Immediate:
        return "immediate";
    case rhi::PresentationMode::Auto:
    default:
        return "auto";
    }
}

} // namespace

class WebGpuRhiSwapchain final : public rhi::Swapchain {
public:
    WebGpuRhiSwapchain(rhi::SwapchainDesc desc,
        std::shared_ptr<WebGpuNativeSwapchainPayload> nativePayload,
        std::shared_ptr<WebGpuNativeSwapchainStats> swapchainStats,
        std::shared_ptr<WebGpuRhiResourceRegistry> registry)
        : desc_(std::move(desc)), nativePayload_(std::move(nativePayload)),
          swapchainStats_(std::move(swapchainStats)), registry_(std::move(registry))
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
        if (!surfaceAvailable_) {
            std::pmr::string diagnostics = "WebGPU RHI canvas surface is unavailable";
            if (nativePayload_) {
                nativePayload_->hiddenTabFrameSkip(diagnostics);
            }
            return rhi::makeAcquireFailureResult(
                diagnostics, slots_.frameSerial(), 0, rhi::AcquireStatus::SurfaceLost);
        }
        const auto acquired = slots_.acquireSlot();
        if (!acquired.success) {
            return rhi::makeAcquireFailureResult(
                "WebGPU RHI canvas has no free double-buffer frame slot",
                slots_.frameSerial(),
                0,
                rhi::AcquireStatus::NotReady);
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!nativePayload_->acquireCanvas(acquired.slot, diagnostics)) {
                slots_.clearSlot(acquired.slot);
                return rhi::makeAcquireFailureResult(diagnostics,
                    slots_.frameSerial(),
                    acquired.slot,
                    rhi::AcquireStatus::SurfaceLost);
            }
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
            return {
                false,
                "WebGPU RHI acquired canvas image requires a live acquired frame",
                {},
                nullptr,
            };
        }
        if (!nativePayload_) {
            return {
                false,
                "WebGPU RHI swapchain image requires native payload",
                {},
                nullptr,
            };
        }

        // Transfer canvas texture ownership from swapchain to a GpuTexture.
        auto canvasPayload = nativePayload_->takeCanvasTexture(frame.frameIndex);
        if (!canvasPayload) {
            return {
                false,
                "WebGPU RHI canvas texture unavailable for acquired frame",
                {},
                nullptr,
            };
        }

        // Wrap the canvas WGPUTexture in a GpuTexture the FrameGraph can
        // render to.  This mirrors the Vulkan path: the acquired swapchain
        // image IS the backbuffer.
        if (!registry_) {
            return {
                false,
                "WebGPU RHI swapchain image requires backend resource ID ownership",
                {},
                nullptr,
            };
        }
        const auto imageId = rhi::allocateGpuResourceId();

        const auto textureDesc = WebGpuCanvasTextureContract::describe(desc_);

        WebGpuRhiAllocation allocation;
        allocation.success = true;
        allocation.sizeBytes = 0;

        // Retain the exact backend-issued acquisition beside the native image.
        auto identity = core::makeShared<WebGpuOriginalCanvas>(WebGpuOriginalCanvas::Key{},
            surfaceEpoch_.ticket(),
            imageId,
            frame.frameIndex,
            frame.frameSerial);
        canvasPayload->originalCanvas_ = identity;
        canvasIdentities_[frame.frameIndex] = std::move(identity);
        auto texture = createWebGpuRhiTexture(
            imageId, textureDesc, allocation, std::move(canvasPayload), registry_);
        if (!texture) {
            return {
                false,
                "WebGPU RHI swapchain image texture wrapping failed",
                {},
                nullptr,
            };
        }
        // The acquired canvas texture is an ordinary L2 GpuTexture for the
        // duration of this frame. Register it before command encoding so the
        // public RenderPassDesc resource id lowers to the native attachment.
        if (!registry_) {
            return {false, "WebGPU RHI swapchain image has no resource registry", {}, nullptr};
        }
        registry_->registerTexture(*texture);

        return {
            true,
            "",
            rhi::SwapchainImageRef{
                imageId,
                frame.frameIndex,
                frame.frameSerial,
                desc_.extent,
                textureDesc.format,
                rhi::ResourceState::Present,
            },
            std::move(texture),
        };
    }

    rhi::FrameDiscardResult discardFrame(const rhi::AcquiredFrame& frame) override
    {
        if (!isFrameAcquired(frame)) {
            return rhi::makeFrameDiscardFailureResult(
                "WebGPU RHI canvas discard frame does not match a live acquired frame",
                frame.frameSerial,
                frame.frameIndex);
        }
        const auto identity = canvasIdentities_[frame.frameIndex];
        if (identity && identity->claimed_ && identity->submitted_ && !identity->retirementSafe_)
            return rhi::makeFrameDiscardFailureResult(
                "RHI-WEBGPU-ORIGINAL-CANVAS-WORK-RETAINED", frame.frameSerial, frame.frameIndex);
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!nativePayload_->discardCanvasFrame(frame.frameIndex, diagnostics)) {
                slots_.clearSlot(frame.frameIndex);
                return rhi::makeFrameDiscardFailureResult(diagnostics,
                    frame.frameSerial,
                    frame.frameIndex,
                    rhi::FrameDiscardStatus::SurfaceLost);
            }
        }
        if (auto& identity = canvasIdentities_[frame.frameIndex]) {
            identity->discarded_ = true;
            identity.reset();
        }
        slots_.clearSlot(frame.frameIndex);
        frameSlotRenderCompletions_[frame.frameIndex].reset();
        return rhi::makeFrameDiscardSuccessResult(frame.frameSerial, frame.frameIndex);
    }

    rhi::PresentResult present(const rhi::AcquiredFrame& frame,
        const rhi::FramePresentationCompletion& completion = {}) override
    {
        if (!surfaceAvailable_) {
            return rhi::makePresentFailureResult(
                "WebGPU RHI canvas present blocked by unavailable surface",
                frame.frameSerial,
                frame.frameIndex,
                rhi::PresentStatus::SurfaceLost);
        }
        if (!isFrameAcquired(frame)) {
            return rhi::makePresentFailureResult(
                "WebGPU RHI canvas present frame does not match a live acquired frame",
                frame.frameSerial,
                frame.frameIndex);
        }
        const auto identity = canvasIdentities_[frame.frameIndex];
        if (identity && identity->claimed_
            && (!identity->surface_.isCurrent() || !identity->issuer_.isCurrent()
                || identity->discarded_ || !identity->submitted_ || !identity->observed_
                || identity->presented_ || identity->slot_ != frame.frameIndex
                || identity->serial_ != frame.frameSerial))
            return rhi::makePresentFailureResult(
                "RHI-WEBGPU-ORIGINAL-CANVAS-PRESENT-ORDER", frame.frameSerial, frame.frameIndex);
        if (!slots_.markPresented(frame.frameIndex, frame.frameSerial)) {
            return rhi::makePresentFailureResult("WebGPU RHI canvas present frame is not acquired",
                frame.frameSerial,
                frame.frameIndex);
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!nativePayload_->browserManagedPresent(frame.frameIndex, diagnostics)) {
                slots_.clearSlot(frame.frameIndex);
                return rhi::makePresentFailureResult(diagnostics,
                    frame.frameSerial,
                    frame.frameIndex,
                    rhi::PresentStatus::SurfaceLost);
            }
        }
        if (identity)
            identity->presented_ = true;
        canvasIdentities_[frame.frameIndex].reset();
        if (completion.renderCompletion) {
            frameSlotRenderCompletions_[frame.frameIndex] = completion.renderCompletion;
        } else {
            slots_.clearSlot(frame.frameIndex);
        }
        return rhi::makePresentSuccessResult(frame.frameSerial, frame.frameIndex);
    }

    rhi::SwapchainLifecycleResult setSurfaceAvailable(bool available) override
    {
        recycleCompletedFrameSlots();
        if (slots_.anySlotInFlight())
            return {false,
                "RHI-WEBGPU-SURFACE-MUTATION-IN-FLIGHT",
                false,
                false,
                true,
                false,
                desc_.extent};

        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!nativePayload_->surfaceLoss(available, diagnostics)) {
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
                "RHI-WEBGPU-SURFACE-MUTATION-IN-FLIGHT",
                false,
                false,
                true,
                false,
                desc_.extent};

        const bool valid = extent.width > 0 && extent.height > 0;
        if (!valid) {
            return {
                false,
                "WebGPU RHI canvas resize extent is invalid",
                surfaceAvailable_,
                surfaceAvailable_,
                false,
                false,
                desc_.extent,
            };
        }
        if (nativePayload_) {
            std::pmr::string diagnostics;
            if (!nativePayload_->resizeCanvas(extent, diagnostics)) {
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
        }
        desc_.extent = extent;
        clearFrameSlots();
        return {
            true,
            "",
            surfaceAvailable_,
            surfaceAvailable_,
            true,
            true,
            desc_.extent,
        };
    }

    rhi::SwapchainLifecycleResult reconfigure(
        const rhi::SwapchainReconfigureRequest& request) override
    {
        // Browser compositors own WebGPU pacing. The generic request still
        // takes the normal canvas lifecycle path and is recorded in desc_.
        const auto resized = resize(request.desc.extent);
        if (!resized.success)
            return resized;
        desc_ = request.desc;
        return resized;
    }

    rhi::SwapchainPresentationStatus presentationStatus() const override
    {
        return {desc_.presentationMode,
            rhi::PresentationMode::Auto,
            desc_.presentationMode != rhi::PresentationMode::Auto,
            "browser-managed"};
    }

private:
    void recycleCompletedFrameSlots()
    {
        for (std::uint32_t slot = 0u; slot < frameSlotRenderCompletions_.size(); ++slot) {
            if (frameSlotRenderCompletions_[slot]
                && frameSlotRenderCompletions_[slot]->isSignaled()) {
                slots_.clearSlot(slot);
                frameSlotRenderCompletions_[slot].reset();
            }
        }
    }

    void clearFrameSlots()
    {
        surfaceEpoch_.advance();
        for (auto& identity : canvasIdentities_) {
            if (identity)
                identity->discarded_ = true;
            identity.reset();
        }
        slots_.clearAllSlots();
        for (auto& completion : frameSlotRenderCompletions_) {
            completion.reset();
        }
    }

    core::LifetimeEpoch surfaceEpoch_;
    std::array<std::shared_ptr<WebGpuOriginalCanvas>, 2> canvasIdentities_;
    rhi::SwapchainDesc desc_;
    std::shared_ptr<WebGpuNativeSwapchainPayload> nativePayload_;
    std::shared_ptr<WebGpuNativeSwapchainStats> swapchainStats_;
    std::shared_ptr<WebGpuRhiResourceRegistry> registry_;
    bool surfaceAvailable_ = true;
    rhi::FrameSlotStateMachine<2> slots_;
    std::array<std::shared_ptr<const rhi::GpuFence>, 2> frameSlotRenderCompletions_;
};

arp::core::UniquePtr<rhi::Swapchain> createWebGpuRhiSwapchain(const rhi::SwapchainDesc& desc,
    WebGpuRhiNativePayloadPolicy nativePayloadPolicy,
    std::shared_ptr<WebGpuNativeSwapchainStats> swapchainStats,
    std::shared_ptr<WebGpuRhiResourceRegistry> registry)
{
    if (desc.extent.width == 0 || desc.extent.height == 0) {
        return nullptr;
    }
    if (desc.presentationMode != rhi::PresentationMode::Auto) {
        std::fprintf(stderr,
            "[wgpu-swap] request=%s selected=browser-managed (WebGPU presentation timing is "
            "browser-controlled)\n",
            presentationRequestName(desc.presentationMode));
    }
    std::shared_ptr<WebGpuNativeSwapchainPayload> nativePayload;
    if (nativePayloadPolicy.enabled) {
        std::pmr::string diagnostics;
        nativePayload = createWebGpuNativeSwapchainPayload(desc, swapchainStats, diagnostics);
        if (!nativePayload) {
            std::fprintf(
                stderr, "[wgpu-swap] native payload unavailable: %s\n", diagnostics.c_str());
        }
        if (nativePayloadPolicy.required && !nativePayload) {
            std::fprintf(stderr, "[wgpu-swap] FAIL diag='%s'\n", diagnostics.c_str());
            return nullptr;
        }
    }
    return arp::core::makeUnique<WebGpuRhiSwapchain>(
        desc, std::move(nativePayload), std::move(swapchainStats), std::move(registry));
}

} // namespace arp::rhi_webgpu
