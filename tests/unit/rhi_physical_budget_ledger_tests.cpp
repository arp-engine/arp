#include "rhi/frontend/memory_direct_object_budget.h"
#include "rhi/frontend/memory_physical_budget_ledger.h"

#include <cassert>

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi::semantics;

    PhysicalBudgetLedgerReport report;
    report.backend = Backend::Null;
    assert(report.valid());
    assert(report.category(PhysicalBudgetCategory::UploadStaging).state
        == PhysicalBudgetObservationState::Unknown);
    assert(!report.category(PhysicalBudgetCategory::UploadStaging).observed());

    auto& allocation = report.category(PhysicalBudgetCategory::ResourceAllocation);
    allocation.state = PhysicalBudgetObservationState::Observed;
    allocation.unit = PhysicalBudgetUnit::Bytes;
    allocation.capacity = 1024u;
    allocation.current = 512u;
    allocation.peak = 768u;
    allocation.rejectionCount = 1u;
    allocation.liveCount = 2u;
    assert(report.valid());

    allocation.peak = 1025u;
    assert(!report.valid());
    allocation.peak = 768u;

    auto& readback = report.category(PhysicalBudgetCategory::TextureReadback);
    readback = unavailablePhysicalBudgetObservation(
        PhysicalBudgetUnit::Bytes, "ARP-RHI-READBACK-BOUNDED-MODE-UNSUPPORTED");
    assert(report.valid());

    allocation.chargeRelation = PhysicalBudgetChargeRelation::OverlapsResourceAllocation;
    assert(report.valid());

    // Unknown must not carry a zero-valued claim: all scalar values must be
    // absent until a backend-owned observer supplies an Observed report.
    auto& command = report.category(PhysicalBudgetCategory::Command);
    command.current = 1u;
    assert(!report.valid());
    command = {};
    assert(report.valid());

    DirectObjectBudget invalid;
    invalid.policyId = "DirectObjectBudgetTest";
    invalid.pipelineObjectCapacity = 1u;
    assert(!invalid.valid());

    const DirectObjectBudget budget{
        kDirectObjectBudgetContractVersion,
        "DirectObjectBudgetTest",
        2u,
        4u,
    };
    DirectObjectBudgetTracker tracker(budget);
    auto firstPipeline = tracker.reserve(DirectObjectBudgetKind::PipelineLayout);
    auto secondPipeline = tracker.reserve(DirectObjectBudgetKind::RenderPipeline);
    auto rejectedPipeline = tracker.reserve(DirectObjectBudgetKind::ComputePipeline);
    assert(firstPipeline.success && secondPipeline.success && !rejectedPipeline.success);
    auto descriptors = tracker.reserve(DirectObjectBudgetKind::DescriptorArena, 4u);
    auto rejectedDescriptors = tracker.reserve(DirectObjectBudgetKind::DescriptorArena, 1u);
    assert(descriptors.success && !rejectedDescriptors.success);
    const auto stats = tracker.stats();
    assert(stats.configured && stats.currentPipelineObjects == 2u && stats.peakPipelineObjects == 2u
        && stats.pipelineRejectionCount == 1u && stats.currentDescriptors == 4u
        && stats.peakDescriptors == 4u && stats.descriptorRejectionCount == 1u);
    firstPipeline.lease = {};
    assert(tracker.reserve(DirectObjectBudgetKind::ComputePipeline).success);
    return 0;
}
