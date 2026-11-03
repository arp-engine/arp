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

// Explicit diagnostic/validation profile. It may block at finish/submit and
// allocate recording scratch; it never promotes ordinary recording or NativeSync.
struct PrototypeCommandBatchBudget {
    std::uint32_t contractVersion = 0;
    bool optIn = false;
    std::uint32_t maxCommands = 0;
    std::uint64_t maxCpuBytes = 0;
    std::uint64_t maxRetainedGpuBytes = 0;
    std::uint64_t timeoutNanoseconds = 0;
};

} // namespace arp::rhi
