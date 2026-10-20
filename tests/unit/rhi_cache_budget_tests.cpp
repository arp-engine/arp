#include "rhi/backends/null/null_backend.h"
#include "rhi/gpu_descriptors.h"
#include "rhi/gpu_device.h"
#include "rhi/gpu_pipeline.h"

#include <cassert>

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi::semantics;

    auto device = createNullDevice();
    auto& pipelines = device->pipelineCache();
    assert(!pipelines.configureBudget({kCacheBudgetContractVersion, "", 1u}));
    assert(
        pipelines.lastBudgetDiagnostic() == std::string_view("RHI-PIPELINE-CACHE-BUDGET-INVALID"));
    assert(
        pipelines.configureBudget({kCacheBudgetContractVersion, "CacheBudgetTest/Pipeline", 1u}));

    PipelineLayoutDesc firstLayout;
    firstLayout.pushConstantRanges.push_back({ShaderStage::Vertex, 0u, 4u});
    assert(pipelines.acquirePipelineLayout(firstLayout));
    PipelineLayoutDesc secondLayout;
    secondLayout.pushConstantRanges.push_back({ShaderStage::Fragment, 0u, 4u});
    assert(!pipelines.acquirePipelineLayout(secondLayout));
    const auto pipelineStats = pipelines.budgetStats();
    assert(pipelineStats.configured && pipelineStats.capacity == 1u && pipelineStats.current == 1u
        && pipelineStats.peak == 1u && pipelineStats.rejectionCount == 1u);
    pipelines.evictStale(2u, 1u);
    assert(pipelines.entryCount() == 0u);
    assert(pipelines.acquirePipelineLayout(secondLayout));

    auto& descriptors = device->descriptorCache();
    assert(descriptors.configureBudget(
        {kCacheBudgetContractVersion, "CacheBudgetTest/Descriptor", 4u}));
    DescriptorArenaDesc exact{"Exact", 1u, 4u};
    assert(descriptors.acquireDescriptorArena(exact));
    DescriptorArenaDesc oneShort{"OneShort", 1u, 1u};
    assert(!descriptors.acquireDescriptorArena(oneShort));
    const auto descriptorStats = descriptors.budgetStats();
    assert(descriptorStats.configured && descriptorStats.capacity == 4u
        && descriptorStats.current == 4u && descriptorStats.peak == 4u
        && descriptorStats.rejectionCount == 1u);
    descriptors.evictStale(2u, 1u);
    assert(descriptors.budgetStats().current == 0u);
    assert(descriptors.acquireDescriptorArena(oneShort));

    const auto ledger = device->physicalBudgetLedger();
    assert(ledger.valid());
    const auto& descriptorBudget = ledger.category(PhysicalBudgetCategory::Descriptor);
    assert(descriptorBudget.observed() && descriptorBudget.unit == PhysicalBudgetUnit::Descriptors
        && descriptorBudget.capacity == 4u && descriptorBudget.current == 1u);
    const auto& pipelineBudget = ledger.category(PhysicalBudgetCategory::PipelineCache);
    assert(pipelineBudget.observed() && pipelineBudget.unit == PhysicalBudgetUnit::PipelineEntries
        && pipelineBudget.capacity == 1u && pipelineBudget.current == 1u);

    // CPU ownership teardown is independent of frame/completion serials. The
    // original configuration and high-water marks survive an idempotent clear.
    const auto pipelinePeak = pipelines.budgetStats().peak;
    const auto descriptorPeak = descriptors.budgetStats().peak;
    DescriptorSetDesc setDesc;
    setDesc.layout.name = "CacheShutdown";
    setDesc.layout.bindings.push_back({0, DescriptorType::UniformBuffer, ShaderStage::Vertex});
    auto* arena = descriptors.acquireDescriptorArena(oneShort);
    assert(arena && descriptors.acquireDescriptorSet(setDesc, arena));
    assert(descriptors.entryCount() == 2u);
    descriptors.clearForShutdown();
    pipelines.clearForShutdown();
    descriptors.clearForShutdown();
    pipelines.clearForShutdown();
    assert(!descriptors.entryCount() && !descriptors.budgetStats().current);
    assert(!pipelines.entryCount() && !pipelines.budgetStats().current);
    assert(descriptors.budgetStats().configured && pipelines.budgetStats().configured);
    assert(descriptors.budgetStats().peak == descriptorPeak
        && pipelines.budgetStats().peak == pipelinePeak);
}
