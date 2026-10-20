#pragma once

#include "rhi/frontend/descriptor_buffer_write_validation.h"

namespace arp::rhi {
// GeneralWrite1 (whole) and GeneralWrite2 (whole/finite) publish one complete
// scalar group through the existing scoped
// candidate transaction. Creation evidence is not command/completion authority.
struct OriginalGeneralDescriptorWriteRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxBindings = 0u, maxRetainedOwnerSlots = 0u, maxCallbacks = 0u;
    std::array<std::uint64_t, 3u> maxCpuBytes{};
    std::uint64_t maxRetainedAllocationBytes = 0u;
};

class OriginalGeneralDescriptorWriteContract final {
public:
    using Shared = OriginalFiniteDescriptorWriteContract;
    using Facts = Shared::Facts;
    using Decision = Shared::Decision;
    static constexpr auto callbackCount = Shared::callbackCount;
    static constexpr auto quotaCount = Shared::quotaCount;
    // Existing state/group/pipeline/arena/quota/callback custody plus the opaque
    // general declaration owner. Per binding reserves the complete view-parent
    // closure (the largest canonical dependency), its three metadata quota
    // owners, three native observation slots and the published native binding.
    static constexpr auto fixedOwnerSlots = Shared::fixedOwnerSlots + 1u;
    static constexpr auto bindingOwnerSlots =
        OriginalCommandTextureViewSource::completeRetainedOwnerCount + 3u + 3u + 1u;

    static constexpr std::uint64_t ownerSlots(std::uint32_t bindings) noexcept
    {
        return std::uint64_t(fixedOwnerSlots) + std::uint64_t(bindings) * bindingOwnerSlots;
    }

    static constexpr const char* validate(const OriginalGeneralDescriptorWriteRequest& r) noexcept
    {
        if (r.contractVersion != 1u && r.contractVersion != 2u)
            return "RHI-ORIGINAL-GENERAL-WRITE-VERSION";
        if (!r.prototypeOptIn)
            return "RHI-ORIGINAL-GENERAL-WRITE-OPT-IN";
        if (!r.maxBindings || ownerSlots(r.maxBindings) > r.maxRetainedOwnerSlots
            || r.maxCallbacks != callbackCount || !r.maxRetainedAllocationBytes)
            return "RHI-ORIGINAL-GENERAL-WRITE-BUDGET";
        for (const auto bytes : r.maxCpuBytes)
            if (!bytes)
                return "RHI-ORIGINAL-GENERAL-WRITE-CPU-BUDGET";
        return nullptr;
    }

    static constexpr Decision decide(Backend backend, Facts f = {}) noexcept
    {
        return semantics::DescriptorLowering::OriginalGeneralDescriptorWrite_decide<Decision>(
            backend, f);
    }
};
} // namespace arp::rhi
