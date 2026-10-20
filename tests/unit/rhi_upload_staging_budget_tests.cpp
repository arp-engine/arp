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

} // namespace

int main()
{
    assertValidBudget();
    assertRejectsInvalidBudget();
    return 0;
}
