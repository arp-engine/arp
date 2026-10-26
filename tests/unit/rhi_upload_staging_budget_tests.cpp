#include "rhi/frontend/memory_upload_staging_budget.h"

#include <cassert>

namespace {

using arp::rhi::semantics::UploadStagingBudgetV1;
using arp::rhi::semantics::validateUploadStagingBudget;

UploadStagingBudgetV1 validBudget()
{
    return {
        1u,
        16777216u,
        16777216u,
        16u,
        268435456u,
        256u,
    };
}

void assertValidBudget()
{
    const auto result = validateUploadStagingBudget(validBudget());
    assert(result.success);
    assert(result.diagnostics == nullptr);
    assert(result.diagnosticId == nullptr);
}

void assertRejectsInvalidBudget()
{
    auto budget = validBudget();
    budget.version = 2u;
    auto result = validateUploadStagingBudget(budget);
    assert(!result.success);
    assert(result.diagnosticId == arp::rhi::semantics::kUploadStagingBudgetDiagnosticVersion);

    budget = validBudget();
    budget.maxRequestBytes = 0u;
    result = validateUploadStagingBudget(budget);
    assert(!result.success);
    assert(result.diagnosticId == arp::rhi::semantics::kUploadStagingBudgetDiagnosticRequest);

    budget = validBudget();
    budget.pageSizeBytes = 4194304u;
    result = validateUploadStagingBudget(budget);
    assert(!result.success);
    assert(result.diagnosticId == arp::rhi::semantics::kUploadStagingBudgetDiagnosticPage);

    budget = validBudget();
    budget.totalCapacityBytes -= 1u;
    result = validateUploadStagingBudget(budget);
    assert(!result.success);
    assert(result.diagnosticId == arp::rhi::semantics::kUploadStagingBudgetDiagnosticCapacity);

    budget = validBudget();
    budget.alignmentBytes = 3u;
    result = validateUploadStagingBudget(budget);
    assert(!result.success);
    assert(result.diagnosticId == arp::rhi::semantics::kUploadStagingBudgetDiagnosticAlignment);
}

void assertBudgetReduction()
{
    using namespace arp::rhi::semantics;
    const auto ceiling = validBudget();
    constexpr UploadStagingBudgetV1 workload{
        1u, 1024u * 1024u, 1024u * 1024u, 3u, 3u * 1024u * 1024u, 256u};
    static_assert(validateUploadStagingBudgetReduction(
        workload, {1u, 16777216u, 16777216u, 16u, 268435456u, 256u})
            .success);
    assert(validateUploadStagingBudgetReduction(ceiling, ceiling).success);
    assert(validateUploadStagingBudgetReduction(workload, ceiling).success);
    for (unsigned mutation = 0u; mutation < 7u; ++mutation) {
        auto candidate = workload;
        if (mutation == 0u) {
            candidate.maxRequestBytes = ceiling.maxRequestBytes + 1u;
            candidate.pageSizeBytes = candidate.maxRequestBytes;
        }
        if (mutation == 1u)
            candidate.pageSizeBytes = ceiling.pageSizeBytes + 1u;
        if (mutation == 2u)
            candidate.maxPageCount = ceiling.maxPageCount + 1u;
        if (mutation == 3u)
            candidate.alignmentBytes /= 2u;
        if (mutation == 4u)
            candidate.alignmentBytes *= 2u;
        candidate.totalCapacityBytes = candidate.pageSizeBytes * candidate.maxPageCount;
        if (mutation == 5u)
            candidate.version = 0u;
        if (mutation == 6u)
            --candidate.totalCapacityBytes;
        const auto result = validateUploadStagingBudgetReduction(candidate, ceiling);
        assert(!result.success);
        assert(result.diagnosticId
            == (mutation < 5u        ? kUploadStagingBudgetDiagnosticReduction
                    : mutation == 5u ? kUploadStagingBudgetDiagnosticVersion
                                     : kUploadStagingBudgetDiagnosticCapacity));
    }
    auto invalidCeiling = ceiling;
    invalidCeiling.maxPageCount = 0u;
    assert(validateUploadStagingBudgetReduction(workload, invalidCeiling).diagnosticId
        == kUploadStagingBudgetDiagnosticPageCount);
    auto malformed = workload;
    malformed.pageSizeBytes = std::numeric_limits<std::uint64_t>::max();
    assert(validateUploadStagingBudgetReduction(malformed, ceiling).diagnosticId
        == kUploadStagingBudgetDiagnosticCapacity);
    malformed = workload;
    malformed.maxPageCount = 0u;
    assert(validateUploadStagingBudgetReduction(malformed, ceiling).diagnosticId
        == kUploadStagingBudgetDiagnosticPageCount);
    auto independentRequest = workload;
    independentRequest.maxRequestBytes /= 2u;
    // Numerically valid; Metal's page-only representation rejects this
    // separately. The shared validator does not restrict Vulkan's policy.
    assert(validateUploadStagingBudgetReduction(independentRequest, ceiling).success);
}

} // namespace

int main()
{
    assertValidBudget();
    assertRejectsInvalidBudget();
    assertBudgetReduction();
    return 0;
}
