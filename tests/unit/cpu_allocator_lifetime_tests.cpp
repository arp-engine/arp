#include "core/cpu_allocator.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <memory_resource>
#include <thread>

namespace {

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
    scopeMatchesExactQuotaOwnership();
    quotaLeaseIsAtomicAndTransferable();
    allocationLedgerFindsInteriorAddresses();
    allocationLedgerPreservesMixedQuotaOwnership();
    exerciseThreadExit();

    std::thread worker([] {
        exerciseThreadExit();
    });
    worker.join();

    // The main-thread cache intentionally remains populated. Its natural TLS
    // destructor is the process-exit regression for the immortal raw backend.
    assert(threadExitCache().values.back() == 7u);
    return 0;
}
