#pragma once

#include "core/api.h"
#include "core/container/single_use_reservation_pool.h"
#include "core/memory/atomic_capacity.h"
#include "core/memory/cpu_allocator.h"
#include "core/memory/partitioned_budget_ledger.h"
#include "core/ownership/bounded_retainer.h"
#include "rhi/frontend/ordered_graphics_execution.h"
#include "rhi/frontend/resource_cache_accounting.h"
#include "rhi/gpu_device.h"
#include "rhi/gpu_resources.h"
#include "rhi/semantics/frame_graph_retirement_budget.h"

#include <cstdint>
#include <memory>

namespace arp::render_graph {

class FrameGraph;

struct FrameGraphResourcePoolReport {
    std::uint64_t compiledTransientPeakBytes = 0;
    std::uint32_t compiledTransientResourceCount = 0;
    std::uint32_t logicalFrameSlots = 1;
    std::uint64_t byteBudget = 0;
    std::uint32_t resourceBudget = 0;
    std::uint64_t cachedBytes = 0;
    std::uint64_t pendingBytes = 0;
    std::uint32_t cachedResourceCount = 0;
    std::uint32_t pendingResourceCount = 0;
    std::uint64_t hitCount = 0;
    std::uint64_t missCount = 0;
    std::uint64_t evictionCount = 0;
    std::uint64_t reuseFailureCount = 0;
    std::pmr::string lastDiagnostic;
    // These bytes are logical descriptor charges, never resident VRAM.
    std::uint32_t accountingContractVersion = rhi::ResourceCacheAccounting::contractVersion;
    rhi::ResourceCacheAccountingDomain accountingDomain =
        rhi::ResourceCacheAccountingDomain::LogicalDescriptor;
    rhi::semantics::PhysicalBudgetObservation nativePhysicalObservation;
};

struct FrameGraphAliasedResourceLayout {
    core::pmr::Vector<std::uint64_t> heapSizes;
    core::pmr::Vector<std::uint64_t> heapAlignments;
    core::pmr::Vector<rhi::TextureDesc> textureDescs;
    core::pmr::Vector<std::uint32_t> textureHeapIndices;
    core::pmr::Vector<std::uint64_t> textureOffsets;
};

struct FrameGraphAliasedResourceGroup {
    core::pmr::Vector<arp::core::UniquePtr<rhi::GpuMemoryHeap>> heaps;
    core::pmr::Vector<arp::core::UniquePtr<rhi::GpuTexture>> textures;
};

/// Frame-slot level resource pool that caches GpuTexture and GpuBuffer objects
/// across frames, keyed by desc (excluding the label field). Transient
/// textures are acquired from the pool at execute time and released back
/// to the pool on reset(), avoiding per-frame allocation/deallocation.
///
/// The pool is allocator-compliant: all internal containers use core::pmr
/// aliases. The pool does not expose native handles to the upper layer.
class ARP_API FrameGraphResourcePool final {
    struct RetirementFrameBundle;
    struct RetirementFrameStorage;
    using RetirementMetadataSlots = core::SingleUseReservationPool<RetirementFrameBundle*>;
    struct RetirementMetadataState;
    struct AllocationAccountingState;
    struct AllocationReceipt;

public:
    explicit FrameGraphResourcePool(rhi::GpuDevice& device, std::uint32_t logicalFrameSlots = 1);
    ~FrameGraphResourcePool();

    FrameGraphResourcePool(const FrameGraphResourcePool&) = delete;
    FrameGraphResourcePool& operator=(const FrameGraphResourcePool&) = delete;

    // FrameGraphPoolMetadataSource/1. Explicit CPU prototype only; this is not
    // a frame lease and has no consume/native issue/completion operation.
    class ARP_API RetirementMetadataReservation final {
    public:
        RetirementMetadataReservation() noexcept = default;
        ~RetirementMetadataReservation();
        RetirementMetadataReservation(const RetirementMetadataReservation&) = delete;
        RetirementMetadataReservation& operator=(const RetirementMetadataReservation&) = delete;
        RetirementMetadataReservation(RetirementMetadataReservation&&) noexcept;
        RetirementMetadataReservation& operator=(RetirementMetadataReservation&&) noexcept;

        const char* diagnostic() const noexcept;
        const char* frameStorageDiagnostic() const noexcept;

        bool nativeExecutionProven() const noexcept { return false; }

