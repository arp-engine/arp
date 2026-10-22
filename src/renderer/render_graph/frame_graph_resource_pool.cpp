#include "renderer/render_graph/frame_graph_resource_pool.h"

#include "core/algorithm/stable_vector_erase.h"
#include "core/container/bounded_sequence.h"
#include "core/math/unsigned_range.h"
#include "core/memory/cpu_allocator.h"
#include "core/ownership/bounded_retainer.h"
#include "renderer/render_graph/render_graph.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace arp::render_graph {

namespace {

bool layoutsMatch(
    const FrameGraphAliasedResourceLayout& left, const FrameGraphAliasedResourceLayout& right)
{
    if (left.heapSizes != right.heapSizes || left.heapAlignments != right.heapAlignments
        || left.textureHeapIndices != right.textureHeapIndices
        || left.textureOffsets != right.textureOffsets
        || left.textureDescs.size() != right.textureDescs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.textureDescs.size(); ++index) {
        const auto& a = left.textureDescs[index];
        const auto& b = right.textureDescs[index];
        if (a.extent.width != b.extent.width || a.extent.height != b.extent.height
            || a.extent.depth != b.extent.depth || a.format != b.format || a.usage != b.usage
            || a.memory != b.memory || a.initialState != b.initialState
            || a.mipLevelCount != b.mipLevelCount || a.arrayLayerCount != b.arrayLayerCount
            || a.sampleCount != b.sampleCount || a.cubeCompatible != b.cubeCompatible) {
            return false;
        }
    }
    return true;
}

} // namespace

struct FrameGraphResourcePool::AllocationAccountingState {
    AllocationAccountingState(const rhi::semantics::FrameGraphPoolAllocationBudget& budget,
        rhi::OriginalDeviceLifetime::Ticket device,
        rhi::Backend backend,
        std::shared_ptr<core::CpuMemoryQuota> quota) noexcept
        : budget(budget), device(std::move(device)), backend(backend), quota(std::move(quota)),
          textures(budget.maxTextures), buffers(budget.maxBuffers),
          textureBytes(budget.maxLogicalTextureBytes), bufferBytes(budget.maxLogicalBufferBytes),
          heapLeases(budget.heapLeases.maxHeapLeases),
          heapBytes(budget.heapLeases.maxLogicalHeapReservationBytes)
    {}

    std::pair<core::AtomicCapacityBudget*, core::AtomicCapacityBudget*> budgets(
        rhi::OriginalRegisteredResourceKind kind) noexcept
    {
        switch (kind) {
        case rhi::OriginalRegisteredResourceKind::Texture:
            return {&textures, &textureBytes};
        case rhi::OriginalRegisteredResourceKind::Buffer:
            return {&buffers, &bufferBytes};
        case rhi::OriginalRegisteredResourceKind::MemoryHeap:
            return {&heapLeases, &heapBytes};
        default:
            return {nullptr, nullptr};
        }
    }

    const rhi::semantics::FrameGraphPoolAllocationBudget budget;
    const rhi::OriginalDeviceLifetime::Ticket device;
    const rhi::Backend backend;
    const std::shared_ptr<core::CpuMemoryQuota> quota;
    core::AtomicCapacityBudget textures, buffers, textureBytes, bufferBytes, heapLeases, heapBytes;
    std::optional<AllocationPartitions> partitions;
    AllocationPartitions::Lease fixedStorage;
};

struct FrameGraphResourcePool::AllocationReceipt final : rhi::OriginalPoolAllocationReceipt {
    AllocationReceipt(ConstructionKey key,
        const std::shared_ptr<AllocationAccountingState>& state,
        rhi::OriginalResourceRegistrationTicket registration,
        rhi::OriginalRegisteredResourceKind kind,
        std::uint64_t bytes)
        : OriginalPoolAllocationReceipt(key, std::move(registration), state.get()),
          count(std::shared_ptr<core::AtomicCapacityBudget>(state, state->budgets(kind).first), 1u),
          logicalBytes(
              std::shared_ptr<core::AtomicCapacityBudget>(state, state->budgets(kind).second),
              bytes)
    {
        if (!count.charged() || !logicalBytes.charged())
            throw core::DiagnosticError("RG-POOL-ALLOCATION-RESOURCE-CAPACITY");
    }

    core::AtomicCapacityLease count, logicalBytes;
    AllocationPartitions::Identity partitionIdentity;
};

const char* FrameGraphResourcePool::prepareAllocationAccounting(
    const rhi::semantics::FrameGraphPoolAllocationBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota)
{
    if (allocationAccounting_) {
        if (allocationAccounting_->partitions)
            if (const auto* id = allocationAccounting_->partitions->threadDiagnostic())
                return id;
        if (const auto* id = allocationAccounting_->device.diagnostic())
            return id;
        if (allocationAccounting_->partitions) {
            const auto snapshot = allocationAccounting_->partitions->snapshot();
            if (snapshot.diagnostic)
                return snapshot.diagnostic;
            if (snapshot.liveOwners > 1u)
                return "RG-POOL-ALLOCATION-BUSY";
        }
    }
    using Contract = rhi::semantics::FrameGraphPoolAllocationContract;
    if (const auto* id = Contract::validateIntent(budget))
        return id;
    if (graphOwners_.used())
        return "RG-POOL-ALLOCATION-GRAPH-ACTIVE";
    if (budget.graphOwners.requested())
        if (const auto* id =
                rhi::semantics::FrameGraphOwnedResourcesContract::lowering(device_.backend())
                    .diagnostic)
            return id;
    if (budget.heapLeases.requested()) {
        if (const auto* id =
                Contract::heapLowering(device_.backend(), budget.heapLeases).diagnostic)
            return id;
        if (const auto* id = device_.originalPoolHeapAllocationRegistrationDiagnostic())
            return id;
    }
    if (const auto* id = Contract::lowering(device_.backend()).diagnostic)
        return id;
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes)
        return "RG-POOL-ALLOCATION-QUOTA";
    if (device_.overlapsOriginalDeviceCpuOwner(quota)
        || overlapsRetirementMetadataQuota(quota, false))
        return "RG-POOL-ALLOCATION-QUOTA-DOMAIN";
    if (const auto* id = device_.originalPoolAllocationRegistrationDiagnostic())
        return id;
    if (allocationAccounting_
        && (allocationAccounting_->textures.used() || allocationAccounting_->buffers.used()
            || allocationAccounting_->heapLeases.used()))
        return "RG-POOL-ALLOCATION-BUSY";
    if (!allocationAccounting_ && (missCount_ || bufferMissCount_))
        return "RG-POOL-ALLOCATION-PRIOR-ACQUIRE";
    if (poolSize_ || bufferPoolSize_ || pendingRecycleCount_ || pendingBufferRecycleCount_
        || cachedAliasedResourceCount_ || pendingAliasedResourceCount_
        || !cachedAliasedGroups_.empty() || !pendingAliasedGroups_.empty())
        return "RG-POOL-ALLOCATION-NONEMPTY";
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto replacement = core::makeShared<AllocationAccountingState>(
            budget, device_.originalDeviceLifetimeTicket(), device_.backend(), quota);
        if (budget.partitions.requested()) {
            const char* diagnostic = nullptr;
            auto ledger = AllocationPartitions::prepareCold(budget.partitions.capacity,
                rhi::semantics::frameGraphRetirementFields,
                budget.partitions.maxResourceOwners + 1u,
                quota,
                diagnostic);
            if (!ledger)
                return diagnostic;
            core::CpuAllocationFootprint footprint(quota);
            footprint.add(replacement.get());
            if (const auto* id = footprint.diagnostic())
                return id;
            rhi::semantics::FrameGraphRetirementCounters fixed;
            if (!core::MemoryBudgetLedger::canAdd(
                    footprint.bytes(), ledger->retainedStorageBytes()))
                return "RG-POOL-PARTITION-CPU-OVERFLOW";
            fixed.cpuMetadataBytes = footprint.bytes() + ledger->retainedStorageBytes();
            replacement->partitions.emplace(std::move(*ledger));
            replacement->fixedStorage = replacement->partitions->reserve(fixed, 1u, diagnostic);
            if (!replacement->fixedStorage)
                return diagnostic;
        }
        allocationAccounting_ = std::move(replacement);
        return nullptr;
    } catch (const std::bad_alloc&) {
        return "RG-POOL-ALLOCATION-ALLOCATION";
    }
}

