#include "renderer/volumetric_cloud/cloud_prepared_history.h"

#include "renderer/render_graph/original_frame_graph_execution.h"

namespace arp::renderer::cloud {

CloudResult CloudPreparedHistory::create(rhi::GpuDevice& device,
    std::uint64_t cameraId,
    const CloudRenderConfig& config,
    const CloudExecutionPlan& plan,
    const assets::cloud::CloudCookDesc& asset,
    const CloudPreparedHistorySlot* resources,
    std::size_t slotCount,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    const CloudReconstructionConfig* reconstruction,
    const rhi::OriginalCommandBufferLease::CapacityOwners& capacityOwners,
    const std::shared_ptr<core::AtomicCapacityBudget>& immutableGenerations,
    const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota)
{
    if (device_ || !idle())
        return CloudResult::failure("CLOUD-PREPARED-HISTORY-ALREADY-CREATED");
    for (const auto& owner : capacityOwners)
        if (owner && !owner->charged())
            return CloudResult::failure("RHI-ORIGINAL-SOURCE-LEASE-CAPACITY-RELEASED");
    if (immutableGenerations
        && (capacityOwners[1] || immutableGenerations->capacity() != config.framesInFlight))
        return CloudResult::failure("CLOUD-PREPARED-HISTORY-GENERATION-BUDGET");
    std::uint32_t cells = 0u;
    const auto assetPlan = assets::cloud::CloudAssetCooker::plan(asset, cells);
    if (!assetPlan.success)
        return CloudResult::failure(assetPlan.diagnostic);
    CloudExecutionPlan expected;
    const auto described = CloudExecutionPlanner::describeResources(config,
        plan.extent,
        std::uint64_t(cells) * sizeof(core::Vec4),
        sizeof(CloudGpuConstants),
        expected);
    if (!described.success)
        return described;
    CloudReconstructionResources reconstructed;
    if (reconstruction) {
        const auto result = CloudReconstruction::describeResources(*reconstruction,
            config,
            plan.extent,
            std::uint64_t(cells) * sizeof(core::Vec4),
            sizeof(CloudGpuConstants),
            reconstructed);
        if (!result.success)
            return result;
    }
    if (!cameraId || !quota || !quota->limitBytes() || (sourceQuota && !sourceQuota->limitBytes())
        || !resources || slotCount < 2u || slotCount != config.framesInFlight
        || !plan.sampleBufferBytes || !config.resolutionDivisor || !plan.extent.width
        || !plan.extent.height || plan.sampleExtent.width != expected.sampleExtent.width
        || plan.sampleExtent.height != expected.sampleExtent.height
        || plan.sampleCount != std::uint64_t(plan.sampleExtent.width) * plan.sampleExtent.height
        || plan.sampleCount > UINT32_MAX
        || plan.sampleBufferBytes != plan.sampleCount * kCloudSampleBytes)
        return CloudResult::failure("CLOUD-PREPARED-HISTORY-CONFIG");
    if (plan.dispatchGroups != expected.dispatchGroups
        || plan.retainedGpuBytes != expected.retainedGpuBytes
        || plan.densityEvaluationBound != expected.densityEvaluationBound)
        return CloudResult::failure("CLOUD-PREPARED-HISTORY-CONFIG");
    for (std::size_t i = 0u; i < slotCount; ++i)
        for (const auto& lease : {resources[i].raw, resources[i].resolved})
            if (const auto* id = rhi::OriginalCommandBufferLease::validateOriginal(lease))
                return CloudResult::failure(id);
    for (std::size_t i = 0u; i < slotCount; ++i)
        for (const auto& lease : {resources[i].raw, resources[i].resolved}) {
            const auto& buffer = *lease->originalObject();
            // Null is a validator; original preparation separately requires
            // native registration on every native backend.
            if (const auto* id = device.validateOriginalResourceRegistration(buffer))
                return CloudResult::failure(id);
            if (!buffer.hostWritesSealed() || buffer.gpuWritesSealed()
                || (buffer.desc().usage & rhi::ResourceUsage::StorageBuffer)
                    == rhi::ResourceUsage::None
                || buffer.desc().sizeBytes < (reconstruction && lease == resources[i].resolved
                           ? reconstructed.resolvedBufferBytes
                           : plan.sampleBufferBytes))
                return CloudResult::failure("CLOUD-PREPARED-HISTORY-BUFFER");
            for (std::size_t j = 0u; j < i; ++j)
                for (const auto& other : {resources[j].raw, resources[j].resolved})
                    if (other && other->originalObject() == &buffer)
                        return CloudResult::failure("CLOUD-PREPARED-HISTORY-ALIAS");
            if (resources[i].raw->originalObject() == resources[i].resolved->originalObject())
                return CloudResult::failure("CLOUD-PREPARED-HISTORY-ALIAS");
        }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        core::pmr::Vector<Slot> next(&core::defaultCpuMemoryResource());
        next.reserve(slotCount);
        for (std::size_t i = 0u; i < slotCount; ++i)
            next.push_back({resources[i], {}, {}});
        slots_ = std::move(next);
    } catch (const std::bad_alloc&) {
        return CloudResult::failure("CLOUD-PREPARED-HISTORY-CPU-BUDGET");
    }
    device_ = &device;
    quota_ = quota;
    sourceQuota_ = sourceQuota ? sourceQuota : quota;
    capacityOwners_ = capacityOwners;
    immutableGenerations_ = immutableGenerations;
    cameraId_ = cameraId;
    config_ = config;
    plan_ = plan;
    reconstruction_ = reconstruction ? *reconstruction : CloudReconstructionConfig{};
    reconstructionPlan_ = reconstructed;
    asset_ = asset;
    densityBytes_ = std::uint64_t(cells) * sizeof(core::Vec4);
    resetHistory();
    return {};
}

