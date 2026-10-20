#pragma once

#include <cstdint>

namespace arp::rhi {

inline constexpr std::uint32_t kCommandRetentionBudgetContractVersion = 1u;

// CommandRetentionBudget/1: independent caller-owned limits. Counts are
// retained ownership/record units; only the allocator measures CPU bytes.
struct CommandRetentionBudget {
    std::uint32_t contractVersion = kCommandRetentionBudgetContractVersion;
    bool optIn = false;
    std::uint32_t maxResourceOwners = 0u;
    std::uint32_t maxQueryPools = 0u;
    std::uint32_t maxTimestampWrites = 0u;
    std::uint32_t maxPipelineStatisticsWrites = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
};

} // namespace arp::rhi