core::AtomicCapacityLease FrameGraphResourcePool::retainGraphOwner(
    const std::shared_ptr<FrameGraphResourcePool>& owner, const rhi::GpuDevice& device)
{
    if (allocationAccounting_ && !ownsGraphResources())
        throw core::DiagnosticError("RG-POOL-ALLOCATION-GRAPH-UNAVAILABLE");
    if (ownsGraphResources()) {
        if (const auto* id = validateGraphAllocationDevice(device))
            throw core::DiagnosticError(id);
        if (graphOwners_.used() >= allocationAccounting_->budget.graphOwners.maxGraphs)
            throw core::DiagnosticError("RG-OWNED-RESOURCES-GRAPH-CAPACITY");
    }
    if (!graphOwners_.canAcquire(1u))
        throw core::DiagnosticError("RG-POOL-GRAPH-OWNER-CAPACITY");
    return core::AtomicCapacityLease(
        std::shared_ptr<core::AtomicCapacityBudget>(owner, &graphOwners_), 1u);
}

bool FrameGraphResourcePool::ownsGraphResources() const noexcept
{
    return allocationAccounting_ && allocationAccounting_->budget.graphOwners.requested();
}

const char* FrameGraphResourcePool::validateGraphAllocationDevice(
    const rhi::GpuDevice& device) const noexcept
{
    if (!ownsGraphResources() || !allocationAccounting_->partitions)
        return "RG-OWNED-RESOURCES-POOL";
    if (const auto* id = allocationAccounting_->partitions->threadDiagnostic())
        return id;
    return device.validateOriginalDeviceLifetime(allocationAccounting_->device);
}

FrameGraphResourcePool::AllocationAccountingReport
FrameGraphResourcePool::allocationAccountingReport() const noexcept
{
    if (!allocationAccounting_)
        return {};
    const auto& state = *allocationAccounting_;
    AllocationAccountingReport result{1u,
        rhi::semantics::FrameGraphPoolAllocationContract::lowering(state.backend),
        state.textures.used(),
        state.buffers.used(),
        state.textureBytes.used(),
        state.bufferBytes.used(),
        state.quota->liveBytes(),
        state.heapLeases.used(),
        state.heapBytes.used(),
        rhi::semantics::FrameGraphPoolAllocationContract::heapLowering(
            state.backend, state.budget.heapLeases)};
    if (state.partitions) {
        result.partitionContractVersion = 1u;
        const auto snapshot = state.partitions->snapshot();
        result.partitionDiagnostic = snapshot.diagnostic;
        // No partial dimensional observation from a foreign thread.
        if (snapshot.diagnostic) {
            result.textures = result.buffers = result.logicalTextureBytes =
                result.logicalBufferBytes = 0u;
            result.allocatedCpuMetadataBytes = result.heapLeases =
                result.logicalHeapReservationBytes = 0u;
            return result;
        }
        result.partitions = {
            snapshot.usage[0], snapshot.usage[1], snapshot.usage[2], snapshot.usage[3]};
        result.textures = snapshot.total.textureCount;
        result.buffers = snapshot.total.bufferCount;
        result.logicalTextureBytes = snapshot.total.logicalTextureBytes;
        result.logicalBufferBytes = snapshot.total.logicalBufferBytes;
        result.heapLeases = snapshot.total.aliasHeapCount;
        result.allocatedCpuMetadataBytes = snapshot.total.cpuMetadataBytes;
    }
    return result;
}

FrameGraphResourcePool::AllocationPartitions::Lease FrameGraphResourcePool::preflightAllocation(
    rhi::OriginalRegisteredResourceKind kind, std::uint64_t bytes) const
{
    if (!allocationAccounting_)
        return {};
    auto& state = *allocationAccounting_;
    if (state.partitions)
        if (const auto* id = state.partitions->threadDiagnostic())
            throw core::DiagnosticError(id);
    if (const auto* id = state.device.diagnostic())
        throw core::DiagnosticError(id);
    if (state.quota->limitBytes() != state.budget.maxCpuMetadataBytes)
        throw core::DiagnosticError("RG-POOL-ALLOCATION-QUOTA");
    const auto [count, logical] = state.budgets(kind);
    if (!count || !logical)
        throw core::DiagnosticError("RG-POOL-ALLOCATION-KIND");
    if (!count->canAcquire(1u) || !logical->canAcquire(bytes))
        throw core::DiagnosticError("RG-POOL-ALLOCATION-RESOURCE-CAPACITY");
    if (!state.partitions)
        return {};
    rhi::semantics::FrameGraphRetirementCounters charge;
    switch (kind) {
    case rhi::OriginalRegisteredResourceKind::Texture:
        charge.textureCount = 1u;
        charge.logicalTextureBytes = bytes;
        break;
    case rhi::OriginalRegisteredResourceKind::Buffer:
        charge.bufferCount = 1u;
        charge.logicalBufferBytes = bytes;
        break;
    case rhi::OriginalRegisteredResourceKind::MemoryHeap:
        charge.aliasHeapCount = 1u;
        break;
    default:
        throw core::DiagnosticError("RG-POOL-ALLOCATION-KIND");
    }
    const char* diagnostic = nullptr;
    auto lease = state.partitions->reserve(charge, 1u, diagnostic);
    if (!lease)
        throw core::DiagnosticError(diagnostic);
    return lease;
}

template <class T>
void FrameGraphResourcePool::bindAllocationReceipt(T& resource, AllocationPartitions::Lease& lease)
{
    if (!allocationAccounting_)
        return;
    if (const auto* id = device_.validateOriginalResourceRegistration(resource))
        throw core::DiagnosticError(id);
    const auto bytes = [&] {
        if constexpr (std::is_same_v<T, rhi::GpuMemoryHeap>)
            return resource.sizeBytes();
        else
            return rhi::ResourceCacheAccounting::requireLogicalBytes(
                device_.backend(), resource.desc());
    }();
    core::CpuMemoryQuotaScope scope(allocationAccounting_->quota);
    const auto makeReceipt = [&](auto&& factory) {
        return factory(rhi::OriginalPoolAllocationReceipt::ConstructionKey{},
            allocationAccounting_,
            resource.originalResourceRegistration(),
            rhi::OriginalRegisteredResourceTraits<T>::kind,
            bytes);
    };
    auto& partitions = allocationAccounting_->partitions;
    auto receipt = makeReceipt([&](auto&&... args) {
        return partitions
            ? partitions->makeSharedTracked<AllocationReceipt>(
                  lease, std::forward<decltype(args)>(args)...)
            : core::makeShared<AllocationReceipt>(std::forward<decltype(args)>(args)...);
    });
    if (partitions) {
        core::CpuAllocationFootprint footprint(allocationAccounting_->quota);
        footprint.add(receipt.get());
        if (const auto* id = footprint.diagnostic())
            throw core::DiagnosticError(id);
        rhi::semantics::FrameGraphRetirementCounters addition;
        addition.cpuMetadataBytes = footprint.bytes();
        if (const auto* id = partitions->amend(lease, addition))
            throw core::DiagnosticError(id);
        const char* diagnostic = nullptr;
        receipt->partitionIdentity = partitions->identity(lease, diagnostic);
        if (diagnostic)
            throw core::DiagnosticError(diagnostic);
    }
    if (const auto* id = device_.bindOriginalPoolAllocationReceipt(resource, receipt))
        throw core::DiagnosticError(id);
    if (partitions) {
        if (const auto* id = partitions->transfer(lease, 1u, 0u))
            throw core::DiagnosticError(id);
        lease.detach();
    }
}

template <class T>
const char* FrameGraphResourcePool::validateAllocationReceipt(const T& resource) const noexcept
{
    if (!allocationAccounting_)
        return "RG-POOL-ALLOCATION-SOURCE";
    if (const auto* id = allocationAccounting_->device.diagnostic())
        return id;
    const char* diagnostic = nullptr;
    const auto* receipt = device_.originalPoolAllocationReceipt(resource, diagnostic);
    if (diagnostic)
        return diagnostic;
    return receipt && receipt->pool_ == allocationAccounting_.get()
        ? nullptr
        : "RG-POOL-ALLOCATION-FOREIGN-POOL";
}