bool CloudPreparedHistory::idle() const noexcept
{
    if (pending_)
        return false;
    for (const auto& slot : slots_)
        if (slot.completion && !slot.completion->isSignaled())
            return false;
    return true;
}

bool CloudPreparedHistory::frameReady() const noexcept
{
    if (!device_ || pending_)
        return false;
    for (std::size_t i = 0u; i < slots_.size(); ++i)
        if ((!historyValid_ || i != previousSlot_)
            && (!slots_[i].completion || slots_[i].completion->isSignaled()))
            return true;
    return false;
}

CloudResult CloudPreparedHistory::prepareFrame(const CloudGraphContext& context)
{
    if (!device_ || pending_)
        return CloudResult::failure("CLOUD-PREPARED-HISTORY-NOT-READY");
    if (context.cameraId != cameraId_)
        return CloudResult::failure("CLOUD-CAMERA-IDENTITY");
    // A signaled completion may still retain the submitted original command
    // and all its resources. Release that custody during cold preparation,
    // before admitting the next immutable generation against live budgets.
    for (auto& slot : slots_)
        if (slot.completion && slot.completion->isSignaled())
            slot.completion.reset();
    // These are observers of a submitted generation. Real graph/Source owners
    // retain its immutable buffers independently until they release them.
    pendingConstants_.reset();
    pendingReconstructionConstants_.reset();
    CloudFrameState next;
    const auto result = CloudFrameParameters::build(context,
        device_->backend(),
        config_,
        plan_,
        asset_,
        samplingFrame_,
        historyValid_ ? &slots_[previousSlot_].frame : nullptr,
        next);
    if (!result.success)
        return result;
    std::size_t index = slots_.size();
    for (std::size_t i = 0u; i < slots_.size(); ++i)
        if ((!historyValid_ || i != previousSlot_)
            && (!slots_[i].completion || slots_[i].completion->isSignaled())) {
            index = i;
            break;
        }
    if (index == slots_.size())
        return CloudResult::failure("CLOUD-FRAMES-IN-FLIGHT");
    try {
        core::CpuMemoryQuotaScope scope(quota_);
        auto capacity = capacityOwners_;
        if (immutableGenerations_) {
            capacity[1] = core::makeShared<core::AtomicCapacityLease>(immutableGenerations_, 1u);
            if (!capacity[1]->charged())
                return CloudResult::failure("CLOUD-PREPARED-HISTORY-GENERATION-CAPACITY");
        }
        const auto makeLease = [&](core::UniquePtr<rhi::GpuBuffer>& object, const char*& id) {
            auto lease = rhi::OriginalCommandBufferLease::prepareColdOriginal(object,
                {1u,
                    true,
                    rhi::OriginalCommandBufferLease::retainedOwnerCount,
                    sourceQuota_->limitBytes()},
                sourceQuota_,
                id,
                capacity);
            if (lease && (id = rhi::OriginalCommandBufferLease::adoptOriginal(lease, object)))
                lease.reset();
            return lease;
        };
        rhi::BufferDesc desc;
        desc.label = "Cloud.PreparedConstants";
        desc.sizeBytes = sizeof(CloudGpuConstants);
        desc.usage = rhi::ResourceUsage::UniformBuffer;
        desc.memory = rhi::MemoryUsage::CpuToGpu;
        desc.initialState = rhi::ResourceState::ShaderRead;
        auto buffer = device_->createBuffer(desc);
        if (!buffer)
            return CloudResult::failure("CLOUD-PREPARED-HISTORY-CONSTANT-CREATE");
        if (!device_->writeBuffer(*buffer, &next.parameters, sizeof(next.parameters)).success)
            return CloudResult::failure("CLOUD-CONSTANT-UPLOAD");
        buffer->sealWrites();
        const char* id = nullptr;
        auto lease = makeLease(buffer, id);
        if (!lease)
            return CloudResult::failure(id ? id : "CLOUD-PREPARED-HISTORY-CONSTANT-LEASE");
        std::shared_ptr<const rhi::OriginalCommandBufferLease> policyLease;
        if (reconstruction_.contractVersion) {
            const CloudReconstructionGpuConstants policy{reconstruction_.contractVersion,
                reconstruction_.maxRadianceRange,
                reconstruction_.maxTransmittanceRange,
                0u};
            desc.sizeBytes = sizeof(policy);
            auto policyBuffer = device_->createBuffer(desc);
            if (!policyBuffer)
                return CloudResult::failure("CLOUD-RECONSTRUCTION-CONSTANT-CREATE");
            if (!device_->writeBuffer(*policyBuffer, &policy, sizeof(policy)).success)
                return CloudResult::failure("CLOUD-RECONSTRUCTION-CONSTANT-UPLOAD");
            policyBuffer->sealWrites();
            policyLease = makeLease(policyBuffer, id);
            if (!policyLease)
                return CloudResult::failure(id ? id : "CLOUD-RECONSTRUCTION-CONSTANT-LEASE");
        }
        pendingConstants_ = std::move(lease);
        pendingReconstructionConstants_ = std::move(policyLease);
    } catch (const std::bad_alloc&) {
        return CloudResult::failure("CLOUD-PREPARED-HISTORY-CPU-BUDGET");
    }
    pendingFrame_ = next;
    pendingSlot_ = static_cast<std::uint32_t>(index);
    pending_ = true;
    executionValidated_ = false;
    ++generation_;
    return {};
}

