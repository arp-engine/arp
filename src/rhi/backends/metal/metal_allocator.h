#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/frontend/memory_budget_ledger.h"
#include "rhi/gpu_resources.h"

#include <cstdint>
#include <memory>
#include <string>

namespace arp::rhi_metal {
class MetalRhiAllocationReleaseToken;

enum class MetalRhiMemoryPolicy {
    PrivateStorage,
    SharedUpload,
    ManagedReadback,
    MemorylessTransient
};

MetalRhiMemoryPolicy metalRhiMemoryPolicyFor(rhi::MemoryUsage) noexcept;

struct MetalRhiAllocatorDesc {
    std::pmr::string label;
    std::uint64_t memoryBudgetBytes = 0;
    bool enableMemorylessTransientTextures = true;
    bool exposeNativeAllocationsToUpperLayer = false;
};

struct MetalRhiAllocatorReport {
    bool success = false;
    std::pmr::string diagnostics;
    bool memoryIntentMapped = false;
    bool memoryBudgetTelemetrySanitized = false;
    bool memorylessTransientSupported = false;
    bool nativeAllocationExposedToUpperLayer = false;
};

struct MetalRhiAllocationDesc {
    std::pmr::string label;
    std::uint64_t sizeBytes = 0;
    rhi::MemoryUsage memory = rhi::MemoryUsage::GpuOnly;
};

struct MetalRhiAllocation {
    bool success = false;
    std::pmr::string diagnostics;
    MetalRhiMemoryPolicy policy = MetalRhiMemoryPolicy::PrivateStorage;
    std::uint64_t sizeBytes = 0;
    bool nativeAllocationExposedToUpperLayer = false;
    // Kept backend-private through an opaque shared pointer.  Resources own
    // this token so the budget ledger follows the resource lifetime instead
    // of the allocation call site.
    std::shared_ptr<MetalRhiAllocationReleaseToken> releaseToken;
};

MetalRhiAllocatorReport describeMetalRhiAllocator(const MetalRhiAllocatorDesc& desc);

class MetalRhiAllocator {
public:
    explicit MetalRhiAllocator(MetalRhiAllocatorDesc desc);

    MetalRhiAllocation allocate(const MetalRhiAllocationDesc& desc);
    bool shrinkReservation(MetalRhiAllocation&, std::uint64_t actualBytes) noexcept;
    void release(std::uint64_t sizeBytes) noexcept;
    rhi::MemoryStats memoryStats() const;

private:
    MetalRhiAllocatorDesc desc_;
    std::shared_ptr<arp::core::MemoryBudgetLedger> ledger_;
};

} // namespace arp::rhi_metal
