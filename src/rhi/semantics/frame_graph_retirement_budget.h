#pragma once

#include "core/budget_vector_preflight.h"
#include "rhi/frontend/resource_cache_accounting.h"

#include <limits>

namespace arp::rhi::semantics {

struct FrameGraphRetirementCounters {
    static constexpr std::size_t dimensionCount = 9u;
    std::uint64_t logicalTextureBytes = 0u;
    std::uint64_t logicalBufferBytes = 0u;
    std::uint64_t logicalAliasBytes = 0u;
    std::uint64_t textureCount = 0u;
    std::uint64_t bufferCount = 0u;
    std::uint64_t aliasHeapCount = 0u;
    std::uint64_t frameBundleCount = 0u;
    std::uint64_t cpuMetadataBytes = 0u;
    std::uint64_t referenceCount = 0u;
};

struct FrameGraphRetirementBudget {
    std::uint32_t contractVersion = 0u;
    FrameGraphRetirementCounters capacity;
};

struct FrameGraphRetirementUsage {
    FrameGraphRetirementCounters inUse;
    FrameGraphRetirementCounters reserved;
    FrameGraphRetirementCounters pending;
    FrameGraphRetirementCounters cached;
};

using FrameGraphRetirementFields = core::BudgetVectorPreflight<FrameGraphRetirementCounters,
    FrameGraphRetirementCounters::dimensionCount>::Fields;
inline constexpr FrameGraphRetirementFields frameGraphRetirementFields{
    &FrameGraphRetirementCounters::logicalTextureBytes,
    &FrameGraphRetirementCounters::logicalBufferBytes,
    &FrameGraphRetirementCounters::logicalAliasBytes,
    &FrameGraphRetirementCounters::textureCount,
    &FrameGraphRetirementCounters::bufferCount,
    &FrameGraphRetirementCounters::aliasHeapCount,
    &FrameGraphRetirementCounters::frameBundleCount,
    &FrameGraphRetirementCounters::cpuMetadataBytes,
    &FrameGraphRetirementCounters::referenceCount};

struct FrameGraphPoolPartitionRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxResourceOwners = 0u;
    FrameGraphRetirementCounters capacity;

    constexpr bool requested() const noexcept
    {
        if (contractVersion || prototypeOptIn || maxResourceOwners)
            return true;
        for (auto field : frameGraphRetirementFields)
            if (capacity.*field)
                return true;
        return false;
    }

    constexpr const char* diagnostic(std::uint64_t metadataQuota) const noexcept
    {
        if (!requested())
            return nullptr;
        if (contractVersion != 1u)
            return "RG-POOL-PARTITION-VERSION";
        if (!prototypeOptIn)
            return "RG-POOL-PARTITION-OPT-IN";
        if (!maxResourceOwners || maxResourceOwners == std::numeric_limits<std::uint32_t>::max())
            return "RG-POOL-PARTITION-OWNERS";
        return capacity.cpuMetadataBytes && capacity.cpuMetadataBytes <= metadataQuota
            ? nullptr
            : "RG-POOL-PARTITION-CPU-BUDGET";
    }
};

struct FrameGraphPoolHeapLeaseRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxHeapLeases = 0u;
    std::uint64_t maxLogicalHeapReservationBytes = 0u;

    constexpr bool requested() const noexcept
    {
        return contractVersion || prototypeOptIn || maxHeapLeases || maxLogicalHeapReservationBytes;
    }

    constexpr const char* diagnostic() const noexcept
    {
        if (!requested())
            return nullptr;
        if (contractVersion != 1u)
            return "RG-POOL-HEAP-VERSION";
        if (!prototypeOptIn)
            return "RG-POOL-HEAP-OPT-IN";
        return maxHeapLeases && maxLogicalHeapReservationBytes ? nullptr : "RG-POOL-HEAP-CAPACITY";
    }
};

struct FrameGraphOwnedResourcesRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;

    constexpr bool requested() const noexcept { return contractVersion || prototypeOptIn; }

    constexpr const char* diagnostic() const noexcept
    {
        if (!requested())
            return nullptr;
        if (contractVersion != 1u && contractVersion != 2u && contractVersion != 3u)
            return "RG-OWNED-RESOURCES-VERSION";
        return prototypeOptIn ? nullptr : "RG-OWNED-RESOURCES-OPT-IN";
    }
};

struct FrameGraphPoolGraphOwnersRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxGraphs = 0u;

    constexpr bool requested() const noexcept
    {
        return contractVersion || prototypeOptIn || maxGraphs;
    }

    constexpr const char* diagnostic() const noexcept
    {
        if (!requested())
            return nullptr;
        if (contractVersion != 1u)
            return "RG-OWNED-RESOURCES-VERSION";
        if (!prototypeOptIn)
            return "RG-OWNED-RESOURCES-OPT-IN";
        return maxGraphs ? nullptr : "RG-OWNED-RESOURCES-GRAPH-CAPACITY";
    }
};

class FrameGraphOwnedResourcesContract final {
public:
    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool retentionAvailable = false;
        bool validatorOnly = false;
        bool nativeExecutionProven = false;
        const char* diagnostic = "RG-OWNED-RESOURCES-BACKEND";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::Equivalent, true, true, false, nullptr};
        case Backend::Vulkan:
            return {CapabilityMode::Equivalent, true, false, false, nullptr};
        case Backend::Metal:
        case Backend::WebGPU:
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RG-OWNED-RESOURCES-BACKEND-UNAVAILABLE"};
        default:
            return {};
        }
    }
};

struct FrameGraphPoolAllocationBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxTextures = 0u, maxBuffers = 0u;
    std::uint64_t maxLogicalTextureBytes = 0u, maxLogicalBufferBytes = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
    FrameGraphPoolHeapLeaseRequest heapLeases;
    FrameGraphPoolPartitionRequest partitions;
    FrameGraphPoolGraphOwnersRequest graphOwners;
};

class FrameGraphPoolAllocationContract final {
public:
    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool allocationOwnerAvailable = false;
        bool validatorOnly = false;
        bool nativeExecutionProven = false;
        const char* diagnostic = "RG-POOL-ALLOCATION-BACKEND";
    };

    static constexpr const char* validateIntent(const FrameGraphPoolAllocationBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RG-POOL-ALLOCATION-VERSION";
        if (!b.prototypeOptIn)
            return "RG-POOL-ALLOCATION-OPT-IN";
        if (const auto* id = b.graphOwners.diagnostic())
            return id;
        if (b.graphOwners.requested() && !b.partitions.requested())
            return "RG-OWNED-RESOURCES-PARTITIONS";
        if (const auto* id = b.partitions.diagnostic(b.maxCpuMetadataBytes))
            return id;
        if (const auto* id = b.heapLeases.diagnostic())
            return id;
        if ((!b.maxTextures && !b.maxBuffers && !b.heapLeases.requested())
            || bool(b.maxTextures) != bool(b.maxLogicalTextureBytes)
            || bool(b.maxBuffers) != bool(b.maxLogicalBufferBytes))
            return "RG-POOL-ALLOCATION-CAPACITY";
        return b.maxCpuMetadataBytes ? nullptr : "RG-POOL-ALLOCATION-CPU-BUDGET";
    }

    static constexpr Decision lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::Equivalent, true, true, false, nullptr};
        case Backend::Vulkan:
            return {CapabilityMode::Equivalent, true, false, false, nullptr};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RG-POOL-ALLOCATION-METAL-OWNER-UNAVAILABLE"};
        case Backend::WebGPU:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RG-POOL-ALLOCATION-WEBGPU-OWNER-UNAVAILABLE"};
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RG-POOL-ALLOCATION-WEBGL-OWNER-UNAVAILABLE"};
        default:
            return {};
        }
    }

    static constexpr Decision heapLowering(
        Backend backend, const FrameGraphPoolHeapLeaseRequest& request) noexcept
    {
        if (!request.requested())
            return {CapabilityMode::DiagnosticFailure, false, false, false, "RG-POOL-HEAP-OPT-IN"};
        if (const auto* id = request.diagnostic())
            return {CapabilityMode::DiagnosticFailure, false, false, false, id};
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::Equivalent, true, true, false, nullptr};
        case Backend::Vulkan:
            return {CapabilityMode::Equivalent, true, false, false, nullptr};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RG-POOL-HEAP-METAL-OWNER-UNAVAILABLE"};
        case Backend::WebGPU:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RG-POOL-HEAP-WEBGPU-OWNER-UNAVAILABLE"};
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RG-POOL-HEAP-WEBGL-OWNER-UNAVAILABLE"};
        default:
            return {CapabilityMode::DiagnosticFailure, false, false, false, "RG-POOL-HEAP-BACKEND"};
        }
    }
};

