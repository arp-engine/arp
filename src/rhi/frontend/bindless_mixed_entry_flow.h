#pragma once
#include "rhi/frontend/command_sources.h"
#include "rhi/semantics/bindless_mixed_entry_flow.h"

namespace arp::rhi {
// An authenticated three-entry dataflow over the original canonical inventory:
// completed depth -> copy cell -> reduction cell -> Results. Entry names,
// physical binding numbers and backend names never determine resource roles.
// This borrowed inspection grants no recording, mutation or completion rights.
class ARP_API OriginalBindlessMixedEntryFlowContract final {
public:
    using Root = OriginalCommandBindlessRootSource;
    using Roots = std::array<std::shared_ptr<const Root>, 3u>;

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxDeclarationVisits = 0u;
    };

    struct Inspection {
        const char* diagnostic = nullptr;
        std::uint64_t declarationVisits = 0u;
        std::array<std::size_t, 3u> rootSources{SIZE_MAX, SIZE_MAX, SIZE_MAX};
        std::size_t depthSource = SIZE_MAX, copyOutputSource = SIZE_MAX;
        std::size_t reduceOutputSource = SIZE_MAX, resultsSource = SIZE_MAX;

        constexpr bool nativeExecutionProven() const noexcept { return false; }
    };

    static constexpr const char* validateIntent(const Budget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-BINDLESS-MIXED-ENTRY-FLOW-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-BINDLESS-MIXED-ENTRY-FLOW-OPT-IN";
        return b.maxDeclarationVisits ? nullptr : "RHI-BINDLESS-MIXED-ENTRY-FLOW-BUDGET";
    }

    static constexpr auto lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        return semantics::OriginalBindlessMixedEntryFlowLowering::lowering(backend, version);
    }

    static Inspection inspect(const Roots&,
        const std::shared_ptr<const OriginalCommandResourceSources>&,
        const Budget&) noexcept;
};
} // namespace arp::rhi