template <class T>
const char* FrameGraphResourcePool::validateGraphAllocationReceipt(const T& resource) const noexcept
{
    if (const auto* id = validateAllocationReceipt(resource))
        return id;
    const char* diagnostic = nullptr;
    const auto* base = device_.originalPoolAllocationReceipt(resource, diagnostic);
    if (diagnostic)
        return diagnostic;
    // The exact original pool identity was checked above; only this pool's
    // private factory can publish receipts carrying that state pointer.
    const auto* receipt = static_cast<const AllocationReceipt*>(base);
    return allocationAccounting_->partitions->validatePartition(receipt->partitionIdentity, 0u);
}

const char* FrameGraphResourcePool::validateAllocationAccounting(
    const rhi::GpuBuffer& resource) const noexcept
{
    return validateAllocationReceipt(resource);
}

const char* FrameGraphResourcePool::validateAllocationAccounting(
    const rhi::GpuTexture& resource) const noexcept
{
    return validateAllocationReceipt(resource);
}

void FrameGraphResourcePool::rejectUnjoinedAllocationReturn(bool charged) const
{
    if (allocationAccounting_ || charged)
        throw core::DiagnosticError("RG-POOL-ALLOCATION-RETURN-UNAVAILABLE");
}

const char* FrameGraphResourcePool::validateAllocationAccounting(
    const rhi::GpuMemoryHeap& heap) const noexcept
{
    return validateAllocationReceipt(heap);
}

core::UniquePtr<rhi::GpuMemoryHeap> FrameGraphResourcePool::acquireMemoryHeap(
    std::uint64_t sizeBytes, std::uint64_t alignmentBytes, rhi::MemoryUsage usage)
{
    if (!allocationAccounting_ || !allocationAccounting_->budget.heapLeases.requested())
        throw core::DiagnosticError("RG-POOL-HEAP-OPT-IN");
    auto lease = preflightAllocation(rhi::OriginalRegisteredResourceKind::MemoryHeap, sizeBytes);
    auto heap = device_.createMemoryHeap(sizeBytes, alignmentBytes, usage);
    if (!heap)
        throw core::DiagnosticError("RG-POOL-HEAP-FACTORY-FAILED");
    bindAllocationReceipt(*heap, lease);
    return heap;
}

struct FrameGraphResourcePool::RetirementFrameBundle {
    explicit RetirementFrameBundle(
        const rhi::semantics::FrameGraphRetirementFrameStorageBudget& budget)
        : heaps(budget.maxAliasHeapsPerFrame), textures(budget.maxTexturesPerFrame),
          buffers(budget.maxBuffersPerFrame), references(budget.maxReferencesPerFrame)
    {}

    RetirementFrameBundle(RetirementFrameBundle&&) noexcept = default;
    RetirementFrameBundle(const RetirementFrameBundle&) = delete;

    // Explicit CPU Prototype owners; execution awaits the private native join.
    // Release the aggregate lease after all dependent owners, including when
    // the containing storage is destroyed without an active reservation.
    AllocationPartitions::Lease ownership;
    core::BoundedSequence<core::UniquePtr<rhi::GpuMemoryHeap>> heaps;
    core::BoundedSequence<core::UniquePtr<rhi::GpuTexture>> textures;
    core::BoundedSequence<core::UniquePtr<rhi::GpuBuffer>> buffers;
    core::BoundedRetainer<void> references;
    bool reserved = false;
    bool importsPinned = false;
};

struct FrameGraphResourcePool::RetirementFrameStorage {
    RetirementFrameStorage(rhi::semantics::FrameGraphRetirementFrameStorageBudget budget,
        std::shared_ptr<core::CpuMemoryQuota> quota)
        : budget(budget), quota(std::move(quota)), frames(budget.maxFrames)
    {
        for (std::uint32_t i = 0u; i < budget.maxFrames; ++i)
            frames.append(RetirementFrameBundle(budget));
    }

    const rhi::semantics::FrameGraphRetirementFrameStorageBudget budget;
    const std::shared_ptr<core::CpuMemoryQuota> quota;
    core::BoundedSequence<RetirementFrameBundle> frames;
    std::uint64_t cpuCharge = 0u;
};

struct FrameGraphResourcePool::RetirementMetadataState {
    RetirementMetadataState(RetirementMetadataSlots slots,
        rhi::OriginalDeviceLifetime::Ticket device,
        std::shared_ptr<core::CpuMemoryQuota> quota) noexcept
        : slots(std::move(slots)), device(std::move(device)), quota(std::move(quota))
    {}

    RetirementMetadataSlots slots;
    const rhi::OriginalDeviceLifetime::Ticket device;
    const std::shared_ptr<core::CpuMemoryQuota> quota;
    std::shared_ptr<RetirementFrameStorage> storage;
};

FrameGraphResourcePool::RetirementMetadataReservation::~RetirementMetadataReservation()
{
    reset();
}

FrameGraphResourcePool::RetirementMetadataReservation::RetirementMetadataReservation(
    RetirementMetadataReservation&& other) noexcept
    : source_(std::move(other.source_)), slot_(std::move(other.slot_)),
      pinnedStorage_(std::move(other.pinnedStorage_)),
      pinnedBundle_(std::exchange(other.pinnedBundle_, nullptr))
{}

FrameGraphResourcePool::RetirementMetadataReservation&
FrameGraphResourcePool::RetirementMetadataReservation::operator=(
    RetirementMetadataReservation&& other) noexcept
{
    if (this != &other) {
        reset();
        source_ = std::move(other.source_);
        slot_ = std::move(other.slot_);
        pinnedStorage_ = std::move(other.pinnedStorage_);
        pinnedBundle_ = std::exchange(other.pinnedBundle_, nullptr);
    }
    return *this;
}

void FrameGraphResourcePool::RetirementMetadataReservation::reset() noexcept
{
    if (pinnedBundle_) {
        pinnedBundle_->references.clear();
        pinnedBundle_->textures.clear();
        pinnedBundle_->buffers.clear();
        pinnedBundle_->heaps.clear();
        pinnedBundle_->ownership.reset();
        pinnedBundle_->importsPinned = false;
        pinnedBundle_->reserved = false;
        pinnedBundle_ = nullptr;
    } else if (slot_.stage() == RetirementMetadataSlots::Stage::Reserved) {
        const auto* binding = slot_.metadata();
        if (binding && *binding)
            (*binding)->reserved = false;
    }
    slot_.reset();
    source_.reset();
    pinnedStorage_.reset();
}

const char* FrameGraphResourcePool::RetirementMetadataReservation::diagnostic() const noexcept
{
    if (!source_)
        return "RG-RETIREMENT-METADATA-EMPTY";
    if (source_->slots.closed())
        return "RG-RETIREMENT-METADATA-CLOSED";
    if (const auto* id = source_->device.diagnostic())
        return id;
    return slot_.stage() == RetirementMetadataSlots::Stage::Reserved
        ? nullptr
        : "RG-RETIREMENT-METADATA-STALE";
}

const char*
FrameGraphResourcePool::RetirementMetadataReservation::frameStorageDiagnostic() const noexcept
{
    if (const auto* id = diagnostic())
        return id;
    const auto* binding = slot_.metadata();
    return binding && *binding && (*binding)->reserved ? nullptr : "RG-FRAME-STORAGE-SOURCE";
}