struct FrameGraphRetirementBudgetRequest {
    std::uint32_t contractVersion = 0u;
    ResourceCacheAccountingDomain domain = ResourceCacheAccountingDomain::Unspecified;
    FrameGraphRetirementCounters addition;
    bool requiresNativePhysicalBound = false;
};

struct FrameGraphRetirementBudgetResult {
    const char* diagnostic = "RG-RETIREMENT-BUDGET-VERSION";
    std::size_t dimension = FrameGraphRetirementCounters::dimensionCount;
    FrameGraphRetirementCounters projected;

    bool fits() const noexcept { return diagnostic == nullptr; }
};

// FrameGraphPoolMetadataSource/1 is an explicit CPU prototype. These slots do
// not reserve GPU owners or authorize command issue, reuse or completion.
struct FrameGraphRetirementMetadataBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxReservations = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
};

class FrameGraphRetirementMetadataContract final {
public:
    static constexpr const char* validateIntent(
        const FrameGraphRetirementMetadataBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RG-RETIREMENT-METADATA-VERSION";
        if (!budget.prototypeOptIn)
            return "RG-RETIREMENT-METADATA-OPT-IN";
        if (!budget.maxReservations)
            return "RG-RETIREMENT-METADATA-CAPACITY";
        if (!budget.maxCpuMetadataBytes)
            return "RG-RETIREMENT-METADATA-CPU-BUDGET";
        return nullptr;
    }
};

struct FrameGraphSharedImportsRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;

    constexpr bool requested() const noexcept { return contractVersion || prototypeOptIn; }

    constexpr const char* diagnostic() const noexcept
    {
        if (!requested())
            return nullptr;
        if (contractVersion != 1u)
            return "RG-SHARED-IMPORTS-VERSION";
        return prototypeOptIn ? nullptr : "RG-SHARED-IMPORTS-OPT-IN";
    }
};

class FrameGraphSharedImportsContract final {
public:
    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool retentionAvailable = false;
        bool validatorOnly = false;
        bool nativeExecutionProven = false;
        const char* diagnostic = "RG-SHARED-IMPORTS-BACKEND";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::Equivalent, true, true, false, nullptr};
        case Backend::Vulkan:
            return {CapabilityMode::Equivalent, true, false, false, nullptr};
        case Backend::Metal:
        case Backend::WebGPU:
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RG-SHARED-IMPORTS-BACKEND-UNAVAILABLE"};
        default:
            return {};
        }
    }
};

struct FrameGraphCompiledRetirementSourceBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxResourceBindings = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
    FrameGraphSharedImportsRequest sharedImports;
    FrameGraphOwnedResourcesRequest ownedResources;
};

class FrameGraphCompiledRetirementSourceContract final {
public:
    static constexpr const char* validateIntent(
        const FrameGraphCompiledRetirementSourceBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RG-COMPILED-SOURCE-VERSION";
        if (!budget.prototypeOptIn)
            return "RG-COMPILED-SOURCE-OPT-IN";
        if (const auto* id = budget.sharedImports.diagnostic())
            return id;
        if (const auto* id = budget.ownedResources.diagnostic())
            return id;
        if ((budget.sharedImports.requested() && budget.ownedResources.contractVersion == 1u)
            || ((budget.ownedResources.contractVersion == 2u
                    || budget.ownedResources.contractVersion == 3u)
                && !budget.sharedImports.requested()))
            return "RG-OWNED-RESOURCES-MIXED-REQUEST";
        return budget.maxCpuMetadataBytes ? nullptr : "RG-COMPILED-SOURCE-CPU-BUDGET";
    }
};

struct FrameGraphRetirementFrameStorageBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxFrames = 0u;
    std::uint32_t maxTexturesPerFrame = 0u;
    std::uint32_t maxBuffersPerFrame = 0u;
    std::uint32_t maxAliasHeapsPerFrame = 0u;
    std::uint32_t maxReferencesPerFrame = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
};

class FrameGraphRetirementFrameStorageContract final {
public:
    static constexpr const char* validateIntent(
        const FrameGraphRetirementFrameStorageBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RG-FRAME-STORAGE-VERSION";
        if (!budget.prototypeOptIn)
            return "RG-FRAME-STORAGE-OPT-IN";
        if (!budget.maxFrames)
            return "RG-FRAME-STORAGE-CAPACITY";
        return budget.maxCpuMetadataBytes ? nullptr : "RG-FRAME-STORAGE-CPU-BUDGET";
    }

    // Pure counts only. The pool separately authenticates its original token.
    static constexpr const char* validateCapacity(
        const FrameGraphRetirementFrameStorageBudget& budget,
        std::size_t textures,
        std::size_t buffers,
        std::size_t heaps,
        std::size_t references) noexcept
    {
        if (const auto* id = validateIntent(budget))
            return id;
        if (textures > budget.maxTexturesPerFrame)
            return "RG-FRAME-STORAGE-TEXTURE-CAPACITY";
        if (buffers > budget.maxBuffersPerFrame)
            return "RG-FRAME-STORAGE-BUFFER-CAPACITY";
        if (heaps > budget.maxAliasHeapsPerFrame)
            return "RG-FRAME-STORAGE-HEAP-CAPACITY";
        return references > budget.maxReferencesPerFrame ? "RG-FRAME-STORAGE-REFERENCE-CAPACITY"
                                                         : nullptr;
    }
};

// FrameGraphRetirementBudgetPreflight/1. Pure metadata only: no native resource,
// pool ticket, storage reservation, command admission or completion authority.
class FrameGraphRetirementBudgetPreflight final {
public:
    static FrameGraphRetirementBudgetResult assess(Backend backend,
        const FrameGraphRetirementBudget& budget,
        const FrameGraphRetirementUsage& usage,
        const FrameGraphRetirementBudgetRequest& request) noexcept
    {
        if (budget.contractVersion != 1u)
            return {"RG-RETIREMENT-BUDGET-VERSION"};
        if (request.contractVersion != 1u)
            return {"RG-RETIREMENT-BUDGET-REQUEST-VERSION"};
        switch (backend) {
        case Backend::Null:
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
        case Backend::WebGL:
            break;
        default:
            return {"RG-RETIREMENT-BUDGET-BACKEND"};
        }
        if (request.domain != ResourceCacheAccountingDomain::LogicalDescriptor)
            return {"RG-RETIREMENT-BUDGET-DOMAIN"};
        if (request.requiresNativePhysicalBound)
            return {"RG-RETIREMENT-BUDGET-PHYSICAL-BOUND-UNAVAILABLE"};
        constexpr auto& fields = frameGraphRetirementFields;
        const auto result =
            core::BudgetVectorPreflight<FrameGraphRetirementCounters, fields.size()>::assess(
                budget.capacity,
                std::array{usage.inUse, usage.reserved, usage.pending, usage.cached},
                request.addition,
                fields);
        switch (result.status) {
        case core::BudgetVectorStatus::Fits:
            return {nullptr, fields.size(), result.projected};
        case core::BudgetVectorStatus::LiveOverflow:
            return {"RG-RETIREMENT-BUDGET-LIVE-OVERFLOW", result.dimension};
        case core::BudgetVectorStatus::LiveExceedsCapacity:
            return {"RG-RETIREMENT-BUDGET-LIVE-EXCEEDS-CAPACITY", result.dimension};
        case core::BudgetVectorStatus::Overflow:
            return {"RG-RETIREMENT-BUDGET-OVERFLOW", result.dimension};
        case core::BudgetVectorStatus::Capacity:
            return {"RG-RETIREMENT-BUDGET-CAPACITY", result.dimension};
        }
        return {"RG-RETIREMENT-BUDGET-CAPACITY"};
    }
};

} // namespace arp::rhi::semantics
