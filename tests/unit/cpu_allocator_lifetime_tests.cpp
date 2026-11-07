#include "core/memory/cpu_allocator.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory_resource>
#include <thread>

namespace {

void allocationScopeStateIsThreadIsolated()
{
    using namespace arp::core;
    auto& resource = defaultCpuMemoryResource();
    for (unsigned round = 0; round < 4; ++round) {
        std::atomic<unsigned> ready{0};
        std::atomic<bool> proceed{false};
        std::array<std::thread, 4> workers;
        for (unsigned i = 0; i < workers.size(); ++i) {
            workers[i] = std::thread([&, i] {
                assert(currentCpuMemoryQuota() == nullptr);
                auto quota = i % 2 ? makeShared<CpuMemoryQuota>(128) : nullptr;
                {
                    CpuMemoryQuotaScope binding(quota);
                    CpuAllocationGroupScope group;
                    auto* allocation = resource.allocate(37, 64);
                    const auto charge = inspectCpuAllocationContaining(allocation);
                    assert(charge.found && charge.quota == quota.get());
                    assert((charge.allocationIdentity == allocation) == (quota != nullptr));
                    ready.fetch_add(1, std::memory_order_release);
                    while (!proceed.load(std::memory_order_acquire))
                        std::this_thread::yield();
                    assert(currentCpuMemoryQuota() == quota);
                    resource.deallocate(allocation, 37, 64);
                }
                assert(currentCpuMemoryQuota() == nullptr);
                assert(!quota || quota->liveBytes() == 0);
            });
        }
        while (ready.load(std::memory_order_acquire) != workers.size())
            std::this_thread::yield();
        assert(currentCpuMemoryQuota() == nullptr);
        auto independent = makeUnique<int>(3);
        assert(inspectCpuAllocationCharge(independent.get()).found);
        proceed.store(true, std::memory_order_release);
        for (auto& worker : workers)
            worker.join();
    }
}

void allocationGroupsPreserveMixedPayloadsAndTransferredBackingCharge()
{
    using namespace arp::core;
    constexpr std::array<std::size_t, 8> sizes{0, 1, 17, 4097, 70000, 3, 8193, 33};
    constexpr std::array<std::size_t, 8> alignments{1, 8, 16, 256, 4096, 64, 32, 128};
    std::array<void*, sizes.size()> allocations{};
    auto quota = makeShared<CpuMemoryQuota>();
    auto& resource = defaultCpuMemoryResource();
    const auto before = defaultCpuAllocator().stats().liveBytes;
    CpuAllocationChargeInfo chargedBacking;
    {
        CpuAllocationGroupScope group;
        for (std::size_t i = 0; i < sizes.size(); ++i) {
            allocations[i] = resource.allocate(sizes[i], alignments[i]);
            assert(reinterpret_cast<std::uintptr_t>(allocations[i]) % alignments[i] == 0);
            std::memset(allocations[i], static_cast<int>(i + 1), sizes[i] ? sizes[i] : 1);
        }
        chargedBacking = inspectCpuAllocationContaining(allocations.back());
        assert(chargedBacking.found);
        assert(!cpuAllocationOverlapsQuotaOwner(allocations.back(), quota));
        CpuAllocationDesc desc;
        desc.sizeBytes = chargedBacking.sizeBytes;
        CpuAllocationResult source;
        {
            CpuMemoryQuotaScope scope(quota);
            source = defaultCpuAllocator().allocate(desc);
        }
        assert(source.ptr);
        assert(!transferCpuAllocationCharge(source.ptr, allocations.back()));
        assert(transferCpuAllocationCharge(
            source.ptr, const_cast<void*>(chargedBacking.allocationIdentity)));
        defaultCpuAllocator().deallocate(source.ptr, desc);
        assert(quota->liveBytes() == chargedBacking.sizeBytes);
        assert(cpuAllocationOverlapsQuotaOwner(chargedBacking.allocationIdentity, quota));

        CpuAllocationDesc invalid;
        invalid.sizeBytes = std::numeric_limits<std::size_t>::max();
        assert(!defaultCpuAllocator().allocate(invalid).ptr);
        invalid.sizeBytes = 1;
        invalid.alignment = 3;
        assert(!defaultCpuAllocator().allocate(invalid).ptr);
    }
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        const auto* bytes = static_cast<const unsigned char*>(allocations[i]);
        for (std::size_t j = 0; j < (sizes[i] ? sizes[i] : 1); ++j)
            assert(bytes[j] == i + 1);
    }
    // Keep the first tiny object and last backing alive while freeing all
    // other sizes. Child release must not release a transferred backing quota.
    for (std::size_t i = 1; i + 1 < sizes.size(); ++i)
        resource.deallocate(allocations[i], sizes[i], alignments[i]);
    assert(quota->liveBytes() == chargedBacking.sizeBytes);
    std::thread finalRelease([&] {
        resource.deallocate(allocations.front(), sizes.front(), alignments.front());
        resource.deallocate(allocations.back(), sizes.back(), alignments.back());
    });
    finalRelease.join();
    assert(quota->liveBytes() == 0);
    assert(defaultCpuAllocator().stats().liveBytes == before);
}