const char* FrameGraphResourcePool::prepareRetirementMetadata(
    const rhi::semantics::FrameGraphRetirementMetadataBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota)
{
    if (const auto* id =
            rhi::semantics::FrameGraphRetirementMetadataContract::validateIntent(budget))
        return id;
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes)
        return "RG-RETIREMENT-METADATA-QUOTA";
    if (retirementMetadata_ && retirementMetadata_->slots.liveCount())
        return "RG-RETIREMENT-METADATA-BUSY";
    const auto device = device_.originalDeviceLifetimeTicket();
    if (const auto* id = device_.validateOriginalDeviceLifetime(device))
        return id;
    if (device_.overlapsOriginalDeviceCpuOwner(quota)
        || (allocationAccounting_
            && core::SharedOwnerProjection::overlapsOwner(allocationAccounting_->quota, quota)))
        return "RG-RETIREMENT-METADATA-QUOTA-DOMAIN";
    try {
        core::CpuMemoryQuotaScope scope(quota);
        const char* diagnostic = nullptr;
        auto slots =
            RetirementMetadataSlots::prepareCold(budget.maxReservations, quota, diagnostic);
        if (!slots)
            return diagnostic && std::string_view(diagnostic) == "CORE-RESERVATION-ALLOCATION"
                ? "RG-RETIREMENT-METADATA-ALLOCATION"
                : diagnostic;
        auto replacement =
            core::makeShared<RetirementMetadataState>(std::move(*slots), device, std::move(quota));
        closeRetirementMetadata();
        retirementMetadata_ = std::move(replacement);
        return nullptr;
    } catch (const std::bad_alloc&) {
        return "RG-RETIREMENT-METADATA-ALLOCATION";
    }
}

FrameGraphResourcePool::RetirementMetadataReservation
FrameGraphResourcePool::reserveRetirementMetadata(const char*& diagnostic) noexcept
{
    diagnostic = nullptr;
    if (!retirementMetadata_) {
        diagnostic = "RG-RETIREMENT-METADATA-SOURCE";
        return {};
    }
    if (retirementMetadata_->slots.closed()) {
        diagnostic = "RG-RETIREMENT-METADATA-CLOSED";
        return {};
    }
    if ((diagnostic = retirementMetadata_->device.diagnostic()))
        return {};
    RetirementFrameBundle* bundle = nullptr;
    if (retirementMetadata_->storage) {
        retirementMetadata_->storage->frames.visitMutable([&](RetirementFrameBundle& candidate) {
            if (!bundle && !candidate.reserved)
                bundle = &candidate;
        });
        if (!bundle) {
            diagnostic = "RG-FRAME-STORAGE-FRAME-CAPACITY";
            return {};
        }
    }
    RetirementMetadataReservation result;
    result.slot_ = retirementMetadata_->slots.reserve(bundle, diagnostic);
    if (!diagnostic) {
        result.source_ = retirementMetadata_;
        if (bundle)
            bundle->reserved = true;
    }
    return result;
}

const char* FrameGraphResourcePool::validateRetirementMetadata(
    const RetirementMetadataReservation& token) const noexcept
{
    if (!retirementMetadata_)
        return "RG-RETIREMENT-METADATA-SOURCE";
    if (!token.source_)
        return "RG-RETIREMENT-METADATA-EMPTY";
    if (retirementMetadata_ != token.source_)
        return "RG-RETIREMENT-METADATA-FOREIGN";
    return token.diagnostic();
}

const char* FrameGraphResourcePool::cancelRetirementMetadata(
    RetirementMetadataReservation& token) noexcept
{
    if (!retirementMetadata_)
        return "RG-RETIREMENT-METADATA-SOURCE";
    if (!token.source_)
        return "RG-RETIREMENT-METADATA-EMPTY";
    if (retirementMetadata_ != token.source_)
        return "RG-RETIREMENT-METADATA-FOREIGN";
    // reset owns original-bundle cleanup even after cancel clears slot metadata.
    const auto* binding = token.slot_.metadata();
    auto* bundle = binding ? *binding : nullptr;
    if (const auto* id = retirementMetadata_->slots.cancel(token.slot_))
        return id;
    if (bundle && !token.pinnedBundle_)
        bundle->reserved = false;
    token.reset();
    return nullptr;
}

void FrameGraphResourcePool::closeRetirementMetadata() noexcept
{
    if (retirementMetadata_) {
        retirementMetadata_->slots.close();
        if (retirementMetadata_->storage)
            retirementMetadata_->storage->frames.visitMutable([](RetirementFrameBundle& frame) {
                if (!frame.importsPinned)
                    frame.reserved = false;
            });
    }
}

const char* FrameGraphResourcePool::prepareRetirementFrameStorage(
    const rhi::semantics::FrameGraphRetirementFrameStorageBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota)
{
    if (const auto* id =
            rhi::semantics::FrameGraphRetirementFrameStorageContract::validateIntent(budget))
        return id;
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes)
        return "RG-FRAME-STORAGE-QUOTA";
    if (const auto* id = validateRetirementMetadataDevice(device_))
        return id;
    if (retirementMetadata_->slots.liveCount())
        return "RG-FRAME-STORAGE-BUSY";
    if (device_.overlapsOriginalDeviceCpuOwner(quota)
        || (allocationAccounting_
            && core::SharedOwnerProjection::overlapsOwner(allocationAccounting_->quota, quota))
        || core::SharedOwnerProjection::overlapsOwner(retirementMetadata_->quota, quota))
        return "RG-FRAME-STORAGE-QUOTA-DOMAIN";
    try {
        const auto before = quota->liveBytes();
        core::CpuMemoryQuotaScope scope(quota);
        auto replacement = core::makeShared<RetirementFrameStorage>(budget, quota);
        replacement->cpuCharge = quota->liveBytes() - before;
        retirementMetadata_->storage = std::move(replacement);
        return nullptr;
    } catch (const std::bad_alloc&) {
        return "RG-FRAME-STORAGE-ALLOCATION";
    } catch (const std::length_error&) {
        return "RG-FRAME-STORAGE-CAPACITY";
    }
}

FrameGraphResourcePool::RetirementFrameStorageReport
FrameGraphResourcePool::retirementFrameStorageReport() const noexcept
{
    RetirementFrameStorageReport result;
    if (retirementMetadata_ && retirementMetadata_->storage) {
        const auto& storage = *retirementMetadata_->storage;
        result.capacity = storage.budget;
        result.allocatedCpuMetadataBytes = storage.cpuCharge;
        for (const auto& frame : storage.frames.values()) {
            if (frame.reserved)
                ++result.reservedFrames;
            result.retainedReferences += frame.references.values().size();
            result.retainedTextures += frame.textures.values().size();
            result.retainedBuffers += frame.buffers.values().size();
        }
    }
    return result;
}

core::BoundedRetainer<void>* FrameGraphResourcePool::pinRetirementFrameReferences(
    RetirementMetadataReservation& token, const char*& diagnostic) noexcept
{
    diagnostic = validateRetirementMetadata(token);
    if (diagnostic)
        return nullptr;
    diagnostic = token.frameStorageDiagnostic();
    if (diagnostic)
        return nullptr;
    const auto* binding = token.slot_.metadata();
    token.pinnedStorage_ = retirementMetadata_->storage;
    token.pinnedBundle_ = *binding;
    token.pinnedBundle_->importsPinned = true;
    return &token.pinnedBundle_->references;
}

