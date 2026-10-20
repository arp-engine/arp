#pragma once

#include <cstdint>

namespace arp::rhi::semantics {

// CacheBudget/1 configures a bounded, backend-owned cache. The workload owner
// supplies a stable policy id and a fixed unit capacity; zero is deliberately
// not an implicit unlimited production budget.
inline constexpr std::uint32_t kCacheBudgetContractVersion = 1u;

struct CacheBudget {
    std::uint32_t contractVersion = kCacheBudgetContractVersion;
    const char* policyId = nullptr;
    std::uint64_t capacity = 0u;

    bool configured() const noexcept { return policyId != nullptr || capacity != 0u; }

    bool valid() const noexcept
    {
        return contractVersion == kCacheBudgetContractVersion && policyId != nullptr
            && policyId[0] != '\0' && capacity != 0u;
    }
};

struct CacheBudgetStats {
    bool configured = false;
    std::uint64_t capacity = 0u;
    std::uint64_t current = 0u;
    std::uint64_t peak = 0u;
    std::uint64_t rejectionCount = 0u;
};

} // namespace arp::rhi::semantics