CloudResult CloudPreparedHistory::resetHistory() noexcept
{
    if (pending_)
        return CloudResult::failure("CLOUD-RESET-PENDING-FRAME");
    historyValid_ = false;
    samplingFrame_ = 0u;
    return {};
}

CloudResult CloudPreparedHistory::cancelPendingFrame() noexcept
{
    if (executionValidated_)
        return CloudResult::failure("CLOUD-CANCEL-AFTER-EXECUTION-VALIDATION");
    pending_ = false;
    pendingConstants_.reset();
    pendingReconstructionConstants_.reset();
    ++generation_;
    return {};
}

CloudResult CloudPreparedHistory::releaseIdle() noexcept
{
    if (!idle())
        return CloudResult::failure("CLOUD-RESIZE-IN-FLIGHT");
    slots_ = core::pmr::Vector<Slot>{};
    pendingConstants_.reset();
    pendingReconstructionConstants_.reset();
    device_ = nullptr;
    densityBytes_ = 0u;
    quota_.reset();
    sourceQuota_.reset();
    capacityOwners_ = {};
    immutableGenerations_.reset();
    resetHistory();
    ++generation_;
    return {};
}

void CloudPreparedHistory::release() noexcept
{
    if (device_ && !idle())
        device_->waitIdle();
    pending_ = executionValidated_ = false;
    // After native drain, completion metadata can be discarded even when a
    // failed submission never supplied the observer with a completion fence.
    slots_ = core::pmr::Vector<Slot>{};
    pendingConstants_.reset();
    pendingReconstructionConstants_.reset();
    device_ = nullptr;
    densityBytes_ = 0u;
    quota_.reset();
    sourceQuota_.reset();
    capacityOwners_ = {};
    immutableGenerations_.reset();
    resetHistory();
    ++generation_;
}