const char* FrameGraphResourcePool::adoptRetirementFrameOwners(RetirementMetadataReservation& token,
    FrameGraph& graph,
    const std::shared_ptr<const void>& source)
{
    if (const auto* id = validateGraphAllocationDevice(graph.device_))
        return id;
    if (const auto* id = validateRetirementMetadata(token))
        return id;
    if (const auto* id = token.frameStorageDiagnostic())
        return id;
    const bool mixed = graph.mixedResourceOwnersEnabled();
    const bool materialize = graph.transientResourceOwnersEnabled();
    if (const auto* id = graph.sharedImportSourceDiagnostic())
        return id;
    std::size_t textures = 0u, buffers = 0u, references = 0u;
    for (const auto& record : graph.textures_) {
        if (mixed && record.textureRef) {
            if (record.texture || record.bundleTexture || !record.imported || record.placed)
                return "RG-OWNED-RESOURCES-OWNER-UNAVAILABLE";
            ++references;
            continue;
        }
        if (record.textureRef || record.bundleTexture || record.imported || record.placed)
            return "RG-OWNED-RESOURCES-OWNER-UNAVAILABLE";
        if (record.texture) {
            if (const auto* id = validateGraphAllocationReceipt(*record.texture))
                return id;
        } else if (!materialize || !record.transient)
            return "RG-OWNED-RESOURCES-OWNER-UNAVAILABLE";
        ++textures;
    }
    for (const auto& record : graph.buffers_) {
        if (mixed && record.bufferRef) {
            if (record.buffer || record.bundleBuffer)
                return "RG-OWNED-RESOURCES-OWNER-UNAVAILABLE";
            ++references;
            continue;
        }
        if (!record.buffer || record.bufferRef || record.bundleBuffer)
            return "RG-OWNED-RESOURCES-OWNER-UNAVAILABLE";
        if (const auto* id = validateGraphAllocationReceipt(*record.buffer))
            return id;
        ++buffers;
    }
    if (const auto* id = validateRetirementFrameStorage(token, textures, buffers, 0u, references))
        return id;
    const auto* binding = token.slot_.metadata();
    auto& bundle = **binding;
    if (bundle.importsPinned || !bundle.textures.values().empty()
        || !bundle.buffers.values().empty() || !bundle.references.values().empty()
        || bundle.ownership)
        return "RG-OWNED-RESOURCES-ALREADY-PUBLISHED";
    AllocationPartitions::Lease ownership;
    if (mixed) {
        // Actual original inventory, not caller-provided numbers or a second
        // owner ledger. This aggregate owns the bundle's reference entries;
        // unique allocations keep their own original receipt charges.
        rhi::semantics::FrameGraphRetirementCounters charge;
        charge.frameBundleCount = 1u;
        charge.referenceCount = references;
        const char* diagnostic = nullptr;
        ownership = allocationAccounting_->partitions->reserve(charge, 1u, diagnostic);
        if (!ownership)
            return diagnostic;
    }
    // The original reservation becomes sole provisional owner before any
    // factory call. Failure destroys staged objects without exposing them in
    // graph records or moving any preexisting owner/import reference.
    token.pinnedStorage_ = retirementMetadata_->storage;
    token.pinnedBundle_ = &bundle;
    if (materialize) {
        for (const auto& record : graph.textures_) {
            if (record.texture || record.textureRef)
                continue;
            auto desc = record.desc;
            desc.initialState = rhi::ResourceState::Undefined;
            auto texture = acquireTexture(desc);
            if (const auto* id = validateGraphAllocationReceipt(*texture))
                return id;
            bundle.textures.append(std::move(texture));
        }
    }
    if (ownership)
        if (const auto* id = allocationAccounting_->partitions->transfer(ownership, 1u, 0u))
            return id;
    // All fallible work is complete. Bounded containers were allocated cold;
    // moves and aliasing shared_ptr construction do not allocate or throw.
    bundle.importsPinned = true;
    bundle.ownership = std::move(ownership);
    std::size_t materializedIndex = 0u;
    for (auto& record : graph.textures_) {
        if (record.textureRef) {
            bundle.references.retain(record.textureRef);
            continue;
        }
        if (record.texture) {
            record.bundleTexture = std::shared_ptr<rhi::GpuTexture>(source, record.texture.get());
            bundle.textures.append(std::move(record.texture));
        } else {
            record.bundleTexture = std::shared_ptr<rhi::GpuTexture>(
                source, bundle.textures.values()[materializedIndex++].get());
        }
    }
    for (auto& record : graph.buffers_) {
        if (record.bufferRef) {
            bundle.references.retain(record.bufferRef);
            continue;
        }
        record.bundleBuffer = std::shared_ptr<rhi::GpuBuffer>(source, record.buffer.get());
        bundle.buffers.append(std::move(record.buffer));
    }
    return nullptr;
}

const char* FrameGraphResourcePool::validateRetirementFrameStorage(
    const RetirementMetadataReservation& token,
    std::size_t textures,
    std::size_t buffers,
    std::size_t heaps,
    std::size_t references) const noexcept
{
    if (const auto* id = validateRetirementMetadata(token))
        return id;
    if (!retirementMetadata_->storage)
        return nullptr; // Existing explicit CPU source-only prototype.
    if (const auto* id = token.frameStorageDiagnostic())
        return id;
    return rhi::semantics::FrameGraphRetirementFrameStorageContract::validateCapacity(
        retirementMetadata_->storage->budget, textures, buffers, heaps, references);
}

const char* FrameGraphResourcePool::validateRetirementMetadataDevice(
    const rhi::GpuDevice& device) const noexcept
{
    if (!retirementMetadata_)
        return "RG-RETIREMENT-METADATA-SOURCE";
    if (retirementMetadata_->slots.closed())
        return "RG-RETIREMENT-METADATA-CLOSED";
    if (const auto* id = retirementMetadata_->device.diagnostic())
        return id;
    return device.validateOriginalDeviceLifetime(retirementMetadata_->device);
}

bool FrameGraphResourcePool::overlapsRetirementMetadataQuota(
    const std::shared_ptr<core::CpuMemoryQuota>& quota, bool includeAllocation) const noexcept
{
    if (includeAllocation && allocationAccounting_
        && core::SharedOwnerProjection::overlapsOwner(allocationAccounting_->quota, quota))
        return true;
    return retirementMetadata_
        && (core::SharedOwnerProjection::overlapsOwner(retirementMetadata_->quota, quota)
            || (retirementMetadata_->storage
                && core::SharedOwnerProjection::overlapsOwner(
                    retirementMetadata_->storage->quota, quota)));
}

FrameGraphResourcePool::TextureCacheKey FrameGraphResourcePool::makeKey(
    const rhi::TextureDesc& desc) noexcept
{
    TextureCacheKey key;
    key.extent = desc.extent;
    key.format = desc.format;
    key.usage = desc.usage;
    key.memory = desc.memory;
    key.initialState = desc.initialState;
    key.mipLevelCount = desc.mipLevelCount;
    key.arrayLayerCount = desc.arrayLayerCount;
    key.sampleCount = desc.sampleCount;
    key.cubeCompatible = desc.cubeCompatible;
    return key;
}

FrameGraphResourcePool::FrameGraphResourcePool(
    rhi::GpuDevice& device, std::uint32_t logicalFrameSlots)
    : device_(device), cache_(&core::defaultCpuMemoryResource()),
      bufferCache_(&core::defaultCpuMemoryResource()),
      pendingRecycle_(&core::defaultCpuMemoryResource()),
      pendingBufferRecycle_(&core::defaultCpuMemoryResource()),
      pendingAliasedGroups_(&core::defaultCpuMemoryResource()),
      cachedAliasedGroups_(&core::defaultCpuMemoryResource()), logicalFrameSlots_(logicalFrameSlots)
{
    if (logicalFrameSlots_ == 0) {
        throw std::invalid_argument("RG-POOL-LOGICAL-FRAME-SLOTS-INVALID");
    }
}

FrameGraphResourcePool::~FrameGraphResourcePool()
{
    closeRetirementMetadata();
    if (std::getenv("ARP_FRAME_GRAPH_RETIREMENT_DIAG") != nullptr) {
        const auto value = report();
        std::cerr << "[rg-retirement] pool-destroyed=" << this << " pending=" << value.pendingBytes
                  << " cached=" << value.cachedBytes << " resources=" << value.pendingResourceCount
                  << '\n';
    }
}

