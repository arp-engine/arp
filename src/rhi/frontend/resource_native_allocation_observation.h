#pragma once
#include "rhi/semantics/resource_lowering.h"

namespace arp::rhi {
// Optional cold creation substrate only. Reserve the complete explicit native
// ceiling before creating one independent resource; observe its actual bytes,
// reject overflow without publication, then shrink the same reservation.
// No content, state, registration, completion or aliasing authority is issued.
class OriginalNativeAllocationObservationContract final {
public:
    struct Intent {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
    };
    enum class Disposition { DiagnosticFailure, ValidatorOnly, Prototype };

    struct Decision {
        Disposition disposition = Disposition::DiagnosticFailure;
        bool reserveThenObserve = false;
        const char* diagnostic = "RHI-NATIVE-ALLOCATION-OBSERVATION-VERSION";
    };

    static constexpr const char* validateIntent(
        const Intent& intent, std::uint32_t creationVersion, std::uint64_t ceiling) noexcept
    {
        if (intent.contractVersion != 1u)
            return "RHI-NATIVE-ALLOCATION-OBSERVATION-VERSION";
        if (!intent.prototypeOptIn)
            return "RHI-NATIVE-ALLOCATION-OBSERVATION-OPT-IN";
        if (creationVersion != 3u || !ceiling)
            return "RHI-NATIVE-ALLOCATION-OBSERVATION-CREATION-PROFILE";
        return nullptr;
    }

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        return semantics::ResourceLowering::OriginalNativeAllocationObservation_lowering<Decision,
            Disposition>(backend, version);
    }

    static constexpr const char* validateObserved(
        std::uint64_t reserved, std::uint64_t actual) noexcept
    {
        return reserved && actual && actual <= reserved ? nullptr
                                                        : "RHI-NATIVE-ALLOCATION-OBSERVATION-BOUND";
    }
};
} // namespace arp::rhi
