#pragma once

#include "renderer/render_graph/ordered_graphics_measurements.h"
#include "renderer/render_graph/render_graph.h"
#include "rhi/frontend/ordered_graphics_execution.h"

namespace arp::platform {
class RenderSystem;

struct RenderSystemOrderedGraphicsDesc {
    std::uint32_t contractVersion = 0;
    bool prototypeOptIn = false;
    rhi::SwapchainDesc surface;
    rhi::semantics::PreparedGraphicsCommandsRequest commands;
    std::uint64_t maxFrameCpuMetadataBytes = 0;
    std::shared_ptr<core::CpuMemoryQuota> frameQuota, commandQuota;
};

class OrderedGraphicsFrameWorkload {
public:
    virtual ~OrderedGraphicsFrameWorkload() = default;
    virtual core::ArrayView<std::shared_ptr<rhi::RenderPipeline>> pipelines() const noexcept = 0;
    virtual void build(render_graph::FrameGraph&, render_graph::TextureHandle canvas) = 0;
};

struct RenderSystemOrderedGraphicsFrameReport {
    bool success = false, workAccepted = false, presented = false, recycled = false;
    bool gpuCompletionClaimed = false;
    std::uint64_t frameSerial = 0;
    std::uint32_t executedPasses = 0;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

// Controlled Prototype branch of RenderSystem. The original issuer and device
// are consumed together; caller reports/capability flags are not accepted.
class ARP_API RenderSystemOrderedGraphicsPath final {
    friend class RenderSystem;

    class ConstructionKey {
        friend class RenderSystem;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    RenderSystemOrderedGraphicsPath(ConstructionKey,
        core::UniquePtr<rhi::GpuDevice>,
        std::shared_ptr<rhi::OriginalOrderedGraphicsExecution>,
        const RenderSystemOrderedGraphicsDesc&);
    ~RenderSystemOrderedGraphicsPath();
    RenderSystemOrderedGraphicsFrameReport renderFrame(OrderedGraphicsFrameWorkload&);
    RenderSystemOrderedGraphicsFrameReport renderFrame(
        OrderedGraphicsFrameWorkload&, render_graph::OrderedGraphicsMeasurements*);
    rhi::SwapchainLifecycleResult resize(rhi::Extent2D);
    rhi::SwapchainLifecycleResult setSurfaceAvailable(bool);

    rhi::GpuDevice& device() noexcept { return *device_; }

    render_graph::FrameGraphResourcePoolReport poolReport() const { return pool_->report(); }

private:
    const char* rebuildCanvas();
    core::UniquePtr<rhi::GpuDevice> device_;
    std::shared_ptr<rhi::OriginalOrderedGraphicsExecution> execution_;
    std::shared_ptr<core::CpuMemoryQuota> frameQuota_, commandQuota_;
    std::shared_ptr<render_graph::FrameGraphResourcePool> pool_;
    rhi::SwapchainDesc surface_{core::pmr::String(&core::defaultCpuMemoryResource())};
    rhi::semantics::PreparedGraphicsCommandsRequest commands_;
    core::UniquePtr<rhi::Swapchain> canvas_;
    core::UniquePtr<render_graph::FrameGraph> pendingGraph_;
    bool failed_ = false, paused_ = false;
};
} // namespace arp::platform