arp::core::UniquePtr<rhi::GpuTexture> FrameGraphResourcePool::acquireTexture(
    const rhi::TextureDesc& desc)
{
    auto lease = allocationAccounting_
        ? preflightAllocation(rhi::OriginalRegisteredResourceKind::Texture,
              rhi::ResourceCacheAccounting::requireLogicalBytes(
                  allocationAccounting_->backend, desc))
        : AllocationPartitions::Lease{};
    recycleCompleted();
    const auto key = makeKey(desc);
    auto it = cache_.find(key);
    if (it != cache_.end() && !it->second.empty()) {
        const auto& entry = it->second.back();
        if (entry.orderedIssuer) {
            const auto* id = entry.orderedIssuer->diagnostic(device_);
            if (!id)
                id = entry.orderedIssuer->payload_->reuseDiagnostic(*entry.texture);
            if (id)
                throw core::DiagnosticError(id);
        }
        auto texture = std::move(it->second.back().texture);
        cachedBytes_ -= it->second.back().bytes;
        it->second.pop_back();
        if (it->second.empty()) {
            cache_.erase(it);
        }
        --poolSize_;
        ++hitCount_;
        return texture;
    }

    auto texture = device_.createTexture(desc);
    if (!texture) {
        if (allocationAccounting_)
            throw core::DiagnosticError("RG-POOL-ALLOCATION-FACTORY-FAILED");
        const auto memory = device_.memoryStats();
        auto diagnostic =
            std::pmr::string("frame graph resource pool acquire failed for texture: ");
        diagnostic += desc.label;
        diagnostic += " extent=";
        diagnostic += std::pmr::string(std::to_string(desc.extent.width));
        diagnostic += "x";
        diagnostic += std::pmr::string(std::to_string(desc.extent.height));
        diagnostic += "x";
        diagnostic += std::pmr::string(std::to_string(desc.extent.depth));
        diagnostic += " format=";
        diagnostic += std::pmr::string(std::to_string(static_cast<std::uint32_t>(desc.format)));
        diagnostic += " usage=";
        diagnostic += std::pmr::string(std::to_string(static_cast<std::uint32_t>(desc.usage)));
        diagnostic += " memory=";
        diagnostic += std::pmr::string(std::to_string(static_cast<std::uint32_t>(desc.memory)));
        diagnostic += " requestBytes=";
        diagnostic += std::pmr::string(std::to_string(
            rhi::ResourceCacheAccounting::requireLogicalBytes(device_.backend(), desc)));
        diagnostic += " allocatedBytes=";
        diagnostic += std::pmr::string(std::to_string(memory.allocatedBytes));
        diagnostic += " liveResources=";
        diagnostic += std::pmr::string(std::to_string(memory.liveResourceCount));
        diagnostic += " cachedTextures=";
        diagnostic += std::pmr::string(std::to_string(poolSize_));
        diagnostic += " pendingRecycle=";
        diagnostic += std::pmr::string(std::to_string(pendingRecycleCount_));
        throw std::runtime_error(arp::core::toStdString(diagnostic));
    }
    bindAllocationReceipt(*texture, lease);
    ++missCount_;
    return texture;
}

void FrameGraphResourcePool::releaseTexture(arp::core::UniquePtr<rhi::GpuTexture> texture)
{
    cacheTexture(texture);
}

void FrameGraphResourcePool::cacheTexture(arp::core::UniquePtr<rhi::GpuTexture>& texture,
    std::shared_ptr<rhi::OriginalOrderedGraphicsExecution> ordered)
{
    if (!texture) {
        return;
    }
    rejectUnjoinedAllocationReturn(texture->originalPoolAllocationCharged());
    const auto key = makeKey(texture->desc());
    const auto bytes =
        rhi::ResourceCacheAccounting::requireLogicalBytes(device_.backend(), texture->desc());
    if (!core::MemoryBudgetLedger::canAdd(cachedBytes_, bytes) || poolSize_ == UINT32_MAX)
        throw core::DiagnosticError("RG-POOL-ACCOUNTING-OVERFLOW");
    if (nextSequence_ == UINT64_MAX)
        throw core::DiagnosticError("RG-POOL-SEQUENCE-OVERFLOW");
    auto& entries = cache_[key];
    if (entries.size() == entries.capacity())
        entries.reserve(entries.size() + 1u);
    entries.push_back(
        CachedTextureEntry{std::move(texture), bytes, nextSequence_, std::move(ordered)});
    ++nextSequence_;
    cachedBytes_ += bytes;
    ++poolSize_;
    enforceBudget();
}

FrameGraphResourcePool::BufferCacheKey FrameGraphResourcePool::makeBufferKey(
    const rhi::BufferDesc& desc) noexcept
{
    BufferCacheKey key;
    key.sizeBytes = desc.sizeBytes;
    key.usage = desc.usage;
    key.memory = desc.memory;
    key.initialState = desc.initialState;
    return key;
}

arp::core::UniquePtr<rhi::GpuBuffer> FrameGraphResourcePool::acquireBuffer(
    const rhi::BufferDesc& desc)
{
    auto lease = allocationAccounting_
        ? preflightAllocation(rhi::OriginalRegisteredResourceKind::Buffer,
              rhi::ResourceCacheAccounting::requireLogicalBytes(
                  allocationAccounting_->backend, desc))
        : AllocationPartitions::Lease{};
    recycleCompleted();
    const auto key = makeBufferKey(desc);
    auto it = bufferCache_.find(key);
    if (it != bufferCache_.end() && !it->second.empty()) {
        const auto& entry = it->second.back();
        if (entry.orderedIssuer) {
            const auto* id = entry.orderedIssuer->diagnostic(device_);
            if (!id)
                id = entry.orderedIssuer->payload_->reuseDiagnostic(*entry.buffer);
            if (id)
                throw core::DiagnosticError(id);
        }
        auto buffer = std::move(it->second.back().buffer);
        cachedBytes_ -= it->second.back().bytes;
        it->second.pop_back();
        if (it->second.empty()) {
            bufferCache_.erase(it);
        }
        --bufferPoolSize_;
        ++bufferHitCount_;
        return buffer;
    }

    auto buffer = device_.createBuffer(desc);
    if (!buffer) {
        if (allocationAccounting_)
            throw core::DiagnosticError("RG-POOL-ALLOCATION-FACTORY-FAILED");
        const auto memory = device_.memoryStats();
        auto diagnostic = std::pmr::string("frame graph resource pool acquire failed for buffer: ");
        diagnostic += desc.label;
        diagnostic += " requestBytes=";
        diagnostic += std::pmr::string(std::to_string(desc.sizeBytes));
        diagnostic += " allocatedBytes=";
        diagnostic += std::pmr::string(std::to_string(memory.allocatedBytes));
        diagnostic += " liveResources=";
        diagnostic += std::pmr::string(std::to_string(memory.liveResourceCount));
        diagnostic += " cachedBuffers=";
        diagnostic += std::pmr::string(std::to_string(bufferPoolSize_));
        diagnostic += " pendingRecycle=";
        diagnostic += std::pmr::string(std::to_string(pendingBufferRecycleCount_));
        throw std::runtime_error(arp::core::toStdString(diagnostic));
    }
    bindAllocationReceipt(*buffer, lease);
    ++bufferMissCount_;
    return buffer;
}

void FrameGraphResourcePool::releaseBuffer(arp::core::UniquePtr<rhi::GpuBuffer> buffer)
{
    cacheBuffer(buffer);
}

void FrameGraphResourcePool::cacheBuffer(arp::core::UniquePtr<rhi::GpuBuffer>& buffer,
    std::shared_ptr<rhi::OriginalOrderedGraphicsExecution> ordered)
{
    if (!buffer) {
        return;
    }
    rejectUnjoinedAllocationReturn(buffer->originalPoolAllocationCharged());
    const auto key = makeBufferKey(buffer->desc());
    const auto bytes =
        rhi::ResourceCacheAccounting::requireLogicalBytes(device_.backend(), buffer->desc());
    if (!core::MemoryBudgetLedger::canAdd(cachedBytes_, bytes) || bufferPoolSize_ == UINT32_MAX)
        throw core::DiagnosticError("RG-POOL-ACCOUNTING-OVERFLOW");
    if (nextSequence_ == UINT64_MAX)
        throw core::DiagnosticError("RG-POOL-SEQUENCE-OVERFLOW");
    auto& entries = bufferCache_[key];
    if (entries.size() == entries.capacity())
        entries.reserve(entries.size() + 1u);
    entries.push_back(
        CachedBufferEntry{std::move(buffer), bytes, nextSequence_, std::move(ordered)});
    ++nextSequence_;
    cachedBytes_ += bytes;
    ++bufferPoolSize_;
    enforceBudget();
}

void FrameGraphResourcePool::releaseDeferred(
    arp::core::UniquePtr<rhi::GpuTexture> texture, arp::core::UniquePtr<rhi::GpuFence> fence)
{
    if (texture)
        rejectUnjoinedAllocationReturn(texture->originalPoolAllocationCharged());
    releaseDeferred(std::move(texture),
        fence ? std::shared_ptr<rhi::GpuFence>(std::move(fence))
              : std::shared_ptr<rhi::GpuFence>());
}

