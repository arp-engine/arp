#include "rhi/backends/metal/metal_allocator.h"

#include <limits>
#include <memory>
#include <utility>

namespace arp::rhi_metal {

class MetalRhiAllocationReleaseToken final {
public:
    MetalRhiAllocationReleaseToken(
        std::shared_ptr<arp::core::MemoryBudgetLedger> ledger, std::uint64_t sizeBytes) noexcept
        : ledger_(std::move(ledger)), sizeBytes_(sizeBytes)
    {}

    ~MetalRhiAllocationReleaseToken()
    {
        if (ledger_) {
            ledger_->releaseAllocation(sizeBytes_);
        }
    }

    bool shrink(const std::shared_ptr<core::MemoryBudgetLedger>& ledger,
        std::uint64_t previous,
        std::uint64_t actual) noexcept
    {
        if (ledger != ledger_ || previous != sizeBytes_
            || !ledger_->shrinkAllocation(previous, actual))
            return false;
        sizeBytes_ = actual;
        return true;
    }

private:
    std::shared_ptr<arp::core::MemoryBudgetLedger> ledger_;
    std::uint64_t sizeBytes_ = 0;
};

MetalRhiMemoryPolicy metalRhiMemoryPolicyFor(rhi::MemoryUsage usage) noexcept
{
    switch (usage) {
    case rhi::MemoryUsage::GpuOnly:
        return MetalRhiMemoryPolicy::PrivateStorage;
    case rhi::MemoryUsage::CpuToGpu:
        return MetalRhiMemoryPolicy::SharedUpload;
    case rhi::MemoryUsage::GpuToCpu:
        return MetalRhiMemoryPolicy::ManagedReadback;
    case rhi::MemoryUsage::Transient:
        return MetalRhiMemoryPolicy::MemorylessTransient;
    }
    return MetalRhiMemoryPolicy::PrivateStorage;
}

MetalRhiAllocatorReport describeMetalRhiAllocator(const MetalRhiAllocatorDesc& desc)
{
    MetalRhiAllocatorReport report;
    report.memoryIntentMapped = true;
    report.memoryBudgetTelemetrySanitized = desc.memoryBudgetBytes > 0;
    report.memorylessTransientSupported = desc.enableMemorylessTransientTextures;
    report.nativeAllocationExposedToUpperLayer = desc.exposeNativeAllocationsToUpperLayer;

    if (desc.exposeNativeAllocationsToUpperLayer) {
        report.diagnostics = "Metal RHI allocator must not expose native allocations upward";
        return report;
    }
    if (desc.memoryBudgetBytes == 0) {
        report.diagnostics = "Metal RHI allocator requires sanitized memory budget telemetry";
        return report;
    }
    if (!desc.enableMemorylessTransientTextures) {
        report.diagnostics = "Metal RHI allocator requires memoryless transient texture support";
        return report;
    }

    report.success = true;
    return report;
}

MetalRhiAllocator::MetalRhiAllocator(MetalRhiAllocatorDesc desc)
    : desc_(std::move(desc)), ledger_(arp::core::makeShared<arp::core::MemoryBudgetLedger>(
                                  arp::core::MemoryBudgetLedgerDesc{
                                      desc_.memoryBudgetBytes,
                                      /*backendFallbacksHiddenFromUpperLayer=*/true,
                                  }))
{}

MetalRhiAllocation MetalRhiAllocator::allocate(const MetalRhiAllocationDesc& allocationDesc)
{
    MetalRhiAllocation allocation;
    allocation.policy = metalRhiMemoryPolicyFor(allocationDesc.memory);
    allocation.sizeBytes = allocationDesc.sizeBytes;

    const auto report = describeMetalRhiAllocator(desc_);
    if (!report.success) {
        allocation.diagnostics = report.diagnostics;
        return allocation;
    }
    if (allocationDesc.sizeBytes == 0) {
        allocation.diagnostics = "Metal RHI allocation size must be greater than zero";
        return allocation;
    }
    if (!ledger_->canAllocate(allocationDesc.sizeBytes)) {
        ledger_->recordBudgetRejection();
        allocation.diagnostics = "Metal RHI allocation exceeds sanitized memory budget";
        return allocation;
    }

    // Token construction can fail under CPU pressure. Publish the charge only
    // after custody exists; the remaining ledger update and moves cannot throw.
    auto token =
        arp::core::makeShared<MetalRhiAllocationReleaseToken>(ledger_, allocationDesc.sizeBytes);
    ledger_->recordAllocation(allocationDesc.sizeBytes);
    allocation.releaseToken = std::move(token);
    allocation.success = true;
    return allocation;
}

void MetalRhiAllocator::release(std::uint64_t sizeBytes) noexcept
{
    ledger_->releaseAllocation(sizeBytes);
}

bool MetalRhiAllocator::shrinkReservation(
    MetalRhiAllocation& allocation, std::uint64_t actualBytes) noexcept
{
    if (!allocation.success || !allocation.releaseToken
        || !allocation.releaseToken->shrink(ledger_, allocation.sizeBytes, actualBytes))
        return false;
    allocation.sizeBytes = actualBytes;
    return true;
}

rhi::MemoryStats MetalRhiAllocator::memoryStats() const
{
    const auto ledgerStats = ledger_->stats();
    rhi::MemoryStats stats;
    stats.allocatedBytes = ledgerStats.allocatedBytes;
    stats.budgetCapacityBytes = ledgerStats.memoryBudgetBytes;
    stats.peakAllocatedBytes = ledgerStats.peakAllocatedBytes;
    stats.budgetRejectionCount = ledgerStats.budgetRejectionCount;
    stats.liveResourceCount = ledgerStats.liveResourceCount;
    stats.backendFallbacksHiddenFromUpperLayer = ledgerStats.backendFallbacksHiddenFromUpperLayer;
    return stats;
}

} // namespace arp::rhi_metal