    private:
        friend class FrameGraphResourcePool;
        void reset() noexcept;
        std::shared_ptr<RetirementMetadataState> source_;
        RetirementMetadataSlots::Reservation slot_;
        std::shared_ptr<RetirementFrameStorage> pinnedStorage_;
        RetirementFrameBundle* pinnedBundle_ = nullptr;
    };

    const char* prepareRetirementMetadata(
        const rhi::semantics::FrameGraphRetirementMetadataBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota);
    RetirementMetadataReservation reserveRetirementMetadata(const char*& diagnostic) noexcept;
    const char* validateRetirementMetadata(
        const RetirementMetadataReservation& token) const noexcept;
    const char* cancelRetirementMetadata(RetirementMetadataReservation& token) noexcept;
    void closeRetirementMetadata() noexcept;

    struct RetirementFrameStorageReport {
        rhi::semantics::FrameGraphRetirementFrameStorageBudget capacity;
        std::uint32_t reservedFrames = 0u;
        std::uint64_t allocatedCpuMetadataBytes = 0u;
        std::uint64_t retainedReferences = 0u;
        std::uint64_t retainedTextures = 0u, retainedBuffers = 0u;
    };

    // Cold CPU owner-slot storage only; no GPU adoption or completion API.
    const char* prepareRetirementFrameStorage(
        const rhi::semantics::FrameGraphRetirementFrameStorageBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota);
    RetirementFrameStorageReport retirementFrameStorageReport() const noexcept;

    // Explicit acquire-only Prototype; no reuse/submission/completion authority.
    struct AllocationAccountingReport {
        std::uint32_t contractVersion = 0u;
        rhi::semantics::FrameGraphPoolAllocationContract::Decision lowering;
        std::uint64_t textures = 0u, buffers = 0u;
        std::uint64_t logicalTextureBytes = 0u, logicalBufferBytes = 0u;
        std::uint64_t allocatedCpuMetadataBytes = 0u;
        std::uint64_t heapLeases = 0u, logicalHeapReservationBytes = 0u;
        rhi::semantics::FrameGraphPoolAllocationContract::Decision heapLeaseLowering;
        std::uint32_t partitionContractVersion = 0u;
        const char* partitionDiagnostic = nullptr;
        rhi::semantics::FrameGraphRetirementUsage partitions;
    };

    const char* prepareAllocationAccounting(
        const rhi::semantics::FrameGraphPoolAllocationBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota);
    AllocationAccountingReport allocationAccountingReport() const noexcept;
    const char* validateAllocationAccounting(const rhi::GpuBuffer& resource) const noexcept;
    const char* validateAllocationAccounting(const rhi::GpuTexture& resource) const noexcept;
    const char* validateAllocationAccounting(const rhi::GpuMemoryHeap& heap) const noexcept;

    // Independent explicit heapLeases opt-in. Charges declared logical heap
    // reservation size until the last placement-lease owner dies, not VRAM.
    core::UniquePtr<rhi::GpuMemoryHeap> acquireMemoryHeap(
        std::uint64_t sizeBytes, std::uint64_t alignmentBytes, rhi::MemoryUsage usage);

    /// Acquire a texture matching the given desc. If a cached texture with a
    /// compatible desc (all fields except label) exists, it is moved out of
    /// the cache (pool hit). Otherwise a new texture is created via
    /// device_.createTexture (pool miss). Throws std::runtime_error if the
    /// device cannot create the texture.
    arp::core::UniquePtr<rhi::GpuTexture> acquireTexture(const rhi::TextureDesc& desc);

    /// Release a texture back to the pool for future reuse. The texture's
    /// desc (excluding label) determines the cache bucket.
    void releaseTexture(arp::core::UniquePtr<rhi::GpuTexture> texture);

    /// Acquire a buffer matching the given desc. Same semantics as
    /// acquireTexture but for GpuBuffer objects.
    arp::core::UniquePtr<rhi::GpuBuffer> acquireBuffer(const rhi::BufferDesc& desc);

    /// Release a buffer back to the pool for future reuse.
    void releaseBuffer(arp::core::UniquePtr<rhi::GpuBuffer> buffer);

    /// Number of textures currently held in the pool cache.
    std::uint32_t poolSize() const noexcept { return poolSize_; }

    /// Cumulative cache hit count (textures served from the pool without
    /// creating a new GPU resource).
    std::uint32_t hitCount() const noexcept { return hitCount_; }

    /// Cumulative cache miss count (textures that required a new GPU
    /// resource creation).
    std::uint32_t missCount() const noexcept { return missCount_; }

    /// Number of buffers currently held in the buffer pool cache.
    std::uint32_t bufferPoolSize() const noexcept { return bufferPoolSize_; }

