#include "null_backend_internal.h"
#include "rhi/frontend/frame_slot_state_machine.h"

#include <array>
#include <cstdio>
#include <utility>

namespace arp::rhi::null_backend {

namespace {

const char* presentationRequestName(PresentationMode mode)
{
    switch (mode) {
    case PresentationMode::VSync:
        return "vsync";
    case PresentationMode::LowLatency:
        return "low-latency";
    case PresentationMode::Immediate:
        return "immediate";
    case PresentationMode::Auto:
    default:
        return "auto";
    }
}

class NullSwapchain final : public Swapchain {
public:
    explicit NullSwapchain(SwapchainDesc desc) : desc_(std::move(desc)) {}

    std::uint32_t imageCount() const override { return 2; }

    std::uint32_t maxFramesInFlight() const override { return 2; }

    AcquiredFrame acquireNextFrame() override
    {
        recycleCompletedFrameSlots();
        if (!surfaceAvailable_) {
            return makeAcquireFailureResult("surface is unavailable for acquire",
                slots_.frameSerial(),
                0,
                AcquireStatus::SurfaceLost);
        }

        const auto acquired = slots_.acquireSlot();
        if (!acquired.success) {
            return makeAcquireFailureResult("no free double-buffer frame slot",
                slots_.frameSerial(),
                0,
                AcquireStatus::NotReady);
        }
        return makeAcquireSuccessResult(acquired.serial, acquired.slot);
    }

    bool isFrameAcquired(const AcquiredFrame& frame) const noexcept override
    {
        return frame.success && slots_.slotInFlight(frame.frameIndex, frame.frameSerial)
            && slots_.slotAcquired(frame.frameIndex, frame.frameSerial);
    }

    AcquiredSwapchainImage acquiredImage(const AcquiredFrame& frame) override
    {
        if (!isFrameAcquired(frame)) {
            return {
                false,
                "null swapchain image requires a live acquired frame",
                {},
                nullptr,
            };
        }

        auto texture = createTexture(
            TextureDesc{
                "NullSwapchainImage",
                {desc_.extent.width, desc_.extent.height, 1u},
                desc_.format,
                TextureUsage::RenderAttachment | TextureUsage::CopyDst,
                MemoryUsage::GpuOnly,
                // Acquire never promises preserved external contents. The
                // SwapchainImageRef carries the separate final Present state.
                ResourceState::Undefined,
                1u,
                1u,
            },
            ledger_,
            registry_);
        if (!texture) {
            return {
                false,
                "null swapchain image texture creation failed",
                {},
                nullptr,
            };
        }

        auto* texturePtr = texture.get();
        return {
            true,
            "",
            SwapchainImageRef{
                texturePtr->id(),
                frame.frameIndex,
                frame.frameSerial,
                desc_.extent,
                desc_.format,
                ResourceState::Present,
            },
            std::move(texture),
        };
    }

    FrameDiscardResult discardFrame(const AcquiredFrame& frame) override
    {
        if (!isFrameAcquired(frame)) {
            return makeFrameDiscardFailureResult(
                "null swapchain discard rejected frame that is not in flight",
                frame.frameSerial,
                frame.frameIndex);
        }
        slots_.clearSlot(frame.frameIndex);
        frameSlotRenderCompletions_[frame.frameIndex].reset();
        return makeFrameDiscardSuccessResult(frame.frameSerial, frame.frameIndex);
    }

    PresentResult present(
        const AcquiredFrame& frame, const FramePresentationCompletion& completion = {}) override
    {
        if (!surfaceAvailable_) {
            return makePresentFailureResult("present rejected unavailable surface",
                frame.frameSerial,
                frame.frameIndex,
                PresentStatus::SurfaceLost);
        }
        if (!isFrameAcquired(frame)) {
            return makePresentFailureResult("present rejected frame that is not in flight",
                frame.frameSerial,
                frame.frameIndex);
        }
        if (!slots_.markPresented(frame.frameIndex, frame.frameSerial)) {
            return makePresentFailureResult(
                "null swapchain present rejected frame that is not acquired",
                frame.frameSerial,
                frame.frameIndex);
        }
        if (completion.renderCompletion) {
            frameSlotRenderCompletions_[frame.frameIndex] = completion.renderCompletion;
        } else {
            slots_.clearSlot(frame.frameIndex);
        }
        return makePresentSuccessResult(frame.frameSerial, frame.frameIndex);
    }

    SwapchainLifecycleResult setSurfaceAvailable(bool available) override
    {
        surfaceAvailable_ = available;
        clearFrameSlots();

        SwapchainLifecycleResult result;
        result.success = true;
        result.acquireAllowed = surfaceAvailable_;
        result.presentAllowed = surfaceAvailable_;
        result.swapchainRebuildRequired = surfaceAvailable_;
        result.releaseSurfaceResources = !surfaceAvailable_;
        result.extent = desc_.extent;
        return result;
    }

    SwapchainLifecycleResult resize(const Extent2D& extent) override
    {
        if (extent.width == 0 || extent.height == 0) {
            SwapchainLifecycleResult result;
            result.success = false;
            result.diagnostics = "swapchain resize requires non-zero extent";
            result.acquireAllowed = surfaceAvailable_;
            result.presentAllowed = surfaceAvailable_;
            result.extent = desc_.extent;
            return result;
        }

        desc_.extent = extent;
        clearFrameSlots();

        SwapchainLifecycleResult result;
        result.success = true;
        result.acquireAllowed = surfaceAvailable_;
        result.presentAllowed = surfaceAvailable_;
        result.swapchainRebuildRequired = true;
        result.releaseSurfaceResources = true;
        result.extent = desc_.extent;
        return result;
    }

    SwapchainLifecycleResult reconfigure(const SwapchainReconfigureRequest& request) override
    {
        if (request.desc.extent.width == 0 || request.desc.extent.height == 0) {
            return {false,
                "null swapchain reconfigure requires non-zero extent",
                surfaceAvailable_,
                surfaceAvailable_,
                false,
                false,
                desc_.extent};
        }
        desc_ = request.desc;
        clearFrameSlots();
        return {true, "", surfaceAvailable_, surfaceAvailable_, true, true, desc_.extent};
    }

    SwapchainPresentationStatus presentationStatus() const override
    {
        return {desc_.presentationMode,
            PresentationMode::Auto,
            desc_.presentationMode != PresentationMode::Auto,
            "simulated"};
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
        slots_.clearAllSlots();
        for (auto& completion : frameSlotRenderCompletions_) {
            completion.reset();
        }
    }

    SwapchainDesc desc_;
    MemoryLedgerPtr ledger_ = arp::core::makeShared<MemoryLedger>();
    ResourceRegistryPtr registry_ = arp::core::makeShared<ResourceRegistry>();
    bool surfaceAvailable_ = true;
    FrameSlotStateMachine<2> slots_;
    std::array<std::shared_ptr<const GpuFence>, 2> frameSlotRenderCompletions_;
};

} // namespace

arp::core::UniquePtr<Swapchain> createSwapchain(const SwapchainDesc& desc)
{
    if (desc.presentationMode != PresentationMode::Auto) {
        std::fprintf(stderr,
            "[null-swapchain] request=%s selected=simulated (presentation mode is not applicable "
            "to the null backend)\n",
            presentationRequestName(desc.presentationMode));
    }
    return arp::core::makeUnique<NullSwapchain>(desc);
}

} // namespace arp::rhi::null_backend
