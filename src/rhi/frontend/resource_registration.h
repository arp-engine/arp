#pragma once
#include "core/memory/atomic_capacity.h"
#include "core/ownership/lifetime_epoch.h"
#include "core/ownership/monotonic_id_sequence.h"
#include "rhi/frontend/resource_device_lifetime.h"
#include "rhi/frontend/resource_device_native_loss.h"
#include "rhi/semantics/resource_lowering.h"

namespace arp::rhi {
class GpuDevice;
enum class OriginalRegisteredResourceKind {
    Buffer,
    Texture,
    TextureView,
    Unsupported,
    PipelineLayout,
    GraphicsPipeline,
    ComputePipeline,
    DescriptorArena,
    Descriptor,
    DescriptorTable,
    Sampler,
    MemoryHeap,
    QueryPool
};

// Independent opt-in; ordinary sampler creation does not acquire original custody.
struct OriginalSamplerRegistrationRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxSamplers = 0u;

    constexpr bool requested() const noexcept
    {
        return contractVersion || prototypeOptIn || maxSamplers;
    }

    constexpr const char* diagnostic() const noexcept
    {
        if (!requested())
            return nullptr;
        if (contractVersion != 1u)
            return "RHI-ORIGINAL-SAMPLER-REGISTRATION-VERSION";
        if (!prototypeOptIn)
            return "RHI-ORIGINAL-SAMPLER-REGISTRATION-OPT-IN";
        return maxSamplers ? nullptr : "RHI-ORIGINAL-SAMPLER-REGISTRATION-CAPACITY";
    }
};

// Independent factory-provenance request; no placement or completion authority.
struct OriginalHeapRegistrationRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxHeaps = 0u;

    constexpr bool requested() const noexcept
    {
        return contractVersion || prototypeOptIn || maxHeaps;
    }

    constexpr const char* diagnostic() const noexcept
    {
        if (!requested())
            return nullptr;
        if (contractVersion != 1u)
            return "RHI-ORIGINAL-HEAP-REGISTRATION-VERSION";
        if (!prototypeOptIn)
            return "RHI-ORIGINAL-HEAP-REGISTRATION-OPT-IN";
        return maxHeaps ? nullptr : "RHI-ORIGINAL-HEAP-REGISTRATION-CAPACITY";
    }
};

struct OriginalQueryRegistrationRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxQueryPools = 0u;

    constexpr bool requested() const noexcept
    {
        return contractVersion || prototypeOptIn || maxQueryPools;
    }

    constexpr const char* diagnostic() const noexcept
    {
        if (!requested())
            return nullptr;
        if (contractVersion != 1u)
            return "RHI-ORIGINAL-QUERY-REGISTRATION-VERSION";
        if (!prototypeOptIn)
            return "RHI-ORIGINAL-QUERY-REGISTRATION-OPT-IN";
        return maxQueryPools ? nullptr : "RHI-ORIGINAL-QUERY-REGISTRATION-CAPACITY";
    }
};

struct OriginalResourceRegistrationBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxBuffers = 0u, maxTextures = 0u, maxTextureViews = 0u, maxRetainedOwners = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
    std::uint32_t maxPipelineLayouts = 0u, maxGraphicsPipelines = 0u, maxComputePipelines = 0u;
    std::uint32_t maxDescriptorArenas = 0u, maxDescriptorSets = 0u, maxDescriptorTables = 0u;
    OriginalSamplerRegistrationRequest samplers;
    OriginalHeapRegistrationRequest heaps;
    OriginalQueryRegistrationRequest queries;
};

class OriginalResourceRegistrationContract final {
public:
    static constexpr bool observesNativeLoss(std::uint32_t version) noexcept
    {
        return version == 7u || version == 8u || version == 9u || version == 10u;
    }

    static constexpr std::uint32_t fixedOwnerCount = 3u, perResourceOwnerCount = 2u;
    static constexpr std::uint32_t perPipelineOwnerCount = 4u;
    static constexpr std::uint32_t perFullDefinitionPipelineOwnerCount = 5u;
    static constexpr std::uint32_t perDescriptorOwnerCount = 3u;

