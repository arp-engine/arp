#include "rhi/backends/webgpu/webgpu_allocator.h"

#include <iostream>
#include <limits>
#include <string>
#include <utility>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

namespace arp::rhi_webgpu {

struct WebGpuRhiAllocationLedger {
    explicit WebGpuRhiAllocationLedger(std::uint64_t budgetBytes)
        : budget({budgetBytes, /*backendFallbacksHiddenFromUpperLayer=*/true})
    {}

    arp::core::MemoryBudgetLedger budget;
};

namespace {

bool allocationDiagnosticsEnabled()
{
#if defined(__EMSCRIPTEN__)
    return emscripten_run_script_int(
               "typeof window !== 'undefined' && window.location && "
               "new URLSearchParams(window.location.search).get('diag') === 'gpu-mem'")
        != 0;
#else
    return false;
#endif
}

class WebGpuRhiAllocationReleaseToken final {
public:
    WebGpuRhiAllocationReleaseToken(std::shared_ptr<WebGpuRhiAllocationLedger> ledger,
        std::uint64_t sizeBytes,
        std::pmr::string label)
        : ledger_(std::move(ledger)), sizeBytes_(sizeBytes), label_(std::move(label))
    {}

    ~WebGpuRhiAllocationReleaseToken()
    {
        if (!ledger_)
            return;
        ledger_->budget.releaseAllocation(sizeBytes_);
        if (allocationDiagnosticsEnabled()) {
            const auto stats = ledger_->budget.stats();
            std::cerr << "[webgpu-allocation] release label=" << label_.c_str()
                      << " bytes=" << sizeBytes_ << " live=" << stats.liveResourceCount << '\n';
        }
    }

private:
    std::shared_ptr<WebGpuRhiAllocationLedger> ledger_;
    std::uint64_t sizeBytes_ = 0;
    std::pmr::string label_;
};

WebGpuRhiMemoryPolicy policyFor(rhi::MemoryUsage usage)
{
    switch (usage) {
    case rhi::MemoryUsage::GpuOnly:
        return WebGpuRhiMemoryPolicy::DeviceResource;
    case rhi::MemoryUsage::CpuToGpu:
        return WebGpuRhiMemoryPolicy::UploadResource;
    case rhi::MemoryUsage::GpuToCpu:
        return WebGpuRhiMemoryPolicy::ReadbackResource;
    case rhi::MemoryUsage::Transient:
        return WebGpuRhiMemoryPolicy::TransientResource;
    }
    return WebGpuRhiMemoryPolicy::DeviceResource;
}

} // namespace

WebGpuRhiAllocatorReport describeWebGpuRhiAllocator(const WebGpuRhiAllocatorDesc& desc)
{
    WebGpuRhiAllocatorReport report;
    report.memoryIntentMapped = true;
    report.memoryBudgetTelemetrySanitized = desc.memoryBudgetBytes > 0;
    report.nativeAllocationExposedToUpperLayer = desc.exposeNativeAllocationsToUpperLayer;

    if (desc.exposeNativeAllocationsToUpperLayer) {
        report.diagnostics = "WebGPU RHI allocator must not expose native allocations upward";
        return report;
    }
    if (desc.memoryBudgetBytes == 0) {
        report.diagnostics = "WebGPU RHI allocator requires sanitized memory budget telemetry";
        return report;
    }

    report.success = true;
    return report;
}

WebGpuRhiAllocator::WebGpuRhiAllocator(WebGpuRhiAllocatorDesc desc)
    : desc_(std::move(desc)),
      ledger_(arp::core::makeShared<WebGpuRhiAllocationLedger>(desc_.memoryBudgetBytes))
{}

WebGpuRhiAllocation WebGpuRhiAllocator::allocate(const WebGpuRhiAllocationDesc& allocationDesc)
{
    WebGpuRhiAllocation allocation;
    allocation.policy = policyFor(allocationDesc.memory);
    allocation.sizeBytes = allocationDesc.sizeBytes;

    const auto report = describeWebGpuRhiAllocator(desc_);
    if (!report.success) {
        allocation.diagnostics = report.diagnostics;
        return allocation;
    }
    if (allocationDesc.sizeBytes == 0) {
        allocation.diagnostics = "WebGPU RHI allocation size must be greater than zero";
        return allocation;
    }
    if (!ledger_->budget.canAllocate(allocationDesc.sizeBytes)) {
        ledger_->budget.recordBudgetRejection();
        const auto stats = ledger_->budget.stats();
        allocation.diagnostics = "WebGPU RHI allocation exceeds sanitized memory budget: allocated="
            + std::to_string(stats.allocatedBytes)
            + " request=" + std::to_string(allocationDesc.sizeBytes)
            + " budget=" + std::to_string(stats.memoryBudgetBytes);
        return allocation;
    }

    allocation.releaseToken = arp::core::makeShared<WebGpuRhiAllocationReleaseToken>(
        ledger_, allocationDesc.sizeBytes, allocationDesc.label);
    // CPU owner allocation may fail. Publish accounting only after the exact
    // release token exists, so a failed cold reservation cannot leak budget.
    ledger_->budget.recordAllocation(allocationDesc.sizeBytes);
    if (allocationDiagnosticsEnabled()) {
        const auto stats = ledger_->budget.stats();
        std::cerr << "[webgpu-allocation] acquire label=" << allocationDesc.label.c_str()
                  << " bytes=" << allocationDesc.sizeBytes << " live=" << stats.liveResourceCount
                  << '\n';
    }
    allocation.success = true;
    return allocation;
}

rhi::MemoryStats WebGpuRhiAllocator::memoryStats() const
{
    const auto ledgerStats = ledger_->budget.stats();
    rhi::MemoryStats stats;
    stats.allocatedBytes = ledgerStats.allocatedBytes;
    stats.budgetCapacityBytes = ledgerStats.memoryBudgetBytes;
    stats.peakAllocatedBytes = ledgerStats.peakAllocatedBytes;
    stats.budgetRejectionCount = ledgerStats.budgetRejectionCount;
    stats.liveResourceCount = ledgerStats.liveResourceCount;
    stats.backendFallbacksHiddenFromUpperLayer = ledgerStats.backendFallbacksHiddenFromUpperLayer;
    return stats;
}

} // namespace arp::rhi_webgpu
