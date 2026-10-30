#include "rhi/backends/webgl/webgl_canvas_internal.h"
#include "rhi/backends/webgl/webgl_device.h"
#include "rhi/backends/webgl/webgl_pipeline_internal.h"

namespace arp::rhi::webgl {

class WebGlOrderedGraphicsPayload final : public OriginalOrderedGraphicsExecution::Payload {
public:
    WebGlOrderedGraphicsPayload(const GpuDevice& device,
        core::LifetimeEpoch::Ticket lifetime,
        std::shared_ptr<WebGlResources> resources,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        std::shared_ptr<core::CpuMemoryQuota> deviceQuota,
        OrderedGraphicsExecutionRequest request)
        : device_(&device), lifetime_(std::move(lifetime)), resources_(std::move(resources)),
          quota_(std::move(quota)), deviceQuota_(std::move(deviceQuota)), request_(request)
    {}

    const char* current() const noexcept
    {
        if (!lifetime_.isCurrent())
            return "RHI-ORDERED-GRAPHICS-DEVICE-RETIRED";
        return resources_->currentDiagnostic();
    }

    const char* diagnostic(const GpuDevice& device) const noexcept override
    {
        return &device == device_ ? current() : "RHI-ORDERED-GRAPHICS-DEVICE-MISMATCH";
    }

