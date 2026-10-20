#pragma once
#include "rhi/backends/webgl/webgl_framebuffer_internal.h"
#include "rhi/semantics/webgl_canvas_lease.h"

namespace arp::rhi::webgl {
class WebGlCanvasPayload final : public WebGlResourcePayload {
public:
    enum class Phase { Prepared, Acquired, Submitted, Presented, Discarded, Failed };

    WebGlCanvasPayload(
        WebGlResourceFactoryKey key, std::shared_ptr<WebGlResourceDomain> domain, Extent2D extent)
        : WebGlResourcePayload(key, std::move(domain), WebGlObjectKind::CanvasDrawingBuffer),
          surfaceEpoch(this->domain->runtime->surfaceEpoch()), extent(extent),
          state(shape(extent), ResourceState::ColorAttachment)
    {
        serial = id.value;
    }

    static TextureDesc shape(Extent2D extent)
    {
        TextureDesc d;
        d.extent = {extent.width, extent.height, 1};
        d.format = TextureFormat::Rgba8Unorm;
        d.usage = TextureUsage::RenderAttachment;
        d.initialState = ResourceState::ColorAttachment;
        return d;
    }

    const char* diagnostic() const noexcept;
    core::LifetimeEpoch::Ticket surfaceEpoch;
    Extent2D extent;
    common::RecordedTextureStateControl state;
    Phase phase = Phase::Prepared;
    bool ownerActive = true;
    std::uint64_t serial = 0;
    std::uint32_t maxLeases = 1, leaseOrdinal = 1;
    std::weak_ptr<WebGlFramebuffer> framebuffer;
};

class WebGlCanvasImage final : public GpuTexture {
public:
    WebGlCanvasImage(WebGlResourceFactoryKey, std::shared_ptr<WebGlCanvasPayload> p)
        : payload(std::move(p)), definition(WebGlCanvasPayload::shape(payload->extent))
    {
        bindOriginalRecordedState(payload->state);
    }

    GpuResourceId id() const override { return payload->id; }

    const TextureDesc& desc() const override { return definition; }

    const ResourceAllocation& allocation() const override { return allocation_; }

    ResourceState state() const override
    {
        return payload->state.uniformWholeState().value_or(ResourceState::Undefined);
    }

    ResourceState subresourceState(const TextureSubresourceRange& range) const override
    {
        return payload->state.uniformState(range).value_or(ResourceState::Undefined);
    }

    std::shared_ptr<WebGlCanvasPayload> payload;
    TextureDesc definition;
    ResourceAllocation allocation_;
};

class WebGlCanvas final : public Swapchain {
public:
    WebGlCanvas(std::shared_ptr<WebGlCanvasPayload>,
        core::UniquePtr<GpuTexture>,
        std::shared_ptr<WebGlFramebuffer>);
    ~WebGlCanvas() override;

    std::uint32_t imageCount() const override { return 0; }

    std::uint32_t maxFramesInFlight() const override { return 1; }

    AcquiredFrame acquireNextFrame() override;
    bool isFrameAcquired(const AcquiredFrame&) const noexcept override;
    AcquiredSwapchainImage acquiredImage(const AcquiredFrame&) override;
    FrameDiscardResult discardFrame(const AcquiredFrame&) override;
    PresentResult present(const AcquiredFrame&, const FramePresentationCompletion&) override;
    SwapchainLifecycleResult setSurfaceAvailable(bool) override;
    SwapchainLifecycleResult resize(const Extent2D&) override;
    SwapchainLifecycleResult reconfigure(const SwapchainReconfigureRequest&) override;
    SwapchainPresentationStatus presentationStatus() const override;

    const std::shared_ptr<WebGlFramebuffer>& framebuffer() const noexcept { return framebuffer_; }

    const std::shared_ptr<WebGlCanvasPayload>& payload() const noexcept { return payload_; }

    const char* enableReuse(const semantics::WebGlCanvasReuseRequest&) noexcept;
    const char* recycle(const AcquiredFrame&, core::UniquePtr<GpuTexture>&) noexcept;

private:
    const char* current() const noexcept;
    const char* lifecycleOwnerDiagnostic() const noexcept;
    SwapchainLifecycleResult lifecycleResult(const char*) const;
    std::shared_ptr<WebGlCanvasPayload> payload_;
    core::UniquePtr<GpuTexture> image_;
    std::shared_ptr<WebGlFramebuffer> framebuffer_;
};
} // namespace arp::rhi::webgl