    std::uint32_t bufferHitCount() const noexcept { return bufferHitCount_; }

    std::uint32_t bufferMissCount() const noexcept { return bufferMissCount_; }

    /// Deferred release: queue a texture for recycling when the fence
    /// signals completion. The texture is held until recycleCompleted()
    /// is called and the fence is signaled, at which point it is moved
    /// into the normal pool cache. This prevents destroying resources
    /// that are still referenced by in-flight GPU work.
    void releaseDeferred(
        arp::core::UniquePtr<rhi::GpuTexture> texture, arp::core::UniquePtr<rhi::GpuFence> fence);

    /// Shared-fence form used when one submitted frame owns multiple
    /// transient resources. Every resource keeps the same completion fence
    /// alive until the queue has finished consuming the frame.
    void releaseDeferred(
        arp::core::UniquePtr<rhi::GpuTexture> texture, std::shared_ptr<rhi::GpuFence> fence);

    /// Buffer equivalent of the fence-gated texture release.
    void releaseDeferredBuffer(
        arp::core::UniquePtr<rhi::GpuBuffer> buffer, std::shared_ptr<rhi::GpuFence> fence);

    /// Retire one physical alias allocation as an indivisible completion-gated
    /// group. Textures are destroyed before their backing heaps, and neither
    /// object may be cached or destroyed while GPU work is in flight.
    void releaseDeferredAliasedGroup(
        core::pmr::Vector<arp::core::UniquePtr<rhi::GpuTexture>> textures,
        core::pmr::Vector<arp::core::UniquePtr<rhi::GpuMemoryHeap>> heaps,
        std::shared_ptr<rhi::GpuFence> fence);

    /// Recover an exact completed alias layout. A miss leaves group empty;
    /// callers create the requested placement objects through GpuDevice.
    bool acquireAliasedGroup(
        const FrameGraphAliasedResourceLayout& layout, FrameGraphAliasedResourceGroup& group);

    /// Completion-gated retirement for a complete physical alias layout.
    void releaseDeferredAliasedGroup(FrameGraphAliasedResourceGroup group,
        FrameGraphAliasedResourceLayout layout,
        std::shared_ptr<rhi::GpuFence> fence);

    /// Recycle deferred textures whose fences have signaled. Moves
    /// completed entries from the pending queue into the normal pool
    /// cache for reuse.
    void recycleCompleted();

    /// Number of textures currently pending fence-gated recycling.
    std::uint32_t pendingRecycleCount() const noexcept { return pendingRecycleCount_; }

    /// Drop completed, cached resources when a descriptor shape changes. The
    /// pool retains only resources that are safe to destroy; in-flight
    /// resources stay in the deferred queues until their fences signal.
    void trimCachedResources();

    void configureForCompiledGraph(
        std::uint64_t transientPeakBytes, std::uint32_t transientResourceCount);
    void setLogicalFrameSlots(std::uint32_t logicalFrameSlots);
    FrameGraphResourcePoolReport report() const;

private:
    friend class FrameGraph;
    void releaseOrdered(core::UniquePtr<rhi::GpuTexture>&,
        const std::shared_ptr<rhi::OriginalOrderedGraphicsExecution>&) noexcept;
    void releaseOrdered(core::UniquePtr<rhi::GpuBuffer>&,
        const std::shared_ptr<rhi::OriginalOrderedGraphicsExecution>&) noexcept;
    bool ownsGraphResources() const noexcept;
    const char* validateGraphAllocationDevice(const rhi::GpuDevice& device) const noexcept;
    const char* adoptRetirementFrameOwners(RetirementMetadataReservation& token,
        FrameGraph& graph,
        const std::shared_ptr<const void>& source);
    core::BoundedRetainer<void>* pinRetirementFrameReferences(
        RetirementMetadataReservation& token, const char*& diagnostic) noexcept;
    core::AtomicCapacityLease retainGraphOwner(
        const std::shared_ptr<FrameGraphResourcePool>& owner, const rhi::GpuDevice& device);
    const char* validateRetirementMetadataDevice(const rhi::GpuDevice& device) const noexcept;
    bool overlapsRetirementMetadataQuota(const std::shared_ptr<core::CpuMemoryQuota>& quota,
        bool includeAllocation = true) const noexcept;
    const char* validateRetirementFrameStorage(const RetirementMetadataReservation& token,
        std::size_t textures,
        std::size_t buffers,
        std::size_t heaps,
        std::size_t references) const noexcept;