const char* CloudPreparedHistory::validateGraph(const CloudPreparedGraphContext& context) const
{
    if (!pending_ || context.history != this || context.historyGeneration != generation_)
        return "CLOUD-PREPARED-HISTORY-STALE-GENERATION";
    auto& graph = context.execution.graph();
    if (reconstruction_.contractVersion) {
        if (context.contractVersion != (reconstruction_.contractVersion == 2u ? 4u : 3u)
            || !context.reconstruction || !pendingReconstructionConstants_
            || context.reconstruction->resolvedCount != reconstructionPlan_.resolvedCount
            || context.reconstruction->sampleContractVersion
                != reconstructionPlan_.sampleContractVersion
            || context.reconstruction->resolvedBufferBytes
                != reconstructionPlan_.resolvedBufferBytes
            || context.reconstruction->dispatchGroups != reconstructionPlan_.dispatchGroups
            || context.reconstruction->retainedGpuBytes != reconstructionPlan_.retainedGpuBytes
            || context.reconstruction->densityEvaluationBound
                != reconstructionPlan_.densityEvaluationBound
            || &graph.buffer(context.reconstructionConstants)
                != pendingReconstructionConstants_->originalObject())
            return "CLOUD-RECONSTRUCTION-HISTORY-BINDING";
    } else if (context.reconstruction || context.contractVersion >= 3u)
        return "CLOUD-RECONSTRUCTION-HISTORY-VERSION";
    if (context.plan.extent.width != plan_.extent.width
        || context.plan.extent.height != plan_.extent.height
        || context.plan.sampleExtent.width != plan_.sampleExtent.width
        || context.plan.sampleExtent.height != plan_.sampleExtent.height
        || context.plan.sampleBufferBytes != plan_.sampleBufferBytes
        || &graph.buffer(context.constants) != pendingConstants_->originalObject()
        || &graph.buffer(context.raw) != raw()->originalObject()
        || &graph.buffer(context.resolved) != resolved()->originalObject()
        || &graph.buffer(context.previous) != previous()->originalObject())
        return "CLOUD-PREPARED-HISTORY-RESOURCE-BINDING";
    return nullptr;
}

const char* CloudPreparedHistory::validateSubmission(
    render_graph::PassSubmissionToken token) const noexcept
{
    if (!pending_ || token.ownerId != cameraId_ || token.generation != generation_)
        return "CLOUD-STALE-SUBMISSION";
    executionValidated_ = true;
    return nullptr;
}

void CloudPreparedHistory::onSubmitted(render_graph::PassSubmissionToken token,
    const std::shared_ptr<const rhi::GpuFence>& completion) noexcept
{
    if (validateSubmission(token) || !completion) {
        historyValid_ = false;
        return;
    }
    auto& slot = slots_[pendingSlot_];
    slot.frame = pendingFrame_;
    slot.completion = completion;
    if (pendingFrame_.historyUsed)
        slots_[previousSlot_].completion = completion;
    else if (reconstruction_.contractVersion)
        slots_[(pendingSlot_ + 1u) % slots_.size()].completion = completion;
    previousSlot_ = pendingSlot_;
    pending_ = executionValidated_ = false;
    historyValid_ = true;
    ++samplingFrame_;
}

} // namespace arp::renderer::cloud