    const char* metadataQuotaDiagnostic(
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept override
    {
        if (const auto* id = current())
            return id;
        if (conformance_ && core::SharedOwnerProjection::overlapsOwner(quota, conformance_->quota_))
            return "RHI-CONFORMANCE-ORIGINAL-QUOTA-OVERLAP";
        const auto& domain = resources_->domain_;
        const std::array owners{
            quota, quota_, deviceQuota_, domain->metadataQuota, domain->uploadQuota};
        return quota
                && core::SharedOwnerProjection::hasIndependentOwners(owners.size(),
                    [&](std::size_t i) noexcept {
                        return owners[i];
                    })
            ? nullptr
            : "RHI-ORDERED-GRAPHICS-CPU-QUOTA";
    }

    core::UniquePtr<CommandEncoder> prepare(
        core::ArrayView<std::shared_ptr<RenderPipeline>> pipelines,
        core::ArrayView<OrderedGraphicsTarget> targets,
        const semantics::PreparedGraphicsCommandsRequest& request,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const char*& diagnostic) override
    {
        diagnostic = current();
        if (diagnostic)
            return {};
        if (submissions_ >= request_.maxSubmissions) {
            diagnostic = "RHI-ORDERED-GRAPHICS-SUBMISSION-BUDGET";
            return {};
        }
        if ((request.contractVersion < 3 || request.contractVersion > 5)
            || (diagnostic = semantics::PreparedGraphicsCommandsContract::validate(request))) {
            if (!diagnostic)
                diagnostic = "RHI-ORDERED-GRAPHICS-COMMAND-VERSION";
            return {};
        }
        if (pipelines.empty() || pipelines.size() > request.maxPipelines || targets.empty()
            || targets.size() > request.maxTargets) {
            diagnostic = "RHI-ORDERED-GRAPHICS-CATALOG-BUDGET";
            return {};
        }
        const auto& domain = resources_->domain_;
        if (!quota || quota->limitBytes() != request.issue.maxCpuMetadataBytes
            || metadataQuotaDiagnostic(quota)) {
            diagnostic = "RHI-ORDERED-GRAPHICS-COMMAND-QUOTA";
            return {};
        }
        // Validate every owner before creating any target. Publication identity,
        // original profile limits and extensions were consumed at pipeline creation.
        for (const auto& pipeline : pipelines) {
            const auto original = std::dynamic_pointer_cast<WebGlRenderPipeline>(pipeline);
            if (!original || original->impl->object->domain != domain
                || !original->impl->object->current()) {
                diagnostic = "RHI-ORDERED-GRAPHICS-PIPELINE-OWNER";
                return {};
            }
            const auto& artifact = original->impl->program->artifact();
            const auto& r = request_.requirements;
            if (artifact->vertex().inputs.size() > r.vertexAttributes
                || artifact->vertex().uniformVectors > r.vertexUniformVectors
                || artifact->fragment().uniformVectors > r.fragmentUniformVectors
                || artifact->vertex().pairs.size() > r.vertexTextureUnits
                || artifact->fragment().pairs.size() > r.fragmentTextureUnits) {
                diagnostic = "RHI-ORDERED-GRAPHICS-PIPELINE-REQUIREMENTS";
                return {};
            }
        }
        for (const auto& target : targets) {
            if (!target.color
                || target.color->desc().extent.width > request_.requirements.textureWidth
                || target.color->desc().extent.height > request_.requirements.textureHeight) {
                diagnostic = "RHI-ORDERED-GRAPHICS-TARGET-REQUIREMENTS";
                return {};
            }
        }
        try {
            core::CpuMemoryQuotaScope scope(quota_);
            core::pmr::Vector<std::shared_ptr<WebGlFramebuffer>> owners{
                &core::defaultCpuMemoryResource()};
            owners.reserve(targets.size());
            for (const auto& target : targets) {
                auto owner = resources_->retainFramebuffer(*target.color, target.depthStencil);
                if (!owner) {
                    diagnostic = resources_->diagnostic();
                    return {};
                }
                owners.push_back(std::move(owner));
            }
            return WebGlExecution::prepare(
                *resources_, pipelines, owners, request, quota, diagnostic);
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORDERED-GRAPHICS-CPU-ALLOCATION";
            return {};
        }
    }

    const char* beginConformance(core::UniquePtr<ConformanceClearWorkload> clear,
        core::UniquePtr<ConformanceSampledWorkload> sampled,
        const OriginalConformanceRequest& request,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) override
    {
        if (const auto* id = current())
            return id;
        if (conformance_ || submissions_)
            return "RHI-CONFORMANCE-ORIGINAL-ALREADY-STARTED";
        if (const auto* id = OriginalConformanceContract::validate(request))
            return id;
        if (bool(clear) == bool(sampled))
            return "RHI-CONFORMANCE-ORIGINAL-WORKLOAD";
        if (const auto* id = metadataQuotaDiagnostic(quota))
            return id;
        if (quota->limitBytes() != request.maxCpuMetadataBytes)
            return "RHI-CONFORMANCE-ORIGINAL-QUOTA";
        const auto count = clear ? clear->frames().size() : sampled->frames().size();
        if (!count || count > request.maxFrames || count > request_.maxSubmissions)
            return "RHI-CONFORMANCE-ORIGINAL-FRAME-BUDGET";
        std::uint64_t bytes = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const auto extent = clear ? clear->frames()[i].extent : sampled->frames()[i].extent;
            const auto pixels = std::uint64_t(extent.width) * extent.height;
            if (pixels > SIZE_MAX / 4 || pixels > (request.maxOutputBytes - bytes) / 4)
                return "RHI-CONFORMANCE-ORIGINAL-OUTPUT-BUDGET";
            bytes += pixels * 4;
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            conformance_ = core::makeUnique<OriginalConformanceRun>(
                OriginalConformanceRun::ConstructionKey{},
                std::move(clear),
                std::move(sampled),
                quota,
                lifetime_,
                resources_->domain_->contextEpoch,
                resources_->domain_->lifetime.ticket(),
                resources_->domain_,
                +[](const void* owner) noexcept {
                    return static_cast<const WebGlResourceDomain*>(owner)->currentDiagnostic();
                });
            return nullptr;
        } catch (const std::bad_alloc&) {
            return "RHI-CONFORMANCE-ORIGINAL-CPU-ALLOCATION";
        }
    }

    const OriginalConformanceRun* conformanceRun() const noexcept override
    {
        return conformance_.get();
    }