void FrameGraphResourcePool::releaseDeferred(
    arp::core::UniquePtr<rhi::GpuTexture> texture, std::shared_ptr<rhi::GpuFence> fence)
{
    if (!texture) {
        return;
    }
    rejectUnjoinedAllocationReturn(texture->originalPoolAllocationCharged());
    if (!fence) {
        ++reuseFailureCount_;
        lastDiagnostic_ = "RG-POOL-RELEASE-MISSING-COMPLETION";
        return;
    }
    const auto bytes =
        rhi::ResourceCacheAccounting::requireLogicalBytes(device_.backend(), texture->desc());
    if (!core::MemoryBudgetLedger::canAdd(pendingBytes_, bytes)
        || pendingRecycleCount_ == UINT32_MAX)
        throw core::DiagnosticError("RG-POOL-ACCOUNTING-OVERFLOW");
    pendingRecycle_.push_back({std::move(texture), std::move(fence)});
    pendingBytes_ += bytes;
    ++pendingRecycleCount_;
}

void FrameGraphResourcePool::releaseDeferredBuffer(
    arp::core::UniquePtr<rhi::GpuBuffer> buffer, std::shared_ptr<rhi::GpuFence> fence)
{
    if (!buffer) {
        return;
    }
    rejectUnjoinedAllocationReturn(buffer->originalPoolAllocationCharged());
    if (!fence) {
        ++reuseFailureCount_;
        lastDiagnostic_ = "RG-POOL-RELEASE-MISSING-COMPLETION";
        return;
    }
    const auto bytes =
        rhi::ResourceCacheAccounting::requireLogicalBytes(device_.backend(), buffer->desc());
    if (!core::MemoryBudgetLedger::canAdd(pendingBytes_, bytes)
        || pendingBufferRecycleCount_ == UINT32_MAX)
        throw core::DiagnosticError("RG-POOL-ACCOUNTING-OVERFLOW");
    pendingBufferRecycle_.push_back({std::move(buffer), std::move(fence)});
    pendingBytes_ += bytes;
    ++pendingBufferRecycleCount_;
}

void FrameGraphResourcePool::releaseDeferredAliasedGroup(
    core::pmr::Vector<arp::core::UniquePtr<rhi::GpuTexture>> textures,
    core::pmr::Vector<arp::core::UniquePtr<rhi::GpuMemoryHeap>> heaps,
    std::shared_ptr<rhi::GpuFence> fence)
{
    if (textures.empty() && heaps.empty()) {
        return;
    }
    rejectUnjoinedAllocationReturn(std::any_of(textures.begin(),
                                       textures.end(),
                                       [](const auto& texture) {
                                           return texture
                                               && texture->originalPoolAllocationCharged();
                                       })
        || std::any_of(heaps.begin(), heaps.end(), [](const auto& heap) {
               return heap && heap->originalPoolAllocationCharged();
           }));
    const auto bytes = logicalGroupBytes(textures);
    if (textures.size() > UINT32_MAX || textures.size() > UINT32_MAX - pendingAliasedResourceCount_
        || !core::MemoryBudgetLedger::canAdd(pendingBytes_, bytes))
        throw core::DiagnosticError("RG-POOL-ACCOUNTING-OVERFLOW");
    const auto resourceCount = static_cast<std::uint32_t>(textures.size());
    if (!fence) {
        ++reuseFailureCount_;
        lastDiagnostic_ = "RG-POOL-ALIAS-RELEASE-MISSING-COMPLETION";
    }
    pendingAliasedGroups_.push_back(
        {std::move(heaps), std::move(textures), std::move(fence), bytes, resourceCount, {}, 0});
    pendingBytes_ += bytes;
    pendingAliasedResourceCount_ += resourceCount;
}

bool FrameGraphResourcePool::acquireAliasedGroup(
    const FrameGraphAliasedResourceLayout& layout, FrameGraphAliasedResourceGroup& group)
{
    rejectUnjoinedAllocationReturn();
    recycleCompleted();
    for (auto it = cachedAliasedGroups_.begin(); it != cachedAliasedGroups_.end(); ++it) {
        if (!layoutsMatch(it->layout, layout))
            continue;
        group.heaps = std::move(it->heaps);
        group.textures = std::move(it->textures);
        cachedBytes_ -= it->bytes;
        cachedAliasedResourceCount_ -= it->resourceCount;
        cachedAliasedGroups_.erase(it);
        hitCount_ += static_cast<std::uint32_t>(group.textures.size());
        return true;
    }
    missCount_ += static_cast<std::uint32_t>(layout.textureDescs.size());
    return false;
}

void FrameGraphResourcePool::releaseDeferredAliasedGroup(FrameGraphAliasedResourceGroup group,
    FrameGraphAliasedResourceLayout layout,
    std::shared_ptr<rhi::GpuFence> fence)
{
    if (group.textures.empty() && group.heaps.empty())
        return;
    rejectUnjoinedAllocationReturn(std::any_of(group.textures.begin(),
                                       group.textures.end(),
                                       [](const auto& texture) {
                                           return texture
                                               && texture->originalPoolAllocationCharged();
                                       })
        || std::any_of(group.heaps.begin(), group.heaps.end(), [](const auto& heap) {
               return heap && heap->originalPoolAllocationCharged();
           }));
    const auto bytes = logicalGroupBytes(group.textures);
    if (group.textures.size() > UINT32_MAX
        || group.textures.size() > UINT32_MAX - pendingAliasedResourceCount_
        || !core::MemoryBudgetLedger::canAdd(pendingBytes_, bytes))
        throw core::DiagnosticError("RG-POOL-ACCOUNTING-OVERFLOW");
    const auto resourceCount = static_cast<std::uint32_t>(group.textures.size());
    if (!fence) {
        ++reuseFailureCount_;
        lastDiagnostic_ = "RG-POOL-ALIAS-RELEASE-MISSING-COMPLETION";
    }
    pendingAliasedGroups_.push_back({std::move(group.heaps),
        std::move(group.textures),
        std::move(fence),
        bytes,
        resourceCount,
        std::move(layout),
        0});
    pendingBytes_ += bytes;
    pendingAliasedResourceCount_ += resourceCount;
}

void FrameGraphResourcePool::recycleCompleted()
{
    core::StableVectorErase::eraseIf(pendingRecycle_, [this](DeferredEntry& entry) {
        if (!entry.fence || !entry.fence->isSignaled())
            return false;
        const auto bytes = rhi::ResourceCacheAccounting::requireLogicalBytes(
            device_.backend(), entry.texture->desc());
        const auto prepared = device_.prepareTextureForPoolReuse(*entry.texture);
        if (prepared.success) {
            cacheTexture(entry.texture);
        } else {
            lastDiagnostic_ =
                std::pmr::string("RG-POOL-REUSE-NORMALIZATION-FAILED: ") + prepared.diagnostics;
            ++reuseFailureCount_;
            entry.texture.reset();
        }
        pendingBytes_ -= bytes;
        --pendingRecycleCount_;
        return true;
    });
    core::StableVectorErase::eraseIf(pendingBufferRecycle_, [this](DeferredBufferEntry& entry) {
        if (!entry.fence || !entry.fence->isSignaled())
            return false;
        const auto bytes = rhi::ResourceCacheAccounting::requireLogicalBytes(
            device_.backend(), entry.buffer->desc());
        const auto prepared = device_.prepareBufferForPoolReuse(*entry.buffer);
        if (prepared.success) {
            cacheBuffer(entry.buffer);
        } else {
            lastDiagnostic_ =
                std::pmr::string("RG-POOL-REUSE-NORMALIZATION-FAILED: ") + prepared.diagnostics;
            ++reuseFailureCount_;
            entry.buffer.reset();
        }
        pendingBytes_ -= bytes;
        --pendingBufferRecycleCount_;
        return true;
    });
    core::StableVectorErase::eraseIf(pendingAliasedGroups_, [this](DeferredAliasedGroup& group) {
        if (!group.fence || !group.fence->isSignaled())
            return false;
        const auto bytes = group.bytes;
        const auto resourceCount = group.resourceCount;
        bool reusable = !group.layout.textureDescs.empty();
        if (reusable) {
            for (auto& texture : group.textures) {
                const auto prepared = device_.prepareTextureForPoolReuse(*texture);
                if (!prepared.success) {
                    lastDiagnostic_ = std::pmr::string("RG-POOL-REUSE-NORMALIZATION-FAILED: ")
                        + prepared.diagnostics;
                    ++reuseFailureCount_;
                    reusable = false;
                    break;
                }
            }
        }
        if (reusable) {
            if (!core::MemoryBudgetLedger::canAdd(cachedBytes_, bytes)
                || resourceCount > UINT32_MAX - cachedAliasedResourceCount_)
                throw core::DiagnosticError("RG-POOL-ACCOUNTING-OVERFLOW");
            if (nextSequence_ == UINT64_MAX)
                throw core::DiagnosticError("RG-POOL-SEQUENCE-OVERFLOW");
            if (cachedAliasedGroups_.size() == cachedAliasedGroups_.capacity())
                cachedAliasedGroups_.reserve(cachedAliasedGroups_.size() + 1u);
            group.sequence = nextSequence_;
            cachedAliasedGroups_.push_back(std::move(group));
            ++nextSequence_;
            cachedBytes_ += bytes;
            cachedAliasedResourceCount_ += resourceCount;
        }
        // Completed uncached owners retire before compaction can assign over
        // the entry. Memberwise assignment alone would release heaps first.
        group.textures.clear();
        group.heaps.clear();
        pendingBytes_ -= bytes;
        pendingAliasedResourceCount_ -= resourceCount;
        enforceBudget();
        return true;
    });
}

