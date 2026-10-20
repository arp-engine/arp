#pragma once
#include "rhi/frontend/command_sources.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class OriginalStorageRootClosureFixture final {
public:
    template <class Closure>
    static void verify(rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& root,
        const std::shared_ptr<rhi::OriginalCommandResourceSources>& inventory)
    {
        using C = rhi::OriginalBindlessRootNativeClosureContract;
        constexpr std::uint64_t bytes = 1024u * 1024u;
        const auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        const auto gpu = device.memoryStats().allocatedBytes;
        rhi::OriginalBindlessRootNativeClosureBudget budget{
            10u, true, static_cast<std::uint32_t>(root->elements().size() + 1u), 256u, 256u, bytes};
        assert(!C::validateBudget(budget));
        const char* id = nullptr;
        auto closure = Closure::prepareOriginal(root, inventory, device, budget, quota, id);
        if (id)
            std::fprintf(
                stderr, "Storage closure10 backend=%u: %s\n", unsigned(device.backend()), id);
        assert(closure && !id && !Closure::validateOriginal(closure));
        assert(!closure->nativeExecutionProven() && closure->originalRoot().get() == root.get()
            && closure->originalInventory().get() == inventory.get()
            && closure->resourceEntryCount() == budget.maxResourceEntries);
        budget.maxNativeOwners = closure->nativeOwnerCount();
        budget.maxRetainedOwners = closure->retainedOwnerCount();
        closure.reset();
        assert(!quota->liveBytes());
        for (unsigned field = 0u; field != 5u; ++field) {
            auto bad = budget;
            if (field == 0u)
                --bad.maxResourceEntries;
            if (field == 1u)
                --bad.maxNativeOwners;
            if (field == 2u)
                --bad.maxRetainedOwners;
            if (field == 3u)
                bad.prototypeOptIn = false;
            if (field == 4u)
                bad.contractVersion = 1u;
            assert(!Closure::prepareOriginal(root, inventory, device, bad, quota, id) && id);
            assert(!quota->liveBytes() && device.memoryStats().allocatedBytes == gpu);
        }
        {
            core::CpuMemoryQuotaLease pressure(quota, bytes);
            assert(pressure.charged());
            assert(!Closure::prepareOriginal(root, inventory, device, budget, quota, id) && id);
        }
        assert(!quota->liveBytes());
        closure = Closure::prepareOriginal(root, inventory, device, budget, quota, id);
        assert(closure && !id && !Closure::validateOriginal(closure));
        auto forged = std::shared_ptr<const Closure>(closure.get(), [](const Closure*) {});
        assert(Closure::validateOriginal(forged));
        forged.reset();
        closure.reset();
        assert(!quota->liveBytes() && device.memoryStats().allocatedBytes == gpu);
    }
};
} // namespace arp::test
