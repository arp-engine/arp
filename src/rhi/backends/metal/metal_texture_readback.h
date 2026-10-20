#pragma once
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/gpu_extensions.h"

namespace arp::rhi_metal {

// Device-owned diagnostic queue. schedule/configure/poll share the owning
// render thread; GPU callbacks do not access this container.
class MetalTextureReadback final : public rhi::TextureReadbackExtension {
public:
    MetalTextureReadback(rhi::GpuDevice& device, std::shared_ptr<MetalRhiResourceRegistry> registry)
        : device_(device), registry_(std::move(registry))
    {}

    bool configureBudget(rhi::TextureReadbackBudget budget, core::pmr::String& diagnostic) override;
    rhi::TextureReadbackBudgetStats budgetStats() const override;
    bool schedule(
        rhi::CommandEncoder&, const rhi::GpuTexture&, std::string_view, std::string&) override;
    bool poll(rhi::TextureReadbackSample&) override;

    bool supportsTrackedRequests() const override { return true; }

    bool scheduleTracked(rhi::CommandEncoder&,
        const rhi::GpuTexture&,
        std::string_view,
        rhi::TextureReadbackIdentity,
        core::pmr::String&) override;

private:
    bool scheduleImpl(rhi::CommandEncoder&,
        const rhi::GpuTexture&,
        std::string_view,
        rhi::TextureReadbackIdentity,
        core::pmr::String&);

    struct Pending {
        core::UniquePtr<rhi::GpuBuffer> buffer;
        std::shared_ptr<MetalRhiNativeCommandPayload> command;
        core::pmr::String label;
        rhi::TextureFormat format;
        std::uint32_t width, height, pitch;
        std::uint64_t charge;
        rhi::TextureReadbackIdentity identity;
        rhi::GpuResourceId source;
    };

    rhi::GpuDevice& device_;
    std::shared_ptr<MetalRhiResourceRegistry> registry_;
    rhi::TextureReadbackBudget budget_;
    std::uint64_t charged_ = 0;
    std::uint64_t peakCharged_ = 0;
    std::uint64_t budgetRejectionCount_ = 0;
    core::pmr::Vector<Pending> pending_;
};
} // namespace arp::rhi_metal
