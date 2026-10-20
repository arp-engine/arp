#pragma once
#include "rhi/backends/webgpu/webgpu_native_ordinary_graphics_triangle_range.h"
#include "rhi/frontend/pipeline_single_color_validation.h"

namespace arp::rhi_webgpu {
struct WebGpuOrdinaryGraphicsAttachmentBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxColorAttachments = 0u;
    WebGpuOrdinaryResourceBudget resources;
};

class WebGpuOrdinaryGraphicsAttachments final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class WebGpuOrdinaryGraphicsAttachments;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    WebGpuOrdinaryGraphicsAttachments(ConstructionKey,
        std::shared_ptr<const WebGpuOrdinaryGraphicsTriangleRange> range,
        std::shared_ptr<const WebGpuRhiResourceRegistry> registry,
        core::UniquePtr<WebGpuOrdinaryResourceProjection> resources,
        rhi::GpuResourceId target,
        rhi::Color clear,
        rhi::Extent3D extent)
        : range_(std::move(range)), registry_(std::move(registry)),
          resources_(std::move(resources)), target_(target), clear_(clear), extent_(extent)
    {}

    static const char* validateBudget(const WebGpuOrdinaryGraphicsAttachmentBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-WEBGPU-ORDINARY-ATTACHMENT-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-WEBGPU-ORDINARY-ATTACHMENT-OPT-IN";
        if (!budget.maxColorAttachments)
            return "RHI-WEBGPU-ORDINARY-ATTACHMENT-CAPACITY";
        return WebGpuOrdinaryResourceProjection::validateBudget(budget.resources);
    }

    static std::shared_ptr<WebGpuOrdinaryGraphicsAttachments> prepare(
        std::shared_ptr<const WebGpuOrdinaryGraphicsTriangleRange> range,
        std::shared_ptr<const WebGpuRhiResourceRegistry> registry,
        const rhi::RenderPassDesc& pass,
        const WebGpuOrdinaryGraphicsAttachmentBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(budget);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != budget.resources.retention.maxCpuMetadataBytes) {
            diagnostic = "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH";
            return {};
        }
        if ((diagnostic = rhi::semantics::SingleColor2DRendering::validatePass(pass)))
            return {};
        if (!range || !range->hasQuotaBoundSharedOwner(range)) {
            diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-RANGE-OWNER";
            return {};
        }
        if ((diagnostic = range->validate()))
            return {};
        if (!registry || !registry->sharedOwnerMatches(registry)) {
            diagnostic = "RHI-WEBGPU-ORDINARY-ATTACHMENT-REGISTRY";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            const auto& color = pass.colorAttachments.front();
            auto resources = WebGpuOrdinaryResourceProjection::prepare(
                *registry, &color.target, 1u, budget.resources, quota, diagnostic);
            if (!resources)
                return {};
            const auto& target = resources->resources().front();
            rhi::Extent3D extent;
            if (!target.view() || !target.backing().texture) {
                diagnostic = "RHI-SINGLE-COLOR-VIEW-COMPATIBILITY";
                return {};
            }
            if ((diagnostic = rhi::semantics::SingleColor2DRendering::resolveTarget(
                     target.backing().textureDesc, target.viewDesc(), color.format, extent)))
                return {};
            const auto& pipeline = range->validation_->groups_->pipeline_->contract();
            if (target.nativeDevice() != pipeline.nativeDevice()) {
                diagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-DEVICE-MISMATCH";
                return {};
            }
            if ((diagnostic = rhi::semantics::SingleColor2DRendering::validatePipeline(
                     pipeline.graphics(), color.format, extent)))
                return {};
            auto result = core::makeUnique<WebGpuOrdinaryGraphicsAttachments>(ConstructionKey{},
                std::move(range),
                std::move(registry),
                std::move(resources),
                color.target,
                color.clearColor,
                extent);
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-WEBGPU-ORDINARY-ATTACHMENT-ALLOCATION";
            return {};
        }
    }

    const char* validate() const noexcept
    {
        if (!range_->hasQuotaBoundSharedOwner(range_))
            return "RHI-WEBGPU-ORDINARY-RECORDING-RANGE-OWNER";
        if (const auto* id = range_->validate())
            return id;
        if (!registry_->sharedOwnerMatches(registry_))
            return "RHI-WEBGPU-ORDINARY-ATTACHMENT-REGISTRY";
        return registry_->validateOrdinaryResources(
            resources_->resources().data(), resources_->resources().size());
    }

    const auto& range() const noexcept { return range_; }

    const auto& target() const noexcept { return resources_->resources().front(); }

    const auto& clear() const noexcept { return clear_; }

    rhi::Extent3D extent() const noexcept { return extent_; }

private:
    const std::shared_ptr<const WebGpuOrdinaryGraphicsTriangleRange> range_;
    const std::shared_ptr<const WebGpuRhiResourceRegistry> registry_;
    const core::UniquePtr<WebGpuOrdinaryResourceProjection> resources_;
    const rhi::GpuResourceId target_;
    const rhi::Color clear_;
    const rhi::Extent3D extent_;
};
} // namespace arp::rhi_webgpu
