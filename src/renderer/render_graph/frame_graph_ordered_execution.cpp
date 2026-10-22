#include "core/diagnostics/diagnostic_error.h"
#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/frame_graph_executor.h"
#include "renderer/render_graph/render_graph.h"

#include <algorithm>

namespace arp::render_graph {

TextureHandle FrameGraph::importOrderedSwapchainTexture(rhi::Swapchain& swapchain,
    const rhi::AcquiredFrame& frame,
    const std::shared_ptr<rhi::OriginalOrderedGraphicsExecution>& execution)
{
    if (!execution)
        throw core::DiagnosticError("RG-ORDERED-ISSUER-MISSING");
    if (const auto* id = execution->diagnostic(device_))
        throw core::DiagnosticError(id);
    if (orderedExecution_ && orderedExecution_ != execution)
        throw core::DiagnosticError("RG-ORDERED-ISSUER-MISMATCH");
    if (!swapchain.isFrameAcquired(frame))
        throw core::DiagnosticError("RG-ORDERED-ACQUISITION-STALE");
    auto image = swapchain.acquiredImage(frame);
    if (!image.success || !image.texture || image.ref.image != image.texture->id()
        || image.ref.frameSerial != frame.frameSerial
        || image.ref.state != rhi::ResourceState::ColorAttachment
        || image.texture->state() != image.ref.state
        || image.ref.extent.width != image.texture->desc().extent.width
        || image.ref.extent.height != image.texture->desc().extent.height
        || image.ref.format != image.texture->desc().format)
        throw core::DiagnosticError("RG-ORDERED-ACQUISITION-CONTRACT");
    const auto handle = importTexture(std::move(image.texture));
    textureRecord(handle).orderedSwapchainImage = true;
    // Browser handoff owns the ColorAttachment -> Present transition. The
    // graph must not invent a native present barrier or completed image.
    orderedExecution_ = execution;
    return handle;
}

core::UniquePtr<rhi::GpuTexture> FrameGraph::takeOrderedSwapchainTexture(TextureHandle handle)
{
    auto& record = textureRecord(handle);
    if (!orderedExecution_ || !record.orderedSwapchainImage || !record.texture)
        throw core::DiagnosticError("RG-ORDERED-IMAGE-RETURN");
    auto image = std::move(record.texture);
    // No subsequent compile/execution may resolve a moved-out image.
    record.orderedSwapchainImage = false;
    return image;
}

void FrameGraph::executeOrdered(
    const std::shared_ptr<rhi::OriginalOrderedGraphicsExecution>& execution,
    core::ArrayView<std::shared_ptr<rhi::RenderPipeline>> pipelines,
    const rhi::semantics::PreparedGraphicsCommandsRequest& request,
    const std::shared_ptr<core::CpuMemoryQuota>& commandQuota)
{
    executeOrdered(execution, pipelines, request, commandQuota, nullptr);
}

void FrameGraph::executeOrdered(
    const std::shared_ptr<rhi::OriginalOrderedGraphicsExecution>& execution,
    core::ArrayView<std::shared_ptr<rhi::RenderPipeline>> pipelines,
    const rhi::semantics::PreparedGraphicsCommandsRequest& request,
    const std::shared_ptr<core::CpuMemoryQuota>& commandQuota,
    OrderedGraphicsMeasurements* measurements)
{
    core::CpuWorkMeasurementScope compileMeasurement(
        measurements ? &measurements->compile : nullptr);
    lastExecutionSubmitted_ = false;
    if (!execution)
        throw core::DiagnosticError("RG-ORDERED-ISSUER-MISSING");
    if (const auto* id = execution->diagnostic(device_))
        throw core::DiagnosticError(id);
    if (orderedExecution_ && orderedExecution_ != execution)
        throw core::DiagnosticError("RG-ORDERED-ISSUER-MISMATCH");
    if (const auto* id = rhi::semantics::PreparedGraphicsCommandsContract::validate(request))
        throw core::DiagnosticError(id);
    if (hasExecutedCompiledGraph_ || submissionFence_)
        throw core::DiagnosticError("RG-ORDERED-GRAPH-ALREADY-EXECUTED");
    if ((request.contractVersion != 3 && request.contractVersion != 4) || passes_.empty()
        || passes_.size() > request.maxPasses
        || textures_.size() + buffers_.size() > request.issue.maxResourceOwners)
        throw core::DiagnosticError("RG-ORDERED-GRAPH-BUDGET");
    if (!subgraphs_.empty() || !subpassGraphs_.empty() || !gpuTimingPassScopes_.empty())
        throw core::DiagnosticError("RG-ORDERED-GRAPH-COMPOSITION-UNSUPPORTED");
    for (const auto& pass : passes_) {
        if (pass.kind != PassKind::Graphics || pass.queue != QueueRole::Graphics
            || pass.asyncComputeEligible || pass.submissionObserver || pass.subpassGroup
            || !pass.subpassInputBindings.empty() || !pass.renderPassCallback || pass.callback
            || !pass.rhiDispatchCommands.empty() || !pass.rhiTransferCommands.empty())
            throw core::DiagnosticError("RG-ORDERED-GRAPH-OPERATION-UNSUPPORTED");
        std::uint32_t colors = 0;
        for (const auto& attachment : pass.attachmentWrites)
            if (attachment.state == rhi::ResourceState::ColorAttachment)
                ++colors;
        if (colors != execution->requirements().colorAttachments)
            throw core::DiagnosticError("RG-ORDERED-GRAPH-ATTACHMENT-REQUIREMENTS");
    }
    for (const auto& texture : textures_) {
        const auto& desc = texture.desc;
        if (desc.extent.width > execution->requirements().textureWidth
            || desc.extent.height > execution->requirements().textureHeight || desc.sampleCount != 1
            || texture.transient || texture.placed
            || texture.externalFinalState != rhi::ResourceState::Undefined)
            throw core::DiagnosticError("RG-ORDERED-GRAPH-RESOURCE-REQUIREMENTS");
    }
    for (const auto& buffer : buffers_)
        if (buffer.transient)
            throw core::DiagnosticError("RG-ORDERED-GRAPH-RESOURCE-REQUIREMENTS");
    // Compile from the actual graph. The old public CompiledFrameGraph aggregate
    // has no role in granting this execution entry.
    auto compiled = compile();
    if (const auto* id = compiledRetirementExecutionDiagnostic(compiled))
        throw core::DiagnosticError(id);
    if (!compiled.aliasPlan().empty() || !compiled.aliasHandoffs().empty()
        || !compiled.subpassTopologies().empty()
        || compiled.barrierPlan().size() > request.maxTransitions)
        throw core::DiagnosticError("RG-ORDERED-GRAPH-LOWERING-UNSUPPORTED");
    allocateTransientTextures(compiled);
    // Ordered graph lifetime includes ordinary graph-owned resources, too.
    // Derive the exact logical cache bound from descriptors, never from a
    // fixed spare count or a fictitious GPU allocation measurement.
    std::uint64_t logicalBytes = 0;
    std::uint32_t resources = 0;
    for (const auto& texture : textures_) {
        if (texture.imported)
            continue;
        if (const auto* id = rhi::ResourceCacheAccounting::accumulate(logicalBytes,
                rhi::ResourceCacheAccounting::requireLogicalBytes(device_.backend(), texture.desc)))
            throw core::DiagnosticError(id);
        ++resources;
    }
    for (const auto& buffer : buffers_) {
        if (buffer.imported())
            continue;
        if (const auto* id = rhi::ResourceCacheAccounting::accumulate(logicalBytes,
                rhi::ResourceCacheAccounting::requireLogicalBytes(
                    device_.backend(), buffer.buffer->desc())))
            throw core::DiagnosticError(id);
        ++resources;
    }
    resourcePool_->configureForCompiledGraph(logicalBytes, resources);
    core::pmr::Vector<rhi::OrderedGraphicsTarget> targets{&core::defaultCpuMemoryResource()};
    targets.reserve(compiled.executablePassCount());
    for (const auto index : compiled.executionOrder()) {
        const auto& pass = compiled.passes()[index];
        if (pass.culled)
            continue;
        rhi::OrderedGraphicsTarget target;
        for (const auto& attachment : passes_[pass.originalPassIndex].attachmentWrites) {
            auto* texture = textureRecord(attachment.handle).resolveTexture();
            if (attachment.state == rhi::ResourceState::ColorAttachment)
                target.color = texture;
            else if (attachment.state == rhi::ResourceState::DepthAttachment)
                target.depthStencil = texture;
            else
                throw core::DiagnosticError("RG-ORDERED-GRAPH-ATTACHMENT-STATE");
        }
        // Catalog attachment pairs once; pass order and every write remain in
        // the compiled execution stream, including overwrite-after-read passes.
        if (std::none_of(targets.begin(), targets.end(), [&](const auto& existing) {
                return existing.color == target.color
                    && existing.depthStencil == target.depthStencil;
            }))
            targets.push_back(target);
    }
    const char* diagnostic = nullptr;
    compileMeasurement.finish();
    core::CpuWorkMeasurementScope prepareMeasurement(
        measurements ? &measurements->prepare : nullptr,
        measurements ? measurements->preparationObserver : nullptr);
    auto encoder = execution->prepare(pipelines, targets, request, commandQuota, diagnostic);
    prepareMeasurement.finish();
    if (!encoder)
        throw core::DiagnosticError(diagnostic ? diagnostic : "RG-ORDERED-ENCODER-MISSING");
    // Only the original native issuer can reach the no-fence executor branch.
    orderedExecution_ = execution;
    FrameGraphExecutor(*this).executeImpl(
        compiled, std::move(encoder), execution.get(), measurements);
    hasExecutedCompiledGraph_ = true;
    executedCompiledGraph_ = nullptr;
    reuseCompiledTrace_ = false;
}

void FrameGraphResourcePool::releaseOrdered(core::UniquePtr<rhi::GpuTexture>& texture,
    const std::shared_ptr<rhi::OriginalOrderedGraphicsExecution>& issuer) noexcept
{
    if (!texture)
        return;
    const auto* id = issuer ? issuer->diagnostic(device_) : "RG-ORDERED-ISSUER-MISSING";
    if (!id)
        id = issuer->payload_->reuseDiagnostic(*texture);
    if (id) {
        ++reuseFailureCount_;
        orderedReturnDiagnostic_ = id;
        texture.reset(); // Original native payload still owns all recorder references.
        return;
    }
    try {
        cacheTexture(texture, issuer);
    } catch (const std::exception&) {
        ++reuseFailureCount_;
        orderedReturnDiagnostic_ = "RG-ORDERED-CACHE-RETURN-FAILED";
        texture.reset();
    }
}

void FrameGraphResourcePool::releaseOrdered(core::UniquePtr<rhi::GpuBuffer>& buffer,
    const std::shared_ptr<rhi::OriginalOrderedGraphicsExecution>& issuer) noexcept
{
    if (!buffer)
        return;
    const auto* id = issuer ? issuer->diagnostic(device_) : "RG-ORDERED-ISSUER-MISSING";
    if (!id)
        id = issuer->payload_->reuseDiagnostic(*buffer);
    if (id) {
        ++reuseFailureCount_;
        orderedReturnDiagnostic_ = id;
        buffer.reset();
        return;
    }
    try {
        cacheBuffer(buffer, issuer);
    } catch (const std::exception&) {
        ++reuseFailureCount_;
        orderedReturnDiagnostic_ = "RG-ORDERED-CACHE-RETURN-FAILED";
        buffer.reset();
    }
}
} // namespace arp::render_graph