    static constexpr bool isPipeline(OriginalRegisteredResourceKind kind) noexcept
    {
        return kind == OriginalRegisteredResourceKind::GraphicsPipeline
            || kind == OriginalRegisteredResourceKind::ComputePipeline;
    }

    static constexpr bool isDescriptor(OriginalRegisteredResourceKind kind) noexcept
    {
        return kind == OriginalRegisteredResourceKind::Descriptor
            || kind == OriginalRegisteredResourceKind::DescriptorTable;
    }

    static constexpr bool isTypedObject(OriginalRegisteredResourceKind kind) noexcept
    {
        return kind == OriginalRegisteredResourceKind::PipelineLayout || isPipeline(kind)
            || kind == OriginalRegisteredResourceKind::DescriptorArena || isDescriptor(kind)
            || kind == OriginalRegisteredResourceKind::QueryPool;
    }

    static constexpr bool hasResourceId(OriginalRegisteredResourceKind kind) noexcept
    {
        return kind == OriginalRegisteredResourceKind::Buffer
            || kind == OriginalRegisteredResourceKind::Texture
            || kind == OriginalRegisteredResourceKind::TextureView
            || kind == OriginalRegisteredResourceKind::Sampler
            || kind == OriginalRegisteredResourceKind::MemoryHeap;
    }

    static constexpr std::uint32_t ownerCount(
        OriginalRegisteredResourceKind kind, std::uint32_t version = 1u) noexcept
    {
        return isPipeline(kind)
            ? (((version >= 5u && kind == OriginalRegisteredResourceKind::ComputePipeline)
                   || (version >= 6u && kind == OriginalRegisteredResourceKind::GraphicsPipeline))
                      ? perFullDefinitionPipelineOwnerCount + 1u
                      : version >= 4u ? perFullDefinitionPipelineOwnerCount
                                      : perPipelineOwnerCount)
            : isDescriptor(kind) ? perDescriptorOwnerCount
                                 : perResourceOwnerCount;
    }

    static constexpr const char* validateIntent(
        const OriginalResourceRegistrationBudget& b) noexcept
    {
        if (b.contractVersion < 1u || b.contractVersion > 10u)
            return "RHI-ORIGINAL-REGISTRATION-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-REGISTRATION-OPT-IN";
        if (const auto* id = b.samplers.diagnostic())
            return id;
        if (const auto* id = b.heaps.diagnostic())
            return id;
        if (const auto* id = b.queries.diagnostic())
            return id;
        if (b.queries.requested() && observesNativeLoss(b.contractVersion))
            return "RHI-ORIGINAL-QUERY-NATIVE-LOSS-JOIN-UNAVAILABLE";
        if (b.heaps.requested() && observesNativeLoss(b.contractVersion))
            return "RHI-ORIGINAL-HEAP-NATIVE-LOSS-JOIN-UNAVAILABLE";
        const auto typed = b.maxPipelineLayouts || b.maxGraphicsPipelines || b.maxComputePipelines;
        if (b.contractVersion == 1u && typed)
            return "RHI-ORIGINAL-REGISTRATION-TYPED-VERSION";
        const auto descriptors =
            b.maxDescriptorArenas || b.maxDescriptorSets || b.maxDescriptorTables;
        if (b.contractVersion < 3u && descriptors)
            return "RHI-ORIGINAL-REGISTRATION-DESCRIPTOR-VERSION";
        if (!b.maxBuffers && !b.maxTextures && !b.maxTextureViews && !typed && !descriptors
            && !b.samplers.maxSamplers && !b.heaps.maxHeaps && !b.queries.maxQueryPools)
            return "RHI-ORIGINAL-REGISTRATION-KIND-BUDGET";
        if (b.maxTextureViews && !b.maxTextures)
            return "RHI-ORIGINAL-REGISTRATION-PARENT-BUDGET";
        if ((b.maxGraphicsPipelines || b.maxComputePipelines) && !b.maxPipelineLayouts)
            return "RHI-ORIGINAL-REGISTRATION-PARENT-BUDGET";
        if ((b.maxDescriptorSets || b.maxDescriptorTables) && !b.maxDescriptorArenas)
            return "RHI-ORIGINAL-REGISTRATION-PARENT-BUDGET";
        if (b.maxRetainedOwners < fixedOwnerCount + perResourceOwnerCount)
            return "RHI-ORIGINAL-REGISTRATION-OWNER-BUDGET";
        if (!b.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-REGISTRATION-CPU-BUDGET";
        return nullptr;
    }

    struct Decision {
        std::uint32_t contractVersion = 1u;
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool typedLifetimeIssuerAvailable = false, originalStateIssuerAvailable = false;
        bool nativeLossObserved = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-REGISTRATION-BACKEND-UNKNOWN";
        bool pipelineLifetimeIssuerAvailable = false;
        bool descriptorLifetimeIssuerAvailable = false;
        bool fullPipelineDefinitionCustodyAvailable = false;
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (observesNativeLoss(version))
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-TYPED-LOSS-EXPLICIT-REQUEST-REQUIRED",
                false,
                false,
                false};
        if (version < 1u || version > 8u)
            return {version,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-REGISTRATION-VERSION",
                false,
                false,
                false};
        return semantics::ResourceLowering::OriginalResourceRegistration_lowering<Decision>(
            backend, version);
    }

