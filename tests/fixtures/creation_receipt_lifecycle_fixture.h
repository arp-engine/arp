#pragma once
#include "core/cpu_allocator.h"
#include "core/runtime/creation_receipt_lifecycle.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class CreationReceiptLifecycleFixture final {
public:
    static void verify()
    {
        using Lifecycle = core::runtime::CreationReceiptLifecycle;
        using Phase = Lifecycle::Phase;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        unsigned validations = 0;
        const auto valid = [&]() noexcept -> const char* {
            ++validations;
            return nullptr;
        };
        Lifecycle ready(3);
        assert(ready.phase() == Phase::Prepared && !ready.evidence().proven());
        assert(ready.begin() && !ready.begin());
        ready.observe(2, true, "receipt", valid);
        ready.observe(0, true, "receipt", valid);
        assert(ready.phase() == Phase::Pending && validations == 0);
        ready.observe(1, true, "receipt", valid);
        assert(ready.phase() == Phase::Ready && ready.evidence().proven() && validations == 1);
        ready.observe(1, true, "duplicate", valid);
        assert(ready.phase() == Phase::Rejected && !ready.evidence().proven());
        for (unsigned scenario = 0; scenario < 4; ++scenario) {
            Lifecycle rejected(3);
            if (scenario != 0)
                assert(rejected.begin());
            if (scenario == 2) {
                rejected.observe(0, true, "receipt", valid);
                rejected.observe(0, true, "first", valid);
            } else
                rejected.observe(scenario == 3 ? 3 : 0, scenario != 1, "first", valid);
            assert(rejected.phase() == Phase::Rejected && !rejected.begin());
            const auto* first = rejected.failure();
            assert(first);
            rejected.reject("second");
            for (unsigned i = 0; i < 3; ++i)
                if (!rejected.evidence().hasReceived(i))
                    rejected.observe(i, true, "drain", valid);
            assert(rejected.evidence().allReceived() && rejected.phase() == Phase::Rejected
                && rejected.failure() == first);
        }
        Lifecycle finalFailure(2);
        assert(finalFailure.begin());
        finalFailure.observe(1, true, "receipt", valid);
        finalFailure.observe(0, true, "receipt", []() noexcept {
            return "immutable-input-changed";
        });
        assert(finalFailure.phase() == Phase::Rejected && finalFailure.evidence().allReceived());
        assert(validations == 1);
        for (unsigned count : {0u, 65u}) {
            Lifecycle invalid(count);
            assert(invalid.begin());
            invalid.observe(0, true, "invalid", valid);
            assert(invalid.phase() == Phase::Rejected && !invalid.evidence().proven());
        }
        std::pmr::set_default_resource(previous);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        std::puts(
            "BINDLESS_CREATION_RECEIPT_LIFECYCLE_OK no-premature-ready duplicate-unknown-error final-validation sticky-drain hostile-pmr allocation0");
    }
};
} // namespace arp::test
