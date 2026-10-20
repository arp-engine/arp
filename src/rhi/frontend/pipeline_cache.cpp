#include "rhi/gpu_device.h"
#include "rhi/gpu_pipeline.h"

namespace arp::rhi {

PipelineCache::PipelineCache(GpuDevice& device, std::pmr::memory_resource* memoryResource)
    : device_(device),
      memoryResource_(memoryResource ? memoryResource : &core::defaultCpuMemoryResource()),
      renderCache_(memoryResource_), computeCache_(memoryResource_), layoutCache_(memoryResource_)
{}

PipelineCache::~PipelineCache() = default;

bool PipelineCache::configureBudget(const semantics::CacheBudget& budget) noexcept
{
    if (!budget.valid() || entryCount() != 0u) {
        lastBudgetDiagnostic_ = !budget.valid() ? "RHI-PIPELINE-CACHE-BUDGET-INVALID"
                                                : "RHI-PIPELINE-CACHE-BUDGET-CONFIGURE-AFTER-USE";
        return false;
    }
    budget_ = budget;
    lastBudgetDiagnostic_ = nullptr;
    return true;
}

semantics::CacheBudgetStats PipelineCache::budgetStats() const noexcept
{
    return {budget_.valid(),
        budget_.valid() ? budget_.capacity : 0u,
        entryCount(),
        peakEntryCount_,
        budgetRejectionCount_};
}

RenderPipeline* PipelineCache::acquireRenderPipeline(const RenderPipelineDesc& desc)
{
    const auto key = stableRenderPipelineStateKey(desc);
    auto it = renderCache_.find(key);
    if (it != renderCache_.end()) {
        it->second.lastUsedFrameSerial = currentFrameSerial_;
        ++renderHitCount_;
        return it->second.pipeline.get();
    }
    if (budget_.valid() && entryCount() >= budget_.capacity) {
        ++budgetRejectionCount_;
        lastBudgetDiagnostic_ = "RHI-PIPELINE-CACHE-BUDGET-EXHAUSTED";
        return nullptr;
    }

    auto pipeline = device_.createRenderPipeline(desc);
    if (!pipeline) {
        return nullptr;
    }
    RenderEntry entry;
    entry.pipeline = std::move(pipeline);
    entry.lastUsedFrameSerial = currentFrameSerial_;
    auto* ptr = entry.pipeline.get();
    renderCache_.emplace(key, std::move(entry));
    peakEntryCount_ = std::max<std::uint64_t>(peakEntryCount_, entryCount());
    ++renderMissCount_;
    return ptr;
}

ComputePipeline* PipelineCache::acquireComputePipeline(const ComputePipelineDesc& desc)
{
    const auto key = stableComputePipelineStateKey(desc);
    auto it = computeCache_.find(key);
    if (it != computeCache_.end()) {
        it->second.lastUsedFrameSerial = currentFrameSerial_;
        ++computeHitCount_;
        return it->second.pipeline.get();
    }
    if (budget_.valid() && entryCount() >= budget_.capacity) {
        ++budgetRejectionCount_;
        lastBudgetDiagnostic_ = "RHI-PIPELINE-CACHE-BUDGET-EXHAUSTED";
        return nullptr;
    }

    auto pipeline = device_.createComputePipeline(desc);
    if (!pipeline) {
        return nullptr;
    }
    ComputeEntry entry;
    entry.pipeline = std::move(pipeline);
    entry.lastUsedFrameSerial = currentFrameSerial_;
    auto* ptr = entry.pipeline.get();
    computeCache_.emplace(key, std::move(entry));
    peakEntryCount_ = std::max<std::uint64_t>(peakEntryCount_, entryCount());
    ++computeMissCount_;
    return ptr;
}

PipelineLayout* PipelineCache::acquirePipelineLayout(const PipelineLayoutDesc& desc)
{
    const auto key = stablePipelineLayoutKey(desc);
    auto it = layoutCache_.find(key);
    if (it != layoutCache_.end()) {
        it->second.lastUsedFrameSerial = currentFrameSerial_;
        ++layoutHitCount_;
        return it->second.layout.get();
    }
    if (budget_.valid() && entryCount() >= budget_.capacity) {
        ++budgetRejectionCount_;
        lastBudgetDiagnostic_ = "RHI-PIPELINE-CACHE-BUDGET-EXHAUSTED";
        return nullptr;
    }

    auto layout = device_.createPipelineLayout(desc);
    if (!layout) {
        return nullptr;
    }
    LayoutEntry entry;
    entry.layout = std::move(layout);
    entry.lastUsedFrameSerial = currentFrameSerial_;
    auto* ptr = entry.layout.get();
    layoutCache_.emplace(key, std::move(entry));
    peakEntryCount_ = std::max<std::uint64_t>(peakEntryCount_, entryCount());
    ++layoutMissCount_;
    return ptr;
}

void PipelineCache::onFrameComplete()
{
    ++currentFrameSerial_;
}

void PipelineCache::evictStale(std::uint64_t currentFrameSerial, std::uint32_t maxUnusedFrames)
{
    const auto threshold =
        (currentFrameSerial > maxUnusedFrames) ? currentFrameSerial - maxUnusedFrames : 0;
    for (auto it = renderCache_.begin(); it != renderCache_.end();) {
        if (it->second.lastUsedFrameSerial < threshold) {
            it = renderCache_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = computeCache_.begin(); it != computeCache_.end();) {
        if (it->second.lastUsedFrameSerial < threshold) {
            it = computeCache_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = layoutCache_.begin(); it != layoutCache_.end();) {
        if (it->second.lastUsedFrameSerial < threshold) {
            it = layoutCache_.erase(it);
        } else {
            ++it;
        }
    }
}

std::uint32_t PipelineCache::entryCount() const noexcept
{
    return static_cast<std::uint32_t>(
        renderCache_.size() + computeCache_.size() + layoutCache_.size());
}

void PipelineCache::clearForShutdown()
{
    renderCache_.clear();
    computeCache_.clear();
    layoutCache_.clear();
}

void PipelineCache::resetTelemetry()
{
    renderHitCount_ = 0;
    renderMissCount_ = 0;
    computeHitCount_ = 0;
    computeMissCount_ = 0;
    layoutHitCount_ = 0;
    layoutMissCount_ = 0;
}

} // namespace arp::rhi