    QueueSubmitResult submit(const CommandBuffer& commands) override
    {
        if (const auto* id = current())
            return {false, core::toPmrString(id)};
        if (submissions_ >= request_.maxSubmissions)
            return {false, "RHI-ORDERED-GRAPHICS-SUBMISSION-BUDGET"};
        if (conformance_) {
            if (conformance_->failure_)
                return {false, core::toPmrString(conformance_->failure_)};
            if (const auto* id =
                    WebGlExecution::validateConformance(*resources_, commands, *conformance_)) {
                conformance_->failure_ = id;
                return {false, core::toPmrString(id)};
            }
        }
        auto result = resources_->queue().submit(commands);
        if (result.hasAcceptedWork())
            ++submissions_;
        if (conformance_) {
            if (!result.success) {
                conformance_->failure_ = "RHI-CONFORMANCE-ORIGINAL-SUBMIT-FAILED";
            } else if (const auto* id = WebGlExecution::observeConformance(
                           *resources_, commands, *conformance_)) {
                conformance_->failure_ = id;
                result.success = false;
                result.diagnostics = id;
            }
        }
        return result;
    }

    const char* reuseDiagnostic(const GpuTexture& resource) const noexcept override
    {
        if (const auto* id = current())
            return id;
        const auto* original = dynamic_cast<const WebGlTexture*>(&resource);
        if (!original || original->payload->domain != resources_->domain_
            || !original->payload->current())
            return "RHI-ORDERED-GRAPHICS-RESOURCE-OWNER";
        if (original->payload->retainedReads || original->payload->state.pending())
            return "RHI-ORDERED-GRAPHICS-RESOURCE-RECORDED";
        if (!original->payload->nativeContentsValid)
            return "RHI-ORDERED-GRAPHICS-RESOURCE-CONTENTS-INVALID";
        return nullptr;
    }

    const char* reuseDiagnostic(const GpuBuffer& resource) const noexcept override
    {
        if (const auto* id = current())
            return id;
        const auto* original = dynamic_cast<const WebGlBuffer*>(&resource);
        if (!original || original->payload->domain != resources_->domain_
            || !original->payload->current())
            return "RHI-ORDERED-GRAPHICS-RESOURCE-OWNER";
        if (original->payload->retainedReads || original->payload->state.pending())
            return "RHI-ORDERED-GRAPHICS-RESOURCE-RECORDED";
        return nullptr;
    }

    const char* recycleCanvas(
        Swapchain& canvas, const AcquiredFrame& frame, core::UniquePtr<GpuTexture>& image) override
    {
        if (const auto* id = current())
            return id;
        if (conformance_) {
            if (!conformance_->next_ || conformance_->failure_)
                return "RHI-CONFORMANCE-ORIGINAL-RECYCLE-WITHOUT-OUTPUT";
            auto& observed = conformance_->frames_[conformance_->next_ - 1];
            const auto* original = dynamic_cast<const WebGlCanvas*>(&canvas);
            if (!original || original->payload()->domain != resources_->domain_
                || original->payload()->id != observed.canvas
                || original->payload()->serial != observed.serial
                || frame.frameSerial != observed.serial
                || original->payload()->phase != WebGlCanvasPayload::Phase::Presented
                || observed.presented) {
                conformance_->failure_ = "RHI-CONFORMANCE-ORIGINAL-PRESENT-IDENTITY";
                return conformance_->failure_;
            }
            if (const auto* id = resources_->recycleCanvas(canvas, frame, image)) {
                conformance_->failure_ = id;
                return id;
            }
            observed.presented = true;
            return nullptr;
        }
        return resources_->recycleCanvas(canvas, frame, image);
    }

    const RenderPipelineDesc* pipelineDefinition(
        const RenderPipeline& pipeline, const char*& diagnostic) const noexcept override
    {
        diagnostic = current();
        if (diagnostic)
            return nullptr;
        const auto* original = dynamic_cast<const WebGlRenderPipeline*>(&pipeline);
        if (!original || original->impl->object->domain != resources_->domain_
            || !original->impl->object->current()) {
            diagnostic = "RHI-ORDERED-GRAPHICS-PIPELINE-OWNER";
            return nullptr;
        }
        return &original->impl->definition;
    }

