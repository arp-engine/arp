#pragma once

#include "rhi/gpu_device.h"

#include <cassert>
#include <string_view>

namespace arp::tests {
class OriginalHeapRegistrationFixture final {
public:
    // CPU/API rejection only; this deliberately makes no native backend claim.
    static void verifyUnavailable(rhi::GpuDevice& device, const char* expected)
    {
        constexpr std::uint64_t bytes = 65536u;
        auto domain = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto metadata = core::makeShared<core::CpuMemoryQuota>(bytes);
        assert(!device.prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
        rhi::OriginalResourceRegistrationBudget budget{1u, true, 0u, 0u, 0u, 5u, bytes};
        budget.heaps = {1u, true, 1u};
        const auto* diagnostic = device.prepareOriginalResourceRegistration(budget, metadata);
        assert(diagnostic && std::string_view(diagnostic) == expected);
        assert(!metadata->liveBytes());
        assert(device.originalPoolAllocationRegistrationDiagnostic());
    }
};
} // namespace arp::tests
