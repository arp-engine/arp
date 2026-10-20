#pragma once
#include "rhi/frontend/finite_graphics_content_validation.h"

#include <algorithm>
#include <cassert>

namespace arp::test {
// Cold fixture ceiling for the existing mixed Graphics content inspector, one
// attachment cell and S completed inputs. Execution still narrows to its actual
// counts and tests one-less rejection. No L2 or device budget is inferred.
class OriginalGraphicsContentVisitBudgetFixture final {
public:
    static void configure(rhi::OriginalFiniteGraphicsContentPlanBudget& budget,
        std::uint64_t commands,
        std::uint64_t entries,
        std::uint64_t sampled,
        std::uint64_t colorAttachments)
    {
        assert(commands <= UINT32_MAX && entries <= UINT32_MAX && sampled <= UINT32_MAX
            && colorAttachments <= UINT32_MAX);
        // Read-order per input, plus attachment order/timeline walks.
        const auto commandWalks = sampled + 3u;
        assert(commands <= UINT64_MAX / commandWalks);
        budget.maxCommandVisits = std::max(budget.maxCommandVisits, commands * commandWalks);
        // Mixed and selected inspection each charge one aggregate triangular
        // duplicate scan. Classification and selected scans cost 2*N*E;
        // sampled reads S*E; the attachment timeline C*E. Classification and
        // begin/end scan at most three color lists per matching entry.
        const auto perEntry = (entries ? entries - 1u : 0u) + 1u + 2u * (sampled + 1u) + sampled
            + commands + 3u * colorAttachments;
        assert(!entries || perEntry <= UINT64_MAX / entries);
        budget.maxProjectionVisits = std::max(budget.maxProjectionVisits, entries * perEntry);
    }
};
} // namespace arp::test