    CommandValidationResult uploadTexture(
        GpuTexture& texture, const semantics::ClientTextureUpload& upload) override
    {
        if (const auto* id = current())
            return {false, core::toPmrString(id), 0};
        return resources_->uploadTexture(texture, upload);
    }

    core::UniquePtr<Swapchain> createCanvas(
        const SwapchainDesc& desc, const char*& diagnostic) override
    {
        diagnostic = current();
        if (diagnostic)
            return {};
        if (desc.extent.width > request_.requirements.textureWidth
            || desc.extent.height > request_.requirements.textureHeight) {
            diagnostic = "RHI-ORDERED-GRAPHICS-TARGET-REQUIREMENTS";
            return {};
        }
        auto canvas = resources_->createCanvas(desc);
        if (!canvas) {
            diagnostic = resources_->diagnostic();
            return {};
        }
        diagnostic = resources_->enableCanvasReuse(*canvas, {1, true, request_.maxSubmissions + 1});
        return diagnostic ? core::UniquePtr<Swapchain>{} : std::move(canvas);
    }

private:
    const GpuDevice* device_;
    core::LifetimeEpoch::Ticket lifetime_;
    std::shared_ptr<WebGlResources> resources_;
    std::shared_ptr<core::CpuMemoryQuota> quota_;
    std::shared_ptr<core::CpuMemoryQuota> deviceQuota_;
    OrderedGraphicsExecutionRequest request_;
    std::uint32_t submissions_ = 0;
    core::UniquePtr<OriginalConformanceRun> conformance_;
};

core::UniquePtr<OriginalOrderedGraphicsExecution> WebGlDevice::createOrderedGraphicsExecution(
    const OrderedGraphicsExecutionRequest& request,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    const char*& diagnostic)
{
    diagnostic = nullptr;
    if (request.contractVersion != 1 || !request.prototypeOptIn || !request.maxSubmissions
        || request.maxSubmissions == UINT32_MAX || !request.maxCpuMetadataBytes) {
        diagnostic = "RHI-ORDERED-GRAPHICS-EXECUTION-REQUEST";
        return {};
    }
    if ((diagnostic = resources_->currentDiagnostic())
        || (diagnostic = semantics::GraphicsWorkloadRequirementsContract::orderedSubset(
                request.requirements)))
        return {};
    const auto& l = resources_->profile().limits();
    const semantics::GraphicsWorkloadFacts facts{1,
        l.maxTextureSize,
        l.maxTextureSize,
        l.maxVertexAttributes,
        l.maxVertexUniformVectors,
        l.maxFragmentUniformVectors,
        l.maxVertexTextureUnits,
        l.maxFragmentTextureUnits,
        l.maxCombinedTextureUnits};
    if ((diagnostic = semantics::GraphicsWorkloadRequirementsContract::assessLimits(
             request.requirements, facts)))
        return {};
    const std::array quotas{
        quota, quota_, resources_->domain_->metadataQuota, resources_->domain_->uploadQuota};
    if (!quota || quota->limitBytes() != request.maxCpuMetadataBytes
        || !core::SharedOwnerProjection::hasIndependentOwners(
            quotas.size(), [&](std::size_t i) noexcept {
                return quotas[i];
            })) {
        diagnostic = "RHI-ORDERED-GRAPHICS-CPU-QUOTA";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto payload = core::makeUnique<WebGlOrderedGraphicsPayload>(
            *this, frameExecutionLifetime_.ticket(), resources_, quota, quota_, request);
        return core::makeUnique<OriginalOrderedGraphicsExecution>(
            OriginalOrderedGraphicsExecution::ConstructionKey{},
            std::move(payload),
            request.requirements);
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-ORDERED-GRAPHICS-CPU-ALLOCATION";
        return {};
    }
}
} // namespace arp::rhi::webgl
