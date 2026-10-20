#pragma once
#include "core/cpu_allocator.h"
#include "core/monotone_threshold_search.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace arp::test {
// Measure real allocator high water before exhausted probes raise the historical
// peak to the limit. Call measure for each factory, then verify exact/one-less.
class CpuQuotaFactoryFixture final {
public:
    explicit CpuQuotaFactoryFixture(std::shared_ptr<core::CpuMemoryQuota> quota)
        : quota_(std::move(quota))
    {}

    template <class Factory> std::uint64_t measure(Factory&& factory) const
    {
        core::CpuMemoryQuotaLease marker(quota_, quota_->peakBytes() - quota_->liveBytes() + 1u);
        assert(marker.charged());
        const auto baseline = quota_->liveBytes();
        auto object = factory();
        assert(object);
        const auto peak = quota_->peakBytes() - baseline;
        assert(peak);
        return peak;
    }

    template <class Factory> void verify(std::uint64_t peak, Factory&& factory) const
    {
        const auto baseline = quota_->liveBytes();
        {
            core::CpuMemoryQuotaLease pressure(quota_, quota_->limitBytes() - baseline - peak);
            assert(pressure.charged());
            auto exact = factory();
            assert(exact);
        }
        assert(quota_->liveBytes() == baseline);
        {
            core::CpuMemoryQuotaLease pressure(quota_, quota_->limitBytes() - baseline - peak + 1u);
            assert(pressure.charged());
            auto less = factory();
            assert(!less);
        }
        assert(quota_->liveBytes() == baseline);
    }

    template <std::size_t Domains, class Action>
    static const char* verifyHotNoAllocation(
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, Domains>& quotas, Action&& action)
    {
        static_assert(noexcept(action()), "Hot action must not throw");
        std::array<core::CpuMemoryQuotaLease, Domains> pressure;
        for (std::size_t i = 0u; i < Domains; ++i) {
            assert(quotas[i]);
            pressure[i] = core::CpuMemoryQuotaLease(
                quotas[i], quotas[i]->limitBytes() - quotas[i]->liveBytes());
            assert(pressure[i].charged());
        }
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const char* diagnostic = action();
        std::pmr::set_default_resource(previous);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        return diagnostic;
    }

    // One-shot publications may retire their original candidate on destruction.
    // Rebuild the genuine candidate before pressure, never under the quota probe.
    template <class Prepare, class Factory>
    void verifySingleUse(std::uint64_t peak, Prepare&& prepare, Factory&& factory) const
    {
        assert(peak);
        for (const auto available : std::array<std::uint64_t, 3>{peak, peak - 1u, 0u}) {
            prepare();
            const auto baseline = quota_->liveBytes();
            {
                core::CpuMemoryQuotaLease pressure(
                    quota_, quota_->limitBytes() - baseline - available);
                assert(pressure.charged());
                auto result = factory();
                assert(static_cast<bool>(result) == (available == peak));
            }
            assert(quota_->liveBytes() == baseline);
        }
    }

    // History-independent cold admission measurement. Rebuild a genuine
    // one-shot candidate before each probe, preserve the quota limit/domain,
    // and find the exact minimum available bytes by a monotone core search.
    // Factories must have deterministic charges and no retained failed output.
    template <class Prepare, class Factory>
    std::uint64_t measureSingleUse(Prepare&& prepare, Factory&& factory) const
    {
        prepare();
        const auto baseline = quota_->liveBytes();
        assert(baseline < quota_->limitBytes());
        const auto available = quota_->limitBytes() - baseline;
        const auto measured =
            core::MonotoneThresholdSearch::findMinimum(0u, available, [&](std::uint64_t bytes) {
                prepare();
                assert(quota_->liveBytes() == baseline);
                bool successful = false;
                {
                    const auto reserved = available - bytes;
                    core::CpuMemoryQuotaLease pressure(quota_, reserved);
                    assert(!reserved || pressure.charged());
                    auto result = factory();
                    successful = static_cast<bool>(result);
                }
                if (quota_->liveBytes() != baseline)
                    std::fprintf(stderr,
                        "Quota factory retained bytes: baseline=%llu live=%llu available=%llu success=%u\n",
                        static_cast<unsigned long long>(baseline),
                        static_cast<unsigned long long>(quota_->liveBytes()),
                        static_cast<unsigned long long>(bytes),
                        successful);
                assert(quota_->liveBytes() == baseline);
                return successful;
            });
        assert(measured.status == core::MonotoneThresholdSearch::Status::Found && measured.value);
        return measured.value;
    }

    // For factories whose every successful allocation stays owned by the
    // returned object, the live charge is the exact incremental high water.
    // Observe the allocator rather than estimating headers/object sizes. A
    // transient deallocation invalidates this measurement and fails the test.
    template <class Factory> std::uint64_t measureRetained(Factory&& factory) const
    {
        const auto baseline = quota_->liveBytes();
        const auto deallocations = core::defaultCpuAllocator().stats().deallocationCount;
        std::uint64_t retained = 0u;
        {
            auto result = factory();
            assert(result);
            assert(core::defaultCpuAllocator().stats().deallocationCount == deallocations);
            assert(quota_->liveBytes() > baseline);
            retained = quota_->liveBytes() - baseline;
        }
        assert(quota_->liveBytes() == baseline);
        return retained;
    }

private:
    std::shared_ptr<core::CpuMemoryQuota> quota_;
};
} // namespace arp::test