void allocationGroupsPreserveIndependentLifetimes()
{
    using namespace arp::core;
    auto& resource = defaultCpuMemoryResource();
    const auto before = defaultCpuAllocator().stats();
    std::array<void*, 128> allocations{};
    {
        CpuAllocationGroupScope group;
        for (auto& allocation : allocations)
            allocation = resource.allocate(37, 64);
        for (auto* allocation : allocations) {
            assert(reinterpret_cast<std::uintptr_t>(allocation) % 64 == 0);
            const auto backing = inspectCpuAllocationContaining(allocation);
            assert(backing.found && backing.quota == nullptr && backing.sizeBytes >= 37);
            assert(backing.allocationIdentity != allocation);
            assert(!inspectCpuAllocationCharge(allocation).found);
        }
        assert(defaultCpuAllocator().stats().allocationCount - before.allocationCount
            < allocations.size());
        // Nested scopes share the outer ownership window. Pausing creates a
        // real independent allocation, suitable for a process/thread cache.
        CpuAllocationGroupScope nested;
        {
            CpuAllocationGroupScope paused(false);
            auto independent = makeUnique<int>(91);
            assert(inspectCpuAllocationCharge(independent.get()).found);
        }
    }
    assert(defaultCpuAllocator().stats().liveBytes > before.liveBytes);
    // The backing lease survives the scope and supports concurrent releases.
    std::thread releaseFirstHalf([&] {
        for (std::size_t index = 0; index < allocations.size() / 2; ++index)
            resource.deallocate(allocations[index], 37, 64);
    });
    for (std::size_t index = allocations.size() / 2; index < allocations.size(); ++index)
        resource.deallocate(allocations[index], 37, 64);
    releaseFirstHalf.join();
    assert(defaultCpuAllocator().stats().liveBytes == before.liveBytes);
}

void allocationGroupsKeepQuotaAndFailureContracts()
{
    using namespace arp::core;
    auto quota = makeShared<CpuMemoryQuota>(64);
    auto& resource = defaultCpuMemoryResource();
    const auto before = defaultCpuAllocator().stats().liveBytes;
    {
        CpuAllocationGroupScope group;
        {
            CpuMemoryQuotaScope binding(quota);
            auto object = makeUnique<int>(11);
            const auto charge = inspectCpuAllocationCharge(object.get());
            assert(charge.found && charge.quota == quota.get());
            assert(quota->liveBytes() == sizeof(int));
            bool rejected = false;
            try {
                (void)resource.allocate(65, 8);
            } catch (const std::bad_alloc&) {
                rejected = true;
            }
            assert(rejected && quota->liveBytes() == sizeof(int));
        }
        assert(quota->liveBytes() == 0);

        struct Throws {
            Throws() { throw 1; }
        };

        bool unwound = false;
        try {
            auto object = makeUnique<Throws>();
        } catch (int) {
            unwound = true;
        }
        assert(unwound);
    }
    assert(defaultCpuAllocator().stats().liveBytes == before);
}

