#pragma once

#include "rhi/semantics/ordered_resource_reuse.h"

#include <array>
#include <cassert>
#include <cstring>
#include <limits>

namespace arp::test {
// Exhausts the caller-authored intent flags. No scalar combination constitutes
// backend custody, a pool lease, native release, or GPU completion authority.
class OrderedReuseAdmissionFixture final {
    using Intent = rhi::semantics::OrderedResourceReuseIntent;
    using Contract = rhi::semantics::OrderedResourceReuseContract;

    static void expect(const char* actual, const char* expected)
    {
        assert(actual && std::strcmp(actual, expected) == 0);
    }

public:
    static void verify()
    {
        constexpr std::array backends{rhi::Backend::Null,
            rhi::Backend::Vulkan,
            rhi::Backend::Metal,
            rhi::Backend::WebGPU,
            rhi::Backend::WebGL,
            static_cast<rhi::Backend>(255u)};
        constexpr std::array fields{&Intent::sameContext,
            &Intent::sameQueue,
            &Intent::allRecordedReferencesIssued,
            &Intent::cpuReadback,
            &Intent::externalSharing,
            &Intent::aliasHeap,
            &Intent::publishesGpuCompletion,
            &Intent::contextLost};
        for (auto backend : backends) {
            for (unsigned mask = 0u; mask < (1u << fields.size()); ++mask) {
                Intent intent;
                intent.contractVersion = 1u;
                intent.contextGeneration = intent.resourceContextGeneration = 1u;
                for (std::size_t index = 0u; index != fields.size(); ++index)
                    intent.*fields[index] = (mask & (1u << index)) != 0u;
                const auto decision = Contract::assess(backend, intent);
                const bool admissible = mask == 7u;
                const bool eligible =
                    admissible && (backend == rhi::Backend::Null || backend == rhi::Backend::WebGL);
                assert(decision.orderingCompatible == eligible);
                assert(decision.validatorOnly == (admissible && backend == rhi::Backend::Null));
                assert(!decision.executable && !decision.provesGpuCompletion);
                if (intent.contextLost)
                    expect(decision.diagnostic, "RHI-ORDERED-REUSE-STALE-CONTEXT");
                else if (!admissible)
                    expect(decision.diagnostic, "RHI-ORDERED-REUSE-INTENT-UNSUPPORTED");
                else if (backend == rhi::Backend::Null)
                    expect(decision.diagnostic, "RHI-ORDERED-REUSE-VALIDATOR-ONLY");
                else if (backend == rhi::Backend::WebGL)
                    expect(decision.diagnostic, "RHI-ORDERED-REUSE-NATIVE-CUSTODY-UNAVAILABLE");
                else if (backend == static_cast<rhi::Backend>(255u))
                    expect(decision.diagnostic, "RHI-ORDERED-REUSE-BACKEND-UNKNOWN");
                else
                    expect(decision.diagnostic, "RHI-ORDERED-REUSE-COMPLETION-PATH-REQUIRED");

                // Contract-version rejection wins even over lost/forbidden intent.
                for (auto version : {0u, 2u, std::numeric_limits<std::uint32_t>::max()}) {
                    intent.contractVersion = version;
                    const auto invalid = Contract::assess(backend, intent);
                    assert(!invalid.orderingCompatible && !invalid.validatorOnly
                        && !invalid.executable && !invalid.provesGpuCompletion);
                    expect(invalid.diagnostic, "RHI-ORDERED-REUSE-VERSION");
                }
            }
            Intent valid;
            valid.contractVersion = 1u;
            valid.sameContext = valid.sameQueue = valid.allRecordedReferencesIssued = true;
            for (auto generations :
                {std::array<std::uint64_t, 2u>{0u, 0u},
                    std::array<std::uint64_t, 2u>{0u, 1u},
                    std::array<std::uint64_t, 2u>{1u, 0u},
                    std::array<std::uint64_t, 2u>{1u, 2u},
                    std::array<std::uint64_t, 2u>{std::numeric_limits<std::uint64_t>::max(), 1u}}) {
                valid.contextGeneration = generations[0];
                valid.resourceContextGeneration = generations[1];
                const auto invalid = Contract::assess(backend, valid);
                assert(!invalid.orderingCompatible && !invalid.validatorOnly && !invalid.executable
                    && !invalid.provesGpuCompletion);
                expect(invalid.diagnostic, "RHI-ORDERED-REUSE-STALE-CONTEXT");
            }
        }
    }
};
} // namespace arp::test
