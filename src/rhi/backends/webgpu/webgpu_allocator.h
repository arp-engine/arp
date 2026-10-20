#pragma once

#include "core/cpu_allocator.h"
#include "core/memory_budget_ledger.h"
#include "rhi/gpu_resources.h"

#include <cstdint>
#include <memory>
#include <string>

namespace arp::rhi_webgpu {

enum class WebGpuRhiMemoryPolicy {
    DeviceResource,
    UploadResource,
    ReadbackResource,
    TransientResource
};

struct WebGpuRhiAllocatorDesc {
    std::pmr::string label;
    std::uint64_t memoryBudgetBytes = 0;
    bool exposeNativeAllocationsToUpperLayer = false;
};

struct WebGpuRhiAllocatorReport {
    bool success = false;
    std::pmr::string diagnostics;
    bool memoryIntentMapped = false;
    bool memoryBudgetTelemetrySanitized = false;
    bool nativeAllocationExposedToUpperLayer = false;
};

struct WebGpuRhiAllocationDesc {
    std::pmr::string label;
    std::uint64_t sizeBytes = 0;
    rhi::MemoryUsage memory = rhi::MemoryUsage::GpuOnly;
};

struct WebGpuRhiAllocation {
    bool success = false;
    std::pmr::string diagnostics;
    WebGpuRhiMemoryPolicy policy = WebGpuRhiMemoryPolicy::DeviceResource;
    std::uint64_t sizeBytes = 0;
    bool nativeAllocationExposedToUpperLayer = false;
    std::shared_ptr<void> releaseToken;
};

struct WebGpuRhiAllocationLedger;

WebGpuRhiAllocatorReport describeWebGpuRhiAllocator(const WebGpuRhiAllocatorDesc& desc);

class WebGpuRhiAllocator {
public:
    explicit WebGpuRhiAllocator(WebGpuRhiAllocatorDesc desc);

    WebGpuRhiAllocation allocate(const WebGpuRhiAllocationDesc& desc);
    rhi::MemoryStats memoryStats() const;

private:
    WebGpuRhiAllocatorDesc desc_;
    std::shared_ptr<WebGpuRhiAllocationLedger> ledger_;
};

} // namespace arp::rhi_webgpu
