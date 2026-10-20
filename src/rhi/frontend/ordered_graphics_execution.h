#pragma once

#include "core/array_view.h"
#include "rhi/conformance/original_output.h"
#include "rhi/gpu_device.h"
#include "rhi/semantics/client_texture_upload.h"
#include "rhi/semantics/graphics_workload_requirements.h"
#include "rhi/semantics/prepared_graphics_commands.h"

namespace arp::rhi::webgl {
class WebGlDevice;
}

namespace arp::render_graph {
class FrameGraphResourcePool;
class FrameGraphUniformRingBuffer;
} // namespace arp::render_graph

namespace arp::rhi {

struct OrderedGraphicsTarget {
    GpuTexture* color = nullptr;
    GpuTexture* depthStencil = nullptr;
};

struct OrderedGraphicsExecutionRequest {
    std::uint32_t contractVersion = 0;
    bool prototypeOptIn = false;
    std::uint32_t maxSubmissions = 0;
    std::uint64_t maxCpuMetadataBytes = 0;
    semantics::GraphicsWorkloadRequirements requirements;
};

// An original issuer-owned execution entry, never a writable admission report.
// Its default path grants no completion/readback/external publication. The cold
// OriginalConformanceOutput/1 opt-in is separate. This cannot be forged by
// implementing Payload: only a backend's original device can construct its key.
class OriginalOrderedGraphicsExecution final {
    friend class webgl::WebGlDevice;

    class ConstructionKey {
        friend class webgl::WebGlDevice;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    class Payload {
    public:
        virtual ~Payload() = default;
        virtual const char* diagnostic(const GpuDevice&) const noexcept = 0;
        virtual const char* metadataQuotaDiagnostic(
            const std::shared_ptr<core::CpuMemoryQuota>&) const noexcept = 0;
        virtual core::UniquePtr<CommandEncoder> prepare(
            core::ArrayView<std::shared_ptr<RenderPipeline>>,
            core::ArrayView<OrderedGraphicsTarget>,
            const semantics::PreparedGraphicsCommandsRequest&,
            const std::shared_ptr<core::CpuMemoryQuota>&,
            const char*&) = 0;
        virtual QueueSubmitResult submit(const CommandBuffer&) = 0;

        virtual const char* beginConformance(core::UniquePtr<ConformanceClearWorkload>,
            core::UniquePtr<ConformanceSampledWorkload>,
            const OriginalConformanceRequest&,
            const std::shared_ptr<core::CpuMemoryQuota>&)
        {
            return "RHI-CONFORMANCE-ORIGINAL-SOURCE-UNAVAILABLE";
        }

        virtual const OriginalConformanceRun* conformanceRun() const noexcept { return nullptr; }

        virtual const char* reuseDiagnostic(const GpuTexture&) const noexcept = 0;
        virtual const char* reuseDiagnostic(const GpuBuffer&) const noexcept = 0;
        virtual const RenderPipelineDesc* pipelineDefinition(
            const RenderPipeline&, const char*&) const noexcept = 0;
        virtual CommandValidationResult uploadTexture(
            GpuTexture&, const semantics::ClientTextureUpload&) = 0;
        virtual const char* recycleCanvas(
            Swapchain&, const AcquiredFrame&, core::UniquePtr<GpuTexture>&) = 0;
        virtual core::UniquePtr<Swapchain> createCanvas(const SwapchainDesc&, const char*&) = 0;
    };

    OriginalOrderedGraphicsExecution(ConstructionKey,
        core::UniquePtr<Payload> payload,
        const semantics::GraphicsWorkloadRequirements& requirements)
        : payload_(std::move(payload)), requirements_(requirements)
    {}

    OriginalOrderedGraphicsExecution(const OriginalOrderedGraphicsExecution&) = delete;
    OriginalOrderedGraphicsExecution& operator=(const OriginalOrderedGraphicsExecution&) = delete;

    const char* diagnostic(const GpuDevice& device) const noexcept
    {
        return payload_->diagnostic(device);
    }

    const auto& requirements() const noexcept { return requirements_; }

    const char* metadataQuotaDiagnostic(
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
    {
        return payload_->metadataQuotaDiagnostic(quota);
    }

    core::UniquePtr<CommandEncoder> prepare(
        core::ArrayView<std::shared_ptr<RenderPipeline>> pipelines,
        core::ArrayView<OrderedGraphicsTarget> targets,
        const semantics::PreparedGraphicsCommandsRequest& request,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const char*& diagnostic)
    {
        return payload_->prepare(pipelines, targets, request, quota, diagnostic);
    }

    QueueSubmitResult submit(const CommandBuffer& command) { return payload_->submit(command); }

    const char* beginConformance(core::UniquePtr<ConformanceClearWorkload> clear,
        core::UniquePtr<ConformanceSampledWorkload> sampled,
        const OriginalConformanceRequest& request,
        const std::shared_ptr<core::CpuMemoryQuota>& quota)
    {
        return payload_->beginConformance(std::move(clear), std::move(sampled), request, quota);
    }

    const OriginalConformanceRun* conformanceRun() const noexcept
    {
        return payload_->conformanceRun();
    }

    // Original L2 facts only. Neither this borrow nor a successful CPU reuse
    // check grants GPU completion, native handles or an external publication.
    const RenderPipelineDesc* pipelineDefinition(
        const RenderPipeline& pipeline, const char*& diagnostic) const noexcept
    {
        return payload_->pipelineDefinition(pipeline, diagnostic);
    }

    const char* resourceReuseDiagnostic(const GpuBuffer& buffer) const noexcept
    {
        return payload_->reuseDiagnostic(buffer);
    }

    const char* resourceReuseDiagnostic(const GpuTexture& texture) const noexcept
    {
        return payload_->reuseDiagnostic(texture);
    }

    CommandValidationResult uploadTexture(
        GpuTexture& texture, const semantics::ClientTextureUpload& upload)
    {
        return payload_->uploadTexture(texture, upload);
    }

    const char* recycleCanvas(
        Swapchain& canvas, const AcquiredFrame& frame, core::UniquePtr<GpuTexture>& image)
    {
        return payload_->recycleCanvas(canvas, frame, image);
    }

    core::UniquePtr<Swapchain> createCanvas(const SwapchainDesc& desc, const char*& diagnostic)
    {
        return payload_->createCanvas(desc, diagnostic);
    }

private:
    friend class render_graph::FrameGraphResourcePool;
    friend class render_graph::FrameGraphUniformRingBuffer;
    core::UniquePtr<Payload> payload_;
    const semantics::GraphicsWorkloadRequirements requirements_;
};
} // namespace arp::rhi