    static constexpr Decision heapLowering(
        Backend backend, const OriginalHeapRegistrationRequest& request) noexcept
    {
        if (!request.requested())
            return {1u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-HEAP-REGISTRATION-OPT-IN"};
        if (const auto* id = request.diagnostic())
            return {1u, CapabilityMode::DiagnosticFailure, false, false, false, false, id};
        return semantics::ResourceLowering::OriginalHeapRegistration_lowering<Decision>(backend);
    }

    static constexpr Decision queryLowering(
        Backend backend, const OriginalQueryRegistrationRequest& request) noexcept
    {
        if (!request.requested())
            return {1u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-QUERY-REGISTRATION-OPT-IN"};
        if (const auto* id = request.diagnostic())
            return {1u, CapabilityMode::DiagnosticFailure, false, false, false, false, id};
        return semantics::ResourceLowering::OriginalQueryRegistration_lowering<Decision>(backend);
    }

    // CPU factory identity/custody only. Neither this request nor a sampler
    // record authorizes descriptor writes, native loss joins or GPU execution.
    static constexpr Decision samplerLowering(
        Backend backend, const OriginalSamplerRegistrationRequest& request) noexcept
    {
        if (!request.requested())
            return {1u,
                CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-SAMPLER-REGISTRATION-OPT-IN"};
        if (const auto* id = request.diagnostic())
            return {1u, CapabilityMode::DiagnosticFailure, false, false, false, false, id};
        return semantics::ResourceLowering::OriginalSamplerRegistration_lowering<Decision>(backend);
    }
};
class OriginalResourceRegistration;
class OriginalResourceRegistrationRegistry;
class OriginalResourceRegistrationBinding;

// A copyable weak receipt for an original factory record, never a resource owner.
class OriginalResourceRegistrationTicket final {
    friend class GpuDevice;
    friend class OriginalResourceRegistrationBinding;

public:
    OriginalResourceRegistrationTicket() = default;
    const char* diagnostic() const noexcept;
    bool matchesIdentity(
        const void* object, std::uint64_t id, OriginalRegisteredResourceKind kind) const noexcept;
    bool matchesObjectIdentity(
        const void* object, OriginalRegisteredResourceKind kind) const noexcept;
    bool matchesParent(const OriginalResourceRegistrationTicket& parent) const noexcept;
    std::uint64_t generation() const noexcept;
    const char* independentCpuQuotaDiagnostic(
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept;

    bool nativeExecutionProven() const noexcept { return false; }

private:
    explicit OriginalResourceRegistrationTicket(
        const std::shared_ptr<OriginalResourceRegistration>& record) noexcept
        : record_(record)
    {}

    std::weak_ptr<OriginalResourceRegistration> record_;
};

// Embedded in the original wrapper; sole persistent strong owner of its record.
class OriginalResourceRegistrationBinding final {
    friend class GpuDevice;

public:
    OriginalResourceRegistrationBinding() = default;
    OriginalResourceRegistrationBinding(const OriginalResourceRegistrationBinding&) = delete;
    OriginalResourceRegistrationBinding& operator=(
        const OriginalResourceRegistrationBinding&) = delete;

    OriginalResourceRegistrationTicket ticket() const noexcept
    {
        return OriginalResourceRegistrationTicket(record_);
    }

private:
    std::shared_ptr<OriginalResourceRegistration> record_;
};

// No synthetic GPU ID. Only the original device can bind a cold typed factory
// candidate; inheritance or a caller-authored kind never grants a receipt.
class OriginalRegisteredObject {
    friend class GpuDevice;

public:
    OriginalResourceRegistrationTicket originalResourceRegistration() const noexcept
    {
        return originalRegistration_.ticket();
    }

    OriginalRegisteredResourceKind originalRegisteredKind() const noexcept { return originalKind_; }

protected:
    explicit OriginalRegisteredObject(OriginalRegisteredResourceKind kind) noexcept
        : originalKind_(kind)
    {}

    ~OriginalRegisteredObject() = default;

private:
    const OriginalRegisteredResourceKind originalKind_;
    OriginalResourceRegistrationBinding originalRegistration_;
};

class OriginalResourceRegistrationRegistry final {
    friend class GpuDevice;
    friend class OriginalResourceRegistration;

    class ConstructionKey {
        friend class GpuDevice;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    OriginalResourceRegistrationRegistry(ConstructionKey,
        const GpuDevice* device,
        std::shared_ptr<OriginalDeviceLifetime> domain,
        OriginalDeviceLifetime::Ticket lifetime,
        OriginalResourceRegistrationBudget budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        std::weak_ptr<const OriginalDeviceNativeLossBinding> loss = {})
        : device_(device), domain_(std::move(domain)), lifetime_(std::move(lifetime)),
          budget_(budget), quota_(std::move(quota)), loss_(std::move(loss)),
          buffers_(budget.maxBuffers), textures_(budget.maxTextures),
          views_(budget.maxTextureViews), layouts_(budget.maxPipelineLayouts),
          graphics_(budget.maxGraphicsPipelines), compute_(budget.maxComputePipelines),
          arenas_(budget.maxDescriptorArenas), sets_(budget.maxDescriptorSets),
          tables_(budget.maxDescriptorTables), samplers_(budget.samplers.maxSamplers),
          heaps_(budget.heaps.maxHeaps), queries_(budget.queries.maxQueryPools),
          owners_(budget.maxRetainedOwners - OriginalResourceRegistrationContract::fixedOwnerCount)
    {}

    OriginalResourceRegistrationRegistry(const OriginalResourceRegistrationRegistry&) = delete;

    bool overlapsCpuQuota(const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
    {
        if (core::SharedOwnerProjection::overlapsOwner(quota_, quota)
            || domain_->overlapsCpuQuota(quota))
            return true;
        const auto loss = loss_.lock();
        return loss
            && core::SharedOwnerProjection::overlapsOwner(loss->originalMetadataQuota(), quota);
    }

private:
    const char* validate() const noexcept
    {
        if (const auto* id = lifetime_.diagnostic())
            return id;
        if (OriginalResourceRegistrationContract::observesNativeLoss(budget_.contractVersion)) {
            const auto loss = loss_.lock();
            if (!loss)
                return "RHI-ORIGINAL-TYPED-LOSS-SUBSCRIPTION-EXPIRED";
            if (!loss->hasQuotaBoundSharedOwner(loss))
                return "RHI-ORIGINAL-TYPED-LOSS-SUBSCRIPTION-OWNER";
            if (const auto* id = loss->diagnostic())
                return id;
        }
        return quota_ && quota_->limitBytes() == budget_.maxCpuMetadataBytes
            ? nullptr
            : "RHI-ORIGINAL-REGISTRATION-QUOTA";
    }

    core::AtomicCapacityBudget* capacity(OriginalRegisteredResourceKind kind) noexcept
    {
        switch (kind) {
        case OriginalRegisteredResourceKind::Buffer:
            return &buffers_;
        case OriginalRegisteredResourceKind::Texture:
            return &textures_;
        case OriginalRegisteredResourceKind::TextureView:
            return &views_;
        case OriginalRegisteredResourceKind::PipelineLayout:
            return &layouts_;
        case OriginalRegisteredResourceKind::GraphicsPipeline:
            return &graphics_;
        case OriginalRegisteredResourceKind::ComputePipeline:
            return &compute_;
        case OriginalRegisteredResourceKind::DescriptorArena:
            return &arenas_;
        case OriginalRegisteredResourceKind::Descriptor:
            return &sets_;
        case OriginalRegisteredResourceKind::DescriptorTable:
            return &tables_;
        case OriginalRegisteredResourceKind::Sampler:
            return &samplers_;
        case OriginalRegisteredResourceKind::MemoryHeap:
            return &heaps_;
        case OriginalRegisteredResourceKind::QueryPool:
            return &queries_;
        case OriginalRegisteredResourceKind::Unsupported:
            return nullptr;
        }
        return nullptr;
    }

    const GpuDevice* const device_;
    const std::shared_ptr<OriginalDeviceLifetime> domain_;
    const OriginalDeviceLifetime::Ticket lifetime_;
    const OriginalResourceRegistrationBudget budget_;
    const std::shared_ptr<core::CpuMemoryQuota> quota_;
    // Device-issued weak custody: no extra strong owner, callback or facade.
    const std::weak_ptr<const OriginalDeviceNativeLossBinding> loss_;
    core::AtomicCapacityBudget buffers_, textures_, views_, layouts_, graphics_, compute_, arenas_,
        sets_, tables_, samplers_, heaps_, queries_, owners_;
    core::MonotonicIdSequence generations_;
};

class OriginalResourceRegistration final {
    friend class GpuDevice;
    friend class OriginalResourceRegistrationTicket;

    class ConstructionKey {
        friend class GpuDevice;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    OriginalResourceRegistration(ConstructionKey,
        std::shared_ptr<OriginalResourceRegistrationRegistry> registry,
        const void* object,
        std::uint64_t id,
        OriginalRegisteredResourceKind kind,
        std::uint64_t generation,
        std::weak_ptr<OriginalResourceRegistration> parent,
        core::LifetimeEpoch::Ticket allocationEpoch = {})
        : registry_(std::move(registry)), object_(object), id_(id), kind_(kind),
          generation_(generation), parent_(std::move(parent)),
          allocationEpoch_(std::move(allocationEpoch)),
          kindCapacity_(
              std::shared_ptr<core::AtomicCapacityBudget>(registry_, registry_->capacity(kind)),
              1u),
          ownerCapacity_(
              std::shared_ptr<core::AtomicCapacityBudget>(registry_, &registry_->owners_),
              OriginalResourceRegistrationContract::ownerCount(
                  kind, registry_->budget_.contractVersion))
    {}

    OriginalResourceRegistration(const OriginalResourceRegistration&) = delete;

private:
    const char* validate() const noexcept
    {
        if (!object_)
            return "RHI-ORIGINAL-REGISTRATION-CANDIDATE-MISSING";
        if (const auto* id = registry_->validate())
            return id;
        if (!kindCapacity_.charged())
            return "RHI-ORIGINAL-REGISTRATION-KIND-CAPACITY";
        if (!ownerCapacity_.charged())
            return "RHI-ORIGINAL-REGISTRATION-OWNER-CAPACITY";
        if (!generation_)
            return "RHI-ORIGINAL-REGISTRATION-GENERATION-EXHAUSTED";
        if (kind_ == OriginalRegisteredResourceKind::TextureView
            || OriginalResourceRegistrationContract::isPipeline(kind_)
            || OriginalResourceRegistrationContract::isDescriptor(kind_)) {
            const auto parent = parent_.lock();
            if (!parent)
                return "RHI-ORIGINAL-REGISTRATION-PARENT-EXPIRED";
            const auto parentKind = kind_ == OriginalRegisteredResourceKind::TextureView
                ? OriginalRegisteredResourceKind::Texture
                : OriginalResourceRegistrationContract::isDescriptor(kind_)
                ? OriginalRegisteredResourceKind::DescriptorArena
                : OriginalRegisteredResourceKind::PipelineLayout;
            if (parent->registry_.get() != registry_.get() || parent->kind_ != parentKind)
                return "RHI-ORIGINAL-REGISTRATION-PARENT-FOREIGN";
            if (const auto* id = parent->validate())
                return id;
            if (OriginalResourceRegistrationContract::isDescriptor(kind_)
                && !allocationEpoch_.isCurrent())
                return "RHI-ORIGINAL-REGISTRATION-ALLOCATION-STALE";
        }
        return nullptr;
    }

    const std::shared_ptr<OriginalResourceRegistrationRegistry> registry_;
    // Null only while privately reserved before the native factory call.
    const void* object_;
    // Filled once by the private heap publisher after a pre-reserved factory.
    std::uint64_t id_;
    const OriginalRegisteredResourceKind kind_;
    const std::uint64_t generation_;
    const std::weak_ptr<OriginalResourceRegistration> parent_;
    const core::LifetimeEpoch::Ticket allocationEpoch_;
    core::AtomicCapacityLease kindCapacity_, ownerCapacity_;
};

inline const char* OriginalResourceRegistrationTicket::diagnostic() const noexcept
{
    const auto record = record_.lock();
    return record ? record->validate() : "RHI-ORIGINAL-REGISTRATION-EXPIRED";
}

inline bool OriginalResourceRegistrationTicket::matchesIdentity(
    const void* object, std::uint64_t id, OriginalRegisteredResourceKind kind) const noexcept
{
    const auto record = record_.lock();
    return OriginalResourceRegistrationContract::hasResourceId(kind) && record
        && !record->validate() && record->object_ == object && record->id_ == id
        && record->kind_ == kind;
}

inline bool OriginalResourceRegistrationTicket::matchesObjectIdentity(
    const void* object, OriginalRegisteredResourceKind kind) const noexcept
{
    const auto record = record_.lock();
    return OriginalResourceRegistrationContract::isTypedObject(kind) && record
        && !record->validate() && record->object_ == object && record->kind_ == kind
        && record->id_ == 0u;
}

inline bool OriginalResourceRegistrationTicket::matchesParent(
    const OriginalResourceRegistrationTicket& input) const noexcept
{
    const auto record = record_.lock();
    const auto parent = input.record_.lock();
    return record && parent && !record->validate() && !parent->validate()
        && (record->kind_ == OriginalRegisteredResourceKind::TextureView
            || OriginalResourceRegistrationContract::isPipeline(record->kind_)
            || OriginalResourceRegistrationContract::isDescriptor(record->kind_))
        && record->parent_.lock() == parent;
}

inline std::uint64_t OriginalResourceRegistrationTicket::generation() const noexcept
{
    const auto record = record_.lock();
    return record && !record->validate() ? record->generation_ : 0u;
}

inline const char* OriginalResourceRegistrationTicket::independentCpuQuotaDiagnostic(
    const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
{
    const auto record = record_.lock();
    if (!record)
        return "RHI-ORIGINAL-REGISTRATION-EXPIRED";
    if (const auto* id = record->validate())
        return id;
    return !quota || record->registry_->overlapsCpuQuota(quota)
        ? "RHI-ORIGINAL-REGISTRATION-QUOTA-DOMAIN"
        : nullptr;
}
} // namespace arp::rhi
