#include "platform/render_system_ordered_graphics.h"

#include "core/diagnostics/diagnostic_error.h"
#include "core/ownership/shared_owner_projection.h"
#include "platform/render_system.h"

namespace arp::platform {

RenderSystemOrderedGraphicsPath::RenderSystemOrderedGraphicsPath(ConstructionKey,
    core::UniquePtr<rhi::GpuDevice> device,
    std::shared_ptr<rhi::OriginalOrderedGraphicsExecution> execution,
    const RenderSystemOrderedGraphicsDesc& desc)
    : device_(std::move(device)), execution_(std::move(execution)), frameQuota_(desc.frameQuota),
      commandQuota_(desc.commandQuota),
      pool_(core::makeShared<render_graph::FrameGraphResourcePool>(*device_, 1)),
      commands_(desc.commands)
{
    surface_ = desc.surface;
}

RenderSystemOrderedGraphicsPath::~RenderSystemOrderedGraphicsPath() = default;

const char* RenderSystemOrderedGraphicsPath::rebuildCanvas()
{
    canvas_.reset();
    pool_->trimCachedResources();
    const char* diagnostic = nullptr;
    canvas_ = execution_->createCanvas(surface_, diagnostic);
    return diagnostic ? diagnostic : canvas_ ? nullptr : "PLATFORM-ORDERED-CANVAS-UNAVAILABLE";
}

core::UniquePtr<RenderSystemOrderedGraphicsPath> RenderSystem::createOrderedGraphicsPath(
    core::UniquePtr<rhi::GpuDevice> device,
    std::shared_ptr<rhi::OriginalOrderedGraphicsExecution> execution,
    const RenderSystemOrderedGraphicsDesc& desc,
    const char*& diagnostic)
{
    diagnostic = nullptr;
    if (desc.contractVersion != 1 || !desc.prototypeOptIn || !device || !execution) {
        diagnostic = "PLATFORM-ORDERED-CREATION-REQUEST";
        return {};
    }
    if ((diagnostic = execution->diagnostic(*device))
        || (diagnostic = rhi::semantics::PreparedGraphicsCommandsContract::validate(desc.commands)))
        return {};
    if (!desc.frameQuota || !desc.commandQuota
        || core::SharedOwnerProjection::overlapsOwner(desc.frameQuota, desc.commandQuota)
        || !desc.maxFrameCpuMetadataBytes
        || desc.frameQuota->limitBytes() != desc.maxFrameCpuMetadataBytes
        || desc.commandQuota->limitBytes() != desc.commands.issue.maxCpuMetadataBytes) {
        diagnostic = "PLATFORM-ORDERED-CPU-QUOTA";
        return {};
    }
    if ((diagnostic = execution->metadataQuotaDiagnostic(desc.frameQuota))
        || (diagnostic = execution->metadataQuotaDiagnostic(desc.commandQuota)))
        return {};
    try {
        core::CpuMemoryQuotaScope scope(desc.frameQuota);
        auto path = core::makeUnique<RenderSystemOrderedGraphicsPath>(
            RenderSystemOrderedGraphicsPath::ConstructionKey{},
            std::move(device),
            std::move(execution),
            desc);
        if ((diagnostic = path->rebuildCanvas()))
            return {};
        return path;
    } catch (const std::bad_alloc&) {
        diagnostic = "PLATFORM-ORDERED-CPU-ALLOCATION";
        return {};
    }
}

RenderSystemOrderedGraphicsFrameReport RenderSystemOrderedGraphicsPath::renderFrame(
    OrderedGraphicsFrameWorkload& workload)
{
    return renderFrame(workload, nullptr);
}

RenderSystemOrderedGraphicsFrameReport RenderSystemOrderedGraphicsPath::renderFrame(
    OrderedGraphicsFrameWorkload& workload, render_graph::OrderedGraphicsMeasurements* measurements)
{
    if (measurements) {
        auto* observer = measurements->preparationObserver;
        *measurements = {};
        measurements->preparationObserver = observer;
    }
    core::CpuWorkMeasurementScope totalMeasurement(measurements ? &measurements->total : nullptr);
    RenderSystemOrderedGraphicsFrameReport report;
    if (failed_ || paused_ || !canvas_ || !surface_.extent.width || !surface_.extent.height) {
        report.diagnostics = "PLATFORM-ORDERED-FRAME-CLOSED";
        return report;
    }
    if (const auto* id = execution_->diagnostic(*device_)) {
        report.diagnostics = id;
        return report;
    }
    auto frame = canvas_->acquireNextFrame();
    if (!frame.success) {
        report.diagnostics = frame.diagnostics;
        return report;
    }
    report.frameSerial = frame.frameSerial;
    try {
        core::CpuMemoryQuotaScope scope(frameQuota_);
        core::CpuWorkMeasurementScope buildMeasurement(
            measurements ? &measurements->build : nullptr);
        pendingGraph_ = core::makeUnique<render_graph::FrameGraph>(*device_, pool_);
        const auto image =
            pendingGraph_->importOrderedSwapchainTexture(*canvas_, frame, execution_);
        workload.build(*pendingGraph_, image);
        buildMeasurement.finish();
        pendingGraph_->executeOrdered(
            execution_, workload.pipelines(), commands_, commandQuota_, measurements);
        report.workAccepted = pendingGraph_->lastExecutionSubmitted();
        report.executedPasses = static_cast<std::uint32_t>(pendingGraph_->executionTrace().size());
        core::CpuWorkMeasurementScope presentMeasurement(
            measurements ? &measurements->presentRetire : nullptr);
        const auto presented = canvas_->present(frame, {});
        report.presented = presented.success;
        if (!presented.success) {
            failed_ = true;
            report.diagnostics = presented.diagnostics;
            return report;
        }
        auto originalImage = pendingGraph_->takeOrderedSwapchainTexture(image);
        pendingGraph_.reset();
        if (const auto pool = pool_->report(); pool.reuseFailureCount) {
            failed_ = true;
            report.diagnostics = pool.lastDiagnostic;
            return report;
        }
        if (const auto* id = execution_->recycleCanvas(*canvas_, frame, originalImage)) {
            failed_ = true;
            report.diagnostics = id;
            return report;
        }
        report.success = report.recycled = true;
    } catch (const std::exception& error) {
        // Preserve accepted work and original graph owners. No CPU failure or
        // callback exception is converted into completion or a retryable frame.
        report.workAccepted = pendingGraph_ && pendingGraph_->lastExecutionSubmitted();
        failed_ = true;
        report.diagnostics = error.what();
    }
    return report;
}

rhi::SwapchainLifecycleResult RenderSystemOrderedGraphicsPath::resize(rhi::Extent2D extent)
{
    if (failed_ || pendingGraph_ || !canvas_) {
        rhi::SwapchainLifecycleResult result;
        result.diagnostics = "PLATFORM-ORDERED-LIFECYCLE-CLOSED";
        return result;
    }
    auto result = canvas_->resize(extent);
    if (!result.success)
        return result;
    surface_.extent = extent;
    if (extent.width && extent.height) {
        if (const auto* id = rebuildCanvas()) {
            result.success = false;
            result.diagnostics = id;
            failed_ = true;
        }
    }
    return result;
}

rhi::SwapchainLifecycleResult RenderSystemOrderedGraphicsPath::setSurfaceAvailable(bool available)
{
    if (failed_ || pendingGraph_ || !canvas_) {
        rhi::SwapchainLifecycleResult result;
        result.diagnostics = "PLATFORM-ORDERED-LIFECYCLE-CLOSED";
        return result;
    }
    auto result = canvas_->setSurfaceAvailable(available);
    if (!result.success)
        return result;
    paused_ = !available;
    if (available && result.swapchainRebuildRequired && surface_.extent.width
        && surface_.extent.height) {
        if (const auto* id = rebuildCanvas()) {
            result.success = false;
            result.diagnostics = id;
            failed_ = true;
        }
    }
    return result;
}
} // namespace arp::platform
