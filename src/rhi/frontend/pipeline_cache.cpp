#include "rhi/frontend/pipeline_definition_identity.h"
#include "rhi/frontend/pipeline_state_copy.h"
#include "rhi/gpu_device.h"
#include "rhi/gpu_pipeline.h"

namespace arp::rhi {

PipelineCache::PipelineCache(GpuDevice& device, std::pmr::memory_resource* memoryResource)
    : device_(device),
      memoryResource_(memoryResource ? memoryResource : &core::defaultCpuMemoryResource()),
      renderCache_(memoryResource_), computeCache_(memoryResource_), layoutCache_(memoryResource_),
      sharedRenderCache_(memoryResource_), sharedLayoutCache_(memoryResource_)
{}

struct RenderPipelineLease::State {
    explicit State(const RenderPipelineDesc& source, core::SharedPtr<PipelineLayout> owner)
        : shader(ShaderPackageStateCopy::copy(*source.shaderPackage)), layout(std::move(owner)),
          definition(RenderPipelineStateCopy::copy(source))
    {
        definition.shaderPackage = &shader;
        definition.layout = layout.get();
    }

    ShaderPackageDesc shader;
    core::SharedPtr<PipelineLayout> layout;
    RenderPipelineDesc definition;
    core::UniquePtr<RenderPipeline> pipeline;
};

RenderPipeline* RenderPipelineLease::get() const noexcept
{
    return state_ ? state_->pipeline.get() : nullptr;
}

core::SharedPtr<PipelineLayout> PipelineCache::acquireSharedPipelineLayout(
    const PipelineLayoutDesc& desc)
{
    const auto key = stablePipelineLayoutKey(desc);
    for (auto& entry : sharedLayoutCache_)
        if (entry.key == key && PipelineDefinitionIdentity::matches(entry.layout->desc(), desc)) {
            entry.lastUsedFrameSerial = currentFrameSerial_;
            ++layoutHitCount_;
            return entry.layout;
        }
    if (budget_.valid() && entryCount() >= budget_.capacity) {
        ++budgetRejectionCount_;
        lastBudgetDiagnostic_ = "RHI-PIPELINE-CACHE-BUDGET-EXHAUSTED";
        return {};
    }
    auto layout = core::shareUnique(device_.createPipelineLayout(desc));
    if (!layout)
        return {};
    sharedLayoutCache_.push_back({key, layout, currentFrameSerial_});
    peakEntryCount_ = std::max<std::uint64_t>(peakEntryCount_, entryCount());
    ++layoutMissCount_;
    return layout;
}

RenderPipelineLease PipelineCache::acquireRenderPipelineLease(const RenderPipelineDesc& desc)
{
    // Validation cannot be skipped because another descriptor happened to
    // hash the same. In particular a cache hit grants no artifact authority.
    if (!desc.layout || !desc.shaderPackage
        || !validateGeneratedShaderPackageInput(*desc.shaderPackage, false).success
        || !device_.validateRenderPipeline(desc).success)
        return {};
    auto layout = acquireSharedPipelineLayout(desc.layout->desc());
    if (!layout)
        return {};
    auto normalized = RenderPipelineStateCopy::copy(desc);
    normalized.layout = layout.get();
    const auto key = stableRenderPipelineStateKey(normalized);
    for (auto& entry : sharedRenderCache_)
        if (entry.key == key
            && PipelineDefinitionIdentity::matches(entry.state->definition, normalized)) {
            entry.lastUsedFrameSerial = currentFrameSerial_;
            ++renderHitCount_;
            RenderPipelineLease lease;
            lease.state_ = entry.state;
            return lease;
        }
    if (budget_.valid() && entryCount() >= budget_.capacity) {
        ++budgetRejectionCount_;
        lastBudgetDiagnostic_ = "RHI-PIPELINE-CACHE-BUDGET-EXHAUSTED";
        return {};
    }
    auto state = core::makeShared<RenderPipelineLease::State>(normalized, std::move(layout));
    state->pipeline = device_.createRenderPipeline(state->definition);
    if (!state->pipeline)
        return {};
    sharedRenderCache_.push_back({key, state, currentFrameSerial_});
    peakEntryCount_ = std::max<std::uint64_t>(peakEntryCount_, entryCount());
    ++renderMissCount_;
    RenderPipelineLease lease;
    lease.state_ = std::move(state);
    return lease;
}

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
    if (desc.shaderPackage
        && !validateGeneratedShaderPackageInput(*desc.shaderPackage, false).success)
        return nullptr;
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
    if (desc.shaderPackage
        && !validateGeneratedShaderPackageInput(*desc.shaderPackage, false).success)
        return nullptr;
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
    sharedRenderCache_.erase(std::remove_if(sharedRenderCache_.begin(),
                                 sharedRenderCache_.end(),
                                 [&](const auto& entry) {
                                     return entry.lastUsedFrameSerial < threshold
                                         && entry.state.use_count() == 1;
                                 }),
        sharedRenderCache_.end());
    sharedLayoutCache_.erase(std::remove_if(sharedLayoutCache_.begin(),
                                 sharedLayoutCache_.end(),
                                 [&](const auto& entry) {
                                     return entry.lastUsedFrameSerial < threshold
                                         && entry.layout.use_count() == 1;
                                 }),
        sharedLayoutCache_.end());
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
    return static_cast<std::uint32_t>(renderCache_.size() + computeCache_.size()
        + layoutCache_.size() + sharedRenderCache_.size() + sharedLayoutCache_.size());
}

void PipelineCache::clearForShutdown()
{
    sharedRenderCache_.clear();
    sharedLayoutCache_.clear();
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
