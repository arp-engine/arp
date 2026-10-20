#pragma once
#include <cstdint>

namespace arp::rhi {
// Cold synchronous creation from immutable original bytes. This selected
// Prototype profile grants no command, output audit or production factory proof.
class OriginalSynchronousPipelineCreationContract final {
public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxTargetBytes = 0u, maxCpuMetadataBytes = 0u,
                      coldCreationTimeoutNanoseconds = 0u;
    };

    static constexpr const char* validateBudget(Budget b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-ORIGINAL-SYNC-PIPELINE-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-SYNC-PIPELINE-OPT-IN";
        return b.maxTargetBytes && b.maxCpuMetadataBytes && b.coldCreationTimeoutNanoseconds
                && b.coldCreationTimeoutNanoseconds <= INT64_MAX
            ? nullptr
            : "RHI-ORIGINAL-SYNC-PIPELINE-BUDGET";
    }
};

} // namespace arp::rhi