void scopeMatchesExactQuotaOwnership()
{
    using namespace arp::core;
    auto quota = makeShared<CpuMemoryQuota>();
    auto other = makeShared<CpuMemoryQuota>();
    auto owner = makeShared<int>(0);
    const std::shared_ptr<CpuMemoryQuota> alias(owner, quota.get());
    const std::shared_ptr<CpuMemoryQuota> nullAlias(owner, nullptr);
    {
        CpuMemoryQuotaScope scope{quota};
        const auto count = quota.use_count();
        const auto before = defaultCpuAllocator().stats();
        assert(scope.matchesQuota(quota));
        assert(!scope.matchesQuota(other));
        assert(!scope.matchesQuota(alias));
        assert(!scope.matchesQuota(nullptr));
        assert(quota.use_count() == count);
        const auto after = defaultCpuAllocator().stats();
        assert(after.allocationCount == before.allocationCount);
        assert(after.liveBytes == before.liveBytes);
    }
    {
        CpuMemoryQuotaScope scope{nullptr};
        assert(scope.matchesQuota(nullptr));
        assert(!scope.matchesQuota(quota));
        assert(!scope.matchesQuota(nullAlias));
    }
    {
        CpuMemoryQuotaScope scope{alias};
        assert(scope.matchesQuota(alias));
        assert(!scope.matchesQuota(quota));
    }
}

struct ThreadExitCache {
    std::pmr::vector<std::uint64_t> values{&arp::core::defaultCpuMemoryResource()};
};

struct ThreadExitAllocationProbe {
    ~ThreadExitAllocationProbe()
    {
        using namespace arp::core;
        assert(currentCpuMemoryQuota() == nullptr);
        CpuMemoryQuotaScope quota(nullptr);
        CpuAllocationGroupScope group;
        auto value = makeUnique<int>(29);
        assert(*value == 29 && inspectCpuAllocationContaining(value.get()).found);
    }
};

void prepareThreadExitAllocationProbe()
{
    thread_local ThreadExitAllocationProbe probe;
    (void)probe;
}

ThreadExitCache& threadExitCache()
{
    thread_local ThreadExitCache cache;
    return cache;
}

void exerciseThreadExit()
{
    auto outer = arp::core::makeShared<arp::core::CpuMemoryQuota>();
    auto inner = arp::core::makeShared<arp::core::CpuMemoryQuota>();

    {
        arp::core::CpuMemoryQuotaScope outerScope{outer};
        assert(arp::core::currentCpuMemoryQuota() == outer);
        threadExitCache().values.resize(128u, 7u);
        assert(outer->liveBytes() > 0u);

        {
            arp::core::CpuMemoryQuotaScope innerScope{inner};
            assert(arp::core::currentCpuMemoryQuota() == inner);
            std::pmr::vector<std::uint64_t> nested{&arp::core::defaultCpuMemoryResource()};
            nested.resize(64u, 11u);
            assert(inner->liveBytes() > 0u);
        }

        assert(arp::core::currentCpuMemoryQuota() == outer);
        assert(inner->liveBytes() == 0u);
    }

    assert(arp::core::currentCpuMemoryQuota() == nullptr);
}

struct PrechargedObject {
    std::uint64_t words[4]{};
};

void quotaLeaseIsAtomicAndTransferable()
{
    auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(128u);
    arp::core::CpuMemoryQuotaLease lease;
    const bool acquired = lease.acquire(quota, 64u);
    if (!acquired)
        std::abort();
    assert(acquired);
    assert(quota->liveBytes() == 64u);
    const bool resized = lease.resize(96u);
    if (!resized)
        std::abort();
    assert(resized);
    assert(quota->liveBytes() == 96u);
    const bool exceeded = lease.resize(129u);
    if (exceeded)
        std::abort();
    assert(!exceeded);
    assert(lease.bytes() == 96u && quota->liveBytes() == 96u);
    lease.release();
    assert(quota->liveBytes() == 0u);

    arp::core::CpuMemoryQuotaLease objectLease;
    const bool objectChargeAcquired = objectLease.acquire(quota, sizeof(PrechargedObject));
    if (!objectChargeAcquired)
        std::abort();
    assert(objectChargeAcquired);
    arp::core::CpuMemoryQuotaScope quotaScope{quota};
    auto object = arp::core::makeQuotaPrechargedUnique<PrechargedObject>(std::move(objectLease));
    assert(quota->liveBytes() == sizeof(PrechargedObject));
    const auto charge = arp::core::inspectCpuAllocationCharge(object.get());
    assert(charge.found && charge.sizeBytes == sizeof(PrechargedObject)
        && charge.quota == quota.get());
    object.reset();
    assert(quota->liveBytes() == 0u);

    arp::core::CpuMemoryQuotaLease sourceLease;
    const bool sourceChargeAcquired = sourceLease.acquire(quota, sizeof(PrechargedObject));
    if (!sourceChargeAcquired)
        std::abort();
    assert(sourceChargeAcquired);
    auto source = arp::core::makeQuotaPrechargedUnique<PrechargedObject>(std::move(sourceLease));
    auto replacement = arp::core::makeQuotaDeferredUnique<PrechargedObject>();
    assert(quota->liveBytes() == sizeof(PrechargedObject));
    const bool chargeTransferred =
        arp::core::transferCpuAllocationCharge(source.get(), replacement.get());
    if (!chargeTransferred)
        std::abort();
    assert(chargeTransferred);
    assert(!arp::core::inspectCpuAllocationCharge(source.get()).quota);
    assert(arp::core::inspectCpuAllocationCharge(replacement.get()).quota == quota.get());
    source.reset();
    assert(quota->liveBytes() == sizeof(PrechargedObject));
    replacement.reset();
    assert(quota->liveBytes() == 0u);
}

