#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "scriptvm/runtime/value.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace arp::scriptvm {

struct VmError;

struct ScriptValueMemoryDescriptor {
    const void* allocationIdentity = nullptr;
    std::size_t retainedBytes = 0u;
    // True only when the provider guarantees that retainedBytes is already
    // held by an external CpuMemoryQuotaLease for the full Value lifetime.
    bool externallyLeased = false;
};

using ScriptValueMemoryDescriptorFn = bool (*)(
    void*, const Value&, ScriptValueMemoryDescriptor&) noexcept;

struct ScriptValueSpan {
    const Value* values = nullptr;
    std::size_t count = 0u;
};

class ARP_API ScriptValueMemoryAccountant final {
public:
    explicit ScriptValueMemoryAccountant(std::shared_ptr<core::CpuMemoryQuota> quota);

    bool synchronize(const core::pmr::Vector<Value>& globals,
        const ScriptValueSpan* spans,
        std::size_t spanCount,
        const core::pmr::Vector<std::shared_ptr<ScriptCell>>* captures,
        std::size_t maxVisitedValues,
        ScriptValueMemoryDescriptorFn descriptor,
        void* descriptorUserData,
        VmError* error);

    bool synchronizeOwner(const void* owner,
        const core::pmr::Vector<Value>& globals,
        const ScriptValueSpan* spans,
        std::size_t spanCount,
        const core::pmr::Vector<std::shared_ptr<ScriptCell>>* captures,
        std::size_t maxVisitedValues,
        ScriptValueMemoryDescriptorFn descriptor,
        void* descriptorUserData,
        VmError* error);

    void releaseOwner(const void* owner) noexcept;
    bool transferOwner(const void* sourceOwner, const void* targetOwner) noexcept;

    void reset() noexcept;

    std::size_t externalBytes() const noexcept { return lease_.bytes(); }

    std::size_t visitedValueCount() const noexcept { return visitedValueCount_; }

private:
    using ChargeMap = core::pmr::UnorderedMap<const void*, std::size_t>;

    // Traversal identities are scratch only: no Value or ledger ownership is
    // moved to this pool. Blocks use the core allocator and retain the highest
    // admitted graph workspace until reset. Recursive descriptor callbacks
    // receive a separate workspace, preserving the existing reentrant behavior.
    struct TraversalWorkspace final {
        using IdentitySet = core::pmr::UnorderedSet<const void*>;
        std::pmr::unsynchronized_pool_resource pool{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<const Value*> pending{&pool};
        IdentitySet values{IdentitySet::allocator_type{&pool}};
        IdentitySet cells{IdentitySet::allocator_type{&pool}};
        IdentitySet descriptors{IdentitySet::allocator_type{&pool}};

        void clear() noexcept
        {
            pending.clear();
            values.clear();
            cells.clear();
            descriptors.clear();
        }
    };

    core::UniquePtr<TraversalWorkspace> workspace_;
    bool workspaceBusy_ = false;

    struct OwnerLedger final {
        explicit OwnerLedger(std::pmr::memory_resource* resource) : charges(resource) {}

        ChargeMap charges;
        std::size_t visitedValueCount = 0u;
    };

    using OwnerMap = core::pmr::UnorderedMap<const void*, OwnerLedger>;

    bool aggregateBytes(const void* replacementOwner,
        const ChargeMap* replacementCharges,
        std::size_t& total) const noexcept;

    std::shared_ptr<core::CpuMemoryQuota> quota_;
    core::CpuMemoryQuotaLease lease_;
    OwnerMap owners_{&core::defaultCpuMemoryResource()};
    std::size_t visitedValueCount_ = 0u;
};

} // namespace arp::scriptvm