    /// Cache key: all TextureDesc fields except label. Two textures with
    /// different labels but identical key fields are interchangeable in the
    /// pool.
    struct TextureCacheKey {
        rhi::Extent3D extent;
        rhi::TextureFormat format = rhi::TextureFormat::Unknown;
        rhi::TextureUsage usage = rhi::TextureUsage::None;
        rhi::MemoryUsage memory = rhi::MemoryUsage::GpuOnly;
        rhi::ResourceState initialState = rhi::ResourceState::Undefined;
        std::uint32_t mipLevelCount = 1;
        std::uint32_t arrayLayerCount = 1;
        std::uint32_t sampleCount = 1;
        bool cubeCompatible = false;

        bool operator==(const TextureCacheKey& other) const noexcept
        {
            return extent.width == other.extent.width && extent.height == other.extent.height
                && extent.depth == other.extent.depth && format == other.format
                && usage == other.usage && memory == other.memory
                && initialState == other.initialState && mipLevelCount == other.mipLevelCount
                && arrayLayerCount == other.arrayLayerCount && sampleCount == other.sampleCount
                && cubeCompatible == other.cubeCompatible;
        }
    };

    struct TextureCacheKeyHash {
        std::size_t operator()(const TextureCacheKey& key) const noexcept
        {
            std::size_t h = 0;
            h ^= std::hash<std::uint32_t>{}(key.extent.width) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<std::uint32_t>{}(key.extent.height) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<std::uint32_t>{}(key.extent.depth) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<std::uint32_t>{}(static_cast<std::uint32_t>(key.format)) + 0x9e3779b9
                + (h << 6) + (h >> 2);
            h ^= std::hash<std::uint32_t>{}(static_cast<std::uint32_t>(key.usage)) + 0x9e3779b9
                + (h << 6) + (h >> 2);
            h ^= std::hash<std::uint32_t>{}(static_cast<std::uint32_t>(key.memory)) + 0x9e3779b9
                + (h << 6) + (h >> 2);
            h ^= std::hash<std::uint32_t>{}(static_cast<std::uint32_t>(key.initialState))
                + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<std::uint32_t>{}(key.mipLevelCount) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<std::uint32_t>{}(key.arrayLayerCount) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<std::uint32_t>{}(key.sampleCount) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<bool>{}(key.cubeCompatible) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    static TextureCacheKey makeKey(const rhi::TextureDesc& desc) noexcept;

    struct CachedTextureEntry {
        arp::core::UniquePtr<rhi::GpuTexture> texture;
        std::uint64_t bytes = 0;
        std::uint64_t sequence = 0;
        std::shared_ptr<rhi::OriginalOrderedGraphicsExecution> orderedIssuer;
    };

    rhi::GpuDevice& device_;
    core::AtomicCapacityBudget graphOwners_{std::numeric_limits<std::uint64_t>::max()};
    std::shared_ptr<RetirementMetadataState> retirementMetadata_;
    std::shared_ptr<AllocationAccountingState> allocationAccounting_;
    using AllocationPartitions =
        core::PartitionedBudgetLedger<rhi::semantics::FrameGraphRetirementCounters,
            rhi::semantics::FrameGraphRetirementCounters::dimensionCount,
            4u>;
    template <class T> void bindAllocationReceipt(T& resource, AllocationPartitions::Lease& lease);
    template <class T> const char* validateAllocationReceipt(const T& resource) const noexcept;
    template <class T> const char* validateGraphAllocationReceipt(const T& resource) const noexcept;
    AllocationPartitions::Lease preflightAllocation(
        rhi::OriginalRegisteredResourceKind kind, std::uint64_t bytes) const;
    void rejectUnjoinedAllocationReturn(bool charged = false) const;
    core::pmr::
        UnorderedMap<TextureCacheKey, core::pmr::Vector<CachedTextureEntry>, TextureCacheKeyHash>
            cache_;
    std::uint32_t poolSize_ = 0;
    std::uint32_t hitCount_ = 0;
    std::uint32_t missCount_ = 0;

    /// Cache key for buffers: all BufferDesc fields except label.
    struct BufferCacheKey {
        std::uint64_t sizeBytes = 0;
        rhi::ResourceUsage usage = rhi::ResourceUsage::None;
        rhi::MemoryUsage memory = rhi::MemoryUsage::GpuOnly;
        rhi::ResourceState initialState = rhi::ResourceState::Undefined;

        bool operator==(const BufferCacheKey& other) const noexcept
        {
            return sizeBytes == other.sizeBytes && usage == other.usage && memory == other.memory
                && initialState == other.initialState;
        }
    };

    struct BufferCacheKeyHash {
        std::size_t operator()(const BufferCacheKey& key) const noexcept
        {
            std::size_t h = 0;
            h ^= std::hash<std::uint64_t>{}(key.sizeBytes) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<std::uint32_t>{}(static_cast<std::uint32_t>(key.usage)) + 0x9e3779b9
                + (h << 6) + (h >> 2);
            h ^= std::hash<std::uint32_t>{}(static_cast<std::uint32_t>(key.memory)) + 0x9e3779b9
                + (h << 6) + (h >> 2);
            h ^= std::hash<std::uint32_t>{}(static_cast<std::uint32_t>(key.initialState))
                + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    static BufferCacheKey makeBufferKey(const rhi::BufferDesc& desc) noexcept;

    struct CachedBufferEntry {
        arp::core::UniquePtr<rhi::GpuBuffer> buffer;
        std::uint64_t bytes = 0;
        std::uint64_t sequence = 0;
        std::shared_ptr<rhi::OriginalOrderedGraphicsExecution> orderedIssuer;
    };

    core::pmr::
        UnorderedMap<BufferCacheKey, core::pmr::Vector<CachedBufferEntry>, BufferCacheKeyHash>
            bufferCache_;
    std::uint32_t bufferPoolSize_ = 0;
    std::uint32_t bufferHitCount_ = 0;
    std::uint32_t bufferMissCount_ = 0;

    struct DeferredEntry {
        arp::core::UniquePtr<rhi::GpuTexture> texture;
        std::shared_ptr<rhi::GpuFence> fence;
    };

    core::pmr::Vector<DeferredEntry> pendingRecycle_;
    std::uint32_t pendingRecycleCount_ = 0;

    struct DeferredBufferEntry {
        arp::core::UniquePtr<rhi::GpuBuffer> buffer;
        std::shared_ptr<rhi::GpuFence> fence;
    };

    core::pmr::Vector<DeferredBufferEntry> pendingBufferRecycle_;
    std::uint32_t pendingBufferRecycleCount_ = 0;

    struct DeferredAliasedGroup {
        // Member destruction is reverse declaration order: placed textures
        // must release before their backing heaps.
        core::pmr::Vector<arp::core::UniquePtr<rhi::GpuMemoryHeap>> heaps;
        core::pmr::Vector<arp::core::UniquePtr<rhi::GpuTexture>> textures;
        std::shared_ptr<rhi::GpuFence> fence;
        std::uint64_t bytes = 0;
        std::uint32_t resourceCount = 0;
        FrameGraphAliasedResourceLayout layout;
        std::uint64_t sequence = 0;
    };

    core::pmr::Vector<DeferredAliasedGroup> pendingAliasedGroups_;
    core::pmr::Vector<DeferredAliasedGroup> cachedAliasedGroups_;
    std::uint32_t cachedAliasedResourceCount_ = 0;
    std::uint32_t pendingAliasedResourceCount_ = 0;
    std::uint32_t logicalFrameSlots_ = 1;
    std::uint64_t compiledTransientPeakBytes_ = 0;
    std::uint32_t compiledTransientResourceCount_ = 0;
    std::uint64_t byteBudget_ = UINT64_MAX;
    std::uint32_t resourceBudget_ = UINT32_MAX;
    std::uint64_t cachedBytes_ = 0;
    std::uint64_t pendingBytes_ = 0;
    std::uint64_t nextSequence_ = 1;
    std::uint64_t evictionCount_ = 0;
    std::uint64_t reuseFailureCount_ = 0;
    std::pmr::string lastDiagnostic_;
    const char* orderedReturnDiagnostic_ = nullptr;

    void enforceBudget();
    void configureBudget(std::uint64_t transientPeakBytes,
        std::uint32_t transientResourceCount,
        std::uint32_t logicalFrameSlots);
    void cacheTexture(arp::core::UniquePtr<rhi::GpuTexture>& texture,
        std::shared_ptr<rhi::OriginalOrderedGraphicsExecution> ordered = {});
    void cacheBuffer(arp::core::UniquePtr<rhi::GpuBuffer>& buffer,
        std::shared_ptr<rhi::OriginalOrderedGraphicsExecution> ordered = {});
    std::uint64_t logicalGroupBytes(
        const core::pmr::Vector<core::UniquePtr<rhi::GpuTexture>>& textures) const;
};

} // namespace arp::render_graph