void allocationLedgerFindsInteriorAddresses()
{
    auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(4096u);
    arp::core::CpuMemoryQuotaScope quotaScope{quota};
    std::pmr::vector<std::uint64_t> values{&arp::core::defaultCpuMemoryResource()};
    values.resize(32u, 17u);
    const auto first = arp::core::inspectCpuAllocationContaining(values.data());
    const auto interior = arp::core::inspectCpuAllocationContaining(values.data() + 11u);
    if (!first.found || !interior.found || first.allocationIdentity != interior.allocationIdentity
        || first.sizeBytes != interior.sizeBytes || first.quota != quota.get()) {
        std::abort();
    }
    int foreign = 0;
    if (arp::core::inspectCpuAllocationContaining(&foreign).found)
        std::abort();
    const auto identity = first.allocationIdentity;
    values.clear();
    values.shrink_to_fit();
    if (arp::core::inspectCpuAllocationContaining(identity).found)
        std::abort();
}

void allocationLedgerPreservesMixedQuotaOwnership()
{
    auto outerQuota = arp::core::makeShared<arp::core::CpuMemoryQuota>(16384u);
    auto innerQuota = arp::core::makeShared<arp::core::CpuMemoryQuota>(16384u);
    arp::core::pmr::String externallyAllocated{&arp::core::defaultCpuMemoryResource()};
    {
        arp::core::CpuMemoryQuotaScope innerScope{innerQuota};
        externallyAllocated.assign(1024u, 'x');
    }
    const auto stringAllocation =
        arp::core::inspectCpuAllocationContaining(externallyAllocated.data());
    if (!stringAllocation.found || stringAllocation.quota != innerQuota.get())
        std::abort();

    {
        arp::core::CpuMemoryQuotaScope outerScope{outerQuota};
        arp::core::pmr::Map<int, arp::core::pmr::String> values{
            &arp::core::defaultCpuMemoryResource()};
        values.emplace(7, std::move(externallyAllocated));
        const auto nodeAllocation = arp::core::inspectCpuAllocationContaining(&*values.begin());
        const auto nestedAllocation =
            arp::core::inspectCpuAllocationContaining(values.begin()->second.data());
        if (!nodeAllocation.found || nodeAllocation.quota != outerQuota.get()
            || !nestedAllocation.found || nestedAllocation.quota != innerQuota.get()
            || nestedAllocation.allocationIdentity != stringAllocation.allocationIdentity) {
            std::abort();
        }
        assert(outerQuota->liveBytes() == nodeAllocation.sizeBytes);
        assert(innerQuota->liveBytes() == nestedAllocation.sizeBytes);
    }
    assert(outerQuota->liveBytes() == 0u);
    assert(innerQuota->liveBytes() == 0u);
}

} // namespace

int main()
{
    allocationScopeStateIsThreadIsolated();
    allocationGroupsPreserveMixedPayloadsAndTransferredBackingCharge();
    allocationGroupsPreserveIndependentLifetimes();
    allocationGroupsKeepQuotaAndFailureContracts();
    scopeMatchesExactQuotaOwnership();
    quotaLeaseIsAtomicAndTransferable();
    allocationLedgerFindsInteriorAddresses();
    allocationLedgerPreservesMixedQuotaOwnership();
    exerciseThreadExit();

    std::thread worker([] {
        prepareThreadExitAllocationProbe();
        exerciseThreadExit();
    });
    worker.join();

    // The main-thread cache intentionally remains populated. Its natural TLS
    // destructor is the process-exit regression for the immortal raw backend.
    assert(threadExitCache().values.back() == 7u);
    return 0;
}
