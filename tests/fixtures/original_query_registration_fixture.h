#pragma once

#include "rhi/gpu_device.h"

#include <cassert>
#include <string_view>

namespace arp::test {
// Host decision evidence only; this fixture never supplies a native query.
class OriginalQueryRegistrationFixture final {
public:
    static void verifyRejected(rhi::GpuDevice& device)
    {
        constexpr std::uint64_t bytes = 65536u;
        auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        rhi::OriginalResourceRegistrationBudget b{1u, true, 0u, 0u, 0u, 5u, bytes};
        b.queries = {1u, true, 1u};
        const auto decision =
            rhi::OriginalResourceRegistrationContract::queryLowering(device.backend(), b.queries);
        assert(decision.diagnostic && !decision.typedLifetimeIssuerAvailable
            && !decision.executionAvailable);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const auto* id = device.prepareOriginalResourceRegistration(b, quota);
        assert(id && std::string_view(id) == decision.diagnostic && !quota->liveBytes()
            && core::defaultCpuAllocator().stats().allocationCount == allocations);
    }
};
} // namespace arp::test