void FrameGraphResourcePool::trimCachedResources()
{
    cache_.clear();
    bufferCache_.clear();
    cachedAliasedGroups_.clear();
    poolSize_ = 0;
    bufferPoolSize_ = 0;
    cachedAliasedResourceCount_ = 0;
    cachedBytes_ = 0;
}

void FrameGraphResourcePool::setLogicalFrameSlots(std::uint32_t logicalFrameSlots)
{
    configureBudget(
        compiledTransientPeakBytes_, compiledTransientResourceCount_, logicalFrameSlots);
}

void FrameGraphResourcePool::configureForCompiledGraph(
    std::uint64_t transientPeakBytes, std::uint32_t transientResourceCount)
{
    rejectUnjoinedAllocationReturn();
    configureBudget(transientPeakBytes, transientResourceCount, logicalFrameSlots_);
}

void FrameGraphResourcePool::configureBudget(std::uint64_t transientPeakBytes,
    std::uint32_t transientResourceCount,
    std::uint32_t logicalFrameSlots)
{
    if (logicalFrameSlots == 0u)
        throw std::invalid_argument("RG-POOL-LOGICAL-FRAME-SLOTS-INVALID");
    if (!core::canMultiplyUnsigned(transientPeakBytes, std::uint64_t(logicalFrameSlots))
        || !core::canMultiplyUnsigned(transientResourceCount, logicalFrameSlots)) {
        throw std::overflow_error("RG-POOL-BUDGET-OVERFLOW");
    }
    const auto byteBudget = transientPeakBytes * logicalFrameSlots;
    const auto resourceBudget = transientResourceCount * logicalFrameSlots;
    // Cached compiled topologies call this once per frame. Avoid rescanning
    // the cache with enforceBudget() when neither the topology budget nor the
    // logical frame-slot multiplier changed; recycleCompleted() still runs on
    // every frame before this method and remains responsible for retirement.
    if (logicalFrameSlots_ == logicalFrameSlots && compiledTransientPeakBytes_ == transientPeakBytes
        && compiledTransientResourceCount_ == transientResourceCount && byteBudget_ == byteBudget
        && resourceBudget_ == resourceBudget) {
        return;
    }
    logicalFrameSlots_ = logicalFrameSlots;
    compiledTransientPeakBytes_ = transientPeakBytes;
    compiledTransientResourceCount_ = transientResourceCount;
    byteBudget_ = byteBudget;
    resourceBudget_ = resourceBudget;
    enforceBudget();
}

FrameGraphResourcePoolReport FrameGraphResourcePool::report() const
{
    FrameGraphResourcePoolReport value;
    value.compiledTransientPeakBytes = compiledTransientPeakBytes_;
    value.compiledTransientResourceCount = compiledTransientResourceCount_;
    value.logicalFrameSlots = logicalFrameSlots_;
    value.byteBudget = byteBudget_;
    value.resourceBudget = resourceBudget_;
    value.cachedBytes = cachedBytes_;
    value.pendingBytes = pendingBytes_;
    value.cachedResourceCount = poolSize_ + bufferPoolSize_ + cachedAliasedResourceCount_;
    value.pendingResourceCount =
        pendingRecycleCount_ + pendingBufferRecycleCount_ + pendingAliasedResourceCount_;
    value.hitCount = hitCount_ + bufferHitCount_;
    value.missCount = missCount_ + bufferMissCount_;
    value.evictionCount = evictionCount_;
    value.reuseFailureCount = reuseFailureCount_;
    value.lastDiagnostic = orderedReturnDiagnostic_ ? orderedReturnDiagnostic_ : lastDiagnostic_;
    return value;
}

std::uint64_t FrameGraphResourcePool::logicalGroupBytes(
    const core::pmr::Vector<core::UniquePtr<rhi::GpuTexture>>& textures) const
{
    std::uint64_t bytes = 0u;
    for (const auto& texture : textures) {
        if (!texture)
            continue;
        const auto charge =
            rhi::ResourceCacheAccounting::requireLogicalBytes(device_.backend(), texture->desc());
        if (const auto* id = rhi::ResourceCacheAccounting::accumulate(bytes, charge))
            throw core::DiagnosticError(id);
    }
    return bytes;
}

void FrameGraphResourcePool::enforceBudget()
{
    while (cachedBytes_ > byteBudget_
        || poolSize_ + bufferPoolSize_ + cachedAliasedResourceCount_ > resourceBudget_) {
        std::uint64_t oldest = UINT64_MAX;
        const TextureCacheKey* textureKey = nullptr;
        std::size_t textureIndex = 0;
        const BufferCacheKey* bufferKey = nullptr;
        std::size_t bufferIndex = 0;
        for (auto& item : cache_) {
            for (std::size_t index = 0; index < item.second.size(); ++index) {
                if (item.second[index].sequence < oldest) {
                    oldest = item.second[index].sequence;
                    textureKey = &item.first;
                    textureIndex = index;
                    bufferKey = nullptr;
                }
            }
        }
        for (auto& item : bufferCache_) {
            for (std::size_t index = 0; index < item.second.size(); ++index) {
                if (item.second[index].sequence < oldest) {
                    oldest = item.second[index].sequence;
                    bufferKey = &item.first;
                    bufferIndex = index;
                    textureKey = nullptr;
                }
            }
        }
        std::size_t aliasedIndex = 0;
        for (std::size_t index = 0; index < cachedAliasedGroups_.size(); ++index) {
            if (cachedAliasedGroups_[index].sequence < oldest) {
                oldest = cachedAliasedGroups_[index].sequence;
                aliasedIndex = index;
                textureKey = nullptr;
                bufferKey = nullptr;
            }
        }
        if (oldest == UINT64_MAX) {
            break;
        }
        if (textureKey) {
            auto found = cache_.find(*textureKey);
            cachedBytes_ -= found->second[textureIndex].bytes;
            found->second.erase(found->second.begin() + textureIndex);
            --poolSize_;
            if (found->second.empty())
                cache_.erase(found);
        } else if (bufferKey) {
            auto found = bufferCache_.find(*bufferKey);
            cachedBytes_ -= found->second[bufferIndex].bytes;
            found->second.erase(found->second.begin() + bufferIndex);
            --bufferPoolSize_;
            if (found->second.empty())
                bufferCache_.erase(found);
        } else {
            cachedBytes_ -= cachedAliasedGroups_[aliasedIndex].bytes;
            cachedAliasedResourceCount_ -= cachedAliasedGroups_[aliasedIndex].resourceCount;
            std::size_t index = 0u;
            core::StableVectorErase::eraseIf(
                cachedAliasedGroups_, [&](DeferredAliasedGroup& group) noexcept {
                    if (index++ != aliasedIndex)
                        return false;
                    group.textures.clear();
                    group.heaps.clear();
                    return true;
                });
        }
        ++evictionCount_;
    }
}

} // namespace arp::render_graph
