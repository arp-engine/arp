#include "renderer/volumetric_cloud/cloud_prepared_resources.h"

#include "rhi/frontend/buffer_placement.h"

namespace arp::renderer::cloud {
using BufferLease = rhi::OriginalCommandBufferLease;

struct CloudPreparedResources::State {
    std::shared_ptr<const rhi::ComputeDispatchValidationDomain> dispatchDomain;
    std::shared_ptr<const core::AtomicCapacityLease> reservation;
    std::shared_ptr<const BufferLease> density;
    core::pmr::Vector<CloudPreparedHistorySlot> slots;
    // Destroy/drain history before dropping the persistent buffer owners.
    CloudPreparedHistory history;
};

CloudPreparedResources::CloudPreparedResources() = default;
CloudPreparedResources::~CloudPreparedResources() = default;

CloudResult CloudPreparedResources::validateCreate(const CloudPreparedResourceCreateContext& c,
    const rhi::DeviceCapabilities& capabilities,
    CloudPreparedResourcePlan& output) noexcept
{
    output = {};
    if (c.contractVersion != 1u || !c.prototypeOptIn)
        return CloudResult::failure("CLOUD-PREPARED-RESOURCES-OPT-IN");
    if (!c.cameraId)
        return CloudResult::failure("CLOUD-CAMERA-IDENTITY");
    if (const auto result = assets::cloud::CloudAssetCooker::validate(c.asset); !result.success)
        return CloudResult::failure(result.diagnostic);
    if (c.resources.assetBytes != c.asset.cells.size() * sizeof(core::Vec4))
        return CloudResult::failure("CLOUD-PREPARED-RESOURCES-ASSET-SIZE");
    if (!c.sampleMemory
        || (*c.sampleMemory != rhi::MemoryUsage::GpuOnly
            && *c.sampleMemory != rhi::MemoryUsage::CpuToGpu))
        return CloudResult::failure("CLOUD-PREPARED-RESOURCES-MEMORY-INTENT");
    rhi::BufferDesc placement;
    placement.sizeBytes = c.resources.assetBytes;
    placement.memory = *c.sampleMemory;
    placement.hostVisibleDeviceLocal = c.hostVisibleDeviceLocal;
    if (const auto* id = rhi::HostVisibleDeviceLocalBufferContract::validate(
            placement, c.device.backend(), capabilities.memory.hostVisibleDeviceLocalBuffers))
        return CloudResult::failure(id);
    CloudPreparedResourcePlan plan;
    if (const auto result = c.dispatchDomain
            ? CloudPreparedPipelines::planResources(c.selection,
                  c.resources,
                  capabilities.computeResources,
                  {c.device, c.dispatchDomain},
                  plan)
            : CloudPreparedPipelines::planResources(
                  c.selection, c.resources, capabilities.computeResources, plan);
        !result.success)
        return result;
    if (!capabilities.memory.hostVisible)
        return CloudResult::failure("CLOUD-CAPABILITY-UNAVAILABLE");
    const bool spatial = c.selection.isReconstruction();
    const auto resolvedBytes =
        spatial ? plan.reconstruction.resolvedBufferBytes : plan.raw.sampleBufferBytes;
    const auto initializeBytes = std::max(plan.raw.sampleBufferBytes, resolvedBytes);
    if (initializeBytes > c.maxCpuInitializationBytes || initializeBytes > SIZE_MAX
        || !c.maxCpuInitializationBytes || !c.initializationQuota
        || c.initializationQuota->limitBytes() != c.maxCpuInitializationBytes)
        return CloudResult::failure("CLOUD-PREPARED-RESOURCES-INITIALIZATION-BUDGET");
    if (!c.maxCpuOwnerBytes || !c.ownerQuota || c.ownerQuota->limitBytes() != c.maxCpuOwnerBytes
        || !c.sourceQuota || !c.sourceQuota->limitBytes() || !c.historyQuota
        || !c.historyQuota->limitBytes())
        return CloudResult::failure("CLOUD-PREPARED-RESOURCES-CPU-BUDGET");
    if (!c.retainedLogicalBytes
        || c.retainedLogicalBytes->capacity() != c.resources.config.gpuBudgetBytes)
        return CloudResult::failure("CLOUD-PREPARED-RESOURCES-RETAINED-BUDGET");
    if (const auto* id = c.device.originalDeviceLifetimeTicket().diagnostic())
        return CloudResult::failure(id);
    output = plan;
    return {};
}

CloudResult CloudPreparedResources::create(const CloudPreparedResourceCreateContext& c)
{
    if (state_)
        return CloudResult::failure("CLOUD-PREPARED-RESOURCES-ALREADY-CREATED");
    if (!c.ownerQuota || !c.maxCpuOwnerBytes || c.ownerQuota->limitBytes() != c.maxCpuOwnerBytes)
        return CloudResult::failure("CLOUD-PREPARED-RESOURCES-CPU-BUDGET");
    try {
        core::CpuMemoryQuotaScope owners(c.ownerQuota);
        const auto capabilities = c.device.capabilities();
        CloudPreparedResourcePlan plan;
        if (const auto result = validateCreate(c, capabilities, plan); !result.success)
            return result;
        const bool spatial = c.selection.isReconstruction();
        const auto resolvedBytes =
            spatial ? plan.reconstruction.resolvedBufferBytes : plan.raw.sampleBufferBytes;
        const auto initializeBytes = std::max(plan.raw.sampleBufferBytes, resolvedBytes);
        const auto retainedBytes =
            spatial ? plan.reconstruction.retainedGpuBytes : plan.raw.retainedGpuBytes;
        auto next = core::makeUnique<State>();
        next->dispatchDomain = c.dispatchDomain;
        next->reservation =
            core::makeShared<core::AtomicCapacityLease>(c.retainedLogicalBytes, retainedBytes);
        if (!next->reservation->charged())
            return CloudResult::failure("CLOUD-PREPARED-RESOURCES-RETAINED-BUDGET");
        const BufferLease::CapacityOwners capacity{next->reservation, {}};
        const auto generations =
            core::makeShared<core::AtomicCapacityBudget>(c.resources.config.framesInFlight);
        const auto makeBuffer =
            [&](const char* label,
                std::uint64_t size,
                bool writable,
                const void* data,
                std::shared_ptr<const BufferLease>& destination) -> CloudResult {
            rhi::BufferDesc desc;
            desc.label = label;
            desc.sizeBytes = size;
            desc.usage = rhi::ResourceUsage::StorageBuffer
                | (writable ? rhi::ResourceUsage::CopySrc : rhi::ResourceUsage::None);
            desc.memory = writable ? *c.sampleMemory : rhi::MemoryUsage::CpuToGpu;
            desc.hostVisibleDeviceLocal = c.hostVisibleDeviceLocal;
            desc.initialState =
                writable ? rhi::ResourceState::ShaderWrite : rhi::ResourceState::ShaderRead;
            auto object = c.device.createBuffer(desc);
            if (!object)
                return CloudResult::failure("CLOUD-PREPARED-RESOURCES-BUFFER-CREATE");
            if (const auto* id = c.device.validateOriginalResourceRegistration(*object))
                return CloudResult::failure(id);
            if (!c.device.writeBuffer(*object, data, size).success)
                return CloudResult::failure("CLOUD-PREPARED-RESOURCES-BUFFER-INITIALIZATION");
            if (writable)
                object->sealHostWrites();
            else
                object->sealWrites();
            const char* id = nullptr;
            auto lease = BufferLease::prepareColdOriginal(object,
                {1u, true, BufferLease::retainedOwnerCount, c.sourceQuota->limitBytes()},
                c.sourceQuota,
                id,
                capacity);
            if (!lease || (id = BufferLease::adoptOriginal(lease, object)))
                return CloudResult::failure(id ? id : "CLOUD-PREPARED-RESOURCES-LEASE");
            destination = std::move(lease);
            return {};
        };
        if (const auto result = makeBuffer("Cloud.DensityAsset",
                c.resources.assetBytes,
                false,
                c.asset.cells.data(),
                next->density);
            !result.success)
            return result;
        next->slots.resize(c.resources.config.framesInFlight);
        {
            core::CpuMemoryQuotaScope initialization(c.initializationQuota);
            core::pmr::Vector<std::byte> zeros(static_cast<std::size_t>(initializeBytes));
            for (auto& slot : next->slots) {
                // Buffer/wrapper owners consume the owner quota, while only
                // the reusable initialization bytes consume the scratch quota.
                core::CpuMemoryQuotaScope bufferOwners(c.ownerQuota);
                if (const auto result = makeBuffer(
                        "Cloud.Raw", plan.raw.sampleBufferBytes, true, zeros.data(), slot.raw);
                    !result.success)
                    return result;
                if (const auto result = makeBuffer(
                        "Cloud.Resolved", resolvedBytes, true, zeros.data(), slot.resolved);
                    !result.success)
                    return result;
            }
        }
        if (const auto result = next->history.create(c.device,
                c.cameraId,
                c.resources.config,
                plan.raw,
                c.asset.source,
                next->slots.data(),
                next->slots.size(),
                c.historyQuota,
                spatial ? &c.selection.reconstruction : nullptr,
                capacity,
                generations,
                c.sourceQuota);
            !result.success)
            return result;
        plan_ = plan;
        selection_ = c.selection;
        state_ = std::move(next);
        return {};
    } catch (const std::bad_alloc&) {
        return CloudResult::failure("CLOUD-PREPARED-RESOURCES-ALLOCATION");
    }
}

CloudResult CloudPreparedResources::prepareFrame(const CloudGraphContext& context)
{
    if (state_ && state_->dispatchDomain)
        if (const auto* id = state_->dispatchDomain->diagnostic())
            return CloudResult::failure(id);
    return state_ ? state_->history.prepareFrame(context)
                  : CloudResult::failure("CLOUD-PREPARED-RESOURCES-NOT-CREATED");
}

CloudResult CloudPreparedResources::pendingBuffers(CloudPreparedFrameBuffers& output) const noexcept
{
    output = {};
    if (!state_ || !state_->history.raw())
        return CloudResult::failure("CLOUD-PREPARED-RESOURCES-NO-PENDING-FRAME");
    if (state_->dispatchDomain)
        if (const auto* id = state_->dispatchDomain->diagnostic())
            return CloudResult::failure(id);
    const auto& h = state_->history;
    output = {h.constants(),
        state_->density,
        h.raw(),
        h.previous(),
        h.resolved(),
        h.reconstructionConstants(),
        h.token().generation};
    return {};
}

CloudPreparedHistory* CloudPreparedResources::history() noexcept
{
    return state_ ? &state_->history : nullptr;
}

core::ArrayView<CloudPreparedHistorySlot> CloudPreparedResources::historySlots() const noexcept
{
    return state_ ? core::ArrayView<CloudPreparedHistorySlot>{state_->slots}
                  : core::ArrayView<CloudPreparedHistorySlot>{};
}

CloudResult CloudPreparedResources::releaseIdle() noexcept
{
    if (state_)
        if (const auto result = state_->history.releaseIdle(); !result.success)
            return result;
    release();
    return {};
}

void CloudPreparedResources::release() noexcept
{
    state_.reset();
    plan_ = {};
    selection_ = {};
}
} // namespace arp::renderer::cloud
